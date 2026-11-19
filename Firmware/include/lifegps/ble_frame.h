/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - BLE wire codec (pure, no Bluetooth stack needed).
 *
 * The bytes on the air are specified by Doc/Software/BLE_Wire_v1.md, not by this
 * header. Frame envelopes (magic/ver/seq/crc) and the record layouts come from
 * protocol/lifegps_protocol.yaml through generated/lifegps_protocol.h; this file
 * only adds the two payload layouts that the YAML does not describe yet:
 *
 *   - the 82-byte Status snapshot (one fixed frame per second)
 *   - the GET_CAPS / FLASH_INFO response bodies
 *
 * Everything here is dependency free (stdint/errno only) so it can be unit
 * tested on native_sim and host-compiled while the radio code is unavailable.
 * Fields are written byte by byte instead of by struct overlay: the compiler is
 * then free to pad struct lgps_ble_status, and a future field addition cannot
 * silently change the wire offsets.
 */

#ifndef LIFEGPS_BLE_FRAME_H_
#define LIFEGPS_BLE_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <generated/lifegps_protocol.h>

/* Version byte of every frame this firmware speaks. Bump only on a breaking
 * change; Doc/Software/BLE_Wire_v1.md section 7 defines what that means.
 */
#define LGPS_BLE_WIRE_VER 1U

/*
 * Firmware version reported by GET_CAPS. Bump MAJOR/MINOR when a BLE-visible
 * behaviour changes (new opcode, new status field), PATCH for fixes that do not
 * touch the wire contract. Kept next to the encoder that ships it, so the number
 * a phone reads cannot disagree with the layout it parses.
 */
#define LGPS_BLE_FW_VERSION_MAJOR 0U
#define LGPS_BLE_FW_VERSION_MINOR 3U
#define LGPS_BLE_FW_VERSION_PATCH 0U

/* Largest command frame: header + max payload + crc16. */
#define LGPS_BLE_CMD_MAX_FRAME                                                \
	(LGPS_FRAME_COMMAND_HEADER_SIZE + LGPS_PROTO_MAX_COMMAND_PAYLOAD +     \
	 LGPS_FRAME_COMMAND_TRAILER_SIZE)

/* Command response status, first payload byte of every response. */
enum lgps_ble_status_code {
	LGPS_BLE_OK = 0x00,
	LGPS_BLE_INVALID_ARGUMENT = 0x01,
	LGPS_BLE_UNKNOWN_OPCODE = 0x02,
	LGPS_BLE_NOT_SUPPORTED = 0x03,
	LGPS_BLE_BUSY = 0x04,
	LGPS_BLE_INTERNAL_ERROR = 0x05,
	LGPS_BLE_UNAUTHORIZED = 0x06,
};

/* flags byte of a response frame: bit0 marks a response, the rest stays 0. */
#define LGPS_BLE_CMD_FLAG_RESPONSE 0x01U

/* Status.flags, Doc/Software/BLE_Wire_v1.md section 4. */
#define LGPS_BLE_STAT_FIX_VALID 0x01U
#define LGPS_BLE_STAT_CHARGING 0x02U
#define LGPS_BLE_STAT_ZUPT 0x04U
#define LGPS_BLE_STAT_IMU_VALID 0x08U
#define LGPS_BLE_STAT_MAG_VALID 0x10U
#define LGPS_BLE_STAT_BARO_VALID 0x20U
#define LGPS_BLE_STAT_RING_READY 0x40U
#define LGPS_BLE_STAT_UTC_VALID 0x80U

/* Status frame `sensor_flags` bits, appended fields only (offsets 82..99). */
#define LGPS_BLE_SENSOR_GRAV_VALID 0x01U
#define LGPS_BLE_SENSOR_IMU_TEMP_VALID 0x02U
#define LGPS_BLE_SENSOR_MAG_TEMP_VALID 0x04U
#define LGPS_BLE_SENSOR_BARO_TEMP_VALID 0x08U

