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

/* The thread decodes whole UBX lists on its own stack: NAV-SAT keeps 32 rows and
 * NAV-SIG up to LGPS_UBX_NAV_SIG_MAX (64) rows there before they are copied under
 * the satellite lock, so the 2048 the NAV-SAT-only firmware needed is no longer
 * enough.
 */
#define GNSS_THREAD_STACK 3072
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

/* Wait before retrying after a forced power cycle that repairs a split link. */
#define GNSS_LINK_RECOVERY_MS 500U

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
static struct k_spinlock fix_lock;
static K_SEM_DEFINE(gnss_wake_sem, 0, 1);
static enum lgps_gnss_state gnss_state = LGPS_GNSS_STATE_OFF;
static uint32_t state_since_ms;
static uint32_t last_pvt_log_ms;
/* One bad UTC must not turn into a WRN at the PVT rate; one line per 5 s. */
static uint32_t last_clock_reject_ms;
static uint32_t next_reconfig_ms;
static uint32_t current_baud = LGPS_GNSS_BAUD_DEFAULT;
static bool baud_fallback_done;

/* --- TTFF and receiver liveness -------------------------------------------- */

/*
 * Time to first fix, measured per power cycle. rail_on_ms is taken right after
 * GPS_EN goes high; rail_off_ms is taken when the rail drops, so the next cycle can
 * report how long the receiver was unpowered (that is what separates a hot start
 * from a cold one, together with the V_BCKP-backed almanac).
 */
static uint32_t rail_on_ms;
static uint32_t rail_off_ms;
static struct lgps_gnss_ttff ttff = { .ttff_ms = 0U, .power_off_ms = UINT32_MAX };
/* GNSS thread writes, main loop reads. */
static struct k_spinlock ttff_lock;

/*
 * Receiver liveness.
 *
 * A configuration write can leave the receiver mute in ways this firmware cannot
 * enumerate: a power save mode that switches the UART off between updates, a
 * signal combination that restarts the GNSS subsystem in a loop, a rate the module
 * accepted but cannot sustain. What they have in common is silence, so silence is
 * what is watched. Only used when the power policy is disabled, because otherwise
 * the policy itself already brings the receiver back.
 */
#define GNSS_LIVENESS_MS 20000U
#define GNSS_MAX_RECOVERIES 3U
static uint32_t ubx_last_rx_ms;
static uint32_t recovery_count;
static bool liveness_given_up;
/* A UBX-CFG-PM write asked for a power save mode: silence is then by design. */
static atomic_t psm_active;

/*
 * Last completed configuration writes, newest last. A wedge is almost always the
 * write just before the silence, and the serial log is the only witness left once
 * the link is gone.
 */
#define GNSS_CFG_TRAIL 4U
static struct {
	uint32_t key;
	uint32_t value;
	int status;
} cfg_trail[GNSS_CFG_TRAIL];
static uint8_t cfg_trail_next;
static struct k_spinlock cfg_trail_lock;

static void gnss_cfg_trail_record(uint32_t key, uint32_t value, int status)
{
	k_spinlock_key_t lock = k_spin_lock(&cfg_trail_lock);

	cfg_trail[cfg_trail_next].key = key;
	cfg_trail[cfg_trail_next].value = value;
	cfg_trail[cfg_trail_next].status = status;
	cfg_trail_next = (uint8_t)((cfg_trail_next + 1U) % GNSS_CFG_TRAIL);
	k_spin_unlock(&cfg_trail_lock, lock);
}


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
/* Newest UBX-NAV-SIG list, same producer. NAV-SIG names the signal behind each
 * C/N0 (L1/E1/B1I/B1C), which is what the App's CF column needs; the two lists are
 * joined in lgps_gnss_get_satellites() by (gnss_id, sv_id).
 */
static struct lgps_ubx_nav_sig sig_list[LGPS_UBX_NAV_SIG_MAX];
static uint8_t sig_count;
static uint32_t sig_uptime_ms;
/* The GNSS thread writes the list, the main loop copies it for the BLE frame. */
static struct k_spinlock sat_lock;
#define GNSS_SAT_STALE_MS 5000U

/*
 * Cold / warm / hot start request (BLE opcode 0x37).
 *
 * Same hand-over as the debug session: the BLE thread records the request, the
 * GNSS thread owns the UART and sends UBX-CFG-RST. `restart_at_ms` is the moment
 * the receiver is expected to be back on its UART at the boot rate, 0 = no
 * restart in flight.
 */
static struct k_spinlock restart_lock;
static bool restart_pending;
static uint8_t restart_mode;
static uint32_t restart_at_ms;

/* A controlled software reset keeps the receiver off its UART for about a second.
 * Interface description section 3.10.2.1 gives no number; the 100 ms boot delay in
 * gnss_power_up() is the floor, and a reset additionally restarts the GNSS
 * subsystem, so the wait is deliberately generous.
 */
