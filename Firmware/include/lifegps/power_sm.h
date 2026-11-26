/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - 4-state power / user interaction state machine.
 */

#ifndef LIFEGPS_POWER_SM_H_
#define LIFEGPS_POWER_SM_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/keys.h>
#include <lifegps/power_sm_logic.h>

typedef void (*lgps_power_poi_cb_t)(void);
typedef void (*lgps_power_pairing_cb_t)(void);

/* Play the power-on confirmation and hook the key callback into the LED engine. */
int lgps_power_sm_init(void);

/* Register optional callbacks for KEY1 POI mark and KEY1 3 s BLE pairing window. */
void lgps_power_register_callbacks(lgps_power_poi_cb_t poi_cb,
				   lgps_power_pairing_cb_t pairing_cb);

/* Current power state (thread-safe). */
enum lgps_power_state lgps_power_get_state(void);

/* True when in HIGH_PRECISION continuous mode. */
bool lgps_power_precision_requested(void);

/* Periodic update from main loop; returns current power state. */
enum lgps_power_state lgps_power_tick(uint32_t now_ms, uint32_t steps_total, bool zupt_locked,
				      bool ble_connected, bool charging, uint8_t soc_pct);

/* Notify motion wake-up from IMU INT1 ISR (deferred to work/main context). */
void lgps_power_on_motion_irq(uint32_t now_ms, uint32_t steps_total);

/* Request mode change via BLE SET_MODE (0x10). */
int lgps_power_set_mode(enum lgps_power_state target);

/* Inject synthetic key event via BLE BUTTON_EVENT (0x33). */
int lgps_power_inject_key(enum lgps_key_id key, enum lgps_key_event_type event);

#endif /* LIFEGPS_POWER_SM_H_ */
