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
 * 26 Hz ODR with no sleep-drift. 26 samples therefore span exactly 1.000 s, the
 * window README 7.5 specifies. The same loop feeds the watchdog and publishes a
 * 1 Hz status line.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <math.h>
#include <string.h>

#include <lifegps/battery.h>
#include <lifegps/board_io.h>
#include <lifegps/diag.h>
#include <lifegps/flash_ring.h>
#include <lifegps/gnss.h>
#include <lifegps/imu.h>
#include <lifegps/keys.h>
#include <lifegps/led.h>
#include <lifegps/nav.h>
#include <lifegps/power_sm.h>
#include <lifegps/telemetry.h>
#include <lifegps/traj.h>
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_main, LOG_LEVEL_INF);

/* Poll faster than the 26 Hz ODR and let the data-ready bit do the pacing. */
#define MAIN_LOOP_PERIOD_MS 10U
#define TELEMETRY_PERIOD_MS 1000U
#define BATTERY_PERIOD_MS 10000U
#define PDR_RECORD_PERIOD_MS 10000U
#define PDR_MIN_STEPS_DELTA 4U
#define DEG_PER_RAD 57.2957795F
#define MAG_UT_PER_LSB 0.15F
#define LAT_LON_E7_PER_METRE 90.0F

static struct lgps_zupt zupt;
static struct lgps_pdr pdr;
static struct lgps_step_detector step_det;
static bool ring_ready;
static uint32_t next_seq_id;
static uint32_t last_recorded_pvt_ms;
static uint32_t last_recorded_steps;
static uint32_t last_pdr_record_ms;
static int32_t anchor_lat_e7;
static int32_t anchor_lon_e7;
static int16_t anchor_alt_m;
static uint16_t last_heading_ddeg;
static float mag_ref_ut;
static bool mag_anomaly_active;

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
			LOG_INF("battery: %d mV at boot (R25/R26 || R_in corrected)", millivolts);
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

	ret = lgps_imu_hub_configure();
	if (ret < 0) {
		LOG_ERR("sensor hub streaming not configured (%d)", ret);
	}

	ret = lgps_ring_init();
	if (ret < 0) {
		LOG_ERR("external flash ring unavailable (%d)", ret);
		ring_ready = false;
	} else {
		struct lgps_ring_stats stats;

		lgps_ring_get_stats(&stats);
		next_seq_id = stats.records_written;
		ring_ready = true;
	}

	lgps_zupt_reset(&zupt, lgps_diag_uptime_ms());
	lgps_pdr_init(&pdr);
	lgps_step_detector_reset(&step_det);

	return 0;
}

static int16_t clamp_alt_m(int32_t hmsl_mm)
{
	int32_t alt_m = hmsl_mm / 1000;

	if (alt_m > 32767) {
		return 32767;
	}
	if (alt_m < -32768) {
		return -32768;
	}

	return (int16_t)alt_m;
}

