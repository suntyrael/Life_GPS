/* SPDX-License-Identifier: MIT */
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
#define LGPS_UBX_ID_NAV_DOP 0x04U
#define LGPS_UBX_ID_NAV_PVT 0x07U
#define LGPS_UBX_ID_NAV_SAT 0x35U
#define LGPS_UBX_ID_NAV_SIG 0x43U
#define LGPS_UBX_CLASS_RXM 0x02U
#define LGPS_UBX_ID_RXM_PMREQ 0x41U
#define LGPS_UBX_CLASS_ACK 0x05U
#define LGPS_UBX_ID_ACK_NAK 0x00U
#define LGPS_UBX_ID_ACK_ACK 0x01U
#define LGPS_UBX_CLASS_CFG 0x06U
#define LGPS_UBX_ID_CFG_VALSET 0x8AU
#define LGPS_UBX_ID_CFG_VALGET 0x8BU
#define LGPS_UBX_ID_CFG_VALDEL 0x8CU

#define LGPS_UBX_NAV_PVT_PAYLOAD_LEN 92U
#define LGPS_UBX_NAV_DOP_PAYLOAD_LEN 18U
#define LGPS_UBX_RXM_PMREQ_PAYLOAD_LEN 16U

/* UBX-NAV-SAT (section 3.15.13): 8-byte header, then 12 bytes per satellite. */
#define LGPS_UBX_NAV_SAT_HEADER_LEN 8U
#define LGPS_UBX_NAV_SAT_SV_LEN 12U
/* Offset of `azim` (I2, deg) inside one 12-byte group. */
#define LGPS_UBX_NAV_SAT_SV_OFF_AZIM 4U

/* UBX-NAV-SIG (section 3.15.15): 8-byte header, then 16 bytes per signal. */
#define LGPS_UBX_NAV_SIG_HEADER_LEN 8U
#define LGPS_UBX_NAV_SIG_SIG_LEN 16U
/* sigFlags (X2 at group offset 10), section 3.15.15.1: bit 3 = prUsed. */
#define LGPS_UBX_NAV_SIG_FLAG_PR_USED 0x0008U

/* NAV-SAT flags (12-byte group, offset +8) bit masks and field positions.
 *
 * Table in interface description section 3.15.13.1: bits 2..0 qualityInd, bit 3
 * svUsed, bits 5..4 health, bit 6 diffCorr, bit 7 smoothed, bits 10..8
 * orbitSource, bit 11 ephAvail, bit 12 almAvail. The two availability bits are
 * what the App table's "A E" columns need.
 */
#define LGPS_UBX_NAV_SAT_QUALITY_MASK 0x07U
#define LGPS_UBX_NAV_SAT_FLAG_SV_USED 0x08U
#define LGPS_UBX_NAV_SAT_FLAG_EPH_AVAIL 0x00000800U
#define LGPS_UBX_NAV_SAT_FLAG_ALM_AVAIL 0x00001000U

/*
 * UBX-CFG-VALSET / UBX-CFG-VALGET (interface description sections 3.10.5 and
 * 3.10.4). These are the only configuration messages the M10 documents: the
 * UBX-CFG chapter of UBX-21035062 R03 contains exactly CFG-CFG, CFG-RST,
 * CFG-VALDEL, CFG-VALGET and CFG-VALSET, and section 4.10 notes that "some
 * legacy UBX-CFG messages may not be available for certain products" - which is
 * why UBX-CFG-MSG (0x06 0x01) never turned NAV-PVT on.
 */
#define LGPS_UBX_CFG_HEADER_LEN 4U

/* VALSET `layers` bits (payload offset 1, section 3.10.5.1). */
#define LGPS_UBX_LAYER_RAM 0x01U
#define LGPS_UBX_LAYER_BBR 0x02U
#define LGPS_UBX_LAYER_FLASH 0x04U
#define LGPS_UBX_LAYER_ALL (LGPS_UBX_LAYER_RAM | LGPS_UBX_LAYER_BBR | LGPS_UBX_LAYER_FLASH)

/* VALGET `layer` field values (payload offset 1, section 3.10.4.1). */
#define LGPS_UBX_VALGET_LAYER_RAM 0U
#define LGPS_UBX_VALGET_LAYER_BBR 1U
#define LGPS_UBX_VALGET_LAYER_FLASH 2U
#define LGPS_UBX_VALGET_LAYER_DEFAULT 7U

/*
 * Largest value a single key we write carries. Every key in the current list is
 * U1/U2/U4/E1/L, so four bytes cover all of them; a longer value is rejected
 * instead of being silently truncated.
 */
#define LGPS_UBX_CFG_VALUE_MAX_LEN 4U

