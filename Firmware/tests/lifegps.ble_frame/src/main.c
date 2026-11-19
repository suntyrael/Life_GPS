/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - BLE wire codec and UUID tests (native_sim).
 *
 * Two invariants are pinned here and nowhere else:
 *
 *   1. the little-endian GATT UUID octets in lifegps/ble_uuid.h are the
 *      byte-reversed form of the canonical strings in
 *      generated/lifegps_protocol.h - a wrong service UUID otherwise shows up
 *      only as "the phone cannot see the device";
 *   2. every Status/command byte offset matches Doc/Software/BLE_Wire_v1.md.
 *      The offsets below are written as literals on purpose: deriving them from
 *      the encoder would make the test agree with a mistake.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <generated/lifegps_protocol.h>
#include <lifegps/ble_frame.h>
#include <lifegps/ble_uuid.h>

/* --- UUID consistency ------------------------------------------------------ */

/* Parse "4C475053-0002-4C47-5053-000000000001" into Bluetooth wire order (the
 * least significant byte first), which is how a 128-bit UUID is transmitted.
 */
static int uuid_to_wire(const char *text, uint8_t out[16])
{
	uint8_t canonical[16];
	size_t digits = 0U;

	if ((text == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	for (const char *cursor = text; *cursor != '\0'; cursor++) {
		uint8_t nibble;

		if (*cursor == '-') {
			continue;
		}

		if (digits >= 32U) {
			return -EINVAL;
		}

		if ((*cursor >= '0') && (*cursor <= '9')) {
			nibble = (uint8_t)(*cursor - '0');
		} else if ((*cursor >= 'A') && (*cursor <= 'F')) {
			nibble = (uint8_t)(*cursor - 'A' + 10);
		} else if ((*cursor >= 'a') && (*cursor <= 'f')) {
			nibble = (uint8_t)(*cursor - 'a' + 10);
		} else {
			return -EINVAL;
		}

		if ((digits % 2U) == 0U) {
			canonical[digits / 2U] = (uint8_t)(nibble << 4);
		} else {
			canonical[digits / 2U] |= nibble;
		}

		digits++;
	}

	if (digits != 32U) {
		return -EINVAL;
	}

	for (size_t index = 0U; index < 16U; index++) {
		out[index] = canonical[15U - index];
	}

	return 0;
}

static const uint8_t uuid_service_wire[16] = { LGPS_UUID_SERVICE_LE };
static const uint8_t uuid_status_wire[16] = { LGPS_UUID_STATUS_LE };
static const uint8_t uuid_control_wire[16] = { LGPS_UUID_CONTROL_LE };
static const uint8_t uuid_log_wire[16] = { LGPS_UUID_LOG_LE };

struct uuid_case {
	const char *canonical;
	const uint8_t *wire;
};

static const struct uuid_case uuid_cases[] = {
	{ LGPS_UUID_SERVICE, uuid_service_wire },
	{ LGPS_UUID_STATUS, uuid_status_wire },
	{ LGPS_UUID_CONTROL, uuid_control_wire },
	{ LGPS_UUID_LOG, uuid_log_wire },
};

ZTEST(lifegps_ble, test_uuid_octets_match_generated_strings)
{
	for (size_t index = 0U; index < ARRAY_SIZE(uuid_cases); index++) {
		uint8_t wire[16];

		zassert_equal(uuid_to_wire(uuid_cases[index].canonical, wire), 0,
			      "cannot parse %s", uuid_cases[index].canonical);
		zassert_mem_equal(wire, uuid_cases[index].wire, sizeof(wire),
				  "%s does not match its little-endian octets",
				  uuid_cases[index].canonical);
	}

	/* The parser itself must reject a malformed string, otherwise the check
	 * above could pass on garbage input.
	 */
	{
		uint8_t wire[16];

		zassert_equal(uuid_to_wire("4C475053-0002-4C47-5053", wire), -EINVAL,
			      "a short UUID string must be rejected");
	}
}

/* --- command frame --------------------------------------------------------- */

ZTEST(lifegps_ble, test_command_frame_roundtrip)
{
	uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t frame[LGPS_BLE_CMD_MAX_FRAME];
	struct lgps_ble_cmd parsed;
	int len;

	len = lgps_ble_cmd_build(0x12U, 0U, 0x1234U, payload, sizeof(payload), frame,
				 sizeof(frame));
	zassert_equal(len, LGPS_FRAME_COMMAND_HEADER_SIZE + 4 + LGPS_FRAME_COMMAND_TRAILER_SIZE,
		      "frame length must be header + payload + crc");

	/* Header layout, Doc/Software/BLE_Wire_v1.md section 3. */
	zassert_equal(frame[0], 0xA5U, "magic");
	zassert_equal(frame[1], 0x01U, "version");
	zassert_equal(frame[2], 0x12U, "opcode");
	zassert_equal(frame[3], 0x00U, "flags");
	zassert_equal(frame[4], 0x34U, "seq low byte (little endian)");
	zassert_equal(frame[5], 0x12U, "seq high byte (little endian)");
	zassert_equal(frame[6], 0x04U, "payload_len low byte");
	zassert_equal(frame[7], 0x00U, "payload_len high byte");

	zassert_equal(lgps_ble_cmd_parse(frame, (size_t)len, &parsed), 0, "parse must succeed");
	zassert_equal(parsed.opcode, 0x12U, "opcode");
	zassert_equal(parsed.seq, 0x1234U, "seq");
	zassert_equal(parsed.payload_len, 4U, "payload_len");
	zassert_mem_equal(parsed.payload, payload, sizeof(payload), "payload body");
}

ZTEST(lifegps_ble, test_command_frame_rejects_damage)
{
	uint8_t payload[2] = { 0x00, 0x02 };
	uint8_t frame[LGPS_BLE_CMD_MAX_FRAME];
	struct lgps_ble_cmd parsed;
	int len;

	/* Empty payload is legal and must yield payload == NULL. */
	len = lgps_ble_cmd_build(0x01U, 0U, 7U, NULL, 0U, frame, sizeof(frame));
	zassert_equal(len, 10, "empty payload frame is 10 bytes");
	zassert_equal(lgps_ble_cmd_parse(frame, (size_t)len, &parsed), 0, "parse");
	zassert_is_null(parsed.payload, "empty payload must be NULL");

	len = lgps_ble_cmd_build(0x30U, 0U, 1U, payload, sizeof(payload), frame, sizeof(frame));

	/* Corrupted CRC. */
	{
		uint8_t copy[LGPS_BLE_CMD_MAX_FRAME];

		memcpy(copy, frame, (size_t)len);
		copy[(size_t)len - 1] ^= 0xFFU;
		zassert_equal(lgps_ble_cmd_parse(copy, (size_t)len, &parsed), -EBADMSG,
			      "bad CRC must be rejected");
	}

	/* Truncated frame: the length byte no longer matches the buffer. */
	zassert_equal(lgps_ble_cmd_parse(frame, (size_t)len - 1, &parsed), -EBADMSG,
		      "truncated frame must be rejected");

	/* Wrong magic and wrong version. */
	{
		uint8_t copy[LGPS_BLE_CMD_MAX_FRAME];

		memcpy(copy, frame, (size_t)len);
		copy[0] = 0x00U;
		zassert_equal(lgps_ble_cmd_parse(copy, (size_t)len, &parsed), -EBADMSG, "magic");

		memcpy(copy, frame, (size_t)len);
		copy[1] = 0x02U;
		zassert_equal(lgps_ble_cmd_parse(copy, (size_t)len, &parsed), -EBADMSG, "version");
	}

	/* A payload longer than the protocol allows is not buildable. */
	{
		uint8_t big[LGPS_PROTO_MAX_COMMAND_PAYLOAD + 1U] = { 0 };

		zassert_equal(lgps_ble_cmd_build(0x01U, 0U, 0U, big, (uint16_t)sizeof(big), frame,
						 sizeof(frame)),
			      -EINVAL, "oversized payload");
	}
}

/* --- status frame ---------------------------------------------------------- */

ZTEST(lifegps_ble, test_status_offsets_match_wire_doc)
{
	struct lgps_ble_status status = {
		.flags = 0x8DU,
		.gnss_state = 2U,
		.fix_type = 3U,
		.satellites = 11U,
		.key_levels = 0x01U,
		.soc_pct = LGPS_BLE_SOC_UNKNOWN,
		.pdop_1e2 = 0x0142U,
		.speed_cms = 0x0234U,
		.heading_ddeg = 0x0403U,
		.lat_e7 = 0x0A0B0C0D,
		.lon_e7 = -0x01020304,
		.hmsl_mm = -1234,
		.bat_mv = 4012,
		.utc_epoch_s = 0x11223344U,
		.uptime_ms = 0x55667788U,
		.gnss_age_ms = 0x00000064U,
		.steps = 0x000003E8U,
		.acc_lsb = { 0x0102, -2, 0x7FFF },
		.gyro_lsb = { 0x0304, -4, 0x7FFE },
		.mag_lsb = { 0x0506, -6, 0x7FFD },
		.baro_press_raw = 0x00ABCDEFU,
		.baro_temp_raw = 0x000FFFFFU,
		.ring_sector_seq = 0x00000007U,
		.ring_records = 0x00001234U,
		.gravity_mg = { 0x0064, -0x0064, 0x03E8 },
		.linear_mg = { 0x0001, -0x0001, 0x0002 },
		.imu_temp_cc = 2345,
		.mag_temp_cc = -125,
		.sensor_flags = 0x0FU,
	};
	uint8_t out[LGPS_BLE_STATUS_SIZE];
	int len;

	len = lgps_ble_status_encode(&status, out, sizeof(out));
	zassert_equal(len, LGPS_BLE_STATUS_SIZE, "status frame size");
	zassert_equal(LGPS_BLE_STATUS_BASE_SIZE, 82, "the v1 fields stay at 0..81");
	zassert_equal(LGPS_BLE_STATUS_SIZE, 100, "appended motion/temperature block");

	/* Offsets are the ones printed in Doc/Software/BLE_Wire_v1.md section 4. */
	zassert_equal(out[0], 0x01U, "off 0 ver");
	zassert_equal(out[1], 0x8DU, "off 1 flags");
	zassert_equal(out[2], 0x02U, "off 2 gnss_state");
	zassert_equal(out[3], 0x03U, "off 3 fix_type");
	zassert_equal(out[4], 0x0BU, "off 4 satellites");
	zassert_equal(out[5], 0x01U, "off 5 key_levels");
	zassert_equal(out[6], 0xFFU, "off 6 soc_pct unknown");
	zassert_equal(out[7], 0x00U, "off 7 reserved");
	zassert_equal(out[8], 0x42U, "off 8 pdop_1e2 low");
	zassert_equal(out[9], 0x01U, "off 8 pdop_1e2 high");
	zassert_equal(out[10], 0x34U, "off 10 speed_cms low");
	zassert_equal(out[11], 0x02U, "off 10 speed_cms high");
	zassert_equal(out[12], 0x03U, "off 12 heading_ddeg low");
	zassert_equal(out[13], 0x04U, "off 12 heading_ddeg high");
	zassert_equal(out[16], 0x0DU, "off 16 lat_e7 byte 0");
	zassert_equal(out[19], 0x0AU, "off 16 lat_e7 byte 3");
	zassert_equal(out[20], 0xFCU, "off 20 lon_e7 byte 0");
	zassert_equal(out[23], 0xFEU, "off 20 lon_e7 byte 3 (two's complement)");
	zassert_equal(out[24], 0x2EU, "off 24 hmsl_mm byte 0");
	zassert_equal(out[27], 0xFFU, "off 24 hmsl_mm byte 3");
	zassert_equal(out[28], 0xACU, "off 28 bat_mv byte 0");
	zassert_equal(out[29], 0x0FU, "off 28 bat_mv byte 1");
	zassert_equal(out[32], 0x44U, "off 32 utc_epoch_s byte 0");
	zassert_equal(out[36], 0x88U, "off 36 uptime_ms byte 0");
	zassert_equal(out[40], 100U, "off 40 gnss_age_ms");
	zassert_equal(out[44], 0xE8U, "off 44 steps byte 0");
	zassert_equal(out[45], 0x03U, "off 44 steps byte 1");
	zassert_equal(out[48], 0x02U, "off 48 acc_lsb[0] low");
	zassert_equal(out[49], 0x01U, "off 48 acc_lsb[0] high");
	zassert_equal(out[50], 0xFEU, "off 50 acc_lsb[1] negative");
	zassert_equal(out[54], 0x04U, "off 54 gyro_lsb[0] low");
	zassert_equal(out[60], 0x06U, "off 60 mag_lsb[0] low");
	zassert_equal(out[66], 0xEFU, "off 66 baro_press_raw byte 0");
	zassert_equal(out[69], 0x00U, "off 66 baro_press_raw byte 3");
	zassert_equal(out[70], 0xFFU, "off 70 baro_temp_raw byte 0");
	zassert_equal(out[74], 0x07U, "off 74 ring_sector_seq");
	zassert_equal(out[78], 0x34U, "off 78 ring_records byte 0");

	/* Appended block, offsets 82..99. */
	zassert_equal(out[82], 0x64U, "off 82 gravity_mg[0] low");
	zassert_equal(out[83], 0x00U, "off 82 gravity_mg[0] high");
	zassert_equal(out[84], 0x9CU, "off 84 gravity_mg[1] negative");
	zassert_equal(out[85], 0xFFU, "off 84 gravity_mg[1] high byte");
	zassert_equal(out[86], 0xE8U, "off 86 gravity_mg[2] low");
	zassert_equal(out[87], 0x03U, "off 86 gravity_mg[2] high");
	zassert_equal(out[88], 0x01U, "off 88 linear_mg[0] low");
	zassert_equal(out[89], 0x00U, "off 88 linear_mg[0] high");
	zassert_equal(out[94], 0x29U, "off 94 imu_temp_cc low");
	zassert_equal(out[95], 0x09U, "off 94 imu_temp_cc high");
	zassert_equal(out[96], 0x83U, "off 96 mag_temp_cc low (negative)");
	zassert_equal(out[97], 0xFFU, "off 96 mag_temp_cc high");
	zassert_equal(out[98], 0x0FU, "off 98 sensor_flags");
	zassert_equal(out[99], 0x00U, "off 99 reserved");

	/* A 240-byte ATT payload cannot be written into an 81-byte buffer. */
	zassert_equal(lgps_ble_status_encode(&status, out, LGPS_BLE_STATUS_SIZE - 1U), -ENOMEM,
		      "short output buffer");
}

/* --- caps / flash info ----------------------------------------------------- */

ZTEST(lifegps_ble, test_caps_and_flash_info_layout)
{
	struct lgps_ble_caps caps = {
		.caps = LGPS_BLE_CAP_STATUS_NOTIFY | LGPS_BLE_CAP_LOG_NOTIFY | LGPS_BLE_CAP_POI |
			LGPS_BLE_CAP_FLASH_INFO | LGPS_BLE_CAP_REBOOT |
			LGPS_BLE_CAP_DEBUG_UNLOCK,
		.telemetry_mask = 0U,
		.att_mtu = (uint16_t)LGPS_PROTO_MTU,
	};
	struct lgps_ble_flash_info info = {
		.flash_bytes = 0x04000000U,
		.ota_slot1_bytes = 0x00100000U,
		.traj_bytes = 0x03F00000U,
		.sector_seq = 3U,
		.records_written = 400U,
		.records_in_sector = 17U,
		.erase_count_max = 2U,
		.sector_count = 16U,
	};
	uint8_t caps_buf[LGPS_BLE_CAPS_SIZE];
	uint8_t info_buf[LGPS_BLE_FLASH_INFO_SIZE];
	int len;

	len = lgps_ble_caps_encode(&caps, caps_buf, sizeof(caps_buf));
	zassert_equal(len, LGPS_BLE_CAPS_SIZE, "caps size");
	zassert_equal(LGPS_BLE_CAPS_SIZE, 24, "caps stays 24 bytes");
	zassert_equal(caps_buf[0], 0x01U, "caps ver");
	zassert_equal(caps_buf[1], (uint8_t)LGPS_PROTO_VERSION_MAJOR, "proto major");
	zassert_equal(caps_buf[4], (uint8_t)LGPS_BLE_FW_VERSION_MAJOR, "fw major");
	zassert_equal(caps_buf[8], 0xF7U, "att_mtu low byte (247)");
	zassert_equal(caps_buf[9], 0x00U, "att_mtu high byte");
	zassert_equal(caps_buf[10], (uint8_t)(LGPS_PROTO_MAX_COMMAND_PAYLOAD & 0xFFU),
		      "max_cmd_payload low");
	zassert_equal(caps_buf[12],
		      (uint8_t)((LGPS_BLE_CAP_STATUS_NOTIFY | LGPS_BLE_CAP_LOG_NOTIFY |
				 LGPS_BLE_CAP_POI | LGPS_BLE_CAP_FLASH_INFO |
				 LGPS_BLE_CAP_REBOOT | LGPS_BLE_CAP_DEBUG_UNLOCK) &
				0xFFU),
		      "caps bitmap");
	zassert_equal(caps_buf[16], 0x00U, "telemetry_mask");
	zassert_equal(caps_buf[20], (uint8_t)LGPS_BLE_STATUS_SIZE, "status_size");

	len = lgps_ble_flash_info_encode(&info, info_buf, sizeof(info_buf));
	zassert_equal(len, LGPS_BLE_FLASH_INFO_SIZE, "flash info size");
	zassert_equal(LGPS_BLE_FLASH_INFO_SIZE, 24, "flash info stays 24 bytes");
	zassert_equal(info_buf[0], 0x00U, "flash_bytes byte 0");
	zassert_equal(info_buf[3], 0x04U, "flash_bytes byte 3 (64 MB)");
	zassert_equal(info_buf[20], 17U, "records_in_sector");
	zassert_equal(info_buf[22], 16U, "sector_count");
}

/* --- telemetry packets ----------------------------------------------------- */

ZTEST(lifegps_ble, test_telemetry_imu_packet_layout)
{
	const struct lgps_ble_telem_imu imu = {
		.acc_lsb = { 0x0102, -2, 0x7FFF },
		.gyro_lsb = { 0x0304, -4, 0x7FFE },
		.quat_q15 = { 32767, 0, -16384, 8192 },
	};
	uint8_t out[LGPS_BLE_TELEM_IMU_SIZE];
	int len;

	len = lgps_ble_telem_imu_encode(0x1234U, &imu, out, sizeof(out));

	zassert_equal(len, LGPS_BLE_TELEM_IMU_SIZE, "IMU telemetry packet length");
	zassert_equal(LGPS_BLE_TELEM_IMU_SIZE, 24, "IMU telemetry packet stays 24 bytes");

	/* Header, then the documented payload offsets. */
	zassert_equal(out[0], 0x02U, "telemetry id IMU");
	zassert_equal(out[1], 0x01U, "telemetry version");
	zassert_equal(out[2], 0x34U, "seq low byte");
	zassert_equal(out[3], 0x12U, "seq high byte");

	zassert_equal(out[4], 0x02U, "acc[0] low");
	zassert_equal(out[5], 0x01U, "acc[0] high");
	zassert_equal(out[6], 0xFEU, "acc[1] negative");
	zassert_equal(out[10], 0x04U, "gyro[0] low");
	zassert_equal(out[12], 0xFCU, "gyro[1] negative");
	zassert_equal(out[16], 0xFFU, "quat w low byte (32767)");
	zassert_equal(out[17], 0x7FU, "quat w high byte (32767)");
	zassert_equal(out[18], 0x00U, "quat x is zero");
	zassert_equal(out[20], 0x00U, "quat y low byte (-16384)");
	zassert_equal(out[21], 0xC0U, "quat y high byte (-16384)");
	zassert_equal(out[22], 0x00U, "quat z low byte (8192)");
	zassert_equal(out[23], 0x20U, "quat z high byte (8192)");

	zassert_equal(lgps_ble_telem_imu_encode(0U, &imu, out, LGPS_BLE_TELEM_IMU_SIZE - 1U),
		      -ENOMEM, "short output buffer must be refused");
	zassert_equal(lgps_ble_telem_imu_encode(0U, NULL, out, sizeof(out)), -EINVAL,
		      "NULL payload must be refused");
}

ZTEST(lifegps_ble, test_telemetry_pdr_packet_layout)
{
	const struct lgps_ble_telem_pdr pdr = {
		.x_mm = 0x000186A0,  /* 100 000 mm east */
		.y_mm = -0x00000064, /* -100 mm north */
		.heading_ddeg = 3599,
		.steps = 1234,
	};
	uint8_t out[LGPS_BLE_TELEM_PDR_SIZE];
	int len;

	len = lgps_ble_telem_pdr_encode(7U, &pdr, out, sizeof(out));

	zassert_equal(len, LGPS_BLE_TELEM_PDR_SIZE, "PDR telemetry packet length");
	zassert_equal(LGPS_BLE_TELEM_PDR_SIZE, 16, "PDR telemetry packet stays 16 bytes");
	zassert_equal(out[0], 0x05U, "telemetry id PDR");
	zassert_equal(out[2], 7U, "seq low byte");
	zassert_equal(out[4], 0xA0U, "x_mm byte 0");
	zassert_equal(out[7], 0x00U, "x_mm byte 3");
	zassert_equal(out[8], 0x9CU, "y_mm byte 0 (-100)");
	zassert_equal(out[11], 0xFFU, "y_mm byte 3 (-100, two's complement)");
	zassert_equal(out[12], 0x0FU, "heading_ddeg low byte (3599)");
	zassert_equal(out[13], 0x0EU, "heading_ddeg high byte (3599)");
	zassert_equal(out[14], 0xD2U, "steps low byte (1234)");
	zassert_equal(out[15], 0x04U, "steps high byte (1234)");
}

ZTEST(lifegps_ble, test_telemetry_gnss_packet_layout)
{
	/* One distinct constant per field so a swapped offset cannot pass. */
	const struct lgps_ble_telem_gnss gnss = {
		.state = 2U,
		.fix_type = 3U,
		.satellites = 11U,
		.flags = LGPS_BLE_TELEM_GNSS_FLAG_FIX_VALID | LGPS_BLE_TELEM_GNSS_FLAG_UTC_VALID,
		.pdop_1e2 = 0x0123U,
		.age_ms = 0x11223344U,
		.rx_bytes = 0x55667788U,
		.ubx_frames = 9U,
		.ubx_bad_checksum = 1U,
		.nmea_starts = 0x42U,
		.rx_dropped = 0U,
		.baud = 38400U,
	};
	uint8_t out[LGPS_BLE_TELEM_GNSS_SIZE];
	int len;

	len = lgps_ble_telem_gnss_encode(0x1234U, &gnss, out, sizeof(out));

	zassert_equal(len, LGPS_BLE_TELEM_GNSS_SIZE, "GNSS telemetry packet length");
	zassert_equal(LGPS_BLE_TELEM_GNSS_SIZE, 38, "GNSS telemetry packet stays 38 bytes");
	zassert_equal(out[0], 0x01U, "telemetry id GNSS");
	zassert_equal(out[1], 0x01U, "telemetry version");
	zassert_equal(out[2], 0x34U, "seq low byte");
	zassert_equal(out[3], 0x12U, "seq high byte");

	zassert_equal(out[4], 2U, "gnss_state");
	zassert_equal(out[5], 3U, "fix_type");
	zassert_equal(out[6], 11U, "satellites");
	zassert_equal(out[7], 0x03U, "flags fix+utc");
	zassert_equal(out[8], 0x23U, "pdop low byte");
	zassert_equal(out[9], 0x01U, "pdop high byte");
	zassert_equal(out[10], 0x44U, "age_ms byte 0");
	zassert_equal(out[13], 0x11U, "age_ms byte 3");
	zassert_equal(out[14], 0x88U, "rx_bytes byte 0");
	zassert_equal(out[17], 0x55U, "rx_bytes byte 3");
	zassert_equal(out[18], 9U, "ubx_frames");
	zassert_equal(out[22], 1U, "ubx_bad_checksum");
	zassert_equal(out[26], 0x42U, "nmea_starts");
	zassert_equal(out[30], 0U, "rx_dropped");
	zassert_equal(out[34], 0x00U, "baud byte 0");
	zassert_equal(out[35], 0x96U, "baud byte 1 (38400)");

	zassert_equal(lgps_ble_telem_gnss_encode(0U, NULL, out, sizeof(out)), -EINVAL,
		      "NULL payload must be refused");
	zassert_equal(lgps_ble_telem_gnss_encode(0U, &gnss, out, LGPS_BLE_TELEM_GNSS_SIZE - 1U),
		      -ENOMEM, "short output buffer must be refused");
}

ZTEST(lifegps_ble, test_telemetry_generic_encoder_bounds)
{
	uint8_t payload[2] = { 0xAA, 0xBB };
	uint8_t out[LGPS_BLE_TELEM_HEADER_SIZE + 2U];

	zassert_equal(lgps_ble_telem_encode(0x07U, 1U, payload, sizeof(payload), out, sizeof(out)),
		      (int)(LGPS_BLE_TELEM_HEADER_SIZE + 2U), "generic telemetry packet");
	zassert_equal(out[0], 0x07U, "id");
	zassert_equal(out[4], 0xAAU, "payload byte 0");
	zassert_equal(out[5], 0xBBU, "payload byte 1");

	zassert_equal(lgps_ble_telem_encode(0x07U, 1U, payload, sizeof(payload), out,
					    sizeof(out) - 1U),
		      -ENOMEM, "short output buffer");
}

ZTEST(lifegps_ble, test_telemetry_sat_packet_layout)
{
	const struct lgps_ble_telem_sat_entry entries[2] = {
		{ .gnss_id = 0U, .sv_id = 7U, .cno = 42U, .elev = 55, .state = 0x0CU },
		{ .gnss_id = 3U, .sv_id = 21U, .cno = 28U, .elev = -12, .state = 0x01U },
	};
	uint8_t out[LGPS_BLE_TELEM_HEADER_SIZE + LGPS_BLE_TELEM_SAT_HEADER +
		    (2U * LGPS_BLE_TELEM_SAT_ENTRY)];
	int len;

	len = lgps_ble_telem_sat_encode(0x0005U, entries, 2U, 2U, out, sizeof(out));

	zassert_equal(len, (int)sizeof(out), "satellite packet length");
	zassert_equal(out[0], 0x09U, "telemetry id SAT");
	zassert_equal(out[4], 2U, "count");
	zassert_equal(out[5], 2U, "total");
	zassert_equal(out[6], 0U, "reserved low");
	zassert_equal(out[8], 0U, "entry 0 gnssId");
	zassert_equal(out[9], 7U, "entry 0 svId");
	zassert_equal(out[10], 42U, "entry 0 cno");
	zassert_equal(out[11], 55U, "entry 0 elev");
	zassert_equal(out[12], 0x0CU, "entry 0 state (quality 4 | used)");
	zassert_equal(out[13], 3U, "entry 1 gnssId");
	zassert_equal(out[16], 0xF4U, "entry 1 elev is i8 (-12)");
	zassert_equal(out[17], 0x01U, "entry 1 state");

	zassert_equal(lgps_ble_telem_sat_encode(0U, NULL, 1U, 1U, out, sizeof(out)), -EINVAL,
		      "NULL entries with a non-zero count");
	zassert_equal(lgps_ble_telem_sat_encode(0U, entries, LGPS_BLE_TELEM_SAT_MAX + 1U, 0U, out,
					       sizeof(out)),
		      -EINVAL, "more rows than the format allows");
}

ZTEST(lifegps_ble, test_telemetry_nmea_packet_layout)
{
	static const char line[] =
		"$GNGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*4C";
	uint8_t out[LGPS_BLE_TELEM_HEADER_SIZE + 1U + LGPS_BLE_TELEM_NMEA_MAX];
	size_t text_len = sizeof(line) - 1U;
	size_t star = 0U;
	int len;

	/* Locate the checksum star in the source, so the assertion cannot drift with
	 * the test string.
	 */
	for (size_t index = 0U; index < text_len; index++) {
		if (line[index] == '*') {
			star = index;
		}
	}

	zassert_true(star > 0U, "test sentence must carry a checksum");

	len = lgps_ble_telem_nmea_encode(0x0007U, line, (uint8_t)text_len, out, sizeof(out));

	zassert_equal(len, (int)(LGPS_BLE_TELEM_HEADER_SIZE + sizeof(line)), "NMEA packet length");
	zassert_equal(out[0], 0x08U, "telemetry id NMEA");
	zassert_equal(out[2], 7U, "seq low byte");
	zassert_equal(out[4], (uint8_t)text_len, "text length prefix");
	zassert_equal(out[5], (uint8_t)'$', "text starts with the sentence delimiter");
	zassert_equal(out[5 + star], (uint8_t)'*', "checksum star keeps its position");
	zassert_equal(out[5 + text_len - 1U], (uint8_t)line[text_len - 1U],
		      "last text byte round-trips");

	zassert_equal(lgps_ble_telem_nmea_encode(0U, NULL, 4U, out, sizeof(out)), -EINVAL,
		      "NULL line");
	zassert_equal(lgps_ble_telem_nmea_encode(0U, line, 0U, out, sizeof(out)), -EINVAL,
		      "empty line");
}

ZTEST_SUITE(lifegps_ble, NULL, NULL, NULL, NULL, NULL);
