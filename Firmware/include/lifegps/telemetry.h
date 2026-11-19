/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - machine-readable diagnostic telemetry.
 *
 * The device has no screen, so the DEBUG_UART stream doubles as the instrument.
 * Free-form log lines are fine for events, but periodic state must be parseable:
 * this module owns one fixed-format frame per second, seven lines, fixed field
 * order, fixed key names, units inside the key name, "---" for "not available".
 *
 * Frame layout (one call = seven consecutive log lines, tag "lgps_tele"):
 *
 *   GNSS st=<state> fix=<0..5> sats=<n> pdop_e2=<n|---> lat_e7=<n|---> lon_e7=<n|--->
 *        hmsl_mm=<n|---> spd_cms=<n|---> hdg_ddeg=<n|---> utc_s=<n|---> age_ms=<n|--->
 *        rx=<n> ubx=<n> ckerr=<n> nmea=<n>
 *   IMU  id=0x?? sensor: acc_lsb=a,b,c gyro_lsb=a,b,c acc_g=a,b,c gyro_dps=a,b,c
 *   BODY gravity_g=a,b,c linear_g=a,b,c |gravity|=a |linear|=a (1.000 = 1 g)
 *   TEMP imu_C=a mag_C=a baro=--- (die temperatures)
 *   MAG  sensor: lsb=a,b,c ut=a,b,c |B|=a
 *   BARO raw: press=<n|---> temp=<n|---> (uncompensated, needs TPG)
 *   SYS  bat_mv=<n|---> chg=<0|1> keys=<k1>,<k2> zupt=<locked|move> var_mg2=<n>
 *        steps=<n> ring_seq=<n> ring_rec=<n>
 *
 * Two conventions keep the frame readable when hardware is missing:
 * - a value that is not available is printed as "---" and the field count never
 *   changes, so a diff between two captures lines up;
 * - every line names its frame of reference or its unit, because the same three
 *   numbers mean different things in the sensor frame, the body frame and in
 *   milli-g. The App shows the same split (BLE_Wire_v1.md section 4).
 *
 * Integers are used wherever the underlying value is an integer (record fields,
 * LSB counts, millivolts) so nothing is lost to formatting; the few physically
 * scaled values keep one unit per key name.
 */

#ifndef LIFEGPS_TELEMETRY_H_
#define LIFEGPS_TELEMETRY_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/gnss.h>

struct lgps_telemetry_snapshot {
	/* GNSS */
	enum lgps_gnss_state gnss_state;
	struct lgps_gnss_fix fix;
	uint32_t gnss_age_ms;
	struct lgps_gnss_counters gnss_counters;

	/* IMU (sensor frame, exact LSB counts plus the datasheet-scaled values) */
	bool imu_valid;
	uint8_t imu_who_am_i;
	int16_t acc_lsb[3];
	int16_t gyro_lsb[3];
	float acc_g[3];
	float gyro_dps[3];

	/* Body-frame motion split, in milli-g (1000 = 1 g) */
	int16_t gravity_mg[3];
	int16_t linear_mg[3];
	bool gravity_valid;

	/* Die temperatures in 0.01 degC; the barometer reports its raw 24-bit value */
	int16_t imu_temp_cc;
	int16_t mag_temp_cc;
	bool imu_temp_valid;
	bool mag_temp_valid;

	/* Sensor hub: LIS2MDL + BMP388 (uT are derived in the formatter) */
	bool hub_valid;
	bool mag_valid;
	bool baro_valid;
	int16_t mag_lsb[3];
	uint32_t baro_press_raw;
	uint32_t baro_temp_raw;

	/* System */
	bool bat_valid;
	int32_t bat_mv;
	bool charging;
	uint8_t key_levels;   /* bit0 = KEY1_POI pressed, bit1 = KEY2_MODE pressed */
	bool zupt_locked;
	float zupt_var_g2;
	uint32_t steps;
	uint32_t ring_sector_seq;
	uint32_t ring_records;
};

/* Emit the seven-line frame described above. */
void lgps_telemetry_emit(const struct lgps_telemetry_snapshot *snap);

#endif /* LIFEGPS_TELEMETRY_H_ */
