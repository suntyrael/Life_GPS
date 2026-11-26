/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - external NOR ping-pong journal index & LGD1 archive store.
 *
 * Compatible with BOTH 32 MB (W25Q256) and 64 MB (W25Q512NW) SPI NOR Flash:
 *   - Sectors 20..21 (0x14000..0x15FFF): ping-pong journal index (4 KB each)
 *   - Offset 0x16000 .. (capacity.traj_bytes - 1): 4 KB-aligned LGD1 day packages
 * All capacity bounds come from SFDP runtime probing via lgps_ring_get_capacity().
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

#include <lifegps/archive.h>
#include <lifegps/crc.h>
#include <lifegps/flash_ring.h>

LOG_MODULE_REGISTER(lgps_archive, LOG_LEVEL_INF);

#define NOR_SECTOR_SIZE 4096U
#define JOURNAL_HDR_SIZE 20U
/* 20 + 128 * 28 = 3604 bytes, fits comfortably inside one 4096-byte NOR sector. */
#define JOURNAL_MAX_IMAGE_SIZE (JOURNAL_HDR_SIZE + (LGPS_JOURNAL_MAX_DAYS * LGPS_JOURNAL_ENTRY_SIZE))

BUILD_ASSERT(JOURNAL_MAX_IMAGE_SIZE <= NOR_SECTOR_SIZE,
	     "journal image must fit inside one 4 KB NOR sector");

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(archive_lock);

static struct lgps_journal journal_state;
static uint8_t active_sector_idx; /* 0 -> SECTOR_A (20), 1 -> SECTOR_B (21) */
static uint8_t sector_buf[JOURNAL_MAX_IMAGE_SIZE];

