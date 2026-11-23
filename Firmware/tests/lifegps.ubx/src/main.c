/*
 * Life_GPS firmware - ztest for the UBX framing module.
 *
 * The golden frames and payload offsets are taken from
 * Doc/Hardware/Datasheets/UBLOX/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf,
 * independently of the implementation under test.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <lifegps/ubx.h>

/* Synthetic UBX-NAV-PVT payload values: one distinct constant per field. */
#define TEST_ITOW_MS 0x11223344U
#define TEST_YEAR 2026U
#define TEST_MONTH 11U
#define TEST_DAY 17U
#define TEST_HOUR 12U
#define TEST_MINUTE 34U
#define TEST_SECOND 56U
#define TEST_VALID 0x07U
#define TEST_TACC_NS 0x0A0B0C0DU
#define TEST_NANO_NS (-123456789)
#define TEST_FIX_TYPE 3U
#define TEST_FLAGS 0x21U
#define TEST_FLAGS2 0x5AU
#define TEST_NUM_SV 17U
#define TEST_LON_E7 (-1221234567)
#define TEST_LAT_E7 374567890
#define TEST_HEIGHT_MM (-12345)
#define TEST_HMSL_MM 67890
#define TEST_HACC_MM 1500U
#define TEST_VACC_MM 2500U
#define TEST_VEL_N_MMS (-300)
#define TEST_VEL_E_MMS 400
#define TEST_VEL_D_MMS (-500)
#define TEST_GSPEED_MMS 600
#define TEST_HEAD_MOT_1E5 1234567
#define TEST_SACC_MMS 700U
#define TEST_HEADACC_1E5 800U
#define TEST_PDOP_1E2 123U
#define TEST_FLAGS3 0x001FU

#define NAV_PVT_FRAME_LEN                                                                    \
	(LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_PVT_PAYLOAD_LEN + LGPS_UBX_TRAILER_LEN)
#define PMREQ_FRAME_LEN                                                                      \
	(LGPS_UBX_HEADER_LEN + LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN + LGPS_UBX_TRAILER_LEN)

