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
#include <lifegps/ble.h>
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
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_main, LOG_LEVEL_INF);

/* Poll faster than the 26 Hz ODR and let the data-ready bit do the pacing. */
#define MAIN_LOOP_PERIOD_MS 10U
/* The five-line text frame on DEBUG_UART keeps its 1 Hz cadence: it is the
 * scope-free instrument for bring-up and changing it would invalidate every
 * earlier capture.
 */
#define TELEMETRY_PERIOD_MS 1000U
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
 * computed here from the attitude estimate and published with the snapshot.
 */
static float gravity_body_g[3];
static float linear_body_g[3];
static bool gravity_valid;

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

		/* OUT_TEMP: 256 LSB/degC with a +25 degC offset (DS11976 9.30). */
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
		/* LIS2MDL TEMP_OUT: 8 LSB/degC, no offset (DS12144 Rev 6 8.16). */
		snap.mag_temp_cc =
			(int16_t)(((float)mag.raw_temp / LGPS_LIS2MDL_TEMP_LSB_PER_C) * 100.0F);
		snap.mag_temp_valid = mag.valid;
		snap.baro_press_raw = baro.raw_pressure;
		snap.baro_temp_raw = baro.raw_temperature;

		{
			float mag_ut[3];

			for (uint8_t axis = 0U; axis < 3U; axis++) {
				mag_ut[axis] = (float)mag.raw[axis] * LGPS_LIS2MDL_UT_PER_LSB;
			}

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

			lgps_nav_body_from_imu(sample.accel_g, accel_body);
			lgps_nav_body_from_imu(sample.gyro_dps, gyro_body);

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

			/* Split the measurement into gravity and what is left over. The
			 * estimate is only trusted once the filter is live.
			 */
			if (attitude.valid) {
				lgps_attitude_gravity_body(&attitude, gravity_body_g);
				for (uint8_t axis = 0U; axis < 3U; axis++) {
					linear_body_g[axis] =
						accel_body[axis] - gravity_body_g[axis];
				}
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

				/* Position heartbeat, so the phone still knows where the
				 * device stands after a pause in walking.
				 */
				publish_pdr();
			}
		}

		lgps_wdt_feed();

		k_msleep(MAIN_LOOP_PERIOD_MS);
	}

	return 0;
}
