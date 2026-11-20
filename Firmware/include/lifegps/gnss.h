/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - GNSS (u-blox MAX-M10S default part) power and fix state.
 *
 * Timing policy comes from README section 11 phase 2 (firmware roadmap):
 *   cold start window 60 s, then keep the module powered for 35 s after the first
 *   fix so it can collect broadcast ephemeris, then cut the rail and retry with
 *   the exponential backoff 120 s -> 300 s -> 900 s.
 *
 * That policy is a build option: CONFIG_LGPS_GNSS_ALWAYS_ON (boards/lifegps/
 * Kconfig.lifegps) removes both cuts from src/gnss/gnss.c and leaves the receiver
 * tracking continuously - for bench work, or for a full multi-GNSS session that is
 * longer than the debug session TTL below.
 *
 * Electrical order for power-down is fixed by the pinmap (section 3): put the
 * receiver into software backup first, park the UART pins, then drop GPS_EN.
 */

#ifndef LIFEGPS_GNSS_H_
#define LIFEGPS_GNSS_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Line rate of the receiver. The u-blox M10 factory default is 38400 8N1
 * (interface description, CFG-UART1-BAUDRATE 0x40520001), and the devicetree
 * current-speed matches it; 9600 is the other rate the MAX-M10S ships with
 * (integration manual section 2.1.4: the MAX family is delivered at 9600 for
 * backwards compatibility), and it is the only other rate worth trying before
 * declaring the link broken.
 *
 * 38400 stays the rate every power cycle starts at, on both sides. A 10 Hz
 * profile does not fit it, so CONFIG_LGPS_GNSS_FAST_LINK raises the link to
 * LGPS_GNSS_BAUD_FAST after the receiver has answered at the slow rate, and the
 * next power-up starts at 38400 again (the baud rate is written to the RAM layer
 * only, and RXM-PMREQ standby clears RAM).
 *
 * 460800 is inside the MAX-M10S list (integration manual table 16: 4800 ...
 * 921600) and inside the nRF54L15 UARTE limit (datasheet section 11.24:
 * fUARTE = 1000 kbps for the UARTE20-22/30 instances; the BAUDRATE table value
 * 0x07400000 yields 457143 bit/s from a 16 MHz PCLK, i.e. -0.8 %).
 */
#define LGPS_GNSS_BAUD_DEFAULT 38400U
#define LGPS_GNSS_BAUD_FALLBACK 9600U
#define LGPS_GNSS_BAUD_FAST 460800U

/*
 * The rate change is fire-and-forget on purpose: the receiver applies
 * CFG-UART1-BAUDRATE while it builds the ACK, so the ACK may already be sent at
 * the new rate and is invisible to a host still at the old one. The host switches
 * after LGPS_GNSS_BAUD_SETTLE_MS and then proves the rate with a NAV-PVT readback.
 * A failed proof is repaired by power-cycling the receiver, because the new rate
 * lives in its RAM layer and only RXM-PMREQ standby or a rail drop clears that.
 */

#define LGPS_GNSS_COLD_START_TIMEOUT_MS 60000U
#define LGPS_GNSS_EPHEMERIS_HOLD_MS 35000U
#define LGPS_GNSS_BACKOFF_STEP1_MS 120000U
#define LGPS_GNSS_BACKOFF_STEP2_MS 300000U
#define LGPS_GNSS_BACKOFF_STEP3_MS 900000U

enum lgps_gnss_state {
	LGPS_GNSS_STATE_OFF = 0,
	LGPS_GNSS_STATE_COLD_START,
	LGPS_GNSS_STATE_FIXED,
	LGPS_GNSS_STATE_EPHEMERIS_HOLD,
	LGPS_GNSS_STATE_BACKOFF,
};

