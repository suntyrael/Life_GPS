/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <lifegps/keys.h>

LOG_MODULE_REGISTER(lgps_keys, LOG_LEVEL_INF);

#define KEY_POLL_PERIOD_MS 10
#define KEY_THREAD_STACK 1024
#define KEY_THREAD_PRIORITY 8

struct lgps_key_state {
	const struct gpio_dt_spec *spec;
	const char *name;
	uint8_t stable_pressed;
	uint8_t candidate_pressed;
	uint8_t candidate_samples;
	uint32_t press_start_ms;
	bool long_sent;
};

static const struct gpio_dt_spec key_poi = GPIO_DT_SPEC_GET(DT_NODELABEL(key_poi), gpios);
static const struct gpio_dt_spec key_mode = GPIO_DT_SPEC_GET(DT_NODELABEL(key_mode), gpios);

static struct lgps_key_state keys[LGPS_KEY_COUNT] = {
	[LGPS_KEY_POI] = { .spec = &key_poi, .name = "KEY1_POI" },
	[LGPS_KEY_MODE] = { .spec = &key_mode, .name = "KEY2_MODE" },
};

static lgps_key_callback_t key_callback;

static void notify(enum lgps_key_id id, enum lgps_key_event_type event, uint32_t held_ms)
{
	if (key_callback != NULL) {
		key_callback(id, event, held_ms);
	}
}

static void update_key(enum lgps_key_id id, uint32_t now_ms)
{
	struct lgps_key_state *key = &keys[id];
	int value = gpio_pin_get_dt(key->spec);
	uint8_t pressed;

	if (value < 0) {
		LOG_ERR("%s: read failed (%d)", key->name, value);
		return;
	}

	/* Active low: pressed == 0. */
	pressed = (value == 0) ? 1U : 0U;

	if (pressed != key->candidate_pressed) {
		key->candidate_pressed = pressed;
		key->candidate_samples = 0U;
		return;
	}

	if (key->candidate_samples < 255U) {
		key->candidate_samples++;
	}

	if (key->candidate_samples != (LGPS_KEY_DEBOUNCE_MS / KEY_POLL_PERIOD_MS)) {
		return;
	}

	if (pressed == key->stable_pressed) {
		return;
	}

	key->stable_pressed = pressed;

	if (pressed != 0U) {
		key->press_start_ms = now_ms;
		key->long_sent = false;
		LOG_INF("%s pressed", key->name);
		notify(id, LGPS_KEY_EVENT_PRESS, 0U);
		return;
	}

	uint32_t held_ms = now_ms - key->press_start_ms;

	if (!key->long_sent && held_ms < LGPS_KEY_SHORT_PRESS_MS) {
		notify(id, LGPS_KEY_EVENT_SHORT_PRESS, held_ms);
	}

	LOG_INF("%s released after %u ms", key->name, held_ms);
	notify(id, LGPS_KEY_EVENT_RELEASE, held_ms);
}

static void keys_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		uint32_t now_ms = k_uptime_get_32();

		for (uint8_t id = 0U; id < LGPS_KEY_COUNT; id++) {
			update_key((enum lgps_key_id)id, now_ms);

			struct lgps_key_state *key = &keys[id];

			if (key->stable_pressed != 0U && !key->long_sent) {
				uint32_t held_ms = now_ms - key->press_start_ms;

				if (held_ms >= LGPS_KEY_LONG_PRESS_MS) {
					key->long_sent = true;
					LOG_INF("%s long press (%u ms)", key->name, held_ms);
					notify((enum lgps_key_id)id, LGPS_KEY_EVENT_LONG_PRESS,
					       held_ms);
				}
			}
		}

		k_msleep(KEY_POLL_PERIOD_MS);
	}
}

K_THREAD_DEFINE(lgps_keys_thread, KEY_THREAD_STACK, keys_thread, NULL, NULL, NULL,
		KEY_THREAD_PRIORITY, 0, 0);

int lgps_keys_init(lgps_key_callback_t callback)
{
	key_callback = callback;

	for (uint8_t id = 0U; id < LGPS_KEY_COUNT; id++) {
		struct lgps_key_state *key = &keys[id];
		int ret;

		if (!gpio_is_ready_dt(key->spec)) {
			LOG_ERR("%s: GPIO not ready", key->name);
			return -ENODEV;
		}

		/* Internal pull-up, active low; no RC network on the board. */
		ret = gpio_pin_configure_dt(key->spec, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("%s: configure failed (%d)", key->name, ret);
			return ret;
		}

		key->stable_pressed = (gpio_pin_get_dt(key->spec) == 0) ? 1U : 0U;
		key->candidate_pressed = key->stable_pressed;
		key->press_start_ms = k_uptime_get_32();
	}

	LOG_INF("keys ready: KEY1_POI=P1.06 KEY2_MODE=P1.08 (pull-up, %u ms debounce, "
		"%u ms long press)",
		LGPS_KEY_DEBOUNCE_MS, LGPS_KEY_LONG_PRESS_MS);

	return 0;
}
