/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - external NOR multi-sector ping-pong journal index &
 * untruncated streaming LGD1 archive store.
 *
 * Compatible with BOTH 32 MB (W25Q256) and 64 MB (W25Q512NW) SPI NOR Flash:
 *   - Sectors 20..35 (0x14000..0x23FFF): journal Bank A (64 KB, up to 2048 days)
 *   - Sectors 36..51 (0x24000..0x33FFF): journal Bank B (64 KB, up to 2048 days)
 *   - Offset 0x34000 .. (capacity.traj_bytes - 1): 4 KB-aligned LGD1 day packages
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
#define JOURNAL_BANK_BYTES (LGPS_ARCHIVE_JOURNAL_BANK_SECTORS * NOR_SECTOR_SIZE)
/* 20 + 2048 * 28 = 57,364 bytes <= 65,536 bytes (16 sectors per bank). */
#define JOURNAL_MAX_IMAGE_SIZE (JOURNAL_HDR_SIZE + (LGPS_JOURNAL_MAX_DAYS * LGPS_JOURNAL_ENTRY_SIZE))

BUILD_ASSERT(JOURNAL_MAX_IMAGE_SIZE <= JOURNAL_BANK_BYTES,
	     "journal image must fit inside one multi-sector NOR journal bank");

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(archive_lock);

/* Single static journal table (~56 KB); never instantiate struct lgps_journal on any stack. */
static struct lgps_journal journal_state;
static uint8_t active_bank_idx; /* 0 -> Bank A (sectors 20..35), 1 -> Bank B (sectors 36..51) */
/* Single 4 KB streaming I/O buffer serialized by archive_lock. */
static uint8_t io_buf[NOR_SECTOR_SIZE];

/* --- Background ring-to-LGD1 daily compactor state ------------------------- */

#define COMPACT_STACK_SIZE 3072
#define COMPACT_THREAD_PRIO K_PRIO_PREEMPT(12)
#define COMPACT_COMPRESS_MAX_RECS 256U
#define COMPACT_RAW_CAP (COMPACT_COMPRESS_MAX_RECS * LGPS_RECORD_TRAJECTORY_SIZE)
#define COMPACT_PKG_CAP                                                                            \
	(LGPS_PACKAGE_DAY_HEADER_SIZE + COMPACT_RAW_CAP + 512U + LGPS_PACKAGE_DAY_FOOTER_SIZE)
/* Minimum valid epoch: 2022-01-01 in Life_GPS 2020 epoch (2 * 365 * 86400). */
#define COMPACT_MIN_VALID_EPOCH_S 63072000U
#define COMPACT_COOLDOWN_MS (30U * 60U * 1000U)
/* 75% of 16 sectors * 127 records/sector (2032 records). */
#define COMPACT_RING_HIGH_WATER_RECS 1524U
#define COMPACT_STREAM_CHUNK_BYTES 512U

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
static uint32_t last_archived_seq;

/* --- Little-endian & range helpers ----------------------------------------- */

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

static uint32_t align_4k(uint32_t len)
{
	if (len > (UINT32_MAX - (NOR_SECTOR_SIZE - 1U))) {
		return UINT32_MAX;
	}
	return (len + (NOR_SECTOR_SIZE - 1U)) & ~(NOR_SECTOR_SIZE - 1U);
}

static bool ranges_overlap(uint32_t a_start, uint32_t a_len, uint32_t b_start, uint32_t b_len)
{
	if ((a_len == 0U) || (b_len == 0U)) {
		return false;
	}
	if (a_start < b_start) {
		return (b_start - a_start) < a_len;
	}
	return (a_start - b_start) < b_len;
}

static off_t journal_bank_offset(uint8_t idx)
{
	uint32_t sector = (idx == 0U) ? LGPS_ARCHIVE_JOURNAL_SECTOR_A
				      : LGPS_ARCHIVE_JOURNAL_SECTOR_B;

	return (off_t)sector * (off_t)NOR_SECTOR_SIZE;
}

/* --- Multi-sector ping-pong journal bank I/O (caller holds archive_lock) --- */

static bool validate_journal_bank_locked(uint8_t bank_idx, uint32_t flash_limit, uint32_t *ver_out,
					 uint32_t *next_off_out, uint16_t *count_out)
{
	off_t bank_off = journal_bank_offset(bank_idx);
	uint32_t magic;
	uint32_t ver;
	uint32_t next_off;
	uint16_t count;
	uint16_t stored_crc;
	uint16_t computed_crc;
	size_t remaining;
	size_t pos;

	if (flash_read(nor, bank_off, io_buf, JOURNAL_HDR_SIZE) < 0) {
		return false;
	}

	magic = get_u32(&io_buf[0]);
	if (magic != LGPS_JOURNAL_MAGIC) {
		return false;
	}

	ver = get_u32(&io_buf[4]);
	next_off = get_u32(&io_buf[8]);
	count = get_u16(&io_buf[12]);
	stored_crc = get_u16(&io_buf[18]);

	if ((count > LGPS_JOURNAL_MAX_DAYS) || (next_off < LGPS_ARCHIVE_DATA_OFFSET) ||
	    (next_off > flash_limit)) {
		return false;
	}

	/* Streaming CRC-16 over [0..17] + zeroed [18..19] + entry array [20..img_len-1]. */
	io_buf[18] = 0U;
	io_buf[19] = 0U;
	computed_crc = lgps_crc16_ccitt_false_update(0xFFFFU, io_buf, JOURNAL_HDR_SIZE);

	pos = JOURNAL_HDR_SIZE;
	remaining = (size_t)count * LGPS_JOURNAL_ENTRY_SIZE;
	while (remaining > 0U) {
		size_t chunk = (remaining > NOR_SECTOR_SIZE) ? NOR_SECTOR_SIZE : remaining;

		if (flash_read(nor, bank_off + (off_t)pos, io_buf, chunk) < 0) {
			return false;
		}
		computed_crc = lgps_crc16_ccitt_false_update(computed_crc, io_buf, chunk);
		pos += chunk;
		remaining -= chunk;
	}

	if (computed_crc != stored_crc) {
		return false;
	}

	*ver_out = ver;
	*next_off_out = next_off;
	*count_out = count;
	return true;
}

