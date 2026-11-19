/*
 * Life_GPS firmware - ztest coverage for the barometric baseline / altitude.
 *
 * Pure maths plus the automatic-baseline gate; the NOR record is replaced by the
 * in-memory stub at the bottom of this file, so no flash driver, device tree or
 * bus is involved.
 *
 * The expected heights come from the ICAO standard atmosphere used by
 * lifegps/baro.h (h = 44330 * (1 - (p/p0)^0.1902632)), computed independently and
 * pinned here to catch an exponent or a scale slip.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include <lifegps/baro.h>
#include <lifegps/baro_store.h>
#include <lifegps/gnss.h>

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Standard atmosphere reference points, millimetres. */
#define TEST_ALT_TOLERANCE_MM 5
#define TEST_SEA_LEVEL_PA 101325.0F
#define TEST_940_M_PA 95000.0F
#define TEST_1457_M_PA 85000.0F

/* --- NOR store stub ------------------------------------------------------- */

static uint32_t save_calls;
static uint32_t last_saved_q4;
static bool last_saved_auto;

int lgps_baro_store_load(uint32_t *pa_q4, bool *automatic, uint32_t *unix_s)
{
	(void)pa_q4;
	(void)automatic;
	(void)unix_s;

	return -ENOENT; /* a factory-fresh unit has no record */
}

int lgps_baro_store_save(uint32_t pa_q4, bool automatic, uint32_t unix_s)
{
	(void)unix_s;

	save_calls++;
	last_saved_q4 = pa_q4;
	last_saved_auto = automatic;

	return 0;
}

/* --- helpers -------------------------------------------------------------- */

static struct lgps_gnss_fix good_fix(uint16_t speed_cms)
{
	struct lgps_gnss_fix fix = {
		.valid = true,
		.utc_valid = true,
		.fix_type = 3,
		.satellites = 12,
		.pdop_1e2 = 120, /* pDOP 1.20 */
		.speed_cms = speed_cms,
	};

	return fix;
}

static void feed_constant(float pressure_pa, const struct lgps_gnss_fix *fix, uint32_t seconds)
{
	for (uint32_t sample = 0U; sample < seconds; sample++) {
		(void)lgps_baro_feed(true, pressure_pa, fix);
	}
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);

	lgps_baro_init();
	save_calls = 0U;
	last_saved_q4 = 0U;
	last_saved_auto = false;
}

/* --- altitude formula ----------------------------------------------------- */

ZTEST(lifegps_baro, test_altitude_formula)
{
	/* At the baseline the height is exactly zero. */
	zassert_equal(lgps_baro_altitude_mm(TEST_SEA_LEVEL_PA, 101325U * 16U), 0,
		      "pressure equal to the baseline must give 0 mm");

	/* 950 hPa under a 1013.25 hPa baseline is 540 m up. */
	zassert_within(lgps_baro_altitude_mm(TEST_940_M_PA, 101325U * 16U), 540328,
		       TEST_ALT_TOLERANCE_MM, "950 hPa should be 540.3 m above the baseline");

	/* 850 hPa is 1457 m up. */
	zassert_within(lgps_baro_altitude_mm(TEST_1457_M_PA, 101325U * 16U), 1457275,
		       TEST_ALT_TOLERANCE_MM, "850 hPa should be 1457.3 m above the baseline");

	/* Below the baseline the sign must flip, not clamp to zero. */
	zassert_within(lgps_baro_altitude_mm(TEST_SEA_LEVEL_PA, 95000U * 16U), -546995,
		       TEST_ALT_TOLERANCE_MM, "sea level under a 950 hPa baseline is negative");
}

ZTEST(lifegps_baro, test_altitude_without_baseline)
{
	zassert_equal(lgps_baro_altitude_mm(TEST_SEA_LEVEL_PA, LGPS_BARO_BASELINE_UNSET), 0,
		      "no baseline must report 0, never a made-up height");
	zassert_equal(lgps_baro_altitude_mm(0.0F, 101325U * 16U), 0,
		      "no pressure must report 0");
	zassert_equal(lgps_baro_altitude_mm(-5.0F, 101325U * 16U), 0,
		      "a negative pressure is not a measurement");
}