static void put_le16(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)(value & 0xFFU);
	dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
	dst[0] = (uint8_t)(value & 0xFFU);
	dst[1] = (uint8_t)((value >> 8) & 0xFFU);
	dst[2] = (uint8_t)((value >> 16) & 0xFFU);
	dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t test_get_le32(const uint8_t *src)
{
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

static void make_nav_pvt_payload(uint8_t payload[LGPS_UBX_NAV_PVT_PAYLOAD_LEN])
{
	memset(payload, 0, LGPS_UBX_NAV_PVT_PAYLOAD_LEN);
	put_le32(&payload[0], TEST_ITOW_MS);
	put_le16(&payload[4], TEST_YEAR);
	payload[6] = TEST_MONTH;
	payload[7] = TEST_DAY;
	payload[8] = TEST_HOUR;
	payload[9] = TEST_MINUTE;
	payload[10] = TEST_SECOND;
	payload[11] = TEST_VALID;
	put_le32(&payload[12], TEST_TACC_NS);
	put_le32(&payload[16], (uint32_t)TEST_NANO_NS);
	payload[20] = TEST_FIX_TYPE;
	payload[21] = TEST_FLAGS;
	payload[22] = TEST_FLAGS2;
	payload[23] = TEST_NUM_SV;
	put_le32(&payload[24], (uint32_t)TEST_LON_E7);
	put_le32(&payload[28], (uint32_t)TEST_LAT_E7);
	put_le32(&payload[32], (uint32_t)TEST_HEIGHT_MM);
	put_le32(&payload[36], (uint32_t)TEST_HMSL_MM);
	put_le32(&payload[40], TEST_HACC_MM);
	put_le32(&payload[44], TEST_VACC_MM);
	put_le32(&payload[48], (uint32_t)TEST_VEL_N_MMS);
	put_le32(&payload[52], (uint32_t)TEST_VEL_E_MMS);
	put_le32(&payload[56], (uint32_t)TEST_VEL_D_MMS);
	put_le32(&payload[60], (uint32_t)TEST_GSPEED_MMS);
	put_le32(&payload[64], (uint32_t)TEST_HEAD_MOT_1E5);
	put_le32(&payload[68], TEST_SACC_MMS);
	put_le32(&payload[72], TEST_HEADACC_1E5);
	put_le16(&payload[76], TEST_PDOP_1E2);
	put_le16(&payload[78], TEST_FLAGS3);
}

static void build_nav_pvt_frame(uint8_t frame[NAV_PVT_FRAME_LEN])
{
	uint8_t payload[LGPS_UBX_NAV_PVT_PAYLOAD_LEN];
	int ret;

	make_nav_pvt_payload(payload);
	ret = lgps_ubx_build(frame, NAV_PVT_FRAME_LEN, LGPS_UBX_CLASS_NAV, LGPS_UBX_ID_NAV_PVT,
			     payload, LGPS_UBX_NAV_PVT_PAYLOAD_LEN);
	zassert_equal(ret, (int)NAV_PVT_FRAME_LEN, "builder must emit the whole NAV-PVT frame");
}

static void feed_expect_ready(const uint8_t *frame, size_t frame_len_bytes,
			      struct lgps_ubx_parser *parser)
{
	for (size_t i = 0U; i < frame_len_bytes - 1U; i++) {
		zassert_equal(lgps_ubx_parser_feed(parser, frame[i]), LGPS_UBX_STATUS_NONE,
			      "frame must not complete before byte %u", (unsigned int)i);
	}
	zassert_equal(lgps_ubx_parser_feed(parser, frame[frame_len_bytes - 1U]),
		      LGPS_UBX_STATUS_MSG_READY, "CK_B byte must complete the frame");
}

ZTEST(lifegps_ubx, test_nav_pvt_roundtrip)
{
	uint8_t frame[NAV_PVT_FRAME_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_nav_pvt pvt;

	build_nav_pvt_frame(frame);
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, sizeof(frame), &parser);

	zassert_equal(parser.msg_class, LGPS_UBX_CLASS_NAV, "class must be NAV");
	zassert_equal(parser.msg_id, LGPS_UBX_ID_NAV_PVT, "id must be NAV-PVT");
	zassert_equal(parser.payload_len, LGPS_UBX_NAV_PVT_PAYLOAD_LEN, "payload must be 92 bytes");

	zassert_equal(lgps_ubx_decode_nav_pvt(&parser, &pvt), 0, "decode must succeed");
	zassert_equal(pvt.itow_ms, TEST_ITOW_MS, "iTOW");
	zassert_equal(pvt.year, TEST_YEAR, "year");
	zassert_equal(pvt.month, TEST_MONTH, "month");
	zassert_equal(pvt.day, TEST_DAY, "day");
	zassert_equal(pvt.hour, TEST_HOUR, "hour");
	zassert_equal(pvt.minute, TEST_MINUTE, "minute");
	zassert_equal(pvt.second, TEST_SECOND, "second");
	zassert_equal(pvt.valid, TEST_VALID, "valid");
	zassert_equal(pvt.tacc_ns, TEST_TACC_NS, "tAcc");
	zassert_equal(pvt.nano_ns, TEST_NANO_NS, "nano");
	zassert_equal(pvt.fix_type, TEST_FIX_TYPE, "fixType");
	zassert_equal(pvt.flags, TEST_FLAGS, "flags");
	zassert_equal(pvt.flags2, TEST_FLAGS2, "flags2");
	zassert_equal(pvt.num_sv, TEST_NUM_SV, "numSV");
	zassert_equal(pvt.lon_e7, TEST_LON_E7, "lon");
	zassert_equal(pvt.lat_e7, TEST_LAT_E7, "lat");
	zassert_equal(pvt.height_mm, TEST_HEIGHT_MM, "height");
	zassert_equal(pvt.hmsl_mm, TEST_HMSL_MM, "hMSL");
	zassert_equal(pvt.hacc_mm, TEST_HACC_MM, "hAcc");
	zassert_equal(pvt.vacc_mm, TEST_VACC_MM, "vAcc");
	zassert_equal(pvt.vel_n_mms, TEST_VEL_N_MMS, "velN");
	zassert_equal(pvt.vel_e_mms, TEST_VEL_E_MMS, "velE");
	zassert_equal(pvt.vel_d_mms, TEST_VEL_D_MMS, "velD");
	zassert_equal(pvt.gspeed_mms, TEST_GSPEED_MMS, "gSpeed");
	zassert_equal(pvt.head_mot_1e5, TEST_HEAD_MOT_1E5, "headMot");
	zassert_equal(pvt.sacc_mms, TEST_SACC_MMS, "sAcc");
	zassert_equal(pvt.headacc_1e5, TEST_HEADACC_1E5, "headAcc");
	zassert_equal(pvt.pdop_1e2, TEST_PDOP_1E2, "pDOP");
	zassert_equal(pvt.flags3, TEST_FLAGS3, "flags3");
}

ZTEST(lifegps_ubx, test_nav_pvt_bad_checksum)
{
	uint8_t frame[NAV_PVT_FRAME_LEN];
	const size_t mutated_payload_offset = 24U;
	struct lgps_ubx_parser parser;

	build_nav_pvt_frame(frame);
	frame[LGPS_UBX_HEADER_LEN + mutated_payload_offset] =
		(uint8_t)(frame[LGPS_UBX_HEADER_LEN + mutated_payload_offset] ^ 0x01U);

	lgps_ubx_parser_reset(&parser);
	for (size_t i = 0U; i < sizeof(frame) - 1U; i++) {
		zassert_equal(lgps_ubx_parser_feed(&parser, frame[i]), LGPS_UBX_STATUS_NONE,
			      "corrupted byte %u must not complete a frame", (unsigned int)i);
	}
	zassert_equal(lgps_ubx_parser_feed(&parser, frame[sizeof(frame) - 1U]),
		      LGPS_UBX_STATUS_BAD_CHECKSUM, "checksum must reject the corrupted payload");
}

ZTEST(lifegps_ubx, test_parser_resynchronises)
{
	static const uint8_t garbage[] = {0x00U, 0xFFU, 0xB5U, 0x00U, 0x62U};
	uint8_t frame[NAV_PVT_FRAME_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_nav_pvt pvt;

	build_nav_pvt_frame(frame);
	lgps_ubx_parser_reset(&parser);
	for (size_t i = 0U; i < sizeof(garbage); i++) {
		zassert_equal(lgps_ubx_parser_feed(&parser, garbage[i]), LGPS_UBX_STATUS_NONE,
			      "garbage byte %u must be ignored", (unsigned int)i);
	}
	feed_expect_ready(frame, sizeof(frame), &parser);
	zassert_equal(lgps_ubx_decode_nav_pvt(&parser, &pvt), 0,
		      "frame after garbage must decode cleanly");
	zassert_equal(pvt.itow_ms, TEST_ITOW_MS, "iTOW after resynchronisation");
}

ZTEST(lifegps_ubx, test_rxm_pmreq_frame)
{
	/* version 0, reserved 0, duration 0 ms, flags backup|force, wakeup extint0. */
	static const uint8_t expected[] = {
		0xB5U, 0x62U, 0x02U, 0x41U, 0x10U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x06U, 0x00U,
		0x00U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U, 0x79U, 0xCBU,
	};
	uint8_t frame[PMREQ_FRAME_LEN];
	int ret;

	ret = lgps_ubx_build_rxm_pmreq(frame, sizeof(frame), 0U, true, true,
				       LGPS_UBX_PMREQ_WAKE_EXTINT0);
	zassert_equal(ret, (int)sizeof(expected), "PMREQ frame must be 24 bytes");
	zassert_equal(sizeof(frame), sizeof(expected), "test buffer must match the golden vector");
	zassert_mem_equal(frame, expected, sizeof(expected),
			  "PMREQ frame must match the golden vector");
}

ZTEST(lifegps_ubx, test_build_enospc)
{
	uint8_t frame[PMREQ_FRAME_LEN - 1U];
	int ret;

	ret = lgps_ubx_build_rxm_pmreq(frame, sizeof(frame), 1000U, false, false,
				       LGPS_UBX_PMREQ_WAKE_UARTRX);
	zassert_equal(ret, -ENOSPC, "one byte short must fail with -ENOSPC");
}

ZTEST(lifegps_ubx, test_rxm_pmreq_duration_clamped)
{
	uint8_t frame[PMREQ_FRAME_LEN];
	uint32_t duration_ms;
	int ret;

	ret = lgps_ubx_build_rxm_pmreq(frame, sizeof(frame),
				       LGPS_UBX_PMREQ_MAX_DURATION_MS + 1000U, false, false, 0U);
	zassert_equal(ret, (int)sizeof(frame), "clamped PMREQ frame must be 24 bytes");
	zassert_equal(frame[LGPS_UBX_HEADER_LEN], 0U, "version must stay 0");
	duration_ms = test_get_le32(&frame[LGPS_UBX_HEADER_LEN + 4U]);
	zassert_equal(duration_ms, LGPS_UBX_PMREQ_MAX_DURATION_MS,
		      "duration must be clamped to 12 days");
}

/*
 * UBX-CFG-RST cold start, assembled by hand from section 3.10.2.1: navBbrMask
 * 0xFFFF at offset 0 (X2), resetMode 0x01 (controlled software reset) at offset 2,
 * reserved 0 at offset 3.
 */
ZTEST(lifegps_ubx, test_build_cfg_rst_cold_start)
{
	static const uint8_t expected[] = {
		0xB5U, 0x62U, 0x06U, 0x04U, 0x04U, 0x00U, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x0DU, 0x5FU,
	};
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_RST_PAYLOAD_LEN +
		      LGPS_UBX_TRAILER_LEN];
	int ret;

	ret = lgps_ubx_build_cfg_rst(frame, sizeof(frame), LGPS_UBX_CFG_RST_BBR_COLD,
				     LGPS_UBX_CFG_RST_MODE_SOFTWARE);
	zassert_equal(ret, (int)sizeof(expected), "CFG-RST frame must be 12 bytes");
	zassert_mem_equal(frame, expected, sizeof(expected),
			  "cold start must be navBbrMask 0xFFFF, resetMode 0x01");
}

ZTEST(lifegps_ubx, test_build_cfg_rst_hot_and_warm)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_RST_PAYLOAD_LEN +
		      LGPS_UBX_TRAILER_LEN];
	uint16_t mask;
	int ret;

	ret = lgps_ubx_build_cfg_rst(frame, sizeof(frame), LGPS_UBX_CFG_RST_BBR_HOT,
				     LGPS_UBX_CFG_RST_MODE_SOFTWARE);
	zassert_equal(ret, (int)sizeof(frame), "hot start frame");
	mask = (uint16_t)(frame[LGPS_UBX_HEADER_LEN] |
			  ((uint16_t)frame[LGPS_UBX_HEADER_LEN + 1U] << 8));
	zassert_equal(mask, 0x0000U, "hot start keeps every BBR section");

	ret = lgps_ubx_build_cfg_rst(frame, sizeof(frame), LGPS_UBX_CFG_RST_BBR_WARM,
				     LGPS_UBX_CFG_RST_MODE_SOFTWARE);
	zassert_equal(ret, (int)sizeof(frame), "warm start frame");
	mask = (uint16_t)(frame[LGPS_UBX_HEADER_LEN] |
			  ((uint16_t)frame[LGPS_UBX_HEADER_LEN + 1U] << 8));
	zassert_equal(mask, 0x0001U, "warm start drops the ephemeris only");

	zassert_equal(lgps_ubx_build_cfg_rst(NULL, sizeof(frame), 0U, 0x01U), -EINVAL,
		      "NULL buffer must be refused");
	zassert_equal(lgps_ubx_build_cfg_rst(frame, sizeof(frame), 0U, 0x7FU), -EINVAL,
		      "an undefined resetMode must be refused");
}

