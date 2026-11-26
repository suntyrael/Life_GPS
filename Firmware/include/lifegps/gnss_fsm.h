/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C GNSS six-state power policy machine.
 *
 * Dependency free so twister can exercise every cold-start, ephemeris-hold,
 * hot-pulse (3.5 s -> 8 s -> 15 s), exponential backoff (120 s / 300 s / 900 s),
 * vehicle-mode (180 s) and ZUPT-stationary pause transition on native_sim.
 */

#ifndef LIFEGPS_GNSS_FSM_H_
#define LIFEGPS_GNSS_FSM_H_

#include <stdbool.h>
#include <stdint.h>

#define LGPS_GNSS_FSM_COLD_TIMEOUT_MS      60000U
#define LGPS_GNSS_FSM_EPHEMERIS_HOLD_MS    35000U
#define LGPS_GNSS_FSM_EPHEMERIS_TTL_MS     (4U * 3600U * 1000U)
#define LGPS_GNSS_FSM_HOT_STEP1_MS         3500U
#define LGPS_GNSS_FSM_HOT_STEP2_MS         8000U
#define LGPS_GNSS_FSM_HOT_MAX_MS           15000U
#define LGPS_GNSS_FSM_PULSE_PERIOD_MS      60000U
#define LGPS_GNSS_FSM_VEHICLE_PERIOD_MS    180000U
#define LGPS_GNSS_FSM_BACKOFF1_MS          120000U
#define LGPS_GNSS_FSM_BACKOFF2_MS          300000U
#define LGPS_GNSS_FSM_BACKOFF3_MS          900000U

enum lgps_gnss_fsm_state {
	LGPS_GNSS_FSM_OFF = 0,
	LGPS_GNSS_FSM_COLD_ACQ,
	LGPS_GNSS_FSM_EPHEMERIS_HOLD,
	LGPS_GNSS_FSM_HOT_ACQ,
	LGPS_GNSS_FSM_TRACKING,
	LGPS_GNSS_FSM_BACKOFF,
};

enum lgps_gnss_fsm_action {
	LGPS_GNSS_FSM_ACTION_NONE = 0,
	LGPS_GNSS_FSM_ACTION_POWER_UP,
	LGPS_GNSS_FSM_ACTION_POWER_DOWN,
};

struct lgps_gnss_fsm_input {
	uint32_t now_ms;
	bool fix_valid;
	bool zupt_locked;
	bool vehicle_mode;
	bool high_precision;
	bool hold_power;
	bool wake_trigger; /* e.g. motion resumed or POI key pressed */
};

struct lgps_gnss_fsm {
	enum lgps_gnss_fsm_state state;
	uint32_t state_since_ms;
	uint32_t last_fix_ms;
	uint32_t next_wait_ms;
	uint8_t fail_streak;
	bool ephemeris_valid;
	bool rail_on;
};

/* Initialise FSM in OFF state (caller decides when to start first acquisition). */
void lgps_gnss_fsm_init(struct lgps_gnss_fsm *fsm, uint32_t now_ms);

/* Advance FSM by one tick and return whether the GNSS rail must be toggled. */
enum lgps_gnss_fsm_action lgps_gnss_fsm_step(struct lgps_gnss_fsm *fsm,
					     const struct lgps_gnss_fsm_input *in);

/* Notify FSM that receiver was explicitly cold-restarted (invalidates ephemeris). */
void lgps_gnss_fsm_note_cold_restart(struct lgps_gnss_fsm *fsm, uint32_t now_ms);

/* Current hot-acquisition timeout budget (3.5 s / 8 s / 15 s) for diagnostics. */
uint32_t lgps_gnss_fsm_hot_timeout_ms(const struct lgps_gnss_fsm *fsm);

#endif /* LIFEGPS_GNSS_FSM_H_ */
