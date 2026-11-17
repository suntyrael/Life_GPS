/*
 * Life_GPS - trajectory record codec tests (native_sim).
 *
 * The record layout is owned by protocol/lifegps_protocol.yaml and reaches this
 * test only through <generated/lifegps_protocol.h>. Each assertion below pins
 * one frozen offset or one serialised field, so a layout change cannot pass
 * silently.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <lifegps/crc.h>
#include <lifegps/traj.h>

/* Bytes covered by the record CRC-8 (all fields before checksum). */
#define RECORD_CRC_BYTES LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM

_Static_assert(LGPS_RECORD_TRAJECTORY_SIZE == 32, "trajectory record must stay 32 bytes");
_Static_assert(RECORD_CRC_BYTES == 31, "record CRC must cover the first 31 bytes");

/* Known-good sample: every field is distinct and exercises sign handling. */
static const struct lgps_traj_sample test_sample = {
	.seq_id = 0x11223344U,
	.timestamp_s = 0x01020304U,
	.subsecond_ms = 250U,
	.lat_e7 = 375667000,
	.lon_e7 = -1224194000,
	.alt_m = -12,
	.speed_cms = 150U,
	.heading_ddeg = 900U,
	.step_count = 1234U,
	.hdop_d1 = 8U,
	.satellites = 11U,
	.status_flags = (uint8_t)(LGPS_FLAG_FIX_VALID | LGPS_FLAG_NAV_SRC),
	.battery_pct = 87U,
	.floor_level = -2,
};

/*
 * Check that @p value is stored little-endian at @p offset_byte, which must be
 * one of the generated LGPS_RECORD_TRAJECTORY_OFF_* macros.
 */
static void expect_le_bytes(const uint8_t *raw, size_t offset_byte, uint32_t value,
			    size_t len_bytes)
{
	for (size_t index = 0U; index < len_bytes; index++) {
		uint8_t expected = (uint8_t)((value >> (8U * index)) & 0xFFU);

		zassert_equal(raw[offset_byte + index], expected,
			      "byte %zu of the field at offset %zu is wrong", index,
			      offset_byte);
	}
}

ZTEST(lifegps_traj, test_field_offsets)
{
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID, 0U, "seq_id offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP, 4U, "timestamp offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_SUBSECOND_MS, 8U, "subsecond_ms offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_LATITUDE, 10U, "latitude offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_LONGITUDE, 14U, "longitude offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_ALTITUDE, 18U, "altitude offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_SPEED_CMS, 20U, "speed_cms offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_HEADING, 22U, "heading offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_STEP_COUNT, 24U, "step_count offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_HDOP, 26U, "hdop offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_SATELLITES, 27U, "satellites offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_STATUS_FLAGS, 28U, "status_flags offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_BATTERY_LEVEL, 29U, "battery_level offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_FLOOR_LEVEL, 30U, "floor_level offset");
	zassert_equal(LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM, 31U, "checksum offset");
	zassert_equal(sizeof(struct lifegps_record_trajectory), 32U, "packed struct size");
	zassert_equal(sizeof(struct lifegps_record_trajectory), LGPS_RECORD_TRAJECTORY_SIZE,
		      "packed struct matches the generated size");
}

ZTEST(lifegps_traj, test_encode_layout)
{
	uint8_t raw[LGPS_RECORD_TRAJECTORY_SIZE] = { 0 };

	memset(raw, 0, sizeof(raw));
	zassert_equal(lgps_traj_encode(&test_sample, raw), 0, "encode must succeed");

	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID, test_sample.seq_id, 4U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP, test_sample.timestamp_s, 4U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_SUBSECOND_MS,
			test_sample.subsecond_ms, 2U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_LATITUDE,
			(uint32_t)test_sample.lat_e7, 4U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_LONGITUDE,
			(uint32_t)test_sample.lon_e7, 4U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_ALTITUDE,
			(uint16_t)test_sample.alt_m, 2U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_SPEED_CMS, test_sample.speed_cms, 2U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_HEADING,
			test_sample.heading_ddeg, 2U);
	expect_le_bytes(raw, LGPS_RECORD_TRAJECTORY_OFF_STEP_COUNT, test_sample.step_count, 2U);
	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_HDOP], test_sample.hdop_d1, "hdop byte");
	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_SATELLITES], test_sample.satellites,
		      "satellites byte");
	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_STATUS_FLAGS], test_sample.status_flags,
		      "status_flags byte");
	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_BATTERY_LEVEL], test_sample.battery_pct,
		      "battery_level byte");
	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_FLOOR_LEVEL],
		      (uint8_t)test_sample.floor_level, "floor_level byte");

	zassert_equal(raw[LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM], lgps_crc8(raw, RECORD_CRC_BYTES),
		      "checksum must be the CRC-8 of the first 31 bytes");
	zassert_true(lgps_traj_record_valid(raw), "freshly encoded record must validate");
}

