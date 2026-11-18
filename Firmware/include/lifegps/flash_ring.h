/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - W25Q512NW staging ring (external SPI NOR).
 *
 * Layout matches Firmware/docs/PARTITIONS.md: the trajectory area starts at
 * offset 0 of the external flash and the ring occupies the first 64 KB
 * (16 sectors x 4 KB). Each sector starts with a 16-byte header, so a sector
 * holds 127 records of 32 bytes.
 *
 * Sector header (16 bytes, little endian):
 *   uint32 magic       0x54524143 "TRAC"
 *   uint32 sector_seq  global rotation counter
 *   uint32 erase_count physical erase counter for wear monitoring
 *   uint16 commit_flags 0xFFFF writing / 0x55AA archived / 0x0000 erasing
 *   uint16 header_crc16 CRC-16/CCITT-FALSE over the first 14 bytes
 *
 * Records are passed in already serialised by lgps_traj_encode(); this module
 * never interprets them.
 */

#ifndef LIFEGPS_FLASH_RING_H_
#define LIFEGPS_FLASH_RING_H_

#include <stdbool.h>
#include <stdint.h>

#define LGPS_RING_SECTOR_COUNT 16U
#define LGPS_RING_SECTOR_SIZE 4096U
#define LGPS_RING_HEADER_SIZE 16U
#define LGPS_RING_RECORD_SIZE 32U
#define LGPS_RING_RECORDS_PER_SECTOR \
	((LGPS_RING_SECTOR_SIZE - LGPS_RING_HEADER_SIZE) / LGPS_RING_RECORD_SIZE)

#define LGPS_RING_HEADER_MAGIC 0x54524143U
#define LGPS_RING_FLAGS_WRITING 0xFFFFU
#define LGPS_RING_FLAGS_ARCHIVED 0x55AAU
#define LGPS_RING_FLAGS_ERASING 0x0000U

/* OTA slot1 lives at the top of the external flash, so the trajectory area is
 * "whatever the part really is" minus this reservation (Firmware/docs/PARTITIONS.md).
 */
#define LGPS_EXT_FLASH_OTA_SLOT1_BYTES (1024U * 1024U)

struct lgps_ring_capacity {
	uint32_t flash_bytes;     /* density reported by SFDP */
	uint32_t ota_slot1_bytes; /* reserved at the top for MCUboot slot1 */
	uint32_t traj_bytes;      /* flash_bytes - ota_slot1_bytes */
};

struct lgps_ring_stats {
	uint32_t sector_seq;
	uint8_t erase_count_max;
	uint8_t records_in_sector;
	uint32_t records_written;
};

/*
 * Probe the NOR, read the 16 sector headers and pick the active sector.
 * Returns 0 on success, -ENODEV when the flash is missing, negative errno when
 * a sector cannot be read.
 */
int lgps_ring_init(void);

/* Append one 32-byte record, rotating/erasing a new sector when the ring is full. */
int lgps_ring_append(const uint8_t *record);

/* Copy the current statistics (never NULL). */
void lgps_ring_get_stats(struct lgps_ring_stats *out);

/*
 * Detected capacities. The W25Q512NW is 64 MB and the W25Q256 is 32 MB; both are
 * handled because the size comes from SFDP at runtime and nothing assumes 64 MB.
 */
void lgps_ring_get_capacity(struct lgps_ring_capacity *out);

#endif /* LIFEGPS_FLASH_RING_H_ */
