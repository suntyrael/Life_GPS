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

#include <errno.h>

#include <lifegps/board_io.h>
#include <lifegps/diag.h>
#include <lifegps/gnss.h>
#include <lifegps/time.h>
#include <lifegps/ubx.h>
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_gnss, LOG_LEVEL_INF);

#define GNSS_THREAD_STACK 2048
#define GNSS_THREAD_PRIORITY 6
#define GNSS_RX_RING_SIZE 4096
#define GNSS_PVT_LOG_PERIOD_MS 5000U
/*
 * How long NAV-PVT may stay silent before the link is treated as broken.
 *
 * Only reachable with CONFIG_LGPS_GNSS_ALWAYS_ON, where the state machine never
 * powers the receiver down and therefore never gets the "power cycle re-sends the
 * configuration" recovery for free: a receiver reset (brown-out, ESD) reloads its
 * configuration from BBR/flash, the RAM-layer NAV-PVT rate is gone, and without
 * this check the firmware would sit in FIXED forever while only NMEA arrives.
 */
#define GNSS_PVT_LOST_MS 10000U

/*
 * Configuration path: UBX-CFG-VALSET / UBX-CFG-VALGET.
 *
 * The previous code sent UBX-CFG-MSG (0x06 0x01), which the M10 does not
 * document at all - the UBX-CFG chapter of UBX-21035062 R03 holds only CFG-CFG,
 * CFG-RST, CFG-VALDEL, CFG-VALGET and CFG-VALSET (section 3.10), and section
 * 4.10 warns that some legacy UBX-CFG messages are unavailable on some products.
 * A receiver that ignores the frame keeps streaming the factory NMEA set and
 * never emits NAV-PVT, which is exactly what the M1 logs showed (nmea>0, ubx=0,
 * age_ms=0).
 */
#define UBX_CFG_LAYERS LGPS_UBX_LAYER_RAM

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
/* The fast-link profile raised the line rate and the receiver never answered at
 * it; the revert runs once per power cycle.
 */
static bool baud_revert_done;

/*
 * True once CFG-VALGET read the NAV-PVT output rate back as non-zero.
 *
 * This is the state the state machine actually cares about: not "we sent the
 * command" but "the receiver reports the rate". A UBX-ACK-ACK alone would also
 * mean "received", while turning NAV-PVT on has failed before, and an ACK-NAK
 * would otherwise look like progress because it is still a UBX frame.
 */
static bool nav_pvt_rate_verified;

/* One-shot warning per cold-start window; reset together with the verification. */
static bool nav_pvt_rate_failure_logged;

/* One-shot "the window is over but the rail stays on" notice, per power cycle. */
static bool policy_hold_logged;

/*
 * Debug session (BLE opcode 0x36).
 *
 * debug_req_* is written by the BLE command thread and drained by the GNSS
 * thread, which owns the UART; only the GNSS thread advances debug_flags and
 * debug_expires_ms, so the two sides never write the same variable.
 */
static uint8_t debug_flags;
static uint32_t debug_expires_ms;
static bool debug_pending;
static uint8_t debug_req_flags;
static uint16_t debug_req_ttl_s;
/* Guards the five variables above: debug_flags/expires are written by the GNSS
 * thread and read by the BLE thread (query), the request fields the other way
 * round. Spinlock because neither side may sleep while holding it.
 */
static struct k_spinlock debug_lock;
/* Outputs the receiver currently has switched on because of the session. */
static uint8_t debug_applied_flags;

/* Newest UBX-NAV-SAT list, written by the GNSS thread. */
static struct lgps_gnss_sat sat_list[LGPS_GNSS_SAT_MAX];
static uint8_t sat_count;
static uint32_t sat_uptime_ms;
/* The GNSS thread writes the list, the main loop copies it for the BLE frame. */
static struct k_spinlock sat_lock;
#define GNSS_SAT_STALE_MS 5000U

/*
 * NMEA capture ring: one sentence per slot, produced by the GNSS thread and
 * consumed by the main loop.
 *
 * A sentence is at most 82 characters including "$" and the checksum, so 96 is
 * one comfortable slot; eight slots cover the burst that follows a navigation
 * epoch when several talkers emit GSV at once.
 */
#define GNSS_NMEA_LINE_MAX 96U
#define GNSS_NMEA_SLOTS 8U

static char nmea_slots[GNSS_NMEA_SLOTS][GNSS_NMEA_LINE_MAX];
static uint8_t nmea_slot_len[GNSS_NMEA_SLOTS];
static uint32_t nmea_head;
static uint32_t nmea_tail;
static struct k_spinlock nmea_lock;

/* Sentence being assembled right now (GNSS thread only). */
static char nmea_line[GNSS_NMEA_LINE_MAX];
static uint8_t nmea_len;
static bool nmea_active;
static uint32_t nmea_dropped;

/*
 * Configuration request queue (BLE opcode 0x35).
 *
 * The BLE command thread fills cfg_req and waits on cfg_done; the GNSS thread
 * turns it into CFG-VALSET/CFG-VALGET and fills cfg_result from the answer.
 * A receiver answer takes at most one navigation epoch, so the wait is bounded.
 */
enum lgps_gnss_cfg_op {
	CFG_OP_NONE = 0,
	CFG_OP_GET,
	CFG_OP_SET,
};

static struct {
	uint8_t op;
	uint8_t phase;
	bool flash;
	bool acked;
	uint32_t key;
	uint32_t value;
	uint8_t len;
	uint32_t deadline_ms;
} cfg_req;

static struct lgps_gnss_cfg_value cfg_result;
static int cfg_status;
static int cfg_wait; /* GNSS thread: outstanding CFG answer expected */
static uint32_t cfg_wait_ms;

/*
 * Where the request currently is on the wire.
 *
 * A write is not one frame but four steps: VALSET, wait for UBX-ACK, wait out the
 * GNSS subsystem restart when the key belongs to CFG-SIGNAL, then VALGET for the
 * readback. Sending the VALGET straight after the VALSET - which is what this
 * code used to do - puts it inside the restart, where the receiver either ignores
 * it or answers ACK-NAK, i.e. it looks exactly like "the key is unsupported".
 */
enum lgps_gnss_cfg_phase {
	CFG_PHASE_SEND = 0, /* nothing on the wire yet */
	CFG_PHASE_ACK,      /* VALSET sent, waiting for UBX-ACK */
	CFG_PHASE_SETTLE,   /* ACK seen, waiting out the subsystem restart */
	CFG_PHASE_READ,     /* VALGET sent, waiting for the answer */
};

/*
 * Only one VALSET can be outstanding at a time, because an ACK carries only the
 * class/id it acknowledges and not the key. Two producers exist: the BLE command
 * thread (opcode 0x35) and the power-up profile below; they take turns instead of
 * interleaving frames.
 */
enum lgps_gnss_ack_owner {
	ACK_OWNER_NONE = 0,
	ACK_OWNER_CFG,
	ACK_OWNER_BRINGUP,
};

static uint8_t ack_owner;

