/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Life_GPS contributors
 *
 * Fixed-format telemetry frame; see lifegps/telemetry.h for the contract.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <math.h>
#include <stdint.h>

#include <lifegps/diag.h>
#include <lifegps/gnss.h>
#include <lifegps/telemetry.h>
#include <lifegps/time.h>

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

/* Magnitude of a milli-g triple, in g. float math keeps libm small: the rest of
 * the firmware already works in single precision.
 */
static float norm_g(const int16_t mg[3])
{
	const float x = (float)mg[0] / 1000.0F;
	const float y = (float)mg[1] / 1000.0F;
	const float z = (float)mg[2] / 1000.0F;

	return sqrtf((x * x) + (y * y) + (z * z));
}

/* Field magnitude in uT: the quantity that must stay near 25..65 uT for the
 * heading to be trustworthy (README 8.4 anomaly window is relative to it).
 */
static float mag_norm_ut(const struct lgps_telemetry_snapshot *snap)
{
	const float x = (float)snap->mag_lsb[0] * MAG_UT_PER_LSB;
	const float y = (float)snap->mag_lsb[1] * MAG_UT_PER_LSB;
	const float z = (float)snap->mag_lsb[2] * MAG_UT_PER_LSB;

	return sqrtf((x * x) + (y * y) + (z * z));
}

/* 0.01 degC -> "-1.25", or "---" when the sensor did not report. */
static const char *fmt_cc(char *buf, size_t size, int16_t cc, bool valid)
{
	int32_t hundredths = cc;

	if (!valid) {
		return "---";
	}

	(void)snprintk(buf, size, "%d.%02d", (int)(hundredths / 100),
		       (int)((hundredths % 100 < 0) ? -(hundredths % 100) : hundredths % 100));

	return buf;
}

