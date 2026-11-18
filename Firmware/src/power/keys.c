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
#define KEY_SETTLE_MS 5
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
	/* False until the key has been seen released once: a key that is low at boot
	 * cannot start the 8 s power-off countdown. */
	bool armed;
};

static const struct gpio_dt_spec key_poi = GPIO_DT_SPEC_GET(DT_NODELABEL(key_poi), gpios);
static const struct gpio_dt_spec key_mode = GPIO_DT_SPEC_GET(DT_NODELABEL(key_mode), gpios);

/*
 * The board has no external pull-up on either key, so the internal one is not
 * optional: without it an open button leaves the pin floating. Enforce the
 * devicetree flag at build time instead of trusting a comment.
 * gpio_pin_configure_dt() ORs spec->dt_flags into the configuration, so a flag
 * that is present here is a flag that reaches the driver.
 */
BUILD_ASSERT((DT_GPIO_FLAGS(DT_NODELABEL(key_poi), gpios) & GPIO_PULL_UP) != 0,
	     "KEY1_POI (P1.06) must carry GPIO_PULL_UP in devicetree: no external pull-up");
BUILD_ASSERT((DT_GPIO_FLAGS(DT_NODELABEL(key_mode), gpios) & GPIO_PULL_UP) != 0,
	     "KEY2_MODE (P1.08) must carry GPIO_PULL_UP in devicetree: no external pull-up");

static struct lgps_key_state keys[LGPS_KEY_COUNT] = {
	[LGPS_KEY_POI] = { .spec = &key_poi, .name = "KEY1_POI" },
	[LGPS_KEY_MODE] = { .spec = &key_mode, .name = "KEY2_MODE" },
};

static lgps_key_callback_t key_callback;

/*
 * Read the PHYSICAL pad level.
 *
 * The keys are declared GPIO_ACTIVE_LOW, and gpio_pin_get()/gpio_pin_get_dt()
 * return the *logical* level: the driver inverts it for pins marked ACTIVE_LOW.
 * Using those here inverted the state machine - "released" looked pressed and the
 * first real press looked like a release - which is exactly the behaviour seen on
 * the bench. gpio_pin_get_raw() ignores GPIO_ACTIVE_LOW, so 0 always means "the
 * button is pulling the pad to ground".
 */
static int read_level(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_raw(spec->port, spec->pin);
}

/* The thread is created by K_THREAD_DEFINE, which starts it during kernel init,
 * i.e. before lgps_keys_init() runs. It must not sample unconfigured pins. */
static K_SEM_DEFINE(keys_ready_sem, 0, 1);

/*
 * Bench diagnostic for a pin that reads low at boot. A low reading has three very
 * different causes and they must not be confused:
 *   (a) the button really is held / the net is shorted to ground,
 *   (b) the devicetree pull flags never reached the driver, so the pin floats,
 *   (c) the driver ignores the pull configuration.
 * Reading the pin after each explicit configuration separates them, and the
 * devicetree flag value is printed so a lost GPIO_PULL_UP is visible.
 */
static int read_pin(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_raw(spec->port, spec->pin);
}

static void diagnose_key_pin(const struct gpio_dt_spec *spec, const char *name)
{
	gpio_flags_t effective = 0U;
	uint32_t port_in = 0U;
	int as_dt;
	int explicit_up;
	int explicit_down;
	int ret;

	/* Configuration exactly as the devicetree asks (pull-up | active low). */
	ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("%s: configure from devicetree failed (%d)", name, ret);
		return;
	}
	k_msleep(2);
	as_dt = read_pin(spec);

	/* Ignore devicetree flags: request the pull directly. */
	ret = gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("%s: explicit pull-up failed (%d)", name, ret);
		return;
	}
	k_msleep(2);
	explicit_up = read_pin(spec);

	ret = gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT | GPIO_PULL_DOWN);
	if (ret < 0) {
		LOG_ERR("%s: explicit pull-down failed (%d)", name, ret);
		return;
	}
	k_msleep(2);
	explicit_down = read_pin(spec);

	/* Restore the board configuration. */
	ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("%s: restore failed (%d)", name, ret);
	}

	/*
	 * What the driver thinks the pin is: a missing GPIO_INPUT or GPIO_PULL_UP here
	 * means the configuration never reached the hardware, which is a completely
	 * different fault from an externally driven pin.
	 */
	(void)gpio_pin_get_config(spec->port, spec->pin, &effective);
	(void)gpio_port_get_raw(spec->port, &port_in);

	LOG_WRN("%s self-test: dt_flags=0x%02x effective=0x%02x (input=%d pull_up=%d pull_down=%d)",
		name, spec->dt_flags, effective,
		((effective & GPIO_INPUT) != 0U) ? 1 : 0,
		((effective & GPIO_PULL_UP) != 0U) ? 1 : 0,
		((effective & GPIO_PULL_DOWN) != 0U) ? 1 : 0);
	LOG_WRN("%s self-test: port %s IN=0x%08x -> bit%u=%u (dt=%d explicit_up=%d "
		"explicit_down=%d)", name, spec->port->name, port_in, spec->pin,
		(unsigned int)((port_in >> spec->pin) & 0x01U), as_dt, explicit_up, explicit_down);

	if ((effective & GPIO_PULL_UP) == 0U) {
		LOG_ERR("%s: the driver reports no pull-up on this pin - configuration did not "
			"reach the hardware", name);
	} else if ((explicit_up == 0) && (explicit_down == 0)) {
		LOG_WRN("%s: both pulls read 0 -> the pin is externally driven low "
			"(held button, net shorted to GND, or the wrong pin)", name);
	} else if ((explicit_up == 0) && (explicit_down == 1)) {
		LOG_ERR("%s: the pull configuration has no effect on the pin (driver / pin mux)",
			name);
	} else if ((explicit_up == 1) && (explicit_down == 1)) {
		LOG_WRN("%s: both pulls read 1 -> the pin is externally driven high", name);
	} else if (as_dt == 0) {
		/*
		 * up=1/down=0 proves the pull-up works and the button is open, so the low
		 * reading taken right after the first configuration is a settling artefact,
		 * not a wiring fault. Initialisation now uses the settled value.
		 */
		LOG_WRN("%s: pull-up works and the key is released, but the first sample after "
			"configuration read low (pad settling); using the settled sample", name);
	} else {
		LOG_INF("%s: pull-up works, key is released", name);
	}
}

