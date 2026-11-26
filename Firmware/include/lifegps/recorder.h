/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C automatic trajectory recording policy.
 */

#ifndef LIFEGPS_RECORDER_H_
#define LIFEGPS_RECORDER_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/activity.h>
#include <lifegps/power_sm_logic.h>
#include <lifegps/traj.h>

#define LGPS_REC_HIGH_PREC_MS 1000U
#define LGPS_REC_ACTIVE_MOVING_MS 10000U
#define LGPS_REC_ACTIVE_STILL_MS 60000U
#define LGPS_REC_TURN_TRIGGER_DDEG 300U /* 30.0 deg heading change */

struct lgps_recorder {
	uint32_t next_seq_id;
	uint32_t last_write_ms;
	uint16_t last_heading_ddeg;
	int8_t last_floor;
	bool has_written;
};

struct lgps_recorder_sample {
	uint32_t now_ms;
	enum lgps_power_state power_state;
	enum lgps_activity_kind activity;
	uint16_t heading_ddeg;
	int8_t floor_level;
	bool force_poi;
};

void lgps_recorder_init(struct lgps_recorder *rec, uint32_t initial_seq_id);

/* Returns true when a 32-byte trajectory record should be persisted now. */
bool lgps_recorder_should_record(const struct lgps_recorder *rec,
				 const struct lgps_recorder_sample *sample);

/* Allocate the next monotonic seq_id and stamp recorder bookkeeping. */
uint32_t lgps_recorder_commit(struct lgps_recorder *rec,
			      const struct lgps_recorder_sample *sample);

uint32_t lgps_recorder_last_seq(const struct lgps_recorder *rec);

#endif /* LIFEGPS_RECORDER_H_ */
