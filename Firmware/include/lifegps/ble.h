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

/*
 * IMU telemetry, called once per IMU sample with body-frame raw counts. The App
 * uses it for the pose view: same frame as the quaternion, so the acceleration
 * vector can be drawn next to the body without a hidden axis swap.
 *
 * Cheap when nobody subscribes: it returns before encoding in that case, so the
 * main loop can call it at the full 26 Hz unconditionally.
 */
void lgps_ble_publish_imu(const int16_t acc_lsb[3], const int16_t gyro_lsb[3],
			  const int16_t quat_q15[4]);

/* PDR telemetry, called when a step completed (and once per second as a
 * heartbeat by the caller if it wants one).
 */
void lgps_ble_publish_pdr(int32_t x_mm, int32_t y_mm, int16_t heading_ddeg, uint16_t steps);

/* GNSS receive-path telemetry, called once per second.
 *
 * Carries the counters that answer "why is there no fix": silent receiver
 * (rx_bytes stuck), NMEA-only stream (nmea_starts growing, ubx_frames flat),
 * corrupt framing (ubx_bad_checksum) or an overflowing RX ring (rx_dropped).
 * The solution itself stays in the Status frame.
 */
void lgps_ble_publish_gnss(const struct lgps_ble_telem_gnss *gnss);

/* Satellite table (telemetry 0x09): at most LGPS_BLE_TELEM_SAT_MAX rows. */
void lgps_ble_publish_sat(const struct lgps_ble_telem_sat_entry *entries, uint8_t count,
			  uint8_t total);

/* Barometer (telemetry 0x04), once per second: compensated pressure, height above
 * the baseline and the baseline itself. The die temperature is read from the
 * snapshot, so the caller passes the one it just built.
 */
void lgps_ble_publish_baro(const struct lgps_telemetry_snapshot *snap);

/* One raw NMEA sentence (telemetry 0x08). Cheap no-op when nobody subscribes. */
void lgps_ble_publish_nmea(const char *line, uint8_t len);

#endif /* LIFEGPS_BLE_H_ */
