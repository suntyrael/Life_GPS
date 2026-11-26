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

ZTEST_SUITE(lifegps_gnss_fsm, NULL, NULL, NULL, NULL, NULL);
