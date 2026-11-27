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
 * Compact shutdown sequence (3 colours x 550 ms = 1.65 s) plus margin, and
 * fast critical low-battery sequence (5 x 200 ms = 1.0 s) plus margin.
 */
#define SHUTDOWN_SEQUENCE_MS 2000U
#define CRITICAL_SHUTDOWN_MS 1150U
#define LOW_WARN_INTERVAL_MS 30000U
/*
 * Hold duration required on KEY1_POI or KEY2_MODE after waking from System OFF
 * to reject accidental pocket bumps (Q1=A: 2.5 s wake-confirm hold).
 */
#define WAKE_CONFIRM_HOLD_MS 2500U

static const struct gpio_dt_spec key_poi_wake = GPIO_DT_SPEC_GET(DT_NODELABEL(key_poi), gpios);
static const struct gpio_dt_spec key_mode_wake = GPIO_DT_SPEC_GET(DT_NODELABEL(key_mode), gpios);
static const struct device *const ext_nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));

static K_MUTEX_DEFINE(power_lock);
static struct lgps_power_sm_logic sm_logic;
static bool shutdown_pending;
static uint8_t last_soc_pct = LGPS_BATT_SOC_UNKNOWN;
static bool last_charging;
static bool last_gnss_fix;
static bool last_pdr_active;
static uint32_t last_low_warn_ms;
static bool low_warn_sent;
static struct k_work_delayable shutdown_work;
static lgps_power_poi_cb_t poi_callback;
static lgps_power_pairing_cb_t pairing_callback;

static bool wake_key_is_pressed(const struct gpio_dt_spec *spec)
{
	int val;

	if ((spec == NULL) || !gpio_is_ready_dt(spec)) {
		return false;
	}

	/*
	 * Read the physical pad level directly (same active-low convention as
	 * keys.c and board_io.c) to avoid the gpio_pin_get_dt() polarity trap.
	 */
	val = gpio_pin_get_raw(spec->port, spec->pin);
	if (val < 0) {
		LOG_ERR("wake key pin %u read failed (%d)", (unsigned int)spec->pin, val);
		return false;
	}

	return val == 0;
}

static void configure_wake_keys_for_poweroff(void)
{
	int ret;

	/*
	 * Configure both KEY1_POI (P1.06) and KEY2_MODE (P1.08), active-low with
	 * internal pull-up, as System OFF level-low wake sources.
	 */
	if (gpio_is_ready_dt(&key_poi_wake)) {
		ret = gpio_pin_interrupt_configure_dt(&key_poi_wake, GPIO_INT_LEVEL_LOW);
		if (ret < 0) {
			LOG_ERR("KEY1_POI wake IRQ configure failed (%d)", ret);
		}
	}

	if (gpio_is_ready_dt(&key_mode_wake)) {
		ret = gpio_pin_interrupt_configure_dt(&key_mode_wake, GPIO_INT_LEVEL_LOW);
		if (ret < 0) {
			LOG_ERR("KEY2_MODE wake IRQ configure failed (%d)", ret);
		}
	}
}

static void schedule_shutdown_locked(bool critical)
{
	enum lgps_led_pattern pattern;
	uint32_t delay_ms;

	if (shutdown_pending) {
		return;
	}

	shutdown_pending = true;
	lgps_power_logic_request_shutdown(&sm_logic);
	(void)lgps_led_off_all();
	if (critical) {
		pattern = LGPS_LED_PATTERN_CRITICAL_OFF;
		delay_ms = CRITICAL_SHUTDOWN_MS;
	} else {
		pattern = LGPS_LED_PATTERN_SHUTDOWN_CONFIRM;
		delay_ms = SHUTDOWN_SEQUENCE_MS;
	}
	(void)lgps_led_play(pattern);
	(void)k_work_schedule(&shutdown_work, K_MSEC(delay_ms));
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

	configure_wake_keys_for_poweroff();

	/* Let the last UART line drain before the rail collapses. */
	k_msleep(50);

	sys_poweroff();
}

