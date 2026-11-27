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
 *   uint16 header_crc16 CRC-16/CCITT-FALSE over bytes 0..11 (magic, sector_seq,
 *                      erase_count). commit_flags is outside the CRC on purpose:
 *                      sealing a sector must be a pure 1->0 bit update, because
 *                      NOR programming can only clear bits.
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

/* Since M5, MCUboot slot1_partition (712 KB) lives in internal nRF54L15 RRAM
 * (Firmware/docs/PARTITIONS.md, Firmware/boards/lifegps/lifegps/lifegps_nrf54l15_cpuapp.dts),
 * so external SPI NOR reserves 0 bytes for OTA and the full SFDP density is
 * available to the trajectory/archive area.
 */
#define LGPS_EXT_FLASH_OTA_SLOT1_BYTES 0U

struct lgps_ring_capacity {
	uint32_t flash_bytes;     /* density reported by SFDP */
	uint32_t ota_slot1_bytes; /* reserved on external NOR for OTA slot1 (0 since M5) */
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
 * The number of records already present in that sector is rebuilt from the media
 * (first fully erased slot), so a reset cannot make the write cursor point at an
 * already programmed record.
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

/* Highest trajectory seq_id recovered on mount or appended since boot (0 when empty). */
uint32_t lgps_ring_last_seq_id(void);

/* Thread-safe monotonic seq_id allocator shared by recorder and BLE POI_MARK. */
uint32_t lgps_ring_alloc_seq_id(void);

/*
 * Read up to @p max_records valid 32-byte trajectory records whose seq_id >= @p start_seq,
 * ordered by sector_seq then slot index. Used by BLE SYNC_READ and nightly archiver.
 */
int lgps_ring_read_from_seq(uint32_t start_seq, uint8_t *out_buf, uint16_t max_records,
			    uint16_t *out_count);

/*
 * Count valid trajectory records across all non-erasing staging ring sectors
 * whose seq_id > @p after_seq_id.
 */
uint16_t lgps_ring_count_after_seq(uint32_t after_seq_id);

/*
 * Mark sealed (non-active) staging ring sectors as LGPS_RING_FLAGS_ERASING
 * (0x0000, pure 1->0 NOR update preserving erase_count and header_crc16) once
 * every valid record in the sector has seq_id <= @p max_archived_seq.
 */
int lgps_ring_release_sectors_up_to(uint32_t max_archived_seq);

/* Erase all 16 staging ring sectors (0..15) and reset cursors (FACTORY_RESET 0x51). */
int lgps_ring_erase_all(void);

#endif /* LIFEGPS_FLASH_RING_H_ */
