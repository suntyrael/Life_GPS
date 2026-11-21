/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * M1 bring-up entry point.
 *
 * Order matters and mirrors the pinmap:
 *   1. diagnostics first, because everything below logs into it
 *   2. board GPIO (chip selects before the memories are powered/selected)
 *   3. status LED and keys (the user needs feedback even if a later stage fails)
 *   4. watchdog, so a hang in any later stage is recoverable
 *   5. GNSS, IMU, storage
 *
 * The loop polls the IMU every 10 ms but only consumes samples the chip reports as
 * fresh (STATUS_REG XLDA|GDA), so the ZUPT/PDR chain runs at exactly the configured
 * 30 Hz ODR with no sleep-drift. 30 samples therefore span exactly 1.000 s, the
 * window README 7.5 specifies. The same loop feeds the watchdog and publishes a
 * 1 Hz status line.
 *
 * The LSM6DSV16X also streams its SFLP game rotation vector and gravity vector
 * through the FIFO (lifegps/sflp.h). The gravity vector is consumed here: it
 * splits the measured acceleration into gravity and linear acceleration for the
 * BLE Status frame. The quaternion is decoded and logged once at bring-up but not
 * published - it is a 6-axis vector without magnetometer yaw, so the world-aligned
 * quaternion in the BLE contract stays the firmware filter's (see README 7.2).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <math.h>
#include <string.h>

#include <lifegps/battery.h>
#include <lifegps/baro.h>
#include <lifegps/ble.h>
#include <lifegps/board_io.h>
#include <lifegps/bmp388.h>
#include <lifegps/calib.h>
#include <lifegps/diag.h>
#include <lifegps/flash_ring.h>
#include <lifegps/gnss.h>
#include <lifegps/imu.h>
#include <lifegps/keys.h>
#include <lifegps/led.h>
#include <lifegps/nav.h>
#include <lifegps/power_sm.h>
#include <lifegps/telemetry.h>
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_main, LOG_LEVEL_INF);

/* Poll faster than the 30 Hz ODR and let the data-ready bit do the pacing. */
#define MAIN_LOOP_PERIOD_MS 10U
/* The five-line text frame on DEBUG_UART keeps its 1 Hz cadence: it is the
 * scope-free instrument for bring-up and changing it would invalidate every
 * earlier capture.
 */
#define TELEMETRY_PERIOD_MS 1000U
/* NMEA drain cadence: the ring holds 8 sentences, a burst must not overflow it. */
#define NMEA_PUMP_PERIOD_MS 50U
#define NMEA_PUMP_BURST 4U
#define GNSS_NMEA_LINE_MAX_TEXT 96U
#define BATTERY_PERIOD_MS 10000U
/* The BLE Status frame is the phone UI's only live feed, so it is published much
 * faster - but only while a central is subscribed. Without a subscriber there is
 * nothing to show, so the snapshot is refreshed at the text cadence and the
 * GET_STATUS cache stays warm.
 */
#define BLE_STATUS_PERIOD_MS 100U
#define BLE_STATUS_IDLE_PERIOD_MS 1000U
/* A GNSS course is only meaningful once the device is really moving; below this
 * speed the heading comes from the magnetometer instead.
 */
#define HEADING_ALIGN_MIN_SPEED_CMS 150U
#define RAD_TO_DEG 57.2957795F

static struct lgps_zupt zupt;
static struct lgps_pdr pdr;
static struct lgps_attitude attitude;
static struct lgps_step step_detector;

/* Last magnetometer sample in the body frame, refreshed whenever the sensor hub
 * is read. The attitude filter consumes it at the IMU rate, but the hub itself
 * has a 50 Hz ODR and is only read with the published snapshot, so the value is
 * cached rather than fetched twice.
 */
static float hub_mag_body_ut[3];
static bool hub_mag_valid;

/* Body-frame gravity and linear acceleration of the newest IMU sample. The
 * accelerometer alone cannot tell "tilted" from "being pushed", so the split is
 * computed here from the attitude estimate (or from the SFLP gravity vector, when
 * the sensor delivers one) and published with the snapshot.
 */