static void record_trajectory_point(uint32_t now_ms, const struct lgps_gnss_fix *fix,
				    bool is_poi, bool is_pdr_only)
{
	struct lgps_traj_sample traj_sample;
	uint8_t raw_record[LGPS_RECORD_TRAJECTORY_SIZE];
	uint32_t pdop_d1;
	uint8_t flags = 0U;
	int ret;

	if (!ring_ready || (fix == NULL)) {
		return;
	}

	memset(&traj_sample, 0, sizeof(traj_sample));
	traj_sample.seq_id = next_seq_id;
	traj_sample.timestamp_s = fix->utc_valid ? fix->utc_epoch_s : (now_ms / 1000U);
	traj_sample.subsecond_ms = (uint16_t)(now_ms % 1000U);

	if (fix->valid && !is_pdr_only) {
		anchor_lat_e7 = fix->lat_e7;
		anchor_lon_e7 = fix->lon_e7;
		anchor_alt_m = clamp_alt_m(fix->hmsl_mm);
		last_heading_ddeg = (uint16_t)(fix->heading_ddeg % 3600U);
		pdr.position_x_m = 0.0F;
		pdr.position_y_m = 0.0F;

		traj_sample.lat_e7 = anchor_lat_e7;
		traj_sample.lon_e7 = anchor_lon_e7;
		traj_sample.alt_m = anchor_alt_m;
		traj_sample.speed_cms = fix->speed_cms;
		traj_sample.heading_ddeg = last_heading_ddeg;
		flags |= LGPS_FLAG_FIX_VALID;
	} else {
		int32_t dlat_e7 = (int32_t)(pdr.position_y_m * LAT_LON_E7_PER_METRE);
		int32_t dlon_e7 = (int32_t)(pdr.position_x_m * LAT_LON_E7_PER_METRE);

		traj_sample.lat_e7 = anchor_lat_e7 + dlat_e7;
		traj_sample.lon_e7 = anchor_lon_e7 + dlon_e7;
		traj_sample.alt_m = anchor_alt_m;
		traj_sample.speed_cms = 0U;
		traj_sample.heading_ddeg = (uint16_t)(last_heading_ddeg % 3600U);
		flags |= LGPS_FLAG_NAV_SRC;
	}

	if (is_poi) {
		flags |= LGPS_FLAG_POI_EVENT;
	}
	if (lgps_zupt_locked(&zupt)) {
		flags |= LGPS_FLAG_STN_LOCK;
	}
	if (mag_anomaly_active) {
		flags |= LGPS_FLAG_MAG_ANOMALY;
	}

	pdop_d1 = (uint32_t)fix->pdop_1e2 / 10U;
	traj_sample.step_count = (uint16_t)(pdr.steps_total & 0xFFFFU);
	traj_sample.hdop_d1 = (pdop_d1 > 255U) ? 255U : (uint8_t)pdop_d1;
	traj_sample.satellites = fix->satellites;
	traj_sample.status_flags = flags;
	traj_sample.battery_pct = 0U;
	traj_sample.floor_level = 0;

	ret = lgps_traj_encode(&traj_sample, raw_record);
	if (ret < 0) {
		LOG_ERR("trajectory encode failed (%d)", ret);
		return;
	}

	ret = lgps_ring_append(raw_record);
	if (ret < 0) {
		LOG_ERR("flash ring append failed (%d)", ret);
		return;
	}

	next_seq_id++;
}

static void emit_telemetry(uint32_t now_ms, const struct lgps_imu_sample *sample,
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
	}

	ret = lgps_imu_read_hub(&mag, &baro);
	if (ret == 0) {
		float mag_sensor_ut[3];
		float mag_body_ut[3];
		float mag_norm_ut;

		snap.hub_valid = true;
		for (uint8_t axis = 0U; axis < 3U; axis++) {
			snap.mag_lsb[axis] = mag.raw[axis];
			mag_sensor_ut[axis] = (float)mag.raw[axis] * MAG_UT_PER_LSB;
		}
		lgps_nav_body_from_mag(mag_sensor_ut, mag_body_ut);
		mag_norm_ut = sqrtf((mag_body_ut[0] * mag_body_ut[0]) +
				    (mag_body_ut[1] * mag_body_ut[1]) +
				    (mag_body_ut[2] * mag_body_ut[2]));
		if ((mag_ref_ut <= 0.0F) && (mag_norm_ut >= 15.0F) && (mag_norm_ut <= 80.0F)) {
			mag_ref_ut = mag_norm_ut;
		}
		mag_anomaly_active = lgps_nav_mag_anomaly(mag_norm_ut, mag_ref_ut);

		snap.baro_press_raw = baro.raw_pressure;
		snap.baro_temp_raw = baro.raw_temperature;
	} else if (ret != -EAGAIN) {
		LOG_WRN("sensor hub read failed (%d)", ret);
	} else {
		/* Sensor hub not yet configured; keep previous anomaly state. */
	}

	snap.bat_valid = bat_valid;
	snap.bat_mv = bat_mv;
	(void)lgps_charger_charging(&charging);
	snap.charging = charging;
	snap.key_levels = lgps_keys_levels();
	snap.zupt_locked = lgps_zupt_locked(&zupt);
	snap.zupt_var_g2 = lgps_zupt_accel_variance_g2(&zupt);
	snap.steps = pdr.steps_total;

	lgps_ring_get_stats(&ring);
	snap.ring_sector_seq = ring.sector_seq;
	snap.ring_records = ring.records_written;

	lgps_telemetry_emit(&snap);
}