/*
 * Keys one UBX-CFG-VALDEL frame can carry.
 *
 * The receiver accepts up to 64 (section 3.10.3.1), but nothing in this firmware
 * deletes more than its own short rescue list, and the frame buffer is a stack
 * array in a 2 KB GNSS thread: 8 keys is 36 bytes of payload instead of 260.
 */
#define LGPS_UBX_CFG_VALDEL_MAX_KEYS 8U

/*
 * Configuration key: output rate of UBX-NAV-PVT on UART1.
 *
 * Section 4.9.11 (CFG-MSGOUT) lists the three transports per message as
 * I2C 0x20910006, SPI 0x2091000a and UART1 0x20910007; the defaults table
 * (section 4.9.11, "configuration defaults") repeats 0x20910007 with default 0,
 * i.e. NAV-PVT is off until the host turns it on. Our PDF copy prints the UART1
 * row's *name* as a redacted placeholder, so the key id is derived from its two
 * siblings and from the defaults table, and the firmware reads the value back
 * with CFG-VALGET after writing it instead of trusting the constant.
 */
#define LGPS_UBX_CFG_KEY_MSGOUT_NAV_PVT_UART1 0x20910007U

/*
 * Further configuration keys this firmware reads or writes. Ids and types are
 * from section 4.9 of the same document; every one of them is a whitelisted item
 * in gnss.c, never an arbitrary key from the air.
 */
#define LGPS_UBX_CFG_KEY_MSGOUT_NAV_SAT_UART1 0x20910016U /* U1, section 4.9.11 */
#define LGPS_UBX_CFG_KEY_MSGOUT_NAV_DOP_UART1 0x20910039U /* U1, section 4.9.11, default 0 */
#define LGPS_UBX_CFG_KEY_MSGOUT_NAV_SIG_UART1 0x20910346U /* U1, section 4.9.11, default 0 */
#define LGPS_UBX_CFG_KEY_ANA_USE 0x10230001U              /* L, section 4.9.1 */
#define LGPS_UBX_CFG_KEY_ANA_ORBMAXERR 0x30230002U        /* U2, section 4.9.1 */
#define LGPS_UBX_CFG_KEY_DYNMODEL 0x20110021U             /* E1, section 4.9.13 */
#define LGPS_UBX_CFG_KEY_SIGNAL_GPS_ENA 0x1031001FU       /* L, section 4.9.20 */
#define LGPS_UBX_CFG_KEY_SIGNAL_GAL_ENA 0x10310021U
#define LGPS_UBX_CFG_KEY_SIGNAL_BDS_ENA 0x10310022U
#define LGPS_UBX_CFG_KEY_SIGNAL_GLO_ENA 0x10310025U
#define LGPS_UBX_CFG_KEY_INFIL_MINELEV 0x201100A4U         /* I1, section 4.9.13 */
#define LGPS_UBX_CFG_KEY_OUTFIL_PDOP 0x301100B1U           /* U2 0.1, section 4.9.13 */
#define LGPS_UBX_CFG_KEY_RATE_MEAS 0x30210001U             /* U2 0.001 s, section 4.9.17 */

/*
 * Per-signal enables (section 4.9.21, table 42). They are not decoration: on
 * MAX-M10S the signal combination decides whether a write is accepted at all.
 * Integration manual section 2.1.3: "BeiDou B1I signal cannot be used
 * simultaneously with the BeiDou B1C or GLONASS L1OF signals. Unsupported
 * combinations are rejected with a UBX-ACK-NAK message". The factory profile is
 * GPS + Galileo + BeiDou B1I, so CFG-SIGNAL-GLO_ENA alone can never be accepted -
 * B1I has to be switched off first (or replaced by B1C in the same profile).
 */
#define LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1_ENA 0x1031000DU  /* L, BeiDou B1I */
#define LGPS_UBX_CFG_KEY_SIGNAL_BDS_B1C_ENA 0x1031000FU /* L, BeiDou B1C */
#define LGPS_UBX_CFG_KEY_SIGNAL_GLO_L1_ENA 0x10310018U  /* L, GLONASS L1OF */

/* Power management (section 4.9.15, table 33). Lengths differ: the periods are
 * U4, ONTIME is U2 and the rest are one-byte E1/L items.
 */
#define LGPS_UBX_CFG_KEY_PM_OPERATEMODE 0x20D00001U    /* E1, 0 FULL / 1 PSMOO / 2 PSMCT */
#define LGPS_UBX_CFG_KEY_PM_POSUPDATEPERIOD 0x40D00002U /* U4, s */
#define LGPS_UBX_CFG_KEY_PM_ACQPERIOD 0x40D00003U      /* U4, s */
#define LGPS_UBX_CFG_KEY_PM_ONTIME 0x30D00005U         /* U2, s */
#define LGPS_UBX_CFG_KEY_PM_DONOTENTEROFF 0x10D00008U  /* L */

