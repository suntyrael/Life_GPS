/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - staging-ring sector format tests (native_sim).
 *
 * These cases cover the two defects that no board build could have caught, because
 * flash_ring.c is only ever executed on real hardware:
 *
 *   1. sealing a sector used to recompute the header CRC in place. NOR programming
 *      can only clear bits, so a CRC byte that had to go back from 0 to 1 stayed
 *      wrong and the next mount reformatted the whole ring. test_seal_* pins the
 *      rule that makes sealing safe: commit_flags is outside the CRC, and sealing
 *      changes nothing but flags bits that are cleared.
 *   2. records_in_sector lived only in RAM, so the first append after a reboot
 *      overwrote an already programmed record. test_used_records_* pins the media
 *      scan that rebuilds the write cursor.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <lifegps/flash_ring.h>
#include <lifegps/sector_format.h>

/* The record area of one sector: what follows the 16-byte header. */
#define RECORD_AREA_BYTES (LGPS_RING_SECTOR_SIZE - LGPS_RING_HEADER_SIZE)

/* A programmed byte pattern; 0xFF is the erased state and never valid data. */
#define PROGRAMMED_BYTE 0xA5U

static uint8_t image[RECORD_AREA_BYTES];

static void ring_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Erased NOR reads back as all ones. */
	memset(image, 0xFF, sizeof(image));
}

/* Pretend @p count records were written, in order, from the first slot. */
static void program_records(uint16_t count)
{
	zassert_true(count <= LGPS_RING_RECORDS_PER_SECTOR, "count out of range");

	memset(image, PROGRAMMED_BYTE, (size_t)count * (size_t)LGPS_RING_RECORD_SIZE);
}

ZTEST(lifegps_ring, test_geometry_constants)
{
	zassert_equal(LGPS_RING_HEADER_SIZE, 16U, "header size");
	zassert_equal(LGPS_RING_RECORD_SIZE, 32U, "record size");
	zassert_equal(LGPS_RING_RECORDS_PER_SECTOR, 127U, "records per 4 KiB sector");
	zassert_equal(sizeof(struct lgps_sector_header), LGPS_RING_HEADER_SIZE,
		      "header struct must match on-media size");
	zassert_equal(offsetof(struct lgps_sector_header, commit_flags), 12U,
		      "commit_flags offset is part of the on-media format");
	zassert_equal(offsetof(struct lgps_sector_header, header_crc16), 14U,
		      "header CRC is the last field");
}

ZTEST(lifegps_ring, test_header_init_valid)
{
	struct lgps_sector_header header;

	lgps_sector_header_init(&header, 7U, 3U);

	zassert_equal(header.magic, LGPS_RING_HEADER_MAGIC, "magic");
	zassert_equal(header.sector_seq, 7U, "sector_seq");
	zassert_equal(header.erase_count, 3U, "erase_count");
	zassert_equal(header.commit_flags, LGPS_RING_FLAGS_WRITING, "flags after init");
	zassert_true(lgps_sector_header_valid(&header), "fresh header must verify");
}

ZTEST(lifegps_ring, test_header_rejects_corruption)
{
	struct lgps_sector_header header;

	lgps_sector_header_init(&header, 1U, 1U);

	header.erase_count ^= 1U;
	zassert_false(lgps_sector_header_valid(&header), "erase_count must be covered by the CRC");

	lgps_sector_header_init(&header, 1U, 1U);
	header.magic ^= 0xFFU;
	zassert_false(lgps_sector_header_valid(&header), "a wrong magic must be rejected");

	lgps_sector_header_init(&header, 1U, 1U);
	header.header_crc16 ^= 0x0001U;
	zassert_false(lgps_sector_header_valid(&header), "a wrong CRC must be rejected");
}

/*
 * Regression for the reformat loop: the CRC written when the sector started must
 * still verify after the sector was sealed, without rewriting it.
 */
ZTEST(lifegps_ring, test_seal_keeps_header_valid)
{
	struct lgps_sector_header header;
	struct lgps_sector_header sealed;
	uint16_t crc_before;

	lgps_sector_header_init(&header, 4U, 9U);
	crc_before = header.header_crc16;

	sealed = header;
	lgps_sector_header_seal(&sealed);

	zassert_equal(sealed.commit_flags, LGPS_RING_FLAGS_ARCHIVED, "flags after seal");
	zassert_equal(sealed.header_crc16, crc_before,
		      "sealing must not need a new CRC (NOR cannot program 0 -> 1)");
	zassert_true(lgps_sector_header_valid(&sealed), "sealed header must verify");
}

/*
 * Everything a seal does has to be representable as programming an erased cell:
 * the sealed image may only have 1 -> 0 transitions relative to the initial one,
 * i.e. sealed & initial == sealed for every byte.
 */
