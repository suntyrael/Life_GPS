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

	LOG_INF("BAT_ADC ready: AIN4 (P1.11), divider calibrated pack:tap = %u:%u uV, "
		"gain/reference/resolution from devicetree",
		(uint32_t)LGPS_BATT_CAL_PACK_UV, (uint32_t)LGPS_BATT_CAL_TAP_UV);

	return 0;
}

int lgps_battery_read_mv(int32_t *millivolts)
{
	uint16_t raw = 0U;
	int32_t pin_mv;
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

	/*
	 * Let the ADC helper apply gain, reference and resolution from the devicetree
	 * channel (zephyr,gain = ADC_GAIN_1_2, zephyr,reference = ADC_REF_INTERNAL,
	 * zephyr,resolution = 12). The previous code hard-coded the 1/2 gain as "* 2"
	 * and divided by (1 << resolution): an edited devicetree gain silently produced
	 * a wrong pack voltage, and resolution == 0 divided by zero.
	 */
	pin_mv = (int32_t)raw;

	ret = adc_raw_to_millivolts_dt(&battery_channel, &pin_mv);
	if (ret < 0) {
		LOG_ERR("adc_raw_to_millivolts_dt failed (%d)", ret);
		return ret;
	}

	/*
	 * vbat = vpin * pack calibration / tap calibration, i.e. the measured divider
	 * ratio instead of the R25/R26/SAADC-R_in model. The product is taken in 64
	 * bits: a 1.8 V full-scale pin reading would overflow 32 bits.
	 */
	*millivolts = (int32_t)((int64_t)pin_mv * (int64_t)LGPS_BATT_CAL_PACK_UV /
				(int64_t)LGPS_BATT_CAL_TAP_UV);

	/*
	 * Raw numbers in the log: a wrong gain / reference / pin shows up here as a
	 * mismatch between raw and the expected divider voltage, and a missing pack
	 * shows up as a plausible raw with ~0 V at the pin.
	 */
	LOG_DBG("battery: raw=%u pin=%d mV -> vbat=%d mV", raw, pin_mv, *millivolts);

	if (*millivolts < LGPS_BATT_MV_PLAUSIBLE_MIN) {
		static bool warned;

		if (!warned) {
			warned = true;
			LOG_INF("battery: %d mV (raw=%u) is below any Li-ion pack - expected on a "
				"build without the divider/pack fitted, so treat bat_mv as invalid",
				*millivolts, raw);
		}
	}

	return 0;
}