static bool load_journal_bank_entries_locked(uint8_t bank_idx, uint32_t flash_limit, uint32_t ver,
					     uint32_t next_off, uint16_t count)
{
	const uint16_t entries_per_chunk = (uint16_t)(NOR_SECTOR_SIZE / LGPS_JOURNAL_ENTRY_SIZE);
	off_t bank_off = journal_bank_offset(bank_idx);
	uint16_t loaded = 0U;

	lgps_journal_init(&journal_state, flash_limit);
	journal_state.index_version = ver;
	journal_state.next_write_offset = next_off;
	journal_state.count = count;

	while (loaded < count) {
		uint16_t batch = (uint16_t)(count - loaded);
		size_t bytes;
		off_t flash_pos;

		if (batch > entries_per_chunk) {
			batch = entries_per_chunk;
		}
		bytes = (size_t)batch * LGPS_JOURNAL_ENTRY_SIZE;
		flash_pos = bank_off + (off_t)JOURNAL_HDR_SIZE +
			    (off_t)((size_t)loaded * LGPS_JOURNAL_ENTRY_SIZE);

		if (flash_read(nor, flash_pos, io_buf, bytes) < 0) {
			lgps_journal_init(&journal_state, flash_limit);
			return false;
		}

		for (uint16_t k = 0U; k < batch; k++) {
			lgps_journal_entry_decode(&io_buf[(size_t)k * LGPS_JOURNAL_ENTRY_SIZE],
						  &journal_state.entries[loaded + k]);
		}
		loaded = (uint16_t)(loaded + batch);
	}

	return true;
}

static int persist_journal_locked(void)
{
	uint8_t target_idx = active_bank_idx ^ 1U;
	uint8_t hdr[JOURNAL_HDR_SIZE];
	uint8_t ent_buf[LGPS_JOURNAL_ENTRY_SIZE];
	size_t img_len;
	size_t sectors_needed;
	uint16_t crc;
	off_t write_off;
	size_t buf_used;
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	img_len = JOURNAL_HDR_SIZE + ((size_t)journal_state.count * LGPS_JOURNAL_ENTRY_SIZE);
	if (img_len > JOURNAL_BANK_BYTES) {
		return -ENOSPC;
	}

	/* Pass 1: build header and compute streaming CRC-16 with zero extra RAM. */
	memset(hdr, 0, sizeof(hdr));
	put_u32(&hdr[0], LGPS_JOURNAL_MAGIC);
	put_u32(&hdr[4], journal_state.index_version);
	put_u32(&hdr[8], journal_state.next_write_offset);
	put_u16(&hdr[12], journal_state.count);
	put_u32(&hdr[14], journal_state.flash_limit_bytes);
	hdr[18] = 0U;
	hdr[19] = 0U;

	crc = lgps_crc16_ccitt_false_update(0xFFFFU, hdr, JOURNAL_HDR_SIZE);
	for (uint16_t i = 0U; i < journal_state.count; i++) {
		lgps_journal_entry_encode(&journal_state.entries[i], ent_buf);
		crc = lgps_crc16_ccitt_false_update(crc, ent_buf, sizeof(ent_buf));
	}
	put_u16(&hdr[18], crc);

	/* Erase only the sectors needed for this image (clamped >= 1). */
	sectors_needed = (img_len + (NOR_SECTOR_SIZE - 1U)) / NOR_SECTOR_SIZE;
	if (sectors_needed == 0U) {
		sectors_needed = 1U;
	}

	write_off = journal_bank_offset(target_idx);
	ret = flash_erase(nor, write_off, sectors_needed * NOR_SECTOR_SIZE);
	if (ret < 0) {
		return ret;
	}

	/* Pass 2: pack header + entries into io_buf[4096] and write sector-by-sector. */
	memcpy(io_buf, hdr, JOURNAL_HDR_SIZE);
	buf_used = JOURNAL_HDR_SIZE;

	for (uint16_t i = 0U; i < journal_state.count; i++) {
		size_t ent_off = 0U;

		lgps_journal_entry_encode(&journal_state.entries[i], ent_buf);
		while (ent_off < sizeof(ent_buf)) {
			size_t space = NOR_SECTOR_SIZE - buf_used;
			size_t copy = sizeof(ent_buf) - ent_off;

			if (copy > space) {
				copy = space;
			}
			memcpy(&io_buf[buf_used], &ent_buf[ent_off], copy);
			buf_used += copy;
			ent_off += copy;

			if (buf_used == NOR_SECTOR_SIZE) {
				ret = flash_write(nor, write_off, io_buf, NOR_SECTOR_SIZE);
				if (ret < 0) {
					return ret;
				}
				write_off += (off_t)NOR_SECTOR_SIZE;
				buf_used = 0U;
			}
		}
	}

	if (buf_used > 0U) {
		ret = flash_write(nor, write_off, io_buf, buf_used);
		if (ret < 0) {
			return ret;
		}
	}

	active_bank_idx = target_idx;
	return 0;
}

/* --- Write-region allocator, overlap eviction, and readback verification --- */

