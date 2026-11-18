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

/*
 * Supervision window. Tasks run at very different rates (IMU 26 Hz data-ready,
 * GNSS 50 Hz service loop, main 100 Hz), so "all tasks checked in since the last
 * pet" can never be true at an arbitrary instant. The feed is therefore evaluated
 * once per window: every task must have checked in at least once inside it.
 * 5 s leaves six evaluation attempts before the 30 s timeout, and a dead task
 * cannot satisfy the next window because the mask is cleared on a successful pet.
 */
#define LGPS_WDT_FEED_WINDOW_MS 5000U

/*
 * One entry per *thread*. The IMU sampling runs inside the main loop, so it is not
 * a supervised task of its own: a check-in must mean "this thread is still
 * running", never "the last bus operation succeeded". Otherwise a silent sensor
 * (or a rejected SPI read) turns into a 30 s reset loop instead of a logged fault.
 */
enum lgps_wdt_task {
	LGPS_WDT_TASK_MAIN = 0,
	LGPS_WDT_TASK_GNSS,
	LGPS_WDT_TASK_COUNT,
};

/* Install the 30 s timeout and start the watchdog. */
int lgps_wdt_init(void);

/* Mark @p task as alive for the current watchdog window. */
void lgps_wdt_checkin(enum lgps_wdt_task task);

/*
 * Called from the main loop at any rate; only acts once per
 * LGPS_WDT_FEED_WINDOW_MS. Pets the watchdog when every task checked in inside the
 * window, otherwise logs the missing bitmap and lets the timeout expire.
 */
void lgps_wdt_feed(void);

/* Bitmask of tasks that have not checked in yet (for diagnostics). */
uint32_t lgps_wdt_missing(void);

#endif /* LIFEGPS_WDT_H_ */