/*
 * Power-up profile (CONFIG_LGPS_GNSS_FAST_LINK).
 *
 * Every entry is written one frame at a time and waits for its ACK, so a step the
 * receiver rejects is skipped instead of silently corrupting the next one. The
 * line rate is changed last and is the only step that touches the host side.
 *
 * The 10 Hz arithmetic: NAV-PVT is 100 bytes on the wire, so 10 Hz needs 1 kB/s,
 * which 38400 carries fine. What does not fit is the factory NMEA set (~1.25 kB
 * per epoch => 12 kB/s at 10 Hz) and NAV-SAT (16 + 12*n bytes, 400 at 32 sats),
 * so step 1 removes NMEA and step 3 opens the 460800 link. That rate carries
 * ~46 kB/s, i.e. 12 k nav-PVT + 4 k NAV-SAT + 6.6 k NAV-SIG at 10 Hz (about 50 %
 * of the line).
 */
struct gnss_bringup_step {
	uint32_t key;
	uint32_t value;
	uint8_t len;
	uint8_t layers;
	uint16_t gap_ms;
	bool switch_baud;
};

#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
static const struct gnss_bringup_step bringup_profile[] = {
	/* NMEA off: one key for the whole protocol on UART1 (section 4.9.29). */
	{ LGPS_UBX_CFG_KEY_UART1OUTPROT_NMEA, 0U, 1U, LGPS_UBX_LAYER_RAM, 0U, false },
	/* Measurement period; BBR so it survives the standby request. */
	{ LGPS_UBX_CFG_KEY_RATE_MEAS, CONFIG_LGPS_GNSS_RATE_MS, 2U,
	  LGPS_UBX_LAYER_RAM | LGPS_UBX_LAYER_BBR, 0U, false },
	/* Line rate last, RAM layer only (see ubx.h). */
	{ LGPS_UBX_CFG_KEY_UART1_BAUDRATE, LGPS_GNSS_BAUD_FAST, 4U, LGPS_UBX_LAYER_RAM,
	  LGPS_GNSS_BAUD_SETTLE_MS, true },
};
#endif

enum lgps_gnss_bringup_phase {
	BRINGUP_PHASE_SEND = 0,
	BRINGUP_PHASE_ACK,
	BRINGUP_PHASE_SETTLE,
};

#define GNSS_BRINGUP_DONE 0xFFU

/* Outside the conditional on purpose: the thread loop and gnss_bringup_active()
 * consult it whether or not the profile was compiled in.
 */
static uint8_t bringup_index = GNSS_BRINGUP_DONE;

#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
static uint8_t bringup_phase;
static uint32_t bringup_deadline_ms;
#endif

/* Written by handle_ubx_ack() either way, so not part of the block above. */
static bool bringup_acked;
static bool bringup_nak;

/* The profile runs at most once per power cycle, and only after the receiver has
 * proved it answers at the boot rate. Without the latch the readback that follows
 * the baud rate change would arm the profile again, forever.
 */
static bool bringup_armed;

/* True while a profile step is in flight; the BLE path then waits its turn. */
static bool gnss_bringup_active(void)
{
#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
	return bringup_index < ARRAY_SIZE(bringup_profile);
#else
	return false;
#endif
}

static void gnss_bringup_start(void)
{
#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
	bringup_index = 0U;
	bringup_phase = BRINGUP_PHASE_SEND;
	bringup_acked = false;
	bringup_nak = false;
#else
	bringup_index = GNSS_BRINGUP_DONE;
#endif
}

static K_MUTEX_DEFINE(cfg_req_lock);
static K_SEM_DEFINE(cfg_done_sem, 0, 1);

#define GNSS_CFG_TIMEOUT_MS 800

/*
 * How long the BLE command thread waits for a write.
 *
 * A write is ACK + (CFG-SIGNAL restart margin, 0.5 s) + readback, and a 1 Hz
 * receiver only answers the VALGET at its next navigation epoch. The flat
 * GNSS_CFG_TIMEOUT_MS + 300 used before was shorter than that sum, so a perfectly
 * healthy CFG-SIGNAL write could time out in the App and show up as "busy".
 */
#define GNSS_CFG_SET_WAIT_MS                                                                 \
	(GNSS_CFG_TIMEOUT_MS + LGPS_GNSS_CFG_SIGNAL_SETTLE_MS + GNSS_CFG_TIMEOUT_MS + 300U)

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
	out->rx_dropped = (uint32_t)atomic_get(&isr_rx_dropped);
}

