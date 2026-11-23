/*
 * ztest suite for Firmware/src/power/battery_soc.c.
 *
 * These are the same anchors as the App JVM test
 * (App/app/src/test/java/com/lifegps/app/ble/BatterySocTest.kt): both sides must
 * agree until the App fallback for older firmware is removed.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/ztest.h>

#include <lifegps/battery.h>

static void expect_pct(int32_t millivolts, bool charging, uint8_t expected)
{
	uint8_t percent = 0xAAU;
	int ret = lgps_battery_soc_pct(millivolts, charging, &percent);

	zassert_equal(ret, 0, "mv=%d charging=%d ret=%d", millivolts, charging, ret);
	zassert_equal((uint32_t)percent, (uint32_t)expected, "mv=%d charging=%d",
		      millivolts, charging);
}

ZTEST(lifegps_battery_soc, test_ocv_points)
{
	expect_pct(3230, false, 0U);
	expect_pct(3427, false, 5U);
	expect_pct(3809, false, 40U);
	expect_pct(3869, false, 50U);
	expect_pct(4029, false, 75U);
	expect_pct(4113, false, 95U);
	expect_pct(4173, false, 100U);
}

ZTEST(lifegps_battery_soc, test_charge_points)
{
	uint8_t charging_pct;
	uint8_t discharging_pct;

	expect_pct(3281, true, 0U);
	expect_pct(3926, true, 50U);
	expect_pct(4200, true, 100U);

	/* At the same terminal voltage the pack is less full while charging. */
	zassert_equal(lgps_battery_soc_pct(3900, true, &charging_pct), 0,
		      "charging interpolation failed");
	zassert_equal(lgps_battery_soc_pct(3900, false, &discharging_pct), 0,
		      "OCV interpolation failed");
	zassert_true(charging_pct < discharging_pct,
		     "charging %u%% must be below OCV %u%%", charging_pct, discharging_pct);
}

ZTEST(lifegps_battery_soc, test_clamps_and_invalid)
{
	expect_pct(2500, false, 0U);
	expect_pct(LGPS_BATT_MV_PLAUSIBLE_MIN - 1, false, LGPS_BATT_SOC_UNKNOWN);
	expect_pct(0, false, LGPS_BATT_SOC_UNKNOWN);
	expect_pct(4300, false, 100U);
}

ZTEST(lifegps_battery_soc, test_null_percent)
{
	zassert_equal(lgps_battery_soc_pct(3800, false, NULL), -EINVAL,
		      "NULL percent must be rejected");
}

ZTEST_SUITE(lifegps_battery_soc, NULL, NULL, NULL, NULL, NULL);