/* --- automatic baseline gate ---------------------------------------------- */

ZTEST(lifegps_baro, test_gate_sets_baseline_after_window)
{
	const struct lgps_gnss_fix fix = good_fix(0U);

	zassert_false(lgps_baro_baseline_set(), "a fresh module has no baseline");

	/* One sample short of the window: still nothing. */
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES - 1U);
	zassert_false(lgps_baro_baseline_set(), "baseline must not appear early");

	feed_constant(TEST_SEA_LEVEL_PA, &fix, 1U);
	zassert_true(lgps_baro_baseline_set(), "30 good seconds must define a baseline");
	zassert_true(lgps_baro_baseline_auto(), "the gate sets an automatic baseline");
	zassert_equal(save_calls, 1U, "the new baseline must be persisted once");
	zassert_true(last_saved_auto, "the record must be marked automatic");
	zassert_within((float)lgps_baro_baseline_pa_q4() / 16.0F, TEST_SEA_LEVEL_PA, 1.0F,
		       "baseline = mean of the window");
}

ZTEST(lifegps_baro, test_gate_rejects_moving_unit)
{
	/* 2 m/s: the height may change during the window. */
	const struct lgps_gnss_fix walking = good_fix(200U);

	feed_constant(TEST_SEA_LEVEL_PA, &walking, 3U * LGPS_BARO_AUTO_WINDOW_SAMPLES);

	zassert_false(lgps_baro_baseline_set(), "a moving unit must not define a baseline");
	zassert_equal(save_calls, 0U, "no record for a rejected window");
}

ZTEST(lifegps_baro, test_gate_rejects_poor_fix)
{
	struct lgps_gnss_fix fix = good_fix(0U);

	fix.satellites = LGPS_BARO_AUTO_MIN_SATELLITES - 1U;
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_false(lgps_baro_baseline_set(), "too few satellites must not define a baseline");

	fix = good_fix(0U);
	fix.pdop_1e2 = LGPS_BARO_AUTO_MAX_PDOP_1E2 + 1U;
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_false(lgps_baro_baseline_set(), "pDOP above 2.0 must not define a baseline");

	/* pDOP = 0 means "not reported", which is not a perfect geometry. */
	fix = good_fix(0U);
	fix.pdop_1e2 = 0U;
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_false(lgps_baro_baseline_set(), "unknown pDOP must not define a baseline");

	fix = good_fix(0U);
	fix.fix_type = 2U;
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_false(lgps_baro_baseline_set(), "a 2D fix must not define a baseline");

	fix = good_fix(0U);
	fix.valid = false;
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_false(lgps_baro_baseline_set(), "an invalid fix must not define a baseline");
}

ZTEST(lifegps_baro, test_gate_rejects_noisy_pressure)
{
	const struct lgps_gnss_fix fix = good_fix(0U);

	/* +/- 300 Pa around sea level (a slam or a gust) is far above 30 Pa sigma. */
	for (uint32_t sample = 0U; sample < LGPS_BARO_AUTO_WINDOW_SAMPLES; sample++) {
		const float pressure = ((sample % 2U) == 0U) ? (TEST_SEA_LEVEL_PA - 300.0F)
							    : (TEST_SEA_LEVEL_PA + 300.0F);

		(void)lgps_baro_feed(true, pressure, &fix);
	}

	zassert_false(lgps_baro_baseline_set(), "a noisy window must not define a baseline");
}

ZTEST(lifegps_baro, test_gate_interrupted_window_restarts)
{
	const struct lgps_gnss_fix fix = good_fix(0U);
	const struct lgps_gnss_fix walking = good_fix(200U);

	/* 20 good seconds, one bad second, 20 more: the window restarts. */
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES - 10U);
	feed_constant(TEST_SEA_LEVEL_PA, &walking, 1U);
	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES - 10U);
	zassert_false(lgps_baro_baseline_set(), "an interrupted window must not count as 30 s");

	feed_constant(TEST_SEA_LEVEL_PA, &fix, 10U);
	zassert_true(lgps_baro_baseline_set(), "10 more good seconds complete the window");
}

