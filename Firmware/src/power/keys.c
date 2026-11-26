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

#define KEY_SETTLE_MS 5

struct lgps_key_state {
	enum lgps_key_id id;
	const struct gpio_dt_spec *spec;
	const char *name;
	struct gpio_callback irq_cb;
	struct k_work_delayable debounce_work;
	struct k_work_delayable hold_work;
	uint8_t stable_pressed;
	uint32_t press_start_ms;
	bool pairing_sent;
	bool long_sent;
	/* False until the key has been seen released once: a key that is low at boot
	 * cannot start the 8 s power-off countdown. */
	bool armed;
};

static const struct gpio_dt_spec key_poi = GPIO_DT_SPEC_GET(DT_NODELABEL(key_poi), gpios);
static const struct gpio_dt_spec key_mode = GPIO_DT_SPEC_GET(DT_NODELABEL(key_mode), gpios);

BUILD_ASSERT((DT_GPIO_FLAGS(DT_NODELABEL(key_poi), gpios) & GPIO_PULL_UP) != 0,
	     "KEY1_POI (P1.06) must carry GPIO_PULL_UP in devicetree: no external pull-up");
BUILD_ASSERT((DT_GPIO_FLAGS(DT_NODELABEL(key_mode), gpios) & GPIO_PULL_UP) != 0,
	     "KEY2_MODE (P1.08) must carry GPIO_PULL_UP in devicetree: no external pull-up");

static struct lgps_key_state keys[LGPS_KEY_COUNT] = {
	[LGPS_KEY_POI] = { .id = LGPS_KEY_POI, .spec = &key_poi, .name = "KEY1_POI" },
	[LGPS_KEY_MODE] = { .id = LGPS_KEY_MODE, .spec = &key_mode, .name = "KEY2_MODE" },
};

static lgps_key_callback_t key_callback;
static atomic_t debounced_levels = ATOMIC_INIT(0);

static int read_level(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_raw(spec->port, spec->pin);
}

static void update_level_mask(enum lgps_key_id id, uint8_t pressed)
{
	if (pressed != 0U) {
		(void)atomic_or(&debounced_levels, (atomic_val_t)BIT(id));
	} else {
		(void)atomic_and(&debounced_levels, (atomic_val_t)~BIT(id));
	}
}

static void notify(enum lgps_key_id id, enum lgps_key_event_type event, uint32_t held_ms)
{
	if (key_callback != NULL) {
		key_callback(id, event, held_ms);
	}
}

static void hold_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lgps_key_state *key = CONTAINER_OF(dwork, struct lgps_key_state, hold_work);
	int level;
	uint32_t now_ms;
	uint32_t held_ms;

	if (!key->armed || (key->stable_pressed == 0U)) {
		return;
	}

	level = read_level(key->spec);
	if (level != 0) {
		/* Key already released; let debounce handler finish release transition. */
		(void)k_work_reschedule(&key->debounce_work, K_MSEC(LGPS_KEY_DEBOUNCE_MS));
		return;
	}

	now_ms = k_uptime_get_32();
	held_ms = now_ms - key->press_start_ms;

	if (!key->pairing_sent && (held_ms >= LGPS_KEY_PAIRING_PRESS_MS)) {
		key->pairing_sent = true;
		LOG_INF("%s pairing hold (%u ms)", key->name, held_ms);
		notify(key->id, LGPS_KEY_EVENT_PAIRING_PRESS, held_ms);
	}

	if (!key->long_sent && (held_ms >= LGPS_KEY_LONG_PRESS_MS)) {
		key->long_sent = true;
		LOG_INF("%s long press (%u ms)", key->name, held_ms);
		notify(key->id, LGPS_KEY_EVENT_LONG_PRESS, held_ms);
		return;
	}

	if (!key->long_sent) {
		uint32_t next_target =
			key->pairing_sent ? LGPS_KEY_LONG_PRESS_MS : LGPS_KEY_PAIRING_PRESS_MS;
		uint32_t remain = (next_target > held_ms) ? (next_target - held_ms) : 10U;

		(void)k_work_reschedule(&key->hold_work, K_MSEC(remain));
	}
}