static void notify(enum lgps_key_id id, enum lgps_key_event_type event, uint32_t held_ms)
{
	if (key_callback != NULL) {
		key_callback(id, event, held_ms);
	}
}

static void update_key(enum lgps_key_id id, uint32_t now_ms)
{
	struct lgps_key_state *key = &keys[id];
	int value = read_level(key->spec);
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

		if (!key->armed) {
			/* Level was already low at boot: wait for a real release first. */
			return;
		}

		/* Immediate feedback: the user must see the press before releasing. */
		LOG_INF("%s pressed", key->name);
		notify(id, LGPS_KEY_EVENT_PRESS, 0U);
		return;
	}

	if (!key->armed) {
		key->armed = true;
		LOG_INF("%s released after boot, key armed", key->name);
		return;
	}

	uint32_t held_ms = now_ms - key->press_start_ms;

	if (!key->long_sent && held_ms < LGPS_KEY_SHORT_PRESS_MS) {
		notify(id, LGPS_KEY_EVENT_SHORT_PRESS, held_ms);
	}

	LOG_DBG("%s released after %u ms", key->name, held_ms);
	notify(id, LGPS_KEY_EVENT_RELEASE, held_ms);
}

static void keys_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (k_sem_take(&keys_ready_sem, K_FOREVER) != 0) {
		return;
	}

	for (;;) {
		uint32_t now_ms = k_uptime_get_32();

		for (uint8_t id = 0U; id < LGPS_KEY_COUNT; id++) {
			update_key((enum lgps_key_id)id, now_ms);

			struct lgps_key_state *key = &keys[id];

			if (key->armed && key->stable_pressed != 0U && !key->long_sent) {
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

		/*
		 * Let the pad settle before the first sample: right after the pull-up is
		 * enabled the very first read can still return the old level, which would
		 * latch a "pressed" state that no event can clear.
		 */
		k_msleep(KEY_SETTLE_MS);

		int level = read_level(key->spec);

		if (level < 0) {
			LOG_ERR("%s: initial read failed (%d)", key->name, level);
			return level;
		}

		if (level == 0) {
			LOG_WRN("%s reads low after settling: long press disarmed until it is "
				"released once", key->name);
			diagnose_key_pin(key->spec, key->name);

			/* Use the settled value the diagnostic left behind. */
			level = read_level(key->spec);
			if (level < 0) {
				LOG_ERR("%s: read after self-test failed (%d)", key->name, level);
				return level;
			}
		}

		key->stable_pressed = (level == 0) ? 1U : 0U;
		key->candidate_pressed = key->stable_pressed;
		key->press_start_ms = k_uptime_get_32();
		key->armed = (key->stable_pressed == 0U);
	}

	LOG_INF("keys ready: KEY1_POI=P1.06 KEY2_MODE=P1.08 (pull-up, %u ms debounce, "
		"%u ms long press, arm-on-release)", LGPS_KEY_DEBOUNCE_MS, LGPS_KEY_LONG_PRESS_MS);

	/* Sampling may start only now that both pins are configured and settled. */
	k_sem_give(&keys_ready_sem);

	return 0;
}

uint8_t lgps_keys_levels(void)
{
	uint8_t levels = 0U;

	for (uint8_t id = 0U; id < LGPS_KEY_COUNT; id++) {
		if (keys[id].stable_pressed != 0U) {
			levels |= (uint8_t)BIT(id);
		}
	}

	return levels;
}
