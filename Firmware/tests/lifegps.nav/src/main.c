/*
 * Life_GPS firmware - ztest coverage for the attitude / ZUPT / PDR maths.
 *
 * Feeds synthetic IMU and magnetometer data; no device tree, no drivers.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include <lifegps/nav.h>

#include <math.h>
#include <stdint.h>

#define TEST_TOLERANCE 1e-6F
/*
 * Judging the bench sample below: the dominant body component must exceed 0.90
 * and the cross components stay below 0.25. The recorded samples were hand-held
 * (up to ~12 degrees of tilt), the smallest dominant value is 0.954 and the
 * largest cross value is 0.187, so both thresholds keep margin while a mapping
 * that is 90 degrees off (dominant component near 0) or has a wrong sign
 * (negative instead of positive) fails immediately.
 */
#define BENCH_DOMINANT_MIN 0.90F
#define BENCH_CROSS_MAX 0.25F
/*
 * 26 Hz is the LSM6DSR ODR the firmware programs (1000 / 26 = 38.46 ms, rounded
 * down here); together with LGPS_ZUPT_WINDOW_SAMPLES == 26 it makes the window
 * exactly 1.000 s. Tests therefore pace their samples like the real device.
 */
#define TEST_SAMPLE_PERIOD_MS 38U
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
	const float body_neg_y[3] = {0.0F, -1.0F, 0.0F};
	const float body_neg_x[3] = {-1.0F, 0.0F, 0.0F};
	const float body_neg_z[3] = {0.0F, 0.0F, -1.0F};

	/* X_body = -Y_sensor, Y_body = -X_sensor, Z_body = -Z_sensor (det = +1). */
	assert_body_from_imu(sensor_x, body_neg_y);
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

/*
 * det(R) = +1. This is the assertion that would have caught the frozen matrix:
 * a reflection (det = -1) cannot come from a rigid mount and makes the gyroscope
 * rotate the wrong way relative to gravity. R * e_i is the i-th column of R, so the
 * determinant of the three images laid out as rows is det(R^T) = det(R).
 */
ZTEST(lifegps_nav, test_body_frame_rotation_is_proper)
{
	const float basis[3][3] = {
		{1.0F, 0.0F, 0.0F},
		{0.0F, 1.0F, 0.0F},
		{0.0F, 0.0F, 1.0F},
	};
	float image[3][3] = { { 0.0F } };
	float cross[3];
	float determinant;
	uint8_t index;

	for (index = 0U; index < 3U; index++) {
		lgps_nav_body_from_imu(basis[index], image[index]);

		/* Orthonormal rows: the rotation may not stretch or skew. */
		zassert_within(sqrtf((image[index][0] * image[index][0]) +
				      (image[index][1] * image[index][1]) +
				      (image[index][2] * image[index][2])),
			       1.0F, TEST_TOLERANCE, "row %u is not a unit vector",
			       (uint32_t)index);
	}

	for (index = 0U; index + 1U < 3U; index++) {
		zassert_within((image[index][0] * image[index + 1U][0]) +
				      (image[index][1] * image[index + 1U][1]) +
				      (image[index][2] * image[index + 1U][2]),
			       0.0F, TEST_TOLERANCE, "rows %u/%u are not orthogonal",
			       (uint32_t)index, (uint32_t)(index + 1U));
	}

	cross[0] = (image[0][1] * image[1][2]) - (image[0][2] * image[1][1]);
	cross[1] = (image[0][2] * image[1][0]) - (image[0][0] * image[1][2]);
	cross[2] = (image[0][0] * image[1][1]) - (image[0][1] * image[1][0]);
	determinant = (cross[0] * image[2][0]) + (cross[1] * image[2][1]) +
		      (cross[2] * image[2][2]);

	zassert_within(determinant, 1.0F, TEST_TOLERANCE,
		       "det(R) must be +1 (proper rotation), got %.6f", (double)determinant);
}

/*
 * The measured mapping is a 180 degree rotation about (1, -1, 0) / sqrt(2), so it
 * is its own inverse: applying it twice restores every vector. (The previous
 * matrix instead satisfied R^2 = diag(-1, -1, +1), which is a reflection property.)
 */
ZTEST(lifegps_nav, test_body_frame_rotation_double_apply_is_identity)
{
	const float basis[3][3] = {
		{1.0F, 0.0F, 0.0F},
		{0.0F, 1.0F, 0.0F},
		{0.0F, 0.0F, 1.0F},
	};
	float once[3] = { 0.0F };
	float twice[3] = { 0.0F };
	uint8_t axis;
	uint8_t index;

	for (index = 0U; index < 3U; index++) {
		lgps_nav_body_from_imu(basis[index], once);
		lgps_nav_body_from_imu(once, twice);

		for (axis = 0U; axis < 3U; axis++) {
			zassert_within(twice[axis], basis[index][axis], TEST_TOLERANCE,
				       "R^2 != I on axis %u of basis %u", (uint32_t)axis,
				       (uint32_t)index);
		}
	}
}

/*
 * Bench data from Main_V0.2 (2026-11, static placements, sensor frame): every
 * placement must put gravity on the body axis that physically points up. This is
 * the case that turns the frozen-matrix bug into a red test instead of a field
 * surprise; the cross-axis tolerance absorbs hand-held tilt (up to ~12 degrees in
 * the recorded samples).
 */