int main(void)
{
	uint32_t last_telemetry_ms = 0U;
	uint32_t last_battery_ms = 0U;
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
		struct lgps_gnss_fix fix;
		int ret = lgps_imu_read_sample_if_ready(&sample);

		lgps_wdt_checkin(LGPS_WDT_TASK_MAIN);
		lgps_gnss_set_continuous(lgps_power_precision_requested());

		if (ret == 0) {
			float accel_body[3];
			float gyro_body[3];
			float delta_accel_g = 0.0F;
			bool locked;

			lgps_nav_body_from_imu(sample.accel_g, accel_body);
			lgps_nav_body_from_imu(sample.gyro_dps, gyro_body);

			/* ZUPT expects rad/s, the driver reports deg/s. */
			const float gyro_rps[3] = {
				gyro_body[0] / DEG_PER_RAD,
				gyro_body[1] / DEG_PER_RAD,
				gyro_body[2] / DEG_PER_RAD,
			};

			lgps_zupt_update(&zupt, accel_body, gyro_rps, now_ms);
			locked = lgps_zupt_locked(&zupt);
			lgps_pdr_set_frozen(&pdr, locked);
			lgps_gnss_set_stationary(locked);

			if (lgps_step_detector_update(&step_det, accel_body, now_ms,
						      &delta_accel_g)) {
				float heading_rad =
					((float)last_heading_ddeg * 0.1F) / DEG_PER_RAD;

				(void)lgps_pdr_step(&pdr, delta_accel_g, heading_rad);
			}
			imu_valid = true;
		} else if (ret != -EAGAIN) {
			LOG_ERR("IMU read failed (%d)", ret);
		} else {
			/* No fresh IMU sample on this 10 ms tick. */
		}

		lgps_gnss_get_fix(&fix);

		if (lgps_power_consume_poi_request()) {
			if (!fix.valid) {
				lgps_gnss_request_immediate();
			}
			record_trajectory_point(now_ms, &fix, true, !fix.valid);
		}

		if (fix.valid && (fix.last_pvt_uptime_ms != 0U) &&
		    (fix.last_pvt_uptime_ms != last_recorded_pvt_ms)) {
			if (last_recorded_pvt_ms == 0U) {
				(void)lgps_led_play(LGPS_LED_PATTERN_FIX);
			}
			last_recorded_pvt_ms = fix.last_pvt_uptime_ms;
			last_recorded_steps = pdr.steps_total;
			record_trajectory_point(now_ms, &fix, false, false);
		} else if (!fix.valid &&
			   ((uint32_t)(now_ms - last_pdr_record_ms) >= PDR_RECORD_PERIOD_MS) &&
			   ((pdr.steps_total - last_recorded_steps) >= PDR_MIN_STEPS_DELTA)) {
			last_pdr_record_ms = now_ms;
			last_recorded_steps = pdr.steps_total;
			record_trajectory_point(now_ms, &fix, false, true);
			(void)lgps_led_play(LGPS_LED_PATTERN_PDR);
		} else {
			/* Neither a new GNSS fix nor a PDR checkpoint arrived on this tick. */
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

		if ((uint32_t)(now_ms - last_telemetry_ms) >= TELEMETRY_PERIOD_MS) {
			last_telemetry_ms = now_ms;
			emit_telemetry(now_ms, &sample, imu_valid, bat_mv, bat_valid);
		}

		lgps_wdt_feed();

		k_msleep(MAIN_LOOP_PERIOD_MS);
	}

	return 0;
}
