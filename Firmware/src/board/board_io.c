/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lifegps/board_io.h>

LOG_MODULE_REGISTER(lgps_board_io, LOG_LEVEL_INF);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec gnss_en = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, gnss_en_gpios);
static const struct gpio_dt_spec gnss_wake = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, gnss_wake_gpios);
static const struct gpio_dt_spec chg_en = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, chg_en_gpios);
static const struct gpio_dt_spec chg_stat = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, chg_stat_gpios);
static const struct gpio_dt_spec flash_cs = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, flash_cs_gpios);
static const struct gpio_dt_spec imu_cs = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, imu_cs_gpios);

/* Unused nRF54L15 pins on Main_V0.2 (pinmap section 2, "未使用引脚"). */
struct lgps_parked_pin {
	const struct device *port;
	gpio_pin_t pin;
	const char *net;
};

#define LGPS_GPIO2_NODE DT_NODELABEL(gpio2)

static const struct lgps_parked_pin parked_pins[] = {
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 0U, "P2.00" },
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 3U, "P2.03" },
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 6U, "P2.06" },
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 8U, "P2.08" },
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 9U, "P2.09" },
	{ DEVICE_DT_GET(LGPS_GPIO2_NODE), 10U, "P2.10" },
};

static int configure_output(const struct gpio_dt_spec *spec, bool active, const char *name,
			    const char *pin_label)
{
	int ret;

	if (!gpio_is_ready_dt(spec)) {
		LOG_ERR("%s: GPIO controller not ready", name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("%s: configure failed (%d)", name, ret);
		return ret;
	}

	if (active) {
		ret = gpio_pin_set_dt(spec, 1);
		if (ret < 0) {
			LOG_ERR("%s: set active failed (%d)", name, ret);
			return ret;
		}
	}

	LOG_INF("%s: %s -> output, active=%d", name, pin_label, active);

	return 0;
}

/* Chip selects first: neither the NOR nor the IMU has an external pull-up. */
static int configure_chip_selects(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&flash_cs, GPIO_OUTPUT_INACTIVE | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("Flash /CS (P2.05) configure failed (%d)", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&imu_cs, GPIO_OUTPUT_INACTIVE | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("IMU /CS (P1.14) configure failed (%d)", ret);
		return ret;
	}

	LOG_INF("chip selects: Flash /CS=P2.05 IMU /CS=P1.14 driven high with internal pull-up");

	return 0;
}

int lgps_board_io_init(void)
{
	int ret;

	ret = configure_chip_selects();
	if (ret < 0) {
		return ret;
	}

	/* GNSS rail off until the state machine asks for it. */
	ret = configure_output(&gnss_en, false, "GPS_EN", "P1.07");
	if (ret < 0) {
		return ret;
	}

	ret = configure_output(&gnss_wake, false, "GPS_WAKE", "P1.09");
	if (ret < 0) {
		return ret;
	}

	/*
	 * SGM4056 EN is active low: driving it low allows charging, which is the
	 * state a battery powered device wants whenever a charger is attached.
	 */
	ret = configure_output(&chg_en, true, "CHG_EN", "P0.03");
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&chg_stat, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("CHG_STAT (P0.04) configure failed (%d)", ret);
		return ret;
	}

	LOG_INF("board IO ready: GPS_EN=P1.07(low) GPS_WAKE=P1.09(low) CHG_EN=P0.03(low,charging allowed) "
		"CHG_STAT=P0.04(input,pull-up)");

	return 0;
}

int lgps_board_io_unused_pins_off(void)
{
	int ret;

	ARRAY_FOR_EACH(parked_pins, i) {
		const struct lgps_parked_pin *p = &parked_pins[i];

		ret = gpio_pin_configure(p->port, p->pin, GPIO_DISCONNECTED);
		if (ret < 0) {
			LOG_ERR("%s: disconnect failed (%d)", p->net, ret);
			return ret;
		}
	}

	LOG_INF("parked unused pins: P2.00 P2.03 P2.06 P2.08 P2.09 P2.10 (input, buffer disconnected)");

	return 0;
}

int lgps_gnss_rail_set(bool on)
{
	int ret = gpio_pin_set_dt(&gnss_en, on ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("GPS_EN write failed (%d)", ret);
		return ret;
	}

	LOG_INF("GNSS 1V8 rail -> %s", on ? "on" : "off");

	return 0;
}

int lgps_gnss_wake_pulse(uint32_t width_ms)
{
	int ret;

	ret = gpio_pin_set_dt(&gnss_wake, 1);
	if (ret < 0) {
		LOG_ERR("GPS_WAKE set failed (%d)", ret);
		return ret;
	}

	k_msleep((int32_t)width_ms);

	ret = gpio_pin_set_dt(&gnss_wake, 0);
	if (ret < 0) {
		LOG_ERR("GPS_WAKE clear failed (%d)", ret);
		return ret;
	}

	LOG_DBG("GPS_WAKE pulse %u ms", width_ms);

	return 0;
}

int lgps_charger_charging(bool *charging)
{
	int value;

	if (charging == NULL) {
		return -EINVAL;
	}

	value = gpio_pin_get_dt(&chg_stat);
	if (value < 0) {
		LOG_ERR("CHG_STAT read failed (%d)", value);
		return value;
	}

	/* Active low: pin low == charging. */
	*charging = (value == 0);

	return 0;
}
