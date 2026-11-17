/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stddef.h>

#include <lifegps/crc.h>
#include <lifegps/flash_ring.h>

LOG_MODULE_REGISTER(lgps_ring, LOG_LEVEL_INF);

struct sector_header {
	uint32_t magic;
	uint32_t sector_seq;
	uint32_t erase_count;
	uint16_t commit_flags;
	uint16_t header_crc16;
} __packed;

BUILD_ASSERT(sizeof(struct sector_header) == LGPS_RING_HEADER_SIZE,
	     "sector header must be exactly 16 bytes");

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(ring_lock);

static struct lgps_ring_stats stats;
static uint8_t active_sector;
static bool ring_ready;

static off_t sector_offset(uint8_t sector)
{
	return (off_t)sector * (off_t)LGPS_RING_SECTOR_SIZE;
}

static uint16_t header_crc(const struct sector_header *header)
{
	return lgps_crc16_ccitt_false((const uint8_t *)header, offsetof(struct sector_header,
								       header_crc16));
}

static int header_write(uint8_t sector, const struct sector_header *header)
{
	int ret = flash_write(nor, sector_offset(sector), header, sizeof(*header));

	if (ret < 0) {
		LOG_ERR("sector %u header write failed (%d)", sector, ret);
	}

	return ret;
}

static int header_read(uint8_t sector, struct sector_header *header)
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
	struct sector_header header = {
		.magic = LGPS_RING_HEADER_MAGIC,
		.sector_seq = sector_seq,
		.erase_count = erase_count,
		.commit_flags = LGPS_RING_FLAGS_WRITING,
		.header_crc16 = 0U,
	};

	header.header_crc16 = header_crc(&header);
	(void)header_write(sector, &header);

	active_sector = sector;
	stats.records_in_sector = 0U;
	stats.sector_seq = sector_seq;
	if (erase_count > stats.erase_count_max) {
		stats.erase_count_max = (uint8_t)MIN(erase_count, 255U);
	}
}

static int sector_rotate(void)
{
	struct sector_header header;
	uint8_t next = (uint8_t)((active_sector + 1U) % LGPS_RING_SECTOR_COUNT);
	int ret;

	/* 1. Seal the sector that just filled up. */
	ret = header_read(active_sector, &header);
	if (ret < 0) {
		return ret;
	}

	header.commit_flags = LGPS_RING_FLAGS_ARCHIVED;
	header.header_crc16 = 0U;
	header.header_crc16 = header_crc(&header);

	ret = header_write(active_sector, &header);
	if (ret < 0) {
		return ret;
	}

	/* 2. Remember the wear counter before the erase destroys it. */
	uint8_t previous_sector = active_sector;
	uint32_t erase_count = 0U;

	ret = header_read(next, &header);
	if (ret == 0 && header.magic == LGPS_RING_HEADER_MAGIC) {
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
	struct sector_header header;
	bool found = false;
	uint8_t best_sector = 0U;
	uint32_t best_seq = 0U;
	uint32_t best_erase = 0U;
	int ret;

	if (!device_is_ready(nor)) {
		LOG_ERR("W25Q512NW (SPIM00) not ready");
		return -ENODEV;
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

		if (header.magic != LGPS_RING_HEADER_MAGIC ||
		    header.header_crc16 != header_crc(&header)) {
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
		active_sector = best_sector;
		stats.sector_seq = best_seq;
		stats.erase_count_max = (uint8_t)MIN(best_erase, 255U);
		LOG_INF("ring mounted on sector %u (seq %u, max erase count %u)", best_sector,
			best_seq, stats.erase_count_max);
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

	k_mutex_unlock(&ring_lock);

	return 0;
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