uint32_t lgps_gnss_baud(void)
{
	return current_baud;
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

static int gnss_enable_nav_pvt(void)
{
	/* Section 3.10.5.1: version 0, layers, 2 reserved bytes, then key + value. */
	static const uint8_t rate_on = 1U;
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 4U + 1U +
		      LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build_valset_item(frame, sizeof(frame), UBX_CFG_LAYERS,
					LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1, &rate_on,
					(uint8_t)sizeof(rate_on));
	if (ret < 0) {
		LOG_ERR("UBX-CFG-VALSET build failed (%d)", ret);
		return ret;
	}

	int len = ret;

	ret = gnss_uart_send(frame, (size_t)len);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("sent UBX-CFG-VALSET: NAV-PVT UART1 rate = 1 (key 0x%08x, RAM layer, %d bytes)",
		LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1, len);

	return 0;
}

/*
 * Ask the receiver what it thinks the NAV-PVT output rate is.
 *
 * The answer, not the ACK, is what tells us the link works: it proves the key id
 * this firmware uses is the one the receiver knows (our interface-description
 * copy redacts that row's name) and that the value really is in the RAM layer.
 */
static int gnss_request_rate_readback(void)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 4U + LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build_valget_item(frame, sizeof(frame), LGPS_UBX_VALGET_LAYER_RAM,
					LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1);
	if (ret < 0) {
		LOG_ERR("UBX-CFG-VALGET build failed (%d)", ret);
		return ret;
	}

	int len = ret;

	ret = gnss_uart_send(frame, (size_t)len);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/* --- configuration items ---------------------------------------------------- */

struct gnss_cfg_key {
	uint32_t key;
	uint8_t len;
	uint32_t min_value;
	uint32_t max_value;
	const char *name;
};

/*
 * The only keys this firmware will touch.
 *
 * Hard-coded on purpose: the BLE message carries a key id, and a key is a write
 * into the receiver's configuration. An open key/value path would let a bug or a
 * hostile host rewrite CFG-UART1-BAUDRATE and lose the link for good, which is
 * exactly what a debug feature must not be able to do. Ranges are the documented
 * ones (interface description section 4.9 and the enums in the M10 SPG release
 * notes); a value outside the range is rejected before it reaches the UART.
 *
 * Deliberately absent, and not an oversight (decided 2026-11-20 during the M1 GNSS bring-up):
 *   - CFG-HW-RF_LNA_MODE (0x20a30057) programs OTP memory and needs a power cycle
 *     to take effect; a mis-set value is not undoable over BLE.
 *   - CFG-NAVSPG-CONSTR_ALT / CONSTR_ALTVAR (0x401100c1 / c2) only apply when
 *     CFG-NAVSPG-FIXMODE is 2D; writing the altitude alone pins nothing and looks
 *     like a working feature.
 *   - CFG-ITFM-* thresholds are unreadable without UBX-MON-RF, so exposing them
 *     would be a blind control.
 *   - CFG-UART1-BAUDRATE / CFG-UART1OUTPROT-NMEA: written by the firmware itself
 *     (power-up profile), never by the host.
 */
static const struct gnss_cfg_key cfg_keys[] = {
	{ LGPS_UBX_CFG_KEY_ANA_USE, 1U, 0U, 1U, "CFG-ANA-USE_ANA" },
	{ LGPS_UBX_CFG_KEY_ANA_ORBMAXERR, 2U, 1U, 65535U, "CFG-ANA-ORBMAXERR" },
	{ LGPS_UBX_CFG_KEY_DYNMODEL, 1U, 0U, 10U, "CFG-NAVSPG-DYNMODEL" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GPS_ENA, 1U, 0U, 1U, "CFG-SIGNAL-GPS_ENA" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GAL_ENA, 1U, 0U, 1U, "CFG-SIGNAL-GAL_ENA" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_BDS_ENA, 1U, 0U, 1U, "CFG-SIGNAL-BDS_ENA" },
	/* Per-signal items: needed because MAX-M10S rejects BDS B1I together with
	 * GLONASS L1OF (integration manual section 2.1.3). Enabling GLONASS is
	 * therefore a sequence, not a single write: BDS_B1_ENA=0 first, then
	 * BDS_B1C_ENA=1, then GLO_ENA/GLO_L1_ENA=1. Every intermediate state has to
	 * be legal on its own, so the order matters.
	 */
	{ LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1_ENA, 1U, 0U, 1U, "CFG-SIGNAL-BDS_B1_ENA" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1C_ENA, 1U, 0U, 1U, "CFG-SIGNAL-BDS_B1C_ENA" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GLO_ENA, 1U, 0U, 1U, "CFG-SIGNAL-GLO_ENA" },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GLO_L1_ENA, 1U, 0U, 1U, "CFG-SIGNAL-GLO_L1_ENA" },
	{ LGPS_UBX_CFG_KEY_INFIL_MINELEV, 1U, 0U, 90U, "CFG-NAVSPG-INFIL_MINELEV" },
	{ LGPS_UBX_CFG_KEY_INFIL_MINCNO, 1U, 0U, 60U, "CFG-NAVSPG-INFIL_MINCNO" },
	{ LGPS_UBX_CFG_KEY_OUTFIL_PDOP, 2U, 1U, 9999U, "CFG-NAVSPG-OUTFIL_PDOP" },
	{ LGPS_UBX_CFG_KEY_RATE_MEAS, 2U, 25U, 1000U, "CFG-RATE-MEAS" },
	{ LGPS_UBX_CFG_KEY_RATE_NAV, 2U, 1U, 127U, "CFG-RATE-NAV" },
	/* Power management (section 4.9.15). OPERATEMODE is the switch that actually
	 * saves power in the pulse-positioning profile; the periods next to it are
	 * meaningless while it is FULL, which is the factory value.
	 */
	{ LGPS_UBX_CFG_KEY_PM_OPERATEMODE, 1U, 0U, 2U, "CFG-PM-OPERATEMODE" },
	{ LGPS_UBX_CFG_KEY_PM_POSUPDATEPERIOD, 4U, 5U, 604800U, "CFG-PM-POSUPDATEPERIOD" },
	{ LGPS_UBX_CFG_KEY_PM_ACQPERIOD, 4U, 0U, 604800U, "CFG-PM-ACQPERIOD" },
	{ LGPS_UBX_CFG_KEY_PM_ONTIME, 2U, 0U, 65535U, "CFG-PM-ONTIME" },
	{ LGPS_UBX_CFG_KEY_PM_DONOTENTEROFF, 1U, 0U, 1U, "CFG-PM-DONOTENTEROFF" },
	/* Receiver-side static hold, the hardware twin of the firmware's ZUPT test. */
	{ LGPS_UBX_CFG_KEY_MOT_GNSSSPEED_THRS, 1U, 0U, 255U, "CFG-MOT-GNSSSPEED_THRS" },
	{ LGPS_UBX_CFG_KEY_MOT_GNSSDIST_THRS, 2U, 0U, 65535U, "CFG-MOT-GNSSDIST_THRS" },
};

static const struct gnss_cfg_key *cfg_key_lookup(uint32_t key)
{
	ARRAY_FOR_EACH(cfg_keys, index) {
		if (cfg_keys[index].key == key) {
			return &cfg_keys[index];
		}
	}

	return NULL;
}

static uint32_t cfg_value_assemble(const uint8_t *bytes, uint8_t len)
{
	uint32_t value = 0U;

	for (uint8_t index = 0U; (index < len) && (index < 4U); index++) {
		value |= (uint32_t)bytes[index] << (8U * index);
	}

	return value;
}

static uint8_t gnss_send_cfg_item(uint32_t key, uint32_t value, uint8_t len, uint8_t layers)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 4U +
		     LGPS_UBX_CFG_VALUE_MAX_LEN + LGPS_UBX_TRAILER_LEN];
	uint8_t bytes[LGPS_UBX_CFG_VALUE_MAX_LEN];
	int ret;

	if ((len == 0U) || (len > LGPS_UBX_CFG_VALUE_MAX_LEN)) {
		return 0U;
	}

	for (uint8_t index = 0U; index < len; index++) {
		bytes[index] = (uint8_t)((value >> (8U * index)) & 0xFFU);
	}

	ret = lgps_ubx_build_valset_item(frame, sizeof(frame), layers, key, bytes, len);
	if (ret < 0) {
		LOG_ERR("CFG-VALSET build failed (%d)", ret);
		return 0U;
	}

	if (gnss_uart_send(frame, (size_t)ret) < 0) {
		return 0U;
	}

	return (uint8_t)ret;
}

static uint8_t gnss_send_cfg_get(uint32_t key)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 4U + LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build_valget_item(frame, sizeof(frame), LGPS_UBX_VALGET_LAYER_RAM, key);
	if (ret < 0) {
		LOG_ERR("CFG-VALGET build failed (%d)", ret);
		return 0U;
	}

	if (gnss_uart_send(frame, (size_t)ret) < 0) {
		return 0U;
	}

	return (uint8_t)ret;
}

/*
 * Message output rate of NAV-SAT, the only output the debug session switches.
 *
 * RAM layer only, never BBR or flash: the session is a temporary debugging tool,
 * and a receiver that keeps emitting a 392-byte message every second after the
 * session ends would silently cost power on every later cold start.
 */
static void gnss_apply_nav_sat_rate(bool enable)
{
	if (gnss_send_cfg_item(LGPS_UBX_CFG_KEY_MSGOUT_NAV_SAT_UART1, enable ? 1U : 0U, 1U,
			       LGPS_UBX_LAYER_RAM) != 0U) {
		LOG_INF("NAV-SAT output rate -> %u (RAM layer)", enable ? 1U : 0U);
	}
}