ZTEST(lifegps_ring, test_seal_only_clears_bits)
{
	struct lgps_sector_header header;
	struct lgps_sector_header sealed;
	const uint8_t *init_bytes = (const uint8_t *)&header;
	const uint8_t *sealed_bytes = (const uint8_t *)&sealed;
	bool flags_changed = false;

	lgps_sector_header_init(&header, 2U, 5U);
	sealed = header;
	lgps_sector_header_seal(&sealed);

	for (size_t i = 0U; i < sizeof(sealed); i++) {
		zassert_equal(sealed_bytes[i], (uint8_t)(sealed_bytes[i] & init_bytes[i]),
			      "byte %u needs a 0 -> 1 transition (illegal on NOR)",
			      (unsigned int)i);

		if (sealed_bytes[i] != init_bytes[i]) {
			flags_changed = true;
		}
	}

	zassert_true(flags_changed, "sealing must change the commit_flags bits");
	/* 0xFFFF -> 0x55AA: the archived value is a bitwise subset of "writing". */
	zassert_equal(LGPS_RING_FLAGS_ARCHIVED,
		      (uint16_t)(LGPS_RING_FLAGS_ARCHIVED & LGPS_RING_FLAGS_WRITING),
		      "archived flags must be a subset of the writing flags");
}

ZTEST(lifegps_ring, test_slot_erased)
{
	uint8_t slot[LGPS_RING_RECORD_SIZE];

	memset(slot, 0xFF, sizeof(slot));
	zassert_true(lgps_sector_slot_erased(slot), "all-0xFF is the erased state");

	slot[LGPS_RING_RECORD_SIZE - 1U] = 0xFEU;
	zassert_false(lgps_sector_slot_erased(slot), "a single programmed bit is enough");
}

ZTEST(lifegps_ring, test_used_records_empty_sector)
{
	zassert_equal(lgps_sector_used_records(image, sizeof(image)), 0U,
		      "a freshly erased sector holds no record");
}

ZTEST(lifegps_ring, test_used_records_partial_sector)
{
	program_records(5U);

	zassert_equal(lgps_sector_used_records(image, sizeof(image)), 5U,
		      "the cursor must advance by the number of records stored");
}

ZTEST(lifegps_ring, test_used_records_one_before_rotate)
{
	program_records(LGPS_RING_RECORDS_PER_SECTOR - 1U);

	zassert_equal(lgps_sector_used_records(image, sizeof(image)),
		      LGPS_RING_RECORDS_PER_SECTOR - 1U, "second to last slot");
}

ZTEST(lifegps_ring, test_used_records_full_sector)
{
	program_records(LGPS_RING_RECORDS_PER_SECTOR);

	zassert_equal(lgps_sector_used_records(image, sizeof(image)), LGPS_RING_RECORDS_PER_SECTOR,
		      "a full sector must report every slot, so the next append rotates");
}

/*
 * The mount path scans in 256-byte chunks (8 records) and has to stop as soon as a
 * chunk is not completely programmed - this reproduces that boundary exactly.
 */
ZTEST(lifegps_ring, test_used_records_chunked_scan)
{
	const size_t chunk_bytes = 256U;
	const size_t records_per_chunk = chunk_bytes / LGPS_RING_RECORD_SIZE;
	size_t offset;
	uint16_t total = 0U;

	/* 11 records = one full chunk, then a chunk with 3 of 8 slots programmed. */
	program_records(11U);

	for (offset = 0U; offset < sizeof(image); offset += chunk_bytes) {
		size_t len_bytes = MIN(chunk_bytes, sizeof(image) - offset);
		size_t slots = len_bytes / LGPS_RING_RECORD_SIZE;
		uint16_t in_chunk = lgps_sector_used_records(&image[offset], len_bytes);

		total = (uint16_t)(total + in_chunk);

		if (in_chunk < slots) {
			break;
		}
	}

	zassert_equal(total, 11U, "chunked scan must find the same cursor");
	zassert_equal(records_per_chunk, 8U, "a 256-byte chunk holds 8 records");
}

/*
 * A truncated tail (the last chunk of a sector is 7 records, not 8) must not be
 * counted twice or read past the end of the image.
 */
ZTEST(lifegps_ring, test_used_records_ignores_partial_slot)
{
	program_records(2U);

	/* Only one and a half records fit into this view. */
	zassert_equal(lgps_sector_used_records(image, (LGPS_RING_RECORD_SIZE + 16U)), 1U,
		      "a partial trailing slot must be ignored");
}

ZTEST_SUITE(lifegps_ring, NULL, NULL, ring_before, NULL, NULL);
