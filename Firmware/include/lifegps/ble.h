/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - Bluetooth LE link API.
 *
 * The product has no screen: the phone is the display, so the BLE link is a
 * first-class output, not a debug add-on. It mirrors what the DEBUG_UART
 * already prints (status snapshot + log lines) and accepts the documented
 * command set from Doc/Software/BLE_Wire_v1.md.
 *
 * Division of labour with the rest of the firmware:
 *   - nothing here reads a sensor, the ADC or the GNSS, because those buses have
 *     a single owner (the main loop). lgps_ble_publish() is called by the main
 *     loop with the snapshot it already assembled.
 *   - the GATT callbacks never block: Control writes are queued to a thread,
 *     log lines go through a slot ring, and only the POI command touches flash.
 */

#ifndef LIFEGPS_BLE_H_
#define LIFEGPS_BLE_H_

#include <lifegps/ble_frame.h>
#include <lifegps/telemetry.h>

/*
 * Start the Bluetooth stack, publish the LGPS service and begin advertising
 * as LGPS-XXXX. Call once from the bring-up sequence, after the subsystems the
 * status frame reports on.
 *
 * Returns 0 on success, negative errno when Bluetooth cannot be enabled or the
 * GATT table cannot be resolved (in which case the link is unusable and the
 * caller reports it instead of advertising a device that answers nothing).
 */
int lgps_ble_init(void);

/*
 * True while a central has the Status characteristic subscribed. The main loop
 * uses it to publish at the BLE rate only when somebody is listening; without a
 * subscriber the snapshot is refreshed at the (slow) text cadence instead.
 */
bool lgps_ble_status_subscribed(void);

/*
 * Encode @p snap as the 82-byte Status frame, cache it for reads and notify the
 * subscribed central. Thread context: any thread (the main loop calls it at the
 * publication rate). Safe to call before lgps_ble_init(): the frame is cached
 * and simply not notified.
 */
void lgps_ble_publish(const struct lgps_telemetry_snapshot *snap);

#endif /* LIFEGPS_BLE_H_ */