/*
 * GNSS debug session (BLE opcode 0x36).
 *
 * The normal power policy cuts the rail 35 s after the first fix and retries
 * minutes later, which makes the receiver impossible to watch. A debug session
 * suspends that policy for a bounded time and can switch the raw NMEA stream on.
 *
 * LGPS_GNSS_DEBUG_NAV_SAT is kept in the wire format but does nothing since 0.3.0:
 * NAV-SAT is an output of the receiver for as long as it is powered, because the
 * satellite table is what tells an antenna problem from a sky-view problem and
 * should not need a session to exist first.
 */
#define LGPS_GNSS_DEBUG_HOLD_POWER 0x01U
#define LGPS_GNSS_DEBUG_NAV_SAT 0x02U
#define LGPS_GNSS_DEBUG_NMEA 0x04U
#define LGPS_GNSS_DEBUG_FLAGS_ALL                                                             \
	(LGPS_GNSS_DEBUG_HOLD_POWER | LGPS_GNSS_DEBUG_NAV_SAT | LGPS_GNSS_DEBUG_NMEA)

#define LGPS_GNSS_DEBUG_DEFAULT_TTL_S 300U
#define LGPS_GNSS_DEBUG_MAX_TTL_S 3600U

/*
 * Settle time after a UBX-ACK before the next command, for the configuration
 * groups that restart the GNSS subsystem.
 *
 * Integration manual section 2.1.3 and interface description section 4.9.21:
 * "Any change to the signal configuration items triggers a restart of the GNSS
 * subsystem. During the restart, the host application should wait for message
 * acknowledgement and a margin of 0.5 seconds prior to sending any further
 * commands." A VALGET issued into that restart is either not answered or
 * answered with ACK-NAK, which the App then reports as "not supported".
 */
#define LGPS_GNSS_CFG_SIGNAL_SETTLE_MS 500U

/* Gap between a baud rate change and the first byte at the new rate (integration
 * manual section 3.2.1: "a short time delay of typically 100 ms").
 */
#define LGPS_GNSS_BAUD_SETTLE_MS 100U

/* Largest satellite list the firmware caches (UBX-NAV-SAT, section 3.15.13). */
#define LGPS_GNSS_SAT_MAX 32U

/* One satellite for the App table (UBX-NAV-SAT, interface description 3.15.13). */
struct lgps_gnss_sat {
	uint8_t gnss_id;
	uint8_t sv_id;
	uint8_t cno;
	int8_t elev;
	uint8_t quality;
	bool used;
};

/* A whitelisted receiver configuration item with its current value. */
struct lgps_gnss_cfg_value {
	uint32_t key;
	uint8_t len;   /* value width in bytes: 1, 2 or 4 */
	uint32_t value; /* little-endian bytes assembled into a u32 */
};

struct lgps_gnss_fix {
	/* fixType 3 (3D) or 4 (GNSS + DR) with gnssFixOK set. fixType 5 is time-only:
	 * the receiver reports it without a position, so it must not lock a fix.
	 */
	bool valid;
	bool utc_valid;
	uint8_t fix_type;
	uint8_t satellites;
	/* NAV-PVT carries pDOP, not hDOP (hDOP only exists in UBX-NAV-DOP). */
	uint16_t pdop_1e2;
	int32_t lat_e7;
	int32_t lon_e7;
	int32_t hmsl_mm;
	/* NAV-PVT vAcc: 1-sigma vertical accuracy estimate in mm. The barometric
	 * calibration uses it to refuse a height that is noisier than the sensor it
	 * is supposed to calibrate (see lifegps/baro.h).
	 */
	uint32_t v_acc_mm;
	uint16_t speed_cms;
	uint16_t heading_ddeg;
	uint32_t utc_epoch_s; /* Life_GPS epoch, 0 when unknown */
	uint32_t last_pvt_uptime_ms;
};

/* Open the UART, register the interrupt handler and start the state machine. */
int lgps_gnss_init(void);

