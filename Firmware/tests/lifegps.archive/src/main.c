/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - LGD1 day archive codec (Byte-Delta + Heatshrink) and SFDP-bounded
 * 32 MB / 64 MB journal index unit tests (native_sim).
 */

#include <string.h>
#include <zephyr/ztest.h>

#include <lifegps/archive.h>
#include <lifegps/crc.h>
#include "generated_vectors.h"

ZTEST(lifegps_archive, test_delta_and_heatshrink_roundtrip)
{
	uint8_t delta[sizeof(lgps_vec_traj_basic)];
	uint8_t packed[256];
	uint8_t unpacked[sizeof(lgps_vec_traj_basic)];
	uint8_t restored[sizeof(lgps_vec_traj_basic)];
	size_t packed_len = 0U;

	zassert_equal(lgps_delta_encode(lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic),
					LGPS_RECORD_TRAJECTORY_SIZE, delta),
		      0, "delta encode");
	zassert_equal(lgps_heatshrink_encode(delta, sizeof(delta), packed, sizeof(packed),
					     &packed_len),
		      0, "heatshrink encode");
	zassert_true(packed_len > 0U, "non-empty compressed payload");
	zassert_equal(lgps_heatshrink_decode(packed, packed_len, unpacked, sizeof(unpacked)), 0,
		      "heatshrink decode");
	zassert_mem_equal(unpacked, delta, sizeof(delta), "delta bytes match after heatshrink");
	zassert_equal(lgps_delta_decode(unpacked, sizeof(unpacked), LGPS_RECORD_TRAJECTORY_SIZE,
					restored),
		      0, "delta decode");
	zassert_mem_equal(restored, lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic),
			  "trajectory records round-trip bit-exact");
}

ZTEST(lifegps_archive, test_golden_day_packages_decode_bit_exact)
{
	struct lgps_day_header hdr;
	uint8_t scratch[256];
	uint8_t raw_out[256];
	size_t raw_len = 0U;

	/* Raw golden vector (3 records = 96 B, trajectory_basic.bin) */
	zassert_equal(lgps_day_pkg_decode(lgps_vec_day_raw, sizeof(lgps_vec_day_raw), scratch,
					  &hdr, raw_out, sizeof(raw_out), &raw_len),
		      0, "decode golden raw LGD1");
	zassert_equal(hdr.codec_id, LGPS_PACKAGE_DAY_CODEC_RAW, "raw codec id");
	zassert_equal(hdr.record_count, 3U, "3 records");
	zassert_equal(raw_len, sizeof(lgps_vec_traj_basic), "raw length");
	zassert_mem_equal(raw_out, lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic),
			  "raw LGD1 payload matches golden trajectory");

	/* Compressed golden vector (8 records = 256 B, trajectory_delta_wrap.bin) */
	memset(raw_out, 0, sizeof(raw_out));
	raw_len = 0U;
	zassert_equal(lgps_day_pkg_decode(lgps_vec_day_hs, sizeof(lgps_vec_day_hs), scratch, &hdr,
					  raw_out, sizeof(raw_out), &raw_len),
		      0, "decode golden heatshrink LGD1");
	zassert_equal(hdr.codec_id, LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK, "hs codec id");
	zassert_equal(hdr.day_id, 20261126U, "hs day_id");
	zassert_equal(hdr.record_count, 8U, "8 records");
	zassert_equal(raw_len, 256U, "256 B raw length");
	zassert_equal(lgps_crc32_iso_hdlc(raw_out, raw_len), 0xCC798479U,
		      "hs decoded payload CRC-32 matches manifest");
}

