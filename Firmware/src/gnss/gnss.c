/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#include <lifegps/board_io.h>
#include <lifegps/diag.h>
#include <lifegps/gnss.h>
#include <lifegps/time.h>
#include <lifegps/ubx.h>
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_gnss, LOG_LEVEL_INF);

#define GNSS_THREAD_STACK 2048
#define GNSS_THREAD_PRIORITY 6
#define GNSS_RX_RING_SIZE 512
#define GNSS_PVT_LOG_PERIOD_MS 5000U

/* UBX-CFG-MSG (0x06 0x01) payload: msgClass, msgID, rate (interface description). */
#define UBX_CFG_MSG_CLASS 0x06U
#define UBX_CFG_MSG_ID 0x01U
#define UBX_RATE_ON 0x01U

static const struct device *gnss_uart = DEVICE_DT_GET(DT_NODELABEL(uart20));

RING_BUF_DECLARE(gnss_rx_ring, GNSS_RX_RING_SIZE);

static struct lgps_ubx_parser parser;
static struct lgps_gnss_fix current_fix;
static enum lgps_gnss_state gnss_state = LGPS_GNSS_STATE_OFF;
static uint32_t state_since_ms;
static uint32_t last_pvt_log_ms;
static uint32_t next_reconfig_ms;
static uint32_t current_baud = LGPS_GNSS_BAUD_DEFAULT;
static bool baud_fallback_done;

/*
 * Receive-path counters, split by the thread that owns them.
 *
 * The ISR is the only writer of the bottom three, so atomic_t both removes the
 * torn read a 64-bit or non-atomic counter would allow and keeps the ISR free of
 * locking. The two UBX counters are touched only by the GNSS thread.
 */
static atomic_t isr_rx_bytes;
static atomic_t isr_nmea_starts;
static atomic_t isr_rx_dropped;
static uint32_t ubx_frames;
static uint32_t ubx_bad_checksum;

/* rx_bytes seen when the current baud-probe window started. */
static uint32_t baud_probe_base_bytes;

/* Given by lgps_gnss_init() only after the UART and its IRQ handler are live,
 * so the state machine can never power the rail before the receive path exists.
 */
static K_SEM_DEFINE(gnss_ready_sem, 0, 1);

static const char *const state_names[] = {
	[LGPS_GNSS_STATE_OFF] = "OFF",
	[LGPS_GNSS_STATE_COLD_START] = "COLD_START",
	[LGPS_GNSS_STATE_FIXED] = "FIXED",
	[LGPS_GNSS_STATE_EPHEMERIS_HOLD] = "EPHEMERIS_HOLD",
	[LGPS_GNSS_STATE_BACKOFF] = "BACKOFF",
};

void lgps_gnss_get_counters(struct lgps_gnss_counters *out)
{
	if (out == NULL) {
		return;
	}

	out->rx_bytes = (uint32_t)atomic_get(&isr_rx_bytes);
	out->ubx_frames = ubx_frames;
	out->ubx_bad_checksum = ubx_bad_checksum;
	out->nmea_starts = (uint32_t)atomic_get(&isr_nmea_starts);
}

static uint32_t rx_bytes_total(void)
{
	return (uint32_t)atomic_get(&isr_rx_bytes);
}

const char *lgps_gnss_state_name(enum lgps_gnss_state state)
{
	if ((unsigned int)state >= ARRAY_SIZE(state_names)) {
		return "?";
	}

	return state_names[state];
}

static void set_state(enum lgps_gnss_state next, uint32_t duration_ms)
{
	LOG_INF("GNSS state %s -> %s (duration %u ms)", lgps_gnss_state_name(gnss_state),
		lgps_gnss_state_name(next), duration_ms);
	gnss_state = next;
	state_since_ms = lgps_diag_uptime_ms();
}

void lgps_gnss_get_fix(struct lgps_gnss_fix *out)
{
	if (out == NULL) {
		return;
	}

	*out = current_fix;
}

enum lgps_gnss_state lgps_gnss_get_state(void)
{
	return gnss_state;
}