/* Golden UBX-CFG-VALSET / VALGET frames for key 0x20910007 (NAV-PVT rate on
 * UART1), assembled by hand from section 3.10.5.1 / 3.10.4.1 so the test does
 * not share a code path with the implementation it checks.
 */
#define CFG_KEY_NAV_PVT_UART1 0x20910007U
#define VALSET_FRAME_LEN (LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 5U + LGPS_UBX_TRAILER_LEN)
#define VALGET_FRAME_LEN (LGPS_UBX_HEADER_LEN + LGPS_UBX_CFG_HEADER_LEN + 4U + LGPS_UBX_TRAILER_LEN)

ZTEST(lifegps_ubx, test_valset_u1_golden_frame)
{
	/* version 0, layers = RAM, reserved 0, key, value 1. */
	static const uint8_t expected[] = {
		0xB5U, 0x62U, 0x06U, 0x8AU, 0x09U, 0x00U, 0x00U, 0x01U, 0x00U,
		0x00U, 0x07U, 0x00U, 0x91U, 0x20U, 0x01U, 0x53U, 0x48U,
	};
	uint8_t frame[VALSET_FRAME_LEN];
	const uint8_t value = 1U;
	int ret;

	ret = lgps_ubx_build_valset_item(frame, sizeof(frame), LGPS_UBX_LAYER_RAM,
					CFG_KEY_NAV_PVT_UART1, &value, 1U);
	zassert_equal(ret, (int)sizeof(expected), "VALSET frame must be 17 bytes");
	zassert_equal(sizeof(frame), sizeof(expected), "test buffer must match the golden vector");
	zassert_mem_equal(frame, expected, sizeof(expected),
			  "VALSET frame must match section 3.10.5.1");
}

