/*
 * Life_GPS firmware - u-blox UBX framing: streaming parser, NAV-PVT decoder and
 * request builders.
 *
 * Wire format, class/id and field offsets come from
 * Doc/Hardware/Datasheets/UBLOX/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
 * (frame section 2, UBX-NAV-PVT section 3.15.11, UBX-RXM-PMREQ section 3.16.6).
 * The module keeps no global state and logs nothing: the caller owns the UART
 * and decides how to report a rejected frame.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lifegps/ubx.h>

/* Parser positions inside one frame (header length is checked before use). */
enum ubx_parser_state {
	UBX_STATE_SYNC1 = 0,
	UBX_STATE_SYNC2,
	UBX_STATE_CLASS,
	UBX_STATE_ID,
	UBX_STATE_LEN_LO,
	UBX_STATE_LEN_HI,
	UBX_STATE_PAYLOAD,
	UBX_STATE_CK_A,
	UBX_STATE_CK_B,
};

/* UBX-NAV-PVT payload offsets in bytes, table in section 3.15.11. */
enum ubx_nav_pvt_offset {
	PVT_OFF_ITOW = 0,
	PVT_OFF_YEAR = 4,
	PVT_OFF_MONTH = 6,
	PVT_OFF_DAY = 7,
	PVT_OFF_HOUR = 8,
	PVT_OFF_MINUTE = 9,
	PVT_OFF_SECOND = 10,
	PVT_OFF_VALID = 11,
	PVT_OFF_TACC = 12,
	PVT_OFF_NANO = 16,
	PVT_OFF_FIX_TYPE = 20,
	PVT_OFF_FLAGS = 21,
	PVT_OFF_FLAGS2 = 22,
	PVT_OFF_NUM_SV = 23,
	PVT_OFF_LON = 24,
	PVT_OFF_LAT = 28,
	PVT_OFF_HEIGHT = 32,
	PVT_OFF_HMSL = 36,
	PVT_OFF_HACC = 40,
	PVT_OFF_VACC = 44,
	PVT_OFF_VEL_N = 48,
	PVT_OFF_VEL_E = 52,
	PVT_OFF_VEL_D = 56,
	PVT_OFF_GSPEED = 60,
	PVT_OFF_HEAD_MOT = 64,
	PVT_OFF_SACC = 68,
	PVT_OFF_HEAD_ACC = 72,
	PVT_OFF_PDOP = 76,
	PVT_OFF_FLAGS3 = 78,
};

/* UBX-RXM-PMREQ payload offsets in bytes, table in section 3.16.6. */
enum ubx_rxm_pmreq_offset {
	PMREQ_OFF_VERSION = 0,
	PMREQ_OFF_DURATION_MS = 4,
	PMREQ_OFF_FLAGS = 8,
	PMREQ_OFF_WAKEUP_SOURCES = 12,
};