static void gnss_uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	/*
	 * uart_irq_update() is a three-valued return: 1 = this interrupt belongs to
	 * the UART, 0 = spurious, negative = the driver has no interrupt support
	 * (-ENOTSUP / -ENOSYS). Treating it as a boolean spun on the error case.
	 */
	if (uart_irq_update(dev) != 1) {
		return;
	}

	if (uart_irq_rx_ready(dev) != 1) {
		return;
	}

	/*
	 * uart_fifo_read() is a drain loop, not a one-shot read: the driver returns at
	 * most what the FIFO holds right now, and the documented contract is to call it
	 * until it returns 0. A single fixed-size read per interrupt leaves bytes in
	 * the FIFO while the receiver keeps clocking new ones in.
	 *
	 * NOTHING in this function may log: CONFIG_LOG_MODE_IMMEDIATE sends every
	 * character through the UARTE30 poll_out path while holding the log lock
	 * (log_output.c), which costs several milliseconds - exactly when the GNSS
	 * FIFO is overflowing. Drops are counted here and reported by the thread.
	 */
	for (;;) {
		uint8_t chunk[32];
		uint32_t stored;
		int read;

		read = uart_fifo_read(dev, chunk, sizeof(chunk));
		if (read <= 0) {
			break;
		}

		atomic_add(&isr_rx_bytes, (atomic_val_t)read);

		for (int i = 0; i < read; i++) {
			if (chunk[i] == (uint8_t)'$') {
				atomic_inc(&isr_nmea_starts);
			}
		}

		/* Drop on overflow; the parser resynchronises on the next sync pair. */
		stored = ring_buf_put(&gnss_rx_ring, chunk, (uint32_t)read);
		if (stored < (uint32_t)read) {
			atomic_add(&isr_rx_dropped, (atomic_val_t)((uint32_t)read - stored));
		}
	}
}

static int gnss_uart_set_baud(uint32_t baud)
{
	struct uart_config cfg;
	int ret;

	if (baud == current_baud) {
		return 0;
	}

	ret = uart_config_get(gnss_uart, &cfg);
	if (ret < 0) {
		LOG_ERR("uart_config_get failed (%d)", ret);
		return ret;
	}

	cfg.baudrate = baud;

	ret = uart_configure(gnss_uart, &cfg);
	if (ret < 0) {
		LOG_ERR("uart_configure(%u) failed (%d)", baud, ret);
		return ret;
	}

	current_baud = baud;
	LOG_INF("GNSS UART baud -> %u 8N1", baud);

	return 0;
}

static int gnss_uart_send(const uint8_t *data, size_t len_bytes)
{
	for (size_t i = 0U; i < len_bytes; i++) {
		uart_poll_out(gnss_uart, data[i]);
	}

	return 0;
}

