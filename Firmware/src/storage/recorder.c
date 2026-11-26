/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C automatic trajectory recording policy.
 */

#include <stddef.h>
#include <string.h>

#include <lifegps/recorder.h>

static uint16_t heading_diff_ddeg(uint16_t a, uint16_t b)
{
	uint16_t diff = (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);

	if (diff > 1800U) {
		diff = (uint16_t)(3600U - diff);
	}

	return diff;
}

void lgps_recorder_init(struct lgps_recorder *rec, uint32_t initial_seq_id)
{
	if (rec == NULL) {
		return;
	}

	memset(rec, 0, sizeof(*rec));
	rec->next_seq_id = initial_seq_id;
}

bool lgps_recorder_should_record(const struct lgps_recorder *rec,
				 const struct lgps_recorder_sample *sample)
{
	uint32_t elapsed;
	uint32_t period_ms;

	if ((rec == NULL) || (sample == NULL)) {
		return false;
	}

	if (sample->force_poi) {
		return true;
	}

	if ((sample->power_state == LGPS_PWR_DEEP_SLEEP) ||
	    (sample->power_state == LGPS_PWR_SYSTEM_OFF)) {
		return false;
	}

	if (!rec->has_written) {
		return true;
	}

	elapsed = sample->now_ms - rec->last_write_ms;

	/* Immediate write on floor transition or >=30 deg turn while moving. */
	if (sample->floor_level != rec->last_floor) {
		return true;
	}

	if ((sample->activity != LGPS_ACT_STATIONARY) && (elapsed >= 2000U) &&
	    (heading_diff_ddeg(sample->heading_ddeg, rec->last_heading_ddeg) >=
	     LGPS_REC_TURN_TRIGGER_DDEG)) {
		return true;
	}

	if (sample->power_state == LGPS_PWR_HIGH_PRECISION) {
		period_ms = LGPS_REC_HIGH_PREC_MS;
	} else if (sample->activity == LGPS_ACT_STATIONARY) {
		period_ms = LGPS_REC_ACTIVE_STILL_MS;
	} else {
		period_ms = LGPS_REC_ACTIVE_MOVING_MS;
	}

	return elapsed >= period_ms;
}

uint32_t lgps_recorder_commit(struct lgps_recorder *rec,
			      const struct lgps_recorder_sample *sample)
{
	uint32_t seq;

	if ((rec == NULL) || (sample == NULL)) {
		return 0U;
	}

	seq = rec->next_seq_id;
	rec->next_seq_id++;
	rec->last_write_ms = sample->now_ms;
	rec->last_heading_ddeg = sample->heading_ddeg;
	rec->last_floor = sample->floor_level;
	rec->has_written = true;

	return seq;
}

uint32_t lgps_recorder_last_seq(const struct lgps_recorder *rec)
{
	if ((rec == NULL) || (rec->next_seq_id == 0U)) {
		return 0U;
	}

	return rec->next_seq_id - 1U;
}