/*
 * NMEA output on UART1, as one protocol-level switch (CFG-UART1OUTPROT-NMEA).
 *
 * With CONFIG_LGPS_GNSS_FAST_LINK the power-up profile turns the factory NMEA set
 * off, because it does not fit a 10 Hz link. That would also silence the debug
 * session's raw NMEA forwarding, so the session turns the protocol back on while
 * it holds the NMEA flag and turns it off again when it ends.
 */
static void gnss_apply_nmea_out(bool enable)
{
	if (!IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)) {
		/* Nothing ever switched NMEA off, so nothing may switch it on. */
		return;
	}

	if (gnss_send_cfg_item(LGPS_UBX_CFG_KEY_UART1OUTPROT_NMEA, enable ? 1U : 0U, 1U,
			       LGPS_UBX_LAYER_RAM) != 0U) {
		LOG_INF("NMEA output on UART1 -> %u (RAM layer)", enable ? 1U : 0U);
	}
}

/* --- debug session --------------------------------------------------------- */

static bool gnss_debug_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&debug_lock);
	uint8_t flags = debug_flags;
	uint32_t expires_ms = debug_expires_ms;

	k_spin_unlock(&debug_lock, key);

	return (flags != 0U) && (expires_ms != 0U) &&
	       ((int32_t)(lgps_diag_uptime_ms() - expires_ms) < 0);
}

static bool gnss_debug_hold_power(void)
{
	k_spinlock_key_t key;
	uint8_t flags;

	if (!gnss_debug_active()) {
		return false;
	}

	key = k_spin_lock(&debug_lock);
	flags = debug_flags;
	k_spin_unlock(&debug_lock, key);

	return (flags & LGPS_GNSS_DEBUG_HOLD_POWER) != 0U;
}

/*
 * The single question the state machine is allowed to ask before dropping the
 * rail. Two independent sources can say no: the build-time switch
 * CONFIG_LGPS_GNSS_ALWAYS_ON (bench debugging, full multi-GNSS tracking) and a
 * live debug session that holds power for its TTL.
 */
static bool gnss_may_cut_power(void)
{
	if (IS_ENABLED(CONFIG_LGPS_GNSS_ALWAYS_ON)) {
		return false;
	}

	return !gnss_debug_hold_power();
}

static void gnss_debug_apply_outputs(void)
{
	uint8_t wanted = gnss_debug_active() ? debug_flags : 0U;

	if ((wanted & LGPS_GNSS_DEBUG_NAV_SAT) != (debug_applied_flags & LGPS_GNSS_DEBUG_NAV_SAT)) {
		gnss_apply_nav_sat_rate((wanted & LGPS_GNSS_DEBUG_NAV_SAT) != 0U);
	}

	if ((wanted & LGPS_GNSS_DEBUG_NMEA) != (debug_applied_flags & LGPS_GNSS_DEBUG_NMEA)) {
		gnss_apply_nmea_out((wanted & LGPS_GNSS_DEBUG_NMEA) != 0U);
	}

	debug_applied_flags = wanted;
}

static void gnss_service_debug(void)
{
	k_spinlock_key_t key = k_spin_lock(&debug_lock);
	bool pending = debug_pending;
	uint8_t req_flags = debug_req_flags;
	uint16_t req_ttl_s = debug_req_ttl_s;
	uint8_t active_flags = debug_flags;

	if (pending) {
		debug_pending = false;
		debug_req_ttl_s = 0U;
	}
	k_spin_unlock(&debug_lock, key);

	if (pending) {
		uint16_t ttl_s = (req_ttl_s == 0U) ? LGPS_GNSS_DEBUG_DEFAULT_TTL_S : req_ttl_s;

		if (ttl_s > LGPS_GNSS_DEBUG_MAX_TTL_S) {
			ttl_s = LGPS_GNSS_DEBUG_MAX_TTL_S;
		}

		key = k_spin_lock(&debug_lock);
		debug_flags = req_flags;
		debug_expires_ms = (req_flags == 0U)
					   ? 0U
					   : lgps_diag_uptime_ms() + ((uint32_t)ttl_s * 1000U);
		k_spin_unlock(&debug_lock, key);

		/* Never log while holding the spinlock: CONFIG_LOG_MODE_IMMEDIATE writes
		 * the console synchronously, which must not happen with interrupts masked.
		 */
		LOG_INF("GNSS debug session: flags=0x%02x ttl=%u s", req_flags, ttl_s);
		/* Force a re-assert: the receiver's RAM layer may have been cleared by a
		 * power cycle in between.
		 */
		debug_applied_flags = 0U;
	} else if ((active_flags != 0U) && !gnss_debug_active()) {
		key = k_spin_lock(&debug_lock);
		debug_flags = 0U;
		debug_expires_ms = 0U;
		k_spin_unlock(&debug_lock, key);

		LOG_INF("GNSS debug session expired, back to the power policy");
	}

	gnss_debug_apply_outputs();
}

int lgps_gnss_debug_set(uint8_t flags, uint16_t ttl_s)
{
	if ((flags & (uint8_t)~LGPS_GNSS_DEBUG_FLAGS_ALL) != 0U) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&debug_lock);

	debug_req_flags = flags;
	debug_req_ttl_s = ttl_s;
	debug_pending = true;
	k_spin_unlock(&debug_lock, key);

	return 0;
}

void lgps_gnss_debug_get(uint8_t *flags, uint16_t *remaining_s)
{
	uint32_t now_ms = lgps_diag_uptime_ms();
	uint32_t expires_ms;
	int32_t left_ms;

	if ((flags == NULL) || (remaining_s == NULL)) {
		return;
	}

	if (!gnss_debug_active()) {
		*flags = 0U;
		*remaining_s = 0U;
		return;
	}

	{
		k_spinlock_key_t key = k_spin_lock(&debug_lock);

		expires_ms = debug_expires_ms;
		*flags = debug_flags;
		k_spin_unlock(&debug_lock, key);
	}

	left_ms = (int32_t)(expires_ms - now_ms);
	*remaining_s = (left_ms > 0) ? (uint16_t)((uint32_t)left_ms / 1000U) : 0U;
}

/* --- NMEA capture ---------------------------------------------------------- */

static void nmea_push(const char *line, uint8_t len)
{
	k_spinlock_key_t key;
	uint32_t slot;

	key = k_spin_lock(&nmea_lock);
	if ((nmea_head - nmea_tail) >= GNSS_NMEA_SLOTS) {
		nmea_dropped++;
		k_spin_unlock(&nmea_lock, key);
		return;
	}

	slot = nmea_head % GNSS_NMEA_SLOTS;
	memcpy(nmea_slots[slot], line, len);
	nmea_slot_len[slot] = len;
	nmea_head++;
	k_spin_unlock(&nmea_lock, key);
}

