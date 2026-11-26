/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C barometric floor-level & vertical motion detector.
 */

#ifndef LIFEGPS_FLOOR_H_
#define LIFEGPS_FLOOR_H_

#include <stdbool.h>
#include <stdint.h>

/* Standard building storey height & hysteresis in millimetres. */
#define LGPS_FLOOR_HEIGHT_MM 3000
#define LGPS_FLOOR_HYSTERESIS_MM 800
#define LGPS_FLOOR_STAIRS_MIN_MMS 150
#define LGPS_FLOOR_ELEVATOR_MIN_MMS 900

enum lgps_vertical_motion {
	LGPS_VERT_LEVEL = 0,
	LGPS_VERT_STAIRS = 1,
	LGPS_VERT_ELEVATOR = 2,
};

struct lgps_floor_detector {
	bool anchored;
	int32_t anchor_alt_mm;
	int32_t last_alt_mm;
	uint32_t last_ms;
	int32_t v_speed_mms;
	int8_t floor_level;
	enum lgps_vertical_motion motion;
};

void lgps_floor_init(struct lgps_floor_detector *det);

/* Anchor floor 0 at the given relative altitude (e.g., entering indoors from GNSS). */
void lgps_floor_reset_outdoor(struct lgps_floor_detector *det, int32_t rel_alt_mm, uint32_t now_ms);

/* Feed barometric relative altitude (mm); returns true if floor_level changed. */
bool lgps_floor_update(struct lgps_floor_detector *det, int32_t rel_alt_mm, uint32_t now_ms);

int8_t lgps_floor_level(const struct lgps_floor_detector *det);
enum lgps_vertical_motion lgps_floor_motion(const struct lgps_floor_detector *det);

#endif /* LIFEGPS_FLOOR_H_ */
