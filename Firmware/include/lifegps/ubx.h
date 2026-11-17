/*
 * Life_GPS firmware - u-blox UBX framing (MAX-M10S default part).
 *
 * Every constant in this header is taken from
 * Doc/Hardware/Datasheets/UBLOX/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf:
 *   - frame 0xB5 0x62, class, id, uint16 little-endian length, payload, CK_A, CK_B
 *     (8-bit Fletcher over class..payload, section 2 "UBX protocol")
 *   - UBX-NAV-PVT (0x01 0x07), payload 92 bytes (section 3.15.11)
 *   - UBX-RXM-PMREQ (0x02 0x41), payload 16 bytes (section 3.16.6)
 * The module is protocol-only: no UART, no device tree, no global state.
 */

#ifndef LIFEGPS_UBX_H_
#define LIFEGPS_UBX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LGPS_UBX_SYNC1 0xB5U
#define LGPS_UBX_SYNC2 0x62U
#define LGPS_UBX_HEADER_LEN 6U
#define LGPS_UBX_TRAILER_LEN 2U

#define LGPS_UBX_CLASS_NAV 0x01U
#define LGPS_UBX_ID_NAV_PVT 0x07U
#define LGPS_UBX_CLASS_RXM 0x02U
#define LGPS_UBX_ID_RXM_PMREQ 0x41U
#define LGPS_UBX_CLASS_ACK 0x05U

#define LGPS_UBX_NAV_PVT_PAYLOAD_LEN 92U
#define LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN 16U

/* Largest payload the streaming parser accepts (NAV-PVT is the biggest we use). */
#define LGPS_UBX_MAX_PAYLOAD_LEN 128U

/* NAV-PVT valid (offset 11) bit masks. */
#define LGPS_UBX_PVT_VALID_DATE 0x01U
#define LGPS_UBX_PVT_VALID_TIME 0x02U
#define LGPS_UBX_PVT_FULLY_RESOLVED 0x04U

/* NAV-PVT flags (offset 21) bit masks. */
#define LGPS_UBX_PVT_FLAGS_FIX_OK 0x01U
#define LGPS_UBX_PVT_FLAGS_HEAD_VEH_VALID 0x20U
#define LGPS_UBX_PVT_FLAGS_PSM_STATE_MASK 0x1CU
#define LGPS_UBX_PVT_FLAGS_PSM_STATE_SHIFT 2U

/* NAV-PVT fixType (offset 20) values. */
#define LGPS_UBX_FIX_NONE 0U
#define LGPS_UBX_FIX_DEAD_RECKONING 1U
#define LGPS_UBX_FIX_2D 2U
#define LGPS_UBX_FIX_3D 3U
#define LGPS_UBX_FIX_GNSS_DR 4U
#define LGPS_UBX_FIX_TIME_ONLY 5U

/* RXM-PMREQ flags (payload offset 8) bit masks. */
#define LGPS_UBX_PMREQ_FLAG_BACKUP 0x02U
#define LGPS_UBX_PMREQ_FLAG_FORCE 0x04U

/* RXM-PMREQ wakeupSources (payload offset 12) bit masks. */
#define LGPS_UBX_PMREQ_WAKE_UARTRX 0x08U
#define LGPS_UBX_PMREQ_WAKE_EXTINT0 0x20U

/* Longest RXM-PMREQ duration the receiver accepts (interface description: 12 days). */
#define LGPS_UBX_PMREQ_MAX_DURATION_MS 1036800000U

struct lgps_nav_pvt {
	uint32_t itow_ms;
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t valid;
	uint32_t tacc_ns;
	int32_t nano_ns;
	uint8_t fix_type;
	uint8_t flags;
	uint8_t flags2;
	uint8_t num_sv;
	int32_t lon_e7;      /* 1e-7 deg */
	int32_t lat_e7;      /* 1e-7 deg */
	int32_t height_mm;   /* ellipsoid height */
	int32_t hmsl_mm;     /* mean sea level height */
	uint32_t hacc_mm;
	uint32_t vacc_mm;
	int32_t vel_n_mms;
	int32_t vel_e_mms;
	int32_t vel_d_mms;
	int32_t gspeed_mms;
	int32_t head_mot_1e5; /* 1e-5 deg */
	uint32_t sacc_mms;
	uint32_t headacc_1e5;
	uint16_t pdop_1e2;
	uint16_t flags3;
};

enum lgps_ubx_status {
	/* Byte consumed, no complete message yet. */
	LGPS_UBX_STATUS_NONE = 0,
	/* Complete frame with a valid checksum, payload available in the parser. */
	LGPS_UBX_STATUS_MSG_READY,
	/* Complete frame with a bad checksum, payload must be discarded. */
	LGPS_UBX_STATUS_BAD_CHECKSUM,
};

struct lgps_ubx_parser {
	uint8_t payload[LGPS_UBX_MAX_PAYLOAD_LEN];
	uint16_t idx;
	uint16_t payload_len;
	uint8_t msg_class;
	uint8_t msg_id;
	uint8_t ck_a;
	uint8_t ck_b;
	/* Sticky: CK_A did not match, so the frame must be reported as corrupt even
	 * though only the CK_B byte can tell us the frame has ended. */
	uint8_t ck_mismatch;
	uint8_t state;
};

/*
 * Reset the streaming parser to "waiting for 0xB5".
 * Must be called once before the first lgps_ubx_parser_feed().
 */
void lgps_ubx_parser_reset(struct lgps_ubx_parser *parser);

/*
 * Feed one received byte.
 *
 * Returns LGPS_UBX_STATUS_MSG_READY when the frame is complete and its checksum
 * matches; the caller may then read @c parser->msg_class, @c parser->msg_id,
 * @c parser->payload_len and @c parser->payload. A frame whose declared payload
 * length exceeds LGPS_UBX_MAX_PAYLOAD_LEN is dropped and resynchronised.
 */
enum lgps_ubx_status lgps_ubx_parser_feed(struct lgps_ubx_parser *parser, uint8_t byte);

/*
 * Decode the payload of a NAV-PVT frame previously reported as MSG_READY.
 * Returns 0 on success, -EINVAL on bad class/id/length or NULL arguments.
 */
int lgps_ubx_decode_nav_pvt(const struct lgps_ubx_parser *parser, struct lgps_nav_pvt *out);

/*
 * Build a complete frame (sync, class, id, length, payload, checksum).
 * Returns the number of bytes written, or -ENOSPC if @p buf_len_bytes is too
 * small, or -EINVAL for a NULL buffer or a payload longer than
 * LGPS_UBX_MAX_PAYLOAD_LEN.
 */
int lgps_ubx_build(uint8_t *buf, size_t buf_len_bytes, uint8_t msg_class, uint8_t msg_id,
		   const uint8_t *payload, uint16_t payload_len);

/* Build a poll request (empty payload) for @p msg_class / @p msg_id. */
int lgps_ubx_build_poll(uint8_t *buf, size_t buf_len_bytes, uint8_t msg_class, uint8_t msg_id);

/*
 * Build UBX-RXM-PMREQ (16-byte payload, version 0).
 * @p duration_ms is clamped to LGPS_UBX_PMREQ_MAX_DURATION_MS; 0 means
 * "wait for a wakeup pin event".
 */
int lgps_ubx_build_rxm_pmreq(uint8_t *buf, size_t buf_len_bytes, uint32_t duration_ms, bool backup,
			     bool force, uint32_t wakeup_sources);

#endif /* LIFEGPS_UBX_H_ */
