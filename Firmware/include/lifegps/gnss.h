/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - GNSS (u-blox MAX-M10S default part) power and fix state.
 *
 * Timing policy comes from README section 11 phase 2 (firmware roadmap):
 *   cold start window 60 s, then keep the module powered for 35 s after the first
 *   fix so it can collect broadcast ephemeris, then cut the rail and retry with
 *   the exponential backoff 120 s -> 300 s -> 900 s.
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
 * current-speed matches it; 9600 is the only other rate worth trying before
 * declaring the link broken.
 */
#define LGPS_GNSS_BAUD_DEFAULT 38400U
#define LGPS_GNSS_BAUD_FALLBACK 9600U

#define LGPS_GNSS_COLD_START_TIMEOUT_MS 60000U
#define LGPS_GNSS_EPHEMERIS_HOLD_MS 35000U
#define LGPS_GNSS_NOMINAL_PERIOD_MS 45000U
#define LGPS_GNSS_HOT_TIMEOUT_MS 15000U
#define LGPS_GNSS_BACKOFF_STEP1_MS 120000U
#define LGPS_GNSS_BACKOFF_STEP2_MS 300000U
#define LGPS_GNSS_BACKOFF_STEP3_MS 900000U

enum lgps_gnss_state {
	LGPS_GNSS_STATE_OFF = 0,
	LGPS_GNSS_STATE_COLD_START,
	LGPS_GNSS_STATE_FIXED,
	LGPS_GNSS_STATE_EPHEMERIS_HOLD,
	LGPS_GNSS_STATE_SLEEP_NOMINAL,
	LGPS_GNSS_STATE_HOT_PULSE,
	LGPS_GNSS_STATE_BACKOFF,
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
};

/*
 * Receive-path counters. They answer the question the fix log cannot: is the
 * receiver silent (rx_bytes == 0), talking NMEA only, or sending corrupt UBX?
 * Each field is either ISR-written (atomic) or GNSS-thread-written, so the copy
 * below has no data race; values from different fields may be one frame apart.
 */
void lgps_gnss_get_counters(struct lgps_gnss_counters *out);

/* Latest decoded solution (copy, safe to call from any thread). */
void lgps_gnss_get_fix(struct lgps_gnss_fix *out);

enum lgps_gnss_state lgps_gnss_get_state(void);

const char *lgps_gnss_state_name(enum lgps_gnss_state state);

/* Request an immediate hot-start wake (e.g. KEY1 POI pressed while GNSS is asleep). */
void lgps_gnss_request_immediate(void);

/* Enable or disable continuous high-precision mode (KEY2 toggle). */
void lgps_gnss_set_continuous(bool enable);

/* Inform the GNSS state machine whether ZUPT has locked the device as stationary. */
void lgps_gnss_set_stationary(bool stationary);

#endif /* LIFEGPS_GNSS_H_ */
