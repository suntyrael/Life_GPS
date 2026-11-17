/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - hardware watchdog with per-task check-in.
 *
 * README section 11 phase 2 requires a 30 s WDT clocked from the low-frequency
 * clock that keeps counting through System ON sleep, plus "layered feeding": the
 * watchdog is only petted when every supervised task has checked in since the
 * previous pet. A single stuck task therefore resets the device instead of being
 * masked by a blanket feed in the main loop.
 */

#ifndef LIFEGPS_WDT_H_
#define LIFEGPS_WDT_H_

#include <stdint.h>

#define LGPS_WDT_TIMEOUT_MS 30000U

enum lgps_wdt_task {
	LGPS_WDT_TASK_MAIN = 0,
	LGPS_WDT_TASK_GNSS,
	LGPS_WDT_TASK_IMU,
	LGPS_WDT_TASK_COUNT,
};

/* Install the 30 s timeout and start the watchdog. */
int lgps_wdt_init(void);

/* Mark @p task as alive for the current watchdog window. */
void lgps_wdt_checkin(enum lgps_wdt_task task);

/*
 * Pet the watchdog only when all tasks checked in; otherwise log which task is
 * missing and let the timeout expire.
 */
void lgps_wdt_feed(void);

/* Bitmask of tasks that have not checked in yet (for diagnostics). */
uint32_t lgps_wdt_missing(void);

#endif /* LIFEGPS_WDT_H_ */