static void put_u16(uint8_t *out, uint16_t v)
{
	out[0] = (uint8_t)(v & 0xFFU);
	out[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void put_u32(uint8_t *out, uint32_t v)
{
	out[0] = (uint8_t)(v & 0xFFU);
	out[1] = (uint8_t)((v >> 8) & 0xFFU);
	out[2] = (uint8_t)((v >> 16) & 0xFFU);
	out[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint16_t get_u16(const uint8_t *in)
{
	return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static off_t journal_sector_offset(uint8_t idx)
{
	uint32_t sector = (idx == 0U) ? LGPS_ARCHIVE_JOURNAL_SECTOR_A
				      : LGPS_ARCHIVE_JOURNAL_SECTOR_B;

	return (off_t)sector * (off_t)NOR_SECTOR_SIZE;
}

static bool load_journal_sector(uint8_t idx, struct lgps_journal *out, uint32_t flash_limit)
{
	uint32_t magic;
	uint32_t ver;
	uint32_t next_off;
	uint16_t count;
	uint16_t stored_crc;
	uint16_t computed_crc;
	size_t img_len;

	if (flash_read(nor, journal_sector_offset(idx), sector_buf, JOURNAL_HDR_SIZE) < 0) {
		return false;
	}

	magic = get_u32(&sector_buf[0]);
	if (magic != LGPS_JOURNAL_MAGIC) {
		return false;
	}

	ver = get_u32(&sector_buf[4]);
	next_off = get_u32(&sector_buf[8]);
	count = get_u16(&sector_buf[12]);
	stored_crc = get_u16(&sector_buf[18]);

	if ((count > LGPS_JOURNAL_MAX_DAYS) || (next_off < LGPS_ARCHIVE_DATA_OFFSET) ||
	    (next_off > flash_limit)) {
		return false;
	}

	img_len = JOURNAL_HDR_SIZE + ((size_t)count * LGPS_JOURNAL_ENTRY_SIZE);
	if (count > 0U) {
		if (flash_read(nor, journal_sector_offset(idx) + JOURNAL_HDR_SIZE,
			       &sector_buf[JOURNAL_HDR_SIZE],
			       (size_t)count * LGPS_JOURNAL_ENTRY_SIZE) < 0) {
			return false;
		}
	}

	/* CRC-16 covers bytes [0..17] + entry array [20..img_len-1], skipping [18..19]. */
	sector_buf[18] = 0U;
	sector_buf[19] = 0U;
	computed_crc = lgps_crc16_ccitt_false(sector_buf, img_len);
	if (computed_crc != stored_crc) {
		return false;
	}

	lgps_journal_init(out, flash_limit);
	out->index_version = ver;
	out->next_write_offset = next_off;
	out->count = count;
	for (uint16_t i = 0U; i < count; i++) {
		lgps_journal_entry_decode(&sector_buf[JOURNAL_HDR_SIZE +
						      ((size_t)i * LGPS_JOURNAL_ENTRY_SIZE)],
					  &out->entries[i]);
	}

	return true;
}

static int persist_journal_locked(void)
{
	uint8_t target_idx = active_sector_idx ^ 1U;
	size_t img_len;
	uint16_t crc;
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	img_len = JOURNAL_HDR_SIZE + ((size_t)journal_state.count * LGPS_JOURNAL_ENTRY_SIZE);
	memset(sector_buf, 0, JOURNAL_HDR_SIZE);
	put_u32(&sector_buf[0], LGPS_JOURNAL_MAGIC);
	put_u32(&sector_buf[4], journal_state.index_version);
	put_u32(&sector_buf[8], journal_state.next_write_offset);
	put_u16(&sector_buf[12], journal_state.count);
	put_u32(&sector_buf[14], journal_state.flash_limit_bytes);

	for (uint16_t i = 0U; i < journal_state.count; i++) {
		lgps_journal_entry_encode(&journal_state.entries[i],
					  &sector_buf[JOURNAL_HDR_SIZE +
						      ((size_t)i * LGPS_JOURNAL_ENTRY_SIZE)]);
	}

	sector_buf[18] = 0U;
	sector_buf[19] = 0U;
	crc = lgps_crc16_ccitt_false(sector_buf, img_len);
	put_u16(&sector_buf[18], crc);

	ret = flash_erase(nor, journal_sector_offset(target_idx), NOR_SECTOR_SIZE);
	if (ret < 0) {
		return ret;
	}

	ret = flash_write(nor, journal_sector_offset(target_idx), sector_buf, img_len);
	if (ret == 0) {
		active_sector_idx = target_idx;
	}

	return ret;
}

int lgps_archive_init(void)
{
	struct lgps_ring_capacity cap = { 0 };
	static struct lgps_journal cand_a;
	static struct lgps_journal cand_b;
	bool valid_a = false;
	bool valid_b = false;

	lgps_ring_get_capacity(&cap);

	k_mutex_lock(&archive_lock, K_FOREVER);
	lgps_journal_init(&journal_state, cap.traj_bytes);
	active_sector_idx = 0U;

	if (device_is_ready(nor) && (cap.traj_bytes > LGPS_ARCHIVE_DATA_OFFSET)) {
		valid_a = load_journal_sector(0U, &cand_a, cap.traj_bytes);
		valid_b = load_journal_sector(1U, &cand_b, cap.traj_bytes);

		if (valid_a && (!valid_b || (cand_a.index_version >= cand_b.index_version))) {
			journal_state = cand_a;
			active_sector_idx = 0U;
		} else if (valid_b) {
			journal_state = cand_b;
			active_sector_idx = 1U;
		}

		LOG_INF("archive journal mounted: %u days, ver=%u, limit=%u B (%u MB flash)",
			journal_state.count, journal_state.index_version,
			journal_state.flash_limit_bytes,
			(unsigned int)(cap.flash_bytes / (1024U * 1024U)));
	}

	k_mutex_unlock(&archive_lock);
	return 0;
}

void lgps_archive_get_journal(struct lgps_journal *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&archive_lock, K_FOREVER);
	*out = journal_state;
	k_mutex_unlock(&archive_lock);
}

int lgps_archive_write_day(uint32_t day_id, uint32_t record_count, uint32_t traj_crc32,
			   uint32_t first_seq_id, uint16_t codec_id, const uint8_t *pkg_data,
			   size_t pkg_len)
{
	struct lgps_journal_entry entry;
	uint32_t aligned_len;
	uint32_t write_off;
	int ret;

	if ((pkg_data == NULL) || (pkg_len == 0U)) {
		return -EINVAL;
	}

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (!device_is_ready(nor)) {
		k_mutex_unlock(&archive_lock);
		return -ENODEV;
	}

	aligned_len = (uint32_t)(((pkg_len + (NOR_SECTOR_SIZE - 1U)) / NOR_SECTOR_SIZE) *
				 NOR_SECTOR_SIZE);
	write_off = journal_state.next_write_offset;
	if ((write_off < LGPS_ARCHIVE_DATA_OFFSET) ||
	    ((write_off + aligned_len) > journal_state.flash_limit_bytes)) {
		k_mutex_unlock(&archive_lock);
		return -ENOSPC;
	}

	ret = flash_erase(nor, (off_t)write_off, aligned_len);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	ret = flash_write(nor, (off_t)write_off, pkg_data, pkg_len);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	memset(&entry, 0, sizeof(entry));
	entry.day_id = day_id;
	entry.flash_offset = write_off;
	entry.package_len = (uint32_t)pkg_len;
	entry.record_count = record_count;
	entry.traj_crc32 = traj_crc32;
	entry.first_seq_id = first_seq_id;
	entry.codec_id = codec_id;

	ret = lgps_journal_add(&journal_state, &entry);
	if (ret == 0) {
		ret = persist_journal_locked();
	}

	k_mutex_unlock(&archive_lock);
	return ret;
}

int lgps_archive_read_chunk(uint32_t day_id, uint32_t offset, uint8_t *out, uint16_t max_len,
			    uint16_t *out_len, uint32_t *total_len_out)
{
	const struct lgps_journal_entry *entry;
	uint32_t remaining;
	uint16_t to_read;
	int ret;

	if ((out == NULL) || (out_len == NULL) || (max_len == 0U)) {
		return -EINVAL;
	}

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (!device_is_ready(nor)) {
		k_mutex_unlock(&archive_lock);
		return -ENODEV;
	}

	entry = lgps_journal_find(&journal_state, day_id);
	if (entry == NULL) {
		k_mutex_unlock(&archive_lock);
		return -ENOENT;
	}

	if (total_len_out != NULL) {
		*total_len_out = entry->package_len;
	}

	if (offset >= entry->package_len) {
		*out_len = 0U;
		k_mutex_unlock(&archive_lock);
		return 0;
	}

	remaining = entry->package_len - offset;
	to_read = (remaining < (uint32_t)max_len) ? (uint16_t)remaining : max_len;
	ret = flash_read(nor, (off_t)(entry->flash_offset + offset), out, to_read);
	if (ret == 0) {
		*out_len = to_read;
	}

	k_mutex_unlock(&archive_lock);
	return ret;
}

int lgps_archive_erase_day(uint32_t day_id)
{
	const struct lgps_journal_entry *found;
	uint32_t flash_off = 0U;
	uint32_t aligned_len = 0U;
	int ret;

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (!device_is_ready(nor)) {
		k_mutex_unlock(&archive_lock);
		return -ENODEV;
	}

	found = lgps_journal_find(&journal_state, day_id);
	if (found == NULL) {
		k_mutex_unlock(&archive_lock);
		return -ENOENT;
	}

	flash_off = found->flash_offset;
	aligned_len = (uint32_t)(((found->package_len + (NOR_SECTOR_SIZE - 1U)) / NOR_SECTOR_SIZE) *
				 NOR_SECTOR_SIZE);

	ret = lgps_journal_remove(&journal_state, day_id);
	if (ret == 0) {
		(void)flash_erase(nor, (off_t)flash_off, aligned_len);
		ret = persist_journal_locked();
	}

	k_mutex_unlock(&archive_lock);
	return ret;
}

int lgps_archive_factory_reset(void)
{
	struct lgps_ring_capacity cap = { 0 };
	int ret = 0;

	lgps_ring_get_capacity(&cap);

	k_mutex_lock(&archive_lock, K_FOREVER);
	lgps_journal_init(&journal_state, cap.traj_bytes);
	active_sector_idx = 0U;
	if (device_is_ready(nor)) {
		int r0 = flash_erase(nor, journal_sector_offset(0U), NOR_SECTOR_SIZE);
		int r1 = flash_erase(nor, journal_sector_offset(1U), NOR_SECTOR_SIZE);

		ret = (r0 < 0) ? r0 : r1;
	}
	k_mutex_unlock(&archive_lock);
	return ret;
}

uint16_t lgps_archive_used_permille(void)
{
	uint64_t used;
	uint64_t total;
	uint16_t permille = 0U;

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (journal_state.flash_limit_bytes > LGPS_ARCHIVE_DATA_OFFSET) {
		total = (uint64_t)(journal_state.flash_limit_bytes - LGPS_ARCHIVE_DATA_OFFSET);
		used = (journal_state.next_write_offset > LGPS_ARCHIVE_DATA_OFFSET)
			       ? (uint64_t)(journal_state.next_write_offset -
					    LGPS_ARCHIVE_DATA_OFFSET)
			       : 0U;
		permille = (uint16_t)((used * 1000U) / total);
		if (permille > 1000U) {
			permille = 1000U;
		}
	}
	k_mutex_unlock(&archive_lock);
	return permille;
}
