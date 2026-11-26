/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - generated protocol header contract tests (native_sim).
 *
 * These numbers are frozen in protocol/lifegps_protocol.yaml. If a value ever
 * changes there, this suite fails in CI instead of letting firmware and App
 * drift apart. Only <generated/lifegps_protocol.h> is exercised here: no
 * firmware module is compiled into this test.
 */

#include <zephyr/ztest.h>

#include <generated/lifegps_protocol.h>
#include <lifegps/archive.h>
#include <lifegps/ble_frame.h>
#include <lifegps/crc.h>
#include <lifegps/traj.h>

#include "generated_vectors.h"

ZTEST(lifegps_protogen, test_record_sizes)
{
	zassert_equal(LGPS_RECORD_TRAJECTORY_SIZE, 32, "trajectory record is 32 B");
	zassert_equal(sizeof(struct lifegps_record_trajectory), LGPS_RECORD_TRAJECTORY_SIZE,
		      "packed trajectory struct matches its size macro");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM, 31, "checksum is the last byte");
}

ZTEST(lifegps_protogen, test_frame_contract)
{
	zassert_equal(LGPS_FRAME_COMMAND_MAGIC, 0xA5, "command frame magic");
	zassert_equal(LGPS_FRAME_DATA_MAGIC, 0x5A, "data frame magic");
	zassert_equal(LGPS_FRAME_COMMAND_HEADER_SIZE, 8, "command header size");
	zassert_equal(LGPS_FRAME_DATA_HEADER_SIZE, 14, "data header size");
	zassert_equal(LGPS_FRAME_DATA_OFF_TRANSFER_ID, 4, "transfer_id offset");
	zassert_equal(LGPS_FRAME_DATA_OFF_CURSOR, 8, "cursor offset");
	zassert_equal(LGPS_FRAME_DATA_OFF_PAYLOAD_LEN, 12, "payload_len offset");
}

ZTEST(lifegps_protogen, test_package_contract)
{
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_VER, 1, "day header version");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_SIZE, 48, "day header size");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_CRC16_LEN, 46, "day header CRC16 coverage");
	zassert_equal(LGPS_PACKAGE_DAY_FOOTER_SIZE, 8, "day footer size");
	zassert_equal(LGPS_PACKAGE_DAY_CODEC_RAW, 0U, "raw codec id");
	zassert_equal(LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK, 1U, "hs codec id");
	zassert_equal(LGPS_PACKAGE_DAY_HS_WINDOW_SZ2, 8U, "heatshrink window_sz2");
	zassert_equal(LGPS_PACKAGE_DAY_HS_LOOKAHEAD_SZ2, 4U, "heatshrink lookahead_sz2");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_MAGIC, 0, "day magic offset");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED0, 16, "reserved0 offset");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED1, 28, "reserved1 offset");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED2, 32, "reserved2 offset");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16, 46, "header CRC offset");
	zassert_equal(LGPS_PACKAGE_DAY_FOOTER_OFF_RESERVED, 4, "footer reserved offset");
	zassert_equal(LGPS_PACKAGE_DAY_MAGIC_LEN, 4, "day magic is four ASCII bytes");
	zassert_equal(LGPS_PACKAGE_DAY_CODEC[0], 'b', "day codec string comes from the YAML");
}

ZTEST(lifegps_protogen, test_crc_contract)
{
	zassert_equal(LGPS_PROTO_CRC_RECORD_POLY, 0x07, "record CRC-8 polynomial");
	zassert_equal(LGPS_PROTO_CRC_RECORD_INIT, 0x00, "record CRC-8 init");
	zassert_equal(LGPS_PROTO_CRC_FRAME_POLY, 0x1021, "frame CRC-16 polynomial");
	zassert_equal(LGPS_PROTO_CRC_FRAME_INIT, 0xFFFF, "frame CRC-16 init");
	zassert_equal(LGPS_PROTO_CRC_PACKAGE_INIT, 0xFFFFFFFF, "package CRC-32 init");
	zassert_equal(LGPS_PROTO_CRC_PACKAGE_XOROUT, 0xFFFFFFFF, "package CRC-32 xorout");
}

ZTEST(lifegps_protogen, test_opcode_and_extension_contract)
{
	zassert_equal(LGPS_CMD_GET_CAPS, 0x01, "GET_CAPS opcode");
	zassert_equal(LGPS_CMD_GET_STATUS, 0x02, "GET_STATUS opcode");
	zassert_equal(LGPS_CMD_SET_MODE, 0x10, "SET_MODE opcode");
	zassert_equal(LGPS_CMD_DEBUG_UNLOCK, 0x7F, "DEBUG_UNLOCK opcode");
	zassert_equal(LGPS_STREAM_TRAJECTORY, 0, "trajectory stream id");
	zassert_equal(LGPS_TELEMETRY_GNSS, 0x01, "GNSS telemetry id");
	zassert_equal(LGPS_PROTO_STATUS_OK, 0x00, "status OK");
	zassert_equal(LGPS_PROTO_STATUS_OUT_OF_RANGE, 0x07, "status OUT_OF_RANGE");
	zassert_equal(LGPS_PROTO_STATUS_SESSION_EXPIRED, 0x0C, "status SESSION_EXPIRED");
	zassert_equal(LGPS_PROTO_CAP_GNSS_DEBUG, 0x00000400U, "GNSS_DEBUG cap bit");
	zassert_equal(LGPS_PROTO_CAP_GNSS_CFG, 0x00000800U, "GNSS_CFG cap bit");
	zassert_equal(LGPS_PROTO_CAP_OTA_SMP, 0x00004000U, "OTA_SMP cap bit");
	zassert_equal(LGPS_ADV_DATA_SIZE, 10U, "adv manufacturer data size");
	zassert_equal(LGPS_PROTO_STATUS_ACCURACY_SIZE, 116U, "v1 accuracy status size");
	zassert_equal(LGPS_PROTO_STATUS_SYNC_SIZE, 128U, "extended sync status size");
}

