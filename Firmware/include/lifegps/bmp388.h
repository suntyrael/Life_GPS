/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - BMP388 TPG compensation (pure maths, no Zephyr dependency).
 *
 * The BMP388 does not report pressure in Pa or temperature in degC. It reports
 * two unsigned 24-bit ADC values, and a physical value only exists after the
 * polynomial in the datasheet has been applied with the trimming ("TPG")
 * coefficients of that individual part. This header owns that arithmetic only,
 * so it can be unit tested on native_sim (Firmware/tests/lifegps.bmp388)
 * without a bus, a driver or a device tree. The register transport stays in
 * src/imu/lsm6dsr.c, which owns the sensor hub.
 *
 * Source, checked in the repository rather than from memory:
 * Doc/Hardware/Datasheets/BMP388_Datasheet.pdf (BST-BMP388-DS001-07, Rev 1.7)
 *   - 3.10 "Data readout": one burst from press_xlsb to temp_msb (0x04..0x09),
 *     each value unsigned 24 bit little endian (tables 28 and 29).
 *   - 3.11 / 3.11.1 and table 23: the trimming coefficients at the read
 *     addresses 0x31..0x45. The size and the signedness differ per coefficient,
 *     see lgps_bmp388_calib_decode().
 *   - 9.1 (coefficient scaling), 9.2 (temperature), 9.3 (pressure): the
 *     reference implementation from the appendix, transcribed term by term.
 * Section 4.3.21 names 0x30..0x57 for the whole calibration area, but table 23
 * lists only 0x31..0x45 as trimming coefficients (0x30 and 0x46..0x57 are
 * reserved), which is why LGPS_BMP388_NVM_LEN is 21.
 *
 * Low bits of the raw values are zero, and that is correct behaviour, not a bus
 * fault: the ADC resolution follows the oversampling that PWR_CTRL/OSR select
 * (the firmware programs osr_p = x8, osr_t = x2). Measured on the M1 bench
 * 2026-11-21: pressure always a multiple of 32 (5 low bits clear, 19 valid) and
 * temperature always a multiple of 128 (7 low bits clear, 17 valid), i.e.
 * 16 + log2(osr_p) and 16 + log2(osr_t) valid bits, exactly as expected.
 */

#ifndef LIFEGPS_BMP388_H_
#define LIFEGPS_BMP388_H_

#include <stdint.h>

/* Data registers, table 28/29: one burst from 0x04 covers pressure
 * (0x04..0x06) followed by temperature (0x07..0x09).
 */
#define LGPS_BMP388_DATA_ADDR 0x04U
#define LGPS_BMP388_DATA_LEN 6U

/* Trimming coefficients, table 23: read addresses 0x31 .. 0x45. */
#define LGPS_BMP388_NVM_ADDR 0x31U
#define LGPS_BMP388_NVM_LEN 21U

/* Full-accuracy operating range, datasheet section 1 (-40..+85 degC,
 * 300..1250 hPa) and table 1. Anything outside this window after compensation
 * is a fault in the coefficients or in the transfer, not weather; the driver
 * uses it for a one-shot warning instead of pretending the value is valid.
 */
#define LGPS_BMP388_PRESS_MIN_PA 30000.0F
#define LGPS_BMP388_PRESS_MAX_PA 125000.0F
#define LGPS_BMP388_TEMP_MIN_C (-40.0F)
#define LGPS_BMP388_TEMP_MAX_C 85.0F

/*
 * The twelve trimming coefficients after the scaling of section 9.1. They are
 * floats because the datasheet's reference implementation is floating point,
 * and 24-bit raw values are exact in a float mantissa.
 */
struct lgps_bmp388_calib {
	float par_t1;
	float par_t2;
	float par_t3;
	float par_p1;
	float par_p2;
	float par_p3;
	float par_p4;
	float par_p5;
	float par_p6;
	float par_p7;
	float par_p8;
	float par_p9;
	float par_p10;
	float par_p11;
};

/*
 * Decode the 21 NVM bytes read from 0x31 into scaled coefficients (section 9.1).
 * Returns 0 on success, -EINVAL on a NULL argument or on a block that reads back
 * all 0x00 or all 0xFF: those are "NVM not readable / not programmed", and
 * feeding them into the polynomial would produce a plausible-looking but
 * meaningless pressure.
 */
int lgps_bmp388_calib_decode(const uint8_t nvm[LGPS_BMP388_NVM_LEN],
			     struct lgps_bmp388_calib *out);

/*
 * Apply sections 9.2 and 9.3 to one burst. raw_pressure/raw_temperature are the
 * 24-bit values of tables 28/29 (not shifted, not sign extended). *pressure_pa
 * lands in Pa and *temperature_c in degC. Both are written even when the result
 * is outside the operating range; the caller decides what to do with it.
 * Returns 0 on success, -EINVAL on a NULL argument.
 */
int lgps_bmp388_compensate(const struct lgps_bmp388_calib *calib, uint32_t raw_pressure,
			   uint32_t raw_temperature, float *pressure_pa,
			   float *temperature_c);

#endif /* LIFEGPS_BMP388_H_ */