size_t lgps_gnss_pop_nmea(char *out, size_t capacity)
{
	k_spinlock_key_t key;
	uint32_t slot;
	size_t len;

	if ((out == NULL) || (capacity < 2U)) {
		return 0U;
	}

	key = k_spin_lock(&nmea_lock);
	if (nmea_head == nmea_tail) {
		k_spin_unlock(&nmea_lock, key);
		return 0U;
	}

	slot = nmea_tail % GNSS_NMEA_SLOTS;
	len = nmea_slot_len[slot];
	if (len > (capacity - 1U)) {
		len = capacity - 1U;
	}

	memcpy(out, nmea_slots[slot], len);
	out[len] = '\0';
	nmea_tail++;
	k_spin_unlock(&nmea_lock, key);

	return len;
}

static void nmea_feed(uint8_t byte)
{
	if ((debug_flags & LGPS_GNSS_DEBUG_NMEA) == 0U) {
		nmea_active = false;
		nmea_len = 0U;
		return;
	}

	if (byte == (uint8_t)'$') {
		nmea_len = 0U;
		nmea_active = true;
	}

	if (!nmea_active) {
		return;
	}

	if ((byte == (uint8_t)'\r') || (byte == (uint8_t)'\n')) {
		/* "$GNGGA,..." without the line terminator is what the App shows. */
		if (nmea_len > 3U) {
			nmea_push(nmea_line, nmea_len);
		}

		nmea_active = false;
		nmea_len = 0U;
		return;
	}

	if (nmea_len < (uint8_t)(GNSS_NMEA_LINE_MAX - 1U)) {
		nmea_line[nmea_len] = (char)byte;
		nmea_len++;
	} else {
		/* Over-long line: the receiver sent something that is not NMEA 0183. */
		nmea_active = false;
		nmea_len = 0U;
	}
}

/* --- satellites ------------------------------------------------------------ */

uint8_t lgps_gnss_get_satellites(struct lgps_gnss_sat *out, uint8_t max)
{
	uint32_t now_ms = lgps_diag_uptime_ms();
	k_spinlock_key_t key;
	uint8_t count;

	if ((out == NULL) || (max == 0U)) {
		return 0U;
	}

	key = k_spin_lock(&sat_lock);

	if ((sat_count == 0U) || ((uint32_t)(now_ms - sat_uptime_ms) > GNSS_SAT_STALE_MS)) {
		k_spin_unlock(&sat_lock, key);
		return 0U;
	}

	count = (sat_count < max) ? sat_count : max;
	memcpy(out, sat_list, (size_t)count * sizeof(sat_list[0]));
	k_spin_unlock(&sat_lock, key);

	return count;
}

/* --- configuration request handling (GNSS thread side) ---------------------- */

/* End the pending request: hand the verdict to the blocked BLE thread and drop
 * every "something is outstanding" flag, so the next request starts clean.
 */
static void gnss_cfg_finish(int status)
{
	ack_owner = ACK_OWNER_NONE;
	cfg_wait = 0;
	cfg_status = status;
	cfg_req.op = CFG_OP_NONE;
	k_sem_give(&cfg_done_sem);
}

/*
 * How long to wait after the ACK before the readback.
 *
 * The group id is bits 16..23 of a configuration key. 0x31 is CFG-SIGNAL, the one
 * group whose writes restart the GNSS subsystem; everything else can be read back
 * immediately.
 */
static uint32_t gnss_cfg_settle_ms(uint32_t key)
{
	return (((key >> 16) & 0xFFU) == 0x31U) ? LGPS_GNSS_CFG_SIGNAL_SETTLE_MS : 0U;
}

static void gnss_service_cfg(void)
{
	uint32_t now_ms;

	if (cfg_req.op == CFG_OP_NONE) {
		/* The host may have given up while a write was in flight (its
		 * semaphore timed out). Clearing the state here, in the GNSS thread,
		 * keeps the single-writer rule for ack_owner.
		 */
		if (ack_owner == ACK_OWNER_CFG) {
			ack_owner = ACK_OWNER_NONE;
			cfg_wait = 0;
		}
		return;
	}

	if ((gnss_state == LGPS_GNSS_STATE_OFF) || (gnss_state == LGPS_GNSS_STATE_BACKOFF)) {
		/* The receiver is unpowered: answer now instead of waiting for a
		 * timeout that cannot produce a value.
		 */
		gnss_cfg_finish(-ENODEV);
		return;
	}

	/* A profile step owns the ACK while it is in flight; one ACK cannot answer
	 * two requests. The BLE side waits (at most the profile's ~1 s) instead.
	 */
	if (gnss_bringup_active() && ((cfg_req.phase == CFG_PHASE_SEND))) {
		return;
	}

	now_ms = lgps_diag_uptime_ms();

	switch (cfg_req.phase) {
	case CFG_PHASE_SEND:
		if (cfg_req.op == CFG_OP_SET) {
			uint8_t layers = LGPS_UBX_LAYER_RAM | LGPS_UBX_LAYER_BBR;

			if (cfg_req.flash) {
				layers |= LGPS_UBX_LAYER_FLASH;
			}

			LOG_INF("CFG-VALSET key 0x%08x = %u (layers 0x%02x)", cfg_req.key,
				(unsigned int)cfg_req.value, layers);

			if (gnss_send_cfg_item(cfg_req.key, cfg_req.value, cfg_req.len, layers) ==
			    0U) {
				gnss_cfg_finish(-EIO);
				return;
			}

			cfg_req.acked = false;
			cfg_req.deadline_ms = now_ms + GNSS_CFG_TIMEOUT_MS;
			cfg_req.phase = CFG_PHASE_ACK;
			ack_owner = ACK_OWNER_CFG;
			return;
		}

		/* A read needs no ACK: the VALGET response itself is the answer. */
		if (gnss_send_cfg_get(cfg_req.key) == 0U) {
			gnss_cfg_finish(-EIO);
			return;
		}

		cfg_wait = 1;
		cfg_wait_ms = now_ms;
		cfg_req.phase = CFG_PHASE_READ;
		return;

	case CFG_PHASE_ACK:
		if (cfg_req.acked) {
			cfg_req.acked = false;
			cfg_req.deadline_ms = now_ms + gnss_cfg_settle_ms(cfg_req.key);
			cfg_req.phase = CFG_PHASE_SETTLE;
			return;
		}

		if ((int32_t)(now_ms - cfg_req.deadline_ms) >= 0) {
			gnss_cfg_finish(-ETIMEDOUT);
		}
		return;

	case CFG_PHASE_SETTLE:
		if ((int32_t)(now_ms - cfg_req.deadline_ms) < 0) {
			return;
		}

		/* Always finish with a read: it is the only proof of what took effect. */
		if (gnss_send_cfg_get(cfg_req.key) == 0U) {
			gnss_cfg_finish(-EIO);
			return;
		}

		cfg_wait = 1;
		cfg_wait_ms = now_ms;
		cfg_req.phase = CFG_PHASE_READ;
		return;

	case CFG_PHASE_READ:
		if ((cfg_wait != 0) &&
		    ((uint32_t)(now_ms - cfg_wait_ms) >= GNSS_CFG_TIMEOUT_MS)) {
			gnss_cfg_finish(-ETIMEDOUT);
		}
		return;

	default:
		gnss_cfg_finish(-EIO);
		return;
	}
}

