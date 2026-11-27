/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_PM_DEVICE)
#include <zephyr/pm/device.h>
#endif
#include <zephyr/sys/poweroff.h>

#include <errno.h>

#include <lifegps/battery.h>
#include <lifegps/diag.h>
#include <lifegps/gnss.h>
#include <lifegps/imu.h>
#include <lifegps/keys.h>
#include <lifegps/led.h>
#include <lifegps/power_sm.h>

LOG_MODULE_REGISTER(lgps_power, LOG_LEVEL_INF);

/*
 * Nine slow blinks (three colours x on/off) plus a small margin.
 */
#define SHUTDOWN_SEQUENCE_MS (3U * 3U * 2U * 500U + 500U)
/* Hold duration required on KEY1_POI after waking from System OFF (prevents pocket bumps). */
#define WAKE_CONFIRM_HOLD_MS 3000U

static const struct gpio_dt_spec key_poi_wake = GPIO_DT_SPEC_GET(DT_NODELABEL(key_poi), gpios);
static const struct device *const ext_nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));

static K_MUTEX_DEFINE(power_lock);
static struct lgps_power_sm_logic sm_logic;
static bool shutdown_pending;
static uint8_t last_soc_pct = LGPS_BATT_SOC_UNKNOWN;
static bool last_charging;
static struct k_work_delayable shutdown_work;
static lgps_power_poi_cb_t poi_callback;
static lgps_power_pairing_cb_t pairing_callback;

static void schedule_shutdown_locked(void)
{
	if (shutdown_pending) {
		return;
	}

	shutdown_pending = true;
	lgps_power_logic_request_shutdown(&sm_logic);
	(void)lgps_led_off_all();
	(void)lgps_led_play(LGPS_LED_PATTERN_SHUTDOWN_CONFIRM);
	(void)k_work_schedule(&shutdown_work, K_MSEC(SHUTDOWN_SEQUENCE_MS));
}

static void enter_system_off(void)
{
	LOG_INF("shutdown confirmed: powering down GNSS/IMU/NOR and entering System OFF");

	lgps_gnss_force_off();
	(void)lgps_imu_power_down();
	(void)lgps_led_off_all();

#if defined(CONFIG_PM_DEVICE)
	if (device_is_ready(ext_nor)) {
		(void)pm_device_action_run(ext_nor, PM_DEVICE_ACTION_SUSPEND);
	}
#endif

	/* Configure KEY1_POI (P1.06, active-low with internal pull-up) as System OFF wake. */
	if (gpio_is_ready_dt(&key_poi_wake)) {
		(void)gpio_pin_interrupt_configure_dt(&key_poi_wake, GPIO_INT_LEVEL_LOW);
	}

	/* Let the last UART line drain before the rail collapses. */
	k_msleep(50);

	sys_poweroff();
}

void lgps_power_boot_confirm(void)
{
	uint32_t cause = 0U;

	if (!IS_ENABLED(CONFIG_LGPS_BOOT_HOLD_8S)) {
		return;
	}

	if (hwinfo_get_reset_cause(&cause) != 0) {
		return;
	}

	/* Only gate wakes from System OFF; POR, J-Link debug, and OTA/software reboots boot immediately. */
	if ((cause & RESET_LOW_POWER_WAKE) == 0U) {
		return;
	}

	if (!gpio_is_ready_dt(&key_poi_wake)) {
		return;
	}
	(void)gpio_pin_configure_dt(&key_poi_wake, GPIO_INPUT);

	for (uint32_t held_ms = 0U; held_ms < WAKE_CONFIRM_HOLD_MS; held_ms += 50U) {
		if (gpio_pin_get_dt(&key_poi_wake) <= 0) {
			LOG_INF("System OFF wake aborted (KEY1 released at %u ms)", held_ms);
			(void)gpio_pin_interrupt_configure_dt(&key_poi_wake, GPIO_INT_LEVEL_LOW);
			k_msleep(20);
			sys_poweroff();
		}
		k_msleep(50);
	}

	LOG_INF("System OFF wake confirmed (KEY1 held %u ms)", WAKE_CONFIRM_HOLD_MS);
}

static void shutdown_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	enter_system_off();
}

