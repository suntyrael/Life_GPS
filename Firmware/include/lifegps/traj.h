/*
 * Life_GPS firmware - on-flash trajectory record codec.
 *
 * The record layout is owned by protocol/lifegps_protocol.yaml and reaches this
 * header through the generated include (tools/protocol-gen/gen_c.py). Nothing in
 * Firmware/src may hard-code offsets: always go through these helpers so a
 * protocol change regenerates the layout and the static assertions fire.
 *
 * Wire/flash byte order is little-endian; the codec serialises field by field so
 * it stays correct on a big-endian host as well.
 */

#ifndef LIFEGPS_TRAJ_H_
#define LIFEGPS_TRAJ_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <generated/lifegps_protocol.h>

/* status_flags bit map, README section 7.6. */
#define LGPS_FLAG_FIX_VALID 0x01U
#define LGPS_FLAG_NAV_SRC 0x02U
#define LGPS_FLAG_POI_EVENT 0x04U
#define LGPS_FLAG_GNSS_TYPE 0x08U
#define LGPS_FLAG_VEHICLE_MODE 0x10U
#define LGPS_FLAG_STN_LOCK 0x20U
#define LGPS_FLAG_MAG_ANOMALY 0x40U
#define LGPS_FLAG_ALT_SRC 0x80U

/* Neutral, driver-facing view of one trajectory point. */
struct lgps_traj_sample {
	uint32_t seq_id;
	uint32_t timestamp_s;   /* Life_GPS epoch */
	uint16_t subsecond_ms;  /* 0..999 */
	int32_t lat_e7;         /* 1e-7 deg */
	int32_t lon_e7;         /* 1e-7 deg */
	int16_t alt_m;
	uint16_t speed_cms;
	uint16_t heading_ddeg;  /* 0..3599 */
	uint16_t step_count;
	uint8_t hdop_d1;        /* 0.1 resolution */
	uint8_t satellites;
	uint8_t status_flags;
	uint8_t battery_pct;
	int8_t floor_level;
};

/*
 * Serialise @p sample into @p raw (LGPS_RECORD_TRAJECTORY_SIZE bytes) and append
 * the CRC-8 of the first 31 bytes.
 * Returns 0 on success, -EINVAL on NULL pointers or out-of-range fields
 * (subsecond_ms > 999, heading_ddeg > 3599, battery_pct > 100).
 */
int lgps_traj_encode(const struct lgps_traj_sample *sample, uint8_t *raw);

/*
 * Parse @p raw into @p sample after validating the CRC-8.
 * Returns 0 on success, -EINVAL on NULL pointers, -EBADMSG on CRC mismatch.
 */
int lgps_traj_decode(const uint8_t *raw, struct lgps_traj_sample *sample);

/* True when the trailing CRC-8 of the record matches its first 31 bytes. */
bool lgps_traj_record_valid(const uint8_t *raw);

#endif /* LIFEGPS_TRAJ_H_ */