/* Local wall clock as HH:MM:SS+08:00, or "---" before the clock is usable. */
static const char *fmt_clock(char *buf, size_t size)
{
	struct lgps_utc local;
	int32_t offset_s;
	uint32_t magnitude_s;
	char sign;

	if (!lgps_time_now_local(&local, lgps_diag_uptime_ms())) {
		return "---";
	}

	offset_s = lgps_time_local_offset_s();
	sign = (offset_s < 0) ? '-' : '+';
	magnitude_s = (uint32_t)((offset_s < 0) ? -offset_s : offset_s);

	(void)snprintk(buf, size, "%02u:%02u:%02u%c%02u:%02u", (unsigned int)local.hour,
		       (unsigned int)local.minute, (unsigned int)local.second, sign,
		       (unsigned int)(magnitude_s / 3600U),
		       (unsigned int)((magnitude_s % 3600U) / 60U));

	return buf;
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
	char imu_txt[10];
	char mag_txt[10];
	char baro_txt[10];
	char clk_txt[24];

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

	char ttff_s[16];

	/*
	 * TTFF of the current power cycle: 0 until the first 3D fix, then the number
	 * of milliseconds from GPS_EN going high. Printed next to the state so a
	 * "seconds or minutes" question is answered without the App.
	 */
	{
		struct lgps_gnss_ttff ttff;

		lgps_gnss_get_ttff(&ttff);
		if (ttff.ttff_ms == 0U) {
			(void)snprintk(ttff_s, sizeof(ttff_s), "---");
		} else {
			(void)snprintk(ttff_s, sizeof(ttff_s), "%u.%u",
				       ttff.ttff_ms / 1000U, (ttff.ttff_ms % 1000U) / 100U);
		}
	}

	LOG_INF("GNSS st=%s fix=%u sats=%u pdop_e2=%s lat_e7=%s lon_e7=%s hmsl_mm=%s "
		"spd_cms=%s hdg_ddeg=%s utc_s=%s age_ms=%u ttff_s=%s rx=%u ubx=%u ckerr=%u nmea=%u",
		lgps_gnss_state_name(snap->gnss_state), snap->fix.fix_type, snap->fix.satellites,
		pdop, lat, lon, hmsl, spd, hdg, utc, snap->gnss_age_ms, ttff_s,
		snap->gnss_counters.rx_bytes, snap->gnss_counters.ubx_frames,
		snap->gnss_counters.ubx_bad_checksum, snap->gnss_counters.nmea_starts);

	/* One line per question a bring-up engineer asks: what did the sensor
	 * report, what does it mean physically, and is the derived split sane.
	 */
	if (snap->imu_valid) {
		LOG_INF("IMU  id=0x%02x sensor: acc_lsb=%d,%d,%d gyro_lsb=%d,%d,%d "
			"acc_g=%.3f,%.3f,%.3f gyro_dps=%.2f,%.2f,%.2f",
			snap->imu_who_am_i, snap->acc_lsb[0], snap->acc_lsb[1], snap->acc_lsb[2],
			snap->gyro_lsb[0], snap->gyro_lsb[1], snap->gyro_lsb[2],
			(double)snap->acc_g[0], (double)snap->acc_g[1], (double)snap->acc_g[2],
			(double)snap->gyro_dps[0], (double)snap->gyro_dps[1],
			(double)snap->gyro_dps[2]);
	} else {
		LOG_INF("IMU  id=0x%02x sensor: acc_lsb=--- gyro_lsb=--- acc_g=--- "
			"gyro_dps=--- (no sample yet)", snap->imu_who_am_i);
	}

	if (snap->gravity_valid) {
		LOG_INF("BODY gravity_g=%.3f,%.3f,%.3f linear_g=%.3f,%.3f,%.3f "
			"|gravity|=%.3f |linear|=%.3f (1.000 = 1 g)",
			(double)(snap->gravity_mg[0] / 1000.0F),
			(double)(snap->gravity_mg[1] / 1000.0F),
			(double)(snap->gravity_mg[2] / 1000.0F),
			(double)(snap->linear_mg[0] / 1000.0F),
			(double)(snap->linear_mg[1] / 1000.0F),
			(double)(snap->linear_mg[2] / 1000.0F),
			(double)norm_g(snap->gravity_mg), (double)norm_g(snap->linear_mg));
	} else {
		LOG_INF("BODY gravity_g=--- linear_g=--- |gravity|=--- |linear|=--- "
			"(attitude not running yet)");
	}

	LOG_INF("TEMP imu_C=%s mag_C=%s baro_C=%s (die temperatures)",
		fmt_cc(imu_txt, sizeof(imu_txt), snap->imu_temp_cc, snap->imu_temp_valid),
		fmt_cc(mag_txt, sizeof(mag_txt), snap->mag_temp_cc, snap->mag_temp_valid),
		fmt_cc(baro_txt, sizeof(baro_txt), snap->baro_temp_cc, snap->baro_temp_valid));

	if (snap->mag_valid) {
		LOG_INF("MAG  sensor: lsb=%d,%d,%d ut=%.1f,%.1f,%.1f |B|=%.1f",
			snap->mag_lsb[0], snap->mag_lsb[1], snap->mag_lsb[2],
			(double)((float)snap->mag_lsb[0] * MAG_UT_PER_LSB),
			(double)((float)snap->mag_lsb[1] * MAG_UT_PER_LSB),
			(double)((float)snap->mag_lsb[2] * MAG_UT_PER_LSB),
			(double)mag_norm_ut(snap));
	} else {
		LOG_INF("MAG  sensor: lsb=--- ut=--- |B|=--- (LIS2MDL not answering)");
	}

	if (snap->baro_valid) {
		char alt_txt[16];
		char msl_txt[16];

		if (snap->baro_alt_valid) {
			(void)snprintk(alt_txt, sizeof(alt_txt), "%d", snap->baro_rel_alt_mm);
		} else {
			(void)snprintk(alt_txt, sizeof(alt_txt), "---");
		}

		if (snap->baro_msl_valid) {
			(void)snprintk(msl_txt, sizeof(msl_txt), "%d", snap->baro_msl_alt_mm);
		} else {
			(void)snprintk(msl_txt, sizeof(msl_txt), "---");
		}

		if (snap->baro_comp_valid) {
			LOG_INF("BARO press_pa=%d temp_C=%s rel_alt_mm=%s alt_msl_mm=%s press_raw=%u "
				"temp_raw=%u (TPG compensated, BMP388 9.2/9.3)", snap->baro_press_pa,
				fmt_cc(baro_txt, sizeof(baro_txt), snap->baro_temp_cc,
				       snap->baro_temp_valid),
				alt_txt, msl_txt, snap->baro_press_raw, snap->baro_temp_raw);
		} else {
			LOG_INF("BARO press_pa=--- temp_C=--- rel_alt_mm=--- alt_msl_mm=--- "
				"press_raw=%u temp_raw=%u (raw counts: no TPG coefficients)",
				snap->baro_press_raw, snap->baro_temp_raw);
		}
	} else {
		LOG_INF("BARO press_pa=--- temp_C=--- rel_alt_mm=--- alt_msl_mm=--- press_raw=--- "
			"temp_raw=--- (BMP388 never acknowledged its identification read)");
	}

	if (snap->bat_valid) {
		LOG_INF("SYS bat_mv=%d chg=%d keys=%u,%u zupt=%s var_mg2=%d steps=%u ring_seq=%u "
			"ring_rec=%u clk=%s sync=%u",
			snap->bat_mv, snap->charging ? 1 : 0,
			(unsigned int)(snap->key_levels & 0x01U),
			(unsigned int)((snap->key_levels >> 1) & 0x01U),
			snap->zupt_locked ? "locked" : "move",
			(int32_t)var_mg2_int(snap), snap->steps,
			snap->ring_sector_seq, snap->ring_records,
			fmt_clock(clk_txt, sizeof(clk_txt)), lgps_time_synced() ? 1U : 0U);
	} else {
		LOG_INF("SYS bat_mv=--- chg=%d keys=%u,%u zupt=%s var_mg2=%d steps=%u ring_seq=%u "
			"ring_rec=%u clk=%s sync=%u",
			snap->charging ? 1 : 0,
			(unsigned int)(snap->key_levels & 0x01U),
			(unsigned int)((snap->key_levels >> 1) & 0x01U),
			snap->zupt_locked ? "locked" : "move",
			(int32_t)var_mg2_int(snap), snap->steps,
			snap->ring_sector_seq, snap->ring_records,
			fmt_clock(clk_txt, sizeof(clk_txt)), lgps_time_synced() ? 1U : 0U);
	}
}