static float gravity_body_g[3];
static float linear_body_g[3];
static bool gravity_valid;

/* Newest SFLP sample from the FIFO. Each half carries its own valid flag, so a
 * gravity entry without a quaternion (or the other way round) is handled.
 */
static struct lgps_imu_sflp sflp_state;
static bool sflp_quat_logged;

/* Body-frame raw counts for the telemetry packet. The body matrix is a signed
 * permutation of the sensor axes (see lifegps/nav.h), so this is the same
 * mapping lgps_nav_body_from_imu() applies to the scaled values.
 */
static void imu_to_body_lsb(const int16_t in[3], int16_t out[3])
{
	out[0] = (int16_t)-in[1];
	out[1] = (int16_t)-in[0];
	out[2] = (int16_t)-in[2];
}

/*
 * Fill gravity_body_g / linear_body_g for one sample.
 *
 * The SFLP gravity vector wins when it is present and plausible: it is the same
 * hardware accelerometer, fused by the SFLP block at 30 Hz, so it has no
 * integration drift. The plausibility window is the one lifegps/nav.h already
 * defines for "this sample is gravity and not a shove"; outside it the SFLP entry
 * is a fault signature and the firmware attitude estimate takes over.
 *
 * Returns true when the two vectors were written.
 */
static bool update_gravity_split(const float accel_body[3])
{
	float body[3] = { 0.0F, 0.0F, 0.0F };
	bool from_sflp = false;

	if (sflp_state.gravity_valid) {
		float norm;

		lgps_nav_body_from_imu(sflp_state.gravity_g, body);
		norm = sqrtf((body[0] * body[0]) + (body[1] * body[1]) +
			     (body[2] * body[2]));
		from_sflp = (norm >= LGPS_ATT_ACCEL_NORM_MIN_G) &&
			    (norm <= LGPS_ATT_ACCEL_NORM_MAX_G);
	}

	if (!from_sflp) {
		if (!attitude.valid) {
			return false;
		}

		lgps_attitude_gravity_body(&attitude, body);
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		gravity_body_g[axis] = body[axis];
		linear_body_g[axis] = accel_body[axis] - body[axis];
	}

	return true;
}

