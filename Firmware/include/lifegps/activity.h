/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C activity classifier (stationary / walking / vehicle / indoor).
 */

#ifndef LIFEGPS_ACTIVITY_H_
#define LIFEGPS_ACTIVITY_H_

#include <stdbool.h>
#include <stdint.h>

/* Speed >= 3.5 m/s (12.6 km/h) without running cadence -> vehicle. */
#define LGPS_ACT_VEHICLE_SPEED_CMS 350U
/* GNSS fix stale > 30 s while moving -> likely indoors. */
#define LGPS_ACT_INDOOR_GNSS_AGE_MS 30000U
/* Recent step window for walking classification. */
#define LGPS_ACT_STEP_WINDOW_MS 5000U

enum lgps_activity_kind {
	LGPS_ACT_STATIONARY = 0,
	LGPS_ACT_WALKING = 1,
	LGPS_ACT_VEHICLE = 2,
};

struct lgps_activity_state {
	enum lgps_activity_kind kind;
	bool indoor;
	uint32_t last_step_ms;
	uint32_t last_steps_total;
	uint8_t recent_steps;
};

struct lgps_activity_inputs {
	uint32_t now_ms;
	uint32_t steps_total;
	bool zupt_locked;
	bool gnss_fix_valid;
	uint32_t gnss_age_ms;
	uint16_t gnss_speed_cms;
};

void lgps_activity_init(struct lgps_activity_state *act, uint32_t now_ms);

void lgps_activity_update(struct lgps_activity_state *act, const struct lgps_activity_inputs *in);

/* Build TrajectoryRecord_t.status_flags bits matching lifegps/traj.h (README 7.6). */
uint8_t lgps_activity_status_flags(const struct lgps_activity_state *act, bool fix_valid,
				   bool pdr_source, bool gnss_3d, bool zupt_locked,
				   bool mag_anomaly, bool baro_alt, bool poi);

#endif /* LIFEGPS_ACTIVITY_H_ */
