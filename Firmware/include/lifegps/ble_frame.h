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
#define LGPS_BLE_FW_VERSION_MINOR 2U
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

/* GET_CAPS.caps bits. A bit means "this opcode/feature really works in v1". */
#define LGPS_BLE_CAP_STATUS_NOTIFY 0x00000001U
#define LGPS_BLE_CAP_LOG_NOTIFY 0x00000002U
#define LGPS_BLE_CAP_POI 0x00000004U
#define LGPS_BLE_CAP_FLASH_INFO 0x00000008U
#define LGPS_BLE_CAP_REBOOT 0x00000010U
#define LGPS_BLE_CAP_DEBUG_UNLOCK 0x00000020U
#define LGPS_BLE_CAP_DATA_SYNC 0x00000040U

/* soc_pct is "unknown" until a fuel gauge or a coulomb counter exists; the app
 * derives a percentage from bat_mv instead of the firmware pretending to know.
 */
#define LGPS_BLE_SOC_UNKNOWN 0xFFU

#define LGPS_BLE_STATUS_SIZE 82U
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

/* Telemetry ids, mirroring protocol/lifegps_protocol.yaml `telemetry:`. */
enum lgps_ble_telem_id {
	LGPS_BLE_TELEM_GNSS = 0x01,
	LGPS_BLE_TELEM_IMU = 0x02,
	LGPS_BLE_TELEM_MAG = 0x03,
	LGPS_BLE_TELEM_BARO = 0x04,
	LGPS_BLE_TELEM_PDR = 0x05,
	LGPS_BLE_TELEM_POWER = 0x06,
	LGPS_BLE_TELEM_SYSTEM = 0x07,
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

#endif /* LIFEGPS_BLE_FRAME_H_ */
