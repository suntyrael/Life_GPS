/*
 * ztest suite for Firmware/src/util/crc.c.
 *
 * The "123456789" check values are the catalogue values of the parameters
 * frozen in protocol/lifegps_protocol.yaml:
 *   CRC-8            0xF4
 *   CRC-16/CCITT-FALSE 0x29B1
 *   CRC-32/ISO-HDLC  0xCBF43926
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <zephyr/ztest.h>

#include <lifegps/crc.h>

static const uint8_t check_data[] = "123456789";

ZTEST(lifegps_crc, test_crc8_check_value)
{
	zassert_equal((uint32_t)lgps_crc8(check_data, sizeof(check_data) - 1U), 0xF4U,
		      "CRC-8(\"123456789\") check value");
}

ZTEST(lifegps_crc, test_crc16_check_value)
{
	zassert_equal((uint32_t)lgps_crc16_ccitt_false(check_data, sizeof(check_data) - 1U),
		      0x29B1U, "CRC-16/CCITT-FALSE(\"123456789\") check value");
}

ZTEST(lifegps_crc, test_crc32_check_value)
{
	zassert_equal(lgps_crc32_iso_hdlc(check_data, sizeof(check_data) - 1U), 0xCBF43926U,
		      "CRC-32/ISO-HDLC(\"123456789\") check value");
}

ZTEST(lifegps_crc, test_empty_input_returns_init_value)
{
	zassert_equal((uint32_t)lgps_crc8(NULL, 0U), 0x00U, "CRC-8 of no bytes");
	zassert_equal((uint32_t)lgps_crc16_ccitt_false(NULL, 0U), 0xFFFFU,
		      "CRC-16 of no bytes");
	zassert_equal(lgps_crc32_iso_hdlc(NULL, 0U), 0x00000000U, "CRC-32 of no bytes");
	zassert_equal((uint32_t)lgps_crc8_update(0xA5U, NULL, 0U), 0xA5U,
		      "CRC-8 seed must pass through untouched");
	zassert_equal((uint32_t)lgps_crc16_ccitt_false_update(0x1234U, NULL, 0U), 0x1234U,
		      "CRC-16 seed must pass through untouched");
	zassert_equal(lgps_crc32_iso_hdlc_update(0x12345678U, NULL, 0U), 0x12345678U,
		      "CRC-32 seed must pass through untouched");
}

ZTEST(lifegps_crc, test_crc8_update_chains)
{
	const size_t total_bytes = sizeof(check_data) - 1U;
	const size_t split_bytes = 4U;
	uint8_t chained_crc;

	chained_crc = lgps_crc8_update(0x00U, check_data, split_bytes);
	chained_crc = lgps_crc8_update(chained_crc, &check_data[split_bytes],
				       total_bytes - split_bytes);

	zassert_equal((uint32_t)chained_crc,
		      (uint32_t)lgps_crc8(check_data, total_bytes),
		      "chained CRC-8 must equal the one-shot CRC-8");
}

ZTEST(lifegps_crc, test_crc16_update_chains)
{
	const size_t total_bytes = sizeof(check_data) - 1U;
	uint16_t chained_crc;

	chained_crc = lgps_crc16_ccitt_false_update(0xFFFFU, check_data, 3U);
	chained_crc = lgps_crc16_ccitt_false_update(chained_crc, &check_data[3], 2U);
	chained_crc = lgps_crc16_ccitt_false_update(chained_crc, &check_data[5],
						    total_bytes - 5U);

	zassert_equal((uint32_t)chained_crc,
		      (uint32_t)lgps_crc16_ccitt_false(check_data, total_bytes),
		      "chained CRC-16 must equal the one-shot CRC-16");
	zassert_equal((uint32_t)chained_crc, 0x29B1U,
		      "chained CRC-16(\"123456789\") check value");
}

ZTEST(lifegps_crc, test_crc32_update_chains)
{
	const size_t total_bytes = sizeof(check_data) - 1U;
	uint32_t running;

	running = lgps_crc32_iso_hdlc_update(0xFFFFFFFFU, check_data, 1U);
	running = lgps_crc32_iso_hdlc_update(running, &check_data[1], 4U);
	running = lgps_crc32_iso_hdlc_update(running, &check_data[5], total_bytes - 5U);

	zassert_equal(~running, lgps_crc32_iso_hdlc(check_data, total_bytes),
		      "chained CRC-32 must equal the one-shot CRC-32");
	zassert_equal(~running, 0xCBF43926U,
		      "chained CRC-32(\"123456789\") check value");
}

ZTEST(lifegps_crc, test_single_bit_flip_is_detected)
{
	/* '1' (0x31) -> '0' (0x30) flips exactly one bit of the message. */
	static const uint8_t flipped_data[] = "023456789";

	zassert_not_equal((uint32_t)lgps_crc8(flipped_data, sizeof(flipped_data) - 1U),
			  (uint32_t)lgps_crc8(check_data, sizeof(check_data) - 1U),
			  "CRC-8 missed a one-bit change");
	zassert_not_equal((uint32_t)lgps_crc16_ccitt_false(flipped_data,
							  sizeof(flipped_data) - 1U),
			  (uint32_t)lgps_crc16_ccitt_false(check_data,
							   sizeof(check_data) - 1U),
			  "CRC-16 missed a one-bit change");
	zassert_not_equal(lgps_crc32_iso_hdlc(flipped_data, sizeof(flipped_data) - 1U),
			  lgps_crc32_iso_hdlc(check_data, sizeof(check_data) - 1U),
			  "CRC-32 missed a one-bit change");
}

ZTEST(lifegps_crc, test_null_buffer_is_empty_input)
{
	/* Truncated flash/BLE frames must not fault the CRC helpers. */
	zassert_equal(lgps_crc8(NULL, 5U), 0x00U, "crc8 NULL must return the init value");
	zassert_equal(lgps_crc8_update(0x5AU, NULL, 5U), 0x5AU, "crc8 chaining must not advance");
	zassert_equal(lgps_crc16_ccitt_false(NULL, 5U), 0xFFFFU, "crc16 NULL must return init");
	zassert_equal(lgps_crc16_ccitt_false_update(0xBEEFU, NULL, 5U), 0xBEEFU,
		      "crc16 chaining must not advance");
	zassert_equal(lgps_crc32_iso_hdlc(NULL, 5U), 0x00000000U, "crc32 NULL must return the empty CRC");
	zassert_equal(lgps_crc32_iso_hdlc_update(0xCAFEBABEU, NULL, 5U), 0xCAFEBABEU,
		      "crc32 chaining must not advance");
}

ZTEST_SUITE(lifegps_crc, NULL, NULL, NULL, NULL, NULL);
