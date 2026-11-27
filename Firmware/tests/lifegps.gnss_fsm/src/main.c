/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - GNSS six-state power policy unit tests (native_sim).
 */

#include <zephyr/ztest.h>

#include <lifegps/gnss_fsm.h>

ZTEST(lifegps_gnss_fsm, test_cold_start_ephemeris_then_hot_pulse)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };
	enum lgps_gnss_fsm_action act;

	lgps_gnss_fsm_init(&fsm, 0U);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_OFF, "starts OFF");

	/* Moving from boot -> powers up into COLD_ACQ. */
	in.now_ms = 1000U;
	act = lgps_gnss_fsm_step(&fsm, &in);
	zassert_equal(act, LGPS_GNSS_FSM_ACTION_POWER_UP, "must power up");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_COLD_ACQ, "first start is cold");

	/* Fix arrives at 20 s -> enters 35 s EPHEMERIS_HOLD. */
	in.now_ms = 21000U;
	in.fix_valid = true;
	act = lgps_gnss_fsm_step(&fsm, &in);
	zassert_equal(act, LGPS_GNSS_FSM_ACTION_NONE, "keep rail on for ephemeris");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_EPHEMERIS_HOLD, "ephemeris hold");

	/* 35 s later -> ephemeris valid, cuts rail for 60 s pulse sleep. */
	in.now_ms = 21000U + LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS;
	act = lgps_gnss_fsm_step(&fsm, &in);
	zassert_equal(act, LGPS_GNSS_FSM_ACTION_POWER_DOWN, "cut rail after ephemeris");
	zassert_true(fsm.ephemeris_valid, "ephemeris marked valid");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_BACKOFF, "sleeping between pulses");
	zassert_equal(fsm.next_wait_ms, LGPS_GNSS_FSM_PULSE_PERIOD_MS, "60 s pulse wait");

	/* Next pulse wakes into HOT_ACQ (3.5 s window), fix locks in 2 s -> immediate sleep. */
	in.fix_valid = false;
	in.now_ms += LGPS_GNSS_FSM_PULSE_PERIOD_MS;
	act = lgps_gnss_fsm_step(&fsm, &in);
	zassert_equal(act, LGPS_GNSS_FSM_ACTION_POWER_UP, "wake for hot pulse");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_HOT_ACQ, "hot acquisition with fresh ephemeris");
	zassert_equal(lgps_gnss_fsm_hot_timeout_ms(&fsm), LGPS_GNSS_FSM_HOT_STEP1_MS,
		      "3.5 s first hot window");

	in.now_ms += 2000U;
	in.fix_valid = true;
	act = lgps_gnss_fsm_step(&fsm, &in);
	zassert_equal(act, LGPS_GNSS_FSM_ACTION_POWER_DOWN, "immediate cut on hot fix");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_BACKOFF, "back to sleep");
}

ZTEST(lifegps_gnss_fsm, test_hot_escalation_and_exponential_backoff)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };
	uint32_t t = 0U;

	lgps_gnss_fsm_init(&fsm, t);
	fsm.ephemeris_valid = true;
	fsm.last_fix_ms = t;

	/* Fail #1: 3.5 s timeout -> 120 s backoff. */
	t += 1000U;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(lgps_gnss_fsm_hot_timeout_ms(&fsm), LGPS_GNSS_FSM_HOT_STEP1_MS);

	t += LGPS_GNSS_FSM_HOT_STEP1_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, LGPS_GNSS_FSM_BACKOFF1_MS, "120 s backoff");

	/* Fail #2: 8 s timeout -> 300 s backoff. */
	t += LGPS_GNSS_FSM_BACKOFF1_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(lgps_gnss_fsm_hot_timeout_ms(&fsm), LGPS_GNSS_FSM_HOT_STEP2_MS);

	t += LGPS_GNSS_FSM_HOT_STEP2_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, LGPS_GNSS_FSM_BACKOFF2_MS, "300 s backoff");

	/* Fail #3: 15 s timeout -> 900 s backoff. */
	t += LGPS_GNSS_FSM_BACKOFF2_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(lgps_gnss_fsm_hot_timeout_ms(&fsm), LGPS_GNSS_FSM_HOT_MAX_MS);

	t += LGPS_GNSS_FSM_HOT_MAX_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, LGPS_GNSS_FSM_BACKOFF3_MS, "900 s backoff");

	/* Explicit wake trigger resets fail streak and immediately powers up. */
	t += 5000U;
	in.now_ms = t;
	in.wake_trigger = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.fail_streak, 0U, "wake trigger resets fail streak");
}

ZTEST(lifegps_gnss_fsm, test_zupt_pause_and_vehicle_period)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };

	lgps_gnss_fsm_init(&fsm, 0U);
	fsm.ephemeris_valid = true;
	fsm.last_fix_ms = 0U;

	/* Stationary at boot stays OFF. */
	in.now_ms = 10000U;
	in.zupt_locked = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_NONE);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_OFF, "ZUPT holds GNSS off");

	/* Vehicle mode after fix sleeps 180 s. */
	in.zupt_locked = false;
	in.vehicle_mode = true;
	in.now_ms = 20000U;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	in.now_ms = 22000U;
	in.fix_valid = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, LGPS_GNSS_FSM_VEHICLE_PERIOD_MS, "180 s vehicle pulse");
}

