/* SPDX-License-Identifier: MIT */
/*
 * Unit tests for the calibration maths, the session state machine and the record
 * codec (Firmware/src/calib/calib.c).
 *
 * Run by CI: `twister -p native_sim -T Firmware/tests`. The App pins the same
 * wire layout in CalibCodecTest.kt, so the two suites together are the contract.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <math.h>

#include <lifegps/calib.h>
#include <lifegps/calib_store.h>

/* Provided by src/store_stub.c */
bool lgps_calib_store_test_has_record(void);
void lgps_calib_store_test_corrupt(void);

/* Nominal sensitivity of the +/-4 g range: 0.122 mg/LSB. */
#define NOMINAL_LSB_PER_G 8196.7F

/* Raw counts a 1 g reading produces with a +2.4 % gain error (the bench finding
 * that started this work: |acc| read 1.024 g at rest).
 */
#define OVERREAD_LSB_PER_G (NOMINAL_LSB_PER_G * 1.024F)

static bool near_f(float actual, float expected, float tolerance)
{
	return fabsf(actual - expected) <= tolerance;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)lgps_calib_store_erase();
	(void)lgps_calib_init();
}

ZTEST_SUITE(lifegps_calib, NULL, NULL, before_each, NULL, NULL);

/* --- stillness ------------------------------------------------------------- */

ZTEST(lifegps_calib, test_rest_pct_maps_variance)
{
	zassert_equal(100U, lgps_calib_rest_pct(0.0F), "a perfectly still device is 100 %%");
	zassert_equal(100U, lgps_calib_rest_pct(0.0005F), "above the best bucket");
	zassert_equal(0U, lgps_calib_rest_pct(0.02F), "a hand-held device is 0 %%");
	zassert_equal(0U, lgps_calib_rest_pct(5.0F), "clamped, not negative");

	const float mid = (float)lgps_calib_rest_pct(0.0105F);

	zassert_true(mid > 40.0F && mid < 60.0F, "midpoint maps to ~50 %, got %d", (int)mid);
}

/* --- pose identification --------------------------------------------------- */

ZTEST(lifegps_calib, test_detect_face_picks_the_dominant_axis)
{
	const float x_up[3] = { 1.0F, 0.0F, 0.0F };
	const float x_down[3] = { -1.0F, 0.0F, 0.0F };
	const float z_up[3] = { 0.0F, 0.0F, 1.0F };
	const float y_down_tilted[3] = { 0.05F, -0.95F, 0.10F };

	zassert_equal(0, lgps_calib_detect_face(x_up), "+X up is face 0");
	zassert_equal(1, lgps_calib_detect_face(x_down), "-X up is face 1");
	zassert_equal(4, lgps_calib_detect_face(z_up), "+Z up is face 4");
	zassert_equal(3, lgps_calib_detect_face(y_down_tilted), "-Y up is face 3");
}

ZTEST(lifegps_calib, test_detect_face_refuses_an_edge_pose)
{
	const float on_an_edge[3] = { 0.6F, 0.6F, 0.0F };
	const float tumbling[3] = { 0.2F, 0.2F, 0.2F };

	zassert_equal(LGPS_CALIB_FACE_NONE, lgps_calib_detect_face(on_an_edge),
		      "two axes sharing the vector must not be recorded as a face");
	zassert_equal(LGPS_CALIB_FACE_NONE, lgps_calib_detect_face(tumbling),
		      "no dominant axis must not be recorded as a face");
	zassert_equal(LGPS_CALIB_FACE_NONE, lgps_calib_detect_face(NULL), "NULL is refused");
}

/* --- six-face solve -------------------------------------------------------- */

static void build_faces(float faces[LGPS_CALIB_POSITION_COUNT][3], float bias_lsb,
			float lsb_per_g)
{
	uint8_t position;

	for (position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		const uint8_t axis = position / 2U;
		const float sign = (position % 2U) ? -1.0F : 1.0F;
		uint8_t component;

		for (component = 0U; component < 3U; component++) {
			faces[position][component] = bias_lsb;
		}

		faces[position][axis] = bias_lsb + (sign * lsb_per_g);
	}
}