/* GET_CAPS.caps bits. A bit means "this opcode/feature really works in v1". */
#define LGPS_BLE_CAP_STATUS_NOTIFY 0x00000001U
#define LGPS_BLE_CAP_LOG_NOTIFY 0x00000002U
#define LGPS_BLE_CAP_POI 0x00000004U
#define LGPS_BLE_CAP_FLASH_INFO 0x00000008U
#define LGPS_BLE_CAP_REBOOT 0x00000010U
#define LGPS_BLE_CAP_DEBUG_UNLOCK 0x00000020U
#define LGPS_BLE_CAP_DATA_SYNC 0x00000040U
/* Calibration, advertised only when CONFIG_LGPS_CALIB built the handlers in. */
#define LGPS_BLE_CAP_MAG_CAL 0x00000080U
#define LGPS_BLE_CAP_ACCEL_CAL 0x00000100U
#define LGPS_BLE_CAP_BARO_BASELINE 0x00000200U
/* GNSS debugging: 0x36 debug session, 0x35 whitelisted receiver settings,
 * 0x08/0x09 telemetry groups. Advertised separately because a host must not
 * offer a settings screen against a firmware that would answer 0x03.
 */
#define LGPS_BLE_CAP_GNSS_DEBUG 0x00000400U
#define LGPS_BLE_CAP_GNSS_CFG 0x00000800U

/*
 * GET_CAPS.telemetry_mask bits: bit n set means the telemetry group whose id is
 * n is implemented (bit 0 is unused because no group has id 0). The App reads
 * this instead of guessing from the protocol version, the same rule the caps
 * bits already follow.
 */
#define LGPS_BLE_TELEM_MASK_GNSS 0x00000002U /* id 0x01 */
#define LGPS_BLE_TELEM_MASK_IMU 0x00000004U  /* id 0x02 */
#define LGPS_BLE_TELEM_MASK_MAG 0x00000008U  /* id 0x03 */
#define LGPS_BLE_TELEM_MASK_BARO 0x00000010U /* id 0x04 */
#define LGPS_BLE_TELEM_MASK_PDR 0x00000020U  /* id 0x05 */
#define LGPS_BLE_TELEM_MASK_POWER 0x00000040U  /* id 0x06 */
#define LGPS_BLE_TELEM_MASK_SYSTEM 0x00000080U /* id 0x07 */
#define LGPS_BLE_TELEM_MASK_NMEA 0x00000100U  /* id 0x08 */
#define LGPS_BLE_TELEM_MASK_SAT 0x00000200U   /* id 0x09 */

/* soc_pct is "unknown" until a fuel gauge or a coulomb counter exists; the app
 * derives a percentage from bat_mv instead of the firmware pretending to know.
 */
#define LGPS_BLE_SOC_UNKNOWN 0xFFU

/* Status body: 0..81 as v1, then the appended motion/temperature block. The size
 * grew from 82 to 100; older hosts must treat a short body as "fields absent".
 */
#define LGPS_BLE_STATUS_BASE_SIZE 82U
#define LGPS_BLE_STATUS_SIZE 100U
#define LGPS_BLE_CAPS_SIZE 24U
#define LGPS_BLE_FLASH_INFO_SIZE 24U
#define LGPS_BLE_LOG_MAX_PACKET 240U

/*
 * Telemetry characteristic (4C475053-0005-...): one small packet per sensor
 * group instead of one wide status frame, so the phone can get the attitude at
 * the IMU rate and the scalar snapshot at its own rate.
 *
 * Header: id u8, ver u8, seq u16 (free running per id).
 */
#define LGPS_BLE_TELEM_HEADER_SIZE 4U
#define LGPS_BLE_TELEM_IMU_PAYLOAD 20U
#define LGPS_BLE_TELEM_IMU_SIZE (LGPS_BLE_TELEM_HEADER_SIZE + LGPS_BLE_TELEM_IMU_PAYLOAD)
#define LGPS_BLE_TELEM_PDR_PAYLOAD 12U
#define LGPS_BLE_TELEM_PDR_SIZE (LGPS_BLE_TELEM_HEADER_SIZE + LGPS_BLE_TELEM_PDR_PAYLOAD)
#define LGPS_BLE_TELEM_GNSS_PAYLOAD 34U
#define LGPS_BLE_TELEM_GNSS_SIZE (LGPS_BLE_TELEM_HEADER_SIZE + LGPS_BLE_TELEM_GNSS_PAYLOAD)

