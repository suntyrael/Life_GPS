/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - KEY1_POI (P1.06) and KEY2_MODE (P1.08).
 *
 * The board has no RC debounce network (pinmap "投板前" list), so this module
 * debounces in software: a level must be stable for LGPS_KEY_DEBOUNCE_MS before
 * it is accepted. Both keys are wired to ground with an internal pull-up, so a
 * pressed key reads 0.
 *
 * A key that already reads low when the firmware boots (stuck button, wrong net,
 * finger on the key during reset) is *disarmed*: it must be released once before
 * it can generate press / long-press events. Without that rule the 8 s power-off
 * timer starts at boot and the device switches itself off ~8 s after power-on.
 *
 * The 8 s long press that powers the device off is defined in README section 9.1.
 */

#ifndef LIFEGPS_KEYS_H_
#define LIFEGPS_KEYS_H_

#include <stdbool.h>
#include <stdint.h>

#define LGPS_KEY_DEBOUNCE_MS 100U
#define LGPS_KEY_LONG_PRESS_MS 8000U
#define LGPS_KEY_SHORT_PRESS_MS 800U

enum lgps_key_id {
	LGPS_KEY_POI = 0,
	LGPS_KEY_MODE,
	LGPS_KEY_COUNT,
};

enum lgps_key_event_type {
	LGPS_KEY_EVENT_PRESS = 0,
	LGPS_KEY_EVENT_RELEASE,
	LGPS_KEY_EVENT_SHORT_PRESS,
	LGPS_KEY_EVENT_LONG_PRESS,
};

typedef void (*lgps_key_callback_t)(enum lgps_key_id key, enum lgps_key_event_type event,
				    uint32_t held_ms);

/*
 * Start the 10 ms polling thread. @p callback is invoked from that thread, so it
 * may block (the power state machine waits for the LED sequence there).
 */
int lgps_keys_init(lgps_key_callback_t callback);

/*
 * Debounced key levels as a bitmask: bit0 = KEY1_POI pressed, bit1 = KEY2_MODE
 * pressed. Used by the telemetry frame so the raw pin state is always visible.
 */
uint8_t lgps_keys_levels(void);

#endif /* LIFEGPS_KEYS_H_ */
