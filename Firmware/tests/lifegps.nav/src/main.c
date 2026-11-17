/*
 * Life_GPS firmware - ztest coverage for the attitude / ZUPT / PDR maths.
 *
 * Feeds synthetic IMU and magnetometer data; no device tree, no drivers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <lifegps/nav.h>

#include <stdint.h>

#define TEST_TOLERANCE 1e-6F
#define TEST_SAMPLE_PERIOD_MS 50U /* 20 Hz, README 7.5 */
#define TEST_HALF_PI_RAD 1.57079632679F
#define TEST_ORIGIN_TOLERANCE_M 1e-6F

static void feed_stationary(struct lgps_zupt *zupt, uint32_t now_ms)
{
	const float accel_g[3] = {0.0F, 0.0F, 1.0F};
	const float gyro_rps[3] = {0.0F, 0.0F, 0.0F};

	lgps_zupt_update(zupt, accel_g, gyro_rps, now_ms);
}

static void assert_body_from_imu(const float in[3], const float expected[3])
{
	float out[3] = {0.0F};
	uint8_t axis;

	lgps_nav_body_from_imu(in, out);
	for (axis = 0U; axis < 3U; axis++) {
		zassert_within(out[axis], expected[axis], TEST_TOLERANCE,
			       "body axis %u", (uint32_t)axis);
	}
}

ZTEST(lifegps_nav, test_body_frame_rotation_basis)
{
	const float sensor_x[3] = {1.0F, 0.0F, 0.0F};
	const float sensor_y[3] = {0.0F, 1.0F, 0.0F};
	const float sensor_z[3] = {0.0F, 0.0F, 1.0F};
	const float body_pos_y[3] = {0.0F, 1.0F, 0.0F};
	const float body_neg_x[3] = {-1.0F, 0.0F, 0.0F};
	const float body_neg_z[3] = {0.0F, 0.0F, -1.0F};

	/* X_body = -Y_sensor, Y_body = +X_sensor, Z_body = -Z_sensor. */
	assert_body_from_imu(sensor_x, body_pos_y);
	assert_body_from_imu(sensor_y, body_neg_x);
	assert_body_from_imu(sensor_z, body_neg_z);
}

ZTEST(lifegps_nav, test_body_frame_mag_matches_imu)
{
	const float sensor[3] = {-0.3F, 0.5F, -0.8F};
	float imu_out[3] = {0.0F};
	float mag_out[3] = {0.0F};
	uint8_t axis;

	lgps_nav_body_from_imu(sensor, imu_out);
	lgps_nav_body_from_mag(sensor, mag_out);

	for (axis = 0U; axis < 3U; axis++) {
		zassert_within(mag_out[axis], imu_out[axis], TEST_TOLERANCE,
			       "mag axis %u", (uint32_t)axis);
	}
}

ZTEST(lifegps_nav, test_body_frame_rotation_signed_basis_cycle)
{
	const float basis[3][3] = {
		{1.0F, 0.0F, 0.0F},
		{0.0F, 1.0F, 0.0F},
		{0.0F, 0.0F, 1.0F},
	};
	/* R * R = diag(-1, -1, +1), so R^4 restores the basis exactly. */
	const float square_scale[3] = {-1.0F, -1.0F, 1.0F};
	float first[3] = {0.0F};
	float second[3] = {0.0F};
	float third[3] = {0.0F};
	float fourth[3] = {0.0F};
	uint8_t axis;
	uint8_t index;

	for (index = 0U; index < 3U; index++) {
		lgps_nav_body_from_imu(basis[index], first);
		lgps_nav_body_from_imu(first, second);
		lgps_nav_body_from_imu(second, third);
		lgps_nav_body_from_imu(third, fourth);

		for (axis = 0U; axis < 3U; axis++) {
			zassert_within(second[axis], square_scale[axis] * basis[index][axis],
				       TEST_TOLERANCE, "R^2 axis %u of basis %u",
				       (uint32_t)axis, (uint32_t)index);
			zassert_within(fourth[axis], basis[index][axis], TEST_TOLERANCE,
				       "R^4 axis %u of basis %u",
				       (uint32_t)axis, (uint32_t)index);
		}
	}
}

