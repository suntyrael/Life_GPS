/*
 * Life_GPS firmware - ztest for the UBX framing module.
 *
 * The golden frames and payload offsets are taken from
 * Doc/Hardware/Datasheets/UBLOX/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf,
 * independently of the implementation under test.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

ZTEST_SUITE(lifegps_ubx, NULL, NULL, NULL, NULL, NULL);
