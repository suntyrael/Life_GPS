/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C 4-state power management state machine.
 *
 * Kept free of Zephyr drivers/kernel calls so Firmware/tests/lifegps.power_sm
 * can exhaustively verify every transition on native_sim.
 */

#ifndef LIFEGPS_POWER_SM_LOGIC_H_
#define LIFEGPS_POWER_SM_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

/* Wire-aligned work modes (matches SET_MODE 0x10 and Status offset 124). */
enum lgps_power_state {
	LGPS_PWR_DEEP_SLEEP = 0,
	LGPS_PWR_ACTIVE = 1,
	LGPS_PWR_HIGH_PRECISION = 2,
	LGPS_PWR_SYSTEM_OFF = 3,
};

/* M1_BRINGUP.md section 2: 2 steps within 3 s confirms motion wake-up. */
#define LGPS_PWR_WAKE_CONFIRM_MS 3000U
#define LGPS_PWR_WAKE_CONFIRM_STEPS 2U
#define LGPS_PWR_DEFAULT_AUTO_SLEEP_MS 30000U
#define LGPS_PWR_LOW_BATT_DEGRADE_SOC 10U
#define LGPS_PWR_CRIT_BATT_OFF_SOC 3U

struct lgps_power_sm_logic {
	enum lgps_power_state state;
	bool wake_confirming;
	uint32_t wake_started_ms;
	uint32_t wake_steps_base;
	uint32_t zupt_since_ms;
	bool zupt_tracking;
	uint32_t auto_sleep_ms;
	uint16_t auto_off_min;
	uint32_t deep_sleep_since_ms;
	bool system_off_requested;
};

struct lgps_power_inputs {
	uint32_t now_ms;
	uint32_t steps_total;
	bool zupt_locked;
	bool ble_connected;
	bool charging;
	uint8_t soc_pct; /* 0..100, or 0xFF when unknown */
};

void lgps_power_logic_init(struct lgps_power_sm_logic *sm, uint32_t now_ms);

/* Feed periodic sensor/battery status; returns true if state changed. */
bool lgps_power_logic_step(struct lgps_power_sm_logic *sm, const struct lgps_power_inputs *in);

/* Notify IMU INT1 motion wake-up while in DEEP_SLEEP. */
bool lgps_power_logic_on_motion_irq(struct lgps_power_sm_logic *sm, uint32_t now_ms,
				    uint32_t steps_total);

/* Toggle HIGH_PRECISION <-> ACTIVE (KEY2 2 s hold). */
bool lgps_power_logic_toggle_precision(struct lgps_power_sm_logic *sm, uint32_t now_ms,
				       uint8_t soc_pct, bool charging);

/* Explicit mode request via BLE SET_MODE (0x10). Returns 0 or -EINVAL/-EPERM. */
int lgps_power_logic_set_mode(struct lgps_power_sm_logic *sm, enum lgps_power_state target,
			      uint32_t now_ms, uint8_t soc_pct, bool charging);

/* Request shutdown (dual-key 3 s hold, single-key 8 s hold, or auto-off). */
void lgps_power_logic_request_shutdown(struct lgps_power_sm_logic *sm);

/* Configure stationary auto-sleep delay in milliseconds (0 disables auto-sleep). */
void lgps_power_logic_set_auto_sleep_ms(struct lgps_power_sm_logic *sm, uint32_t delay_ms);

/* Configure DEEP_SLEEP auto-power-off timeout in minutes (0 disables auto-off). */
void lgps_power_logic_set_auto_off_min(struct lgps_power_sm_logic *sm, uint16_t minutes);

#endif /* LIFEGPS_POWER_SM_LOGIC_H_ */
