/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include <lifegps/diag.h>
#include <lifegps/keys.h>
#include <lifegps/led.h>
#include <lifegps/power_sm.h>

LOG_MODULE_REGISTER(lgps_power, LOG_LEVEL_INF);

/* Nine slow blinks (three colours x on/off) plus a small margin. */
#define SHUTDOWN_SEQUENCE_MS (3U * 3U * 2U * 500U + 500U)

static bool precision_requested;

bool lgps_power_precision_requested(void)
{
	return precision_requested;
}

static void enter_system_off(void)
{
	LOG_INF("shutdown confirmed, entering System OFF (wake source: reset pin only in M1)");

	/* Let the last UART line drain before the rail collapses. */
	k_msleep(50);

	sys_poweroff();
}

static void on_key(enum lgps_key_id key, enum lgps_key_event_type event, uint32_t held_ms)
{
	switch (event) {
	case LGPS_KEY_EVENT_LONG_PRESS:
		LOG_INF("%s held %u ms -> power off sequence", key == LGPS_KEY_POI ? "KEY1_POI"
										 : "KEY2_MODE",
			held_ms);
		(void)lgps_led_play(LGPS_LED_PATTERN_SHUTDOWN_CONFIRM);
		k_msleep((int32_t)SHUTDOWN_SEQUENCE_MS);
		enter_system_off();
		break;

	case LGPS_KEY_EVENT_PRESS:
		/*
		 * POI is marked the moment the press is debounced: the waypoint belongs to
		 * where the user stood when pressing, and the feedback must not wait for the
		 * release.
		 */
		if (key == LGPS_KEY_POI) {
			LOG_INF("POI mark (KEY1_POI pressed)");
			(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
		}
		break;

	case LGPS_KEY_EVENT_SHORT_PRESS:
		if (key == LGPS_KEY_MODE) {
			precision_requested = !precision_requested;
			LOG_INF("high precision continuous mode %s (KEY2_MODE, held %u ms)",
				precision_requested ? "requested" : "cancelled", held_ms);
			if (precision_requested) {
				(void)lgps_led_play(LGPS_LED_PATTERN_PRECISION);
			}
		}
		break;

	case LGPS_KEY_EVENT_RELEASE:
	default:
		break;
	}
}

int lgps_power_sm_init(void)
{
	int ret;

	ret = lgps_keys_init(on_key);
	if (ret < 0) {
		return ret;
	}

	(void)lgps_led_play(LGPS_LED_PATTERN_BOOT_CONFIRM);

	LOG_INF("power state machine ready (boot confirmation playing)");

	return 0;
}
