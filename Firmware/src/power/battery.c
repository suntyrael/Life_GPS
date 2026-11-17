/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <lifegps/battery.h>

LOG_MODULE_REGISTER(lgps_battery, LOG_LEVEL_INF);

#define BATTERY_NODE DT_PATH(zephyr_user)

static const struct adc_dt_spec battery_channel = ADC_DT_SPEC_GET(BATTERY_NODE);

/* Divider denominator after the SAADC input resistance is taken into account. */
static int32_t bottom_effective_ohm(void)
{
	uint64_t product = (uint64_t)LGPS_BATT_R_BOTTOM_OHM * LGPS_BATT_SAADC_RIN_OHM;

	return (int32_t)(product / (LGPS_BATT_R_BOTTOM_OHM + LGPS_BATT_SAADC_RIN_OHM));
}

int lgps_battery_init(void)
{
	int ret;

	if (!adc_is_ready_dt(&battery_channel)) {
		LOG_ERR("SAADC not ready");
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&battery_channel);
	if (ret < 0) {
		LOG_ERR("SAADC channel setup failed (%d)", ret);
		return ret;
	}

	LOG_INF("BAT_ADC ready: AIN4 (P1.11), R25=%u ohm, R26=%u ohm, R_in=%u ohm, "
		"gain 1/2, internal reference",
		LGPS_BATT_R_TOP_OHM, LGPS_BATT_R_BOTTOM_OHM, LGPS_BATT_SAADC_RIN_OHM);

	return 0;
}

int lgps_battery_read_mv(int32_t *millivolts)
{
	uint16_t raw = 0U;
	int32_t vpin_mv;
	int32_t bottom_ohm;
	uint32_t vref_mv;
	int64_t scaled;
	struct adc_sequence sequence = { 0 };
	int ret;

	if (millivolts == NULL) {
		return -EINVAL;
	}

	ret = adc_sequence_init_dt(&battery_channel, &sequence);
	if (ret < 0) {
		LOG_ERR("SAADC sequence init failed (%d)", ret);
		return ret;
	}

	sequence.buffer = &raw;
	sequence.buffer_size = sizeof(raw);

	ret = adc_read_dt(&battery_channel, &sequence);
	if (ret < 0) {
		LOG_ERR("SAADC read failed (%d)", ret);
		return ret;
	}

	vref_mv = adc_ref_internal(battery_channel.dev);
	if (vref_mv == 0U) {
		LOG_ERR("SAADC internal reference unknown");
		return -EIO;
	}

	/*
	 * Gain is 1/2 (board devicetree), so the input range is 2 * Vref:
	 *   vpin = raw / (2^resolution - 1) * 2 * Vref
	 */
	scaled = (int64_t)raw * 2 * (int64_t)vref_mv;
	vpin_mv = (int32_t)(scaled / ((1 << battery_channel.resolution) - 1));

	bottom_ohm = bottom_effective_ohm();

	/* vbat = vpin * (R_top + R_bottom_eff) / R_bottom_eff */
	*millivolts = (int32_t)((int64_t)vpin_mv * (LGPS_BATT_R_TOP_OHM + bottom_ohm) / bottom_ohm);

	return 0;
}
