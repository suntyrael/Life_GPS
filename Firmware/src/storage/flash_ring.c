/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <stddef.h>
#include <string.h>

#include <lifegps/flash_ring.h>
#include <lifegps/sector_format.h>
#include <lifegps/traj.h>

LOG_MODULE_REGISTER(lgps_ring, LOG_LEVEL_INF);

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(ring_lock);

static struct lgps_ring_stats stats;
static uint8_t active_sector;
static bool ring_ready;
static struct lgps_ring_capacity capacity;

static off_t sector_offset(uint8_t sector)
{
	return (off_t)sector * (off_t)LGPS_RING_SECTOR_SIZE;
}

/*
 * All header encoding/decoding lives in lifegps/sector_format.h so it can be unit
 * tested without a flash driver; this module only moves those bytes to and from
 * the device.
 */
static int header_write(uint8_t sector, const struct lgps_sector_header *header)
{
	int ret = flash_write(nor, sector_offset(sector), header, sizeof(*header));

	if (ret < 0) {
		LOG_ERR("sector %u header write failed (%d)", sector, ret);
	}

	return ret;
}

static int header_read(uint8_t sector, struct lgps_sector_header *header)
{
	int ret = flash_read(nor, sector_offset(sector), header, sizeof(*header));

	if (ret < 0) {
		LOG_ERR("sector %u header read failed (%d)", sector, ret);
		return ret;
	}

	return 0;
}

static void start_sector(uint8_t sector, uint32_t sector_seq, uint32_t erase_count)
{
	struct lgps_sector_header header;

	lgps_sector_header_init(&header, sector_seq, erase_count);
	(void)header_write(sector, &header);

	active_sector = sector;
	stats.records_in_sector = 0U;
	stats.sector_seq = sector_seq;
	if (erase_count > stats.erase_count_max) {
		stats.erase_count_max = (uint8_t)MIN(erase_count, 255U);
	}
}

#define RING_SCAN_CHUNK_BYTES 256U
#define RING_SCAN_RECORDS (RING_SCAN_CHUNK_BYTES / LGPS_RING_RECORD_SIZE)

/*
 * Recover the write cursor of the sector an earlier boot left open.
 *
 * records_in_sector lived only in RAM, so after a reset it was 0 while the sector
 * already held records: the next append rewrote slot 0 of an already programmed
 * area. NOR can only clear bits, so the old record was corrupted silently and no
 * CRC ever noticed. The cursor is therefore rebuilt from the media: records are
 * written strictly in order from slot 0, so the first fully erased slot (0xFF...)
 * is where the next record belongs. A serialised record always carries its CRC-8
 * byte and can therefore never be all-0xFF.
 *
 * Cost: one 256-byte static buffer (8 records per flash_read) and 16 reads of
 * 256 B at mount time. Nothing here touches the stack.
 */
static int count_used_records(uint8_t sector, uint16_t *used)
{
	static uint8_t chunk[RING_SCAN_CHUNK_BYTES];
	uint16_t total = 0U;

	for (uint16_t base = 0U; base < LGPS_RING_RECORDS_PER_SECTOR;
	     base = (uint16_t)(base + RING_SCAN_RECORDS)) {
		uint16_t slots = (uint16_t)MIN((uint32_t)RING_SCAN_RECORDS,
					       (uint32_t)LGPS_RING_RECORDS_PER_SECTOR - base);
		off_t offset = sector_offset(sector) + (off_t)LGPS_RING_HEADER_SIZE +
			       (off_t)((uint32_t)base * LGPS_RING_RECORD_SIZE);
		size_t len_bytes = (size_t)slots * (size_t)LGPS_RING_RECORD_SIZE;
		uint16_t in_chunk;
		int ret = flash_read(nor, offset, chunk, len_bytes);

		if (ret < 0) {
			LOG_ERR("sector %u record scan at 0x%08x failed (%d)", sector,
				(unsigned int)offset, ret);
			return ret;
		}

		/* Same helper the unit test covers: count programmed slots until the
		 * first erased one.
		 */
		in_chunk = lgps_sector_used_records(chunk, len_bytes);
		total = (uint16_t)(total + in_chunk);

		if (in_chunk < slots) {
			/* The write cursor lives inside this chunk. */
			break;
		}
	}

	*used = total;

	return 0;
}