static int gnss_configure_output(void)
{
	/* Keep the receiver's default 1 Hz navigation rate and only switch on
	 * UBX-NAV-PVT, which carries everything the trajectory record needs.
	 */
	static const uint8_t payload[3] = { LGPS_UBX_CLASS_NAV, LGPS_UBX_ID_NAV_PVT, UBX_RATE_ON };
	uint8_t frame[LGPS_UBX_HEADER_LEN + 3U + LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build(frame, sizeof(frame), UBX_CFG_MSG_CLASS, UBX_CFG_MSG_ID, payload,
			     sizeof(payload));
	if (ret < 0) {
		LOG_ERR("UBX-CFG-MSG build failed (%d)", ret);
		return ret;
	}

	int len = ret;

	ret = gnss_uart_send(frame, (size_t)len);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("sent UBX-CFG-MSG: NAV-PVT on port 1 at 1 Hz (%d bytes)", len);

	return 0;
}

static void handle_nav_pvt(const struct lgps_nav_pvt *pvt)
{
	struct lgps_utc utc;
	/*
	 * fixType: 0 = no fix, 1 = dead reckoning only, 2 = 2D, 3 = 3D,
	 * 4 = GNSS + dead reckoning, 5 = time only (no position at all!). Only 3 and 4
	 * carry a usable lat/lon/hMSL. "fixType >= 3" also accepted 5 and locked a
	 * garbage position that the receiver explicitly flagged as time-only.
	 */
	bool fix_valid = ((pvt->fix_type == LGPS_UBX_FIX_3D) ||
			  (pvt->fix_type == LGPS_UBX_FIX_GNSS_DR)) &&
			 ((pvt->flags & LGPS_UBX_PVT_FLAGS_FIX_OK) != 0U);
	bool utc_ok = false;
	uint32_t epoch_s = 0U;
	uint32_t now_ms = lgps_diag_uptime_ms();

	if ((pvt->valid & LGPS_UBX_PVT_VALID_DATE) != 0U &&
	    (pvt->valid & LGPS_UBX_PVT_VALID_TIME) != 0U) {
		utc.year = pvt->year;
		utc.month = pvt->month;
		utc.day = pvt->day;
		utc.hour = pvt->hour;
		utc.minute = pvt->minute;
		utc.second = pvt->second;

		if (lgps_time_from_utc(&utc, &epoch_s) == 0) {
			utc_ok = true;
		}
	}

	current_fix.valid = fix_valid;
	current_fix.utc_valid = utc_ok;
	current_fix.fix_type = pvt->fix_type;
	current_fix.satellites = pvt->num_sv;
	current_fix.pdop_1e2 = pvt->pdop_1e2;
	current_fix.lat_e7 = pvt->lat_e7;
	current_fix.lon_e7 = pvt->lon_e7;
	current_fix.hmsl_mm = pvt->hmsl_mm;
	current_fix.speed_cms = (uint16_t)(pvt->gspeed_mms / 10);
	/*
	 * headMot is 1e-5 deg in [-180, +180]; heading_ddeg wants 1e-2 deg in
	 * [0, 360). Dividing by 1000 is the unit change (1e-5 -> 1e-2), and the offset
	 * that shifts a signed course into a bearing is 180 deg = 18000000 units of
	 * 1e-5 deg. The previous code added 1800000, i.e. 18 deg, which rotated every
	 * heading by 18 degrees short of north.
	 */
	current_fix.heading_ddeg = (uint16_t)((pvt->head_mot_1e5 + 18000000) / 1000);
	if (current_fix.heading_ddeg >= 3600U) {
		current_fix.heading_ddeg = 0U;
	}
	current_fix.utc_epoch_s = epoch_s;
	current_fix.last_pvt_uptime_ms = now_ms;

	/* The cold-start window must end on the first real 3D fix. */
	if (fix_valid && gnss_state == LGPS_GNSS_STATE_COLD_START) {
		set_state(LGPS_GNSS_STATE_FIXED, 0U);
	}

	/* 1 Hz NAV-PVT is logged at a lower rate to keep the UART usable. */
	if ((uint32_t)(now_ms - last_pvt_log_ms) >= GNSS_PVT_LOG_PERIOD_MS) {
		last_pvt_log_ms = now_ms;
		LOG_INF("NAV-PVT fix=%u sats=%u pdop=%.2f lat=%.7f lon=%.7f hmsl=%d m "
			"gspd=%u m/s utc=%s",
			pvt->fix_type, pvt->num_sv, (double)pvt->pdop_1e2 / 100.0,
			(double)pvt->lat_e7 / 1e7, (double)pvt->lon_e7 / 1e7,
			pvt->hmsl_mm / 1000, (unsigned int)(pvt->gspeed_mms / 1000),
			utc_ok ? "valid" : "pending");
	}
}

static void gnss_process_rx(void)
{
	uint8_t byte;

	while (ring_buf_get(&gnss_rx_ring, &byte, 1) == 1U) {
		enum lgps_ubx_status status = lgps_ubx_parser_feed(&parser, byte);

		if (status == LGPS_UBX_STATUS_BAD_CHECKSUM) {
			ubx_bad_checksum++;
			LOG_WRN("UBX checksum error, frame dropped (total %u)", ubx_bad_checksum);
			continue;
		}

		if (status != LGPS_UBX_STATUS_MSG_READY) {
			continue;
		}

		ubx_frames++;

		if (parser.msg_class == LGPS_UBX_CLASS_NAV && parser.msg_id == LGPS_UBX_ID_NAV_PVT) {
			struct lgps_nav_pvt pvt;

			if (lgps_ubx_decode_nav_pvt(&parser, &pvt) == 0) {
				handle_nav_pvt(&pvt);
			}
		} else if (parser.msg_class == LGPS_UBX_CLASS_ACK) {
			LOG_DBG("UBX-ACK id 0x%02x for class 0x%02x", parser.msg_id,
				parser.payload_len > 0U ? parser.payload[0] : 0U);
		}
	}
}

static void gnss_power_down(void)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN + LGPS_UBX_TRAILER_LEN];
	int ret;

	/* 1. Ask the receiver to enter software backup (flags: backup | force). */
	ret = lgps_ubx_build_rxm_pmreq(frame, sizeof(frame), 0U, true, true, 0U);
	if (ret < 0) {
		LOG_ERR("UBX-RXM-PMREQ build failed (%d)", ret);
	} else {
		(void)gnss_uart_send(frame, (size_t)ret);
		LOG_INF("sent UBX-RXM-PMREQ backup (16-byte payload, duration 0)");
	}

	k_msleep(100);

	/* 2. Stop the receive interrupts, then 3. drop the rail (pinmap power-down
	 * order).
	 *
	 * uart_irq_rx_disable() returns void (only update/rx_ready/tx_ready/
	 * is_pending report errors), so there is no status to check here. It also only
	 * masks the RX interrupt - it does NOT tri-state the pins. Nothing needs to:
	 * the receiver is unpowered after step 3, so no current can flow into its RX/TX
	 * through the 100 ohm series resistors.
	 */
	uart_irq_rx_disable(gnss_uart);

	(void)lgps_gnss_rail_set(false);
}

