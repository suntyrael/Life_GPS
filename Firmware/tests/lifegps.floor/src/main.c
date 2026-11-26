/* SPDX-License-Identifier: MIT */
#include <zephyr/ztest.h>
#include <lifegps/floor.h>

ZTEST_SUITE(floor_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(floor_tests, test_hysteresis_stairs_and_elevator)
{
	struct lgps_floor_detector det;

	lgps_floor_init(&det);
	lgps_floor_reset_outdoor(&det, 1000, 0U);
	zassert_equal(lgps_floor_level(&det), 0);

	/* +2.0 m is below threshold (1.5 + 0.8 = 2.3 m), stays floor 0. */
	zassert_false(lgps_floor_update(&det, 3000, 4000U));
	zassert_equal(lgps_floor_level(&det), 0);

	/* +2.4 m crosses threshold -> floor +1. */
	zassert_true(lgps_floor_update(&det, 3400, 5000U));
	zassert_equal(lgps_floor_level(&det), 1);

	/* Small drop back to +2.8 m stays floor +1 due to hysteresis. */
	zassert_false(lgps_floor_update(&det, 3800, 6000U));
	zassert_equal(lgps_floor_level(&det), 1);

	/* Elevator ascent: +12 m over several 1 s steps -> high speed + floor +5. */
	for (uint32_t s = 1U; s <= 6U; s++) {
		(void)lgps_floor_update(&det, 3800 + (int32_t)(s * 2000U), 6000U + (s * 1000U));
	}
	zassert_equal(lgps_floor_level(&det), 5);
	zassert_equal(lgps_floor_motion(&det), LGPS_VERT_ELEVATOR);

	/* Outdoor reset brings floor back to 0. */
	lgps_floor_reset_outdoor(&det, 15800, 20000U);
	zassert_equal(lgps_floor_level(&det), 0);
}
