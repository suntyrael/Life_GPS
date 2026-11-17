/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - three-colour status LED.
 *
 * The LED patterns are the ones defined in README section 9.1 ("三色超低功耗
 * RGB LED 状态灯语"): 1 Hz slow blink = 500 ms on / 500 ms off. Each request is
 * queued, so a pattern is never truncated silently; the newest request wins when
 * the queue is full because a status light must show the current state.
 *
 * LED nets: LED_RED = P1.10, LED_GREEN = P2.07, LED_BLUE = P1.12, all driven by
 * DTC143ZETL pre-biased NPNs -> high level = LED on (pinmap section 2).
 */

#ifndef LIFEGPS_LED_H_
#define LIFEGPS_LED_H_

#include <stdbool.h>
#include <stdint.h>

enum lgps_led_color {
	LGPS_LED_RED = 0,
	LGPS_LED_GREEN,
	LGPS_LED_BLUE,
	LGPS_LED_COLOR_COUNT,
};

enum lgps_led_pattern {
	/* 8 s power-on confirmation: red -> blue -> green, 3 slow blinks each. */
	LGPS_LED_PATTERN_BOOT_CONFIRM = 0,
	/* 8 s shutdown confirmation: green -> blue -> red, 3 slow blinks each. */
	LGPS_LED_PATTERN_SHUTDOWN_CONFIRM,
	/* GNSS fix or POI mark: double green flash, 50 ms on. */
	LGPS_LED_PATTERN_FIX,
	/* BLE advertising ready: single blue flash, 50 ms on. */
	LGPS_LED_PATTERN_BLE_READY,
	/* Bulk sync in progress: fast blue flashes. */
	LGPS_LED_PATTERN_SYNC,
	/* High precision continuous mode: blue/green alternation. */
	LGPS_LED_PATTERN_PRECISION,
	/* PDR takeover (no GNSS): yellow (red+green) flash. */
	LGPS_LED_PATTERN_PDR,
	/* Battery below 10 % or storage full: fast red flashes. */
	LGPS_LED_PATTERN_LOW_BATTERY,
	LGPS_LED_PATTERN_COUNT,
};

/* Configure the three LED pins and start the pattern thread. */
int lgps_led_init(void);

/* Queue @p pattern; returns -EINVAL for an unknown id. */
int lgps_led_play(enum lgps_led_pattern pattern);

/* Force all LEDs off and drop any queued pattern. */
int lgps_led_off_all(void);

/* Drive one LED directly (used by the pattern engine and by self-test). */
int lgps_led_set(enum lgps_led_color color, bool on);

#endif /* LIFEGPS_LED_H_ */
