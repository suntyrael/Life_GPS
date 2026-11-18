/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - power / user interaction state machine.
 *
 * Implements the LED language of README section 9.1 for the two events firmware
 * can decide on its own:
 *   power-on   -> red, blue, green: three 1 Hz blinks each
 *   8 s press  -> green, blue, red: three 1 Hz blinks each, then System OFF
 *
 * A short press on KEY1 latches a POI request consumed by the trajectory
 * recording pipeline; a short press on KEY2 toggles the high-precision
 * continuous-mode request flag consumed by the GNSS state machine.
 */

#ifndef LIFEGPS_POWER_SM_H_
#define LIFEGPS_POWER_SM_H_

#include <stdbool.h>

/* Play the power-on confirmation and hook the key callback into the LED engine. */
int lgps_power_sm_init(void);

/* True when the user requested the high precision continuous mode. */
bool lgps_power_precision_requested(void);

/* Atomically consume a pending KEY1 POI request (returns true once per press). */
bool lgps_power_consume_poi_request(void);

#endif /* LIFEGPS_POWER_SM_H_ */