ZTEST(lifegps_archive, test_day_pkg_encode_and_tamper_detection)
{
	uint8_t scratch[sizeof(lgps_vec_traj_basic)];
	uint8_t pkg[512];
	uint8_t decoded[sizeof(lgps_vec_traj_basic)];
	struct lgps_day_header hdr;
	size_t pkg_len = 0U;
	size_t decoded_len = 0U;

	zassert_equal(lgps_day_pkg_encode(20261126U, 20261125U,
					  LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK,
					  lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic),
					  scratch, pkg, sizeof(pkg), &pkg_len),
		      0, "encode compressed LGD1");
	zassert_equal(lgps_day_pkg_decode(pkg, pkg_len, scratch, &hdr, decoded, sizeof(decoded),
					  &decoded_len),
		      0, "decode own LGD1");
	zassert_equal(hdr.day_id, 20261126U, "day_id");
	zassert_mem_equal(decoded, lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic),
			  "decoded payload exact");

	/* Flip a bit in header -> must fail header CRC-16 */
	pkg[10] ^= 0x01U;
	zassert_equal(lgps_day_pkg_decode(pkg, pkg_len, scratch, &hdr, decoded, sizeof(decoded),
					  &decoded_len),
		      -EBADMSG, "header tamper rejected");
}

static struct lgps_journal j32;
static struct lgps_journal j64;

ZTEST(lifegps_archive, test_journal_bounds_32mb_and_64mb_sfdp)
{
	const uint32_t limit_32mb = (32U * 1024U * 1024U) - (1024U * 1024U);
	const uint32_t limit_64mb = (64U * 1024U * 1024U) - (1024U * 1024U);
	struct lgps_journal_entry e = {
		.day_id = 20261126U,
		.flash_offset = LGPS_ARCHIVE_DATA_OFFSET,
		.package_len = 12345U,
		.record_count = 500U,
		.traj_crc32 = 0x12345678U,
		.first_seq_id = 100U,
		.codec_id = LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK,
	};
	uint8_t wire[LGPS_JOURNAL_ENTRY_SIZE];
	struct lgps_journal_entry back;

	lgps_journal_init(&j32, limit_32mb);
	lgps_journal_init(&j64, limit_64mb);
	zassert_equal(j32.next_write_offset, LGPS_ARCHIVE_DATA_OFFSET, "starts at 0x34000");

	zassert_equal(lgps_journal_add(&j32, &e), 0, "add entry to 32MB journal");
	zassert_not_null(lgps_journal_find(&j32, 20261126U), "find entry");
	/* 12345 rounds up to 16384 (4 sectors) */
	zassert_equal(j32.next_write_offset, LGPS_ARCHIVE_DATA_OFFSET + 16384U,
		      "4 KB aligned advance");

	/* Entry exceeding 32 MB limit must be rejected on 32 MB, accepted on 64 MB */
	e.day_id = 20261127U;
	e.flash_offset = limit_32mb + 4096U;
	e.package_len = 4096U;
	zassert_equal(lgps_journal_add(&j32, &e), -ENOSPC, "32MB bound enforced");
	zassert_equal(lgps_journal_add(&j64, &e), 0, "64MB accepts offset past 32MB");

	/* Wire round-trip */
	lgps_journal_entry_encode(&e, wire);
	lgps_journal_entry_decode(wire, &back);
	zassert_equal(back.day_id, e.day_id, "wire day_id");
	zassert_equal(back.flash_offset, e.flash_offset, "wire flash_offset");
	zassert_equal(back.traj_crc32, e.traj_crc32, "wire crc32");

	zassert_equal(lgps_journal_remove(&j32, 20261126U), 0, "remove existing day");
	zassert_is_null(lgps_journal_find(&j32, 20261126U), "removed day gone");
}