/* Satellite table: 8-byte payload header, then 5 bytes per satellite. */
#define LGPS_BLE_TELEM_SAT_HEADER 4U
#define LGPS_BLE_TELEM_SAT_ENTRY 5U
#define LGPS_BLE_TELEM_SAT_MAX 32U
#define LGPS_BLE_TELEM_SAT_PAYLOAD(sat_count)                                                 \
	((uint16_t)(LGPS_BLE_TELEM_SAT_HEADER + ((uint16_t)(sat_count) * LGPS_BLE_TELEM_SAT_ENTRY)))

/* One NMEA sentence per packet: u8 length, then that many ASCII bytes. */
#define LGPS_BLE_TELEM_NMEA_MAX 128U

/* Telemetry ids, mirroring protocol/lifegps_protocol.yaml `telemetry:`. */
enum lgps_ble_telem_id {
	LGPS_BLE_TELEM_GNSS = 0x01,
	LGPS_BLE_TELEM_IMU = 0x02,
	LGPS_BLE_TELEM_MAG = 0x03,
	LGPS_BLE_TELEM_BARO = 0x04,
	LGPS_BLE_TELEM_PDR = 0x05,
	LGPS_BLE_TELEM_POWER = 0x06,
	LGPS_BLE_TELEM_SYSTEM = 0x07,
	LGPS_BLE_TELEM_NMEA = 0x08,
	LGPS_BLE_TELEM_SAT = 0x09,
};

/* Body-frame IMU sample plus the quaternion estimate (Q15, 32767 = 1.0). */
struct lgps_ble_telem_imu {
	int16_t acc_lsb[3];
	int16_t gyro_lsb[3];
	int16_t quat_q15[4];
};

/* PDR position in millimetres (x east, y north) with the current heading. */
struct lgps_ble_telem_pdr {
	int32_t x_mm;
	int32_t y_mm;
	int16_t heading_ddeg;
	uint16_t steps;
};

/* GNSS receive path, not the solution: the Status frame already carries the
 * solution, this group answers "is the receiver talking to us at all".
 * Layout is documented in Doc/Software/BLE_Wire_v1.md section 5.1.
 */
#define LGPS_BLE_TELEM_GNSS_FLAG_FIX_VALID 0x01U
#define LGPS_BLE_TELEM_GNSS_FLAG_UTC_VALID 0x02U

/* One satellite row, already reduced to what the App table shows. */
struct lgps_ble_telem_sat_entry {
	uint8_t gnss_id;
	uint8_t sv_id;
	uint8_t cno;
	int8_t elev;
	uint8_t state; /* bits 2..0 qualityInd, bit 3 svUsed */
};

struct lgps_ble_telem_gnss {
	uint8_t state;
	uint8_t fix_type;
	uint8_t satellites;
	uint8_t flags;
	uint16_t pdop_1e2;
	uint32_t age_ms;
	uint32_t rx_bytes;
	uint32_t ubx_frames;
	uint32_t ubx_bad_checksum;
	uint32_t nmea_starts;
	uint32_t rx_dropped;
	uint32_t baud;
};

/* Status snapshot, field semantics in Doc/Software/BLE_Wire_v1.md section 4. */
struct lgps_ble_status {
	uint8_t flags;
	uint8_t gnss_state;
	uint8_t fix_type;
	uint8_t satellites;
	uint8_t key_levels;
	uint8_t soc_pct;
	uint16_t pdop_1e2;
	uint16_t speed_cms;
	uint16_t heading_ddeg;
	int32_t lat_e7;
	int32_t lon_e7;
	int32_t hmsl_mm;
	int32_t bat_mv;
	uint32_t utc_epoch_s;
	uint32_t uptime_ms;
	uint32_t gnss_age_ms;
	uint32_t steps;
	int16_t acc_lsb[3];
	int16_t gyro_lsb[3];
	int16_t mag_lsb[3];
	uint32_t baro_press_raw;
	uint32_t baro_temp_raw;
	uint32_t ring_sector_seq;
	uint32_t ring_records;
	/* Appended block: gravity and linear acceleration in the body frame (milli-g,
	 * 1000 = 1 g) so the host does not have to guess which frame an accelerometer
	 * sample is in, plus the die temperatures in 0.01 degC.
	 */
	int16_t gravity_mg[3];
	int16_t linear_mg[3];
	int16_t imu_temp_cc;
	int16_t mag_temp_cc;
	uint8_t sensor_flags;
};

