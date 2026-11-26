/* SPDX-License-Identifier: MIT */
#include <zephyr/ztest.h>
#include <lifegps/activity.h>
#include <lifegps/traj.h>

ZTEST_SUITE(activity_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(activity_tests, test_stationary_walk_vehicle_and_flags)
{
	struct lgps_activity_state act;
	struct lgps_activity_inputs in = {
		.now_ms = 1000U,
		.steps_total = 0U,
		.zupt_locked = true,
		.gnss_fix_valid = true,
		.gnss_age_ms = 500U,
		.gnss_speed_cms = 0U,
	};

	lgps_activity_init(&act, 0U);
	lgps_activity_update(&act, &in);
	zassert_equal(act.kind, LGPS_ACT_STATIONARY);
	zassert_false(act.indoor);

	/* Walking with recent steps indoors (GNSS stale > 30 s). */
	in.now_ms = 4000U;
	in.steps_total = 5U;
	in.zupt_locked = false;
	in.gnss_fix_valid = false;
	in.gnss_age_ms = 35000U;
	lgps_activity_update(&act, &in);
	zassert_equal(act.kind, LGPS_ACT_WALKING);
	zassert_true(act.indoor);

	/* Vehicle: high GNSS speed (> 3.5 m/s) with no steps. */
	in.now_ms = 12000U;
	in.steps_total = 5U;
	in.gnss_fix_valid = true;
	in.gnss_age_ms = 200U;
	in.gnss_speed_cms = 1200U;
	lgps_activity_update(&act, &in);
	zassert_equal(act.kind, LGPS_ACT_VEHICLE);
	zassert_false(act.indoor);

	uint8_t flags = lgps_activity_status_flags(&act, true, false, true, false, true, true, true);

	zassert_true((flags & LGPS_FLAG_FIX_VALID) != 0U);
	zassert_true((flags & LGPS_FLAG_VEHICLE_MODE) != 0U);
	zassert_true((flags & LGPS_FLAG_POI_EVENT) != 0U);
	zassert_true((flags & LGPS_FLAG_MAG_ANOMALY) != 0U);
	zassert_true((flags & LGPS_FLAG_ALT_SRC) != 0U);
}