static int sector_rotate(void)
{
	struct lgps_sector_header header;
	uint8_t next = (uint8_t)((active_sector + 1U) % LGPS_RING_SECTOR_COUNT);
	int ret;

	/* 1. Seal the sector that just filled up. The header CRC does not cover
	 * commit_flags, so no CRC byte has to go back from 0 to 1 here.
	 */
	ret = header_read(active_sector, &header);
	if (ret < 0) {
		return ret;
	}

	/* commit_flags is outside the CRC, so sealing only clears bits: the CRC bytes
	 * read back from the header stay exactly as they were and have to go back from
	 * 0 to 1 for nothing.
	 */
	lgps_sector_header_seal(&header);

	ret = header_write(active_sector, &header);
	if (ret < 0) {
		return ret;
	}

	/* 2. Remember the wear counter before the erase destroys it. */
	uint8_t previous_sector = active_sector;
	uint32_t erase_count = 0U;

	ret = header_read(next, &header);
	if ((ret == 0) && (header.magic == LGPS_RING_HEADER_MAGIC)) {
		erase_count = header.erase_count;
	}

	/* 3. Erase the next sector of the ring (wear levelling by rotation). */
	ret = flash_erase(nor, sector_offset(next), LGPS_RING_SECTOR_SIZE);
	if (ret < 0) {
		LOG_ERR("erase of sector %u failed (%d)", next, ret);
		return ret;
	}

	/* 4. Publish a fresh header with an increasing sequence number. */
	start_sector(next, stats.sector_seq + 1U, erase_count + 1U);

	LOG_INF("ring rotated: sector %u -> %u (seq %u, erase_count %u)", previous_sector, next,
		stats.sector_seq, stats.erase_count_max);

	return 0;
}

int lgps_ring_init(void)
{
	struct lgps_sector_header header;
	bool found = false;
	uint8_t best_sector = 0U;
	uint32_t best_seq = 0U;
	uint32_t best_erase = 0U;
	int ret;

	if (!device_is_ready(nor)) {
		LOG_ERR("external NOR (SPIM00) not ready");
		return -ENODEV;
	}

	/* Density first: 32 MB and 64 MB parts must both work. */
	struct flash_pages_info first_page;
	size_t page_count = flash_get_page_count(nor);

	if (flash_get_page_info_by_idx(nor, 0U, &first_page) < 0) {
		LOG_ERR("external NOR: page info unavailable");
		return -EIO;
	}

	capacity.flash_bytes = (uint32_t)(page_count * first_page.size);
	capacity.ota_slot1_bytes = LGPS_EXT_FLASH_OTA_SLOT1_BYTES;
	capacity.traj_bytes = (capacity.flash_bytes > capacity.ota_slot1_bytes)
				      ? (capacity.flash_bytes - capacity.ota_slot1_bytes)
				      : 0U;

	if (capacity.flash_bytes < (LGPS_RING_SECTOR_COUNT * LGPS_RING_SECTOR_SIZE)) {
		LOG_ERR("external NOR too small: %u bytes", capacity.flash_bytes);
		return -ENOSPC;
	}

	LOG_INF("external NOR: %u MiB detected via SFDP (%u pages x %u bytes), "
		"trajectory area %u MiB, OTA slot1 %u MiB at 0x%08x",
		capacity.flash_bytes / (1024U * 1024U), (unsigned int)page_count,
		(unsigned int)first_page.size, capacity.traj_bytes / (1024U * 1024U),
		capacity.ota_slot1_bytes / (1024U * 1024U),
		capacity.flash_bytes - capacity.ota_slot1_bytes);

	if (capacity.flash_bytes != 64U * 1024U * 1024U) {
		LOG_WRN("external NOR is not the 64 MB W25Q512NW the archive budget assumes; "
			"the trajectory area scales with the real density");
	}

	k_mutex_lock(&ring_lock, K_FOREVER);

	stats.records_written = 0U;
	stats.erase_count_max = 0U;
	stats.records_in_sector = 0U;

	for (uint8_t sector = 0U; sector < LGPS_RING_SECTOR_COUNT; sector++) {
		ret = header_read(sector, &header);
		if (ret < 0) {
			k_mutex_unlock(&ring_lock);
			return ret;
		}

		if (!lgps_sector_header_valid(&header)) {
			continue;
		}

		if (header.erase_count > best_erase) {
			best_erase = header.erase_count;
		}

		if (header.commit_flags == LGPS_RING_FLAGS_WRITING && header.sector_seq >= best_seq) {
			best_seq = header.sector_seq;
			best_sector = sector;
			found = true;
		}
	}

	if (found) {
		uint16_t used_records = 0U;

		ret = count_used_records(best_sector, &used_records);
		if (ret < 0) {
			k_mutex_unlock(&ring_lock);
			return ret;
		}

		active_sector = best_sector;
		stats.sector_seq = best_seq;
		stats.records_in_sector = (uint8_t)used_records;
		stats.erase_count_max = (uint8_t)MIN(best_erase, 255U);
		LOG_INF("ring mounted on sector %u (seq %u, %u/%u records used, max erase count %u)",
			best_sector, best_seq, used_records, LGPS_RING_RECORDS_PER_SECTOR,
			stats.erase_count_max);
	} else {
		/* Fresh flash: erase and publish sector 0. */
		ret = flash_erase(nor, sector_offset(0U), LGPS_RING_SECTOR_SIZE);
		if (ret < 0) {
			LOG_ERR("initial erase failed (%d)", ret);
			k_mutex_unlock(&ring_lock);
			return ret;
		}

		start_sector(0U, 1U, 1U);
		LOG_INF("ring formatted: 16 sectors x %u bytes at external flash offset 0",
			LGPS_RING_SECTOR_SIZE);
	}

	ring_ready = true;
	k_mutex_unlock(&ring_lock);

	LOG_INF("staging ring: %u sectors, %u records/sector, record %u bytes",
		LGPS_RING_SECTOR_COUNT, LGPS_RING_RECORDS_PER_SECTOR, LGPS_RING_RECORD_SIZE);

	return 0;
}