/* Parsed view of an incoming command frame; payload points into the caller's
 * buffer, nothing is copied.
 */
struct lgps_ble_cmd {
	uint8_t opcode;
	uint8_t flags;
	uint16_t seq;
	uint16_t payload_len;
	const uint8_t *payload;
};

struct lgps_ble_caps {
	uint32_t caps;
	uint32_t telemetry_mask;
	uint16_t att_mtu;
};

struct lgps_ble_flash_info {
	uint32_t flash_bytes;
	uint32_t ota_slot1_bytes;
	uint32_t traj_bytes;
	uint32_t sector_seq;
	uint32_t records_written;
	uint8_t records_in_sector;
	uint8_t erase_count_max;
	uint8_t sector_count;
};

/**
 * Calibration commands (Doc/Software/BLE_Wire_v1.md 6.3~6.5, protocol 0.2.0).
 *
 * The bodies are byte-wise like every other payload in this header; the field
 * order below is the offset map and nothing here may be reordered without a
 * protocol version bump.
 */
#define LGPS_BLE_MAG_CAL_REQ_SIZE 1U
#define LGPS_BLE_ACCEL_CAL_REQ_SIZE 2U
#define LGPS_BLE_BARO_BASELINE_REQ_SIZE 5U
#define LGPS_BLE_MAG_CAL_RSP_SIZE 30U
#define LGPS_BLE_ACCEL_CAL_RSP_SIZE 48U
#define LGPS_BLE_BARO_BASELINE_RSP_SIZE 8U

/* Response status byte, first byte of every response payload. */
enum lgps_ble_calib_action {
	LGPS_BLE_CALIB_START = 0,
	LGPS_BLE_CALIB_FINISH_SAVE = 1,
	LGPS_BLE_CALIB_CLEAR = 2,
	LGPS_BLE_CALIB_QUERY = 3,
	LGPS_BLE_CALIB_CANCEL = 4,
};

struct lgps_ble_mag_cal_req {
	uint8_t action;
};

struct lgps_ble_accel_cal_req {
	uint8_t action;
	uint8_t position;
};

struct lgps_ble_baro_baseline_req {
	uint8_t action;
	uint32_t baseline_pa_q4;
};

struct lgps_ble_mag_cal_rsp {
	uint16_t progress_permille;
	uint8_t sample_count;
	uint8_t coverage_pct[3];
	int16_t bias_lsb[3];
	int32_t scale_ppm[3];
	uint8_t state;
	uint8_t flags;
	uint32_t last_calib_unix_s;
};

struct lgps_ble_accel_cal_rsp {
	uint16_t progress_permille;
	uint8_t face_mask;
	uint8_t face_state;
	uint8_t rest_pct;
	uint8_t detected_face;
	int32_t bias_ug[3];
	int32_t scale_ppm[3];
	int32_t gyro_bias_mdps[3];
	uint8_t state;
	uint8_t flags;
	uint32_t last_calib_unix_s;
};

/*
 * Validate and split a command frame.
 * Returns 0 on success, -EINVAL on NULL/short input, -EBADMSG when magic,
 * version, payload_len or the CRC-16 does not match (a truncated or corrupted
 * ATT write must never reach the dispatcher).
 */
int lgps_ble_cmd_parse(const uint8_t *frame, size_t len_bytes, struct lgps_ble_cmd *out);

/*
 * Serialise a command/response frame into @p out.
 * Returns the frame length, or -EINVAL on NULL/oversized payload, -ENOMEM when
 * @p out is too small.
 */
