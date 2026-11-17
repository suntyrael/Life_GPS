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

struct lgps_gnss_fix {
	bool valid;          /* fixType >= 3 and gnssFixOK */
	bool utc_valid;
	uint8_t fix_type;
	uint8_t satellites;
	uint16_t hdop_1e2;
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

/* Latest decoded solution (copy, safe to call from any thread). */
void lgps_gnss_get_fix(struct lgps_gnss_fix *out);

enum lgps_gnss_state lgps_gnss_get_state(void);

const char *lgps_gnss_state_name(enum lgps_gnss_state state);

#endif /* LIFEGPS_GNSS_H_ */