ZTEST(lifegps_calib, test_face_math_recovers_bias_and_gain)
{
	float faces[LGPS_CALIB_POSITION_COUNT][3];
	int32_t bias_ug[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };

	build_faces(faces, 200.0F, OVERREAD_LSB_PER_G);

	zassert_equal(0, lgps_calib_face_math(faces, 0x3FU, bias_ug, scale_ppm), "solve failed");

	const float expected_ppm = ((1.0F / 1.024F) - 1.0F) * 1000000.0F;

	zassert_true(near_f((float)bias_ug[0], 200.0F * 122.0F, 10.0F),
		     "X bias %d ug, expected %d", (int)bias_ug[0], (int)(200 * 122));
	zassert_true(near_f((float)scale_ppm[0], expected_ppm, 60.0F),
		     "X scale %d ppm, expected %d", (int)scale_ppm[0], (int)expected_ppm);
	zassert_true(near_f((float)scale_ppm[2], expected_ppm, 60.0F), "Z scale follows X");
}

ZTEST(lifegps_calib, test_face_math_needs_all_six_faces)
{
	float faces[LGPS_CALIB_POSITION_COUNT][3];
	int32_t bias_ug[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };

	build_faces(faces, 0.0F, NOMINAL_LSB_PER_G);

	zassert_equal(-EAGAIN, lgps_calib_face_math(faces, 0x2FU, bias_ug, scale_ppm),
		      "a missing face must refuse, not extrapolate");
}

ZTEST(lifegps_calib, test_face_math_rejects_a_degenerate_pair)
{
	float faces[LGPS_CALIB_POSITION_COUNT][3];
	int32_t bias_ug[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };

	build_faces(faces, 0.0F, NOMINAL_LSB_PER_G);
	/* The "up" and "down" poses of the X axis read the same: the user never
	 * turned the device over.
	 */
	faces[0][0] = faces[1][0];

	zassert_equal(-EAGAIN, lgps_calib_face_math(faces, 0x3FU, bias_ug, scale_ppm),
		      "a zero span must refuse");
}

/* --- magnetometer ---------------------------------------------------------- */

ZTEST(lifegps_calib, test_mag_math_centres_a_symmetric_rotation)
{
	const int16_t min_lsb[3] = { -300, -300, -300 };
	const int16_t max_lsb[3] = { 300, 300, 300 };
	int16_t bias_lsb[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };
	uint8_t coverage[3] = { 0 };

	zassert_equal(0, lgps_calib_mag_math(min_lsb, max_lsb, bias_lsb, scale_ppm, coverage),
		      "solve failed");

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		zassert_equal(0, bias_lsb[axis], "symmetric span has no hard iron");
		zassert_equal(0, scale_ppm[axis], "equal spans need no gain correction");
		zassert_equal(100U, coverage[axis], "600 LSB span is 100 %%");
	}
}

ZTEST(lifegps_calib, test_mag_math_reports_offset_and_anisotropy)
{
	const int16_t min_lsb[3] = { -200, -150, -300 };
	const int16_t max_lsb[3] = { 400, 150, 300 };
	int16_t bias_lsb[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };
	uint8_t coverage[3] = { 0 };

	zassert_equal(0, lgps_calib_mag_math(min_lsb, max_lsb, bias_lsb, scale_ppm, coverage),
		      "solve failed");

	zassert_equal(100, bias_lsb[0], "centre of -200..400 is 100");
	zassert_equal(0, bias_lsb[1], "centre of -150..150 is 0");
	zassert_equal(100U, coverage[0], "600 LSB span is 100 %%");
	zassert_equal(50U, coverage[1], "300 LSB span is 50 %%");
	/* Y is the narrow axis, so it gets a positive gain and X a negative one. */
	zassert_true(scale_ppm[1] > 0, "narrow axis must be stretched, got %d", (int)scale_ppm[1]);
	zassert_true(scale_ppm[0] < 0, "wide axis must be shrunk, got %d", (int)scale_ppm[0]);
}

