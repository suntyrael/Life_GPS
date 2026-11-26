/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C GNSS six-state power policy machine.
 */

#include <stddef.h>
#include <string.h>

#include <lifegps/gnss_fsm.h>

static bool ephemeris_fresh(const struct lgps_gnss_fsm *fsm, uint32_t now_ms)
{
	if (!fsm->ephemeris_valid) {
		return false;
	}

	return (uint32_t)(now_ms - fsm->last_fix_ms) < LGPS_GNSS_FSM_EPHEMERIS_TTL_MS;
}

static uint32_t backoff_for_streak(uint8_t fail_streak)
{
	if (fail_streak <= 1U) {
		return LGPS_GNSS_FSM_BACKOFF1_MS;
	}

	if (fail_streak == 2U) {
		return LGPS_GNSS_FSM_BACKOFF2_MS;
	}

	return LGPS_GNSS_FSM_BACKOFF3_MS;
}

uint32_t lgps_gnss_fsm_hot_timeout_ms(const struct lgps_gnss_fsm *fsm)
{
	if (fsm == NULL) {
		return LGPS_GNSS_FSM_HOT_STEP1_MS;
	}

	if (fsm->fail_streak == 0U) {
		return LGPS_GNSS_FSM_HOT_STEP1_MS;
	}

	if (fsm->fail_streak == 1U) {
		return LGPS_GNSS_FSM_HOT_STEP2_MS;
	}

	return LGPS_GNSS_FSM_HOT_MAX_MS;
}

void lgps_gnss_fsm_init(struct lgps_gnss_fsm *fsm, uint32_t now_ms)
{
	if (fsm == NULL) {
		return;
	}

	memset(fsm, 0, sizeof(*fsm));
	fsm->state = LGPS_GNSS_FSM_OFF;
	fsm->state_since_ms = now_ms;
}

void lgps_gnss_fsm_note_cold_restart(struct lgps_gnss_fsm *fsm, uint32_t now_ms)
{
	if (fsm == NULL) {
		return;
	}

	fsm->ephemeris_valid = false;
	fsm->fail_streak = 0U;
	fsm->state = LGPS_GNSS_FSM_COLD_ACQ;
	fsm->state_since_ms = now_ms;
}

static enum lgps_gnss_fsm_action enter_acquisition(struct lgps_gnss_fsm *fsm, uint32_t now_ms,
						   bool high_precision)
{
	enum lgps_gnss_fsm_action action =
		fsm->rail_on ? LGPS_GNSS_FSM_ACTION_NONE : LGPS_GNSS_FSM_ACTION_POWER_UP;

	fsm->rail_on = true;
	fsm->state_since_ms = now_ms;

	if (high_precision) {
		fsm->state = LGPS_GNSS_FSM_TRACKING;
	} else if (ephemeris_fresh(fsm, now_ms)) {
		fsm->state = LGPS_GNSS_FSM_HOT_ACQ;
	} else {
		fsm->state = LGPS_GNSS_FSM_COLD_ACQ;
	}

	return action;
}

static enum lgps_gnss_fsm_action enter_sleep(struct lgps_gnss_fsm *fsm, uint32_t now_ms,
					     uint32_t wait_ms)
{
	enum lgps_gnss_fsm_action action =
		fsm->rail_on ? LGPS_GNSS_FSM_ACTION_POWER_DOWN : LGPS_GNSS_FSM_ACTION_NONE;

	fsm->rail_on = false;
	fsm->state = LGPS_GNSS_FSM_BACKOFF;
	fsm->state_since_ms = now_ms;
	fsm->next_wait_ms = wait_ms;

	return action;
}