ZTEST(lifegps_ubx, test_valset_rejects_what_the_receiver_would_nak)
{
	uint8_t frame[VALSET_FRAME_LEN];
	const uint8_t value = 1U;

	/* A layers mask without a target layer is NAK'd on the wire; catch it here. */
	zassert_equal(lgps_ubx_build_valset_item(frame, sizeof(frame), 0U, CFG_KEY_NAV_PVT_UART1,
						 &value, 1U),
		      -EINVAL, "empty layers mask");
	/* Zero-length and over-long values are argument errors, not silent truncation. */
	zassert_equal(lgps_ubx_build_valset_item(frame, sizeof(frame), LGPS_UBX_LAYER_RAM,
						 CFG_KEY_NAV_PVT_UART1, &value, 0U),
		      -EINVAL, "zero-length value");
	zassert_equal(lgps_ubx_build_valset_item(frame, sizeof(frame), LGPS_UBX_LAYER_RAM,
						 CFG_KEY_NAV_PVT_UART1, &value,
						 LGPS_UBX_CFG_VALUE_MAX_LEN + 1U),
		      -EINVAL, "over-long value");
	zassert_equal(lgps_ubx_build_valset_item(frame, sizeof(frame) - 1U, LGPS_UBX_LAYER_RAM,
						 CFG_KEY_NAV_PVT_UART1, &value, 1U),
		      -ENOSPC, "one byte short");
}

ZTEST(lifegps_ubx, test_valget_u1_golden_frame)
{
	/* version 0, layer = RAM, position 0, then the single key. */
	static const uint8_t expected[] = {
		0xB5U, 0x62U, 0x06U, 0x8BU, 0x08U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x00U, 0x07U, 0x00U, 0x91U, 0x20U, 0x51U, 0xEFU,
	};
	uint8_t frame[VALGET_FRAME_LEN];
	int ret;

	ret = lgps_ubx_build_valget_item(frame, sizeof(frame), LGPS_UBX_VALGET_LAYER_RAM,
					CFG_KEY_NAV_PVT_UART1);
	zassert_equal(ret, (int)sizeof(expected), "VALGET request must be 16 bytes");
	zassert_equal(sizeof(frame), sizeof(expected), "test buffer must match the golden vector");
	zassert_mem_equal(frame, expected, sizeof(expected),
			  "VALGET request must match section 3.10.4.1");
	/* 0/1/2/7 are the only defined layer values. */
	zassert_equal(lgps_ubx_build_valget_item(frame, sizeof(frame), 3U, CFG_KEY_NAV_PVT_UART1),
		      -EINVAL, "layer 3 is not defined");
}

