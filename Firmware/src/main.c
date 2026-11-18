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

static struct lgps_zupt zupt;
static struct lgps_pdr pdr;

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
	}

	lgps_zupt_reset(&zupt, lgps_diag_uptime_ms());
	lgps_pdr_init(&pdr);

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
	}

	ret = lgps_imu_read_hub(&mag, &baro);
	if (ret == 0) {
		snap.hub_valid = true;
		for (uint8_t axis = 0U; axis < 3U; axis++) {
			snap.mag_lsb[axis] = mag.raw[axis];
		}
		snap.baro_press_raw = baro.raw_pressure;
		snap.baro_temp_raw = baro.raw_temperature;
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

int main(void)
{
	uint32_t last_telemetry_ms = 0U;
	uint32_t last_battery_ms = 0U;
	uint32_t last_ble_ms = 0U;
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
			}
		}

		lgps_wdt_feed();

		k_msleep(MAIN_LOOP_PERIOD_MS);
	}

	return 0;
}