static int init_subsystems(void)
{
	int ret;

	ret = lgps_board_io_init();
	if (ret < 0) {
		return ret;
	}

	ret = lgps_board_io_unused_pins_off();
	if (ret < 0) {
		return ret;
	}

	ret = lgps_led_init();
	if (ret < 0) {
		return ret;
	}

	ret = lgps_power_sm_init();
	if (ret < 0) {
		return ret;
	}

	ret = lgps_wdt_init();
	if (ret < 0) {
		return ret;
	}

	ret = lgps_battery_init();
	if (ret < 0) {
		LOG_ERR("battery sensing unavailable (%d)", ret);
	} else {
		int32_t millivolts = 0;

		if (lgps_battery_read_mv(&millivolts) == 0) {
			LOG_INF("battery: %d mV at boot (divider calibrated pack:tap = %u:%u uV)",
				millivolts, (uint32_t)LGPS_BATT_CAL_PACK_UV,
				(uint32_t)LGPS_BATT_CAL_TAP_UV);
		} else {
			LOG_ERR("battery: first conversion failed");
		}
	}

	ret = lgps_gnss_init();
	if (ret < 0) {
		LOG_ERR("GNSS init failed (%d)", ret);
	}

	ret = lgps_imu_init();
	if (ret < 0) {
		LOG_ERR("IMU init failed (%d)", ret);
		return ret;
	}

	struct lgps_imu_hub_ids ids;

	ret = lgps_imu_probe_hub(&ids);
	if (ret < 0) {
		LOG_ERR("sensor hub probe failed (%d)", ret);
	}

	/* One-shot bring-up diagnostics: proves the hub read/write paths before the
	 * streaming configuration overwrites them. Bring-up only - about 4 s of boot
	 * time and a few hundred bytes of strings - so it is compiled out unless
	 * CONFIG_LGPS_IMU_HUB_DIAG is set (see boards/lifegps/lifegps/Kconfig.lifegps).
	 */
	if (IS_ENABLED(CONFIG_LGPS_IMU_HUB_DIAG)) {
		ret = lgps_imu_hub_diag();
		if (ret < 0) {
			LOG_WRN("sensor hub diagnostics failed (%d)", ret);
		}
	}

	ret = lgps_imu_hub_configure();
	if (ret < 0) {
		LOG_ERR("sensor hub streaming not configured (%d)", ret);
	}

	ret = lgps_ring_init();
	if (ret < 0) {
		LOG_ERR("external flash ring unavailable (%d)", ret);
	}

	/* Calibration coefficients sit in their own NOR sector, so this needs the flash
	 * to be up. A missing or CRC-bad record is not an error: the compiled-in
	 * defaults stay in effect and GET_CAPS/mag_cal report that.
	 */
	ret = lgps_calib_init();
	if (ret < 0) {
		LOG_WRN("calibration record unavailable (%d), using defaults", ret);
	}

	/* Barometric baseline: its own NOR sector, loaded after the ring so the flash
	 * is up. No record (or a corrupted one) just means "no height reference yet",
	 * which the GNSS gate or BARO_BASELINE can fix at any time.
	 */
	lgps_baro_init();
	ret = lgps_baro_load();
	if (ret < 0) {
		LOG_INF("no barometric baseline stored (%d); height relative to nothing", ret);
	}

	lgps_zupt_reset(&zupt, lgps_diag_uptime_ms());
	lgps_pdr_init(&pdr);
	lgps_attitude_reset(&attitude, NULL);
	lgps_step_reset(&step_detector, lgps_diag_uptime_ms());

	/* The BLE link is the product's only display, so it starts last: every
	 * stage above has already reported a failure on the console before the
	 * radio comes up, and advertising never points at a half-built device.
	 */
	ret = lgps_ble_init();
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static struct lgps_telemetry_snapshot *build_snapshot(uint32_t now_ms,
					      const struct lgps_imu_sample *sample,
					      bool imu_valid, int32_t bat_mv, bool bat_valid)
{
	static struct lgps_telemetry_snapshot snap;
	struct lgps_imu_mag mag;
	struct lgps_imu_baro baro;
	struct lgps_ring_stats ring;
	bool charging = false;
	int ret;

	memset(&snap, 0, sizeof(snap));

	lgps_gnss_get_fix(&snap.fix);
	if (snap.fix.utc_valid) {
		/* Only used to date a calibration record; the calibration module has no
		 * clock of its own.
		 */
		lgps_calib_note_utc(snap.fix.utc_epoch_s);
	}
	lgps_gnss_get_counters(&snap.gnss_counters);
	snap.gnss_state = lgps_gnss_get_state();
	if (snap.fix.last_pvt_uptime_ms != 0U) {
		snap.gnss_age_ms = now_ms - snap.fix.last_pvt_uptime_ms;
	}

	snap.imu_who_am_i = lgps_imu_who_am_i();
	snap.imu_valid = imu_valid;
	if (imu_valid) {
		for (uint8_t axis = 0U; axis < 3U; axis++) {
			snap.acc_lsb[axis] = sample->raw_accel[axis];
			snap.gyro_lsb[axis] = sample->raw_gyro[axis];
			snap.acc_g[axis] = sample->accel_g[axis];
			snap.gyro_dps[axis] = sample->gyro_dps[axis];
		}

		/* OUT_TEMP: 256 LSB/degC with a +25 degC offset (DS13510 4.3, table 6). */
		snap.imu_temp_cc = (int16_t)(((float)sample->raw_temp / LGPS_IMU_TEMP_LSB_PER_C +
					      LGPS_IMU_TEMP_OFFSET_C) * 100.0F);
		snap.imu_temp_valid = true;
	}

	snap.gravity_valid = gravity_valid;
	if (gravity_valid) {
		for (uint8_t axis = 0U; axis < 3U; axis++) {
			snap.gravity_mg[axis] = (int16_t)(gravity_body_g[axis] * 1000.0F);
			snap.linear_mg[axis] = (int16_t)(linear_body_g[axis] * 1000.0F);
		}
	}

	ret = lgps_imu_read_hub(&mag, &baro);
	if (ret == 0) {
		snap.hub_valid = true;
		snap.mag_valid = mag.valid;
		snap.baro_valid = baro.valid;
		for (uint8_t axis = 0U; axis < 3U; axis++) {
			snap.mag_lsb[axis] = mag.raw[axis];
		}
		/* LIS2MDL TEMP_OUT: 8 LSB/degC with the +25 degC zero level. */
		snap.mag_temp_cc = (int16_t)(((float)mag.raw_temp / LGPS_LIS2MDL_TEMP_LSB_PER_C +
					      LGPS_LIS2MDL_TEMP_OFFSET_C) * 100.0F);
		snap.mag_temp_valid = mag.valid;
		snap.baro_press_raw = baro.raw_pressure;
		snap.baro_temp_raw = baro.raw_temperature;
		snap.baro_comp_valid = baro.compensated;

		if (baro.compensated) {
			/* BMP388 TPG output is already Pa and degC. Clamp to the part's
			 * specified range before the integer casts: a float outside int16
			 * range is undefined behaviour on Cortex-M, and anything outside the
			 * range is a fault the driver has already warned about.
			 */
			const float temp_c = CLAMP(baro.temperature_c,
						   LGPS_BMP388_TEMP_MIN_C,
						   LGPS_BMP388_TEMP_MAX_C);
			const float press_pa = CLAMP(baro.pressure_pa,
						     LGPS_BMP388_PRESS_MIN_PA,
						     LGPS_BMP388_PRESS_MAX_PA);

			snap.baro_temp_cc = (int16_t)(temp_c * 100.0F);
			snap.baro_press_pa = (int32_t)press_pa;
			snap.baro_temp_valid = true;
		}

		{
			float mag_ut[3];

			lgps_calib_feed_mag(mag.raw);

			for (uint8_t axis = 0U; axis < 3U; axis++) {
				mag_ut[axis] = (float)mag.raw[axis] * LGPS_LIS2MDL_UT_PER_LSB;
			}

			lgps_calib_apply_mag(mag_ut);

			lgps_nav_body_from_mag(mag_ut, hub_mag_body_ut);
			hub_mag_valid = true;
		}
	} else if (ret != -EAGAIN) {
		static uint32_t last_hub_warn_ms;

		/* The snapshot is now rebuilt at the BLE publication rate, so a dead
		 * sensor hub must not turn into ten warning lines per second.
		 */
		if ((uint32_t)(now_ms - last_hub_warn_ms) >= 10000U) {
			last_hub_warn_ms = now_ms;
			LOG_WRN("sensor hub read failed (%d)", ret);
		}
	}

	snap.bat_valid = bat_valid;
	snap.bat_mv = bat_mv;

	/* Barometric height: the module owns the baseline (BARO_BASELINE 0x42 or the
	 * GNSS gate), the snapshot only carries the resulting number.
	 */
	snap.baro_alt_valid = snap.baro_comp_valid && lgps_baro_baseline_set();
	snap.baro_rel_alt_mm = snap.baro_alt_valid ? lgps_baro_rel_alt_mm() : 0;

	/* Absolute altitude only exists after a GNSS height calibration (MSL
	 * reference); without it rel_alt_mm is all the barometer can claim.
	 */
	snap.baro_msl_valid = lgps_baro_msl_valid();
	snap.baro_msl_alt_mm = snap.baro_msl_valid ? lgps_baro_msl_altitude_mm() : 0;
	(void)lgps_charger_charging(&charging);
	snap.charging = charging;
	snap.key_levels = lgps_keys_levels();
	snap.zupt_locked = lgps_zupt_locked(&zupt);
	snap.zupt_var_g2 = lgps_zupt_accel_variance_g2(&zupt);
	snap.steps = pdr.steps_total;

	lgps_ring_get_stats(&ring);
	snap.ring_sector_seq = ring.sector_seq;
	snap.ring_records = ring.records_written;

	return &snap;
}

/*
 * Heading reference for the attitude filter.
 *
 * While the fix is moving, the GNSS course over ground is the only absolute yaw
 * that does not need calibration, so it wins and doubles as the calibration
 * source for the magnetometer offset. Once the fix is lost, the magnetometer
 * keeps the yaw bounded using that learned offset.
 */
static void update_heading_reference(const struct lgps_gnss_fix *fix)
{
	if ((fix->valid) && (fix->speed_cms >= HEADING_ALIGN_MIN_SPEED_CMS)) {
		lgps_attitude_align_heading(&attitude, (float)fix->heading_ddeg / 10.0F / RAD_TO_DEG);

		if (hub_mag_valid) {
			lgps_attitude_learn_mag_offset(&attitude, hub_mag_body_ut, 0.05F);
		}

		return;
	}

	if (hub_mag_valid) {
		lgps_attitude_correct_from_mag(&attitude, hub_mag_body_ut, 0.02F);
	}
}

/* Publish the current dead-reckoning position to the phone. */
static void publish_pdr(void)
{
	float heading_deg = lgps_attitude_heading_rad(&attitude) * RAD_TO_DEG;

	if (heading_deg < 0.0F) {
		heading_deg += 360.0F;
	}

	lgps_ble_publish_pdr((int32_t)(pdr.position_x_m * 1000.0F),
			     (int32_t)(pdr.position_y_m * 1000.0F),
			     (int16_t)(heading_deg * 10.0F), (uint16_t)pdr.steps_total);
}

/*
 * Publish the GNSS receive path at the 1 Hz telemetry cadence.
 *
 * Everything here is read from the snapshot the caller already built, so this
 * adds no bus traffic and no locking: the counters are atomic on the ISR side
 * and thread-written on the GNSS side (see gnss.h).
 */
static void publish_gnss(const struct lgps_telemetry_snapshot *snap)
{
	struct lgps_ble_telem_gnss gnss = {
		.state = (uint8_t)snap->gnss_state,
		.fix_type = snap->fix.fix_type,
		.satellites = snap->fix.satellites,
		.pdop_1e2 = snap->fix.pdop_1e2,
		.age_ms = snap->gnss_age_ms,
		.rx_bytes = snap->gnss_counters.rx_bytes,
		.ubx_frames = snap->gnss_counters.ubx_frames,
		.ubx_bad_checksum = snap->gnss_counters.ubx_bad_checksum,
		.nmea_starts = snap->gnss_counters.nmea_starts,
		.rx_dropped = snap->gnss_counters.rx_dropped,
		.baud = lgps_gnss_baud(),
	};
	struct lgps_gnss_ttff ttff;

	lgps_gnss_get_ttff(&ttff);
	gnss.ttff_ms = ttff.ttff_ms;
	gnss.power_off_ms = ttff.power_off_ms;

	if (snap->fix.valid) {
		gnss.flags |= LGPS_BLE_TELEM_GNSS_FLAG_FIX_VALID;
	}

	if (snap->fix.utc_valid) {
		gnss.flags |= LGPS_BLE_TELEM_GNSS_FLAG_UTC_VALID;
	}

	lgps_ble_publish_gnss(&gnss);
}

/* Barometer packet: the module owns pressure/height/baseline, the snapshot owns
 * the die temperature, lgps_ble_publish_baro() joins the two.
 */
static void publish_baro(const struct lgps_telemetry_snapshot *snap)
{
	lgps_ble_publish_baro(snap);
}

/* Satellite table (telemetry 0x09), 1 Hz while the debug session collects it. */
static void publish_sat(void)
{
	static struct lgps_ble_telem_sat_entry entries[LGPS_BLE_TELEM_SAT_MAX];
	struct lgps_gnss_sat sats[LGPS_GNSS_SAT_MAX];
	uint8_t count;

	count = lgps_gnss_get_satellites(sats, (uint8_t)ARRAY_SIZE(sats));
	if (count == 0U) {
		return;
	}

	for (uint8_t index = 0U; index < count; index++) {
		entries[index].gnss_id = sats[index].gnss_id;
		entries[index].sv_id = sats[index].sv_id;
		entries[index].cno = sats[index].cno;
		entries[index].elev = sats[index].elev;
		entries[index].state = (uint8_t)(sats[index].quality |
						 (sats[index].used ? 0x08U : 0U));
	}

	lgps_ble_publish_sat(entries, count, count);
}

/*
 * Hand queued NMEA sentences to the phone.
 *
 * A 1 Hz navigation epoch produces roughly 15 sentences once several talkers
 * emit GSV, so the drain runs on its own 50 ms cadence rather than inside the
 * 1 Hz status path; otherwise the ring would overflow between two publishes.
 */
static void pump_nmea(uint32_t now_ms)
{
	static char line[GNSS_NMEA_LINE_MAX_TEXT];
	static uint32_t last_ms;
	size_t len;

	if ((uint32_t)(now_ms - last_ms) < NMEA_PUMP_PERIOD_MS) {
		return;
	}

	last_ms = now_ms;

	for (uint8_t burst = 0U; burst < NMEA_PUMP_BURST; burst++) {
		len = lgps_gnss_pop_nmea(line, sizeof(line));
		if (len == 0U) {
			break;
		}

		lgps_ble_publish_nmea(line, (uint8_t)len);
	}
}

/*
 * One step detector update per IMU sample. Steps are only counted while the
 * device is moving (ZUPT unlocked); the detector itself additionally enforces a
 * 260 ms minimum interval.
 */
static void update_steps(const float accel_body[3], uint32_t now_ms)
{
	float accel_norm;
	float delta_accel_g = 0.0F;

	if (lgps_zupt_locked(&zupt)) {
		return;
	}

	accel_norm = sqrtf((accel_body[0] * accel_body[0]) + (accel_body[1] * accel_body[1]) +
			   (accel_body[2] * accel_body[2]));

	if (!lgps_step_update(&step_detector, accel_norm, now_ms, &delta_accel_g)) {
		return;
	}

	(void)lgps_pdr_step(&pdr, delta_accel_g, lgps_attitude_heading_rad(&attitude));
	publish_pdr();
}

int main(void)
{
	uint32_t last_telemetry_ms = 0U;
	uint32_t last_battery_ms = 0U;
	uint32_t last_ble_ms = 0U;
	uint32_t last_sample_ms = 0U;
	int32_t bat_mv = 0;
	bool bat_valid = false;
	bool imu_valid = false;
	struct lgps_imu_sample sample = { 0 };

	lgps_diag_banner();

	if (init_subsystems() < 0) {
		LOG_ERR("subsystem bring-up aborted, watchdog will reset the device");
	}

	for (;;) {
		uint32_t now_ms = lgps_diag_uptime_ms();
		int ret = lgps_imu_read_sample_if_ready(&sample);
		uint32_t ble_period_ms = lgps_ble_status_subscribed() ? BLE_STATUS_PERIOD_MS
								      : BLE_STATUS_IDLE_PERIOD_MS;
		bool ble_due = (uint32_t)(now_ms - last_ble_ms) >= ble_period_ms;
		bool telemetry_due = (uint32_t)(now_ms - last_telemetry_ms) >= TELEMETRY_PERIOD_MS;

		lgps_wdt_checkin(LGPS_WDT_TASK_MAIN);

		if (ret == 0) {
			float accel_body[3];
			float gyro_body[3];

			lgps_calib_apply_imu(&sample);
			lgps_calib_feed_imu(&sample, now_ms);

			lgps_nav_body_from_imu(sample.accel_g, accel_body);
			lgps_nav_body_from_imu(sample.gyro_dps, gyro_body);

			/* SFLP runs at the same 30 Hz as the register burst, so at most a
			 * couple of FIFO entries are waiting per sample.
			 */
			{
				struct lgps_imu_sflp sflp;

				if (lgps_imu_read_sflp(&sflp) == 0) {
					sflp_state = sflp;

					if (sflp.quat_valid && !sflp_quat_logged) {
						sflp_quat_logged = true;
						LOG_INF("SFLP quaternion (sensor frame "
							"w,x,y,z): %.3f %.3f %.3f %.3f",
							(double)sflp.quat[3], (double)sflp.quat[0],
							(double)sflp.quat[1],
							(double)sflp.quat[2]);
					}
				}
			}

			/* ZUPT expects rad/s, the driver reports deg/s. */
			const float gyro_rps[3] = {
				gyro_body[0] / 57.2957795F,
				gyro_body[1] / 57.2957795F,
				gyro_body[2] / 57.2957795F,
			};

			lgps_zupt_update(&zupt, accel_body, gyro_rps, now_ms);
			lgps_pdr_set_frozen(&pdr, lgps_zupt_locked(&zupt));
			imu_valid = true;

			/* Attitude: one filter step per IMU sample. The first usable
			 * sample seeds roll/pitch from gravity, so the estimate does not
			 * start a second of integration from an arbitrary orientation.
			 */
			if (!attitude.valid) {
				lgps_attitude_reset(&attitude, accel_body);
			}

			float dt_s = (float)(uint32_t)(now_ms - last_sample_ms) / 1000.0F;

			last_sample_ms = now_ms;
			lgps_attitude_update(&attitude, accel_body, gyro_rps, dt_s);

			/* Split the measurement into gravity and what is left over. */
			if (update_gravity_split(accel_body)) {
				gravity_valid = true;
			}

			if (lgps_zupt_locked(&zupt)) {
				lgps_attitude_accumulate_bias(&attitude, gyro_rps);
			}

			{
				struct lgps_gnss_fix fix;
				int16_t acc_body_lsb[3];
				int16_t gyro_body_lsb[3];
				int16_t quat_q15[4];

				lgps_gnss_get_fix(&fix);
				update_heading_reference(&fix);
				update_steps(accel_body, now_ms);

				imu_to_body_lsb(sample.raw_accel, acc_body_lsb);
				imu_to_body_lsb(sample.raw_gyro, gyro_body_lsb);
				lgps_attitude_quat_q15(&attitude, quat_q15);
				lgps_ble_publish_imu(acc_body_lsb, gyro_body_lsb, quat_q15);
			}
		} else if (ret != -EAGAIN) {
			LOG_ERR("IMU read failed (%d)", ret);
		}

		if ((uint32_t)(now_ms - last_battery_ms) >= BATTERY_PERIOD_MS) {
			int32_t millivolts = 0;

			int err;

			last_battery_ms = now_ms;
			err = lgps_battery_read_mv(&millivolts);
			bat_valid = (err == 0);
			if (bat_valid) {
				bat_mv = millivolts;
			} else {
				LOG_WRN("battery read failed (%d)", err);
			}
		}

		if (ble_due || telemetry_due) {
			struct lgps_telemetry_snapshot *snap =
				build_snapshot(now_ms, &sample, imu_valid, bat_mv, bat_valid);

			if (ble_due) {
				last_ble_ms = now_ms;
				lgps_ble_publish(snap);
			}

			if (telemetry_due) {
				last_telemetry_ms = now_ms;
				lgps_telemetry_emit(snap);

				/* Barometer, once per second: this cadence is what the automatic
				 * baseline gate counts (30 samples = 30 s), so it must not run in
				 * build_snapshot(), which also runs at the BLE rate.
				 */
				lgps_baro_note_utc(snap->fix.utc_epoch_s);
				(void)lgps_baro_feed(snap->baro_comp_valid,
						     (float)snap->baro_press_pa, &snap->fix);

				/* Position heartbeat, so the phone still knows where the
				 * device stands after a pause in walking.
				 */
				publish_pdr();
				publish_gnss(snap);
				publish_sat();
				publish_baro(snap);
			}
		}

		pump_nmea(now_ms);

		lgps_wdt_feed();

		k_msleep(MAIN_LOOP_PERIOD_MS);
	}

	return 0;
}