ZTEST(lifegps_ubx, test_ack_and_nak_classification)
{
	static const uint8_t ack_frame[] = {
		0xB5U, 0x62U, 0x05U, 0x01U, 0x02U, 0x00U, 0x06U, 0x8AU, 0x98U, 0xC1U,
	};
	static const uint8_t nak_frame[] = {
		0xB5U, 0x62U, 0x05U, 0x00U, 0x02U, 0x00U, 0x06U, 0x8AU, 0x97U, 0xBCU,
	};
	struct lgps_ubx_parser parser;
	uint8_t msg_class = 0U;
	uint8_t msg_id = 0U;
	bool accepted = false;

	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(ack_frame, sizeof(ack_frame), &parser);
	zassert_equal(lgps_ubx_decode_ack(&parser, &accepted, &msg_class, &msg_id), 0,
		      "ACK-ACK must decode");
	zassert_true(accepted, "ACK-ACK means accepted");
	/* The acknowledged message is the payload, not the ACK's own id. */
	zassert_equal(msg_class, LGPS_UBX_CLASS_CFG, "acknowledged class");
	zassert_equal(msg_id, LGPS_UBX_ID_CFG_VALSET, "acknowledged id");

	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(nak_frame, sizeof(nak_frame), &parser);
	zassert_equal(lgps_ubx_decode_ack(&parser, &accepted, &msg_class, &msg_id), 0,
		      "ACK-NAK must decode");
	zassert_false(accepted, "ACK-NAK means refused");
}

ZTEST(lifegps_ubx, test_valget_response_readback)
{
	/* Response: version 0, layer RAM, position 0, key, value 1. */
	static const uint8_t response[] = {
		0xB5U, 0x62U, 0x06U, 0x8BU, 0x09U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x00U, 0x07U, 0x00U, 0x91U, 0x20U, 0x01U, 0x53U, 0x4CU,
	};
	struct lgps_ubx_parser parser;
	uint8_t value = 0U;

	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(response, sizeof(response), &parser);
	zassert_equal(lgps_ubx_decode_valget_u1(&parser, CFG_KEY_NAV_PVT_UART1, &value), 0,
		      "single-key VALGET response must decode");
	zassert_equal(value, 1U, "readback value");

	/* A response for another key must not be mistaken for ours. */
	zassert_equal(lgps_ubx_decode_valget_u1(&parser, 0x20910016U, &value), -ENOENT,
		      "different key must be reported, not ignored");
	/* Truncated payload: 8 bytes cannot hold key plus value. */
	parser.payload_len = 8U;
	zassert_equal(lgps_ubx_decode_valget_u1(&parser, CFG_KEY_NAV_PVT_UART1, &value), -EINVAL,
		      "truncated payload must be rejected");
}

/* UBX-NAV-SAT (section 3.15.13): 8-byte header, 12 bytes per satellite. */
static size_t build_nav_sat_frame(uint8_t *frame, uint8_t declared_svs, uint8_t payload_svs,
				  size_t frame_len)
{
	size_t payload_len = LGPS_UBX_NAV_SAT_HEADER_LEN +
			     ((size_t)payload_svs * LGPS_UBX_NAV_SAT_SV_LEN);
	size_t index;

	memset(frame, 0, frame_len);
	frame[0] = LGPS_UBX_SYNC1;
	frame[1] = LGPS_UBX_SYNC2;
	frame[2] = LGPS_UBX_CLASS_NAV;
	frame[3] = LGPS_UBX_ID_NAV_SAT;
	put_le16(&frame[4], (uint16_t)payload_len);
	put_le32(&frame[LGPS_UBX_HEADER_LEN + 0U], 0x11223344U);
	frame[LGPS_UBX_HEADER_LEN + 4U] = 0x01U;
	frame[LGPS_UBX_HEADER_LEN + 5U] = declared_svs;

	for (index = 0U; index < payload_svs; index++) {
		uint8_t *group = &frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SAT_HEADER_LEN +
				       (index * LGPS_UBX_NAV_SAT_SV_LEN)];
		uint32_t flags;

		group[0] = (uint8_t)(index == 0U ? 0U : 3U); /* GPS, then BeiDou */
		group[1] = (uint8_t)(index == 0U ? 7U : 21U);
		group[2] = (uint8_t)(index == 0U ? 42U : 28U);
		group[3] = (uint8_t)(index == 0U ? 55 : -12);
		/* azim is I2 at group offset 4; the second SV leaves it out of range
		 * (-1), which is how the receiver reports "unknown".
		 */
		put_le16(&group[LGPS_UBX_NAV_SAT_SV_OFF_AZIM],
			 (uint16_t)(index == 0U ? 219U : 0xFFFFU));
		put_le16(&group[6], 0U); /* prRes */
		flags = (uint32_t)(index == 0U
					   ? (LGPS_UBX_NAV_SAT_FLAG_SV_USED | LGPS_UBX_NAV_SAT_FLAG_EPH_AVAIL | 4U)
					   : LGPS_UBX_NAV_SAT_FLAG_ALM_AVAIL | 1U);
		put_le32(&group[8], flags);
	}

	{
		uint8_t ck_a = 0U;
		uint8_t ck_b = 0U;

		for (index = 2U; index < LGPS_UBX_HEADER_LEN + payload_len; index++) {
			ck_a = (uint8_t)(ck_a + frame[index]);
			ck_b = (uint8_t)(ck_b + ck_a);
		}

		frame[LGPS_UBX_HEADER_LEN + payload_len] = ck_a;
		frame[LGPS_UBX_HEADER_LEN + payload_len + 1U] = ck_b;
	}

	return LGPS_UBX_HEADER_LEN + payload_len + LGPS_UBX_TRAILER_LEN;
}