static int prepare_write_region_locked(uint32_t aligned_len, uint32_t replace_day_id,
				       uint32_t protect_off, uint32_t protect_aligned_len,
				       uint32_t *write_off_out)
{
	uint32_t avail;
	uint32_t write_off;
	uint16_t idx;

	if ((write_off_out == NULL) || (aligned_len == 0U) || (aligned_len == UINT32_MAX) ||
	    (journal_state.flash_limit_bytes <= LGPS_ARCHIVE_DATA_OFFSET)) {
		return -ENOSPC;
	}

	avail = journal_state.flash_limit_bytes - LGPS_ARCHIVE_DATA_OFFSET;
	if (aligned_len > avail) {
		return -ENOSPC;
	}
	if ((protect_aligned_len > 0U) &&
	    ((protect_aligned_len > avail) || (aligned_len > (avail - protect_aligned_len)))) {
		return -ENOSPC;
	}

	write_off = journal_state.next_write_offset;
	if ((write_off < LGPS_ARCHIVE_DATA_OFFSET) ||
	    (write_off > journal_state.flash_limit_bytes) ||
	    (aligned_len > (journal_state.flash_limit_bytes - write_off))) {
		write_off = LGPS_ARCHIVE_DATA_OFFSET;
	}

	/* Never overwrite an existing day currently being read for merge. */
	if ((protect_aligned_len > 0U) &&
	    ranges_overlap(write_off, aligned_len, protect_off, protect_aligned_len)) {
		if ((protect_off <= journal_state.flash_limit_bytes) &&
		    (protect_aligned_len <= (journal_state.flash_limit_bytes - protect_off))) {
			write_off = protect_off + protect_aligned_len;
		} else {
			write_off = LGPS_ARCHIVE_DATA_OFFSET;
		}

		if ((write_off > journal_state.flash_limit_bytes) ||
		    (aligned_len > (journal_state.flash_limit_bytes - write_off))) {
			write_off = LGPS_ARCHIVE_DATA_OFFSET;
		}

		if (ranges_overlap(write_off, aligned_len, protect_off, protect_aligned_len)) {
			return -ENOSPC;
		}
	}

	/* Evict all existing entries whose flash sectors overlap [write_off, write_off + aligned_len). */
	idx = 0U;
	while (idx < journal_state.count) {
		const struct lgps_journal_entry *e = &journal_state.entries[idx];
		uint32_t e_len = align_4k(e->package_len);

		if ((replace_day_id != 0U) && (e->day_id == replace_day_id)) {
			idx++;
			continue;
		}
		if (ranges_overlap(write_off, aligned_len, e->flash_offset, e_len)) {
			LOG_WRN("archive write at 0x%08x evicts overlapping day %u", write_off,
				e->day_id);
			(void)lgps_journal_remove(&journal_state, e->day_id);
			continue;
		}
		idx++;
	}

	/* Ensure room in the 2048-day table when adding a brand new day. */
	if ((journal_state.count >= LGPS_JOURNAL_MAX_DAYS) &&
	    ((replace_day_id == 0U) || (lgps_journal_find(&journal_state, replace_day_id) == NULL))) {
		uint32_t oldest_day = journal_state.entries[0].day_id;

		(void)lgps_journal_remove(&journal_state, oldest_day);
		LOG_WRN("archive journal full (%u days), evicted oldest day %u",
			LGPS_JOURNAL_MAX_DAYS, oldest_day);
	}

	*write_off_out = write_off;
	return 0;
}

/*
 * README §8.3 Step 2: read back the written LGD1 package from NOR and verify
 * header CRC-16, total length, full-package CRC-32, and footer traj_crc32
 * BEFORE committing the journal entry. Caller holds archive_lock.
 */
