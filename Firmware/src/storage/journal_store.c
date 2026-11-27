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
		if (journal_state.count > 0U) {
			uint32_t oldest_day = journal_state.entries[0].day_id;

			(void)lgps_journal_remove(&journal_state, oldest_day);
			LOG_WRN("archive full, evicted oldest day %u and wrapped", oldest_day);
		}
		write_off = LGPS_ARCHIVE_DATA_OFFSET;
		journal_state.next_write_offset = write_off;
		if ((write_off + aligned_len) > journal_state.flash_limit_bytes) {
			k_mutex_unlock(&archive_lock);
			return -ENOSPC;
		}
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

/* --- Background ring-to-LGD1 daily compactor (dedicated storage_workq) ---- */

#define COMPACT_STACK_SIZE 3072
#define COMPACT_THREAD_PRIO K_PRIO_PREEMPT(12)
#define COMPACT_MAX_RECS 512U
#define COMPACT_RAW_CAP (COMPACT_MAX_RECS * LGPS_RECORD_TRAJECTORY_SIZE)
#define COMPACT_PKG_CAP                                                                            \
	(LGPS_PACKAGE_DAY_HEADER_SIZE + COMPACT_RAW_CAP + 512U + LGPS_PACKAGE_DAY_FOOTER_SIZE)
/* Minimum valid epoch: 2022-01-01 in Life_GPS 2020 epoch (2 * 365 * 86400). */
#define COMPACT_MIN_VALID_EPOCH_S 63072000U
#define COMPACT_COOLDOWN_MS (30U * 60U * 1000U)

K_THREAD_STACK_DEFINE(storage_workq_stack, COMPACT_STACK_SIZE);
static struct k_work_q storage_workq;
static bool storage_workq_started;
static struct k_work compact_work;
static atomic_t compact_busy = ATOMIC_INIT(0);

static uint8_t compact_raw_buf[COMPACT_RAW_CAP];
static uint8_t compact_scratch_buf[COMPACT_RAW_CAP];
static uint8_t compact_pkg_buf[COMPACT_PKG_CAP];

static uint32_t compact_req_now_utc_s;
static int32_t compact_req_tz_offset_s;
static bool compact_req_allow_today;
static uint32_t last_compact_uptime_ms;