void lgps_power_boot_confirm(void)
{
	uint32_t cause;
	bool poi_ready;
	bool mode_ready;

	/*
	 * Gate Q1=A 2.5 s System OFF wake-confirm behind CONFIG_LGPS_BOOT_HOLD_8S
	 * so conf/debug.conf continues to skip the hold during bench development.
	 */
	if (!IS_ENABLED(CONFIG_LGPS_BOOT_HOLD_8S)) {
		return;
	}

	/* Read cached reset cause (lgps_diag_banner() already cleared RESETREAS). */
	cause = lgps_diag_reset_cause_raw();

	/* Only gate wakes from System OFF; POR, J-Link debug, and OTA/software reboots boot immediately. */
	if ((cause & RESET_LOW_POWER_WAKE) == 0U) {
		return;
	}

	poi_ready = gpio_is_ready_dt(&key_poi_wake);
	mode_ready = gpio_is_ready_dt(&key_mode_wake);

	if (!poi_ready && !mode_ready) {
		return;
	}

	if (poi_ready) {
		int ret = gpio_pin_configure_dt(&key_poi_wake, GPIO_INPUT);

		if (ret < 0) {
			LOG_ERR("KEY1_POI wake input configure failed (%d)", ret);
			poi_ready = false;
		}
	}

	if (mode_ready) {
		int ret = gpio_pin_configure_dt(&key_mode_wake, GPIO_INPUT);

		if (ret < 0) {
			LOG_ERR("KEY2_MODE wake input configure failed (%d)", ret);
			mode_ready = false;
		}
	}

	if (!poi_ready && !mode_ready) {
		return;
	}

	for (uint32_t held_ms = 0U; held_ms < WAKE_CONFIRM_HOLD_MS; held_ms += 50U) {
		bool poi_pressed = poi_ready && wake_key_is_pressed(&key_poi_wake);
		bool mode_pressed = mode_ready && wake_key_is_pressed(&key_mode_wake);

		if (!poi_pressed && !mode_pressed) {
			LOG_INF("System OFF wake aborted (keys released at %u ms)", held_ms);
			configure_wake_keys_for_poweroff();
			k_msleep(20);
			sys_poweroff();
		}
		k_msleep(50);
	}

	LOG_INF("System OFF wake confirmed (key held %u ms)", WAKE_CONFIRM_HOLD_MS);
}

static void shutdown_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	enter_system_off();
}

