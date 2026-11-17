/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <lifegps/led.h>

LOG_MODULE_REGISTER(lgps_led, LOG_LEVEL_INF);

#define LED_STACK_SIZE 1024
#define LED_PRIORITY 7

static const struct gpio_dt_spec led_specs[LGPS_LED_COLOR_COUNT] = {
	[LGPS_LED_RED] = GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	[LGPS_LED_GREEN] = GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	[LGPS_LED_BLUE] = GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue), gpios),
};

struct lgps_led_step {
	uint8_t colors;      /* bitmask of lgps_led_color */
	uint16_t on_ms;
	uint16_t off_ms;
	uint8_t repeats;
};

struct lgps_led_sequence {
	const struct lgps_led_step *steps;
	uint8_t step_count;
};

#define LED_STEPS(...) { .steps = (const struct lgps_led_step[]){ __VA_ARGS__ }, \
	.step_count = sizeof((const struct lgps_led_step[]){ __VA_ARGS__ }) / \
		      sizeof(struct lgps_led_step) }

/* README section 9.1: slow blink is 1 Hz, i.e. 500 ms on / 500 ms off. */
#define SLOW_BLINK_ON_MS 500U
#define SLOW_BLINK_OFF_MS 500U
#define FAST_BLINK_ON_MS 50U

static const struct lgps_led_sequence sequences[LGPS_LED_PATTERN_COUNT] = {
	[LGPS_LED_PATTERN_BOOT_CONFIRM] = LED_STEPS(
		{ BIT(LGPS_LED_RED), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 },
		{ BIT(LGPS_LED_BLUE), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 },
		{ BIT(LGPS_LED_GREEN), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 }),
	[LGPS_LED_PATTERN_SHUTDOWN_CONFIRM] = LED_STEPS(
		{ BIT(LGPS_LED_GREEN), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 },
		{ BIT(LGPS_LED_BLUE), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 },
		{ BIT(LGPS_LED_RED), SLOW_BLINK_ON_MS, SLOW_BLINK_OFF_MS, 3 }),
	[LGPS_LED_PATTERN_FIX] = LED_STEPS(
		{ BIT(LGPS_LED_GREEN), FAST_BLINK_ON_MS, FAST_BLINK_ON_MS, 2 }),
	[LGPS_LED_PATTERN_BLE_READY] = LED_STEPS(
		{ BIT(LGPS_LED_BLUE), FAST_BLINK_ON_MS, 0U, 1 }),
	[LGPS_LED_PATTERN_SYNC] = LED_STEPS(
		{ BIT(LGPS_LED_BLUE), FAST_BLINK_ON_MS, FAST_BLINK_ON_MS, 16 }),
	[LGPS_LED_PATTERN_PRECISION] = LED_STEPS(
		{ BIT(LGPS_LED_BLUE), 250U, 0U, 1 },
		{ BIT(LGPS_LED_GREEN), 250U, 0U, 1 },
		{ BIT(LGPS_LED_BLUE), 250U, 0U, 1 },
		{ BIT(LGPS_LED_GREEN), 250U, 0U, 1 },
		{ BIT(LGPS_LED_BLUE), 250U, 0U, 1 },
		{ BIT(LGPS_LED_GREEN), 250U, 0U, 1 }),
	[LGPS_LED_PATTERN_PDR] = LED_STEPS(
		{ BIT(LGPS_LED_RED) | BIT(LGPS_LED_GREEN), FAST_BLINK_ON_MS, 0U, 1 }),
	[LGPS_LED_PATTERN_LOW_BATTERY] = LED_STEPS(
		{ BIT(LGPS_LED_RED), 100U, 100U, 10 }),
};

K_MSGQ_DEFINE(led_pattern_queue, sizeof(enum lgps_led_pattern), 4, 4);

int lgps_led_set(enum lgps_led_color color, bool on)
{
	int ret;

	if (color >= LGPS_LED_COLOR_COUNT) {
		return -EINVAL;
	}

	ret = gpio_pin_set_dt(&led_specs[color], on ? 1 : 0);
	if (ret < 0) {
		LOG_ERR("LED %d write failed (%d)", color, ret);
		return ret;
	}

	return 0;
}

int lgps_led_off_all(void)
{
	int ret = 0;

	for (uint8_t color = 0U; color < LGPS_LED_COLOR_COUNT; color++) {
		int err = lgps_led_set((enum lgps_led_color)color, false);

		if (err < 0) {
			ret = err;
		}
	}

	return ret;
}

static void apply_colors(uint8_t colors)
{
	(void)lgps_led_set(LGPS_LED_RED, (colors & BIT(LGPS_LED_RED)) != 0U);
	(void)lgps_led_set(LGPS_LED_GREEN, (colors & BIT(LGPS_LED_GREEN)) != 0U);
	(void)lgps_led_set(LGPS_LED_BLUE, (colors & BIT(LGPS_LED_BLUE)) != 0U);
}

static void run_sequence(enum lgps_led_pattern pattern)
{
	const struct lgps_led_sequence *seq = &sequences[pattern];

	for (uint8_t i = 0U; i < seq->step_count; i++) {
		const struct lgps_led_step *step = &seq->steps[i];

		for (uint8_t repeat = 0U; repeat < step->repeats; repeat++) {
			apply_colors(step->colors);
			k_msleep(step->on_ms);
			apply_colors(0U);

			if (step->off_ms > 0U) {
				k_msleep(step->off_ms);
			}
		}
	}
}

static void led_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (;;) {
		enum lgps_led_pattern pattern;

		if (k_msgq_get(&led_pattern_queue, &pattern, K_FOREVER) != 0) {
			continue;
		}

		run_sequence(pattern);
	}
}

K_THREAD_DEFINE(lgps_led_thread, LED_STACK_SIZE, led_thread, NULL, NULL, NULL, LED_PRIORITY, 0, 0);

int lgps_led_play(enum lgps_led_pattern pattern)
{
	enum lgps_led_pattern dropped;

	if ((unsigned int)pattern >= LGPS_LED_PATTERN_COUNT) {
		return -EINVAL;
	}

	/* Newest request wins: drop the oldest queued pattern on overflow. */
	while (k_msgq_put(&led_pattern_queue, &pattern, K_NO_WAIT) != 0) {
		if (k_msgq_get(&led_pattern_queue, &dropped, K_NO_WAIT) != 0) {
			return -ENOMSG;
		}

		LOG_DBG("pattern %d dropped in favour of %d", dropped, pattern);
	}

	return 0;
}

int lgps_led_init(void)
{
	for (uint8_t color = 0U; color < LGPS_LED_COLOR_COUNT; color++) {
		int ret;

		if (!gpio_is_ready_dt(&led_specs[color])) {
			LOG_ERR("LED %d GPIO not ready", color);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&led_specs[color], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("LED %d configure failed (%d)", color, ret);
			return ret;
		}
	}

	LOG_INF("LED ready: RED=P1.10 GREEN=P2.07 BLUE=P1.12, active high");

	return 0;
}
