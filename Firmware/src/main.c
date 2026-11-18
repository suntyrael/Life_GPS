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

#include <lifegps/battery.h>
#include <lifegps/board_io.h>
#include <lifegps/diag.h>
#include <lifegps/flash_ring.h>
#include <lifegps/gnss.h>
#include <lifegps/imu.h>
#include <lifegps/led.h>
#include <lifegps/nav.h>
#include <lifegps/power_sm.h>
#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_main, LOG_LEVEL_INF);

/* Poll faster than the 26 Hz ODR and let the data-ready bit do the pacing. */
#define MAIN_LOOP_PERIOD_MS 10U
#define STATUS_PERIOD_MS 1000U
#define BATTERY_PERIOD_MS 60000U

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

	return 0;
}

static void publish_status(uint32_t now_ms, const struct lgps_imu_sample *sample)
{
	struct lgps_gnss_fix fix;
	struct lgps_ring_stats ring;
	bool charging = false;

	lgps_gnss_get_fix(&fix);
	lgps_ring_get_stats(&ring);
	(void)lgps_charger_charging(&charging);

	LOG_INF("status: uptime=%u ms gnss=%s fix=%u sats=%u batt_ok=%s charging=%d "
		"ring_seq=%u ring_records=%u zupt=%s locked steps=%u",
		now_ms, lgps_gnss_state_name(lgps_gnss_get_state()), fix.fix_type, fix.satellites,
		fix.valid ? "yes" : "no", charging ? 1 : 0, ring.sector_seq, ring.records_written,
		lgps_zupt_locked(&zupt) ? "stationary" : "moving", pdr.steps_total);

	LOG_INF("imu: acc=[%.3f %.3f %.3f] g gyro=[%.2f %.2f %.2f] dps var=%.5f g^2",
		(double)sample->accel_g[0], (double)sample->accel_g[1], (double)sample->accel_g[2],
		(double)sample->gyro_dps[0], (double)sample->gyro_dps[1],
		(double)sample->gyro_dps[2], (double)lgps_zupt_accel_variance_g2(&zupt));
}

int main(void)
{
	uint32_t last_status_ms = 0U;
	uint32_t last_battery_ms = 0U;
	struct lgps_imu_sample sample = { 0 };

	lgps_diag_banner();

	if (init_subsystems() < 0) {
		LOG_ERR("subsystem bring-up aborted, watchdog will reset the device");
	}

	for (;;) {
		uint32_t now_ms = lgps_diag_uptime_ms();
		int ret = lgps_imu_read_sample_if_ready(&sample);

		lgps_wdt_checkin(LGPS_WDT_TASK_MAIN);

		if (ret == 0) {
			float accel_body[3];
			float gyro_body[3];

			lgps_wdt_checkin(LGPS_WDT_TASK_IMU);

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
		} else if (ret != -EAGAIN) {
			LOG_ERR("IMU read failed (%d)", ret);
		}

		if ((uint32_t)(now_ms - last_status_ms) >= STATUS_PERIOD_MS) {
			last_status_ms = now_ms;
			publish_status(now_ms, &sample);
		}

		if ((uint32_t)(now_ms - last_battery_ms) >= BATTERY_PERIOD_MS) {
			int32_t millivolts = 0;

			last_battery_ms = now_ms;

			ret = lgps_battery_read_mv(&millivolts);
			if (ret == 0) {
				LOG_INF("battery: %d mV (divider corrected)", millivolts);
			} else {
				LOG_ERR("battery read failed (%d)", ret);
			}
		}

		lgps_wdt_feed();

		k_msleep(MAIN_LOOP_PERIOD_MS);
	}

	return 0;
}