#define GNSS_RESTART_SETTLE_MS 1500U

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
 *
 * The line rate step is the only one that is NOT confirmed by an ACK: the receiver
 * applies CFG-UART1-BAUDRATE as it builds the answer, so that ACK may already be
 * transmitted at the new rate and is then invisible to a host still listening at
 * 38400. Integration manual section 2.1.4 only says to "allow a short time delay
 * of typically 100 ms between sending a baud rate change message and providing
 * input data at the new rate". Waiting for an ACK there is what leaves the two
 * sides split - the receiver at 460800, the host at 38400 - so the step is sent
 * fire-and-forget, the host switches after the 100 ms margin, and the NAV-PVT
 * readback at the new rate is what proves it worked.
 */
struct gnss_bringup_step {
	uint32_t key;
	uint32_t value;
	uint8_t len;
	uint8_t layers;
	uint16_t gap_ms;
	bool switch_baud;
	bool wait_ack;
};

#if IS_ENABLED(CONFIG_LGPS_GNSS_FAST_LINK)
static const struct gnss_bringup_step bringup_profile[] = {
	/* NMEA off: one key for the whole protocol on UART1 (section 4.9.29). */
	{ LGPS_UBX_CFG_KEY_UART1OUTPROT_NMEA, 0U, 1U, LGPS_UBX_LAYER_RAM, 0U, false, true },
	/* Measurement period; BBR so it survives the standby request. */
	{ LGPS_UBX_CFG_KEY_RATE_MEAS, CONFIG_LGPS_GNSS_RATE_MS, 2U,
	  LGPS_UBX_LAYER_RAM | LGPS_UBX_LAYER_BBR, 0U, false, true },
#if IS_ENABLED(CONFIG_LGPS_GNSS_FOUR_GNSS)
	/*
	 * Four constellations (integration manual section 2.1.3): the factory set is
	 * GPS + Galileo + BeiDou B1I, and B1I cannot be received together with B1C or
	 * GLONASS L1OF. The order below is the only sequence in which every single
	 * write is a legal combination - B1I goes off first, then B1C comes on, then
	 * GLONASS. More satellites in view is the one lever that lowers h/v/pDOP
	 * without touching the hardware.
	 *
	 * Every CFG-SIGNAL write restarts the GNSS subsystem, so each step waits out
	 * the 0.5 s margin the manual asks for after the ACK. A receiver that NAKs one
	 * step keeps the combination it had; nothing is left half-switched.
	 */
	{ LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1_ENA, 0U, 1U, LGPS_UBX_LAYER_RAM, 600U, false, true },
	{ LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1C_ENA, 1U, 1U, LGPS_UBX_LAYER_RAM, 600U, false, true },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GLO_L1_ENA, 1U, 1U, LGPS_UBX_LAYER_RAM, 600U, false, true },
	{ LGPS_UBX_CFG_KEY_SIGNAL_GLO_ENA, 1U, 1U, LGPS_UBX_LAYER_RAM, 600U, false, true },
#endif
	/* Line rate last, RAM layer only, no ACK expected (see above). */
	{ LGPS_UBX_CFG_KEY_UART1_BAUDRATE, LGPS_GNSS_BAUD_FAST, 4U, LGPS_UBX_LAYER_RAM,
	  LGPS_GNSS_BAUD_SETTLE_MS, true, false },
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

/*
 * The receiver has been told to change its line rate at least once this power
 * cycle. From that moment on the two sides may disagree, and every verification
 * failure has to be repaired by a power cycle - the receiver's RAM layer holds
 * the new rate and cannot be talked out of it from a host at the old one.
 */
static bool fast_link_attempted;

/* The attempt failed once: stay at the boot rate until the next reboot instead of
 * flapping between the two rates every 10 s.
 */
static bool fast_link_broken;

/* Set by the profile or by the cold-start state, consumed by the thread loop. */
static bool recovery_needed;

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
	k_spinlock_key_t key;

	if (out == NULL) {
		return;
	}

	key = k_spin_lock(&fix_lock);
	*out = current_fix;
	k_spin_unlock(&fix_lock, key);
}

void lgps_gnss_get_ttff(struct lgps_gnss_ttff *out)
{
	k_spinlock_key_t key;

	if (out == NULL) {
		return;
	}

	key = k_spin_lock(&ttff_lock);
	*out = ttff;
	k_spin_unlock(&ttff_lock, key);
}

enum lgps_gnss_state lgps_gnss_get_state(void)
{
	return gnss_state;
}

