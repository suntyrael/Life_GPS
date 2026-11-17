/*
 * Life_GPS firmware - checksum primitives.
 *
 * Parameters are frozen in protocol/lifegps_protocol.yaml (crc: block):
 *   record  : CRC-8, poly 0x07, init 0x00, xorout 0x00, no reflection
 *   frame   : CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF, xorout 0x0000
 *   package : CRC-32/ISO-HDLC, poly 0x04C11DB7 (reflected 0xEDB88320),
 *             init/xorout 0xFFFFFFFF, reflected input and output
 *
 * Bitwise, table-free and stateless so every function is callable from an ISR:
 * no global state, no allocation, no blocking.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include <lifegps/crc.h>

/* CRC-8, poly 0x07, non-reflected. */
uint8_t lgps_crc8_update(uint8_t crc, const uint8_t *data, size_t len_bytes)
{
	uint8_t value = crc;
	size_t index;
	uint8_t bit;

	if (data == NULL) {
		return crc;
	}

	for (index = 0U; index < len_bytes; index++) {
		value ^= data[index];
		for (bit = 0U; bit < 8U; bit++) {
			if ((value & 0x80U) != 0U) {
				value = (uint8_t)((value << 1U) ^ 0x07U);
			} else {
				value = (uint8_t)(value << 1U);
			}
		}
	}

	return value;
}

uint8_t lgps_crc8(const uint8_t *data, size_t len_bytes)
{
	return lgps_crc8_update(0x00U, data, len_bytes);
}

/* CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF, non-reflected. */
uint16_t lgps_crc16_ccitt_false(const uint8_t *data, size_t len_bytes)
{
	uint16_t value = 0xFFFFU;
	size_t index;
	uint8_t bit;

	if (data == NULL) {
		return value;
	}

	for (index = 0U; index < len_bytes; index++) {
		value ^= (uint16_t)((uint16_t)data[index] << 8U);
		for (bit = 0U; bit < 8U; bit++) {
			if ((value & 0x8000U) != 0U) {
				value = (uint16_t)((value << 1U) ^ 0x1021U);
			} else {
				value = (uint16_t)(value << 1U);
			}
		}
	}

	return value;
}

/* CRC-32/ISO-HDLC, reflected: shift right, reflected poly 0xEDB88320. */
uint32_t lgps_crc32_iso_hdlc(const uint8_t *data, size_t len_bytes)
{
	uint32_t value = 0xFFFFFFFFU;
	size_t index;
	uint8_t bit;

	if (data == NULL) {
		return ~value;
	}

	for (index = 0U; index < len_bytes; index++) {
		value ^= (uint32_t)data[index];
		for (bit = 0U; bit < 8U; bit++) {
			if ((value & 0x01U) != 0U) {
				value = (value >> 1U) ^ 0xEDB88320U;
			} else {
				value >>= 1U;
			}
		}
	}

	/* xorout 0xFFFFFFFF, i.e. the complement of the running register. */
	return ~value;
}
