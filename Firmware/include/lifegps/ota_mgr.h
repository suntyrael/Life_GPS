/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - MCUboot OTA image confirmation helper.
 *
 * When booted under MCUboot after an SMP over BLE firmware swap, the new image
 * starts in TEST state. Calling lgps_ota_confirm_boot() after subsystem bring-up
 * permanently confirms the image; if bring-up hangs or faults before this point,
 * the hardware watchdog resets the SoC and MCUboot rolls back automatically.
 */

#ifndef LIFEGPS_OTA_MGR_H_
#define LIFEGPS_OTA_MGR_H_

#include <stdbool.h>

/** Confirm the currently running MCUboot primary image if not yet confirmed. */
int lgps_ota_confirm_boot(void);

/** Return true if the active image is confirmed (or MCUboot is not enabled). */
bool lgps_ota_is_confirmed(void);

#endif /* LIFEGPS_OTA_MGR_H_ */
