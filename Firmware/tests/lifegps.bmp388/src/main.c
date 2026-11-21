/*
 * Life_GPS firmware - ztest coverage for the BMP388 TPG compensation.
 *
 * Pure maths: no device tree, no bus, no sensor. The expected numbers are
 * derived from the datasheet (BST-BMP388-DS001-07 sections 9.1..9.3) with the
 * trimming block below, which is a synthetic but self-consistent part: applied
 * to the 24-bit raw pair of the 2026-11-21 bench burst it yields ~23 degC and
 * ~1012 hPa, i.e. the values a healthy BMP388 next to the ~23 degC IMU die
 * must produce.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include <lifegps/bmp388.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/*
 * Trimming coefficients 0x31..0x45 of the synthetic part:
 *   T1 = 28961, T2 = 22000, T3 = 3
 *   P1 = 30797, P2 = 16000, P3 = -20, P4 = 5, P5 = 100, P6 = 1000,
 *   P7 = 3, P8 = 2, P9 = 300, P10 = 5, P11 = 4
 */
static const uint8_t test_nvm[LGPS_BMP388_NVM_LEN] = {
	0x21, 0x71,             /* NVM_T1 = 28961 (unsigned 16) */
	0xf0, 0x55,             /* NVM_T2 = 22000 (unsigned 16) */
	0x03,                   /* NVM_T3 = 3     (signed 8) */
	0x4d, 0x78,             /* NVM_P1 = 30797 (signed 16) */
	0x80, 0x3e,             /* NVM_P2 = 16000 (signed 16) */
	0xec,                   /* NVM_P3 = -20   (signed 8) */
	0x05,                   /* NVM_P4 = 5     (signed 8) */
	0x64, 0x00,             /* NVM_P5 = 100   (unsigned 16) */
	0xe8, 0x03,             /* NVM_P6 = 1000  (unsigned 16) */
	0x03,                   /* NVM_P7 = 3     (signed 8) */
	0x02,                   /* NVM_P8 = 2     (signed 8) */
	0x2c, 0x01,             /* NVM_P9 = 300   (signed 16) */
	0x05,                   /* NVM_P10 = 5    (signed 8) */
	0x04,                   /* NVM_P11 = 4    (signed 8) */
};

/* First hub burst of the 2026-11-21 bench log: baro 60 09 6f 80 3b 82. */
#define BENCH_RAW_PRESSURE 7277088U  /* 0x6f0960 */
#define BENCH_RAW_TEMPERATURE 8535808U /* 0x823b80 */

/* Section 9.1 puts every divisor at a power of two, so the decoded coefficients
 * are exactly representable and can be pinned; the tolerance only absorbs the
 * float rounding of the literals below.
 */
#define TEST_REL_TOLERANCE 1e-6F

static void assert_relative(float got, float want)
{
	zassert_true(fabsf(got - want) <= TEST_REL_TOLERANCE * fabsf(want),
		     "got %.9g, want %.9g", (double)got, (double)want);
}

static void decode_default(struct lgps_bmp388_calib *calib)
{
	zassert_ok(lgps_bmp388_calib_decode(test_nvm, calib), "decode of a valid block failed");
}

ZTEST(lifegps_bmp388, test_decode_coefficients)
{
	struct lgps_bmp388_calib calib;

	decode_default(&calib);

	/* Section 9.1 scaling, one coefficient per exponent in the datasheet. */
	assert_relative(calib.par_t1, 7414016.0F);          /* NVM_T1 / 2^-8 */
	assert_relative(calib.par_t2, 2.04890966e-05F);     /* NVM_T2 / 2^30 */
	assert_relative(calib.par_t3, 1.06581410e-14F);     /* NVM_T3 / 2^48 */
	assert_relative(calib.par_p1, 0.0137453079F);       /* (P1 - 2^14) / 2^20 */
	assert_relative(calib.par_p2, -7.15255737e-07F);    /* (P2 - 2^14) / 2^29 */
	assert_relative(calib.par_p3, -4.65661287e-09F);    /* P3 / 2^32 */
	assert_relative(calib.par_p4, 3.63797881e-11F);     /* P4 / 2^37 */
	assert_relative(calib.par_p5, 800.0F);              /* P5 / 2^-3 */
	assert_relative(calib.par_p6, 15.625F);             /* P6 / 2^6 */
	assert_relative(calib.par_p7, 0.01171875F);         /* P7 / 2^8 */
	assert_relative(calib.par_p8, 6.10351562e-05F);     /* P8 / 2^15 */
	assert_relative(calib.par_p9, 1.06581410e-12F);     /* P9 / 2^48 */
	assert_relative(calib.par_p10, 1.77635684e-14F);    /* P10 / 2^48 */
	assert_relative(calib.par_p11, 1.08420217e-19F);    /* P11 / 2^65 */

	/* The signed fields must keep their sign: reading P2/P3 as unsigned would turn
	 * a small correction into a large one.
	 */
	zassert_true(calib.par_p3 < 0.0F, "NVM_P3 = -20 decoded as positive");
	zassert_true(calib.par_p2 < 0.0F, "NVM_P2 below 2^14 decoded as positive");
	zassert_true(calib.par_p1 > 0.0F, "NVM_P1 above 2^14 decoded as negative");
}