ZTEST(lifegps_ubx, test_nav_sat_decode)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SAT_HEADER_LEN +
		      (2U * LGPS_UBX_NAV_SAT_SV_LEN) + LGPS_UBX_TRAILER_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_nav_sat sats[LGPS_UBX_NAV_SAT_MAX_SV];
	uint8_t count = 0xFFU;
	size_t len;

	len = build_nav_sat_frame(frame, 2U, 2U, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);

	zassert_equal(parser.payload_len,
		      (uint16_t)(LGPS_UBX_NAV_SAT_HEADER_LEN + (2U * LGPS_UBX_NAV_SAT_SV_LEN)),
		      "NAV-SAT payload length");
	zassert_equal(lgps_ubx_decode_nav_sat(&parser, sats, ARRAY_SIZE(sats), &count), 0,
		      "NAV-SAT must decode");
	zassert_equal(count, 2U, "two satellites");

	zassert_equal(sats[0].gnss_id, 0U, "GPS");
	zassert_equal(sats[0].sv_id, 7U, "GPS PRN 7");
	zassert_equal(sats[0].cno, 42U, "C/N0");
	zassert_equal(sats[0].elev, 55, "elevation");
	zassert_equal(sats[0].azim, 219, "azimuth at group offset 4");
	zassert_equal(sats[0].quality, 4U, "qualityInd 4 = code locked");
	zassert_true(sats[0].used, "svUsed bit");
	zassert_true(sats[0].eph_avail, "ephAvail is flags bit 11");
	zassert_false(sats[0].alm_avail, "almAvail is flags bit 12");

	zassert_equal(sats[1].gnss_id, 3U, "BeiDou");
	zassert_equal(sats[1].elev, -12, "negative elevation is sign-extended");
	zassert_equal(sats[1].azim, -1, "out-of-range azimuth stays unknown");
	zassert_false(sats[1].used, "not used for navigation");
	zassert_false(sats[1].eph_avail, "no ephemeris for this SV");
	zassert_true(sats[1].alm_avail, "almanac-only SV");

	/* max_sv caps the list without failing the decode. */
	count = 0xFFU;
	zassert_equal(lgps_ubx_decode_nav_sat(&parser, sats, 1U, &count), 0, "capped decode");
	zassert_equal(count, 1U, "cap respected");
}

/* UBX-NAV-DOP (section 3.15.4): 18-byte payload, every DOP scaled by 100. */
ZTEST(lifegps_ubx, test_nav_dop_decode)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_DOP_PAYLOAD_LEN + LGPS_UBX_TRAILER_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_ubx_nav_dop dop;
	uint8_t ck_a = 0U;
	uint8_t ck_b = 0U;
	size_t index;

	memset(frame, 0, sizeof(frame));
	frame[0] = LGPS_UBX_SYNC1;
	frame[1] = LGPS_UBX_SYNC2;
	frame[2] = LGPS_UBX_CLASS_NAV;
	frame[3] = LGPS_UBX_ID_NAV_DOP;
	put_le16(&frame[4], (uint16_t)LGPS_UBX_NAV_DOP_PAYLOAD_LEN);
	put_le32(&frame[LGPS_UBX_HEADER_LEN + 0U], 0x11223344U); /* iTOW */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 4U], 140U);        /* gDOP 1.40 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 6U], 80U);         /* pDOP 0.80 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 8U], 60U);         /* tDOP 0.60 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 10U], 70U);        /* vDOP 0.70 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 12U], 50U);        /* hDOP 0.50 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 14U], 40U);        /* nDOP 0.40 */
	put_le16(&frame[LGPS_UBX_HEADER_LEN + 16U], 30U);        /* eDOP 0.30 */

	for (index = 2U; index < LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_DOP_PAYLOAD_LEN; index++) {
		ck_a = (uint8_t)(ck_a + frame[index]);
		ck_b = (uint8_t)(ck_b + ck_a);
	}

	frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_DOP_PAYLOAD_LEN] = ck_a;
	frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_DOP_PAYLOAD_LEN + 1U] = ck_b;

	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, sizeof(frame), &parser);
	zassert_equal(lgps_ubx_decode_nav_dop(&parser, &dop), 0, "NAV-DOP must decode");
	zassert_equal(dop.pdop_1e2, 80U, "pDOP at offset 6");
	zassert_equal(dop.vdop_1e2, 70U, "vDOP at offset 10");
	zassert_equal(dop.hdop_1e2, 50U, "hDOP at offset 12");

	/* A NAV-PVT frame must not be decoded as NAV-DOP. */
	parser.msg_id = LGPS_UBX_ID_NAV_PVT;
	zassert_equal(lgps_ubx_decode_nav_dop(&parser, &dop), -EINVAL, "wrong message id");

	/* Truncated payload: 16 of the 18 bytes carry no hDOP/eDOP. */
	parser.msg_id = LGPS_UBX_ID_NAV_DOP;
	parser.payload_len = (uint16_t)(LGPS_UBX_NAV_DOP_PAYLOAD_LEN - 2U);
	zassert_equal(lgps_ubx_decode_nav_dop(&parser, &dop), -EINVAL, "short payload");
}