ZTEST(lifegps_baro, test_gate_hysteresis)
{
	const struct lgps_gnss_fix fix = good_fix(0U);

	feed_constant(TEST_SEA_LEVEL_PA, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_true(lgps_baro_baseline_set(), "first window defines the baseline");
	zassert_equal(save_calls, 1U, "one record after the first window");

	/* 0.5 hPa away: below the 1 hPa hysteresis, nothing may be rewritten. */
	feed_constant(TEST_SEA_LEVEL_PA + 50.0F, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_equal(save_calls, 1U, "a 0.5 hPa change must not rewrite the record");

	/* 2 hPa away: a real reference shift, this one goes to flash. */
	feed_constant(TEST_SEA_LEVEL_PA + 200.0F, &fix, LGPS_BARO_AUTO_WINDOW_SAMPLES);
	zassert_equal(save_calls, 2U, "a 2 hPa change must persist a new baseline");
	zassert_within((float)last_saved_q4 / 16.0F, TEST_SEA_LEVEL_PA + 200.0F, 1.0F,
		       "the record carries the new reference");
}

/* --- manual baseline ------------------------------------------------------ */

ZTEST(lifegps_baro, test_take_current_then_height)
{
	const struct lgps_gnss_fix fix = good_fix(0U);

	(void)lgps_baro_feed(true, TEST_SEA_LEVEL_PA, &fix);

	zassert_equal(lgps_baro_take_current(), 0, "take-current must succeed with a pressure");
	zassert_false(lgps_baro_baseline_auto(), "a hand-set baseline is not automatic");
	zassert_equal(save_calls, 1U, "take-current persists the baseline");
	zassert_equal(lgps_baro_rel_alt_mm(), 0, "height at the baseline is zero");

	/* 12 m up: one update call recalculates the height from the new pressure. */
	(void)lgps_baro_feed(true, TEST_SEA_LEVEL_PA - 145.0F, &fix);
	zassert_within(lgps_baro_rel_alt_mm(), 12078, 5,
		       "-1.45 hPa is 12.1 m above the baseline");
}

ZTEST(lifegps_baro, test_take_current_without_pressure)
{
	zassert_equal(lgps_baro_take_current(), -ENODATA,
		      "no pressure means no baseline (the command answers NOT_SUPPORTED)");
	zassert_false(lgps_baro_baseline_set(), "a failed take must not define anything");
}

ZTEST(lifegps_baro, test_manual_baseline_range)
{
	zassert_equal(lgps_baro_set_baseline((LGPS_BARO_BASELINE_MIN_PA - 1U) * 16U, false),
		      -EINVAL, "below the sensor range must be rejected");
	zassert_equal(lgps_baro_set_baseline((LGPS_BARO_BASELINE_MAX_PA + 1U) * 16U, false),
		      -EINVAL, "above the sensor range must be rejected");
	zassert_equal(lgps_baro_set_baseline(0U, false), -EINVAL, "0 is not a pressure");
	zassert_equal(save_calls, 0U, "a rejected value must not reach the flash");

	zassert_equal(lgps_baro_set_baseline(101325U * 16U, false), 0, "sea level is valid");
	zassert_true(lgps_baro_baseline_set(), "the manual value is in effect");
	zassert_equal(lgps_baro_baseline_pa_q4(), 101325U * 16U, "the value round-trips");
	zassert_equal(save_calls, 1U, "the manual value is persisted");
}

ZTEST(lifegps_baro, test_invalid_pressure_clears_height)
{
	const struct lgps_gnss_fix fix = good_fix(0U);

	zassert_equal(lgps_baro_set_baseline(101325U * 16U, false), 0, "baseline set");
	(void)lgps_baro_feed(true, TEST_SEA_LEVEL_PA - 500.0F, &fix);
	zassert_true(lgps_baro_rel_alt_mm() > 30000, "about 42 m above the baseline");

	/* Sensor gone: the height must fall back to "unknown" (0), not stay frozen. */
	(void)lgps_baro_feed(false, 0.0F, &fix);
	zassert_equal(lgps_baro_rel_alt_mm(), 0, "no pressure means no height");
}

/* before_each is the per-test hook (4th argument), not the suite setup. */
ZTEST_SUITE(lifegps_baro, NULL, NULL, before_each, NULL, NULL);
