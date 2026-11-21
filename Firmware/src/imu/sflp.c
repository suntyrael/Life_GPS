/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * LSM6DSV16X FIFO tag and SFLP payload decoding. Pure arithmetic + memcpy on
 * purpose: no device access, so it is covered by native_sim unit tests.
 *
 * The comments in lifegps/sflp.h carry the datasheet (DS13510 Rev 4) and ST
 * driver references for every format used here.
 */

#include <lifegps/sflp.h>

#include <math.h>
#include <string.h>

uint8_t lgps_fifo_tag(uint8_t tag_byte)
{
	/* TAG_SENSOR[4:0] = bits 7..3 (DS13510 table 216). */
	return (uint8_t)(tag_byte >> 3);
}

float lgps_sflp_half_to_float(uint16_t half)
{
	const uint32_t sign = (uint32_t)(half >> 15);
	const uint32_t exponent = (uint32_t)((half >> 10) & 0x1FU);
	uint32_t fraction = (uint32_t)(half & 0x03FFU);
	uint32_t bits;
	float out;

	if (exponent == 0U) {
		if (fraction == 0U) {
			/* +/- 0.0: keep the sign so a signed zero does not flip. */
			return (sign != 0U) ? -0.0F : 0.0F;
		}

		/* Subnormal: shift the fraction left until the implicit bit shows up. */
		int32_t unbiased = -14;

		while ((fraction & 0x0400U) == 0U) {
			fraction <<= 1;
			unbiased--;
		}
		fraction &= 0x03FFU;

		bits = (sign << 31) | ((uint32_t)(unbiased + 127) << 23) | (fraction << 13);
	} else if (exponent == 0x1FU) {
		/* inf / nan: not produced by the SFLP block, see the header. */
		return 0.0F;
	} else {
		const int32_t unbiased = (int32_t)exponent - 15;

		bits = (sign << 31) | ((uint32_t)(unbiased + 127) << 23) | (fraction << 13);
	}

	/* memcpy, not a pointer cast: the bit pattern is an integer, the value is a
	 * float, and type punning through a union or pointer is undefined in C99.
	 */
	memcpy(&out, &bits, sizeof(out));

	return out;
}

float lgps_sflp_gravity_mg(int16_t lsb)
{
	return (float)lsb * LGPS_SFLP_GRAVITY_MG_PER_LSB;
}

void lgps_sflp_game_quat(const uint8_t raw[6], float quat[4])
{
	float sumsq;

	if ((raw == NULL) || (quat == NULL)) {
		return;
	}

	/* Little endian, like every other output register of this part. */
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		const uint16_t half = (uint16_t)((uint16_t)raw[2U * axis] |
						 ((uint16_t)raw[2U * axis + 1U] << 8));

		quat[axis] = lgps_sflp_half_to_float(half);
	}

	sumsq = (quat[0] * quat[0]) + (quat[1] * quat[1]) + (quat[2] * quat[2]);

	if (sumsq > 1.0F) {
		/* Rounding in the binary16 encoding can push a unit vector just past
		 * one; rescale instead of taking the square root of a negative.
		 */
		const float norm = sqrtf(sumsq);

		quat[0] /= norm;
		quat[1] /= norm;
		quat[2] /= norm;
		quat[3] = 0.0F;
	} else {
		quat[3] = sqrtf(1.0F - sumsq);
	}
}