/* UBX-NAV-SIG (section 3.15.15): 8-byte header, 16 bytes per signal. */
static size_t build_nav_sig_frame(uint8_t *frame, uint8_t declared_sigs, uint8_t payload_sigs,
				  size_t frame_len)
{
	size_t payload_len = LGPS_UBX_NAV_SIG_HEADER_LEN +
			     ((size_t)payload_sigs * LGPS_UBX_NAV_SIG_SIG_LEN);
	size_t index;

	memset(frame, 0, frame_len);
	frame[0] = LGPS_UBX_SYNC1;
	frame[1] = LGPS_UBX_SYNC2;
	frame[2] = LGPS_UBX_CLASS_NAV;
	frame[3] = LGPS_UBX_ID_NAV_SIG;
	put_le16(&frame[4], (uint16_t)payload_len);
	put_le32(&frame[LGPS_UBX_HEADER_LEN + 0U], 0x11223344U);
	frame[LGPS_UBX_HEADER_LEN + 4U] = 0x00U; /* version */
	frame[LGPS_UBX_HEADER_LEN + 5U] = declared_sigs;

	for (index = 0U; index < payload_sigs; index++) {
		uint8_t *group = &frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SIG_HEADER_LEN +
				       (index * LGPS_UBX_NAV_SIG_SIG_LEN)];

		group[0] = (uint8_t)(index == 0U ? 0U : 3U);  /* GPS, then BeiDou */
		group[1] = (uint8_t)(index == 0U ? 7U : 21U);
		group[2] = (uint8_t)(index == 0U ? 0U : 5U);  /* L1C/A, B1 Cp */
		group[6] = (uint8_t)(index == 0U ? 42U : 28U); /* cno at offset 6 */
		group[7] = (uint8_t)(index == 0U ? 4U : 2U);  /* qualityInd */
		put_le16(&group[10], (uint16_t)(index == 0U ? LGPS_UBX_NAV_SIG_FLAG_PR_USED : 0U));
	}

	{
		uint8_t ck_a = 0U;
		uint8_t ck_b = 0U;

		for (index = 2U; index < LGPS_UBX_HEADER_LEN + payload_len; index++) {
			ck_a = (uint8_t)(ck_a + frame[index]);
			ck_b = (uint8_t)(ck_b + ck_a);
		}

		frame[LGPS_UBX_HEADER_LEN + payload_len] = ck_a;
		frame[LGPS_UBX_HEADER_LEN + payload_len + 1U] = ck_b;
	}

	return LGPS_UBX_HEADER_LEN + payload_len + LGPS_UBX_TRAILER_LEN;
}

ZTEST(lifegps_ubx, test_nav_sig_decode)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SIG_HEADER_LEN +
		      (2U * LGPS_UBX_NAV_SIG_SIG_LEN) + LGPS_UBX_TRAILER_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_ubx_nav_sig sigs[LGPS_UBX_NAV_SIG_MAX];
	uint8_t count = 0xFFU;
	size_t len;

	len = build_nav_sig_frame(frame, 2U, 2U, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);

	zassert_equal(lgps_ubx_decode_nav_sig(&parser, sigs, ARRAY_SIZE(sigs), &count), 0,
		      "NAV-SIG must decode");
	zassert_equal(count, 2U, "two signals");

	zassert_equal(sigs[0].gnss_id, 0U, "GPS");
	zassert_equal(sigs[0].sv_id, 7U, "GPS PRN 7");
	zassert_equal(sigs[0].sig_id, 0U, "GPS L1C/A");
	zassert_equal(sigs[0].cno, 42U, "cno at group offset 6");
	zassert_equal(sigs[0].quality, 4U, "qualityInd");
	zassert_true(sigs[0].used, "prUsed is sigFlags bit 3");

	zassert_equal(sigs[1].gnss_id, 3U, "BeiDou");
	zassert_equal(sigs[1].sig_id, 5U, "B1 Cp");
	zassert_false(sigs[1].used, "not used for the solution");

	/* Declared above the payload: store what exists, never read past it. */
	len = build_nav_sig_frame(frame, 5U, 2U, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);
	zassert_equal(lgps_ubx_decode_nav_sig(&parser, sigs, ARRAY_SIZE(sigs), &count), 0,
		      "declared count above the payload must not fail");
	zassert_equal(count, 2U, "only the signals really present are stored");

	parser.msg_id = LGPS_UBX_ID_NAV_SAT;
	zassert_equal(lgps_ubx_decode_nav_sig(&parser, sigs, ARRAY_SIZE(sigs), &count), -EINVAL,
		      "a non-NAV-SIG frame must be refused");
}