int lgps_gnss_cfg_get(uint32_t key, struct lgps_gnss_cfg_value *out)
{
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	if (cfg_key_lookup(key) == NULL) {
		return -EINVAL;
	}

	if (k_mutex_lock(&cfg_req_lock, K_MSEC(GNSS_CFG_TIMEOUT_MS * 2U)) != 0) {
		return -EBUSY;
	}

	(void)k_sem_take(&cfg_done_sem, K_NO_WAIT);
	cfg_req.op = CFG_OP_GET;
	cfg_req.phase = CFG_PHASE_SEND;
	cfg_req.acked = false;
	cfg_req.deadline_ms = 0U;
	cfg_req.key = key;
	cfg_req.len = 0U;
	cfg_req.value = 0U;
	cfg_req.flash = false;

	ret = k_sem_take(&cfg_done_sem, K_MSEC(GNSS_CFG_TIMEOUT_MS + 300U));
	if (ret == 0) {
		ret = cfg_status;
	}

	if (ret == 0) {
		*out = cfg_result;
	} else {
		cfg_req.op = CFG_OP_NONE;
	}

	k_mutex_unlock(&cfg_req_lock);

	return (ret == 0) ? 0 : ((ret == -EAGAIN) ? -ETIMEDOUT : ret);
}

int lgps_gnss_cfg_set(uint32_t key, uint32_t value, bool persist_to_flash,
		      struct lgps_gnss_cfg_value *readback)
{
	const struct gnss_cfg_key *desc = cfg_key_lookup(key);
	int ret;

	if (desc == NULL) {
		return -EINVAL;
	}

	if ((value < desc->min_value) || (value > desc->max_value)) {
		LOG_WRN("%s = %u is outside %u..%u", desc->name, (unsigned int)value,
			(unsigned int)desc->min_value, (unsigned int)desc->max_value);
		return -ERANGE;
	}

	if (k_mutex_lock(&cfg_req_lock, K_MSEC(GNSS_CFG_TIMEOUT_MS * 2U)) != 0) {
		return -EBUSY;
	}

	(void)k_sem_take(&cfg_done_sem, K_NO_WAIT);
	cfg_req.op = CFG_OP_SET;
	cfg_req.phase = CFG_PHASE_SEND;
	cfg_req.acked = false;
	cfg_req.deadline_ms = 0U;
	cfg_req.key = key;
	cfg_req.len = desc->len;
	cfg_req.value = value;
	cfg_req.flash = persist_to_flash;

	ret = k_sem_take(&cfg_done_sem, K_MSEC(GNSS_CFG_SET_WAIT_MS));
	if (ret == 0) {
		ret = cfg_status;
	}

	if (ret == 0) {
		if (readback != NULL) {
			*readback = cfg_result;
		}

		LOG_INF("%s set to %u (readback %u)", desc->name, (unsigned int)value,
			(unsigned int)cfg_result.value);
	} else if (ret == -ENOTSUP) {
		LOG_WRN("%s: the receiver rejected the key", desc->name);
	}

	k_mutex_unlock(&cfg_req_lock);

	return (ret == -EAGAIN) ? -ETIMEDOUT : ret;
}

/* --- power-up profile (CONFIG_LGPS_GNSS_FAST_LINK) ------------------------- */

/*
 * Advance the profile by at most one frame per call.
 *
 * A step the receiver rejects or does not acknowledge is skipped, never retried:
 * the profile is a bandwidth optimisation, and a receiver that refuses a key keeps
 * refusing it. The only step whose failure matters is the baud rate change, and
 * that one is caught by the NAV-PVT readback in the cold-start state.
 */
