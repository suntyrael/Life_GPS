/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - BLE wire codec.
 *
 * Pure byte shuffling: no kernel, no Bluetooth, no allocation, no global state.
 * Offsets are spelled out one field at a time so the compiler's struct padding
 * can never leak into the air format (see lifegps/ble_frame.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>

#include <lifegps/ble_frame.h>
#include <lifegps/crc.h>

/* --- little-endian helpers ------------------------------------------------- */

static void put_u16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_u32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
	out[2] = (uint8_t)((value >> 16) & 0xFFU);
	out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void put_i16(uint8_t *out, int16_t value)
{
	/* The conversion is defined: int16_t -> uint16_t is modulo 2^16. */
	put_u16(out, (uint16_t)value);
}

static void put_i32(uint8_t *out, int32_t value)
{
	put_u32(out, (uint32_t)value);
}

static uint16_t get_u16(const uint8_t *in)
{
	return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

/* --- frames ---------------------------------------------------------------- */

int lgps_ble_cmd_parse(const uint8_t *frame, size_t len_bytes, struct lgps_ble_cmd *out)
{
	uint16_t payload_len;
	uint16_t stored_crc;
	uint16_t computed_crc;

	if ((frame == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	if (len_bytes < (size_t)LGPS_FRAME_COMMAND_HEADER_SIZE + LGPS_FRAME_COMMAND_TRAILER_SIZE) {
		return -EINVAL;
	}

	if (frame[LGPS_FRAME_COMMAND_OFF_MAGIC] != (uint8_t)LGPS_FRAME_COMMAND_MAGIC) {
		return -EBADMSG;
	}

	if (frame[LGPS_FRAME_COMMAND_OFF_VER] != (uint8_t)LGPS_BLE_WIRE_VER) {
		return -EBADMSG;
	}

	payload_len = get_u16(&frame[LGPS_FRAME_COMMAND_OFF_PAYLOAD_LEN]);

	/* Exact match: a short ATT write is a truncated frame, not a frame with an
	 * "unknown" payload length, and must not be dispatched.
	 */
	if (payload_len > (uint16_t)LGPS_PROTO_MAX_COMMAND_PAYLOAD) {
		return -EBADMSG;
	}

	if ((size_t)payload_len + LGPS_FRAME_COMMAND_HEADER_SIZE +
		    LGPS_FRAME_COMMAND_TRAILER_SIZE !=
	    len_bytes) {
		return -EBADMSG;
	}

	stored_crc = get_u16(&frame[len_bytes - LGPS_FRAME_COMMAND_TRAILER_SIZE]);
	computed_crc = lgps_crc16_ccitt_false(frame, len_bytes - LGPS_FRAME_COMMAND_TRAILER_SIZE);
	if (stored_crc != computed_crc) {
		return -EBADMSG;
	}

	out->opcode = frame[LGPS_FRAME_COMMAND_OFF_OPCODE];
	out->flags = frame[LGPS_FRAME_COMMAND_OFF_FLAGS];
	out->seq = get_u16(&frame[LGPS_FRAME_COMMAND_OFF_SEQ]);
	out->payload_len = payload_len;
	out->payload = (payload_len == 0U) ? NULL : &frame[LGPS_FRAME_COMMAND_HEADER_SIZE];

	return 0;
}

int lgps_ble_cmd_build(uint8_t opcode, uint8_t flags, uint16_t seq, const uint8_t *payload,
		       uint16_t payload_len, uint8_t *out, size_t out_size)
{
	size_t frame_len;
	uint16_t crc;

	if (out == NULL) {
		return -EINVAL;
	}

	if (payload_len > (uint16_t)LGPS_PROTO_MAX_COMMAND_PAYLOAD) {
		return -EINVAL;
	}

	if ((payload_len != 0U) && (payload == NULL)) {
		return -EINVAL;
	}

	frame_len = (size_t)LGPS_FRAME_COMMAND_HEADER_SIZE + payload_len +
		    LGPS_FRAME_COMMAND_TRAILER_SIZE;
	if (out_size < frame_len) {
		return -ENOMEM;
	}

	out[LGPS_FRAME_COMMAND_OFF_MAGIC] = (uint8_t)LGPS_FRAME_COMMAND_MAGIC;
	out[LGPS_FRAME_COMMAND_OFF_VER] = (uint8_t)LGPS_BLE_WIRE_VER;
	out[LGPS_FRAME_COMMAND_OFF_OPCODE] = opcode;
	out[LGPS_FRAME_COMMAND_OFF_FLAGS] = flags;
	put_u16(&out[LGPS_FRAME_COMMAND_OFF_SEQ], seq);
	put_u16(&out[LGPS_FRAME_COMMAND_OFF_PAYLOAD_LEN], payload_len);

	if (payload_len != 0U) {
		memcpy(&out[LGPS_FRAME_COMMAND_HEADER_SIZE], payload, payload_len);
	}

	crc = lgps_crc16_ccitt_false(out, frame_len - LGPS_FRAME_COMMAND_TRAILER_SIZE);
	put_u16(&out[frame_len - LGPS_FRAME_COMMAND_TRAILER_SIZE], crc);

	return (int)frame_len;
}

/* --- payloads -------------------------------------------------------------- */

int lgps_ble_status_encode(const struct lgps_ble_status *status, uint8_t *out, size_t out_size)
{
	if ((status == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	if (out_size < LGPS_BLE_STATUS_SIZE) {
		return -ENOMEM;
	}

	out[0] = (uint8_t)LGPS_BLE_WIRE_VER;
	out[1] = status->flags;
	out[2] = status->gnss_state;
	out[3] = status->fix_type;
	out[4] = status->satellites;
	out[5] = status->key_levels;
	out[6] = status->soc_pct;
	out[7] = 0U;
	put_u16(&out[8], status->pdop_1e2);
	put_u16(&out[10], status->speed_cms);
	put_u16(&out[12], status->heading_ddeg);
	put_u16(&out[14], 0U);
	put_i32(&out[16], status->lat_e7);
	put_i32(&out[20], status->lon_e7);
	put_i32(&out[24], status->hmsl_mm);
	put_i32(&out[28], status->bat_mv);
	put_u32(&out[32], status->utc_epoch_s);
	put_u32(&out[36], status->uptime_ms);
	put_u32(&out[40], status->gnss_age_ms);
	put_u32(&out[44], status->steps);
	put_i16(&out[48], status->acc_lsb[0]);
	put_i16(&out[50], status->acc_lsb[1]);
	put_i16(&out[52], status->acc_lsb[2]);
	put_i16(&out[54], status->gyro_lsb[0]);
	put_i16(&out[56], status->gyro_lsb[1]);
	put_i16(&out[58], status->gyro_lsb[2]);
	put_i16(&out[60], status->mag_lsb[0]);
	put_i16(&out[62], status->mag_lsb[1]);
	put_i16(&out[64], status->mag_lsb[2]);
	put_u32(&out[66], status->baro_press_raw);
	put_u32(&out[70], status->baro_temp_raw);
	put_u32(&out[74], status->ring_sector_seq);
	put_u32(&out[78], status->ring_records);

	return (int)LGPS_BLE_STATUS_SIZE;
}

int lgps_ble_caps_encode(const struct lgps_ble_caps *caps, uint8_t *out, size_t out_size)
{
	if ((caps == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	if (out_size < LGPS_BLE_CAPS_SIZE) {
		return -ENOMEM;
	}

	out[0] = (uint8_t)LGPS_BLE_WIRE_VER;
	out[1] = (uint8_t)LGPS_PROTO_VERSION_MAJOR;
	out[2] = (uint8_t)LGPS_PROTO_VERSION_MINOR;
	out[3] = (uint8_t)LGPS_PROTO_VERSION_PATCH;
	out[4] = (uint8_t)LGPS_BLE_FW_VERSION_MAJOR;
	out[5] = (uint8_t)LGPS_BLE_FW_VERSION_MINOR;
	out[6] = (uint8_t)LGPS_BLE_FW_VERSION_PATCH;
	out[7] = 0U;
	put_u16(&out[8], caps->att_mtu);
	put_u16(&out[10], (uint16_t)LGPS_PROTO_MAX_COMMAND_PAYLOAD);
	put_u32(&out[12], caps->caps);
	put_u32(&out[16], caps->telemetry_mask);
	put_u16(&out[20], (uint16_t)LGPS_BLE_STATUS_SIZE);
	put_u16(&out[22], 0U);

	return (int)LGPS_BLE_CAPS_SIZE;
}

int lgps_ble_flash_info_encode(const struct lgps_ble_flash_info *info, uint8_t *out,
			       size_t out_size)
{
	if ((info == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	if (out_size < LGPS_BLE_FLASH_INFO_SIZE) {
		return -ENOMEM;
	}

	put_u32(&out[0], info->flash_bytes);
	put_u32(&out[4], info->ota_slot1_bytes);
	put_u32(&out[8], info->traj_bytes);
	put_u32(&out[12], info->sector_seq);
	put_u32(&out[16], info->records_written);
	out[20] = info->records_in_sector;
	out[21] = info->erase_count_max;
	out[22] = info->sector_count;
	out[23] = 0U;

	return (int)LGPS_BLE_FLASH_INFO_SIZE;
}

/* --- telemetry -------------------------------------------------------------- */

int lgps_ble_telem_encode(uint8_t id, uint16_t seq, const uint8_t *payload, uint16_t payload_len,
			  uint8_t *out, size_t out_size)
{
	size_t packet_len;

	if ((out == NULL) || ((payload_len != 0U) && (payload == NULL))) {
		return -EINVAL;
	}

	packet_len = (size_t)LGPS_BLE_TELEM_HEADER_SIZE + payload_len;
	if (out_size < packet_len) {
		return -ENOMEM;
	}

	out[0] = id;
	out[1] = (uint8_t)LGPS_BLE_WIRE_VER;
	put_u16(&out[2], seq);

	if (payload_len != 0U) {
		memcpy(&out[LGPS_BLE_TELEM_HEADER_SIZE], payload, payload_len);
	}

	return (int)packet_len;
}

int lgps_ble_telem_imu_encode(uint16_t seq, const struct lgps_ble_telem_imu *imu, uint8_t *out,
			      size_t out_size)
{
	uint8_t payload[LGPS_BLE_TELEM_IMU_PAYLOAD];

	if (imu == NULL) {
		return -EINVAL;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		put_i16(&payload[axis * 2U], imu->acc_lsb[axis]);
		put_i16(&payload[6U + (axis * 2U)], imu->gyro_lsb[axis]);
	}

	for (uint8_t index = 0U; index < 4U; index++) {
		put_i16(&payload[12U + (index * 2U)], imu->quat_q15[index]);
	}

	return lgps_ble_telem_encode((uint8_t)LGPS_BLE_TELEM_IMU, seq, payload, sizeof(payload), out,
				     out_size);
}

int lgps_ble_telem_pdr_encode(uint16_t seq, const struct lgps_ble_telem_pdr *pdr, uint8_t *out,
			      size_t out_size)
{
	uint8_t payload[LGPS_BLE_TELEM_PDR_PAYLOAD];

	if (pdr == NULL) {
		return -EINVAL;
	}

	put_i32(&payload[0], pdr->x_mm);
	put_i32(&payload[4], pdr->y_mm);
	put_i16(&payload[8], pdr->heading_ddeg);
	put_u16(&payload[10], pdr->steps);

	return lgps_ble_telem_encode((uint8_t)LGPS_BLE_TELEM_PDR, seq, payload, sizeof(payload), out,
				     out_size);
}
