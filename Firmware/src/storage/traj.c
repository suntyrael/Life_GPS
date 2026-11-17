/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - on-flash trajectory record codec.
 *
 * The 32-byte layout is owned by protocol/lifegps_protocol.yaml and reaches this
 * file through <generated/lifegps_protocol.h>: every byte offset below comes from
 * an LGPS_RECORD_TRAJECTORY_OFF_* macro, never from a literal.
 *
 * Serialisation is field by field in little-endian order, so the codec is also
 * correct on a big-endian host and never depends on struct padding. Signed
 * fields are converted through their unsigned bit pattern, which relies on the
 * two's complement representation that Zephyr already requires.
 *
 * The checksum is the CRC-8 (poly 0x07, init 0x00) of the first 31 bytes:
 * i.e. everything except the trailing checksum field.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lifegps/crc.h>
#include <lifegps/traj.h>

/* Bytes covered by the record CRC-8: all fields before checksum. */
#define LGPS_TRAJ_CRC_COVERED_BYTES LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM

/* Ranges frozen by the protocol YAML (README section 7.6). */
#define LGPS_TRAJ_SUBSECOND_MS_MAX 999U
#define LGPS_TRAJ_HEADING_DDEG_MAX 3599U
#define LGPS_TRAJ_BATTERY_PCT_MAX 100U

_Static_assert(LGPS_RECORD_TRAJECTORY_SIZE == 32, "trajectory record must stay 32 bytes");
_Static_assert(LGPS_TRAJ_CRC_COVERED_BYTES == 31,
	       "trajectory record CRC must cover the first 31 bytes");

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

static uint16_t get_le16(const uint8_t *src)
{
	return (uint16_t)((uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8));
}

static uint32_t get_le32(const uint8_t *src)
{
	return (uint32_t)src[0] |
	       ((uint32_t)src[1] << 8) |
	       ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

int lgps_traj_encode(const struct lgps_traj_sample *sample, uint8_t *raw)
{
	if ((sample == NULL) || (raw == NULL)) {
		return -EINVAL;
	}

	if (sample->subsecond_ms > LGPS_TRAJ_SUBSECOND_MS_MAX) {
		return -EINVAL;
	}

	if (sample->heading_ddeg > LGPS_TRAJ_HEADING_DDEG_MAX) {
		return -EINVAL;
	}

	if (sample->battery_pct > LGPS_TRAJ_BATTERY_PCT_MAX) {
		return -EINVAL;
	}

	put_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID], sample->seq_id);
	put_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP], sample->timestamp_s);
	put_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_SUBSECOND_MS], sample->subsecond_ms);
	put_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_LATITUDE], (uint32_t)sample->lat_e7);
	put_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_LONGITUDE], (uint32_t)sample->lon_e7);
	put_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_ALTITUDE], (uint16_t)sample->alt_m);
	put_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_SPEED_CMS], sample->speed_cms);
	put_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_HEADING], sample->heading_ddeg);
	put_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_STEP_COUNT], sample->step_count);
	raw[LGPS_RECORD_TRAJECTORY_OFF_HDOP] = sample->hdop_d1;
	raw[LGPS_RECORD_TRAJECTORY_OFF_SATELLITES] = sample->satellites;
	raw[LGPS_RECORD_TRAJECTORY_OFF_STATUS_FLAGS] = sample->status_flags;
	raw[LGPS_RECORD_TRAJECTORY_OFF_BATTERY_LEVEL] = sample->battery_pct;
	raw[LGPS_RECORD_TRAJECTORY_OFF_FLOOR_LEVEL] = (uint8_t)sample->floor_level;

	raw[LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM] =
		lgps_crc8(raw, (size_t)LGPS_TRAJ_CRC_COVERED_BYTES);

	return 0;
}

int lgps_traj_decode(const uint8_t *raw, struct lgps_traj_sample *sample)
{
	uint8_t checksum;

	if ((raw == NULL) || (sample == NULL)) {
		return -EINVAL;
	}

	checksum = lgps_crc8(raw, (size_t)LGPS_TRAJ_CRC_COVERED_BYTES);
	if (checksum != raw[LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM]) {
		return -EBADMSG;
	}

	sample->seq_id = get_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
	sample->timestamp_s = get_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP]);
	sample->subsecond_ms = get_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_SUBSECOND_MS]);
	sample->lat_e7 = (int32_t)get_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_LATITUDE]);
	sample->lon_e7 = (int32_t)get_le32(&raw[LGPS_RECORD_TRAJECTORY_OFF_LONGITUDE]);
	sample->alt_m = (int16_t)get_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_ALTITUDE]);
	sample->speed_cms = get_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_SPEED_CMS]);
	sample->heading_ddeg = get_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_HEADING]);
	sample->step_count = get_le16(&raw[LGPS_RECORD_TRAJECTORY_OFF_STEP_COUNT]);
	sample->hdop_d1 = raw[LGPS_RECORD_TRAJECTORY_OFF_HDOP];
	sample->satellites = raw[LGPS_RECORD_TRAJECTORY_OFF_SATELLITES];
	sample->status_flags = raw[LGPS_RECORD_TRAJECTORY_OFF_STATUS_FLAGS];
	sample->battery_pct = raw[LGPS_RECORD_TRAJECTORY_OFF_BATTERY_LEVEL];
	sample->floor_level = (int8_t)raw[LGPS_RECORD_TRAJECTORY_OFF_FLOOR_LEVEL];

	return 0;
}

bool lgps_traj_record_valid(const uint8_t *raw)
{
	uint8_t checksum;

	if (raw == NULL) {
		return false;
	}

	checksum = lgps_crc8(raw, (size_t)LGPS_TRAJ_CRC_COVERED_BYTES);
	if (checksum != raw[LGPS_RECORD_TRAJECTORY_OFF_CHECKSUM]) {
		return false;
	}

	return true;
}