ZTEST(lifegps_archive, test_journal_2048_days_eviction_and_inplace_merge)
{
	const uint32_t limit_64mb = (64U * 1024U * 1024U) - (1024U * 1024U);
	struct lgps_journal_entry e;
	const struct lgps_journal_entry *found;
	uint32_t ver_before;

	lgps_journal_init(&j64, limit_64mb);
	zassert_equal(LGPS_JOURNAL_MAX_DAYS, 2048U, "journal supports 2048 days");

	/* Fill past the old 128-day cap all the way up to 2048 days. */
	for (uint32_t i = 0U; i < LGPS_JOURNAL_MAX_DAYS; i++) {
		memset(&e, 0, sizeof(e));
		e.day_id = 20200001U + i;
		e.flash_offset = LGPS_ARCHIVE_DATA_OFFSET + (i * 4096U);
		e.package_len = 2048U;
		e.record_count = 64U;
		e.traj_crc32 = 0x10000000U + i;
		e.first_seq_id = (i * 64U) + 1U;
		e.codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;

		zassert_equal(lgps_journal_add(&j64, &e), 0, "add day %u", (unsigned int)i);
	}

	zassert_equal(j64.count, LGPS_JOURNAL_MAX_DAYS, "filled to 2048 days");
	zassert_not_null(lgps_journal_find(&j64, 20200001U), "oldest day present before eviction");
	zassert_not_null(lgps_journal_find(&j64, 20200301U), ">128th day present");

	/* Updating an existing day_id when full must merge in place without increasing count. */
	ver_before = j64.index_version;
	memset(&e, 0, sizeof(e));
	e.day_id = 20200301U;
	e.flash_offset = LGPS_ARCHIVE_DATA_OFFSET + (LGPS_JOURNAL_MAX_DAYS * 4096U);
	e.package_len = 8192U;
	e.record_count = 250U;
	e.traj_crc32 = 0xDEADBEEFU;
	e.first_seq_id = 19201U;
	e.codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;

	zassert_equal(lgps_journal_add(&j64, &e), 0, "in-place update succeeds at full capacity");
	zassert_equal(j64.count, LGPS_JOURNAL_MAX_DAYS, "count unchanged after merge");
	zassert_equal(j64.index_version, ver_before + 1U, "index_version incremented on merge");
	zassert_equal(j64.next_write_offset, e.flash_offset + 8192U,
		      "next_write_offset advanced on merge");
	found = lgps_journal_find(&j64, 20200301U);
	zassert_not_null(found, "merged day still found");
	zassert_equal(found->record_count, 250U, "merged record_count updated");
	zassert_equal(found->traj_crc32, 0xDEADBEEFU, "merged crc32 updated");

	/* Adding the 2049th distinct day must evict oldest (entries[0]) instead of returning -ENOSPC. */
	memset(&e, 0, sizeof(e));
	e.day_id = 20200001U + LGPS_JOURNAL_MAX_DAYS;
	e.flash_offset = j64.next_write_offset;
	e.package_len = 4096U;
	e.record_count = 100U;
	e.traj_crc32 = 0xCAFEBABEU;
	e.first_seq_id = 999999U;
	e.codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;

	zassert_equal(lgps_journal_add(&j64, &e), 0, "2049th day evicts oldest instead of -ENOSPC");
	zassert_equal(j64.count, LGPS_JOURNAL_MAX_DAYS, "count stays capped at 2048");
	zassert_is_null(lgps_journal_find(&j64, 20200001U), "oldest day evicted");
	zassert_not_null(lgps_journal_find(&j64, 20200002U), "second oldest day retained");
	zassert_not_null(lgps_journal_find(&j64, 20200001U + LGPS_JOURNAL_MAX_DAYS),
			 "2049th day present");
}

ZTEST(lifegps_archive, test_day_id_from_epoch)
{
	/* 2020-01-01 00:00:00Z -> 20200101 */
	zassert_equal(lgps_day_id_from_epoch(0U, 0), 20200101U, "epoch origin");
	/* 2024-02-29 (leap day): 4*365 + 1(2020 leap) + 31(Jan) + 28 = 1520 days since 2020-01-01 */
	zassert_equal(lgps_day_id_from_epoch(1520U * 86400U, 0), 20240229U, "2024 leap day");
	/* Timezone +08:00 rolls 2024-02-29 17:00Z into 2024-03-01 01:00+08:00 */
	zassert_equal(lgps_day_id_from_epoch(1520U * 86400U + 17U * 3600U, 8 * 3600), 20240301U,
		      "tz +08 rollover");
}

ZTEST_SUITE(lifegps_archive, NULL, NULL, NULL, NULL, NULL);