enum lgps_gnss_fsm_action lgps_gnss_fsm_step(struct lgps_gnss_fsm *fsm,
					     const struct lgps_gnss_fsm_input *in)
{
	uint32_t elapsed_ms;

	if ((fsm == NULL) || (in == NULL)) {
		return LGPS_GNSS_FSM_ACTION_NONE;
	}

	elapsed_ms = (uint32_t)(in->now_ms - fsm->state_since_ms);

	/* Explicit high-precision continuous tracking or debug power hold keeps rail on. */
	if (in->high_precision || in->hold_power) {
		if (!fsm->rail_on) {
			return enter_acquisition(fsm, in->now_ms, in->high_precision);
		}
		if (in->fix_valid) {
			fsm->last_fix_ms = in->now_ms;
			fsm->fail_streak = 0U;
		}
		if (in->high_precision) {
			fsm->state = LGPS_GNSS_FSM_TRACKING;
		}
		return LGPS_GNSS_FSM_ACTION_NONE;
	}

	switch (fsm->state) {
	case LGPS_GNSS_FSM_OFF:
		if (in->zupt_locked && !in->wake_trigger) {
			return LGPS_GNSS_FSM_ACTION_NONE;
		}
		return enter_acquisition(fsm, in->now_ms, false);

	case LGPS_GNSS_FSM_COLD_ACQ:
		if (in->fix_valid) {
			fsm->last_fix_ms = in->now_ms;
			fsm->fail_streak = 0U;
			fsm->state = LGPS_GNSS_FSM_EPHEMERIS_HOLD;
			fsm->state_since_ms = in->now_ms;
			return LGPS_GNSS_FSM_ACTION_NONE;
		}
		if (elapsed_ms >= LGPS_GNSS_FSM_COLD_TIMEOUT_MS) {
			if (fsm->fail_streak < 255U) {
				fsm->fail_streak++;
			}
			return enter_sleep(fsm, in->now_ms, backoff_for_streak(fsm->fail_streak));
		}
		return LGPS_GNSS_FSM_ACTION_NONE;

	case LGPS_GNSS_FSM_EPHEMERIS_HOLD:
		if (in->fix_valid) {
			fsm->last_fix_ms = in->now_ms;
		}
		if (elapsed_ms >= LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS) {
			fsm->ephemeris_valid = true;
			fsm->fail_streak = 0U;
			return enter_sleep(fsm, in->now_ms,
					   in->vehicle_mode ? LGPS_GNSS_FSM_VEHICLE_PERIOD_MS
							    : LGPS_GNSS_FSM_PULSE_PERIOD_MS);
		}
		return LGPS_GNSS_FSM_ACTION_NONE;

	case LGPS_GNSS_FSM_HOT_ACQ:
		if (in->fix_valid) {
			fsm->last_fix_ms = in->now_ms;
			fsm->fail_streak = 0U;
			return enter_sleep(fsm, in->now_ms,
					   in->vehicle_mode ? LGPS_GNSS_FSM_VEHICLE_PERIOD_MS
							    : LGPS_GNSS_FSM_PULSE_PERIOD_MS);
		}
		if (elapsed_ms >= lgps_gnss_fsm_hot_timeout_ms(fsm)) {
			if (fsm->fail_streak < 255U) {
				fsm->fail_streak++;
			}
			return enter_sleep(fsm, in->now_ms, backoff_for_streak(fsm->fail_streak));
		}
		return LGPS_GNSS_FSM_ACTION_NONE;

	case LGPS_GNSS_FSM_TRACKING:
		/* Left high_precision / hold_power: return to normal pulse sleep. */
		if (in->fix_valid) {
			fsm->last_fix_ms = in->now_ms;
			fsm->ephemeris_valid = true;
		}
		return enter_sleep(fsm, in->now_ms,
				   in->vehicle_mode ? LGPS_GNSS_FSM_VEHICLE_PERIOD_MS
						    : LGPS_GNSS_FSM_PULSE_PERIOD_MS);

	case LGPS_GNSS_FSM_BACKOFF:
		if (in->wake_trigger) {
			fsm->fail_streak = 0U;
			return enter_acquisition(fsm, in->now_ms, false);
		}
		if (in->zupt_locked) {
			/* Stay asleep while stationary; zero periodic GNSS pulses. */
			return LGPS_GNSS_FSM_ACTION_NONE;
		}
		if (elapsed_ms >= fsm->next_wait_ms) {
			return enter_acquisition(fsm, in->now_ms, false);
		}
		return LGPS_GNSS_FSM_ACTION_NONE;

	default:
		fsm->state = LGPS_GNSS_FSM_OFF;
		return LGPS_GNSS_FSM_ACTION_NONE;
	}
}
