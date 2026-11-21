/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - LSM6DSV16X FIFO tag and SFLP payload decoding.
 *
 * Pure functions: no devicetree, no SPI, no logging, no global state, so the
 * native_sim test (Firmware/tests/lifegps.sflp) can feed recorded FIFO bytes
 * straight in.
 *
 * Formats, and where each one comes from:
 *
 *   FIFO entry (DS13510 Rev 4 section 6.12 and tables 216/217): one tag byte
 *   followed by six data bytes; TAG_SENSOR[4:0] occupies bits 7..3 of the tag
 *   byte, TAG_CNT[1:0] bits 2..1. That matches ST's own
 *   lsm6dsv16x_get_fifo_out_raw(), which reads 7 bytes from FIFO_DATA_OUT_TAG
 *   (0x78) and takes the tag from the same bit field.
 *
 *   SFLP game rotation vector (FIFO tag 0x13): three IEEE 754 binary16 values
 *   (x, y, z), w recovered as sqrt(1 - x^2 - y^2 - z^2) and x/y/z renormalised
 *   when the squared sum exceeds one. DS13510 section 2.8 and section 6.12 say
 *   only that the quaternion is stored in the FIFO; the binary16 layout and the
 *   w reconstruction are taken from ST's LSM6DSV16X SFLP example
 *   (lsm6dsv16x_STdC/examples/lsm6dsv16x_sensor_fusion.c, sflp2q()).
 *
 *   SFLP gravity vector (FIFO tag 0x17): three int16 values in mg, 0.061 mg/LSB
 *   (lsm6dsv16x_from_sflp_to_mg() in the ST driver: lsb * 0.061f, i.e. the
 *   accelerometer +/-2 g sensitivity from DS13510 table 2).
 *
 * The SFLP gyroscope bias tag (0x16) is deliberately not decoded: nothing in
 * this firmware consumes it, and the datasheet (table 350 area) documents that
 * value as a half-precision register for the bias *setting* procedure, which is
 * not the same encoding as the FIFO bias stream. Guessing it would be worse than
 * dropping it.
 */

#ifndef LIFEGPS_SFLP_H_
#define LIFEGPS_SFLP_H_

#include <stdint.h>

/* One FIFO entry: tag byte + six data bytes (DS13510 table 216). */
#define LGPS_FIFO_ENTRY_BYTES 7U

/* TAG_SENSOR values this firmware acts on (DS13510 table 218). */
#define LGPS_FIFO_TAG_EMPTY 0x00U
#define LGPS_FIFO_TAG_SFLP_GAME_ROTATION 0x13U
#define LGPS_FIFO_TAG_SFLP_GYRO_BIAS 0x16U
#define LGPS_FIFO_TAG_SFLP_GRAVITY 0x17U

/* Gravity vector scale, in mg per LSB (ST driver lsm6dsv16x_from_sflp_to_mg). */
#define LGPS_SFLP_GRAVITY_MG_PER_LSB 0.061F

/*
 * TAG_SENSOR[4:0] out of the raw tag byte (bits 7..3). The empty tag (0x00)
 * reads back zero, so a caller can stop on it.
 */
uint8_t lgps_fifo_tag(uint8_t tag_byte);

/*
 * IEEE 754 binary16 -> float. Exact for every normal value; subnormals are
 * renormalised by shifting the fraction until the implicit bit appears. inf/nan
 * collapse to 0.0 F because the SFLP block never emits them and returning a
 * value the caller cannot range-check would be worse than returning zero.
 */
float lgps_sflp_half_to_float(uint16_t half);

/* SFLP gravity vector component, mg (int16, 0.061 mg/LSB). */
float lgps_sflp_gravity_mg(int16_t lsb);

/*
 * Decode the six FIFO data bytes of the game rotation vector into w, x, y, z
 * (quat[0] = w), unit length except for the w = 0 case, which is the correct
 * 180 degree rotation. Returns nothing: the caller checks quat[3] if it wants to
 * know whether the entry was usable.
 */
void lgps_sflp_game_quat(const uint8_t raw[6], float quat[4]);

#endif /* LIFEGPS_SFLP_H_ */