static void gnss_power_up(void)
{
	int ret;

	ret = lgps_gnss_rail_set(true);
	if (ret < 0) {
		LOG_ERR("GNSS rail on failed (%d)", ret);
		return;
	}

	/* Module boot time before its UART answers: 100 ms is the value used by
	 * the bring-up procedure until the first PPK2/scope measurement exists.
	 */
	k_msleep(100);

	/* Drop whatever the previous power cycle left in the ring: replaying stale
	 * bytes re-enters the parser mid-frame and shifts the first CFG-MSG / NAV-PVT.
	 */
	ring_buf_reset(&gnss_rx_ring);
	lgps_ubx_parser_reset(&parser);

	/*
	 * uart_irq_rx_enable() returns void: a driver that cannot do interrupt-driven
	 * RX is caught earlier, by uart_irq_callback_user_data_set() returning
	 * -ENOTSUP in lgps_gnss_init(). A receive path that is silently dead still
	 * shows up in the counters logged below (rx_bytes stays 0).
	 */
	uart_irq_rx_enable(gnss_uart);

	next_reconfig_ms = 0U;
	baud_probe_base_bytes = rx_bytes_total();
	(void)gnss_configure_output();

	LOG_INF("GNSS RX counters: rx_bytes=%u ubx=%u ckerr=%u nmea=%u ring_drop=%u baud=%u",
		rx_bytes_total(), ubx_frames, ubx_bad_checksum,
		(uint32_t)atomic_get(&isr_nmea_starts), (uint32_t)atomic_get(&isr_rx_dropped),
		current_baud);
}

static uint32_t backoff_duration_ms(enum lgps_gnss_state previous, uint32_t previous_ms)
{
	if (previous_ms < LGPS_GNSS_BACKOFF_STEP1_MS) {
		return LGPS_GNSS_BACKOFF_STEP1_MS;
	}

	if (previous_ms < LGPS_GNSS_BACKOFF_STEP2_MS) {
		return LGPS_GNSS_BACKOFF_STEP2_MS;
	}

	ARG_UNUSED(previous);
	return LGPS_GNSS_BACKOFF_STEP3_MS;
}