ZTEST(lifegps_traj, test_round_trip)
{
	uint8_t raw[LGPS_RECORD_TRAJECTORY_SIZE] = { 0 };
	struct lgps_traj_sample decoded = { 0 };

	zassert_equal(lgps_traj_encode(&test_sample, raw), 0, "encode must succeed");
	zassert_equal(lgps_traj_decode(raw, &decoded), 0, "decode must succeed");

	zassert_equal(decoded.seq_id, test_sample.seq_id, "seq_id round trip");
	zassert_equal(decoded.timestamp_s, test_sample.timestamp_s, "timestamp round trip");
	zassert_equal(decoded.subsecond_ms, test_sample.subsecond_ms, "subsecond_ms round trip");
	zassert_equal(decoded.lat_e7, test_sample.lat_e7, "latitude round trip");
	zassert_equal(decoded.lon_e7, test_sample.lon_e7, "longitude round trip");
	zassert_equal(decoded.alt_m, test_sample.alt_m, "altitude round trip");
	zassert_equal(decoded.speed_cms, test_sample.speed_cms, "speed_cms round trip");
	zassert_equal(decoded.heading_ddeg, test_sample.heading_ddeg, "heading round trip");
	zassert_equal(decoded.step_count, test_sample.step_count, "step_count round trip");
	zassert_equal(decoded.hdop_d1, test_sample.hdop_d1, "hdop round trip");
	zassert_equal(decoded.satellites, test_sample.satellites, "satellites round trip");
	zassert_equal(decoded.status_flags, test_sample.status_flags, "status_flags round trip");
	zassert_equal(decoded.battery_pct, test_sample.battery_pct, "battery round trip");
	zassert_equal(decoded.floor_level, test_sample.floor_level, "floor_level round trip");
}

ZTEST(lifegps_traj, test_corrupt_payload)
{
	uint8_t raw[LGPS_RECORD_TRAJECTORY_SIZE] = { 0 };
	struct lgps_traj_sample decoded = { 0 };

	zassert_equal(lgps_traj_encode(&test_sample, raw), 0, "encode must succeed");
	raw[LGPS_RECORD_TRAJECTORY_OFF_LATITUDE] ^= 0x01U;

	zassert_false(lgps_traj_record_valid(raw), "corrupted record must not validate");
	zassert_equal(lgps_traj_decode(raw, &decoded), -EBADMSG, "decode must reject a bad CRC");
}

ZTEST(lifegps_traj, test_invalid_input)
{
	struct lgps_traj_sample sample = test_sample;
	uint8_t raw[LGPS_RECORD_TRAJECTORY_SIZE] = { 0 };

	sample.subsecond_ms = 1000U;
	zassert_equal(lgps_traj_encode(&sample, raw), -EINVAL, "subsecond_ms 1000 rejected");

	sample = test_sample;
	sample.heading_ddeg = 3600U;
	zassert_equal(lgps_traj_encode(&sample, raw), -EINVAL, "heading_ddeg 3600 rejected");

	sample = test_sample;
	sample.battery_pct = 101U;
	zassert_equal(lgps_traj_encode(&sample, raw), -EINVAL, "battery_pct 101 rejected");

	sample = test_sample;
	sample.subsecond_ms = 999U;
	sample.heading_ddeg = 3599U;
	sample.battery_pct = 100U;
	zassert_equal(lgps_traj_encode(&sample, raw), 0, "upper bounds are accepted");

	zassert_equal(lgps_traj_encode(NULL, raw), -EINVAL, "NULL sample rejected");
	zassert_equal(lgps_traj_encode(&test_sample, NULL), -EINVAL, "NULL raw rejected");
	zassert_equal(lgps_traj_decode(NULL, &sample), -EINVAL, "NULL raw rejected");
	zassert_equal(lgps_traj_decode(raw, NULL), -EINVAL, "NULL sample rejected");
	zassert_false(lgps_traj_record_valid(NULL), "NULL record is not valid");
}

ZTEST_SUITE(lifegps_traj, NULL, NULL, NULL, NULL, NULL);