static void gnss_uart_isr(const struct device *dev, void *user_data)
{
	bool got_bytes = false;

	ARG_UNUSED(user_data);

	if (uart_irq_update(dev) != 1) {
		return;
	}

	if (uart_irq_rx_ready(dev) != 1) {
		return;
	}

	for (;;) {
		uint8_t chunk[32];
		uint32_t stored;
		int read;

		read = uart_fifo_read(dev, chunk, sizeof(chunk));
		if (read <= 0) {
			break;
		}

		got_bytes = true;
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

	if (got_bytes) {
		k_sem_give(&gnss_wake_sem);
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
 * Message output rate of NAV-DOP, the only message that carries hDOP/vDOP.
 *
 * Same policy as NAV-SAT: RAM layer only, re-asserted after every power-up
 * because RXM-PMREQ clears the RAM configuration. 18 bytes per navigation epoch
 * is 180 B/s in the 10 Hz profile, well inside the 460800 link budget.
 */
static void gnss_apply_nav_dop_rate(bool enable)
{
	if (gnss_send_cfg_item(LGPS_UBX_CFG_KEY_MSGOUT_NAV_DOP_UART1, enable ? 1U : 0U, 1U,
			       LGPS_UBX_LAYER_RAM) != 0U) {
		LOG_INF("NAV-DOP output rate -> %u (RAM layer)", enable ? 1U : 0U);
	}
}

/*
 * Message output rate of NAV-SIG, the only message that names a satellite's signal.
 *
 * Same policy as NAV-SAT/NAV-DOP: RAM layer, re-asserted after every power-up.
 * 8 + 16*n bytes per epoch is ~500 B at 30 signals, i.e. 5 kB/s at 10 Hz - about
 * 11 % of the 460800 link, which is why the fast-link profile leaves room for it.
 */
static void gnss_apply_nav_sig_rate(bool enable)
{
	if (gnss_send_cfg_item(LGPS_UBX_CFG_KEY_MSGOUT_NAV_SIG_UART1, enable ? 1U : 0U, 1U,
			       LGPS_UBX_LAYER_RAM) != 0U) {
		LOG_INF("NAV-SIG output rate -> %u (RAM layer)", enable ? 1U : 0U);
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

static atomic_t policy_deep_sleep = ATOMIC_INIT(0);
static atomic_t policy_high_precision = ATOMIC_INIT(0);

void lgps_gnss_set_policy(bool deep_sleep, bool high_precision)
{
	atomic_set(&policy_deep_sleep, deep_sleep ? 1 : 0);
	atomic_set(&policy_high_precision, high_precision ? 1 : 0);
	k_sem_give(&gnss_wake_sem);
}

void lgps_gnss_force_off(void)
{
	atomic_set(&policy_deep_sleep, 1);
	atomic_set(&policy_high_precision, 0);
	(void)lgps_board_io_gnss_power(false);
}

/*
 * The single question the state machine is allowed to ask before dropping the
 * rail. Three independent sources can say no: CONFIG_LGPS_GNSS_ALWAYS_ON,
 * HIGH_PRECISION mode, and a live debug session holding power for its TTL.
 */
static bool gnss_may_cut_power(void)
{
	if (IS_ENABLED(CONFIG_LGPS_GNSS_ALWAYS_ON)) {
		return false;
	}

	if (atomic_get(&policy_high_precision) != 0) {
		return false;
	}

	return !gnss_debug_hold_power();
}

static void gnss_debug_apply_outputs(void)
{
	uint8_t wanted = gnss_debug_active() ? debug_flags : 0U;

	/* LGPS_GNSS_DEBUG_NAV_SAT is deliberately not applied any more: NAV-SAT is on
	 * for as long as the receiver is powered (gnss_power_up). The flag stays in
	 * the wire format so old App builds keep working, it just has nothing to do.
	 */

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

	/* Join the signal list onto the satellite rows.
	 *
	 * NAV-SAT has no signal id and NAV-SIG has no elevation/azimuth, so one row per
	 * satellite is all this firmware can build: when an SV tracks two signals (e.g.
	 * BeiDou B1I and B1C) the row carries the strongest one. The receiver is
	 * single-band (MAX-M10S: L1/E1/B1/G1), so a satellite never appears twice - a
	 * dual-frequency host would need the per-signal rows GPSTest lists instead.
	 */
	bool sig_fresh = (sig_count != 0U) &&
			 ((uint32_t)(now_ms - sig_uptime_ms) <= GNSS_SAT_STALE_MS);

	for (uint8_t index = 0U; index < count; index++) {
		out[index].sig_id = LGPS_GNSS_SAT_SIG_ID_UNKNOWN;

		if (!sig_fresh) {
			continue;
		}

		uint8_t best_cno = 0U;

		for (uint8_t sig = 0U; sig < sig_count; sig++) {
			if ((sig_list[sig].gnss_id != out[index].gnss_id) ||
			    (sig_list[sig].sv_id != out[index].sv_id)) {
				continue;
			}

			if ((out[index].sig_id == LGPS_GNSS_SAT_SIG_ID_UNKNOWN) ||
			    (sig_list[sig].cno > best_cno)) {
				best_cno = sig_list[sig].cno;
				out[index].sig_id = sig_list[sig].sig_id;
			}
		}
	}

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

	gnss_cfg_trail_record(key, value, ret);

	/* A power save mode is asked to be silent between updates, so the liveness
	 * watchdog must not read that as a dead receiver.
	 */
	if ((ret == 0) && (key == LGPS_UBX_CFG_KEY_PM_OPERATEMODE)) {
		atomic_set(&psm_active, (value != 0U) ? 1 : 0);
		LOG_INF("receiver power save mode -> %u (%s)", (unsigned int)value,
			(value != 0U) ? "the UART is silent between updates" : "continuous");
	}

	k_mutex_unlock(&cfg_req_lock);

	return (ret == -EAGAIN) ? -ETIMEDOUT : ret;
}

/*
 * Cold / warm / hot start, called from the BLE command thread (opcode 0x37).
 *
 * Nothing is written to the UART here: the GNSS thread owns it and the state
 * machine, so the request is latched and picked up by gnss_service_restart().
 * The answer is therefore "accepted", not "done" - the App shows the state that
 * follows (COLD_START) rather than pretending the receiver already restarted.
 */
int lgps_gnss_restart(enum lgps_gnss_start_mode mode)
{
	k_spinlock_key_t key;

	if ((unsigned int)mode > LGPS_GNSS_START_HOT) {
		return -EINVAL;
	}

	if ((gnss_state == LGPS_GNSS_STATE_OFF) || (gnss_state == LGPS_GNSS_STATE_BACKOFF)) {
		/* A receiver that is not powered has no backup memory to clear. */
		return -ENODEV;
	}

	key = k_spin_lock(&restart_lock);
	restart_pending = true;
	restart_mode = (uint8_t)mode;
	k_spin_unlock(&restart_lock, key);

	LOG_INF("GNSS restart requested: %s start",
		(mode == LGPS_GNSS_START_COLD) ? "cold"
					       : ((mode == LGPS_GNSS_START_WARM) ? "warm" : "hot"));

	return 0;
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

	/* A cold/warm/hot start owns the line rate until it has rebuilt the RAM layer
	 * (gnss_service_restart). A profile step in between would switch the host to
	 * 460800 for a receiver that is rebooting at 38400.
	 */
	if (restart_at_ms != 0U) {
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
		if (step->switch_baud && fast_link_broken) {
			LOG_WRN("line rate stayed at %u: the fast link failed earlier in this boot",
				LGPS_GNSS_BAUD_DEFAULT);
			bringup_index++;
			return;
		}

		if (gnss_send_cfg_item(step->key, step->value, step->len, step->layers) == 0U) {
			LOG_WRN("bring-up step %u (key 0x%08x) not sent, skipping",
				bringup_index, step->key);
			bringup_index++;
			return;
		}

		if (!step->wait_ack) {
			/* The ACK for a line rate change may already be sent at the new
			 * rate, so waiting for it would split the two sides for good.
			 */
			bringup_deadline_ms = now_ms + step->gap_ms;
			bringup_phase = BRINGUP_PHASE_SETTLE;
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
			/* From here on the receiver is at the new rate whether or not the
			 * host manages to follow.
			 */
			fast_link_attempted = true;

			if (gnss_uart_set_baud(step->value) != 0) {
				LOG_ERR("host UART cannot follow to %u: the receiver is already there, "
					"so the link is dead until it is power-cycled",
					step->value);
				fast_link_broken = true;
				recovery_needed = true;
				bringup_index++;
				bringup_phase = BRINGUP_PHASE_SEND;
				return;
			}

			LOG_INF("GNSS link raised to %u 8N1", step->value);
			/* Prove the new rate instead of assuming it: the NAV-PVT readback is
			 * the same test that validated the old one.
			 */
			baud_probe_base_bytes = rx_bytes_total();
			nav_pvt_rate_verified = false;
			next_reconfig_ms = 0U;
			(void)gnss_request_rate_readback();
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

	{
		k_spinlock_key_t key = k_spin_lock(&fix_lock);

		current_fix.valid = fix_valid;
		current_fix.utc_valid = utc_ok;
		current_fix.fix_type = pvt->fix_type;
		current_fix.satellites = pvt->num_sv;
		current_fix.pdop_1e2 =
			(fix_valid && (pvt->pdop_1e2 < 9999U)) ? pvt->pdop_1e2 : 0U;
		if (!fix_valid) {
			current_fix.hdop_1e2 = 0U;
			current_fix.vdop_1e2 = 0U;
		}
		current_fix.lat_e7 = pvt->lat_e7;
		current_fix.lon_e7 = pvt->lon_e7;
		current_fix.height_mm = pvt->height_mm;
		current_fix.hmsl_mm = pvt->hmsl_mm;
		current_fix.h_acc_mm = pvt->hacc_mm;
		current_fix.v_acc_mm = pvt->vacc_mm;
		current_fix.s_acc_mms = pvt->sacc_mms;
		current_fix.head_acc_1e5 = pvt->headacc_1e5;
		current_fix.speed_cms = (uint16_t)(pvt->gspeed_mms / 10);
		current_fix.heading_ddeg = (uint16_t)((pvt->head_mot_1e5 + 18000000) / 1000);
		if (current_fix.heading_ddeg >= 3600U) {
			current_fix.heading_ddeg = 0U;
		}
		current_fix.utc_epoch_s = epoch_s;
		current_fix.last_pvt_uptime_ms = now_ms;
		k_spin_unlock(&fix_lock, key);
	}

	/* Every valid UTC re-anchors the software clock; losing the fix later does
	 * not stop it. lgps_time_sync() rejects an implausible value (0, or more
	 * than ten years away from the running clock) and leaves the clock alone.
	 */
	if (utc_ok && !lgps_time_sync(epoch_s, now_ms)) {
		if ((last_clock_reject_ms == 0U) ||
		    ((uint32_t)(now_ms - last_clock_reject_ms) >= GNSS_PVT_LOG_PERIOD_MS)) {
			last_clock_reject_ms = now_ms;
			LOG_WRN("GNSS UTC %u rejected as clock anchor (implausible epoch "
				"or more than ten years from the running clock)",
				(unsigned int)epoch_s);
		}
	}

	/* TTFF of this power cycle, and a receiver that answers is not wedged. */
	if (fix_valid) {
		recovery_count = 0U;

		k_spinlock_key_t key = k_spin_lock(&ttff_lock);

		if (ttff.ttff_ms == 0U) {
			ttff.ttff_ms = now_ms - rail_on_ms;
		}
		k_spin_unlock(&ttff_lock, key);
	}

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

/*
 * UBX-NAV-DOP: hDOP and vDOP, the two numbers NAV-PVT does not carry.
 *
 * Written from the GNSS thread like every other fix field, so no lock is needed.
 * Both messages describe the same navigation epoch; gating on current_fix.valid
 * keeps one rule for every DOP the App shows - no fix, no DOP - no matter which
 * of the two messages lands first.
 */
static void handle_nav_dop(const struct lgps_ubx_nav_dop *dop)
{
	k_spinlock_key_t key = k_spin_lock(&fix_lock);

	if (!current_fix.valid || (dop->hdop_1e2 >= 9999U) || (dop->vdop_1e2 >= 9999U)) {
		current_fix.hdop_1e2 = 0U;
		current_fix.vdop_1e2 = 0U;
		k_spin_unlock(&fix_lock, key);
		return;
	}

	current_fix.hdop_1e2 = dop->hdop_1e2;
	current_fix.vdop_1e2 = dop->vdop_1e2;
	k_spin_unlock(&fix_lock, key);
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
		ubx_last_rx_ms = lgps_diag_uptime_ms();

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
					sat_list[index].azim = decoded[index].azim;
					sat_list[index].quality = decoded[index].quality;
					sat_list[index].used = decoded[index].used;
					sat_list[index].eph_avail = decoded[index].eph_avail;
					sat_list[index].alm_avail = decoded[index].alm_avail;
				}

				sat_count = count;
				sat_uptime_ms = lgps_diag_uptime_ms();
				k_spin_unlock(&sat_lock, key);
			}
		} else if (parser.msg_class == LGPS_UBX_CLASS_NAV &&
			   parser.msg_id == LGPS_UBX_ID_NAV_DOP) {
			struct lgps_ubx_nav_dop dop;

			if (lgps_ubx_decode_nav_dop(&parser, &dop) == 0) {
				handle_nav_dop(&dop);
			}
		} else if (parser.msg_class == LGPS_UBX_CLASS_NAV &&
			   parser.msg_id == LGPS_UBX_ID_NAV_SIG) {
			struct lgps_ubx_nav_sig decoded[LGPS_UBX_NAV_SIG_MAX];
			uint8_t count = 0U;

			if (lgps_ubx_decode_nav_sig(&parser, decoded, (uint8_t)ARRAY_SIZE(decoded),
						    &count) == 0) {
				k_spinlock_key_t key = k_spin_lock(&sat_lock);

				for (uint8_t index = 0U; index < count; index++) {
					sig_list[index] = decoded[index];
				}

				sig_count = count;
				sig_uptime_ms = lgps_diag_uptime_ms();
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

	rail_off_ms = lgps_diag_uptime_ms();

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
	fast_link_attempted = false;
	recovery_needed = false;

	ret = lgps_gnss_rail_set(true);
	if (ret < 0) {
		LOG_ERR("GNSS rail on failed (%d)", ret);
		return;
	}

	/* TTFF is measured from this instant, not from the first byte we receive. */
	rail_on_ms = lgps_diag_uptime_ms();
	ubx_last_rx_ms = rail_on_ms;
	{
		k_spinlock_key_t key = k_spin_lock(&ttff_lock);

		ttff.power_off_ms = (rail_off_ms == 0U) ? UINT32_MAX
							: (rail_on_ms - rail_off_ms);
		ttff.ttff_ms = 0U;
		k_spin_unlock(&ttff_lock, key);
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
		sig_count = 0U;
		k_spin_unlock(&sat_lock, key);
	}
	next_reconfig_ms = 0U;
	baud_probe_base_bytes = rx_bytes_total();
	(void)gnss_enable_nav_pvt();
	/*
	 * NAV-SAT runs for the whole time the receiver is powered, not only inside a
	 * debug session: the satellite table is the one view that says whether an
	 * antenna, a constellation switch or a sky view is the problem, and asking the
	 * user to start a session first just hides it. 16 + 12*n bytes per epoch is
	 * 400 bytes at 32 satellites, i.e. 400 B/s at 1 Hz and 4 kB/s in the 10 Hz
	 * profile - about 9 % of the 460800 link.
	 */
	gnss_apply_nav_sat_rate(true);
	/* hDOP/vDOP only exist in NAV-DOP; pDOP (NAV-PVT) cannot be split into them. */
	gnss_apply_nav_dop_rate(true);
	/* The signal id behind each C/N0 (CF column): NAV-SAT/NAV-SIG join. */
	gnss_apply_nav_sig_rate(true);
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

/* `navBbrMask` for one of the three start types (interface description 3.10.2.1). */
static uint16_t restart_bbr_mask(uint8_t mode)
{
	switch (mode) {
	case LGPS_GNSS_START_WARM:
		return LGPS_UBX_CFG_RST_BBR_WARM;
	case LGPS_GNSS_START_HOT:
		return LGPS_UBX_CFG_RST_BBR_HOT;
	case LGPS_GNSS_START_COLD:
	default:
		return LGPS_UBX_CFG_RST_BBR_COLD;
	}
}

static const char *restart_mode_name(uint8_t mode)
{
	switch (mode) {
	case LGPS_GNSS_START_WARM:
		return "warm";
	case LGPS_GNSS_START_HOT:
		return "hot";
	default:
		return "cold";
	}
}

/*
 * Cold / warm / hot start (BLE opcode 0x37), GNSS-thread side.
 *
 * Two phases, because a controlled software reset takes the receiver off its UART
 * for a while:
 *
 *   1. send UBX-CFG-RST, then park the host on the boot line rate - the reset
 *      throws away the RAM layer that held the 460800 rate;
 *   2. after GNSS_RESTART_SETTLE_MS rebuild everything that the cleared RAM layer
 *      used to hold: parser state, the satellite/signal lists and the message
 *      output rates. The bring-up profile re-arms itself once NAV-PVT is verified
 *      again, exactly as after a real power cycle.
 *
 * The receiver is not asked to acknowledge CFG-RST (section 3.10.2.1: newer
 * firmware does not), so the proof that it worked is the NAV-PVT readback below.
 */
static void gnss_service_restart(void)
{
	uint32_t now_ms = lgps_diag_uptime_ms();
	bool pending;
	uint8_t mode;
	k_spinlock_key_t key;

	key = k_spin_lock(&restart_lock);
	pending = restart_pending;
	mode = restart_mode;
	restart_pending = false;
	k_spin_unlock(&restart_lock, key);

	if (pending) {
		uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_RST_PAYLOAD_LEN +
			      LGPS_UBX_TRAILER_LEN];
		const uint16_t mask = restart_bbr_mask(mode);
		int len = lgps_ubx_build_cfg_rst(frame, sizeof(frame), mask,
						LGPS_UBX_CFG_RST_MODE_SOFTWARE);

		if (len < 0) {
			LOG_ERR("UBX-CFG-RST build failed (%d)", len);
		} else if (gnss_uart_send(frame, (size_t)len) < 0) {
			LOG_ERR("UBX-CFG-RST could not be sent");
		} else {
			/* The frame is 12 bytes and half a millisecond at 460800; the
			 * sleep is only there to let the TX FIFO drain before the host
			 * changes the rate under it.
			 */
			k_msleep(10);
			(void)gnss_uart_set_baud(LGPS_GNSS_BAUD_DEFAULT);
			restart_at_ms = now_ms + GNSS_RESTART_SETTLE_MS;
			LOG_INF("UBX-CFG-RST sent (navBbrMask 0x%04x): %s start, host stays at %u 8N1",
				(unsigned int)mask, restart_mode_name(mode),
				LGPS_GNSS_BAUD_DEFAULT);
		}
	}

	if ((restart_at_ms == 0U) || ((int32_t)(now_ms - restart_at_ms) < 0)) {
		return;
	}

	restart_at_ms = 0U;

	/* The receiver is back with its flash/BBR configuration. */
	nav_pvt_rate_verified = false;
	nav_pvt_rate_failure_logged = false;
	policy_hold_logged = false;
	fast_link_attempted = false;
	recovery_needed = false;
	recovery_count = 0U;
	debug_applied_flags = 0U;
	bringup_index = GNSS_BRINGUP_DONE;
	bringup_armed = false;
	next_reconfig_ms = 0U;
	ubx_last_rx_ms = now_ms;
	rail_on_ms = now_ms;

	{
		k_spinlock_key_t sat_key = k_spin_lock(&sat_lock);

		sat_count = 0U;
		sig_count = 0U;
		k_spin_unlock(&sat_lock, sat_key);
	}

	{
		k_spinlock_key_t ttff_key = k_spin_lock(&ttff_lock);

		/* TTFF of the start that just began. The rail was not cut, so
		 * power_off_ms becomes 0 - the start type (cold/warm/hot) is what the
		 * BLE command reported, not a measurement the host can re-derive.
		 */
		ttff.ttff_ms = 0U;
		ttff.power_off_ms = 0U;
		k_spin_unlock(&ttff_lock, ttff_key);
	}

	/* Whatever the old rate left half-parsed would shift the first answer. */
	ring_buf_reset(&gnss_rx_ring);
	lgps_ubx_parser_reset(&parser);

	(void)gnss_enable_nav_pvt();
	gnss_apply_nav_sat_rate(true);
	gnss_apply_nav_dop_rate(true);
	gnss_apply_nav_sig_rate(true);
	(void)gnss_request_rate_readback();

	LOG_INF("GNSS restarted: RAM-layer outputs re-asserted at %u 8N1",
		LGPS_GNSS_BAUD_DEFAULT);
	set_state(LGPS_GNSS_STATE_COLD_START, LGPS_GNSS_COLD_START_TIMEOUT_MS);
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

/* --- receiver rescue -------------------------------------------------------- */

/*
 * Keys this firmware can save into BBR/Flash and that can outlive a power cycle in
 * a way that silences the receiver. Deleting them makes the next configuration load
 * fall back to the factory default.
 *
 * The signal entries belong here because the factory profile (GPS + Galileo +
 * BeiDou B1I) is a legal combination while a hand-built one may not be, and
 * CFG-VALDEL does not validate the resulting configuration at all (interface
 * description section 3.10.3.1). CFG-RATE-MEAS is deleted so a rate the receiver
 * accepted but cannot sustain does not survive either; the power-up profile writes
 * it again.
 */
static const uint32_t rescue_keys[] = {
	LGPS_UBX_CFG_KEY_PM_OPERATEMODE,
	LGPS_UBX_CFG_KEY_PM_POSUPDATEPERIOD,
	LGPS_UBX_CFG_KEY_SIGNAL_GLO_ENA,
	LGPS_UBX_CFG_KEY_SIGNAL_GLO_L1_ENA,
	LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1_ENA,
	LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1C_ENA,
	LGPS_UBX_CFG_KEY_RATE_MEAS,
};

BUILD_ASSERT(ARRAY_SIZE(rescue_keys) <= LGPS_UBX_CFG_VALDEL_MAX_KEYS,
	     "the rescue list must fit one UBX-CFG-VALDEL frame");

/* Newest last, oldest first. Logging happens outside the lock on purpose. */
static void gnss_log_cfg_trail(void)
{
	struct {
		uint32_t key;
		uint32_t value;
		int status;
	} copy[GNSS_CFG_TRAIL];
	k_spinlock_key_t lock = k_spin_lock(&cfg_trail_lock);

	memcpy(copy, cfg_trail, sizeof(copy));
	k_spin_unlock(&cfg_trail_lock, lock);

	for (uint8_t index = 0U; index < GNSS_CFG_TRAIL; index++) {
		uint8_t slot = (uint8_t)((cfg_trail_next + index) % GNSS_CFG_TRAIL);

		if (copy[slot].key == 0U) {
			continue;
		}

		LOG_WRN("  recent cfg: key 0x%08x = %u -> %d", copy[slot].key,
			(unsigned int)copy[slot].value, copy[slot].status);
	}
}

/*
 * Ask the receiver to forget the saved values that can silence it. Best effort: it
 * is sent to a receiver that has already stopped answering, so it usually only
 * takes effect once the receiver is alive enough to parse a frame - which is
 * exactly the power save mode case, where the module listens during its ON window
 * even though it emits nothing in between.
 */
static void gnss_clear_saved_config(void)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN +
		     (LGPS_UBX_CFG_VALDEL_MAX_KEYS * 4U) + LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build_valdel(frame, sizeof(frame),
				    LGPS_UBX_LAYER_BBR | LGPS_UBX_LAYER_FLASH, rescue_keys,
				    (uint8_t)ARRAY_SIZE(rescue_keys));
	if (ret < 0) {
		LOG_ERR("UBX-CFG-VALDEL build failed (%d)", ret);
		return;
	}

	(void)gnss_uart_send(frame, (size_t)ret);
	LOG_WRN("sent UBX-CFG-VALDEL for %u saved key(s): the next configuration load uses "
		"the factory defaults for them",
		(unsigned int)ARRAY_SIZE(rescue_keys));
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

		/*
		 * Repair a link the two sides disagree about: the receiver accepted a line
		 * rate the host could not follow, or nothing answered after both sides were
		 * switched. Only dropping the rail clears the RAM layer that holds the new
		 * rate, so this is the one recovery that always works.
		 */
		if (recovery_needed) {
			recovery_needed = false;
			LOG_WRN("recovering the GNSS link: rescuing the receiver and power-cycling it "
				"so both sides return to %u 8N1",
				LGPS_GNSS_BAUD_DEFAULT);
			gnss_log_cfg_trail();
			gnss_clear_saved_config();
			gnss_power_down();
			backoff_ms = GNSS_LINK_RECOVERY_MS;
			set_state(LGPS_GNSS_STATE_BACKOFF, GNSS_LINK_RECOVERY_MS);
		}

		gnss_service_debug();
		gnss_service_cfg();
		gnss_service_restart();
		gnss_bringup_service();
		gnss_process_rx();
		lgps_wdt_checkin(LGPS_WDT_TASK_GNSS);

		/*
		 * Liveness. Every failure mode this firmware cannot enumerate - a power save
		 * mode that mutes the UART, a signal combination the receiver restarts on in
		 * a loop, a rate it accepted but cannot sustain - ends in the same symptom:
		 * no complete UBX frame. Only watched when the power policy is off, because
		 * otherwise the policy itself brings the receiver back on its own.
		 */
		if (IS_ENABLED(CONFIG_LGPS_GNSS_ALWAYS_ON) && (atomic_get(&psm_active) == 0) &&
		    !recovery_needed && (gnss_state != LGPS_GNSS_STATE_OFF) &&
		    (gnss_state != LGPS_GNSS_STATE_BACKOFF) &&
		    ((uint32_t)(lgps_diag_uptime_ms() - ubx_last_rx_ms) > GNSS_LIVENESS_MS)) {
			if (recovery_count < GNSS_MAX_RECOVERIES) {
				recovery_count++;
				LOG_WRN("no UBX frame for %u ms in state %s: the receiver is not "
					"answering, rescue %u/%u",
					GNSS_LIVENESS_MS, lgps_gnss_state_name(gnss_state),
					recovery_count, GNSS_MAX_RECOVERIES);
				recovery_needed = true;
			} else if (!liveness_given_up) {
				liveness_given_up = true;
				LOG_ERR("the receiver stayed silent through %u rescues; leaving it alone",
					GNSS_MAX_RECOVERIES);
			}
		}

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
				/* Re-asserted with the same retry: the first VALSET goes out 100 ms
				 * after the rail, which can be before the module is listening.
				 */
				gnss_apply_nav_sat_rate(true);
				gnss_apply_nav_dop_rate(true);
				gnss_apply_nav_sig_rate(true);
				(void)gnss_request_rate_readback();
			}

			/* One-shot report: after 10 s of retrying, either the key is wrong for
			 * this product or the receiver never answered. Say so instead of
			 * letting the cold start run the remaining 50 s in silence.
			 *
			 * If the line rate was changed this power cycle, the only repair is a power
			 * cycle of the receiver: the new rate lives in its RAM layer, so a host that
			 * simply goes back to the boot rate is still talking to a receiver that does
			 * not answer it. Dropping the rail clears that RAM, which is exactly what
			 * the recovery below does - a CONFIG_LGPS_GNSS_ALWAYS_ON build never gets such
			 * a power cycle on its own.
			 */
			if (!nav_pvt_rate_verified && (elapsed >= 10000U) && fast_link_attempted) {
				LOG_WRN("no NAV-PVT answer at %u baud although both sides were switched",
					current_baud);
				fast_link_broken = true;
				recovery_needed = true;
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
			if (atomic_get(&policy_deep_sleep) != 0 && gnss_may_cut_power()) {
				set_state(LGPS_GNSS_STATE_OFF, 0U);
			} else if (elapsed >= backoff_ms) {
				LOG_INF("backoff expired (%u ms), retrying GNSS", backoff_ms);
				gnss_power_up();
				set_state(LGPS_GNSS_STATE_COLD_START,
					  LGPS_GNSS_COLD_START_TIMEOUT_MS);
			}
			break;

		case LGPS_GNSS_STATE_OFF:
			if (atomic_get(&policy_deep_sleep) == 0) {
				LOG_INF("leaving deep sleep, powering GNSS rail up");
				gnss_power_up();
				set_state(LGPS_GNSS_STATE_COLD_START,
					  LGPS_GNSS_COLD_START_TIMEOUT_MS);
			}
			break;
		default:
			break;
		}

		if ((atomic_get(&policy_deep_sleep) != 0) && gnss_may_cut_power() &&
		    (gnss_state != LGPS_GNSS_STATE_OFF)) {
			LOG_INF("deep sleep requested, powering GNSS rail down");
			gnss_power_down();
			set_state(LGPS_GNSS_STATE_OFF, 0U);
		}

		{
			uint32_t wait_ms = ((gnss_state == LGPS_GNSS_STATE_OFF) ||
					    (gnss_state == LGPS_GNSS_STATE_BACKOFF))
						   ? 500U
						   : 50U;

			(void)k_sem_take(&gnss_wake_sem, K_MSEC(wait_ms));
		}
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