static int verify_written_lgd1_locked(uint32_t flash_off, size_t pkg_len,
				      uint32_t expected_traj_crc32, uint32_t expected_pkg_crc32)
{
	struct lgps_day_header hdr;
	uint8_t footer[LGPS_PACKAGE_DAY_FOOTER_SIZE];
	size_t expected_len;
	uint32_t pkg_crc_run = 0xFFFFFFFFU;
	size_t pos = 0U;
	int ret;

	if (pkg_len < ((size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + LGPS_PACKAGE_DAY_FOOTER_SIZE)) {
		return -EINVAL;
	}

	ret = flash_read(nor, (off_t)flash_off, io_buf, LGPS_PACKAGE_DAY_HEADER_SIZE);
	if (ret < 0) {
		return ret;
	}

	ret = lgps_day_pkg_parse_header(io_buf, LGPS_PACKAGE_DAY_HEADER_SIZE, &hdr);
	if (ret < 0) {
		LOG_ERR("LGD1 readback header invalid at 0x%08x (%d)", flash_off, ret);
		return -EIO;
	}

	expected_len = (size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + (size_t)hdr.traj_packed_len +
		       (size_t)LGPS_PACKAGE_DAY_FOOTER_SIZE;
	if ((expected_len != pkg_len) ||
	    (hdr.traj_raw_len != (hdr.record_count * LGPS_RECORD_TRAJECTORY_SIZE))) {
		LOG_ERR("LGD1 readback length mismatch at 0x%08x (hdr=%u exp=%u)", flash_off,
			(unsigned int)expected_len, (unsigned int)pkg_len);
		return -EIO;
	}

	while (pos < pkg_len) {
		size_t chunk = pkg_len - pos;

		if (chunk > NOR_SECTOR_SIZE) {
			chunk = NOR_SECTOR_SIZE;
		}
		ret = flash_read(nor, (off_t)(flash_off + pos), io_buf, chunk);
		if (ret < 0) {
			return ret;
		}
		pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, io_buf, chunk);
		pos += chunk;
	}

	if ((~pkg_crc_run) != expected_pkg_crc32) {
		LOG_ERR("LGD1 readback CRC-32 mismatch at 0x%08x (got=0x%08x exp=0x%08x)", flash_off,
			(unsigned int)(~pkg_crc_run), (unsigned int)expected_pkg_crc32);
		return -EIO;
	}

	ret = flash_read(nor, (off_t)(flash_off + pkg_len - LGPS_PACKAGE_DAY_FOOTER_SIZE), footer,
			 sizeof(footer));
	if (ret < 0) {
		return ret;
	}
	if ((get_u32(&footer[LGPS_PACKAGE_DAY_FOOTER_OFF_TRAJ_CRC32]) != expected_traj_crc32) ||
	    (get_u32(&footer[LGPS_PACKAGE_DAY_FOOTER_OFF_RESERVED]) != 0U)) {
		LOG_ERR("LGD1 readback footer mismatch at 0x%08x", flash_off);
		return -EIO;
	}

	return 0;
}

/* --- Public runtime NOR journal & archive API ------------------------------ */

int lgps_archive_init(void)
{
	struct lgps_ring_capacity cap = { 0 };
	uint32_t ver_a = 0U;
	uint32_t next_a = 0U;
	uint16_t cnt_a = 0U;
	uint32_t ver_b = 0U;
	uint32_t next_b = 0U;
	uint16_t cnt_b = 0U;
	bool valid_a = false;
	bool valid_b = false;

	lgps_ring_get_capacity(&cap);

	k_mutex_lock(&archive_lock, K_FOREVER);
	lgps_journal_init(&journal_state, cap.traj_bytes);
	active_bank_idx = 0U;
	last_archived_seq = 0U;

	if (device_is_ready(nor) && (cap.traj_bytes > LGPS_ARCHIVE_DATA_OFFSET)) {
		valid_a = validate_journal_bank_locked(0U, cap.traj_bytes, &ver_a, &next_a, &cnt_a);
		valid_b = validate_journal_bank_locked(1U, cap.traj_bytes, &ver_b, &next_b, &cnt_b);

		if (valid_a && (!valid_b || (ver_a >= ver_b))) {
			if (load_journal_bank_entries_locked(0U, cap.traj_bytes, ver_a, next_a,
							     cnt_a)) {
				active_bank_idx = 0U;
			} else if (valid_b &&
				   load_journal_bank_entries_locked(1U, cap.traj_bytes, ver_b,
								    next_b, cnt_b)) {
				active_bank_idx = 1U;
			}
		} else if (valid_b) {
			if (load_journal_bank_entries_locked(1U, cap.traj_bytes, ver_b, next_b,
							     cnt_b)) {
				active_bank_idx = 1U;
			} else if (valid_a &&
				   load_journal_bank_entries_locked(0U, cap.traj_bytes, ver_a,
								    next_a, cnt_a)) {
				active_bank_idx = 0U;
			}
		}

		LOG_INF("archive journal mounted: %u/%u days, bank=%c, ver=%u, limit=%u B (%u MB flash)",
			journal_state.count, LGPS_JOURNAL_MAX_DAYS,
			(active_bank_idx == 0U) ? 'A' : 'B', journal_state.index_version,
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

uint16_t lgps_archive_copy_entries(uint16_t start_idx, struct lgps_journal_entry *out,
				   uint16_t max_entries, uint16_t *total_out,
				   uint32_t *index_ver_out)
{
	uint16_t copied = 0U;

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (total_out != NULL) {
		*total_out = journal_state.count;
	}
	if (index_ver_out != NULL) {
		*index_ver_out = journal_state.index_version;
	}
	if ((out != NULL) && (max_entries > 0U) && (start_idx < journal_state.count)) {
		uint16_t avail = (uint16_t)(journal_state.count - start_idx);

		copied = (avail < max_entries) ? avail : max_entries;
		memcpy(out, &journal_state.entries[start_idx],
		       (size_t)copied * sizeof(struct lgps_journal_entry));
	}
	k_mutex_unlock(&archive_lock);

	return copied;
}

bool lgps_archive_has_day(uint32_t day_id)
{
	bool found;

	k_mutex_lock(&archive_lock, K_FOREVER);
	found = (lgps_journal_find(&journal_state, day_id) != NULL);
	k_mutex_unlock(&archive_lock);

	return found;
}

int lgps_archive_get_day_entry(uint32_t day_id, struct lgps_journal_entry *out)
{
	const struct lgps_journal_entry *found;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&archive_lock, K_FOREVER);
	found = lgps_journal_find(&journal_state, day_id);
	if (found == NULL) {
		k_mutex_unlock(&archive_lock);
		return -ENOENT;
	}
	*out = *found;
	k_mutex_unlock(&archive_lock);

	return 0;
}

uint16_t lgps_archive_day_count(void)
{
	uint16_t count;

	k_mutex_lock(&archive_lock, K_FOREVER);
	count = journal_state.count;
	k_mutex_unlock(&archive_lock);

	return count;
}

uint32_t lgps_archive_index_version(void)
{
	uint32_t ver;

	k_mutex_lock(&archive_lock, K_FOREVER);
	ver = journal_state.index_version;
	k_mutex_unlock(&archive_lock);

	return ver;
}

int lgps_archive_write_day(uint32_t day_id, uint32_t record_count, uint32_t traj_crc32,
			   uint32_t first_seq_id, uint16_t codec_id, const uint8_t *pkg_data,
			   size_t pkg_len)
{
	const struct lgps_journal_entry *existing;
	struct lgps_journal_entry entry;
	uint32_t aligned_len;
	uint32_t protect_off = 0U;
	uint32_t protect_aligned_len = 0U;
	uint32_t replace_day_id = 0U;
	uint32_t write_off = 0U;
	uint32_t expected_pkg_crc32;
	int ret;

	if ((day_id == 0U) || (pkg_data == NULL) ||
	    (pkg_len < ((size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + LGPS_PACKAGE_DAY_FOOTER_SIZE)) ||
	    (pkg_len > (size_t)(UINT32_MAX - (NOR_SECTOR_SIZE - 1U)))) {
		return -EINVAL;
	}

	expected_pkg_crc32 = lgps_crc32_iso_hdlc(pkg_data, pkg_len);
	aligned_len = align_4k((uint32_t)pkg_len);

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (!device_is_ready(nor)) {
		k_mutex_unlock(&archive_lock);
		return -ENODEV;
	}

	existing = lgps_journal_find(&journal_state, day_id);
	if (existing != NULL) {
		replace_day_id = day_id;
		protect_off = existing->flash_offset;
		protect_aligned_len = align_4k(existing->package_len);
	}

	ret = prepare_write_region_locked(aligned_len, replace_day_id, protect_off,
					  protect_aligned_len, &write_off);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
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

	ret = verify_written_lgd1_locked(write_off, pkg_len, traj_crc32, expected_pkg_crc32);
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
		journal_state.next_write_offset = write_off + aligned_len;
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
	aligned_len = align_4k(found->package_len);

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
	active_bank_idx = 0U;
	last_archived_seq = 0U;
	if (device_is_ready(nor)) {
		/* Erase sector 0 of both banks to invalidate both JNL1 headers immediately. */
		int r0 = flash_erase(nor, journal_bank_offset(0U), NOR_SECTOR_SIZE);
		int r1 = flash_erase(nor, journal_bank_offset(1U), NOR_SECTOR_SIZE);

		ret = (r0 < 0) ? r0 : r1;
	}
	k_mutex_unlock(&archive_lock);
	return ret;
}

uint16_t lgps_archive_used_permille(void)
{
	uint64_t used = 0U;
	uint64_t total;
	uint16_t permille = 0U;

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (journal_state.flash_limit_bytes > LGPS_ARCHIVE_DATA_OFFSET) {
		total = (uint64_t)(journal_state.flash_limit_bytes - LGPS_ARCHIVE_DATA_OFFSET);
		for (uint16_t i = 0U; i < journal_state.count; i++) {
			used += (uint64_t)align_4k(journal_state.entries[i].package_len);
		}
		permille = (uint16_t)((used * 1000U) / total);
		if (permille > 1000U) {
			permille = 1000U;
		}
	}
	k_mutex_unlock(&archive_lock);
	return permille;
}

/* --- Background streaming ring-to-LGD1 daily compactor --------------------- */

static uint32_t rec_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/*
 * Return the highest seq_id stored inside an existing journal entry.
 * Caller holds archive_lock and runs on storage_workq (sole user of compact_*_buf).
 */
static int archive_entry_max_seq_locked(const struct lgps_journal_entry *e, uint32_t *max_seq_out)
{
	if ((e == NULL) || (max_seq_out == NULL)) {
		return -EINVAL;
	}
	if (e->record_count == 0U) {
		*max_seq_out = 0U;
		return 0;
	}
	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	if (e->codec_id == LGPS_PACKAGE_DAY_CODEC_RAW) {
		uint8_t rec[LGPS_RECORD_TRAJECTORY_SIZE];
		uint32_t raw_bytes = e->record_count * LGPS_RECORD_TRAJECTORY_SIZE;
		off_t last_off;
		int ret;

		if (e->package_len <
		    (LGPS_PACKAGE_DAY_HEADER_SIZE + raw_bytes + LGPS_PACKAGE_DAY_FOOTER_SIZE)) {
			return -EBADMSG;
		}
		last_off = (off_t)e->flash_offset + (off_t)LGPS_PACKAGE_DAY_HEADER_SIZE +
			   (off_t)((e->record_count - 1U) * LGPS_RECORD_TRAJECTORY_SIZE);
		ret = flash_read(nor, last_off, rec, sizeof(rec));
		if (ret < 0) {
			return ret;
		}
		*max_seq_out = rec_u32_le(&rec[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
		return 0;
	}

	if (e->codec_id == LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK) {
		size_t decoded_len = 0U;
		int ret;

		if ((e->package_len > sizeof(compact_pkg_buf)) ||
		    (e->record_count > COMPACT_COMPRESS_MAX_RECS)) {
			return -ENOMEM;
		}
		ret = flash_read(nor, (off_t)e->flash_offset, compact_pkg_buf, e->package_len);
		if (ret < 0) {
			return ret;
		}
		ret = lgps_day_pkg_decode(compact_pkg_buf, e->package_len, compact_scratch_buf,
					  NULL, compact_raw_buf, sizeof(compact_raw_buf),
					  &decoded_len);
		if (ret < 0) {
			return ret;
		}
		if (decoded_len < LGPS_RECORD_TRAJECTORY_SIZE) {
			return -EBADMSG;
		}
		*max_seq_out = rec_u32_le(&compact_raw_buf[decoded_len -
							   LGPS_RECORD_TRAJECTORY_SIZE +
							   LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
		return 0;
	}

	return -ENOTSUP;
}

static int archive_lookup_day_max_seq(uint32_t day_id, bool *has_entry_out,
				      struct lgps_journal_entry *entry_out, uint32_t *max_seq_out)
{
	const struct lgps_journal_entry *found;
	int ret = 0;

	*has_entry_out = false;
	*max_seq_out = 0U;

	k_mutex_lock(&archive_lock, K_FOREVER);
	found = lgps_journal_find(&journal_state, day_id);
	if (found != NULL) {
		*has_entry_out = true;
		if (entry_out != NULL) {
			*entry_out = *found;
		}
		ret = archive_entry_max_seq_locked(found, max_seq_out);
	}
	k_mutex_unlock(&archive_lock);

	return ret;
}

static uint32_t archive_find_prev_day_id(uint32_t exclude_day_id)
{
	uint32_t prev = 0U;

	k_mutex_lock(&archive_lock, K_FOREVER);
	for (uint16_t i = journal_state.count; i > 0U; i--) {
		uint32_t candidate = journal_state.entries[i - 1U].day_id;

		if (candidate != exclude_day_id) {
			prev = candidate;
			break;
		}
	}
	k_mutex_unlock(&archive_lock);

	return prev;
}

/*
 * Stream-write an untruncated RAW LGD1 package directly to NOR, optionally
 * merging an existing archived copy of @p target_day_id with @p new_count new
 * ring records up to @p new_max_seq. Runs readback verification before commit.
 */
static int stream_write_day_raw(uint32_t target_day_id, uint32_t prev_day_id, bool has_existing,
				const struct lgps_journal_entry *existing, uint32_t existing_max_seq,
				uint32_t new_count, uint32_t new_first_seq, uint32_t new_max_seq,
				int32_t tz_offset_s)
{
	uint32_t existing_recs = has_existing ? existing->record_count : 0U;
	uint32_t total_recs;
	uint32_t raw_len;
	uint32_t pkg_len;
	uint32_t aligned_len;
	uint32_t write_off = 0U;
	uint32_t pkg_crc_run;
	uint32_t traj_crc_run;
	uint32_t traj_crc32;
	uint32_t pos;
	uint32_t cursor;
	uint32_t written_new = 0U;
	size_t chunk_len = 0U;
	uint8_t hdr[LGPS_PACKAGE_DAY_HEADER_SIZE];
	uint8_t footer[LGPS_PACKAGE_DAY_FOOTER_SIZE];
	struct lgps_journal_entry entry;
	int ret;

	if (new_count > (UINT32_MAX - existing_recs)) {
		return -EOVERFLOW;
	}
	total_recs = existing_recs + new_count;
	if ((total_recs == 0U) ||
	    (total_recs > ((UINT32_MAX - LGPS_PACKAGE_DAY_HEADER_SIZE -
			    LGPS_PACKAGE_DAY_FOOTER_SIZE - (NOR_SECTOR_SIZE - 1U)) /
			   LGPS_RECORD_TRAJECTORY_SIZE))) {
		return -EOVERFLOW;
	}

	raw_len = total_recs * LGPS_RECORD_TRAJECTORY_SIZE;
	pkg_len = LGPS_PACKAGE_DAY_HEADER_SIZE + raw_len + LGPS_PACKAGE_DAY_FOOTER_SIZE;
	aligned_len = align_4k(pkg_len);

	k_mutex_lock(&archive_lock, K_FOREVER);
	if (!device_is_ready(nor)) {
		k_mutex_unlock(&archive_lock);
		return -ENODEV;
	}

	if (has_existing) {
		const struct lgps_journal_entry *cur =
			lgps_journal_find(&journal_state, target_day_id);

		if ((cur == NULL) || (cur->flash_offset != existing->flash_offset) ||
		    (cur->record_count != existing->record_count) ||
		    (cur->package_len != existing->package_len)) {
			k_mutex_unlock(&archive_lock);
			return -EAGAIN;
		}
	}

	ret = prepare_write_region_locked(aligned_len, has_existing ? target_day_id : 0U,
					  has_existing ? existing->flash_offset : 0U,
					  has_existing ? align_4k(existing->package_len) : 0U,
					  &write_off);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	ret = flash_erase(nor, (off_t)write_off, aligned_len);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	/* 1. Write 48-byte LGD1 header (RAW codec). */
	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 'L';
	hdr[1] = 'G';
	hdr[2] = 'D';
	hdr[3] = '1';
	put_u16(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_VER], LGPS_PACKAGE_DAY_HEADER_VER);
	put_u16(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_CODEC], LGPS_PACKAGE_DAY_CODEC_RAW);
	put_u32(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_DAY_ID], target_day_id);
	put_u32(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_RECORD_COUNT], total_recs);
	put_u32(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_RAW_LEN], raw_len);
	put_u32(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_PACKED_LEN], raw_len);
	put_u32(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_PREV_DAY_ID], prev_day_id);
	put_u16(&hdr[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16],
		lgps_crc16_ccitt_false(hdr, LGPS_PACKAGE_DAY_HEADER_CRC16_LEN));

	ret = flash_write(nor, (off_t)write_off, hdr, sizeof(hdr));
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	pkg_crc_run = lgps_crc32_iso_hdlc_update(0xFFFFFFFFU, hdr, sizeof(hdr));
	traj_crc_run = 0xFFFFFFFFU;
	pos = LGPS_PACKAGE_DAY_HEADER_SIZE;

	/* 2. Stream existing day records first when merging. */
	if (has_existing && (existing->record_count > 0U)) {
		if (existing->codec_id == LGPS_PACKAGE_DAY_CODEC_RAW) {
			uint32_t old_raw_len = existing->record_count * LGPS_RECORD_TRAJECTORY_SIZE;
			uint32_t copied = 0U;

			while (copied < old_raw_len) {
				size_t chunk = old_raw_len - copied;

				if (chunk > COMPACT_STREAM_CHUNK_BYTES) {
					chunk = COMPACT_STREAM_CHUNK_BYTES;
				}
				ret = flash_read(nor,
						 (off_t)(existing->flash_offset +
							 LGPS_PACKAGE_DAY_HEADER_SIZE + copied),
						 io_buf, chunk);
				if (ret < 0) {
					k_mutex_unlock(&archive_lock);
					return ret;
				}
				ret = flash_write(nor, (off_t)(write_off + pos), io_buf, chunk);
				if (ret < 0) {
					k_mutex_unlock(&archive_lock);
					return ret;
				}
				traj_crc_run = lgps_crc32_iso_hdlc_update(traj_crc_run, io_buf,
									  chunk);
				pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, io_buf,
									 chunk);
				copied += (uint32_t)chunk;
				pos += (uint32_t)chunk;
			}
		} else if (existing->codec_id == LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK) {
			size_t decoded_len = 0U;

			if ((existing->package_len > sizeof(compact_pkg_buf)) ||
			    (existing->record_count > COMPACT_COMPRESS_MAX_RECS)) {
				k_mutex_unlock(&archive_lock);
				return -ENOMEM;
			}
			ret = flash_read(nor, (off_t)existing->flash_offset, compact_pkg_buf,
					 existing->package_len);
			if (ret < 0) {
				k_mutex_unlock(&archive_lock);
				return ret;
			}
			ret = lgps_day_pkg_decode(compact_pkg_buf, existing->package_len,
						  compact_scratch_buf, NULL, compact_raw_buf,
						  sizeof(compact_raw_buf), &decoded_len);
			if ((ret < 0) ||
			    (decoded_len !=
			     (size_t)(existing->record_count * LGPS_RECORD_TRAJECTORY_SIZE))) {
				k_mutex_unlock(&archive_lock);
				return (ret < 0) ? ret : -EBADMSG;
			}
			ret = flash_write(nor, (off_t)(write_off + pos), compact_raw_buf,
					  decoded_len);
			if (ret < 0) {
				k_mutex_unlock(&archive_lock);
				return ret;
			}
			traj_crc_run = lgps_crc32_iso_hdlc_update(traj_crc_run, compact_raw_buf,
								  decoded_len);
			pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, compact_raw_buf,
								 decoded_len);
			pos += (uint32_t)decoded_len;
		} else {
			k_mutex_unlock(&archive_lock);
			return -ENOTSUP;
		}
	}

	/* 3. Stream all new ring records for target_day_id in 512 B batches. */
	cursor = has_existing ? (existing_max_seq + 1U) : 1U;
	while (written_new < new_count) {
		uint16_t got = 0U;

		/* compact_raw_buf is free now; reuse its first 512 B for ring reads. */
		ret = lgps_ring_read_from_seq(cursor, compact_raw_buf, 16U, &got);
		if (ret < 0) {
			k_mutex_unlock(&archive_lock);
			return ret;
		}
		if (got == 0U) {
			break;
		}

		for (uint16_t i = 0U; (i < got) && (written_new < new_count); i++) {
			const uint8_t *r = &compact_raw_buf[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
			uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
			uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP]);

			if (seq >= cursor) {
				cursor = seq + 1U;
			}
			if ((has_existing && (seq <= existing_max_seq)) || (seq > new_max_seq)) {
				continue;
			}
			if (ts < COMPACT_MIN_VALID_EPOCH_S) {
				continue;
			}
			if (lgps_day_id_from_epoch(ts, tz_offset_s) != target_day_id) {
				continue;
			}

			memcpy(&io_buf[chunk_len], r, LGPS_RECORD_TRAJECTORY_SIZE);
			chunk_len += LGPS_RECORD_TRAJECTORY_SIZE;
			written_new++;

			if (chunk_len == COMPACT_STREAM_CHUNK_BYTES) {
				ret = flash_write(nor, (off_t)(write_off + pos), io_buf, chunk_len);
				if (ret < 0) {
					k_mutex_unlock(&archive_lock);
					return ret;
				}
				traj_crc_run = lgps_crc32_iso_hdlc_update(traj_crc_run, io_buf,
									  chunk_len);
				pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, io_buf,
									 chunk_len);
				pos += (uint32_t)chunk_len;
				chunk_len = 0U;
			}
		}

		if (got < 16U) {
			break;
		}
	}

	if (chunk_len > 0U) {
		ret = flash_write(nor, (off_t)(write_off + pos), io_buf, chunk_len);
		if (ret < 0) {
			k_mutex_unlock(&archive_lock);
			return ret;
		}
		traj_crc_run = lgps_crc32_iso_hdlc_update(traj_crc_run, io_buf, chunk_len);
		pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, io_buf, chunk_len);
		pos += (uint32_t)chunk_len;
	}

	if ((written_new != new_count) || (pos != (LGPS_PACKAGE_DAY_HEADER_SIZE + raw_len))) {
		LOG_WRN("archive stream count mismatch day %u (%u != %u)", target_day_id,
			written_new, new_count);
		k_mutex_unlock(&archive_lock);
		return -EAGAIN;
	}

	/* 4. Write 8-byte LGD1 footer [u32 traj_crc32, u32 reserved=0]. */
	traj_crc32 = ~traj_crc_run;
	put_u32(&footer[LGPS_PACKAGE_DAY_FOOTER_OFF_TRAJ_CRC32], traj_crc32);
	put_u32(&footer[LGPS_PACKAGE_DAY_FOOTER_OFF_RESERVED], 0U);

	ret = flash_write(nor, (off_t)(write_off + pos), footer, sizeof(footer));
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}
	pkg_crc_run = lgps_crc32_iso_hdlc_update(pkg_crc_run, footer, sizeof(footer));
	pos += (uint32_t)sizeof(footer);

	/* 5. Readback verify from NOR before committing journal. */
	ret = verify_written_lgd1_locked(write_off, pos, traj_crc32, ~pkg_crc_run);
	if (ret < 0) {
		k_mutex_unlock(&archive_lock);
		return ret;
	}

	/* 6. Commit journal entry and persist ping-pong bank. */
	memset(&entry, 0, sizeof(entry));
	entry.day_id = target_day_id;
	entry.flash_offset = write_off;
	entry.package_len = pos;
	entry.record_count = total_recs;
	entry.traj_crc32 = traj_crc32;
	entry.first_seq_id = has_existing ? existing->first_seq_id : new_first_seq;
	entry.codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;

	ret = lgps_journal_add(&journal_state, &entry);
	if (ret == 0) {
		journal_state.next_write_offset = write_off + aligned_len;
		ret = persist_journal_locked();
	}

	k_mutex_unlock(&archive_lock);
	return ret;
}

