/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <lifegps/led.h>

LOG_MODULE_REGISTER(lgps_led, LOG_LEVEL_INF);

#define LED_STACK_SIZE 1024
/*
 * Cooperative priority keeps GPIO edge timing independent of main (priority 0)
 * and GNSS (priority 6): each wake-up only writes three GPIO output registers
 * and blocks again until the next edge.
 */
#define LED_PRIORITY K_PRIO_COOP(7)

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

/* Q1=A compact boot/shutdown confirmation: 300 ms on / 250 ms off (~1.65 s total). */
#define CONFIRM_BLINK_ON_MS 300U
#define CONFIRM_BLINK_OFF_MS 250U
#define FAST_BLINK_ON_MS 50U
#define BATT_QUERY_PULSE_MS 150U

static const struct lgps_led_sequence sequences[LGPS_LED_PATTERN_COUNT] = {
	[LGPS_LED_PATTERN_BOOT_CONFIRM] = LED_STEPS(
		{ BIT(LGPS_LED_RED), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 },
		{ BIT(LGPS_LED_BLUE), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 },
		{ BIT(LGPS_LED_GREEN), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 }),
	[LGPS_LED_PATTERN_SHUTDOWN_CONFIRM] = LED_STEPS(
		{ BIT(LGPS_LED_GREEN), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 },
		{ BIT(LGPS_LED_BLUE), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 },
		{ BIT(LGPS_LED_RED), CONFIRM_BLINK_ON_MS, CONFIRM_BLINK_OFF_MS, 1 }),
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
	[LGPS_LED_PATTERN_CRITICAL_OFF] = LED_STEPS(
		{ BIT(LGPS_LED_RED), 100U, 100U, 5 }),
	[LGPS_LED_PATTERN_MODE_NORMAL] = LED_STEPS(
		{ BIT(LGPS_LED_GREEN), 250U, 0U, 1 }),
	[LGPS_LED_PATTERN_BATT_HIGH] = LED_STEPS(
		{ BIT(LGPS_LED_GREEN), BATT_QUERY_PULSE_MS, BATT_QUERY_PULSE_MS, 2 }),
	[LGPS_LED_PATTERN_BATT_MID] = LED_STEPS(
		{ BIT(LGPS_LED_RED) | BIT(LGPS_LED_GREEN), BATT_QUERY_PULSE_MS,
		  BATT_QUERY_PULSE_MS, 2 }),
};

K_MSGQ_DEFINE(led_pattern_queue, sizeof(enum lgps_led_pattern), 4, 4);

/*
 * Cancellation token for the sequence that is currently being played.
 * lgps_led_off_all() increments it; the pattern thread compares its own snapshot
 * and aborts as soon as it differs. Without this the thread could only be stopped
 * at the end of a boot/shutdown confirmation, and the power state machine's
 * fixed wait would expire before the shutdown pattern even started.
 */
static atomic_t led_generation;
static atomic_t led_enabled = ATOMIC_INIT(1);

/*
 * Semaphore pair:
 *   led_ready_sem  holds the thread until lgps_led_init() has configured all pins.
 *   led_cancel_sem wakes a running step immediately on lgps_led_off_all() instead
 *                  of slicing each blink into 50 ms relative sleeps.
 */
static K_SEM_DEFINE(led_ready_sem, 0, 1);
static K_SEM_DEFINE(led_cancel_sem, 0, 1);