static void debounce_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lgps_key_state *key = CONTAINER_OF(dwork, struct lgps_key_state, debounce_work);
	int value = read_level(key->spec);
	uint32_t now_ms;
	uint8_t pressed;

	if (value < 0) {
		LOG_ERR("%s: read failed (%d)", key->name, value);
		return;
	}

	pressed = (value == 0) ? 1U : 0U;
	if (pressed == key->stable_pressed) {
		return;
	}

	now_ms = k_uptime_get_32();
	key->stable_pressed = pressed;
	update_level_mask(key->id, pressed);

	if (pressed != 0U) {
		key->press_start_ms = now_ms;
		key->pairing_sent = false;
		key->long_sent = false;

		if (!key->armed) {
			return;
		}

		LOG_INF("%s pressed", key->name);
		notify(key->id, LGPS_KEY_EVENT_PRESS, 0U);
		(void)k_work_reschedule(&key->hold_work, K_MSEC(LGPS_KEY_PAIRING_PRESS_MS));
		return;
	}

	(void)k_work_cancel_delayable(&key->hold_work);

	if (!key->armed) {
		key->armed = true;
		LOG_INF("%s released after boot, key armed", key->name);
		return;
	}

	uint32_t held_ms = now_ms - key->press_start_ms;

	if (!key->long_sent && !key->pairing_sent && (held_ms < LGPS_KEY_SHORT_PRESS_MS)) {
		notify(key->id, LGPS_KEY_EVENT_SHORT_PRESS, held_ms);
	}

	LOG_DBG("%s released after %u ms", key->name, held_ms);
	notify(key->id, LGPS_KEY_EVENT_RELEASE, held_ms);
}

static void key_gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct lgps_key_state *key = CONTAINER_OF(cb, struct lgps_key_state, irq_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&key->debounce_work, K_MSEC(LGPS_KEY_DEBOUNCE_MS));
}

int lgps_keys_init(lgps_key_callback_t callback)
{
	key_callback = callback;

	for (uint8_t id = 0U; id < LGPS_KEY_COUNT; id++) {
		struct lgps_key_state *key = &keys[id];
		int level;
		int ret;

		if (!gpio_is_ready_dt(key->spec)) {
			LOG_ERR("%s: GPIO not ready", key->name);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(key->spec, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("%s: configure failed (%d)", key->name, ret);
			return ret;
		}

		k_msleep(KEY_SETTLE_MS);

		level = read_level(key->spec);
		if (level < 0) {
			LOG_ERR("%s: initial read failed (%d)", key->name, level);
			return level;
		}

		key->stable_pressed = (level == 0) ? 1U : 0U;
		key->press_start_ms = k_uptime_get_32();
		key->pairing_sent = false;
		key->long_sent = false;
		key->armed = (key->stable_pressed == 0U);
		update_level_mask(key->id, key->stable_pressed);

		if (!key->armed) {
			LOG_WRN("%s reads low at boot: disarmed until released once", key->name);
		}

		k_work_init_delayable(&key->debounce_work, debounce_work_handler);
		k_work_init_delayable(&key->hold_work, hold_work_handler);

		gpio_init_callback(&key->irq_cb, key_gpio_isr, BIT(key->spec->pin));
		ret = gpio_add_callback(key->spec->port, &key->irq_cb);
		if (ret < 0) {
			LOG_ERR("%s: gpio_add_callback failed (%d)", key->name, ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(key->spec, GPIO_INT_EDGE_BOTH);
		if (ret < 0) {
			LOG_ERR("%s: interrupt configure failed (%d)", key->name, ret);
			return ret;
		}
	}

	LOG_INF("keys ready (IRQ): KEY1_POI=P1.06 KEY2_MODE=P1.08 (pull-up, %u ms debounce, "
		"%u ms pairing, %u ms power-off)",
		LGPS_KEY_DEBOUNCE_MS, LGPS_KEY_PAIRING_PRESS_MS, LGPS_KEY_LONG_PRESS_MS);

	return 0;
}

uint8_t lgps_keys_levels(void)
{
	return (uint8_t)atomic_get(&debounced_levels);
}