ZTEST(lifegps_nav, test_body_frame_matches_bench_data)
{
	/* Flat, front face up: gravity must read +Z_body. */
	const float bench_flat_s[3] = {-0.010F, 0.187F, -0.954F};
	/* Same, front (top) edge raised ~33 deg: +Y_body and +Z_body share gravity. */
	const float bench_tilted_s[3] = {-0.528F, -0.147F, -0.811F};
	/* Upright, top up: +Y_body. */
	const float bench_upright_s[3] = {-0.995F, -0.023F, 0.048F};
	/* Lying on its side, left side up: -X_body. */
	const float bench_lateral_s[3] = {-0.008F, 0.990F, -0.021F};
	float flat_b[3] = { 0.0F };
	float tilted_b[3] = { 0.0F };
	float upright_b[3] = { 0.0F };
	float lateral_b[3] = { 0.0F };

	lgps_nav_body_from_imu(bench_flat_s, flat_b);
	zassert_true(flat_b[2] > BENCH_DOMINANT_MIN,
		     "face up must put gravity on +Z_body, got %.3f", (double)flat_b[2]);
	zassert_true(fabsf(flat_b[0]) < BENCH_CROSS_MAX, "face up, |X| = %.3f",
		     (double)fabsf(flat_b[0]));
	zassert_true(fabsf(flat_b[1]) < BENCH_CROSS_MAX, "face up, |Y| = %.3f",
		     (double)fabsf(flat_b[1]));

	lgps_nav_body_from_imu(bench_tilted_s, tilted_b);
	zassert_true(tilted_b[1] > 0.40F,
		     "raising the front edge must make +Y_body positive, got %.3f",
		     (double)tilted_b[1]);
	zassert_true(tilted_b[2] > 0.60F, "raised front edge, +Z_body = %.3f",
		     (double)tilted_b[2]);

	lgps_nav_body_from_imu(bench_upright_s, upright_b);
	zassert_true(upright_b[1] > BENCH_DOMINANT_MIN,
		     "upright (top up) must put gravity on +Y_body, got %.3f",
		     (double)upright_b[1]);

	lgps_nav_body_from_imu(bench_lateral_s, lateral_b);
	zassert_true(lateral_b[0] < -BENCH_DOMINANT_MIN,
		     "left side up must put gravity on -X_body, got %.3f",
		     (double)lateral_b[0]);
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
	for (now_ms = 0U; now_ms < LGPS_ZUPT_DWELL_TIME_MS; now_ms += TEST_SAMPLE_PERIOD_MS) {
		feed_stationary(&zupt, now_ms);
	}
	feed_stationary(&zupt, LGPS_ZUPT_DWELL_TIME_MS);
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

	/* zassert_* evaluates its arguments more than once: never call a
	 * side-effecting function inside an assertion.
	 */
	float step_m = lgps_pdr_step(&pdr, 1.0F, 0.0F);

	zassert_within(step_m, LGPS_WEINBERG_K_PRIOR, TEST_TOLERANCE,
		       "step length after unfreezing");
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

ZTEST(lifegps_nav, test_step_detector_and_pdr_chain)
{
	struct lgps_step_detector det;
	struct lgps_pdr pdr;
	const float idle_g[3] = {0.0F, 0.0F, 1.00F};
	const float peak_g[3] = {0.0F, 0.0F, 1.32F};
	const float valley_g[3] = {0.0F, 0.0F, 0.78F};
	float delta_g = 0.0F;
	bool detected;

	lgps_step_detector_reset(&det);
	lgps_pdr_init(&pdr);

	/* Stationary 1 g sample must not trigger a step. */
	detected = lgps_step_detector_update(&det, idle_g, 100U, &delta_g);
	zassert_false(detected, "idle 1 g must not trigger a step");

	/* First step cycle: peak at 200 ms, valley at 350 ms. */
	detected = lgps_step_detector_update(&det, peak_g, 200U, &delta_g);
	zassert_false(detected, "peak alone must not trigger before valley");
	detected = lgps_step_detector_update(&det, valley_g, 350U, &delta_g);
	zassert_true(detected, "peak-to-valley swing must trigger a step");
	zassert_within(delta_g, 0.54F, TEST_TOLERANCE, "delta_accel_g mismatch");

	(void)lgps_pdr_step(&pdr, delta_g, 0.0F);
	zassert_equal(pdr.steps_total, 1U, "first step must increment PDR step count");

	/* Refractory check: another swing within 150 ms (< 300 ms) must be rejected. */
	(void)lgps_step_detector_update(&det, peak_g, 420U, &delta_g);
	detected = lgps_step_detector_update(&det, valley_g, 500U, &delta_g);
	zassert_false(detected, "swing inside 300 ms refractory window must be ignored");

	/* Second valid step at 900 ms (>= 300 ms after 350 ms). */
	(void)lgps_step_detector_update(&det, peak_g, 800U, &delta_g);
	detected = lgps_step_detector_update(&det, valley_g, 900U, &delta_g);
	zassert_true(detected, "second step after refractory window must trigger");
	(void)lgps_pdr_step(&pdr, delta_g, 0.0F);
	zassert_equal(pdr.steps_total, 2U, "second step must increment PDR step count");
}

ZTEST_SUITE(lifegps_nav, NULL, NULL, NULL, NULL, NULL);