int lgps_led_set(enum lgps_led_color color, bool on)
{
	int ret;

	/*
	 * Cast before comparing: an enum whose underlying type is int passes a plain
	 * "color >= LGPS_LED_COLOR_COUNT" check with a negative value and indexes the
	 * array out of bounds.
	 */
	if ((unsigned int)color >= (unsigned int)LGPS_LED_COLOR_COUNT) {
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

	/* Cancel the running sequence, then drop everything that is still queued: the
	 * header promises "force all LEDs off and drop any queued pattern".
	 */
	(void)atomic_inc(&led_generation);
	k_msgq_purge(&led_pattern_queue);
	k_sem_give(&led_cancel_sem);

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

static bool cancelled(atomic_val_t generation)
{
	return atomic_get(&led_generation) != generation;
}

/*
 * Block until @p deadline_ms (k_uptime_get_32() domain) or until lgps_led_off_all()
 * increments led_generation and signals led_cancel_sem. Signed subtraction keeps
 * the comparison valid across the 49-day 32-bit wrap.
 */
static bool wait_until_or_cancelled(uint32_t deadline_ms, atomic_val_t generation)
{
	for (;;) {
		int32_t remaining_ms;

		if (cancelled(generation)) {
			return false;
		}

		remaining_ms = (int32_t)(deadline_ms - k_uptime_get_32());
		if (remaining_ms <= 0) {
			return !cancelled(generation);
		}

		(void)k_sem_take(&led_cancel_sem, K_MSEC(remaining_ms));
	}
}

static void run_sequence(enum lgps_led_pattern pattern, atomic_val_t generation)
{
	const struct lgps_led_sequence *seq = &sequences[pattern];
	uint32_t deadline_ms = k_uptime_get_32();

	for (uint8_t i = 0U; i < seq->step_count; i++) {
		const struct lgps_led_step *step = &seq->steps[i];

		for (uint8_t repeat = 0U; repeat < step->repeats; repeat++) {
			if (cancelled(generation)) {
				goto done;
			}

			apply_colors(step->colors);
			deadline_ms += step->on_ms;

			if ((step->on_ms > 0U) &&
			    !wait_until_or_cancelled(deadline_ms, generation)) {
				goto done;
			}

			apply_colors(0U);

			if (step->off_ms > 0U) {
				deadline_ms += step->off_ms;

				if (!wait_until_or_cancelled(deadline_ms, generation)) {
					goto done;
				}
			}
		}
	}

done:
	/* Always leave the LEDs dark: a cancellation must not freeze a colour on. */
	apply_colors(0U);
}

static void led_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (k_sem_take(&led_ready_sem, K_FOREVER) != 0) {
		return;
	}

	for (;;) {
		enum lgps_led_pattern pattern;
		atomic_val_t generation;

		if (k_msgq_get(&led_pattern_queue, &pattern, K_FOREVER) != 0) {
			continue;
		}

		(void)k_sem_take(&led_cancel_sem, K_NO_WAIT);

		/* Snapshot before playing: an off_all() that lands later must win. */
		generation = atomic_get(&led_generation);
		run_sequence(pattern, generation);
	}
}

K_THREAD_DEFINE(lgps_led_thread, LED_STACK_SIZE, led_thread, NULL, NULL, NULL, LED_PRIORITY, 0, 0);

static bool led_pattern_always_allowed(enum lgps_led_pattern pattern)
{
	switch (pattern) {
	case LGPS_LED_PATTERN_BOOT_CONFIRM:
	case LGPS_LED_PATTERN_SHUTDOWN_CONFIRM:
	case LGPS_LED_PATTERN_CRITICAL_OFF:
	case LGPS_LED_PATTERN_LOW_BATTERY:
		return true;
	default:
		return false;
	}
}

void lgps_led_set_enabled(bool enabled)
{
	(void)atomic_set(&led_enabled, enabled ? 1 : 0);
	LOG_INF("routine status LED %s", enabled ? "enabled" : "disabled");
}

bool lgps_led_is_enabled(void)
{
	return atomic_get(&led_enabled) != 0;
}

int lgps_led_play(enum lgps_led_pattern pattern)
{
	enum lgps_led_pattern dropped;

	if ((unsigned int)pattern >= (unsigned int)LGPS_LED_PATTERN_COUNT) {
		return -EINVAL;
	}

	if (!lgps_led_is_enabled() && !led_pattern_always_allowed(pattern)) {
		return 0;
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

	k_sem_give(&led_ready_sem);

	return 0;
}
