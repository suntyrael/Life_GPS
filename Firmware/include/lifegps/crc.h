/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - checksum primitives.
 *
 * Parameters are the ones frozen in protocol/lifegps_protocol.yaml (crc: block):
 *   record  : CRC-8, poly 0x07, init 0x00, no reflection, xorout 0x00
 *   frame   : CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF, no reflection
 *   package : CRC-32/ISO-HDLC, poly 0x04C11DB7, init/xorout 0xFFFFFFFF, reflected
 *
 * All functions are ISR-safe, allocate nothing and never touch global state.
 *
 * A NULL @p data never dereferences: the function returns the value of an empty
 * input (the CRC init value for the one-shot helpers, the unchanged running value
 * for the *_update helpers). Callers feeding flash/BLE buffers therefore cannot
 * turn a truncated frame into a hard fault.
 */

#ifndef LIFEGPS_CRC_H_
#define LIFEGPS_CRC_H_

#include <stddef.h>
#include <stdint.h>

/* CRC-8 of a buffer; use lgps_crc8_update() to chain partial buffers. */
uint8_t lgps_crc8(const uint8_t *data, size_t len_bytes);

/* Same CRC-8 with an explicit running value (start with 0x00). */
uint8_t lgps_crc8_update(uint8_t crc, const uint8_t *data, size_t len_bytes);

/* CRC-16/CCITT-FALSE; start value for chaining is 0xFFFF. */
uint16_t lgps_crc16_ccitt_false(const uint8_t *data, size_t len_bytes);

/* Same CRC-16/CCITT-FALSE with an explicit running value (start with 0xFFFF). */
uint16_t lgps_crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, size_t len_bytes);

/* CRC-32/ISO-HDLC; start value for chaining is 0xFFFFFFFF. */
uint32_t lgps_crc32_iso_hdlc(const uint8_t *data, size_t len_bytes);

/*
 * Same CRC-32/ISO-HDLC with an explicit running value (start with 0xFFFFFFFF;
 * finalize by taking the bitwise complement ~running_crc).
 */
uint32_t lgps_crc32_iso_hdlc_update(uint32_t running_crc, const uint8_t *data, size_t len_bytes);

#endif /* LIFEGPS_CRC_H_ */