ZTEST(lifegps_bmp388, test_decode_rejects_unprogrammed_block)
{
	struct lgps_bmp388_calib calib;
	uint8_t blank[LGPS_BMP388_NVM_LEN];

	/* All zero: what a slave that never answered leaves in the hub block. */
	memset(blank, 0x00, sizeof(blank));
	zassert_equal(lgps_bmp388_calib_decode(blank, &calib), -EINVAL,
		      "an all-zero block must be rejected");

	/* All ones: NVM that could not be read at all. */
	memset(blank, 0xFF, sizeof(blank));
	zassert_equal(lgps_bmp388_calib_decode(blank, &calib), -EINVAL,
		      "an all-ones block must be rejected");

	/* A block with only the temperature offset present has no gain: it would
	 * report 0 degC and an offset-only pressure forever.
	 */
	memset(blank, 0x00, sizeof(blank));
	blank[0] = 0x21;
	blank[1] = 0x71;
	zassert_equal(lgps_bmp388_calib_decode(blank, &calib), -EINVAL,
		      "par_t2 = 0 must be rejected");

	zassert_equal(lgps_bmp388_calib_decode(NULL, &calib), -EINVAL, "NULL nvm accepted");
	zassert_equal(lgps_bmp388_calib_decode(test_nvm, NULL), -EINVAL, "NULL out accepted");
}

ZTEST(lifegps_bmp388, test_bench_burst_compensates_to_physical_values)
{
	struct lgps_bmp388_calib calib;
	float pressure_pa = 0.0F;
	float temperature_c = 0.0F;

	decode_default(&calib);

	zassert_ok(lgps_bmp388_compensate(&calib, BENCH_RAW_PRESSURE, BENCH_RAW_TEMPERATURE,
					  &pressure_pa, &temperature_c),
		   "compensation of the bench burst failed");

	/* 24-bit raw, not shifted: the ADC values of sections 3.10 and 9.x are used
	 * as read. Shifting them (as a 20-bit BMP280 pipeline would) makes the
	 * temperature term collapse and this test fails by hundreds of degrees.
	 */
	zassert_within(temperature_c, 22.9979F, 0.001F, "temperature = %.4f degC",
		       (double)temperature_c);
	zassert_within(pressure_pa, 101177.55F, 0.5F, "pressure = %.2f Pa",
		       (double)pressure_pa);

	/* The result has to sit inside the part's specified range; a sign or
	 * exponent error in the coefficient scaling would not.
	 */
	zassert_true(temperature_c > LGPS_BMP388_TEMP_MIN_C &&
			     temperature_c < LGPS_BMP388_TEMP_MAX_C,
		     "temperature outside the operating range");
	zassert_true(pressure_pa > LGPS_BMP388_PRESS_MIN_PA &&
			     pressure_pa < LGPS_BMP388_PRESS_MAX_PA,
		     "pressure outside the operating range");
}

ZTEST(lifegps_bmp388, test_raw_values_map_monotonically)
{
	struct lgps_bmp388_calib calib;
	float pressure_pa = 0.0F;
	float temperature_c = 0.0F;
	float pressed_pa = 0.0F;
	float warmed_c = 0.0F;

	decode_default(&calib);

	/* One ADC count of pressure must raise the result: no clamping, no lost
	 * term. The sensor's LSB is far below one Pa, so compare against zero.
	 */
	zassert_ok(lgps_bmp388_compensate(&calib, BENCH_RAW_PRESSURE, BENCH_RAW_TEMPERATURE,
					  &pressure_pa, &temperature_c));
	zassert_ok(lgps_bmp388_compensate(&calib, BENCH_RAW_PRESSURE + 1U,
					  BENCH_RAW_TEMPERATURE, &pressed_pa, &warmed_c));
	zassert_true(pressed_pa > pressure_pa, "one more pressure LSB did not raise Pa");

	/* Same for the temperature raw value (1024 counts is a visible step: with
	 * par_t2 = 2.049e-5 degC per count that is +0.021 degC).
	 */
	zassert_ok(lgps_bmp388_compensate(&calib, BENCH_RAW_PRESSURE,
					  BENCH_RAW_TEMPERATURE + 1024U, &pressed_pa,
					  &warmed_c));
	zassert_true(warmed_c > temperature_c, "one more temperature LSB did not raise degC");
	zassert_within(warmed_c, 23.0189F, 0.001F, "1024 temp counts = %.4f degC",
		       (double)warmed_c);
}

ZTEST(lifegps_bmp388, test_null_arguments)
{
	struct lgps_bmp388_calib calib;
	float value = 0.0F;

	decode_default(&calib);

	zassert_equal(lgps_bmp388_compensate(NULL, 0U, 0U, &value, &value), -EINVAL,
		      "NULL calib accepted");
	zassert_equal(lgps_bmp388_compensate(&calib, 0U, 0U, NULL, &value), -EINVAL,
		      "NULL pressure pointer accepted");
	zassert_equal(lgps_bmp388_compensate(&calib, 0U, 0U, &value, NULL), -EINVAL,
		      "NULL temperature pointer accepted");
}

ZTEST_SUITE(lifegps_bmp388, NULL, NULL, NULL, NULL, NULL);