int lgps_ble_cmd_build(uint8_t opcode, uint8_t flags, uint16_t seq, const uint8_t *payload,
		       uint16_t payload_len, uint8_t *out, size_t out_size);

/* Serialise the status snapshot. Returns LGPS_BLE_STATUS_SIZE or -EINVAL/-ENOMEM. */
int lgps_ble_status_encode(const struct lgps_ble_status *status, uint8_t *out, size_t out_size);

/* Serialise a GET_CAPS body. Returns LGPS_BLE_CAPS_SIZE or -EINVAL/-ENOMEM. */
int lgps_ble_caps_encode(const struct lgps_ble_caps *caps, uint8_t *out, size_t out_size);

/* Serialise an FLASH_INFO body. Returns LGPS_BLE_FLASH_INFO_SIZE or -EINVAL/-ENOMEM. */
int lgps_ble_flash_info_encode(const struct lgps_ble_flash_info *info, uint8_t *out,
			       size_t out_size);

/*
 * Calibration codecs. The parse helpers return 0 on success, -EINVAL on a NULL or
 * wrong-length payload (the dispatcher answers LGPS_BLE_INVALID_ARGUMENT) and
 * never read past @p len_bytes. The encoders return the body length or
 * -EINVAL / -ENOMEM.
 */
int lgps_ble_mag_cal_req_parse(const uint8_t *payload, uint16_t len_bytes,
			       struct lgps_ble_mag_cal_req *out);
int lgps_ble_accel_cal_req_parse(const uint8_t *payload, uint16_t len_bytes,
				 struct lgps_ble_accel_cal_req *out);
int lgps_ble_baro_baseline_req_parse(const uint8_t *payload, uint16_t len_bytes,
				     struct lgps_ble_baro_baseline_req *out);
int lgps_ble_mag_cal_rsp_encode(const struct lgps_ble_mag_cal_rsp *rsp, uint8_t *out,
				size_t out_size);
int lgps_ble_accel_cal_rsp_encode(const struct lgps_ble_accel_cal_rsp *rsp, uint8_t *out,
				  size_t out_size);
int lgps_ble_baro_baseline_rsp_encode(uint32_t baseline_pa_q4, int32_t rel_alt_mm,
				      uint8_t *out, size_t out_size);

/*
 * Serialise one telemetry packet (header + caller supplied payload).
 * Returns the packet length, or -EINVAL on NULL/oversized payload, -ENOMEM when
 * @p out is too small.
 */
int lgps_ble_telem_encode(uint8_t id, uint16_t seq, const uint8_t *payload, uint16_t payload_len,
			  uint8_t *out, size_t out_size);

/* IMU telemetry packet. Returns LGPS_BLE_TELEM_IMU_SIZE or a negative errno. */
int lgps_ble_telem_imu_encode(uint16_t seq, const struct lgps_ble_telem_imu *imu, uint8_t *out,
			      size_t out_size);

/* PDR telemetry packet. Returns LGPS_BLE_TELEM_PDR_SIZE or a negative errno. */
int lgps_ble_telem_pdr_encode(uint16_t seq, const struct lgps_ble_telem_pdr *pdr, uint8_t *out,
			      size_t out_size);

/* GNSS receive-path telemetry packet. Returns LGPS_BLE_TELEM_GNSS_SIZE or a
 * negative errno.
 */
int lgps_ble_telem_gnss_encode(uint16_t seq, const struct lgps_ble_telem_gnss *gnss, uint8_t *out,
			       size_t out_size);

/*
 * Satellite table packet: u8 count, u8 total, u16 reserved, then @p count rows of
 * 5 bytes. @p total is the number of satellites the receiver reported, so a host
 * can tell "we capped the list" from "this is all of them".
 */
int lgps_ble_telem_sat_encode(uint16_t seq, const struct lgps_ble_telem_sat_entry *entries,
			      uint8_t count, uint8_t total, uint8_t *out, size_t out_size);

/* One NMEA sentence (ASCII, no terminator) in one packet. */
int lgps_ble_telem_nmea_encode(uint16_t seq, const char *line, uint8_t len, uint8_t *out,
			       size_t out_size);

#endif /* LIFEGPS_BLE_FRAME_H_ */