ZTEST(lifegps_nav, test_zupt_lock_after_dwell)
{
	struct lgps_zupt zupt;
	uint32_t now_ms;

	lgps_zupt_reset(&zupt, 0U);
	zassert_false(lgps_zupt_locked(&zupt), "reset state must not be locked");

	for (now_ms = 0U; now_ms < LGPS_ZUPT_DWELL_TIME_MS; now_ms += TEST_SAMPLE_PERIOD_MS) {
		feed_stationary(&zupt, now_ms);
		zassert_false(lgps_zupt_locked(&zupt), "locked too early at %u ms", now_ms);
	}

	feed_stationary(&zupt, LGPS_ZUPT_DWELL_TIME_MS);
	zassert_true(lgps_zupt_locked(&zupt), "not locked after the 10 s dwell");
}

ZTEST(lifegps_nav, test_zupt_variance_constant_norm)
{
	struct lgps_zupt zupt;
	const float accel_g[4][3] = {
		{0.0F, 0.0F, 1.0F},
		{0.0F, 0.0F, -1.0F},
		{-1.0F, 0.0F, 0.0F},
		{0.0F, 1.0F, 0.0F},
	};
	const float gyro_rps[3] = {0.0F, 0.0F, 0.0F};
	uint8_t sample;
	uint32_t now_ms;

	lgps_zupt_reset(&zupt, 0U);
	for (sample = 0U; sample < LGPS_ZUPT_WINDOW_SAMPLES; sample++) {
		now_ms = (uint32_t)sample * TEST_SAMPLE_PERIOD_MS;
		lgps_zupt_update(&zupt, accel_g[sample % 4U], gyro_rps, now_ms);
	}

	zassert_within(lgps_zupt_accel_variance_g2(&zupt), 0.0F, TEST_TOLERANCE,
		       "a constant |a| window must give zero variance");
}

ZTEST(lifegps_nav, test_zupt_motion_clears_lock)
{
	struct lgps_zupt zupt;
	const float gyro_rps[3] = {0.0F, 0.0F, 0.0F};
	const float spike_g[3] = {0.0F, 0.0F, 2.0F};
	uint32_t now_ms;

	lgps_zupt_reset(&zupt, 0U);
	for (now_ms = 0U; now_ms <= LGPS_ZUPT_DWELL_TIME_MS; now_ms += TEST_SAMPLE_PERIOD_MS) {
		feed_stationary(&zupt, now_ms);
	}
	zassert_true(lgps_zupt_locked(&zupt), "not locked before the disturbance");

	lgps_zupt_update(&zupt, spike_g, gyro_rps, now_ms);
	zassert_false(lgps_zupt_locked(&zupt), "lock survived a variance spike");
	zassert_true(lgps_zupt_accel_variance_g2(&zupt) > LGPS_ZUPT_ACCEL_VARIANCE_MAX_G2,
		     "spike did not raise the variance above the threshold");
}

ZTEST(lifegps_nav, test_zupt_lock_across_tick_wrap)
{
	struct lgps_zupt zupt;
	const uint32_t start_ms = 0xFFFFF000U;
	uint32_t elapsed_ms;

	lgps_zupt_reset(&zupt, start_ms);
	for (elapsed_ms = 0U; elapsed_ms < LGPS_ZUPT_DWELL_TIME_MS;
	     elapsed_ms += TEST_SAMPLE_PERIOD_MS) {
		feed_stationary(&zupt, start_ms + elapsed_ms);
		zassert_false(lgps_zupt_locked(&zupt), "locked too early at %u ms",
			      elapsed_ms);
	}

	feed_stationary(&zupt, start_ms + LGPS_ZUPT_DWELL_TIME_MS);
	zassert_true(lgps_zupt_locked(&zupt), "dwell not detected across the tick wrap");
}

ZTEST(lifegps_nav, test_weinberg_step_length)
{
	float prior_step_m;
	float sixteenth_g_step_m;

	prior_step_m = lgps_weinberg_step_m(LGPS_WEINBERG_K_PRIOR, 1.0F);
	zassert_within(prior_step_m, LGPS_WEINBERG_K_PRIOR, TEST_TOLERANCE,
		       "delta_a = 1 g must return K");

	/* 0.0625 g = 1/16 g, and 16^-0.25 = 0.5. */
	sixteenth_g_step_m = lgps_weinberg_step_m(LGPS_WEINBERG_K_PRIOR, 0.0625F);
	zassert_within(sixteenth_g_step_m, 0.5F * prior_step_m, TEST_TOLERANCE,
		       "quarter exponent law violated");

	zassert_within(lgps_weinberg_step_m(LGPS_WEINBERG_K_PRIOR, 0.0F), 0.0F,
		       TEST_TOLERANCE, "delta_a = 0 must not produce a step");
	zassert_within(lgps_weinberg_step_m(LGPS_WEINBERG_K_PRIOR, -1.0F), 0.0F,
		       TEST_TOLERANCE, "negative delta_a must not produce a step");
}