static uint32_t last_seq_id;

static uint32_t read_record_seq_id(const uint8_t rec[LGPS_RING_RECORD_SIZE])
{
	return (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |
	       ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
}

static void recover_last_seq_locked(uint8_t sector, uint16_t used_records)
{
	uint8_t rec[LGPS_RING_RECORD_SIZE];

	if (used_records == 0U) {
		return;
	}

	off_t offset = sector_offset(sector) + (off_t)LGPS_RING_HEADER_SIZE +
		       (off_t)((uint32_t)(used_records - 1U) * LGPS_RING_RECORD_SIZE);

	if (flash_read(nor, offset, rec, sizeof(rec)) == 0 && lgps_traj_record_valid(rec)) {
		uint32_t seq = read_record_seq_id(rec);

		if (seq > last_seq_id) {
			last_seq_id = seq;
		}
	}
}

int lgps_ring_append(const uint8_t *record)
{
	off_t offset;
	int ret;

	if (record == NULL) {
		return -EINVAL;
	}

	if (!ring_ready) {
		return -EACCES;
	}

	k_mutex_lock(&ring_lock, K_FOREVER);

	if (stats.records_in_sector >= LGPS_RING_RECORDS_PER_SECTOR) {
		ret = sector_rotate();
		if (ret < 0) {
			k_mutex_unlock(&ring_lock);
			return ret;
		}
	}

	offset = sector_offset(active_sector) + LGPS_RING_HEADER_SIZE +
		 (off_t)stats.records_in_sector * (off_t)LGPS_RING_RECORD_SIZE;

	ret = flash_write(nor, offset, record, LGPS_RING_RECORD_SIZE);
	if (ret < 0) {
		LOG_ERR("record write at 0x%08x failed (%d)", (unsigned int)offset, ret);
		k_mutex_unlock(&ring_lock);
		return ret;
	}

	stats.records_in_sector++;
	stats.records_written++;

	{
		uint32_t seq = read_record_seq_id(record);

		if (seq > last_seq_id) {
			last_seq_id = seq;
		}
	}

	k_mutex_unlock(&ring_lock);

	return 0;
}

uint32_t lgps_ring_last_seq_id(void)
{
	uint32_t seq;

	k_mutex_lock(&ring_lock, K_FOREVER);
	if (ring_ready && (last_seq_id == 0U) && (stats.records_in_sector > 0U)) {
		recover_last_seq_locked(active_sector, stats.records_in_sector);
	}
	seq = last_seq_id;
	k_mutex_unlock(&ring_lock);

	return seq;
}

int lgps_ring_read_from_seq(uint32_t start_seq, uint8_t *out_buf, uint16_t max_records,
			    uint16_t *out_count)
{
	struct lgps_sector_header headers[LGPS_RING_SECTOR_COUNT];
	bool valid[LGPS_RING_SECTOR_COUNT];
	uint8_t order[LGPS_RING_SECTOR_COUNT];
	uint8_t valid_count = 0U;
	uint16_t copied = 0U;
	static uint8_t scan_chunk[RING_SCAN_CHUNK_BYTES];

	if ((out_buf == NULL) || (out_count == NULL)) {
		return -EINVAL;
	}
	*out_count = 0U;

	if (!ring_ready) {
		return -EACCES;
	}
	if (max_records == 0U) {
		return 0;
	}

	k_mutex_lock(&ring_lock, K_FOREVER);

	for (uint8_t s = 0U; s < LGPS_RING_SECTOR_COUNT; s++) {
		valid[s] = (header_read(s, &headers[s]) == 0) &&
			   lgps_sector_header_valid(&headers[s]);
		if (valid[s]) {
			order[valid_count++] = s;
		}
	}

	/* Simple insertion sort of <=16 sectors by ascending sector_seq. */
	for (uint8_t i = 1U; i < valid_count; i++) {
		uint8_t key_s = order[i];
		uint32_t key_seq = headers[key_s].sector_seq;
		int j = (int)i - 1;

		while ((j >= 0) && (headers[order[j]].sector_seq > key_seq)) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = key_s;
	}

	for (uint8_t oi = 0U; (oi < valid_count) && (copied < max_records); oi++) {
		uint8_t s = order[oi];

		for (uint16_t base = 0U;
		     (base < LGPS_RING_RECORDS_PER_SECTOR) && (copied < max_records);
		     base = (uint16_t)(base + RING_SCAN_RECORDS)) {
			uint16_t slots = (uint16_t)MIN((uint32_t)RING_SCAN_RECORDS,
						       (uint32_t)LGPS_RING_RECORDS_PER_SECTOR -
							       base);
			off_t offset = sector_offset(s) + (off_t)LGPS_RING_HEADER_SIZE +
				       (off_t)((uint32_t)base * LGPS_RING_RECORD_SIZE);
			size_t len_bytes = (size_t)slots * (size_t)LGPS_RING_RECORD_SIZE;
			int ret = flash_read(nor, offset, scan_chunk, len_bytes);

			if (ret < 0) {
				k_mutex_unlock(&ring_lock);
				return ret;
			}

			for (uint16_t k = 0U; (k < slots) && (copied < max_records); k++) {
				const uint8_t *rec = &scan_chunk[(size_t)k * LGPS_RING_RECORD_SIZE];

				if (!lgps_traj_record_valid(rec)) {
					continue;
				}
				if (read_record_seq_id(rec) < start_seq) {
					continue;
				}
				memcpy(&out_buf[(size_t)copied * LGPS_RING_RECORD_SIZE], rec,
				       LGPS_RING_RECORD_SIZE);
				copied++;
			}
		}
	}

	k_mutex_unlock(&ring_lock);
	*out_count = copied;
	return 0;
}

void lgps_ring_get_capacity(struct lgps_ring_capacity *out)
{
	if (out == NULL) {
		return;
	}

	*out = capacity;
}

void lgps_ring_get_stats(struct lgps_ring_stats *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&ring_lock, K_FOREVER);
	*out = stats;
	k_mutex_unlock(&ring_lock);
}
