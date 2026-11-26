/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C activity classifier.
 */

#include <stddef.h>
#include <string.h>

#include <lifegps/activity.h>
#include <lifegps/traj.h>

void lgps_activity_init(struct lgps_activity_state *act, uint32_t now_ms)
{
	if (act == NULL) {
		return;
	}

	memset(act, 0, sizeof(*act));
	act->kind = LGPS_ACT_STATIONARY;
	act->last_step_ms = now_ms;
}

void lgps_activity_update(struct lgps_activity_state *act, const struct lgps_activity_inputs *in)
{
	uint32_t delta_steps;
	bool stepping_recently;

	if ((act == NULL) || (in == NULL)) {
		return;
	}

	delta_steps = in->steps_total - act->last_steps_total;
	if (delta_steps > 0U) {
		uint32_t sum = (uint32_t)act->recent_steps + delta_steps;

		act->recent_steps = (sum > 255U) ? 255U : (uint8_t)sum;
		act->last_steps_total = in->steps_total;
		act->last_step_ms = in->now_ms;
	} else if ((uint32_t)(in->now_ms - act->last_step_ms) >= LGPS_ACT_STEP_WINDOW_MS) {
		act->recent_steps = 0U;
	}

	stepping_recently = (act->recent_steps > 0U) &&
			    ((uint32_t)(in->now_ms - act->last_step_ms) < LGPS_ACT_STEP_WINDOW_MS);

	if (in->gnss_fix_valid && (in->gnss_speed_cms >= LGPS_ACT_VEHICLE_SPEED_CMS) &&
	    (act->recent_steps < 6U)) {
		act->kind = LGPS_ACT_VEHICLE;
	} else if (stepping_recently) {
		act->kind = LGPS_ACT_WALKING;
	} else if (in->zupt_locked) {
		act->kind = LGPS_ACT_STATIONARY;
	} else if (in->gnss_fix_valid && (in->gnss_speed_cms >= 80U)) {
		act->kind = LGPS_ACT_WALKING;
	} else {
		act->kind = LGPS_ACT_STATIONARY;
	}

	act->indoor = (!in->gnss_fix_valid) && (in->gnss_age_ms >= LGPS_ACT_INDOOR_GNSS_AGE_MS);
}

uint8_t lgps_activity_status_flags(const struct lgps_activity_state *act, bool fix_valid,
				   bool pdr_source, bool gnss_3d, bool zupt_locked,
				   bool mag_anomaly, bool baro_alt, bool poi)
{
	uint8_t flags = 0U;

	if (fix_valid) {
		flags |= LGPS_FLAG_FIX_VALID;
	}
	if (pdr_source) {
		flags |= LGPS_FLAG_NAV_SRC;
	}
	if (poi) {
		flags |= LGPS_FLAG_POI_EVENT;
	}
	if (gnss_3d) {
		flags |= LGPS_FLAG_GNSS_TYPE;
	}
	if ((act != NULL) && (act->kind == LGPS_ACT_VEHICLE)) {
		flags |= LGPS_FLAG_VEHICLE_MODE;
	}
	if (zupt_locked) {
		flags |= LGPS_FLAG_STN_LOCK;
	}
	if (mag_anomaly) {
		flags |= LGPS_FLAG_MAG_ANOMALY;
	}
	if (baro_alt) {
		flags |= LGPS_FLAG_ALT_SRC;
	}

	return flags;
}