static void on_key(enum lgps_key_id key, enum lgps_key_event_type event, uint32_t held_ms)
{
	bool call_poi = false;
	bool call_pairing = false;

	k_mutex_lock(&power_lock, K_FOREVER);

	switch (event) {
	case LGPS_KEY_EVENT_LONG_PRESS:
		LOG_INF("%s held %u ms -> power off sequence",
			key == LGPS_KEY_POI ? "KEY1_POI" : "KEY2_MODE", held_ms);
		schedule_shutdown_locked();
		break;

	case LGPS_KEY_EVENT_PAIRING_PRESS:
		if (key == LGPS_KEY_POI) {
			LOG_INF("BLE pairing hold detected (KEY1_POI, %u ms)", held_ms);
			(void)lgps_led_play(LGPS_LED_PATTERN_SYNC);
			call_pairing = true;
		}
		break;

	case LGPS_KEY_EVENT_PRESS:
		if (key == LGPS_KEY_POI) {
			LOG_INF("POI mark (KEY1_POI pressed)");
			(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
			call_poi = true;
		}
		break;

	case LGPS_KEY_EVENT_SHORT_PRESS:
		if (key == LGPS_KEY_MODE) {
			bool changed = lgps_power_logic_toggle_precision(
				&sm_logic, lgps_diag_uptime_ms(), last_soc_pct, last_charging);

			if (changed) {
				bool hp = (sm_logic.state == LGPS_PWR_HIGH_PRECISION);

				LOG_INF("high precision mode %s (KEY2_MODE, held %u ms)",
					hp ? "enabled" : "disabled", held_ms);
				if (hp) {
					(void)lgps_led_play(LGPS_LED_PATTERN_PRECISION);
				}
			} else {
				LOG_WRN("HIGH_PRECISION refused (low battery %u%%)", last_soc_pct);
				(void)lgps_led_play(LGPS_LED_PATTERN_LOW_BATTERY);
			}
		}
		break;

	case LGPS_KEY_EVENT_RELEASE:
	default:
		break;
	}

	k_mutex_unlock(&power_lock);

	if (call_poi && (poi_callback != NULL)) {
		poi_callback();
	}
	if (call_pairing && (pairing_callback != NULL)) {
		pairing_callback();
	}
}

void lgps_power_register_callbacks(lgps_power_poi_cb_t poi_cb,
				   lgps_power_pairing_cb_t pairing_cb)
{
	k_mutex_lock(&power_lock, K_FOREVER);
	poi_callback = poi_cb;
	pairing_callback = pairing_cb;
	k_mutex_unlock(&power_lock);
}

enum lgps_power_state lgps_power_get_state(void)
{
	enum lgps_power_state st;

	k_mutex_lock(&power_lock, K_FOREVER);
	st = sm_logic.state;
	k_mutex_unlock(&power_lock);

	return st;
}

bool lgps_power_precision_requested(void)
{
	return lgps_power_get_state() == LGPS_PWR_HIGH_PRECISION;
}

enum lgps_power_state lgps_power_tick(uint32_t now_ms, uint32_t steps_total, bool zupt_locked,
				      bool ble_connected, bool charging, uint8_t soc_pct)
{
	struct lgps_power_inputs in = {
		.now_ms = now_ms,
		.steps_total = steps_total,
		.zupt_locked = zupt_locked,
		.ble_connected = ble_connected,
		.charging = charging,
		.soc_pct = soc_pct,
	};
	enum lgps_power_state prev;
	enum lgps_power_state cur;

	k_mutex_lock(&power_lock, K_FOREVER);
	last_soc_pct = soc_pct;
	last_charging = charging;
	prev = sm_logic.state;
	(void)lgps_power_logic_step(&sm_logic, &in);
	cur = sm_logic.state;

	if (sm_logic.system_off_requested && !shutdown_pending) {
		LOG_WRN("critical battery (%u%%), entering System OFF", soc_pct);
		schedule_shutdown_locked();
	}
	k_mutex_unlock(&power_lock);

	if (cur != prev) {
		LOG_INF("power state: %d -> %d", (int)prev, (int)cur);
	}

	return cur;
}

void lgps_power_on_motion_irq(uint32_t now_ms, uint32_t steps_total)
{
	bool woke;

	k_mutex_lock(&power_lock, K_FOREVER);
	woke = lgps_power_logic_on_motion_irq(&sm_logic, now_ms, steps_total);
	k_mutex_unlock(&power_lock);

	if (woke) {
		LOG_INF("motion IRQ -> tentative ACTIVE wake window (3 s / >=2 steps)");
	}
}

int lgps_power_set_mode(enum lgps_power_state target)
{
	int ret;

	k_mutex_lock(&power_lock, K_FOREVER);
	ret = lgps_power_logic_set_mode(&sm_logic, target, lgps_diag_uptime_ms(), last_soc_pct,
					last_charging);
	if ((ret == 0) && (target == LGPS_PWR_SYSTEM_OFF)) {
		schedule_shutdown_locked();
	}
	k_mutex_unlock(&power_lock);

	return ret;
}

int lgps_power_inject_key(enum lgps_key_id key, enum lgps_key_event_type event)
{
	if (key >= LGPS_KEY_COUNT) {
		return -EINVAL;
	}

	on_key(key, event, 100U);
	return 0;
}

int lgps_power_sm_init(void)
{
	int ret;

	lgps_power_logic_init(&sm_logic, lgps_diag_uptime_ms());
	/* In debug builds (GNSS always on), disable auto-sleep so bench tests stay active. */
	if (IS_ENABLED(CONFIG_LGPS_GNSS_ALWAYS_ON)) {
		lgps_power_logic_set_auto_sleep_ms(&sm_logic, 0U);
	}

	k_work_init_delayable(&shutdown_work, shutdown_work_handler);

	ret = lgps_keys_init(on_key);
	if (ret < 0) {
		return ret;
	}

	(void)lgps_led_play(LGPS_LED_PATTERN_BOOT_CONFIRM);

	LOG_INF("power state machine ready (4-state, auto_sleep=%u ms)", sm_logic.auto_sleep_ms);

	return 0;
}