/* Motion detector (section 4.9.10, table 18): the receiver's own static-hold
 * thresholds, i.e. the same idea as the firmware's ZUPT test.
 */
#define LGPS_UBX_CFG_KEY_MOT_GNSSSPEED_THRS 0x20250038U /* U1 0.01 m/s */
#define LGPS_UBX_CFG_KEY_MOT_GNSSDIST_THRS 0x3025003BU  /* U2, m */

/* Rate ratio and signal-quality gate (sections 4.9.17 and 4.9.12). */
#define LGPS_UBX_CFG_KEY_RATE_NAV 0x30210002U   /* U2, measurements per solution */
#define LGPS_UBX_CFG_KEY_INFIL_MINCNO 0x201100A3U /* U1, dBHz */

/*
 * Interface items this firmware writes without exposing them to the BLE host.
 *
 * CFG-UART1OUTPROT-NMEA alternates all NMEA output on UART1 in one key (section
 * 4.9.29, table 57): the factory set costs about 1.25 kB per epoch, which is
 * 12 kB/s at 10 Hz and does not fit the 460800 link the fast profile opens.
 *
 * CFG-UART1-BAUDRATE is the one item that can lose the link for good, which is
 * why it is written to the RAM layer only: RXM-PMREQ standby clears RAM
 * (integration manual section 3.6.3.2), so the receiver always comes back up at
 * the rate its flash holds, and the host is free to power-cycle at 38400.
 */
#define LGPS_UBX_CFG_KEY_UART1OUTPROT_NMEA 0x10740002U /* L, section 4.9.29 */
#define LGPS_UBX_CFG_KEY_UART1_BAUDRATE 0x40520001U    /* U4, section 4.9.27 */

/*
 * Largest payload the streaming parser accepts. UBX-NAV-PVT is 92 bytes, but
 * UBX-NAV-SAT scales with the number of satellites in view (8 + 12 * numSvs), so
 * the buffer covers the 32-satellite worst case the M10 can report.
 */
#define LGPS_UBX_MAX_PAYLOAD_LEN (LGPS_UBX_NAV_SAT_HEADER_LEN + (32U * LGPS_UBX_NAV_SAT_SV_LEN))

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
 * Build UBX-CFG-VALSET (version 0, section 3.10.5.1) carrying exactly one
 * key/value pair.
 *
 * @p layers is a mask of LGPS_UBX_LAYER_*; a mask without any layer is rejected
 * with -EINVAL because the receiver answers it with UBX-ACK-NAK and applies
 * nothing. @p value_len must be 1..LGPS_UBX_CFG_VALUE_MAX_LEN.
 *
 * Returns the number of bytes written, -EINVAL for a bad argument or -ENOSPC
 * when @p buf_len_bytes is too small.
 */
int lgps_ubx_build_valset_item(uint8_t *buf, size_t buf_len_bytes, uint8_t layers, uint32_t key,
			       const uint8_t *value, uint8_t value_len);

/*
 * Build UBX-CFG-VALGET (version 0, section 3.10.4.1) asking for exactly one key.
 * @p layer is one of LGPS_UBX_VALGET_LAYER_*.
 */
int lgps_ubx_build_valget_item(uint8_t *buf, size_t buf_len_bytes, uint8_t layer, uint32_t key);

/*
 * Build UBX-CFG-VALDEL (version 0, section 3.10.3.1) deleting @p count keys from
 * the BBR and/or Flash layer.
 *
 * This is the only way to undo a saved value that wedges the receiver: the BBR
 * layer sits in V_BCKP-backed memory, so dropping the rail does not clear it, and a
 * receiver that stopped answering cannot be asked to write a new value. Deleting
 * the key makes the next configuration load fall back to the factory default.
 * Only the BBR and Flash layers are addressable here (RAM is not a valid target for
 * this message), and the effect appears after the next reset.
 *
 * Returns the number of bytes written, -EINVAL for a bad argument or -ENOSPC when
 * @p buf_len_bytes is too small.
 */
int lgps_ubx_build_valdel(uint8_t *buf, size_t buf_len_bytes, uint8_t layers, const uint32_t *keys,
			  uint8_t count);

/*
 * Classify a UBX-ACK frame (section 3.9): @p accepted is true for ACK-ACK and
 * false for ACK-NAK; @p msg_class / @p msg_id identify the message that was
 * acknowledged, so the caller can name it instead of guessing.
 *
 * Returns 0 on success, -EINVAL on a NULL argument or a frame that is not a
 * well-formed ACK (wrong class/id or payload length other than 2).
 */