ZTEST(lifegps_calib, test_mag_math_rejects_an_unmoved_axis)
{
	const int16_t min_lsb[3] = { -300, 0, -300 };
	const int16_t max_lsb[3] = { 300, 0, 300 };
	int16_t bias_lsb[3] = { 0 };
	int32_t scale_ppm[3] = { 0 };
	uint8_t coverage[3] = { 0 };

	zassert_equal(-EINVAL, lgps_calib_mag_math(min_lsb, max_lsb, bias_lsb, scale_ppm,
						   coverage),
		      "an axis that never moved must refuse");
}

/* --- record ---------------------------------------------------------------- */

ZTEST(lifegps_calib, test_record_roundtrip)
{
	const struct lgps_calib_coeffs input = {
		.accel_bias_ug = { -24600, 8000, -1000 },
		.accel_scale_ppm = { -23438, 500, -100 },
		.gyro_bias_mdps = { -350, 120, 0 },
		.mag_bias_lsb = { 120, -80, 40 },
		.mag_scale_ppm = { 12000, -9000, 3500 },
		.mag_coverage_pct = { 100, 95, 88 },
		.accel_face_mask = 0x3F,
	};
	uint8_t record[LGPS_CALIB_RECORD_SIZE];
	struct lgps_calib_coeffs output;
	uint8_t flags = 0U;
	uint32_t seq = 0U;
	uint32_t unix_s = 0U;

	zassert_equal(0, lgps_calib_record_encode(&input, LGPS_CALIB_FLAG_FROM_STORE, 7U,
						  1765000000U, record),
		      "encode failed");
	zassert_equal(0, lgps_calib_record_decode(record, &output, &flags, &seq, &unix_s),
		      "decode failed");

	zassert_equal(input.accel_bias_ug[0], output.accel_bias_ug[0], "bias x");
	zassert_equal(input.accel_scale_ppm[2], output.accel_scale_ppm[2], "scale z");
	zassert_equal(input.gyro_bias_mdps[1], output.gyro_bias_mdps[1], "gyro y");
	zassert_equal(input.mag_bias_lsb[1], output.mag_bias_lsb[1], "mag bias y");
	zassert_equal(input.mag_scale_ppm[0], output.mag_scale_ppm[0], "mag scale x");
	zassert_equal(input.mag_coverage_pct[2], output.mag_coverage_pct[2], "coverage z");
	zassert_equal(input.accel_face_mask, output.accel_face_mask, "face mask");
	zassert_equal(LGPS_CALIB_FLAG_FROM_STORE, flags, "flags");
	zassert_equal(7U, seq, "seq");
	zassert_equal(1765000000U, unix_s, "unix time");
}

ZTEST(lifegps_calib, test_record_rejects_a_corrupted_payload)
{
	const struct lgps_calib_coeffs input = { 0 };
	uint8_t record[LGPS_CALIB_RECORD_SIZE];
	struct lgps_calib_coeffs output;
	uint8_t flags = 0U;
	uint32_t seq = 0U;
	uint32_t unix_s = 0U;

	zassert_equal(0, lgps_calib_record_encode(&input, 0U, 1U, 0U, record), "encode failed");

	record[30] ^= 0xFFU;
	zassert_equal(-EBADMSG, lgps_calib_record_decode(record, &output, &flags, &seq, &unix_s),
		      "a flipped byte must be caught by the CRC");

	record[30] ^= 0xFFU;
	record[4] = 2U; /* record version 2 does not exist yet */
	zassert_equal(-EBADMSG, lgps_calib_record_decode(record, &output, &flags, &seq, &unix_s),
		      "an unknown version must not be read with today's field map");
}

/* --- session --------------------------------------------------------------- */

