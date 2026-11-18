/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
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

/*
 * Render the not-checked-in tasks as "main,gnss".
 *
 * snprintk() returns the length the string *would* have had, not the number of
 * bytes stored (printk.c: the return value is the would-be length), so adding it
 * to the write cursor can push @p used past the end of the buffer. Subtracting
 * such a cursor from sizeof(buffer) then underflows size_t into a huge value and
 * the next call writes past the array. Clamp on truncation instead.
 */
static size_t format_missing(char *names, size_t names_size, uint32_t missing)
{
	size_t used = 0U;

	names[0] = '\0';

	for (uint8_t id = 0U; id < LGPS_WDT_TASK_COUNT; id++) {
		int written;
		size_t room;

		if ((missing & BIT(id)) == 0U) {
			continue;
		}

		room = names_size - used;
		written = snprintk(&names[used], room, "%s%s", (used == 0U) ? "" : ",",
				   task_names[id]);

		if (written < 0) {
			break;
		}

		if ((size_t)written >= room) {
			/* Truncated: the buffer is full, keep what fits. */
			return names_size - 1U;
		}

		used += (size_t)written;
	}

	return used;
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

	/*
	 * Every window consumes the mask, whether or not the pet is granted.
	 * Clearing it only after a successful pet let a dead task's stale bit from an
	 * earlier window satisfy later windows, which contradicts the documented
	 * contract: "a dead task cannot satisfy the next window". atomic_clear()
	 * returns the previous value, so this is an atomic test-and-clear and no
	 * check-in can be lost between reading and clearing.
	 */
	missing = ALL_TASKS_MASK & ~(uint32_t)atomic_clear(&checkin_mask);
	if (missing != 0U) {
		char names[48];

		(void)format_missing(names, sizeof(names), missing);

		LOG_ERR("WDT feed withheld: %s did not check in during the last %u ms window",
			names, LGPS_WDT_FEED_WINDOW_MS);
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
}