static uint32_t rec_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void compact_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t today_day_id =
		lgps_day_id_from_epoch(compact_req_now_utc_s, compact_req_tz_offset_s);
	uint32_t target_day_id = 0U;
	uint32_t prev_day_id = 0U;
	uint32_t first_seq_id = 0U;
	uint16_t rec_count = 0U;
	uint16_t kept = 0U;
	uint32_t cursor = 1U;
	uint8_t scan_batch[16U * LGPS_RECORD_TRAJECTORY_SIZE];
	struct lgps_journal jsnap;

	lgps_archive_get_journal(&jsnap);
	if (jsnap.count > 0U) {
		prev_day_id = jsnap.entries[jsnap.count - 1U].day_id;
	}

	/* Pass 1: find oldest unarchived day_id present in the staging ring. */
	for (;;) {
		uint16_t got = 0U;

		if ((lgps_ring_read_from_seq(cursor, scan_batch, 16U, &got) != 0) || (got == 0U)) {
			break;
		}
		for (uint16_t i = 0U; i < got; i++) {
			const uint8_t *r = &scan_batch[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
			uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
			uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP_S]);
			uint32_t d;

			if (seq >= cursor) {
				cursor = seq + 1U;
			}
			if (ts < COMPACT_MIN_VALID_EPOCH_S) {
				continue;
			}
			d = lgps_day_id_from_epoch(ts, compact_req_tz_offset_s);
			if ((d > today_day_id) ||
			    ((d == today_day_id) && !compact_req_allow_today)) {
				continue;
			}
			if (lgps_journal_find(&jsnap, d) != NULL) {
				continue;
			}
			if ((target_day_id == 0U) || (d < target_day_id)) {
				target_day_id = d;
			}
		}
		if (got < 16U) {
			break;
		}
	}

	if (target_day_id == 0U) {
		(void)atomic_set(&compact_busy, 0);
		return;
	}

	/* Pass 2: collect up to COMPACT_MAX_RECS records for target_day_id. */
	cursor = 1U;
	while (kept < COMPACT_MAX_RECS) {
		uint16_t got = 0U;

		if ((lgps_ring_read_from_seq(cursor, scan_batch, 16U, &got) != 0) || (got == 0U)) {
			break;
		}
		for (uint16_t i = 0U; (i < got) && (kept < COMPACT_MAX_RECS); i++) {
			const uint8_t *r = &scan_batch[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
			uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
			uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP_S]);

			if (seq >= cursor) {
				cursor = seq + 1U;
			}
			if (ts < COMPACT_MIN_VALID_EPOCH_S) {
				continue;
			}
			if (lgps_day_id_from_epoch(ts, compact_req_tz_offset_s) != target_day_id) {
				continue;
			}
			if (kept == 0U) {
				first_seq_id = seq;
			}
			memcpy(&compact_raw_buf[(size_t)kept * LGPS_RECORD_TRAJECTORY_SIZE], r,
			       LGPS_RECORD_TRAJECTORY_SIZE);
			kept++;
		}
		if (got < 16U) {
			break;
		}
	}

	rec_count = kept;
	if (rec_count > 0U) {
		size_t raw_len = (size_t)rec_count * LGPS_RECORD_TRAJECTORY_SIZE;
		size_t pkg_len = 0U;
		uint16_t codec_id = LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK;
		uint32_t crc32 = lgps_crc32_iso_hdlc(compact_raw_buf, raw_len);
		int ret;

		ret = lgps_day_pkg_encode(target_day_id, prev_day_id, codec_id, compact_raw_buf,
					  raw_len, compact_scratch_buf, compact_pkg_buf,
					  sizeof(compact_pkg_buf), &pkg_len);
		if ((ret < 0) || (pkg_len >= (raw_len + LGPS_PACKAGE_DAY_HEADER_SIZE +
					      LGPS_PACKAGE_DAY_FOOTER_SIZE))) {
			codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;
			ret = lgps_day_pkg_encode(target_day_id, prev_day_id, codec_id,
						  compact_raw_buf, raw_len, NULL, compact_pkg_buf,
						  sizeof(compact_pkg_buf), &pkg_len);
		}
		if (ret == 0) {
			ret = lgps_archive_write_day(target_day_id, rec_count, crc32, first_seq_id,
						     codec_id, compact_pkg_buf, pkg_len);
			if (ret == 0) {
				LOG_INF("archived day %u (%u recs, %u -> %u B, codec=%u)",
					target_day_id, rec_count, (unsigned int)raw_len,
					(unsigned int)pkg_len, codec_id);
			} else {
				LOG_WRN("archive write day %u failed (%d)", target_day_id, ret);
			}
		} else {
			LOG_WRN("archive encode day %u failed (%d)", target_day_id, ret);
		}
	}

	(void)atomic_set(&compact_busy, 0);
}

void lgps_archive_maybe_compact(uint32_t now_utc_s, int32_t tz_offset_s, uint8_t archive_hour_utc,
				bool idle_or_charging, uint8_t soc_pct)
{
	uint32_t now_ms = k_uptime_get_32();
	uint8_t utc_hour;
	bool allow_today;

	if (now_utc_s < COMPACT_MIN_VALID_EPOCH_S) {
		return;
	}

	/* Require safe battery (>=30% or unknown/charging) before NOR erases. */
	if (!idle_or_charging && (soc_pct <= 100U) && (soc_pct < 30U)) {
		return;
	}

	if ((last_compact_uptime_ms != 0U) &&
	    ((uint32_t)(now_ms - last_compact_uptime_ms) < COMPACT_COOLDOWN_MS)) {
		return;
	}

	if (!storage_workq_started) {
		k_work_queue_init(&storage_workq);
		k_work_queue_start(&storage_workq, storage_workq_stack,
				   K_THREAD_STACK_SIZEOF(storage_workq_stack), COMPACT_THREAD_PRIO,
				   NULL);
		k_work_init(&compact_work, compact_work_handler);
		storage_workq_started = true;
	}

	if (!atomic_cas(&compact_busy, 0, 1)) {
		return;
	}

	utc_hour = (uint8_t)((now_utc_s / 3600U) % 24U);
	allow_today = idle_or_charging && (utc_hour == archive_hour_utc);

	compact_req_now_utc_s = now_utc_s;
	compact_req_tz_offset_s = tz_offset_s;
	compact_req_allow_today = allow_today;
	last_compact_uptime_ms = now_ms;

	(void)k_work_submit_to_queue(&storage_workq, &compact_work);
}