static struct lgps_imu_sample face_sample(uint8_t position, float bias_lsb, float lsb_per_g)
{
	const uint8_t axis = position / 2U;
	const float sign = (position % 2U) ? -1.0F : 1.0F;
	struct lgps_imu_sample sample = { 0 };

	for (uint8_t component = 0U; component < 3U; component++) {
		sample.raw_accel[component] = (int16_t)bias_lsb;
	}

	sample.raw_accel[axis] = (int16_t)(bias_lsb + (sign * lsb_per_g));

	return sample;
}

static void capture_face(uint8_t position, float bias_lsb, float lsb_per_g)
{
	struct lgps_imu_sample sample = face_sample(position, bias_lsb, lsb_per_g);
	uint32_t now_ms = 0U;

	zassert_equal(0, lgps_calib_accel_start(LGPS_CALIB_POSITION_AUTO), "start failed");

	/* One sample to leave WAIT_REST, then a full face worth of still samples. */
	for (uint16_t index = 0U; index < LGPS_CALIB_FACE_SAMPLES + 4U; index++) {
		now_ms += 38U;
		lgps_calib_feed_imu(&sample, now_ms);
	}
}

ZTEST(lifegps_calib, test_auto_detection_records_the_pose)
{
	struct lgps_calib_snapshot snap;

	capture_face(4U, 200.0F, OVERREAD_LSB_PER_G);
	lgps_calib_snapshot(&snap);

	zassert_equal(LGPS_CALIB_STATE_IDLE, snap.accel_state, "the face capture ended");
	zassert_equal(LGPS_CALIB_FACE_DONE, snap.accel_face_state, "face done");
	zassert_equal(4U, snap.accel_detected_face, "the device identified +Z up on its own");
	zassert_equal(1U << 4U, snap.accel_face_mask, "only that face was recorded");
}

ZTEST(lifegps_calib, test_cancel_keeps_the_stored_record)
{
	struct lgps_calib_snapshot snap;
	uint8_t record_flags = 0U;
	uint32_t seq = 0U;
	uint32_t unix_s = 0U;
	struct lgps_calib_coeffs stored;

	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}

	zassert_equal(0, lgps_calib_accel_finish(), "finish failed");
	zassert_true(lgps_calib_store_test_has_record(), "record must be written");

	zassert_equal(0, lgps_calib_accel_start(LGPS_CALIB_POSITION_AUTO), "start failed");
	zassert_equal(0, lgps_calib_accel_cancel(), "cancel failed");

	zassert_equal(0, lgps_calib_store_load(&stored, &record_flags, &seq, &unix_s),
		      "the record must survive a cancelled capture");
	lgps_calib_snapshot(&snap);
	zassert_true((snap.flags & LGPS_CALIB_FLAG_FROM_STORE) != 0U, "still a stored record");
}

ZTEST(lifegps_calib, test_six_face_capture_then_finish_persists)
{
	struct lgps_calib_snapshot before;
	struct lgps_calib_snapshot after;

	lgps_calib_snapshot(&before);
	zassert_true((before.flags & LGPS_CALIB_FLAG_DEFAULTS) != 0U, "starts from defaults");

	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}

	zassert_equal(0, lgps_calib_accel_finish(), "finish failed");
	zassert_true(lgps_calib_store_test_has_record(), "record must be written");

	/* A reboot: reload from the (stubbed) store. */
	zassert_equal(0, lgps_calib_init(), "init failed");
	lgps_calib_snapshot(&after);

	zassert_true((after.flags & LGPS_CALIB_FLAG_FROM_STORE) != 0U, "record reloaded");
	zassert_false((after.flags & LGPS_CALIB_FLAG_DEFAULTS) != 0U, "not the defaults");
	zassert_true(near_f((float)after.coeffs.accel_bias_ug[0], 200.0F * 122.0F, 10.0F),
		     "bias survived the round trip, got %d", (int)after.coeffs.accel_bias_ug[0]);
	zassert_equal(0x3FU, after.coeffs.accel_face_mask, "all six faces remembered");
}