static uint16_t get_le16(const uint8_t *src)
{
	return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t get_le32(const uint8_t *src)
{
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
	dst[0] = (uint8_t)(value & 0xFFU);
	dst[1] = (uint8_t)((value >> 8) & 0xFFU);
	dst[2] = (uint8_t)((value >> 16) & 0xFFU);
	dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/* 8-bit Fletcher step; both running values start at zero. */
static void ubx_checksum_byte(uint8_t byte, uint8_t *ck_a, uint8_t *ck_b)
{
	*ck_a = (uint8_t)(*ck_a + byte);
	*ck_b = (uint8_t)(*ck_b + *ck_a);
}

void lgps_ubx_parser_reset(struct lgps_ubx_parser *parser)
{
	if (parser == NULL) {
		return;
	}
	parser->idx = 0U;
	parser->payload_len = 0U;
	parser->msg_class = 0U;
	parser->msg_id = 0U;
	parser->ck_a = 0U;
	parser->ck_b = 0U;
	parser->state = (uint8_t)UBX_STATE_SYNC1;
}

enum lgps_ubx_status lgps_ubx_parser_feed(struct lgps_ubx_parser *parser, uint8_t byte)
{
	if (parser == NULL) {
		return LGPS_UBX_STATUS_NONE;
	}

	switch (parser->state) {
	case UBX_STATE_SYNC1:
		if (byte == LGPS_UBX_SYNC1) {
			parser->state = (uint8_t)UBX_STATE_SYNC2;
		}
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_SYNC2:
		if (byte == LGPS_UBX_SYNC2) {
			parser->state = (uint8_t)UBX_STATE_CLASS;
		} else if (byte == LGPS_UBX_SYNC1) {
			/* The second 0xB5 of a duplicated sync may open a frame. */
			parser->state = (uint8_t)UBX_STATE_SYNC2;
		} else {
			parser->state = (uint8_t)UBX_STATE_SYNC1;
		}
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_CLASS:
		parser->msg_class = byte;
		parser->ck_a = 0U;
		parser->ck_b = 0U;
		parser->ck_mismatch = 0U;
		ubx_checksum_byte(byte, &parser->ck_a, &parser->ck_b);
		parser->state = (uint8_t)UBX_STATE_ID;
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_ID:
		parser->msg_id = byte;
		ubx_checksum_byte(byte, &parser->ck_a, &parser->ck_b);
		parser->state = (uint8_t)UBX_STATE_LEN_LO;
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_LEN_LO:
		parser->payload_len = byte;
		ubx_checksum_byte(byte, &parser->ck_a, &parser->ck_b);
		parser->state = (uint8_t)UBX_STATE_LEN_HI;
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_LEN_HI:
		parser->payload_len =
			(uint16_t)((uint16_t)parser->payload_len | ((uint16_t)byte << 8));
		ubx_checksum_byte(byte, &parser->ck_a, &parser->ck_b);
		parser->idx = 0U;
		if (parser->payload_len > LGPS_UBX_MAX_PAYLOAD_LEN) {
			/* Drop the frame before its payload can reach the buffer. */
			parser->state = (uint8_t)UBX_STATE_SYNC1;
			return LGPS_UBX_STATUS_NONE;
		}
		if (parser->payload_len == 0U) {
			parser->state = (uint8_t)UBX_STATE_CK_A;
		} else {
			parser->state = (uint8_t)UBX_STATE_PAYLOAD;
		}
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_PAYLOAD:
		parser->payload[parser->idx] = byte;
		ubx_checksum_byte(byte, &parser->ck_a, &parser->ck_b);
		parser->idx++;
		if (parser->idx >= parser->payload_len) {
			parser->state = (uint8_t)UBX_STATE_CK_A;
		}
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_CK_A:
		if (byte != parser->ck_a) {
			parser->ck_mismatch = 1U;
		}
		parser->state = (uint8_t)UBX_STATE_CK_B;
		return LGPS_UBX_STATUS_NONE;
	case UBX_STATE_CK_B:
		parser->state = (uint8_t)UBX_STATE_SYNC1;
		if ((byte != parser->ck_b) || (parser->ck_mismatch != 0U)) {
			return LGPS_UBX_STATUS_BAD_CHECKSUM;
		}
		return LGPS_UBX_STATUS_MSG_READY;
	default:
		/* A corrupted state field must not be trusted; restart the search. */
		parser->state = (uint8_t)UBX_STATE_SYNC1;
		return LGPS_UBX_STATUS_NONE;
	}
}

int lgps_ubx_decode_nav_pvt(const struct lgps_ubx_parser *parser, struct lgps_nav_pvt *out)
{
	if ((parser == NULL) || (out == NULL)) {
		return -EINVAL;
	}
	if ((parser->msg_class != LGPS_UBX_CLASS_NAV) ||
	    (parser->msg_id != LGPS_UBX_ID_NAV_PVT) ||
	    (parser->payload_len != LGPS_UBX_NAV_PVT_PAYLOAD_LEN)) {
		return -EINVAL;
	}

	/*
	 * Signed fields carry two's-complement i4/i2 values on the wire, so the
	 * unsigned little-endian result is reinterpreted as the signed type.
	 */
	out->itow_ms = get_le32(&parser->payload[PVT_OFF_ITOW]);
	out->year = get_le16(&parser->payload[PVT_OFF_YEAR]);
	out->month = parser->payload[PVT_OFF_MONTH];
	out->day = parser->payload[PVT_OFF_DAY];
	out->hour = parser->payload[PVT_OFF_HOUR];
	out->minute = parser->payload[PVT_OFF_MINUTE];
	out->second = parser->payload[PVT_OFF_SECOND];
	out->valid = parser->payload[PVT_OFF_VALID];
	out->tacc_ns = get_le32(&parser->payload[PVT_OFF_TACC]);
	out->nano_ns = (int32_t)get_le32(&parser->payload[PVT_OFF_NANO]);
	out->fix_type = parser->payload[PVT_OFF_FIX_TYPE];
	out->flags = parser->payload[PVT_OFF_FLAGS];
	out->flags2 = parser->payload[PVT_OFF_FLAGS2];
	out->num_sv = parser->payload[PVT_OFF_NUM_SV];
	out->lon_e7 = (int32_t)get_le32(&parser->payload[PVT_OFF_LON]);
	out->lat_e7 = (int32_t)get_le32(&parser->payload[PVT_OFF_LAT]);
	out->height_mm = (int32_t)get_le32(&parser->payload[PVT_OFF_HEIGHT]);
	out->hmsl_mm = (int32_t)get_le32(&parser->payload[PVT_OFF_HMSL]);
	out->hacc_mm = get_le32(&parser->payload[PVT_OFF_HACC]);
	out->vacc_mm = get_le32(&parser->payload[PVT_OFF_VACC]);
	out->vel_n_mms = (int32_t)get_le32(&parser->payload[PVT_OFF_VEL_N]);
	out->vel_e_mms = (int32_t)get_le32(&parser->payload[PVT_OFF_VEL_E]);
	out->vel_d_mms = (int32_t)get_le32(&parser->payload[PVT_OFF_VEL_D]);
	out->gspeed_mms = (int32_t)get_le32(&parser->payload[PVT_OFF_GSPEED]);
	out->head_mot_1e5 = (int32_t)get_le32(&parser->payload[PVT_OFF_HEAD_MOT]);
	out->sacc_mms = get_le32(&parser->payload[PVT_OFF_SACC]);
	out->headacc_1e5 = get_le32(&parser->payload[PVT_OFF_HEAD_ACC]);
	out->pdop_1e2 = get_le16(&parser->payload[PVT_OFF_PDOP]);
	out->flags3 = get_le16(&parser->payload[PVT_OFF_FLAGS3]);
	return 0;
}

int lgps_ubx_build(uint8_t *buf, size_t buf_len_bytes, uint8_t msg_class, uint8_t msg_id,
		   const uint8_t *payload, uint16_t payload_len)
{
	size_t total_len_bytes;
	uint8_t ck_a = 0U;
	uint8_t ck_b = 0U;

	if (buf == NULL) {
		return -EINVAL;
	}
	if ((payload == NULL) && (payload_len != 0U)) {
		return -EINVAL;
	}
	if (payload_len > LGPS_UBX_MAX_PAYLOAD_LEN) {
		return -EINVAL;
	}
	total_len_bytes = (size_t)LGPS_UBX_HEADER_LEN + (size_t)payload_len +
			  (size_t)LGPS_UBX_TRAILER_LEN;
	if (buf_len_bytes < total_len_bytes) {
		return -ENOSPC;
	}

	buf[0] = LGPS_UBX_SYNC1;
	buf[1] = LGPS_UBX_SYNC2;
	buf[2] = msg_class;
	buf[3] = msg_id;
	buf[4] = (uint8_t)(payload_len & 0xFFU);
	buf[5] = (uint8_t)((payload_len >> 8) & 0xFFU);
	for (size_t i = 0U; i < payload_len; i++) {
		buf[LGPS_UBX_HEADER_LEN + i] = payload[i];
	}
	/* Checksum covers class, id, both length bytes and the payload. */
	for (size_t i = 2U; i < LGPS_UBX_HEADER_LEN + (size_t)payload_len; i++) {
		ubx_checksum_byte(buf[i], &ck_a, &ck_b);
	}
	buf[LGPS_UBX_HEADER_LEN + payload_len] = ck_a;
	buf[LGPS_UBX_HEADER_LEN + payload_len + 1U] = ck_b;
	return (int)total_len_bytes;
}

int lgps_ubx_build_poll(uint8_t *buf, size_t buf_len_bytes, uint8_t msg_class, uint8_t msg_id)
{
	return lgps_ubx_build(buf, buf_len_bytes, msg_class, msg_id, NULL, 0U);
}

int lgps_ubx_build_rxm_pmreq(uint8_t *buf, size_t buf_len_bytes, uint32_t duration_ms, bool backup,
			     bool force, uint32_t wakeup_sources)
{
	/* Offsets 1..3 are reserved; version 0 is the only defined value. */
	uint8_t payload[LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN] = {0};
	uint32_t flags = 0U;

	if (buf == NULL) {
		return -EINVAL;
	}
	if (duration_ms > LGPS_UBX_PMREQ_MAX_DURATION_MS) {
		duration_ms = LGPS_UBX_PMREQ_MAX_DURATION_MS;
	}
	if (backup) {
		flags |= LGPS_UBX_PMREQ_FLAG_BACKUP;
	}
	if (force) {
		flags |= LGPS_UBX_PMREQ_FLAG_FORCE;
	}

	payload[PMREQ_OFF_VERSION] = 0U;
	put_le32(&payload[PMREQ_OFF_DURATION_MS], duration_ms);
	put_le32(&payload[PMREQ_OFF_FLAGS], flags);
	put_le32(&payload[PMREQ_OFF_WAKEUP_SOURCES], wakeup_sources);
	return lgps_ubx_build(buf, buf_len_bytes, LGPS_UBX_CLASS_RXM, LGPS_UBX_ID_RXM_PMREQ,
			      payload, LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN);
}
