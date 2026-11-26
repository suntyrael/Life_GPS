/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C 4-state power state machine logic.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <lifegps/power_sm_logic.h>

static bool soc_known(uint8_t soc_pct)
{
	return soc_pct <= 100U;
}

void lgps_power_logic_init(struct lgps_power_sm_logic *sm, uint32_t now_ms)
{
	if (sm == NULL) {
		return;
	}

	memset(sm, 0, sizeof(*sm));
	sm->state = LGPS_PWR_ACTIVE;
	sm->auto_sleep_ms = LGPS_PWR_DEFAULT_AUTO_SLEEP_MS;
	sm->zupt_since_ms = now_ms;
}

void lgps_power_logic_set_auto_sleep_ms(struct lgps_power_sm_logic *sm, uint32_t delay_ms)
{
	if (sm == NULL) {
		return;
	}

	sm->auto_sleep_ms = delay_ms;
}

void lgps_power_logic_request_shutdown(struct lgps_power_sm_logic *sm)
{
	if (sm == NULL) {
		return;
	}

	sm->state = LGPS_PWR_SYSTEM_OFF;
	sm->wake_confirming = false;
	sm->system_off_requested = true;
}

bool lgps_power_logic_on_motion_irq(struct lgps_power_sm_logic *sm, uint32_t now_ms,
				    uint32_t steps_total)
{
	if (sm == NULL) {
		return false;
	}

	if (sm->state != LGPS_PWR_DEEP_SLEEP) {
		return false;
	}

	sm->state = LGPS_PWR_ACTIVE;
	sm->wake_confirming = true;
	sm->wake_started_ms = now_ms;
	sm->wake_steps_base = steps_total;
	sm->zupt_tracking = false;

	return true;
}

bool lgps_power_logic_toggle_precision(struct lgps_power_sm_logic *sm, uint32_t now_ms,
				       uint8_t soc_pct, bool charging)
{
	if ((sm == NULL) || (sm->state == LGPS_PWR_SYSTEM_OFF)) {
		return false;
	}

	if (sm->state == LGPS_PWR_HIGH_PRECISION) {
		sm->state = LGPS_PWR_ACTIVE;
		sm->wake_confirming = false;
		sm->zupt_tracking = false;
		sm->zupt_since_ms = now_ms;
		return true;
	}

	if (!charging && soc_known(soc_pct) && (soc_pct <= LGPS_PWR_LOW_BATT_DEGRADE_SOC)) {
		return false;
	}

	sm->state = LGPS_PWR_HIGH_PRECISION;
	sm->wake_confirming = false;
	sm->zupt_tracking = false;
	return true;
}

int lgps_power_logic_set_mode(struct lgps_power_sm_logic *sm, enum lgps_power_state target,
			      uint32_t now_ms, uint8_t soc_pct, bool charging)
{
	if (sm == NULL) {
		return -EINVAL;
	}

	if (sm->state == LGPS_PWR_SYSTEM_OFF) {
		return -EPERM;
	}

	switch (target) {
	case LGPS_PWR_DEEP_SLEEP:
		sm->state = LGPS_PWR_DEEP_SLEEP;
		sm->wake_confirming = false;
		sm->zupt_tracking = false;
		return 0;

	case LGPS_PWR_ACTIVE:
		sm->state = LGPS_PWR_ACTIVE;
		sm->wake_confirming = false;
		sm->zupt_tracking = false;
		sm->zupt_since_ms = now_ms;
		return 0;

	case LGPS_PWR_HIGH_PRECISION:
		if (!charging && soc_known(soc_pct) && (soc_pct <= LGPS_PWR_LOW_BATT_DEGRADE_SOC)) {
			return -EPERM;
		}
		sm->state = LGPS_PWR_HIGH_PRECISION;
		sm->wake_confirming = false;
		sm->zupt_tracking = false;
		return 0;

	case LGPS_PWR_SYSTEM_OFF:
		lgps_power_logic_request_shutdown(sm);
		return 0;

	default:
		return -EINVAL;
	}
}

bool lgps_power_logic_step(struct lgps_power_sm_logic *sm, const struct lgps_power_inputs *in)
{
	enum lgps_power_state prev;

	if ((sm == NULL) || (in == NULL)) {
		return false;
	}

	if (sm->state == LGPS_PWR_SYSTEM_OFF) {
		return false;
	}

	prev = sm->state;

	/* Critical battery protection: clean shutdown when not charging. */
	if (!in->charging && soc_known(in->soc_pct) && (in->soc_pct <= LGPS_PWR_CRIT_BATT_OFF_SOC)) {
		lgps_power_logic_request_shutdown(sm);
		return sm->state != prev;
	}

	/* Low battery protection: degrade HIGH_PRECISION -> ACTIVE. */
	if ((sm->state == LGPS_PWR_HIGH_PRECISION) && !in->charging && soc_known(in->soc_pct) &&
	    (in->soc_pct <= LGPS_PWR_LOW_BATT_DEGRADE_SOC)) {
		sm->state = LGPS_PWR_ACTIVE;
		sm->zupt_tracking = false;
		sm->zupt_since_ms = in->now_ms;
		return true;
	}

	/* Motion wake confirmation gate: require >= 2 steps within 3 s, else back to DEEP_SLEEP. */
	if ((sm->state == LGPS_PWR_ACTIVE) && sm->wake_confirming) {
		uint32_t delta_steps = in->steps_total - sm->wake_steps_base;

		if (in->ble_connected || (delta_steps >= LGPS_PWR_WAKE_CONFIRM_STEPS)) {
			sm->wake_confirming = false;
			sm->zupt_tracking = false;
		} else if ((uint32_t)(in->now_ms - sm->wake_started_ms) >= LGPS_PWR_WAKE_CONFIRM_MS) {
			sm->wake_confirming = false;
			sm->state = LGPS_PWR_DEEP_SLEEP;
			return true;
		}
	}

	/* Auto-sleep from ACTIVE after sustained ZUPT lock when BLE is idle. */
	if ((sm->state == LGPS_PWR_ACTIVE) && !sm->wake_confirming && (sm->auto_sleep_ms > 0U)) {
		if (in->zupt_locked && !in->ble_connected) {
			if (!sm->zupt_tracking) {
				sm->zupt_tracking = true;
				sm->zupt_since_ms = in->now_ms;
			} else if ((uint32_t)(in->now_ms - sm->zupt_since_ms) >= sm->auto_sleep_ms) {
				sm->state = LGPS_PWR_DEEP_SLEEP;
				sm->zupt_tracking = false;
				return true;
			}
		} else {
			sm->zupt_tracking = false;
		}
	}

	return sm->state != prev;
}
