/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

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

/**
 * Proof that reading a P1 pin through the GPIO driver actually follows the pad.
 * GPS_WAKE (P1.09) is used because the GNSS rail is still off at this point, so a
 * short pulse cannot reach the receiver.
 *
 * The read is done with gpio_pin_get_raw() on purpose: this test proves the
 * PAD -> register path only. It cannot prove the logical (inverted) view, because
 * that needs a drivable pin declared GPIO_ACTIVE_LOW and this board has none - the
 * old version drove an ACTIVE_HIGH pin and therefore could never have caught the
 * inversion bug that broke the keys and the charger status. Code that needs a
 * physical level uses gpio_pin_get_raw() for exactly that reason.
 */
static int p1_read_path_self_test(void)
{
	int high;
	int low;
	int ret;

	ret = gpio_pin_configure_dt(&gnss_wake, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("P1 read-path self-test: drive high failed (%d)", ret);
		return ret;
	}

	high = gpio_pin_get_raw(gnss_wake.port, gnss_wake.pin);

	ret = gpio_pin_set_dt(&gnss_wake, 0);
	if (ret < 0) {
		LOG_ERR("P1 read-path self-test: drive low failed (%d)", ret);
		return ret;
	}

	low = gpio_pin_get_raw(gnss_wake.port, gnss_wake.pin);

	if ((high == 1) && (low == 0)) {
		LOG_INF("P1 read path OK (GPS_WAKE driven 1 -> read 1, driven 0 -> read 0)");
	} else {
		LOG_ERR("P1 read path broken: driven 1 -> read %d, driven 0 -> read %d "
			"(GPIO readback does not follow the pad)", high, low);
	}

	(void)gpio_pin_configure_dt(&gnss_wake, GPIO_OUTPUT_INACTIVE);

	return 0;
}

/*
 * Chip selects.
 *
 * Timing fact: the SPI drivers own both CS pins (devicetree cs-gpios) and configure
 * them during driver initialisation, i.e. BEFORE main() runs. The comment that used
 * to sit here claimed board_io drives them high "first"; it does not - it runs
 * later. What is genuinely added here is the internal pull-up, which the cs-gpios
 * flags do not request (neither memory has an external pull-up, so an undriven CS
 * would float while the NOR/IMU are powered). The pins stay inactive (high, because
 * GPIO_ACTIVE_LOW), so neither device is selected outside a transfer.
 */
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

	LOG_INF("chip selects: Flash /CS=P2.05 IMU /CS=P1.14 (INACTIVE=high) + internal pull-up; "
		"the SPI drivers configured these pins earlier, this only adds the pull-up");

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

	(void)p1_read_path_self_test();

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

	/*
	 * Read the PHYSICAL level on purpose. CHG_STAT is declared GPIO_ACTIVE_LOW,
	 * and gpio_pin_get_dt() returns the *logical* level: it XORs the port value
	 * with the invert mask built from GPIO_ACTIVE_LOW (gpio.h, z_impl_gpio_pin_get()).
	 * Using it here reported "not charging" while charging and vice versa - the
	 * same inverted-reading trap that produced the key behaviour in M1.
	 * SGM4056 /CHG is an open-drain output: pad low == charging in progress.
	 */
	value = gpio_pin_get_raw(chg_stat.port, chg_stat.pin);
	if (value < 0) {
		LOG_ERR("CHG_STAT read failed (%d)", value);
		return value;
	}

	*charging = (value == 0);

	return 0;
}
