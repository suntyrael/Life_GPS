/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <lifegps/wdt.h>

LOG_MODULE_REGISTER(lgps_wdt, LOG_LEVEL_INF);

#define ALL_TASKS_MASK ((uint32_t)BIT(LGPS_WDT_TASK_COUNT) - 1U)

static const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt30));
static atomic_t checkin_mask;
static int wdt_channel = -1;
static uint32_t next_evaluation_ms;

static const char *const task_names[LGPS_WDT_TASK_COUNT] = {
	[LGPS_WDT_TASK_MAIN] = "main",
	[LGPS_WDT_TASK_GNSS] = "gnss",
	[LGPS_WDT_TASK_IMU] = "imu",
};

int lgps_wdt_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_SOC,
		.window = {
			.min = 0U,
			.max = LGPS_WDT_TIMEOUT_MS,
		},
		.callback = NULL,
	};
	int ret;

	if (!device_is_ready(wdt)) {
		LOG_ERR("WDT30 not ready");
		return -ENODEV;
	}

	atomic_clear(&checkin_mask);

	wdt_channel = wdt_install_timeout(wdt, &cfg);
	if (wdt_channel < 0) {
		LOG_ERR("wdt_install_timeout failed (%d)", wdt_channel);
		return wdt_channel;
	}

	ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		LOG_ERR("wdt_setup failed (%d)", ret);
		return ret;
	}

	/* Do not judge the first window: the GNSS/IMU producers may not have started. */
	next_evaluation_ms = k_uptime_get_32() + LGPS_WDT_FEED_WINDOW_MS;

	LOG_INF("WDT30 armed: timeout %u ms, %u supervised tasks, %u ms supervision window "
		"(layered feed)", LGPS_WDT_TIMEOUT_MS, (unsigned int)LGPS_WDT_TASK_COUNT,
		LGPS_WDT_FEED_WINDOW_MS);

	return 0;
}

void lgps_wdt_checkin(enum lgps_wdt_task task)
{
	if ((unsigned int)task >= LGPS_WDT_TASK_COUNT) {
		return;
	}

	atomic_or(&checkin_mask, (atomic_val_t)BIT(task));
}

uint32_t lgps_wdt_missing(void)
{
	return ALL_TASKS_MASK & ~(uint32_t)atomic_get(&checkin_mask);
}

void lgps_wdt_feed(void)
{
	uint32_t now_ms = k_uptime_get_32();
	uint32_t missing;
	int ret;

	/* Signed difference: correct across the 49 day tick wrap. */
	if ((int32_t)(now_ms - next_evaluation_ms) < 0) {
		return;
	}

	next_evaluation_ms = now_ms + LGPS_WDT_FEED_WINDOW_MS;

	missing = lgps_wdt_missing();
	if (missing != 0U) {
		LOG_ERR("WDT feed withheld: tasks missing in this %u ms window: 0x%02x",
			LGPS_WDT_FEED_WINDOW_MS, missing);
		return;
	}

	if (wdt_channel < 0) {
		return;
	}

	ret = wdt_feed(wdt, wdt_channel);
	if (ret < 0) {
		LOG_ERR("wdt_feed failed (%d)", ret);
		return;
	}

	atomic_clear(&checkin_mask);
}