ZTEST(lifegps_calib, test_finish_without_all_faces_is_refused)
{
	capture_face(0U, 0.0F, OVERREAD_LSB_PER_G);
	capture_face(1U, 0.0F, OVERREAD_LSB_PER_G);

	zassert_equal(-EAGAIN, lgps_calib_accel_finish(),
		      "two faces must not be extrapolated into six");
	zassert_false(lgps_calib_store_test_has_record(), "nothing may be persisted");
}

ZTEST(lifegps_calib, test_apply_imu_corrects_the_bench_case)
{
	struct lgps_imu_sample sample = { 0 };

	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}

	zassert_equal(0, lgps_calib_accel_finish(), "finish failed");

	/* Resting with the X axis up: raw = bias + 1 g of an over-reading sensor.
	 * The other two axes carry the same 200 LSB bias the faces were captured
	 * with, so a correct correction must leave them at zero.
	 */
	sample.raw_accel[0] = (int16_t)(200.0F + OVERREAD_LSB_PER_G);
	sample.raw_accel[1] = 200;
	sample.raw_accel[2] = 200;
	lgps_calib_apply_imu(&sample);

	zassert_true(near_f(sample.accel_g[0], 1.0F, 0.01F),
		     "a 1.024 g reading must come out as 1.000 g, got %d mg",
		     (int)(sample.accel_g[0] * 1000.0F));
	zassert_true(near_f(sample.accel_g[1], 0.0F, 0.01F), "the other axes stay near zero");
}

ZTEST(lifegps_calib, test_accel_clear_restores_the_defaults)
{
	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}

	zassert_equal(0, lgps_calib_accel_finish(), "finish failed");
	zassert_equal(0, lgps_calib_accel_clear(), "clear failed");
	zassert_false(lgps_calib_store_test_has_record(), "the record must be gone");

	const struct lgps_calib_coeffs *coeffs = lgps_calib_current();

	zassert_equal(0, coeffs->accel_bias_ug[0], "bias back to zero");
	zassert_equal(0, coeffs->accel_scale_ppm[0], "gain back to one");
}

ZTEST(lifegps_calib, test_mag_session_end_to_end)
{
	const int16_t samples[][3] = {
		{ -300, 0, 0 }, { 300, 0, 0 }, { 0, -300, 0 }, { 0, 300, 0 },
		{ 0, 0, -300 }, { 0, 0, 300 },
	};
	struct lgps_calib_snapshot snap;
	uint8_t record_flags = 0U;
	uint32_t seq = 0U;
	uint32_t unix_s = 0U;
	struct lgps_calib_coeffs stored;

	lgps_calib_note_utc(1765000000U);
	zassert_equal(0, lgps_calib_mag_start(), "start failed");

	for (size_t index = 0U; index < ARRAY_SIZE(samples); index++) {
		lgps_calib_feed_mag(samples[index]);
	}

	lgps_calib_snapshot(&snap);
	zassert_equal(100U, snap.mag_coverage_pct[0], "X coverage reached 100 %%");
	zassert_equal(1000U, snap.mag_progress_permille, "progress follows the weakest axis");

	zassert_equal(0, lgps_calib_mag_finish(), "finish failed");
	zassert_equal(0, lgps_calib_store_load(&stored, &record_flags, &seq, &unix_s),
		      "record written");

	zassert_equal(1765000000U, unix_s, "the record is dated from the GNSS UTC");
	zassert_equal(100U, stored.mag_coverage_pct[2], "coverage is persisted");
}

