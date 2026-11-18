/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Life_GPS contributors
 *
 * Fixed-format telemetry frame; see lifegps/telemetry.h for the contract.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <lifegps/telemetry.h>

LOG_MODULE_REGISTER(lgps_tele, LOG_LEVEL_INF);

/* LIS2MDL sensitivity: 1.5 mgauss/LSB (datasheet table 3, So) = 0.15 uT/LSB. */
#define MAG_UT_PER_LSB 0.15F

/* Largest magnitude the mg^2 field may take; see var_mg2_int(). */
#define VAR_MG2_LIMIT 2000000000.0F

/*
 * var_mg2 is a float, and a direct (int32_t) cast of an out-of-range float is
 * undefined behaviour (the conversion does not saturate on Cortex-M). Clamp and
 * neutralise NaN instead of trusting the input range.
 */
static int32_t var_mg2_int(const struct lgps_telemetry_snapshot *snap)
{
	float value_mg2 = snap->zupt_var_g2 * 1000000.0F;

	if (value_mg2 != value_mg2) {
		return 0;
	}

	if (value_mg2 > VAR_MG2_LIMIT) {
		return 2000000000;
	}

	if (value_mg2 < -VAR_MG2_LIMIT) {
		return -2000000000;
	}

	return (int32_t)value_mg2;
}

void lgps_telemetry_emit(const struct lgps_telemetry_snapshot *snap)
{
	char pdop[8];
	char lat[16];
	char lon[16];
	char hmsl[16];
	char spd[12];
	char hdg[12];
	char utc[16];

	if (snap == NULL) {
		return;
	}

	/* GNSS: absent values are printed as "---" so the field count never changes. */
	if (snap->fix.pdop_1e2 == 0U) {
		(void)snprintk(pdop, sizeof(pdop), "---");
	} else {
		(void)snprintk(pdop, sizeof(pdop), "%u", snap->fix.pdop_1e2);
	}

	if (snap->fix.valid) {
		(void)snprintk(lat, sizeof(lat), "%d", snap->fix.lat_e7);
		(void)snprintk(lon, sizeof(lon), "%d", snap->fix.lon_e7);
		(void)snprintk(hmsl, sizeof(hmsl), "%d", snap->fix.hmsl_mm);
		(void)snprintk(spd, sizeof(spd), "%u", snap->fix.speed_cms);
		(void)snprintk(hdg, sizeof(hdg), "%u", snap->fix.heading_ddeg);
	} else {
		(void)snprintk(lat, sizeof(lat), "---");
		(void)snprintk(lon, sizeof(lon), "---");
		(void)snprintk(hmsl, sizeof(hmsl), "---");
		(void)snprintk(spd, sizeof(spd), "---");
		(void)snprintk(hdg, sizeof(hdg), "---");
	}

	if (snap->fix.utc_valid) {
		(void)snprintk(utc, sizeof(utc), "%u", snap->fix.utc_epoch_s);
	} else {
		(void)snprintk(utc, sizeof(utc), "---");
	}

	LOG_INF("GNSS st=%s fix=%u sats=%u pdop_e2=%s lat_e7=%s lon_e7=%s hmsl_mm=%s "
		"spd_cms=%s hdg_ddeg=%s utc_s=%s age_ms=%u rx=%u ubx=%u ckerr=%u nmea=%u",
		lgps_gnss_state_name(snap->gnss_state), snap->fix.fix_type, snap->fix.satellites,
		pdop, lat, lon, hmsl, spd, hdg, utc, snap->gnss_age_ms,
		snap->gnss_counters.rx_bytes, snap->gnss_counters.ubx_frames,
		snap->gnss_counters.ubx_bad_checksum, snap->gnss_counters.nmea_starts);

	if (snap->imu_valid) {
		LOG_INF("IMU id=0x%02x acc_lsb=%d,%d,%d gyro_lsb=%d,%d,%d acc_g=%.3f,%.3f,%.3f "
			"gyro_dps=%.2f,%.2f,%.2f",
			snap->imu_who_am_i, snap->acc_lsb[0], snap->acc_lsb[1], snap->acc_lsb[2],
			snap->gyro_lsb[0], snap->gyro_lsb[1], snap->gyro_lsb[2],
			(double)snap->acc_g[0], (double)snap->acc_g[1], (double)snap->acc_g[2],
			(double)snap->gyro_dps[0], (double)snap->gyro_dps[1],
			(double)snap->gyro_dps[2]);
	} else {
		LOG_INF("IMU id=0x%02x acc_lsb=--- gyro_lsb=--- acc_g=--- gyro_dps=--- "
			"(no sample yet)", snap->imu_who_am_i);
	}

	if (snap->hub_valid) {
		LOG_INF("MAG lsb=%d,%d,%d ut=%.1f,%.1f,%.1f", snap->mag_lsb[0], snap->mag_lsb[1],
			snap->mag_lsb[2],
			(double)((float)snap->mag_lsb[0] * MAG_UT_PER_LSB),
			(double)((float)snap->mag_lsb[1] * MAG_UT_PER_LSB),
			(double)((float)snap->mag_lsb[2] * MAG_UT_PER_LSB));
		LOG_INF("BARO press_raw=%u temp_raw=%u (uncompensated)", snap->baro_press_raw,
			snap->baro_temp_raw);
	} else {
		LOG_INF("MAG lsb=--- ut=--- (sensor hub not streaming)");
		LOG_INF("BARO press_raw=--- temp_raw=--- (sensor hub not streaming)");
	}

	if (snap->bat_valid) {
		LOG_INF("SYS bat_mv=%d chg=%d keys=%u,%u zupt=%s var_mg2=%d steps=%u ring_seq=%u "
			"ring_rec=%u",
			snap->bat_mv, snap->charging ? 1 : 0,
			(unsigned int)(snap->key_levels & 0x01U),
			(unsigned int)((snap->key_levels >> 1) & 0x01U),
			snap->zupt_locked ? "locked" : "move",
			(int32_t)var_mg2_int(snap), snap->steps,
			snap->ring_sector_seq, snap->ring_records);
	} else {
		LOG_INF("SYS bat_mv=--- chg=%d keys=%u,%u zupt=%s var_mg2=%d steps=%u ring_seq=%u "
			"ring_rec=%u",
			snap->charging ? 1 : 0,
			(unsigned int)(snap->key_levels & 0x01U),
			(unsigned int)((snap->key_levels >> 1) & 0x01U),
			snap->zupt_locked ? "locked" : "move",
			(int32_t)var_mg2_int(snap), snap->steps,
			snap->ring_sector_seq, snap->ring_records);
	}
}
