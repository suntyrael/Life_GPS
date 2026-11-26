/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C barometric floor-level & vertical motion detector.
 */

#include <stddef.h>
#include <string.h>

#include <lifegps/floor.h>

static int32_t abs_i32(int32_t v)
{
	return (v < 0) ? -v : v;
}

static int8_t clamp_floor(int32_t floor)
{
	if (floor > 127) {
		return 127;
	}
	if (floor < -127) {
		return -127;
	}
	return (int8_t)floor;
}

void lgps_floor_init(struct lgps_floor_detector *det)
{
	if (det == NULL) {
		return;
	}

	memset(det, 0, sizeof(*det));
}

void lgps_floor_reset_outdoor(struct lgps_floor_detector *det, int32_t rel_alt_mm, uint32_t now_ms)
{
	if (det == NULL) {
		return;
	}

	det->anchored = true;
	det->anchor_alt_mm = rel_alt_mm;
	det->last_alt_mm = rel_alt_mm;
	det->last_ms = now_ms;
	det->v_speed_mms = 0;
	det->floor_level = 0;
	det->motion = LGPS_VERT_LEVEL;
}

bool lgps_floor_update(struct lgps_floor_detector *det, int32_t rel_alt_mm, uint32_t now_ms)
{
	int32_t dt_ms;
	int32_t diff_mm;
	int32_t floor_center_mm;
	int32_t threshold_mm;
	int8_t prev_floor;

	if (det == NULL) {
		return false;
	}

	if (!det->anchored) {
		lgps_floor_reset_outdoor(det, rel_alt_mm, now_ms);
		return false;
	}

	prev_floor = det->floor_level;
	dt_ms = (int32_t)(now_ms - det->last_ms);
	if (dt_ms >= 500) {
		int32_t d_alt = rel_alt_mm - det->last_alt_mm;
		int32_t inst_mms = (d_alt * 1000) / dt_ms;
		int32_t speed_abs;

		/* Simple 1st-order EMA to suppress BMP388 +/-15 cm single-sample jitter. */
		det->v_speed_mms = (det->v_speed_mms * 3 + inst_mms) / 4;
		speed_abs = abs_i32(det->v_speed_mms);

		if (speed_abs >= LGPS_FLOOR_ELEVATOR_MIN_MMS) {
			det->motion = LGPS_VERT_ELEVATOR;
		} else if (speed_abs >= LGPS_FLOOR_STAIRS_MIN_MMS) {
			det->motion = LGPS_VERT_STAIRS;
		} else {
			det->motion = LGPS_VERT_LEVEL;
		}

		det->last_alt_mm = rel_alt_mm;
		det->last_ms = now_ms;
	}

	/* Hysteresis band around current floor centre prevents chatter near landings. */
	floor_center_mm = det->anchor_alt_mm + ((int32_t)det->floor_level * LGPS_FLOOR_HEIGHT_MM);
	diff_mm = rel_alt_mm - floor_center_mm;
	threshold_mm = (LGPS_FLOOR_HEIGHT_MM / 2) + LGPS_FLOOR_HYSTERESIS_MM;

	while (diff_mm >= threshold_mm) {
		int32_t next = (int32_t)det->floor_level + 1;

		det->floor_level = clamp_floor(next);
		if (det->floor_level == 127) {
			break;
		}
		floor_center_mm = det->anchor_alt_mm + ((int32_t)det->floor_level * LGPS_FLOOR_HEIGHT_MM);
		diff_mm = rel_alt_mm - floor_center_mm;
	}

	while (diff_mm <= -threshold_mm) {
		int32_t next = (int32_t)det->floor_level - 1;

		det->floor_level = clamp_floor(next);
		if (det->floor_level == -127) {
			break;
		}
		floor_center_mm = det->anchor_alt_mm + ((int32_t)det->floor_level * LGPS_FLOOR_HEIGHT_MM);
		diff_mm = rel_alt_mm - floor_center_mm;
	}

	return det->floor_level != prev_floor;
}

int8_t lgps_floor_level(const struct lgps_floor_detector *det)
{
	return (det != NULL) ? det->floor_level : 0;
}

enum lgps_vertical_motion lgps_floor_motion(const struct lgps_floor_detector *det)
{
	return (det != NULL) ? det->motion : LGPS_VERT_LEVEL;
}