ZTEST(lifegps_gnss_fsm, test_deep_sleep_cuts_rail_and_holds_off)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };

	lgps_gnss_fsm_init(&fsm, 0U);

	/* Boot into COLD_ACQ with rail on. */
	in.now_ms = 1000U;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_true(fsm.rail_on, "rail powered");

	/* Deep sleep cuts rail immediately and transitions to OFF. */
	in.now_ms = 5000U;
	in.deep_sleep = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_false(fsm.rail_on, "rail cut in deep sleep");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_OFF, "deep sleep holds OFF");

	/* Stays OFF while deep_sleep is active, even if wake_trigger is raised. */
	in.now_ms = 65000U;
	in.wake_trigger = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_NONE);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_OFF, "still OFF during deep sleep");
	zassert_false(fsm.rail_on, "rail remains off");

	/* high_precision overrides deep_sleep and powers back up into TRACKING. */
	in.now_ms = 70000U;
	in.wake_trigger = false;
	in.high_precision = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_TRACKING, "high_precision overrides deep_sleep");

	/* Dropping high_precision while deep_sleep is still set cuts rail immediately. */
	in.now_ms = 75000U;
	in.high_precision = false;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_OFF, "back to OFF when deep_sleep active");

	/* Clearing deep_sleep allows normal acquisition to resume. */
	in.now_ms = 80000U;
	in.deep_sleep = false;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_COLD_ACQ, "resumes acquisition after deep sleep");
}

ZTEST(lifegps_gnss_fsm, test_cold_timeout_and_pulse_period_overrides)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };
	uint32_t t = 0U;

	lgps_gnss_fsm_init(&fsm, t);

	/* Custom 20 s cold timeout cuts rail at 20 s instead of 60 s. */
	in.cold_timeout_ms = 20000U;
	in.pulse_period_ms = 45000U;
	t = 1000U;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_COLD_ACQ);

	t += 19999U;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_NONE,
		      "still acquiring before custom cold timeout");

	t += 1U;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN,
		      "custom 20 s cold timeout expires");
	zassert_equal(fsm.state, LGPS_GNSS_FSM_BACKOFF);

	/* Wake via trigger, acquire fix, hold ephemeris -> custom 45 s pulse period. */
	t += 1000U;
	in.now_ms = t;
	in.wake_trigger = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	in.wake_trigger = false;

	t += 5000U;
	in.now_ms = t;
	in.fix_valid = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_NONE);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_EPHEMERIS_HOLD);

	t += LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, 45000U, "custom 45 s pulse sleep");

	/* Hot fix also honours custom 45 s pulse period. */
	in.fix_valid = false;
	t += 45000U;
	in.now_ms = t;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_HOT_ACQ);

	t += 1500U;
	in.now_ms = t;
	in.fix_valid = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.next_wait_ms, 45000U, "hot fix uses custom 45 s pulse period");
}

ZTEST(lifegps_gnss_fsm, test_next_wake_ms_reporting)
{
	struct lgps_gnss_fsm fsm;
	struct lgps_gnss_fsm_input in = { 0 };

	zassert_equal(lgps_gnss_fsm_next_wake_ms(NULL, 0U), UINT32_MAX, "NULL returns UINT32_MAX");

	lgps_gnss_fsm_init(&fsm, 0U);
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, 500U), UINT32_MAX,
		      "OFF returns UINT32_MAX");

	/* COLD_ACQ with custom 30 s timeout. */
	in.now_ms = 1000U;
	in.cold_timeout_ms = 30000U;
	in.pulse_period_ms = 45000U;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, 11000U), 20000U,
		      "20 s left in custom cold window");
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, 40000U), 0U, "expired cold window -> 0");

	/* EPHEMERIS_HOLD. */
	in.now_ms = 10000U;
	in.fix_valid = true;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_NONE);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_EPHEMERIS_HOLD);
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, 20000U),
		      LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS - 10000U, "remaining ephemeris hold");

	/* BACKOFF finite & expired & infinite. */
	in.now_ms = 10000U + LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_DOWN);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_BACKOFF);
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, in.now_ms + 15000U), 30000U,
		      "30 s left in 45 s pulse sleep");
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, in.now_ms + 50000U), 0U,
		      "expired backoff -> 0");

	fsm.next_wait_ms = UINT32_MAX;
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, in.now_ms + 50000U), UINT32_MAX,
		      "infinite backoff returns UINT32_MAX");

	/* TRACKING has no timeout. */
	in.high_precision = true;
	in.now_ms += 60000U;
	zassert_equal(lgps_gnss_fsm_step(&fsm, &in), LGPS_GNSS_FSM_ACTION_POWER_UP);
	zassert_equal(fsm.state, LGPS_GNSS_FSM_TRACKING);
	zassert_equal(lgps_gnss_fsm_next_wake_ms(&fsm, in.now_ms + 1000U), UINT32_MAX,
		      "TRACKING returns UINT32_MAX");
}

ZTEST_SUITE(lifegps_gnss_fsm, NULL, NULL, NULL, NULL, NULL);
