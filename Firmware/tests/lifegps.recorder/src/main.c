/* SPDX-License-Identifier: MIT */
#include <zephyr/ztest.h>
#include <lifegps/recorder.h>

ZTEST_SUITE(recorder_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(recorder_tests, test_cadence_turn_floor_and_poi)
{
	struct lgps_recorder rec;
	struct lgps_recorder_sample s = {
		.now_ms = 1000U,
		.power_state = LGPS_PWR_ACTIVE,
		.activity = LGPS_ACT_WALKING,
		.heading_ddeg = 900U,
		.floor_level = 0,
		.force_poi = false,
	};

	lgps_recorder_init(&rec, 42U);

	/* First sample always records. */
	zassert_true(lgps_recorder_should_record(&rec, &s));
	zassert_equal(lgps_recorder_commit(&rec, &s), 42U);

	/* 5 s later, straight walk -> not yet (10 s cadence). */
	s.now_ms = 6000U;
	zassert_false(lgps_recorder_should_record(&rec, &s));

	/* 35 deg heading turn after >= 2 s -> immediate trigger. */
	s.heading_ddeg = 1250U;
	zassert_true(lgps_recorder_should_record(&rec, &s));
	zassert_equal(lgps_recorder_commit(&rec, &s), 43U);

	/* Stationary -> 60 s cadence. */
	s.activity = LGPS_ACT_STATIONARY;
	s.now_ms = 40000U;
	zassert_false(lgps_recorder_should_record(&rec, &s));
	s.now_ms = 66000U;
	zassert_true(lgps_recorder_should_record(&rec, &s));
	zassert_equal(lgps_recorder_commit(&rec, &s), 44U);

	/* Floor change -> immediate trigger. */
	s.now_ms = 67000U;
	s.floor_level = 1;
	zassert_true(lgps_recorder_should_record(&rec, &s));
	zassert_equal(lgps_recorder_commit(&rec, &s), 45U);

	/* DEEP_SLEEP skips periodic writes, except explicit POI. */
	s.power_state = LGPS_PWR_DEEP_SLEEP;
	s.now_ms = 200000U;
	zassert_false(lgps_recorder_should_record(&rec, &s));
	s.force_poi = true;
	zassert_true(lgps_recorder_should_record(&rec, &s));
	zassert_equal(lgps_recorder_commit(&rec, &s), 46U);
	zassert_equal(lgps_recorder_last_seq(&rec), 46U);
}
