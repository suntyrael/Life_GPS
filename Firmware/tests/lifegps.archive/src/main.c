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

ZTEST(lifegps_archive, test_journal_bounds_32mb_and_64mb_sfdp)
{
	const uint32_t limit_32mb = (32U * 1024U * 1024U) - (1024U * 1024U);
	const uint32_t limit_64mb = (64U * 1024U * 1024U) - (1024U * 1024U);
	struct lgps_journal j32;
	struct lgps_journal j64;
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
	zassert_equal(j32.next_write_offset, LGPS_ARCHIVE_DATA_OFFSET, "starts at 0x16000");

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