static void gnss_bringup_service(void)
{
#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
	const struct gnss_bringup_step *step;
	uint32_t now_ms;

	if (bringup_index >= ARRAY_SIZE(bringup_profile)) {
		return;
	}

	/* One ACK owner at a time. A request that is already on the wire wins; one
	 * that is merely queued does not, otherwise the two would wait for each
	 * other: the request waits for the profile to finish, and the profile waits
	 * for the queue to empty.
	 */
	if ((cfg_req.op != CFG_OP_NONE) && (cfg_req.phase != CFG_PHASE_SEND)) {
		return;
	}

	step = &bringup_profile[bringup_index];
	now_ms = lgps_diag_uptime_ms();

	switch (bringup_phase) {
	case BRINGUP_PHASE_SEND:
		if (gnss_send_cfg_item(step->key, step->value, step->len, step->layers) == 0U) {
			LOG_WRN("bring-up step %u (key 0x%08x) not sent, skipping",
				bringup_index, step->key);
			bringup_index++;
			return;
		}

		bringup_acked = false;
		bringup_nak = false;
		ack_owner = ACK_OWNER_BRINGUP;
		bringup_deadline_ms = now_ms + GNSS_CFG_TIMEOUT_MS;
		bringup_phase = BRINGUP_PHASE_ACK;
		return;

	case BRINGUP_PHASE_ACK:
		if (bringup_nak) {
			LOG_WRN("bring-up step %u (key 0x%08x) NAK'd, skipping", bringup_index,
				step->key);
			bringup_index++;
			bringup_phase = BRINGUP_PHASE_SEND;
			return;
		}

		if (!bringup_acked) {
			if ((int32_t)(now_ms - bringup_deadline_ms) >= 0) {
				ack_owner = ACK_OWNER_NONE;
				LOG_WRN("bring-up step %u (key 0x%08x) not acknowledged, skipping",
					bringup_index, step->key);
				bringup_index++;
				bringup_phase = BRINGUP_PHASE_SEND;
			}
			return;
		}

		bringup_acked = false;
		bringup_deadline_ms = now_ms + step->gap_ms;
		bringup_phase = BRINGUP_PHASE_SETTLE;
		return;

	case BRINGUP_PHASE_SETTLE:
		if ((int32_t)(now_ms - bringup_deadline_ms) < 0) {
			return;
		}

		if (step->switch_baud) {
			if (gnss_uart_set_baud(step->value) == 0) {
				LOG_INF("GNSS link raised to %u 8N1", step->value);
				/* Prove the new rate instead of assuming it: the NAV-PVT
				 * readback is the same test that validated the old one.
				 */
				baud_probe_base_bytes = rx_bytes_total();
				nav_pvt_rate_verified = false;
				next_reconfig_ms = 0U;
				(void)gnss_request_rate_readback();
			}
		}

		bringup_index++;
		bringup_phase = BRINGUP_PHASE_SEND;
		return;

	default:
		bringup_index = GNSS_BRINGUP_DONE;
		return;
	}
#endif
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
	/*
	 * pDOP 99.99 is the receiver's "no solution" sentinel, not a DOP value; it
	 * must not reach the status frame, where the App would render it as a real
	 * HDOP of 99.99.
	 */
	current_fix.pdop_1e2 = (fix_valid && (pvt->pdop_1e2 < 9999U)) ? pvt->pdop_1e2 : 0U;
	current_fix.lat_e7 = pvt->lat_e7;
	current_fix.lon_e7 = pvt->lon_e7;
	current_fix.hmsl_mm = pvt->hmsl_mm;
	current_fix.v_acc_mm = pvt->vacc_mm;
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

/*
 * UBX-ACK (section 3.9): ACK-ACK confirms the receiver understood our message,
 * ACK-NAK says it will not apply it. The NAK is the interesting one - it is the
 * only on-wire evidence that a key or a message is unsupported on this product,
 * and the previous code logged both at debug level and printed the payload of
 * the frame as if it were the class id.
 */
static void handle_ubx_ack(void)
{
	bool accepted = false;
	uint8_t ack_class = 0U;
	uint8_t ack_id = 0U;

	if (lgps_ubx_decode_ack(&parser, &accepted, &ack_class, &ack_id) != 0) {
		return;
	}

	if (accepted) {
		LOG_DBG("UBX-ACK-ACK for class 0x%02x id 0x%02x", ack_class, ack_id);
	} else {
		LOG_WRN("UBX-ACK-NAK for class 0x%02x id 0x%02x: the receiver refused it "
			"(unknown key, bad layer, invalid signal combination, or message not "
			"available on this product)",
			ack_class, ack_id);
	}

	if (ack_class != LGPS_UBX_CLASS_CFG) {
		return;
	}

	/* Only a write carries a key, so only a write has an ACK owner. A NAK is the
	 * only on-wire evidence that a key or the resulting signal combination is
	 * refused on this product.
	 */
	if (ack_owner == ACK_OWNER_CFG) {
		ack_owner = ACK_OWNER_NONE;

		if (!accepted) {
			/* End the request now instead of waiting for a readback the
			 * receiver is not going to send.
			 */
			gnss_cfg_finish(-ENOTSUP);
			return;
		}

		cfg_req.acked = true;
		return;
	}

	if (ack_owner == ACK_OWNER_BRINGUP) {
		ack_owner = ACK_OWNER_NONE;
		bringup_acked = accepted;
		bringup_nak = !accepted;
	}
}

/*
 * UBX-CFG-VALGET response (section 3.10.4.1): version, layer, position, then
 * key/value pairs. We request one U1 key, so the answer is 9 bytes.
 */
static void handle_cfg_valget(void)
{
	uint8_t value[LGPS_UBX_CFG_VALUE_MAX_LEN];
	uint8_t value_len = 0U;
	uint8_t u1 = 0U;

	/* A configuration read issued by the BLE command thread comes first. */
	if ((cfg_wait != 0) &&
	    (lgps_ubx_decode_valget(&parser, cfg_req.key, value, sizeof(value), &value_len) == 0)) {
		cfg_wait = 0;
		cfg_status = 0;
		cfg_result.key = cfg_req.key;
		cfg_result.len = value_len;
		cfg_result.value = cfg_value_assemble(value, value_len);
		cfg_req.op = CFG_OP_NONE;
		k_sem_give(&cfg_done_sem);
		return;
	}

	if (lgps_ubx_decode_valget_u1(&parser, LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1, &u1) != 0) {
		LOG_WRN("CFG-VALGET response without a key this firmware asked for "
			"(%u payload bytes)",
			(unsigned int)parser.payload_len);
		return;
	}

	LOG_INF("CFG-VALGET readback: NAV-PVT UART1 rate = %u (key 0x%08x)", u1,
		LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1);

	if (u1 == 0U) {
		nav_pvt_rate_verified = false;
		return;
	}

	if (!nav_pvt_rate_verified) {
		nav_pvt_rate_verified = true;
		LOG_INF("NAV-PVT output confirmed by the receiver, UBX configuration path works");
	}
}

static void gnss_process_rx(void)
{
	uint8_t byte;

	while (ring_buf_get(&gnss_rx_ring, &byte, 1) == 1U) {
		enum lgps_ubx_status status;

		nmea_feed(byte);
		status = lgps_ubx_parser_feed(&parser, byte);

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
		} else if (parser.msg_class == LGPS_UBX_CLASS_NAV &&
			   parser.msg_id == LGPS_UBX_ID_NAV_SAT) {
			struct lgps_nav_sat decoded[LGPS_GNSS_SAT_MAX];
			uint8_t count = 0U;

			if (lgps_ubx_decode_nav_sat(&parser, decoded, (uint8_t)ARRAY_SIZE(decoded),
						    &count) == 0) {
				k_spinlock_key_t key = k_spin_lock(&sat_lock);

				/* The UBX type is the decoder's; the cache holds the public
				 * struct, copied field by field so the two never alias.
				 */
				for (uint8_t index = 0U; index < count; index++) {
					sat_list[index].gnss_id = decoded[index].gnss_id;
					sat_list[index].sv_id = decoded[index].sv_id;
					sat_list[index].cno = decoded[index].cno;
					sat_list[index].elev = decoded[index].elev;
					sat_list[index].quality = decoded[index].quality;
					sat_list[index].used = decoded[index].used;
				}

				sat_count = count;
				sat_uptime_ms = lgps_diag_uptime_ms();
				k_spin_unlock(&sat_lock, key);
			}
		} else if (parser.msg_class == LGPS_UBX_CLASS_ACK) {
			handle_ubx_ack();
		} else if (parser.msg_class == LGPS_UBX_CLASS_CFG &&
			   parser.msg_id == LGPS_UBX_ID_CFG_VALGET) {
			handle_cfg_valget();
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

	/*
	 * The receiver always comes back at the rate its own flash holds, while the
	 * host may still be at the fast rate the profile opened (the baud rate is
	 * written to the RAM layer, which RXM-PMREQ standby clears). Park the host
	 * side first, while the receiver is still unpowered, so the two cannot
	 * disagree on the first byte.
	 */
	(void)gnss_uart_set_baud(LGPS_GNSS_BAUD_DEFAULT);
	baud_revert_done = false;

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
	 * bytes re-enters the parser mid-frame and shifts the first VALSET / NAV-PVT.
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

	/*
	 * The RAM layer is the only layer this firmware writes, and RXM-PMREQ clears
	 * it (integration manual section 3.6.3.2: "Entering the software standby mode
	 * clears the RAM memory including the receiver configuration"), so every power
	 * cycle starts with NAV-PVT off again and the readback has to be redone.
	 */
	nav_pvt_rate_verified = false;
	nav_pvt_rate_failure_logged = false;
	policy_hold_logged = false;
	/* RAM-layer outputs are gone after the standby request: re-assert them. */
	debug_applied_flags = 0U;
	{
		k_spinlock_key_t key = k_spin_lock(&sat_lock);

		sat_count = 0U;
		k_spin_unlock(&sat_lock, key);
	}
	next_reconfig_ms = 0U;
	baud_probe_base_bytes = rx_bytes_total();
	(void)gnss_enable_nav_pvt();
	(void)gnss_request_rate_readback();
	/* The bandwidth profile (NMEA off / rate / line rate) stays disarmed until a
	 * NAV-PVT readback proves the receiver answers at the boot rate; the cold-start
	 * state arms it. A no-op unless CONFIG_LGPS_GNSS_FAST_LINK is set.
	 */
	bringup_index = GNSS_BRINGUP_DONE;
	bringup_armed = false;

	LOG_INF("GNSS RX counters: rx_bytes=%u ubx=%u ckerr=%u nmea=%u ring_drop=%u baud=%u",
		rx_bytes_total(), ubx_frames, ubx_bad_checksum,
		(uint32_t)atomic_get(&isr_nmea_starts), (uint32_t)atomic_get(&isr_rx_dropped),
		current_baud);
}

/*
 * Next backoff step, i.e. what follows a wait of @p current_ms.
 *
 * The previous version stepped *up* before the first wait: backoff_ms starts at
 * 120 s, the first cut asked for the successor of 120 s and slept 300 s, so the
 * documented 120 -> 300 -> 900 sequence never started at 120. The wait is now
 * the current value and only the next attempt escalates.
 */
static uint32_t backoff_next_ms(uint32_t current_ms)
{
	if (current_ms < LGPS_GNSS_BACKOFF_STEP2_MS) {
		return LGPS_GNSS_BACKOFF_STEP2_MS;
	}

	if (current_ms < LGPS_GNSS_BACKOFF_STEP3_MS) {
		return LGPS_GNSS_BACKOFF_STEP3_MS;
	}

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

		gnss_service_debug();
		gnss_service_cfg();
		gnss_bringup_service();
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
			 * Arm the bandwidth profile once the receiver has proved that it answers
			 * at the boot rate. Arming at power-up instead would race the module's
			 * own 100 ms boot and silently drop the first profile step.
			 */
			if (nav_pvt_rate_verified && !bringup_armed) {
				bringup_armed = true;
				gnss_bringup_start();
			}
			/*
			 * A receiver that finished booting after our first VALSET ignores it and
			 * keeps streaming the factory NMEA set. Re-send every 2 s for the first
			 * 10 s and keep asking for the readback until the receiver reports a
			 * non-zero NAV-PVT rate.
			 *
			 * The retry condition used to be "no UBX frame yet", which stopped as
			 * soon as any ACK arrived - including an ACK-NAK, i.e. exactly the case
			 * where the configuration did not take.
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

			if (!nav_pvt_rate_verified && (elapsed < 10000U) &&
			    (lgps_diag_uptime_ms() >= next_reconfig_ms)) {
				next_reconfig_ms = lgps_diag_uptime_ms() + 2000U;
				(void)gnss_enable_nav_pvt();
				(void)gnss_request_rate_readback();
			}

			/* One-shot report: after 10 s of retrying, either the key is wrong for
			 * this product or the receiver never answered. Say so instead of
			 * letting the cold start run the remaining 50 s in silence.
			 *
			 * If the link was raised by the fast-link profile, the useful action is
			 * the revert, not the warning: the receiver kept streaming at the rate
			 * it knows, so dropping the host back to the boot rate restores it.
			 * Without this the link would stay dead until the next power cycle -
			 * and a CONFIG_LGPS_GNSS_ALWAYS_ON build never gets one.
			 */
			if (!nav_pvt_rate_verified && (elapsed >= 10000U) &&
			    (current_baud != LGPS_GNSS_BAUD_DEFAULT) && !baud_revert_done) {
				baud_revert_done = true;
				LOG_WRN("no NAV-PVT answer at %u baud, dropping the link back to %u",
					current_baud, LGPS_GNSS_BAUD_DEFAULT);
				(void)gnss_uart_set_baud(LGPS_GNSS_BAUD_DEFAULT);
				baud_probe_base_bytes = rx_bytes_total();
				/* Restart the window so the retry block above runs again. */
				set_state(LGPS_GNSS_STATE_COLD_START,
					  LGPS_GNSS_COLD_START_TIMEOUT_MS);
				break;
			}

			if (!nav_pvt_rate_verified && (elapsed >= 10000U) &&
			    !nav_pvt_rate_failure_logged) {
				nav_pvt_rate_failure_logged = true;
				LOG_WRN("NAV-PVT output rate never confirmed after 10 s: NAV-PVT stays off, "
					"only NMEA will be received (ubx=%u nmea=%u)",
					ubx_frames, (uint32_t)atomic_get(&isr_nmea_starts));
			}

			if (elapsed >= LGPS_GNSS_COLD_START_TIMEOUT_MS) {
				if (gnss_may_cut_power()) {
					LOG_WRN("no 3D fix within %u s, cutting the GNSS rail",
						LGPS_GNSS_COLD_START_TIMEOUT_MS / 1000U);
					gnss_power_down();
					set_state(LGPS_GNSS_STATE_BACKOFF, backoff_ms);
					backoff_ms = backoff_next_ms(backoff_ms);
				} else if (!policy_hold_logged) {
					/* Logging every 20 ms would bury the very fix this line is
					 * waiting for, hence the one-shot.
					 */
					policy_hold_logged = true;
					LOG_INF("cold-start window over, rail stays on (%s)",
						IS_ENABLED(CONFIG_LGPS_GNSS_ALWAYS_ON)
							? "CONFIG_LGPS_GNSS_ALWAYS_ON"
							: "debug session holds power");
				}
			}
			break;
		}

		case LGPS_GNSS_STATE_FIXED: {
			/* A solution resets the escalation: the next cut is a fresh 120 s. */
			backoff_ms = LGPS_GNSS_BACKOFF_STEP1_MS;

			/*
			 * With the policy disabled this state is terminal, so nothing else would
			 * notice a receiver that stopped talking: re-arm the one-shot
			 * configuration and start over. last_pvt_uptime_ms is set by every
			 * decoded NAV-PVT, valid or not, so this catches a silent link and not
			 * an indoor fix loss.
			 */
			if ((uint32_t)(lgps_diag_uptime_ms() - current_fix.last_pvt_uptime_ms) >
			    GNSS_PVT_LOST_MS) {
				LOG_WRN("no NAV-PVT for %u ms, re-arming the receiver configuration",
					GNSS_PVT_LOST_MS);
				nav_pvt_rate_verified = false;
				nav_pvt_rate_failure_logged = false;
				next_reconfig_ms = 0U;
				set_state(LGPS_GNSS_STATE_COLD_START,
					  LGPS_GNSS_COLD_START_TIMEOUT_MS);
				break;
			}

			if (gnss_may_cut_power()) {
				set_state(LGPS_GNSS_STATE_EPHEMERIS_HOLD,
					  LGPS_GNSS_EPHEMERIS_HOLD_MS);
			}
			break;
		}

		case LGPS_GNSS_STATE_EPHEMERIS_HOLD:
			if ((elapsed >= LGPS_GNSS_EPHEMERIS_HOLD_MS) && gnss_may_cut_power()) {
				LOG_INF("ephemeris hold done, powering the GNSS rail down");
				gnss_power_down();
				set_state(LGPS_GNSS_STATE_BACKOFF, backoff_ms);
				backoff_ms = backoff_next_ms(backoff_ms);
			}
			break;

		case LGPS_GNSS_STATE_BACKOFF:
			if (elapsed >= backoff_ms) {
				LOG_INF("backoff expired (%u ms), retrying GNSS", backoff_ms);
				gnss_power_up();
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
