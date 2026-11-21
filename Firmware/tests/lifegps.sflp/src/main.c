/*
 * Life_GPS firmware - ztest coverage for the LSM6DSV16X FIFO / SFLP decoding.
 *
 * Pure data in, pure data out: no devicetree, no SPI, no sensors. The expected
 * values are the IEEE 754 binary16 encodings themselves, so a decoding mistake
 * cannot cancel out.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include <lifegps/sflp.h>

#include <math.h>
#include <stdint.h>

/* Two binary16 values differ by this much in the worst case (65504 vs 65503). */
#define HALF_ABS_TOLERANCE 1e-3F
#define QUAT_TOLERANCE 1e-5F

/* Little-endian six-byte payload, the byte order the FIFO uses. */
static void payload6(uint8_t raw[6], uint16_t first, uint16_t second, uint16_t third)
{
	raw[0] = (uint8_t)(first & 0xFFU);
	raw[1] = (uint8_t)(first >> 8);
	raw[2] = (uint8_t)(second & 0xFFU);
	raw[3] = (uint8_t)(second >> 8);
	raw[4] = (uint8_t)(third & 0xFFU);
	raw[5] = (uint8_t)(third >> 8);
}

ZTEST(lifegps_sflp, test_tag_field_is_bits_7_to_3)
{
	zassert_equal(lgps_fifo_tag(0x00U), LGPS_FIFO_TAG_EMPTY, "empty tag");
	zassert_equal(lgps_fifo_tag((uint8_t)(LGPS_FIFO_TAG_SFLP_GAME_ROTATION << 3)),
		      LGPS_FIFO_TAG_SFLP_GAME_ROTATION, "quaternion tag");
	zassert_equal(lgps_fifo_tag((uint8_t)((LGPS_FIFO_TAG_SFLP_GRAVITY << 3) | 0x03U)),
		      LGPS_FIFO_TAG_SFLP_GRAVITY, "gravity tag with TAG_CNT = 3");
	zassert_equal(lgps_fifo_tag(0xFFU), 0x1FU, "all tag bits set");
}

ZTEST(lifegps_sflp, test_half_to_float_exact_values)
{
	static const struct {
		uint16_t half;
		float value;
	} cases[] = {
		{0x0000U, 0.0F},
		{0x8000U, 0.0F},          /* negative zero compares equal to zero */
		{0x3C00U, 1.0F},
		{0xBC00U, -1.0F},
		{0x3800U, 0.5F},
		{0x4000U, 2.0F},
		{0xC000U, -2.0F},
		{0x3555U, 0.333251953125F},
		{0x0400U, 6.103515625e-05F},  /* smallest normal, 2^-14 */
		{0x0001U, 5.960464477539063e-08F}, /* smallest subnormal, 2^-24 */
		{0x7BFFU, 65504.0F},            /* largest finite */
	};

	for (size_t index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index++) {
		float got = lgps_sflp_half_to_float(cases[index].half);

		zassert_within(got, cases[index].value,
			       fabsf(cases[index].value) > 1000.0F ? 0.01F : HALF_ABS_TOLERANCE,
			       "half 0x%04x decoded as %f, expected %f", cases[index].half,
			       (double)got, (double)cases[index].value);
	}
}

ZTEST(lifegps_sflp, test_half_infinity_collapses_to_zero)
{
	/* 0x7C00 is +inf, 0x7E00 is NaN: both are unreachable for SFLP output and
	 * must not turn into a quaternion component the range check cannot see.
	 */
	zassert_equal(lgps_sflp_half_to_float(0x7C00U), 0.0F, "+inf");
	zassert_equal(lgps_sflp_half_to_float(0x7E00U), 0.0F, "nan");
}

ZTEST(lifegps_sflp, test_gravity_scale)
{
	/* 0.061 mg/LSB is the accelerometer +/-2 g sensitivity, mg. */
	zassert_within(lgps_sflp_gravity_mg(100), 6.1F, 1e-4F, "+100 LSB");
	zassert_within(lgps_sflp_gravity_mg(-100), -6.1F, 1e-4F, "-100 LSB");
	/* A 1 g gravity vector is about 16393 LSB. */
	zassert_within(lgps_sflp_gravity_mg(16384), 999.424F, 1e-2F, "16384 LSB is 1 g");
}

ZTEST(lifegps_sflp, test_quaternion_identity)
{
	uint8_t raw[6];
	float quat[4] = {0.0F, 0.0F, 0.0F, 0.0F};

	payload6(raw, 0x0000U, 0x0000U, 0x0000U);
	lgps_sflp_game_quat(raw, quat);

	zassert_within(quat[3], 1.0F, QUAT_TOLERANCE, "w");
	zassert_within(quat[0], 0.0F, QUAT_TOLERANCE, "x");
	zassert_within(quat[1], 0.0F, QUAT_TOLERANCE, "y");
	zassert_within(quat[2], 0.0F, QUAT_TOLERANCE, "z");
}

ZTEST(lifegps_sflp, test_quaternion_reconstructs_w)
{
	uint8_t raw[6];
	float quat[4] = {0.0F, 0.0F, 0.0F, 0.0F};

	/* x = 0.5, y = z = 0 -> w = sqrt(0.75). This is the 60 degree rotation. */
	payload6(raw, 0x3800U, 0x0000U, 0x0000U);
	lgps_sflp_game_quat(raw, quat);

	zassert_within(quat[0], 0.5F, QUAT_TOLERANCE, "x");
	zassert_within(quat[1], 0.0F, QUAT_TOLERANCE, "y");
	zassert_within(quat[2], 0.0F, QUAT_TOLERANCE, "z");
	zassert_within(quat[3], sqrtf(0.75F), QUAT_TOLERANCE, "w = sqrt(1 - x^2)");
}

ZTEST(lifegps_sflp, test_quaternion_renormalises_rounding_overshoot)
{
	uint8_t raw[6];
	float quat[4] = {0.0F, 0.0F, 0.0F, 0.0F};
	const float norm = sqrtf(1.25F);

	/* x = 1.0, y = 0.5: the squared sum is 1.25, so x/y/z are rescaled and w
	 * becomes zero instead of sqrt(-0.25).
	 */
	payload6(raw, 0x3C00U, 0x3800U, 0x0000U);
	lgps_sflp_game_quat(raw, quat);

	zassert_within(quat[0], 1.0F / norm, QUAT_TOLERANCE, "x rescaled");
	zassert_within(quat[1], 0.5F / norm, QUAT_TOLERANCE, "y rescaled");
	zassert_within(quat[2], 0.0F, QUAT_TOLERANCE, "z");
	zassert_within(quat[3], 0.0F, QUAT_TOLERANCE, "w = 0");

	/* Still a unit quaternion, which is what the attitude consumers assume. */
	zassert_within((quat[0] * quat[0]) + (quat[1] * quat[1]) + (quat[2] * quat[2]) +
			       (quat[3] * quat[3]),
		       1.0F, QUAT_TOLERANCE, "unit length");
}

ZTEST(lifegps_sflp, test_quaternion_rejects_null)
{
	float quat[4] = {9.0F, 9.0F, 9.0F, 9.0F};

	/* Contract: a NULL payload leaves the output untouched, it does not crash. */
	lgps_sflp_game_quat(NULL, quat);
	zassert_equal(quat[0], 9.0F, "output untouched");
}

ZTEST_SUITE(lifegps_sflp, NULL, NULL, NULL, NULL, NULL);
