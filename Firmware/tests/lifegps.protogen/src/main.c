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

ZTEST(lifegps_protogen, test_record_sizes)
{
	zassert_equal(LGPS_RECORD_TRAJECTORY_SIZE, 32, "trajectory record is 32 B");
	zassert_equal(LGPS_RECORD_BEACON_SIZE, 16, "beacon record is 16 B");
	zassert_equal(sizeof(struct lifegps_record_trajectory), LGPS_RECORD_TRAJECTORY_SIZE,
		      "packed trajectory struct matches its size macro");
	zassert_equal(sizeof(struct lifegps_record_beacon), LGPS_RECORD_BEACON_SIZE,
		      "packed beacon struct matches its size macro");
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
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_SIZE, 48, "day header size");
	zassert_equal(LGPS_PACKAGE_DAY_FOOTER_SIZE, 8, "day footer size");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_MAGIC, 0, "day magic offset");
	zassert_equal(LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16, 46, "header CRC offset");
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

ZTEST(lifegps_protogen, test_opcode_contract)
{
	zassert_equal(LGPS_CMD_GET_CAPS, 0x01, "GET_CAPS opcode");
	zassert_equal(LGPS_CMD_GET_STATUS, 0x02, "GET_STATUS opcode");
	zassert_equal(LGPS_CMD_SET_MODE, 0x10, "SET_MODE opcode");
	zassert_equal(LGPS_CMD_DEBUG_UNLOCK, 0x7F, "DEBUG_UNLOCK opcode");
	zassert_equal(LGPS_STREAM_TRAJECTORY, 0, "trajectory stream id");
	zassert_equal(LGPS_STREAM_BEACON, 1, "beacon stream id");
	zassert_equal(LGPS_TELEMETRY_GNSS, 0x01, "GNSS telemetry id");
}

ZTEST_SUITE(lifegps_protogen, NULL, NULL, NULL, NULL, NULL);