static uint16_t read_le16(const uint8_t *buf)
{
	return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static uint32_t read_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
	       ((uint32_t)buf[3] << 24);
}

ZTEST(lifegps_protogen, test_golden_vectors_match_c_codecs)
{
	/* Standard "123456789" CRC checks across all three protocol algorithms. */
	zassert_equal(lgps_crc8(lgps_vec_crc_ref, sizeof(lgps_vec_crc_ref)), 0xF4U,
		      "golden CRC-8 mismatch");
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_crc_ref, sizeof(lgps_vec_crc_ref)), 0x29B1U,
		      "golden CRC-16 mismatch");
	zassert_equal(lgps_crc32_iso_hdlc(lgps_vec_crc_ref, sizeof(lgps_vec_crc_ref)),
		      0xCBF43926U, "golden CRC-32 mismatch");

	/* Command & Data frame golden CRC-16 checks. */
	zassert_equal(lgps_vec_cmd_caps_req[0], LGPS_FRAME_COMMAND_MAGIC,
		      "golden command request magic mismatch");
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_cmd_caps_req,
					     sizeof(lgps_vec_cmd_caps_req) - 2U),
		      read_le16(&lgps_vec_cmd_caps_req[sizeof(lgps_vec_cmd_caps_req) - 2U]),
		      "golden command request CRC-16 mismatch");
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_cmd_caps_rsp,
					     sizeof(lgps_vec_cmd_caps_rsp) - 2U),
		      read_le16(&lgps_vec_cmd_caps_rsp[sizeof(lgps_vec_cmd_caps_rsp) - 2U]),
		      "golden command response CRC-16 mismatch");
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_data_chunk, sizeof(lgps_vec_data_chunk) - 2U),
		      read_le16(&lgps_vec_data_chunk[sizeof(lgps_vec_data_chunk) - 2U]),
		      "golden data frame CRC-16 mismatch");

	/* Decode all 3 golden trajectory records via real lgps_traj_decode(). */
	zassert_equal(sizeof(lgps_vec_traj_basic), 3U * LGPS_RECORD_TRAJECTORY_SIZE,
		      "3 trajectory records");
	for (size_t i = 0U; i < 3U; i++) {
		struct lgps_traj_sample sample;
		const uint8_t *raw = &lgps_vec_traj_basic[i * LGPS_RECORD_TRAJECTORY_SIZE];

		zassert_equal(lgps_traj_decode(raw, &sample), 0, "record %zu CRC/decode", i);
		zassert_equal(sample.seq_id, (uint32_t)(100U + i), "record %zu seq_id", i);
	}

	/* Verify LGD1 day package header CRC-16 and uncompressed CRC-32 on raw & hs vectors. */
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_day_raw, LGPS_PACKAGE_DAY_HEADER_CRC16_LEN),
		      read_le16(&lgps_vec_day_raw[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16]),
		      "day_raw header CRC-16 mismatch");
	zassert_equal(lgps_crc32_iso_hdlc(lgps_vec_traj_basic, sizeof(lgps_vec_traj_basic)),
		      read_le32(&lgps_vec_day_raw[sizeof(lgps_vec_day_raw) -
						  LGPS_PACKAGE_DAY_FOOTER_SIZE]),
		      "day_raw footer CRC-32 mismatch");
	zassert_equal(lgps_crc16_ccitt_false(lgps_vec_day_hs, LGPS_PACKAGE_DAY_HEADER_CRC16_LEN),
		      read_le16(&lgps_vec_day_hs[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16]),
		      "day_hs header CRC-16 mismatch");

	/* Parse golden 0x5A Data Frame via real lgps_ble_data_parse(). */
	{
		struct lgps_ble_data_frame df;

		zassert_equal(lgps_ble_data_parse(lgps_vec_data_chunk, sizeof(lgps_vec_data_chunk),
						  &df),
			      0, "parse golden 0x5A data frame");
		zassert_equal(df.stream, (uint8_t)LGPS_STREAM_TRAJECTORY, "stream id");
		zassert_equal(df.flags, (uint8_t)LGPS_BLE_DATA_FLAG_LAST_CHUNK, "last chunk flag");
		zassert_equal(df.transfer_id, 42U, "transfer_id");
		zassert_equal(df.cursor, 252U, "cursor");
		zassert_equal(df.payload_len, 224U, "7 records = 224 bytes");
	}

	/* Decode golden compressed LGD1 day package via real lgps_day_pkg_decode(). */
	{
		uint8_t scratch[256];
		uint8_t out[256];
		struct lgps_day_header hdr;
		size_t out_len = 0U;

		zassert_equal(lgps_day_pkg_decode(lgps_vec_day_hs, sizeof(lgps_vec_day_hs),
						  scratch, &hdr, out, sizeof(out), &out_len),
			      0, "decode golden day_hs");
		zassert_equal(hdr.record_count, 8U, "day_hs has 8 records");
		zassert_equal(out_len, 256U, "decoded length 256 B");
		zassert_equal(lgps_crc32_iso_hdlc(out, out_len), 0xCC798479U,
			      "decoded golden day_hs CRC-32 matches manifest");
	}
}

ZTEST_SUITE(lifegps_protogen, NULL, NULL, NULL, NULL, NULL);