struct lgps_gnss_counters {
	uint32_t rx_bytes;        /* bytes read from UARTE20 since boot */
	uint32_t ubx_frames;      /* complete UBX frames with a valid checksum */
	uint32_t ubx_bad_checksum;
	uint32_t nmea_starts;     /* '$' characters: proves the module is talking NMEA */
	uint32_t rx_dropped;      /* bytes the ISR had to drop because the ring was full */
};

/*
 * Receive-path counters. They answer the question the fix log cannot: is the
 * receiver silent (rx_bytes == 0), talking NMEA only, or sending corrupt UBX?
 * Each field is either ISR-written (atomic) or GNSS-thread-written, so the copy
 * below has no data race; values from different fields may be one frame apart.
 */
void lgps_gnss_get_counters(struct lgps_gnss_counters *out);

/*
 * Copy the newest satellite list (at most @p max entries) and return the number
 * of entries. Returns 0 when the list is stale (older than 5 s), so the caller
 * never renders a table that stops updating without saying so.
 */
uint8_t lgps_gnss_get_satellites(struct lgps_gnss_sat *out, uint8_t max);

/*
 * Debug session control (called from the BLE command thread).
 *
 * @p flags is a mask of LGPS_GNSS_DEBUG_*; @p ttl_s == 0 selects the default.
 * The request is handed to the GNSS thread, which owns the UART and applies the
 * extra outputs, so this returns immediately. A session always has a bounded
 * lifetime: no BLE command can leave the rail on after the TTL. The only way to
 * keep the receiver powered without an expiry is the build-time switch
 * CONFIG_LGPS_GNSS_ALWAYS_ON, which is deliberately not reachable from here.
 */
int lgps_gnss_debug_set(uint8_t flags, uint16_t ttl_s);

/* Current flags and the remaining lifetime in seconds (0 = no session). */
void lgps_gnss_debug_get(uint8_t *flags, uint16_t *remaining_s);

/*
 * Pull one captured NMEA sentence (ASCII, "$..." with the checksum, no CR/LF).
 *
 * Returns the length written to @p out, or 0 when no sentence is queued. Only
 * fills while the debug session has the LGPS_GNSS_DEBUG_NMEA flag set; the text
 * never goes through the log backend, because a 1 Hz NMEA burst would starve both
 * the UART console and the BLE log ring.
 */
size_t lgps_gnss_pop_nmea(char *out, size_t capacity);

/*
 * Read one whitelisted configuration item from the receiver (RAM layer).
 *
 * Blocks up to about 800 ms while the GNSS thread performs CFG-VALGET, because
 * the request and its answer share the UART with the navigation stream.
 * Returns 0 on success, -ENODEV when the module is not powered, -EINVAL for a key
 * that is not whitelisted, -ETIMEDOUT when the receiver did not answer within the
 * window and -ENOTSUP when the receiver rejected the key (ACK-NAK).
 */
int lgps_gnss_cfg_get(uint32_t key, struct lgps_gnss_cfg_value *out);

/*
 * Write one whitelisted configuration item and read it back.
 *
 * The value goes to the RAM and BBR layers, so it survives the RXM-PMREQ power
 * down (the integration manual notes that software standby clears RAM); with
 * @p persist_to_flash the flash layer is written as well, which costs an erase
 * cycle and is therefore never implicit. @p readback receives what the receiver
 * reports afterwards - the caller must display that, not the value it sent.
 */
int lgps_gnss_cfg_set(uint32_t key, uint32_t value, bool persist_to_flash,
		      struct lgps_gnss_cfg_value *readback);

/* Latest decoded solution (copy, safe to call from any thread). */
void lgps_gnss_get_fix(struct lgps_gnss_fix *out);

/* Line rate the UART is configured for right now (38400, or 9600 after the
 * one-shot fallback). The value is what the driver really reports, not a
 * constant, so the App can show a rate that is actually in effect.
 */
uint32_t lgps_gnss_baud(void);

enum lgps_gnss_state lgps_gnss_get_state(void);

const char *lgps_gnss_state_name(enum lgps_gnss_state state);

#endif /* LIFEGPS_GNSS_H_ */
