/* SPDX-License-Identifier: MIT */
#include <zephyr/ztest.h>
#include <lifegps/power_sm_logic.h>

ZTEST_SUITE(power_sm_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(power_sm_tests, test_auto_sleep_and_motion_confirm_gate)
{
	struct lgps_power_sm_logic sm;
	struct lgps_power_inputs in = {
		.now_ms = 0U,
		.steps_total = 0U,
		.zupt_locked = true,
		.ble_connected = false,
		.charging = false,
		.soc_pct = 80U,
	};

	lgps_power_logic_init(&sm, 0U);
	zassert_equal(sm.state, LGPS_PWR_ACTIVE);

	/* ZUPT locked for 30 s with BLE disconnected -> DEEP_SLEEP. */
	zassert_false(lgps_power_logic_step(&sm, &in));
	in.now_ms = 29999U;
	zassert_false(lgps_power_logic_step(&sm, &in));
	in.now_ms = 30000U;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* False bump: IRQ wakes to tentative ACTIVE, only 1 step in 3 s -> back to DEEP_SLEEP. */
	zassert_true(lgps_power_logic_on_motion_irq(&sm, 40000U, 10U));
	zassert_equal(sm.state, LGPS_PWR_ACTIVE);
	zassert_true(sm.wake_confirming);

	in.now_ms = 42000U;
	in.steps_total = 11U;
	in.zupt_locked = false;
	zassert_false(lgps_power_logic_step(&sm, &in));

	in.now_ms = 43000U;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Real walking: IRQ + 2 steps within 2 s confirms ACTIVE. */
	zassert_true(lgps_power_logic_on_motion_irq(&sm, 50000U, 11U));
	in.now_ms = 51500U;
	in.steps_total = 13U;
	zassert_false(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_ACTIVE);
	zassert_false(sm.wake_confirming);
}

ZTEST(power_sm_tests, test_low_battery_degrade_and_shutdown)
{
	struct lgps_power_sm_logic sm;
	struct lgps_power_inputs in = {
		.now_ms = 1000U,
		.steps_total = 0U,
		.zupt_locked = false,
		.ble_connected = true,
		.charging = false,
		.soc_pct = 50U,
	};

	lgps_power_logic_init(&sm, 0U);
	zassert_true(lgps_power_logic_toggle_precision(&sm, 1000U, 50U, false));
	zassert_equal(sm.state, LGPS_PWR_HIGH_PRECISION);

	/* SOC <= 10% forces HIGH_PRECISION -> ACTIVE. */
	in.soc_pct = 10U;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_ACTIVE);

	/* Refuses HIGH_PRECISION while low battery and not charging. */
	zassert_false(lgps_power_logic_toggle_precision(&sm, 2000U, 10U, false));
	/* Allowed while charging. */
	zassert_true(lgps_power_logic_toggle_precision(&sm, 2000U, 10U, true));
	zassert_equal(sm.state, LGPS_PWR_HIGH_PRECISION);

	/* Critical battery <= 3% not charging -> SYSTEM_OFF. */
	in.soc_pct = 3U;
	in.charging = false;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_SYSTEM_OFF);
	zassert_true(sm.system_off_requested);
}

ZTEST(power_sm_tests, test_auto_off_in_deep_sleep)
{
	struct lgps_power_sm_logic sm;
	struct lgps_power_inputs in = {
		.now_ms = 0U,
		.steps_total = 0U,
		.zupt_locked = true,
		.ble_connected = false,
		.charging = false,
		.soc_pct = 80U,
	};

	/* Disabled by default (auto_off_min == 0): stays in DEEP_SLEEP indefinitely. */
	lgps_power_logic_init(&sm, 0U);
	zassert_equal(sm.auto_off_min, 0U);
	zassert_false(lgps_power_logic_step(&sm, &in));
	in.now_ms = 30000U;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);
	zassert_equal(sm.deep_sleep_since_ms, 30000U);

	in.now_ms = 30000U + 3600000U; /* +60 min */
	zassert_false(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Enable 5-minute auto-off: suppressed while charging or BLE connected. */
	lgps_power_logic_init(&sm, 0U);
	lgps_power_logic_set_auto_off_min(&sm, 5U);
	in.now_ms = 0U;
	in.charging = false;
	in.ble_connected = false;
	zassert_false(lgps_power_logic_step(&sm, &in));
	in.now_ms = 30000U;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Just before 5-minute threshold -> stays in DEEP_SLEEP. */
	in.now_ms = 30000U + (5U * 60000U) - 1U;
	zassert_false(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Past threshold while charging -> suppressed. */
	in.now_ms = 30000U + (5U * 60000U);
	in.charging = true;
	zassert_false(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Past threshold while BLE connected -> suppressed. */
	in.charging = false;
	in.ble_connected = true;
	zassert_false(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_DEEP_SLEEP);

	/* Idle (not charging and BLE disconnected) past threshold -> SYSTEM_OFF. */
	in.ble_connected = false;
	zassert_true(lgps_power_logic_step(&sm, &in));
	zassert_equal(sm.state, LGPS_PWR_SYSTEM_OFF);
	zassert_true(sm.system_off_requested);
}