int lgps_ubx_decode_ack(const struct lgps_ubx_parser *parser, bool *accepted, uint8_t *msg_class,
			uint8_t *msg_id);

/*
 * Read the value of one U1 item out of a UBX-CFG-VALGET response (section
 * 3.10.4.1: version, layer, position u2, then key/value pairs). Only the first
 * key of a single-key response is considered; @p key must match it.
 *
 * Returns 0 on success, -EINVAL for a NULL argument, a non-VALGET frame, a
 * truncated payload or a different key, and -ENOENT when the requested key is
 * not the one the receiver answered with.
 */
int lgps_ubx_decode_valget_u1(const struct lgps_ubx_parser *parser, uint32_t key, uint8_t *value);

/*
 * Same, for any item width: copies up to @p max_len value bytes and reports the
 * real width in @p value_len (1..4). A response whose key does not match is
 * reported as -ENOENT instead of being guessed at.
 */
int lgps_ubx_decode_valget(const struct lgps_ubx_parser *parser, uint32_t key, uint8_t *value,
			   uint8_t max_len, uint8_t *value_len);

/* One satellite entry as the App needs it (interface description 3.15.13.1). */
#define LGPS_UBX_NAV_SAT_MAX_SV 32U

struct lgps_nav_sat {
	uint8_t gnss_id;
	uint8_t sv_id;
	uint8_t cno;    /* dBHz */
	int8_t elev;    /* deg, unknown if out of range */
	int16_t azim;   /* deg, -1 when the receiver leaves it unknown */
	uint8_t quality; /* qualityInd 0..7, bit 3 of the flags field carries svUsed */
	bool used;
	bool eph_avail; /* flags bit 11 */
	bool alm_avail; /* flags bit 12 */
};

/*
 * Decode the payload of a NAV-SAT frame previously reported as MSG_READY.
 *
 * Returns 0 on success and writes the number of entries to @p count; at most
 * @p max_sv entries are stored. -EINVAL on a NULL argument, a non-NAV-SAT frame
 * or a payload too short for the declared satellite count.
 */
int lgps_ubx_decode_nav_sat(const struct lgps_ubx_parser *parser, struct lgps_nav_sat *out,
			    uint8_t max_sv, uint8_t *count);

/*
 * The three DOP fields the App shows next to pDOP (interface description section
 * 3.15.4.1, 18-byte payload). pDOP itself comes from NAV-PVT; hDOP/vDOP only
 * exist in this message, which is why the firmware has to ask for it separately.
 */
struct lgps_ubx_nav_dop {
	uint16_t pdop_1e2;
	uint16_t vdop_1e2;
	uint16_t hdop_1e2;
};

/*
 * Decode a UBX-NAV-DOP payload previously reported as MSG_READY.
 *
 * Returns 0 on success, -EINVAL on a NULL argument, another message class/id or a
 * payload shorter than LGPS_UBX_NAV_DOP_PAYLOAD_LEN.
 */
int lgps_ubx_decode_nav_dop(const struct lgps_ubx_parser *parser, struct lgps_ubx_nav_dop *out);

/*
 * One tracked signal (interface description section 3.15.15.1, 16-byte group).
 * NAV-SIG is the only message that names the signal behind a C/N0 value, so it is
 * what turns the App's `CF` column into L1/E1/B1I/B1C instead of a guess.
 */
#define LGPS_UBX_NAV_SIG_MAX 32U

struct lgps_ubx_nav_sig {
	uint8_t gnss_id;
	uint8_t sv_id;
	uint8_t sig_id; /* new style signal id, section 1.5.4 */
	uint8_t cno;    /* dBHz */
	uint8_t quality; /* qualityInd 0..7 */
	bool used;      /* sigFlags bit 3: pseudorange used in the solution */
};

/*
 * Decode a UBX-NAV-SIG payload previously reported as MSG_READY.
 *
 * Returns 0 on success and writes the number of signals to @p count; at most
 * @p max entries are stored. -EINVAL on a NULL argument, another message, or a
 * payload too short for the declared signal count.
 */
int lgps_ubx_decode_nav_sig(const struct lgps_ubx_parser *parser, struct lgps_ubx_nav_sig *out,
			    uint8_t max, uint8_t *count);

/*
 * Build UBX-RXM-PMREQ (16-byte payload, version 0).
 * @p duration_ms is clamped to LGPS_UBX_PMREQ_MAX_DURATION_MS; 0 means
 * "wait for a wakeup pin event".
 */
int lgps_ubx_build_rxm_pmreq(uint8_t *buf, size_t buf_len_bytes, uint32_t duration_ms, bool backup,
			     bool force, uint32_t wakeup_sources);

#endif /* LIFEGPS_UBX_H_ */