ZTEST(lifegps_nav, test_pdr_integrates_heading)
{
	struct lgps_pdr pdr;
	float step_m = 0.0F;
	float x_before_m;
	uint32_t step;

	lgps_pdr_init(&pdr);
	zassert_within(pdr.position_x_m, 0.0F, TEST_ORIGIN_TOLERANCE_M, "x origin");
	zassert_within(pdr.position_y_m, 0.0F, TEST_ORIGIN_TOLERANCE_M, "y origin");

	for (step = 0U; step < 4U; step++) {
		step_m = lgps_pdr_step(&pdr, 1.0F, 0.0F);
	}

	zassert_within(step_m, LGPS_WEINBERG_K_PRIOR, TEST_TOLERANCE, "step length");
	zassert_within(pdr.position_y_m, 4.0F * LGPS_WEINBERG_K_PRIOR, TEST_TOLERANCE,
		       "heading 0 must advance +Y only");
	zassert_within(pdr.position_x_m, 0.0F, TEST_ORIGIN_TOLERANCE_M, "x unchanged");
	zassert_equal(pdr.steps_total, 4U, "step count");

	x_before_m = pdr.position_x_m;
	lgps_pdr_step(&pdr, 1.0F, TEST_HALF_PI_RAD);
	zassert_true(pdr.position_x_m > x_before_m, "heading pi/2 must advance +X");
	zassert_equal(pdr.steps_total, 5U, "step count after the turn");
}

ZTEST(lifegps_nav, test_pdr_frozen_holds_position)
{
	struct lgps_pdr pdr;
	float y_before_m;

	lgps_pdr_init(&pdr);
	(void)lgps_pdr_step(&pdr, 1.0F, 0.0F);
	(void)lgps_pdr_step(&pdr, 1.0F, 0.0F);
	y_before_m = pdr.position_y_m;

	lgps_pdr_set_frozen(&pdr, true);
	zassert_true(lgps_pdr_is_frozen(&pdr), "frozen flag not set");
	zassert_within(pdr.position_y_m, y_before_m, TEST_TOLERANCE,
		       "freezing must not reset the accumulated position");

	zassert_within(lgps_pdr_step(&pdr, 1.0F, 0.0F), 0.0F, TEST_TOLERANCE,
		       "frozen step must return zero length");
	zassert_within(pdr.position_y_m, y_before_m, TEST_TOLERANCE, "frozen integration");
	zassert_equal(pdr.steps_total, 2U, "frozen steps must not be counted");

	lgps_pdr_set_frozen(&pdr, false);
	zassert_false(lgps_pdr_is_frozen(&pdr), "frozen flag not cleared");
	zassert_within(lgps_pdr_step(&pdr, 1.0F, 0.0F), LGPS_WEINBERG_K_PRIOR,
		       TEST_TOLERANCE, "step length after unfreezing");
	zassert_equal(pdr.steps_total, 3U, "step counted after unfreezing");
}

ZTEST(lifegps_nav, test_magnetic_anomaly)
{
	zassert_within(lgps_nav_mag_anomaly_ratio(50.0F, 50.0F), 0.0F, TEST_TOLERANCE,
		       "equal norms must give a zero ratio");
	zassert_false(lgps_nav_mag_anomaly(50.0F, 50.0F), "equal norms are not an anomaly");

	zassert_true(lgps_nav_mag_anomaly(65.5F, 50.0F), "31 % deviation must be flagged");
	zassert_false(lgps_nav_mag_anomaly(60.0F, 50.0F), "20 % deviation must pass");

	zassert_within(lgps_nav_mag_anomaly_ratio(65.5F, 0.0F), 0.0F, TEST_TOLERANCE,
		       "zero reference must give a zero ratio");
	zassert_false(lgps_nav_mag_anomaly(65.5F, 0.0F), "zero reference must not flag");
}

ZTEST_SUITE(lifegps_nav, NULL, NULL, NULL, NULL, NULL);