static void gnss_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint32_t backoff_ms = LGPS_GNSS_BACKOFF_STEP1_MS;
	uint32_t reported_drop_bytes = 0U;

	/* Block until lgps_gnss_init() reports the receive path is usable. */
	if (k_sem_take(&gnss_ready_sem, K_FOREVER) != 0) {
		return;
	}

	gnss_power_up();
	set_state(LGPS_GNSS_STATE_COLD_START, LGPS_GNSS_COLD_START_TIMEOUT_MS);

	for (;;) {
		uint32_t dropped;

		gnss_process_rx();
		lgps_wdt_checkin(LGPS_WDT_TASK_GNSS);

		/* The ISR only counts ring overflows; reporting them here keeps the
		 * ISR free of logging latency.
		 */
		dropped = (uint32_t)atomic_get(&isr_rx_dropped);
		if (dropped != reported_drop_bytes) {
			reported_drop_bytes = dropped;
			LOG_WRN("GNSS RX ring overflow: %u byte(s) dropped in total", dropped);
		}

		uint32_t elapsed = lgps_diag_uptime_ms() - state_since_ms;

		switch (gnss_state) {
		case LGPS_GNSS_STATE_COLD_START: {
			/*
			 * A receiver that finished booting after our first CFG-MSG ignores it
			 * and keeps streaming NMEA only. Re-send every 2 s for the first 10 s
			 * until any UBX frame arrives, then stop.
			 */
			/*
			 * Bytes arriving without a single '$' and without a UBX sync mean the
			 * line rate is wrong (framing garbage), not that the module is silent.
			 * Try the other common factory rate once, then report clearly.
			 */
			if (!baud_fallback_done && (rx_bytes_total() - baud_probe_base_bytes > 200U) &&
			    ((uint32_t)atomic_get(&isr_nmea_starts) == 0U) && (ubx_frames == 0U) &&
			    (elapsed > 3000U)) {
				baud_fallback_done = true;
				LOG_WRN("GNSS: %u bytes received at %u baud without any '$' or UBX sync - "
					"assuming a line-rate mismatch",
					rx_bytes_total() - baud_probe_base_bytes, current_baud);
				(void)gnss_uart_set_baud(LGPS_GNSS_BAUD_FALLBACK);
				/* Re-baseline instead of zeroing the ISR counter: writing an ISR
				 * counter from a thread loses every increment that lands in
				 * between the read and the store.
				 */
				baud_probe_base_bytes = rx_bytes_total();
				next_reconfig_ms = 0U;
			}

			if ((ubx_frames == 0U) && (elapsed < 10000U) &&
			    (lgps_diag_uptime_ms() >= next_reconfig_ms)) {
				next_reconfig_ms = lgps_diag_uptime_ms() + 2000U;
				(void)gnss_configure_output();
			}

			if (elapsed >= LGPS_GNSS_COLD_START_TIMEOUT_MS) {
				LOG_WRN("no 3D fix within %u s, cutting the GNSS rail",
					LGPS_GNSS_COLD_START_TIMEOUT_MS / 1000U);
				gnss_power_down();
				backoff_ms = backoff_duration_ms(gnss_state, backoff_ms);
				set_state(LGPS_GNSS_STATE_BACKOFF, backoff_ms);
			}
			break;
		}

		case LGPS_GNSS_STATE_FIXED:
			set_state(LGPS_GNSS_STATE_EPHEMERIS_HOLD, LGPS_GNSS_EPHEMERIS_HOLD_MS);
			break;

		case LGPS_GNSS_STATE_EPHEMERIS_HOLD:
			if (elapsed >= LGPS_GNSS_EPHEMERIS_HOLD_MS) {
				LOG_INF("ephemeris hold done, powering the GNSS rail down");
				gnss_power_down();
				set_state(LGPS_GNSS_STATE_BACKOFF, backoff_ms);
			}
			break;

		case LGPS_GNSS_STATE_BACKOFF:
			if (elapsed >= backoff_ms) {
				LOG_INF("backoff expired (%u ms), retrying GNSS", backoff_ms);
				gnss_power_up();
				backoff_ms = backoff_duration_ms(gnss_state, backoff_ms);
				set_state(LGPS_GNSS_STATE_COLD_START,
					  LGPS_GNSS_COLD_START_TIMEOUT_MS);
			}
			break;

		case LGPS_GNSS_STATE_OFF:
		default:
			break;
		}

		k_msleep(20);
	}
}

K_THREAD_DEFINE(lgps_gnss_thread, GNSS_THREAD_STACK, gnss_thread, NULL, NULL, NULL,
		GNSS_THREAD_PRIORITY, 0, 0);

int lgps_gnss_init(void)
{
	struct uart_config cfg;
	int ret;

	if (!device_is_ready(gnss_uart)) {
		LOG_ERR("UARTE20 (GPS) not ready");
		return -ENODEV;
	}

	ret = uart_irq_callback_user_data_set(gnss_uart, gnss_uart_isr, NULL);
	if (ret < 0) {
		if (ret == -ENOTSUP) {
			LOG_ERR("UARTE20 interrupt API unavailable (%d = -ENOTSUP): "
				"CONFIG_UART_INTERRUPT_DRIVEN must be enabled", ret);
		} else {
			LOG_ERR("UARTE20 callback registration failed (%d)", ret);
		}
		return ret;
	}

	lgps_ubx_parser_reset(&parser);
	memset(&current_fix, 0, sizeof(current_fix));

	/*
	 * current_baud must mirror the line rate the UART is really configured for:
	 * gnss_uart_set_baud() returns early when the requested rate equals it, so a
	 * stale constant would skip a needed reconfiguration and the log would claim a
	 * rate that is not in effect (the 115200-vs-38400 mismatch in the M1 logs).
	 */
	ret = uart_config_get(gnss_uart, &cfg);
	if (ret < 0) {
		LOG_ERR("uart_config_get failed (%d)", ret);
		return ret;
	}

	current_baud = cfg.baudrate;

	LOG_INF("GNSS UART ready: UARTE20 P1.04(TX)/P1.05(RX) %u 8N1 (devicetree current-speed), "
		"default part MAX-M10S (UBX)",
		current_baud);

	/* Only now may the state machine touch the GNSS rail. */
	k_sem_give(&gnss_ready_sem);

	return 0;
}