static void on_key(enum lgps_key_id key, enum lgps_key_event_type event, uint32_t held_ms)
{
	lgps_power_poi_cb_t poi_cb = NULL;
	lgps_power_pairing_cb_t pairing_cb = NULL;

	k_mutex_lock(&power_lock, K_FOREVER);

	if (shutdown_pending) {
		k_mutex_unlock(&power_lock);
		return;
	}

	switch (event) {
	case LGPS_KEY_EVENT_DUAL_POWER_OFF:
		LOG_INF("dual-key held %u ms -> power off sequence", held_ms);
		schedule_shutdown_locked(false);
		break;

	case LGPS_KEY_EVENT_LONG_PRESS:
		LOG_INF("%s held %u ms -> power off sequence",
			key == LGPS_KEY_POI ? "KEY1_POI" : "KEY2_MODE", held_ms);
		schedule_shutdown_locked(false);
		break;

	case LGPS_KEY_EVENT_PAIRING_PRESS:
		if (key == LGPS_KEY_POI) {
			LOG_INF("BLE pairing hold detected (KEY1_POI, %u ms)", held_ms);
			(void)lgps_led_play(LGPS_LED_PATTERN_SYNC);
			pairing_cb = pairing_callback;
		}
		break;

	case LGPS_KEY_EVENT_PRESS:
		LOG_DBG("%s pressed", key == LGPS_KEY_POI ? "KEY1_POI" : "KEY2_MODE");
		break;

	case LGPS_KEY_EVENT_SHORT_PRESS:
		if (key == LGPS_KEY_POI) {
			LOG_INF("POI mark (KEY1_POI short press, held %u ms)", held_ms);
			(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
			poi_cb = poi_callback;
		} else if (key == LGPS_KEY_MODE) {
			enum lgps_led_pattern batt_pattern;

			if (last_soc_pct < 15U) {
				batt_pattern = LGPS_LED_PATTERN_LOW_BATTERY;
			} else if (last_soc_pct < 50U) {
				batt_pattern = LGPS_LED_PATTERN_BATT_MID;
			} else {
				batt_pattern = LGPS_LED_PATTERN_BATT_HIGH;
			}

			LOG_INF("status query (KEY2_MODE, soc=%u%%, fix=%d, pdr=%d)",
				last_soc_pct, last_gnss_fix ? 1 : 0, last_pdr_active ? 1 : 0);
			(void)lgps_led_play(batt_pattern);
			if (last_gnss_fix) {
				(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
			} else if (last_pdr_active) {
				(void)lgps_led_play(LGPS_LED_PATTERN_PDR);
			}
		}
		break;

	case LGPS_KEY_EVENT_MODE_HOLD:
		if (key == LGPS_KEY_MODE) {
			bool changed = lgps_power_logic_toggle_precision(
				&sm_logic, lgps_diag_uptime_ms(), last_soc_pct, last_charging);

			if (changed) {
				bool hp = (sm_logic.state == LGPS_PWR_HIGH_PRECISION);

				LOG_INF("high precision mode %s (KEY2_MODE, held %u ms)",
					hp ? "enabled" : "disabled", held_ms);
				(void)lgps_led_play(hp ? LGPS_LED_PATTERN_PRECISION
						       : LGPS_LED_PATTERN_MODE_NORMAL);
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

	if (poi_cb != NULL) {
		poi_cb();
	}
	if (pairing_cb != NULL) {
		pairing_cb();
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
		bool critical = !charging && (soc_pct <= LGPS_PWR_CRIT_BATT_OFF_SOC);

		if (critical) {
			LOG_WRN("critical battery (%u%%), entering System OFF", soc_pct);
		} else {
			LOG_INF("DEEP_SLEEP auto-off timeout reached, entering System OFF");
		}
		schedule_shutdown_locked(critical);
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
		schedule_shutdown_locked(false);
	}
	k_mutex_unlock(&power_lock);

	return ret;
}

void lgps_power_set_auto_off_min(uint16_t minutes)
{
	k_mutex_lock(&power_lock, K_FOREVER);
	lgps_power_logic_set_auto_off_min(&sm_logic, minutes);
	k_mutex_unlock(&power_lock);

	LOG_INF("DEEP_SLEEP auto-off set to %u min", (unsigned int)minutes);
}

void lgps_power_update_status(bool gnss_fix_valid, bool pdr_active, uint16_t storage_permille)
{
	uint32_t now_ms = lgps_diag_uptime_ms();
	enum lgps_power_state st;
	bool prev_fix;
	bool prev_pdr;
	bool low_batt;
	bool storage_full;

	k_mutex_lock(&power_lock, K_FOREVER);

	prev_fix = last_gnss_fix;
	prev_pdr = last_pdr_active;
	last_gnss_fix = gnss_fix_valid;
	last_pdr_active = pdr_active;
	st = sm_logic.state;

	if (!shutdown_pending && ((st == LGPS_PWR_ACTIVE) || (st == LGPS_PWR_HIGH_PRECISION))) {
		if (!prev_fix && gnss_fix_valid) {
			(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
		}

		if (!prev_pdr && pdr_active && !gnss_fix_valid && (st == LGPS_PWR_ACTIVE)) {
			(void)lgps_led_play(LGPS_LED_PATTERN_PDR);
		}

		low_batt = !last_charging && (last_soc_pct <= 100U) && (last_soc_pct < 10U);
		storage_full = (storage_permille >= 950U);

		if ((low_batt || storage_full) &&
		    (!low_warn_sent ||
		     ((uint32_t)(now_ms - last_low_warn_ms) >= LOW_WARN_INTERVAL_MS))) {
			low_warn_sent = true;
			last_low_warn_ms = now_ms;
			(void)lgps_led_play(LGPS_LED_PATTERN_LOW_BATTERY);
		}
	}

	k_mutex_unlock(&power_lock);
}

int lgps_power_inject_key(enum lgps_key_id key, enum lgps_key_event_type event)
{
	if (((unsigned int)key >= (unsigned int)LGPS_KEY_COUNT) ||
	    ((unsigned int)event > (unsigned int)LGPS_KEY_EVENT_DUAL_POWER_OFF)) {
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