static void compact_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t today_day_id =
		lgps_day_id_from_epoch(compact_req_now_utc_s, compact_req_tz_offset_s);
	uint32_t target_day_id = 0U;
	uint32_t prev_day_id = 0U;
	uint32_t cursor = 1U;
	uint32_t cached_day_id = 0U;
	bool cached_has_entry = false;
	bool cached_lookup_ok = true;
	uint32_t cached_max_seq = 0U;
	uint32_t max_seen_archived_seq = 0U;
	uint32_t min_unarchived_seq = 0U;
	uint8_t scan_batch[16U * LGPS_RECORD_TRAJECTORY_SIZE];
	struct lgps_journal_entry existing;
	bool has_existing = false;
	uint32_t existing_max_seq = 0U;
	uint32_t new_count = 0U;
	uint32_t new_first_seq = 0U;
	uint32_t new_max_seq = 0U;
	int ret = 0;

	/* Pass 1: find oldest eligible day_id with at least one unarchived ring record. */
	for (;;) {
		uint16_t got = 0U;

		if ((lgps_ring_read_from_seq(cursor, scan_batch, 16U, &got) != 0) || (got == 0U)) {
			break;
		}
		for (uint16_t i = 0U; i < got; i++) {
			const uint8_t *r = &scan_batch[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
			uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
			uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP]);
			uint32_t d;

			if (seq >= cursor) {
				cursor = seq + 1U;
			}
			if (ts < COMPACT_MIN_VALID_EPOCH_S) {
				continue;
			}
			d = lgps_day_id_from_epoch(ts, compact_req_tz_offset_s);
			if (d != cached_day_id) {
				cached_lookup_ok = (archive_lookup_day_max_seq(d, &cached_has_entry,
									       NULL,
									       &cached_max_seq) == 0);
				cached_day_id = d;
			}
			if (!cached_lookup_ok) {
				continue;
			}
			if (cached_has_entry && (seq <= cached_max_seq)) {
				if (seq > max_seen_archived_seq) {
					max_seen_archived_seq = seq;
				}
				continue;
			}

			if ((min_unarchived_seq == 0U) || (seq < min_unarchived_seq)) {
				min_unarchived_seq = seq;
			}
			if ((d > today_day_id) ||
			    ((d == today_day_id) && !compact_req_allow_today)) {
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
		/* Self-heal last_archived_seq after reboot so watermark checks don't spin. */
		uint32_t safe_seq = max_seen_archived_seq;

		if ((min_unarchived_seq > 0U) && (safe_seq >= min_unarchived_seq)) {
			safe_seq = min_unarchived_seq - 1U;
		}
		if (safe_seq > last_archived_seq) {
			last_archived_seq = safe_seq;
			(void)lgps_ring_release_sectors_up_to(safe_seq);
		}
		(void)atomic_set(&compact_busy, 0);
		return;
	}

	/* Pass 2: count all new ring records for target_day_id (no 512-rec truncation). */
	memset(&existing, 0, sizeof(existing));
	ret = archive_lookup_day_max_seq(target_day_id, &has_existing, &existing,
					 &existing_max_seq);
	if (ret < 0) {
		LOG_WRN("archive lookup day %u failed (%d)", target_day_id, ret);
		(void)atomic_set(&compact_busy, 0);
		return;
	}

	cursor = has_existing ? (existing_max_seq + 1U) : 1U;
	for (;;) {
		uint16_t got = 0U;

		if ((lgps_ring_read_from_seq(cursor, scan_batch, 16U, &got) != 0) || (got == 0U)) {
			break;
		}
		for (uint16_t i = 0U; i < got; i++) {
			const uint8_t *r = &scan_batch[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
			uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
			uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP]);

			if (seq >= cursor) {
				cursor = seq + 1U;
			}
			if (has_existing && (seq <= existing_max_seq)) {
				continue;
			}
			if (ts < COMPACT_MIN_VALID_EPOCH_S) {
				continue;
			}
			if (lgps_day_id_from_epoch(ts, compact_req_tz_offset_s) != target_day_id) {
				continue;
			}
			if (new_count == 0U) {
				new_first_seq = seq;
			}
			new_max_seq = seq;
			new_count++;
		}
		if (got < 16U) {
			break;
		}
	}

	if (new_count == 0U) {
		(void)atomic_set(&compact_busy, 0);
		return;
	}

	prev_day_id = archive_find_prev_day_id(target_day_id);

	/* Pass 3: write compressed in-RAM fast path (<=256 recs, new past day) or stream RAW. */
	if (!has_existing && (new_count <= COMPACT_COMPRESS_MAX_RECS) &&
	    (target_day_id < today_day_id)) {
		uint16_t kept = 0U;

		cursor = 1U;
		while (kept < new_count) {
			uint16_t got = 0U;

			if ((lgps_ring_read_from_seq(cursor, scan_batch, 16U, &got) != 0) ||
			    (got == 0U)) {
				break;
			}
			for (uint16_t i = 0U; (i < got) && (kept < new_count); i++) {
				const uint8_t *r =
					&scan_batch[(size_t)i * LGPS_RECORD_TRAJECTORY_SIZE];
				uint32_t seq = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_SEQ_ID]);
				uint32_t ts = rec_u32_le(&r[LGPS_RECORD_TRAJECTORY_OFF_TIMESTAMP]);

				if (seq >= cursor) {
					cursor = seq + 1U;
				}
				if ((seq > new_max_seq) || (ts < COMPACT_MIN_VALID_EPOCH_S)) {
					continue;
				}
				if (lgps_day_id_from_epoch(ts, compact_req_tz_offset_s) !=
				    target_day_id) {
					continue;
				}
				memcpy(&compact_raw_buf[(size_t)kept * LGPS_RECORD_TRAJECTORY_SIZE],
				       r, LGPS_RECORD_TRAJECTORY_SIZE);
				kept++;
			}
			if (got < 16U) {
				break;
			}
		}

		if (kept > 0U) {
			size_t raw_len = (size_t)kept * LGPS_RECORD_TRAJECTORY_SIZE;
			size_t pkg_len = 0U;
			uint16_t codec_id = LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK;
			uint32_t crc32 = lgps_crc32_iso_hdlc(compact_raw_buf, raw_len);

			ret = lgps_day_pkg_encode(target_day_id, prev_day_id, codec_id,
						  compact_raw_buf, raw_len, compact_scratch_buf,
						  compact_pkg_buf, sizeof(compact_pkg_buf),
						  &pkg_len);
			if ((ret < 0) ||
			    (pkg_len >= (raw_len + LGPS_PACKAGE_DAY_HEADER_SIZE +
					 LGPS_PACKAGE_DAY_FOOTER_SIZE))) {
				codec_id = LGPS_PACKAGE_DAY_CODEC_RAW;
				ret = lgps_day_pkg_encode(target_day_id, prev_day_id, codec_id,
							  compact_raw_buf, raw_len, NULL,
							  compact_pkg_buf, sizeof(compact_pkg_buf),
							  &pkg_len);
			}
			if (ret == 0) {
				ret = lgps_archive_write_day(target_day_id, kept, crc32,
							     new_first_seq, codec_id,
							     compact_pkg_buf, pkg_len);
				if (ret == 0) {
					LOG_INF("archived day %u (%u recs, %u -> %u B, codec=%u)",
						target_day_id, kept, (unsigned int)raw_len,
						(unsigned int)pkg_len, codec_id);
				} else {
					LOG_WRN("archive write day %u failed (%d)", target_day_id,
						ret);
				}
			} else {
				LOG_WRN("archive encode day %u failed (%d)", target_day_id, ret);
			}
		} else {
			ret = -EAGAIN;
		}
	} else {
		ret = stream_write_day_raw(target_day_id, prev_day_id, has_existing, &existing,
					   existing_max_seq, new_count, new_first_seq, new_max_seq,
					   compact_req_tz_offset_s);
		if (ret == 0) {
			LOG_INF("stream-archived day %u (%u + %u recs, codec=RAW)", target_day_id,
				has_existing ? existing.record_count : 0U, new_count);
		} else {
			LOG_WRN("stream-archive day %u failed (%d)", target_day_id, ret);
		}
	}

	/* Step 4: advance archived watermark and release fully archived past-day sectors. */
	if (ret == 0) {
		if (new_max_seq > last_archived_seq) {
			last_archived_seq = new_max_seq;
		}
		if (target_day_id < today_day_id) {
			(void)lgps_ring_release_sectors_up_to(new_max_seq);
		}
	}

	(void)atomic_set(&compact_busy, 0);
}

void lgps_archive_maybe_compact(uint32_t now_utc_s, int32_t tz_offset_s, uint8_t archive_hour_utc,
				bool idle_or_charging, uint8_t soc_pct)
{
	uint32_t now_ms = k_uptime_get_32();
	uint16_t unarchived;
	bool high_water;

	ARG_UNUSED(archive_hour_utc);

	if (now_utc_s < COMPACT_MIN_VALID_EPOCH_S) {
		return;
	}

	/* Require safe battery (>=10% or unknown/charging) before NOR erases. */
	if (!idle_or_charging && (soc_pct <= 100U) && (soc_pct < 10U)) {
		return;
	}

	unarchived = lgps_ring_count_after_seq(last_archived_seq);
	high_water = (unarchived >= COMPACT_RING_HIGH_WATER_RECS);

	/* Bypass cooldown when the staging ring hits >=75% watermark. */
	if (!high_water && (last_compact_uptime_ms != 0U) &&
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

	compact_req_now_utc_s = now_utc_s;
	compact_req_tz_offset_s = tz_offset_s;
	compact_req_allow_today = high_water;
	last_compact_uptime_ms = now_ms;

	(void)k_work_submit_to_queue(&storage_workq, &compact_work);
}
