/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Life_GPS contributors
 *
 * BMP388 TPG compensation. See lifegps/bmp388.h for the datasheet references
 * (BST-BMP388-DS001-07 sections 3.10, 3.11.1, 9.1, 9.2 and 9.3).
 *
 * No Zephyr, no libm, no I/O: this file must stay pure so the maths can be
 * tested on native_sim, and so it does not depend on which libc the image links.
 * Every scaling factor of section 9.1 is a power of two, so the constants below
 * are exact float literals: a divide by them rounds exactly like ldexpf() would.
 */

#include <lifegps/bmp388.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

/* Datasheet section 9.1 divisors: NVM_PAR_X / 2^n. */
#define DATASHEET_2P6 64.0F
#define DATASHEET_2P8 256.0F
#define DATASHEET_2P15 32768.0F
#define DATASHEET_2P20 1048576.0F
#define DATASHEET_2P29 536870912.0F
#define DATASHEET_2P30 1073741824.0F
#define DATASHEET_2P32 4294967296.0F
#define DATASHEET_2P37 137438953472.0F
#define DATASHEET_2P48 281474976710656.0F
#define DATASHEET_2P65 36893488147419103232.0F

/* Two coefficients are divided by a negative power of two, i.e. multiplied. */
#define DATASHEET_MUL_2P8 256.0F /* NVM_T1 / 2^-8 */
#define DATASHEET_MUL_2P3 8.0F   /* NVM_P5 / 2^-3 */

/* Little-endian pairs, as laid out in table 23 (LSB at the lower address). */
static uint16_t nvm_u16(const uint8_t *raw)
{
	return (uint16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

static int16_t nvm_s16(const uint8_t *raw)
{
	return (int16_t)nvm_u16(raw);
}

static int8_t nvm_s8(const uint8_t *raw)
{
	return (int8_t)raw[0];
}

int lgps_bmp388_calib_decode(const uint8_t nvm[LGPS_BMP388_NVM_LEN],
			     struct lgps_bmp388_calib *out)
{
	bool all_zero = true;
	bool all_ones = true;

	if ((nvm == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	for (uint8_t idx = 0U; idx < LGPS_BMP388_NVM_LEN; idx++) {
		if (nvm[idx] != 0x00U) {
			all_zero = false;
		}

		if (nvm[idx] != 0xFFU) {
			all_ones = false;
		}
	}

	/* Unprogrammed NVM, or a transfer that never happened (the sensor hub fills
	 * the slots of a NACKing slave with zeros). The polynomial would happily
	 * turn either pattern into a number, so reject it here instead.
	 */
	if (all_zero || all_ones) {
		return -EINVAL;
	}

	/* Section 9.1. */
	out->par_t1 = (float)nvm_u16(&nvm[0]) * DATASHEET_MUL_2P8;
	out->par_t2 = (float)nvm_u16(&nvm[2]) / DATASHEET_2P30;
	out->par_t3 = (float)nvm_s8(&nvm[4]) / DATASHEET_2P48;
	out->par_p1 = (float)((int32_t)nvm_s16(&nvm[5]) - 16384) / DATASHEET_2P20;
	out->par_p2 = (float)((int32_t)nvm_s16(&nvm[7]) - 16384) / DATASHEET_2P29;
	out->par_p3 = (float)nvm_s8(&nvm[9]) / DATASHEET_2P32;
	out->par_p4 = (float)nvm_s8(&nvm[10]) / DATASHEET_2P37;
	out->par_p5 = (float)nvm_u16(&nvm[11]) * DATASHEET_MUL_2P3;
	out->par_p6 = (float)nvm_u16(&nvm[13]) / DATASHEET_2P6;
	out->par_p7 = (float)nvm_s8(&nvm[15]) / DATASHEET_2P8;
	out->par_p8 = (float)nvm_s8(&nvm[16]) / DATASHEET_2P15;
	out->par_p9 = (float)nvm_s16(&nvm[17]) / DATASHEET_2P48;
	out->par_p10 = (float)nvm_s8(&nvm[19]) / DATASHEET_2P48;
	out->par_p11 = (float)nvm_s8(&nvm[20]) / DATASHEET_2P65;

	/* par_t2 is the temperature gain (degC per ADC LSB). A zero gain can only
	 * come from a block that was not programmed; it would make every
	 * temperature read 0 degC and every pressure offset-only.
	 */
	if ((out->par_t1 == 0.0F) || (out->par_t2 == 0.0F)) {
		return -EINVAL;
	}

	return 0;
}

int lgps_bmp388_compensate(const struct lgps_bmp388_calib *calib, uint32_t raw_pressure,
			   uint32_t raw_temperature, float *pressure_pa,
			   float *temperature_c)
{
	float partial_data1;
	float partial_data2;
	float partial_data3;
	float partial_data4;
	float partial_out1;
	float partial_out2;
	float t_lin;
	const float uncomp_press = (float)raw_pressure;

	if ((calib == NULL) || (pressure_pa == NULL) || (temperature_c == NULL)) {
		return -EINVAL;
	}

	/* Section 9.2. The datasheet keeps t_lin inside the calibration structure
	 * because the pressure polynomial needs it; a burst carries both values, so
	 * one local is enough and the function stays free of state.
	 */
	partial_data1 = (float)raw_temperature - calib->par_t1;
	partial_data2 = partial_data1 * calib->par_t2;
	t_lin = partial_data2 + (partial_data1 * partial_data1) * calib->par_t3;

	/* Section 9.3, term by term as in the appendix. */
	partial_data1 = calib->par_p6 * t_lin;
	partial_data2 = calib->par_p7 * (t_lin * t_lin);
	partial_data3 = calib->par_p8 * (t_lin * t_lin * t_lin);
	partial_out1 = calib->par_p5 + partial_data1 + partial_data2 + partial_data3;

	partial_data1 = calib->par_p2 * t_lin;
	partial_data2 = calib->par_p3 * (t_lin * t_lin);
	partial_data3 = calib->par_p4 * (t_lin * t_lin * t_lin);
	partial_out2 = uncomp_press *
		       (calib->par_p1 + partial_data1 + partial_data2 + partial_data3);

	partial_data1 = uncomp_press * uncomp_press;
	partial_data2 = calib->par_p9 + calib->par_p10 * t_lin;
	partial_data3 = partial_data1 * partial_data2;
	partial_data4 = partial_data3 +
			(uncomp_press * uncomp_press * uncomp_press) * calib->par_p11;

	*temperature_c = t_lin;
	*pressure_pa = partial_out1 + partial_out2 + partial_data4;

	return 0;
}