ZTEST(lifegps_calib, test_mag_only_calibration_does_not_mark_the_imu)
{
	/* One record, two sensors: a magnetometer calibration must not make the App
	 * report the accelerometer as calibrated as well.
	 */
	const int16_t samples[][3] = {
		{ -300, 0, 0 }, { 300, 0, 0 }, { 0, -300, 0 }, { 0, 300, 0 },
		{ 0, 0, -300 }, { 0, 0, 300 },
	};
	struct lgps_calib_snapshot snap;

	zassert_equal(0, lgps_calib_mag_start(), "start failed");
	for (size_t index = 0U; index < ARRAY_SIZE(samples); index++) {
		lgps_calib_feed_mag(samples[index]);
	}

	zassert_equal(0, lgps_calib_mag_finish(), "finish failed");
	lgps_calib_snapshot(&snap);

	zassert_true((snap.flags & LGPS_CALIB_FLAG_FROM_STORE) != 0U, "record written");
	zassert_true((snap.flags & LGPS_CALIB_FLAG_MAG_VALID) != 0U,
		     "the magnetometer half is calibrated");
	zassert_false((snap.flags & LGPS_CALIB_FLAG_IMU_VALID) != 0U,
		      "the IMU half must stay uncalibrated");

	/* And it stays that way across a reboot. */
	zassert_equal(0, lgps_calib_init(), "init failed");
	lgps_calib_snapshot(&snap);
	zassert_false((snap.flags & LGPS_CALIB_FLAG_IMU_VALID) != 0U,
		      "a reloaded mag-only record still has no IMU calibration");
}

ZTEST(lifegps_calib, test_clearing_one_sensor_keeps_the_other)
{
	const int16_t samples[][3] = {
		{ -300, 0, 0 }, { 300, 0, 0 }, { 0, -300, 0 }, { 0, 300, 0 },
		{ 0, 0, -300 }, { 0, 0, 300 },
	};
	struct lgps_calib_snapshot snap;

	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}
	zassert_equal(0, lgps_calib_accel_finish(), "accel finish failed");

	zassert_equal(0, lgps_calib_mag_start(), "mag start failed");
	for (size_t index = 0U; index < ARRAY_SIZE(samples); index++) {
		lgps_calib_feed_mag(samples[index]);
	}
	zassert_equal(0, lgps_calib_mag_finish(), "mag finish failed");

	zassert_equal(0, lgps_calib_mag_clear(), "mag clear failed");
	lgps_calib_snapshot(&snap);
	zassert_false((snap.flags & LGPS_CALIB_FLAG_MAG_VALID) != 0U, "mag cleared");
	zassert_true((snap.flags & LGPS_CALIB_FLAG_IMU_VALID) != 0U,
		     "clearing the magnetometer must not clear the IMU record");
	zassert_true(lgps_calib_store_test_has_record(), "the IMU half is still stored");
}

ZTEST(lifegps_calib, test_mag_finish_refuses_partial_coverage)
{
	const int16_t few[][3] = {
		{ -100, -100, -100 },
		{ 100, 100, 100 },
	};

	zassert_equal(0, lgps_calib_mag_start(), "start failed");

	for (size_t index = 0U; index < ARRAY_SIZE(few); index++) {
		lgps_calib_feed_mag(few[index]);
	}

	zassert_equal(-EAGAIN, lgps_calib_mag_finish(),
		      "200 LSB of span is 33 %%, below the 80 %% floor");
	zassert_false(lgps_calib_store_test_has_record(), "nothing may be persisted");
}

ZTEST(lifegps_calib, test_corrupted_record_is_ignored_at_boot)
{
	for (uint8_t position = 0U; position < LGPS_CALIB_POSITION_COUNT; position++) {
		capture_face(position, 200.0F, OVERREAD_LSB_PER_G);
	}

	zassert_equal(0, lgps_calib_accel_finish(), "finish failed");
	lgps_calib_store_test_corrupt();

	struct lgps_calib_snapshot snap;

	zassert_equal(0, lgps_calib_init(), "a bad record is not a boot failure");
	lgps_calib_snapshot(&snap);
	zassert_true((snap.flags & LGPS_CALIB_FLAG_DEFAULTS) != 0U,
		     "a CRC-bad record falls back to the defaults");
	zassert_equal(0, snap.coeffs.accel_bias_ug[0], "no half-applied coefficients");
}