ZTEST(lifegps_ubx, test_nav_sat_truncated_and_wrong_class)
{
	uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SAT_HEADER_LEN +
		      (2U * LGPS_UBX_NAV_SAT_SV_LEN) + LGPS_UBX_TRAILER_LEN];
	struct lgps_ubx_parser parser;
	struct lgps_nav_sat sats[LGPS_UBX_NAV_SAT_MAX_SV];
	uint8_t count = 0U;
	size_t len;

	/* Declares five satellites but carries two: store what exists, do not read
	 * past the payload.
	 */
	len = build_nav_sat_frame(frame, 5U, 2U, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);
	zassert_equal(lgps_ubx_decode_nav_sat(&parser, sats, ARRAY_SIZE(sats), &count), 0,
		      "declared count above the payload must not fail");
	zassert_equal(count, 2U, "only the satellites really present are stored");

	parser.msg_id = LGPS_UBX_ID_NAV_PVT;
	zassert_equal(lgps_ubx_decode_nav_sat(&parser, sats, ARRAY_SIZE(sats), &count), -EINVAL,
		      "a non-NAV-SAT frame must be refused");
}

ZTEST(lifegps_ubx, test_nav_sig_full_sky_fits_the_parser)
{
	/* 40 signals = 8 + 16 * 40 = 648 B, the epoch size 0.4.0 documents. Sized for
	 * NAV-SAT only (392 B), the parser dropped this frame as "too long", which
	 * blanked the whole CF column on the phone. The buffers are static because the
	 * frame plus the (now larger) parser state would otherwise cost a few
	 * kilobytes of this thread's stack for no reason.
	 */
	static uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SIG_HEADER_LEN +
			     (40U * LGPS_UBX_NAV_SIG_SIG_LEN) + LGPS_UBX_TRAILER_LEN];
	static struct lgps_ubx_parser parser;
	static struct lgps_ubx_nav_sig sigs[LGPS_UBX_NAV_SIG_MAX];
	uint8_t count = 0U;
	size_t len;

	zassert_true(LGPS_UBX_MAX_PAYLOAD_LEN >= LGPS_UBX_NAV_SIG_PAYLOAD_MAX,
		     "the payload buffer must cover a full NAV-SIG sky");

	len = build_nav_sig_frame(frame, 40U, 40U, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);

	zassert_equal(lgps_ubx_decode_nav_sig(&parser, sigs, ARRAY_SIZE(sigs), &count), 0,
		      "a 40-signal NAV-SIG frame must decode");
	zassert_equal(count, 40U, "every signal of the frame is stored");
}

ZTEST(lifegps_ubx, test_nav_sig_buffer_boundary)
{
	/* The largest frame the buffer can hold: exactly LGPS_UBX_NAV_SIG_MAX signals.
	 * One byte more would be dropped by the parser, so this is the real ceiling.
	 */
	static uint8_t frame[LGPS_UBX_HEADER_LEN + LGPS_UBX_NAV_SIG_PAYLOAD_MAX +
			     LGPS_UBX_TRAILER_LEN];
	static struct lgps_ubx_parser parser;
	static struct lgps_ubx_nav_sig sigs[LGPS_UBX_NAV_SIG_MAX];
	uint8_t count = 0U;
	size_t len;

	zassert_equal(sizeof(frame) - LGPS_UBX_HEADER_LEN - LGPS_UBX_TRAILER_LEN,
		      (size_t)LGPS_UBX_MAX_PAYLOAD_LEN, "buffer sized for the largest message");

	len = build_nav_sig_frame(frame, (uint8_t)LGPS_UBX_NAV_SIG_MAX,
				  (uint8_t)LGPS_UBX_NAV_SIG_MAX, sizeof(frame));
	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(frame, len, &parser);

	zassert_equal(lgps_ubx_decode_nav_sig(&parser, sigs, ARRAY_SIZE(sigs), &count), 0,
		      "the buffer boundary frame must decode");
	zassert_equal(count, (uint8_t)LGPS_UBX_NAV_SIG_MAX, "all signals stored");
}

ZTEST(lifegps_ubx, test_valget_wide_value)
{
	/* Response for CFG-RATE-MEAS (u2, value 1000 ms = 0x03E8). */
	static const uint8_t response[] = {
		0xB5U, 0x62U, 0x06U, 0x8BU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
		0x01U, 0x00U, 0x21U, 0x30U, 0xE8U, 0x03U, 0xD8U, 0xC8U,
	};
	struct lgps_ubx_parser parser;
	uint8_t value[LGPS_UBX_CFG_VALUE_MAX_LEN] = { 0 };
	uint8_t value_len = 0U;

	lgps_ubx_parser_reset(&parser);
	feed_expect_ready(response, sizeof(response), &parser);

	zassert_equal(lgps_ubx_decode_valget(&parser, LGPS_UBX_CFG_KEY_RATE_MEAS, value,
					    sizeof(value), &value_len),
		      0, "two-byte value must decode");
	zassert_equal(value_len, 2U, "width reported");
	zassert_equal(value[0], 0xE8U, "value low byte");
	zassert_equal(value[1], 0x03U, "value high byte");
}

ZTEST_SUITE(lifegps_ubx, NULL, NULL, NULL, NULL, NULL);
