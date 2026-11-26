/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - plaintext LGD1 day archive codec (Byte-Delta + Heatshrink)
 * and SFDP-compatible 32 MB / 64 MB external NOR journal index.
 *
 * Pure C, zero dynamic allocation (all caller-provided or static buffers),
 * bit-exact with tools/protocol-gen/schema.py and Kotlin DayPackageCodec.
 */

#ifndef LIFEGPS_ARCHIVE_H_
#define LIFEGPS_ARCHIVE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <generated/lifegps_protocol.h>

/*
 * External NOR layout (SFDP-dynamic for BOTH 32 MB and 64 MB fitted parts):
 *   Sectors 0..15   (0x00000..0x0FFFF): staging ring (64 KB)
 *   Sector  16      (0x10000..0x10FFF): calibration store (4 KB)
 *   Sector  17      (0x11000..0x11FFF): baro baseline store (4 KB)
 *   Sectors 18..19  (0x12000..0x13FFF): device config TLV store (8 KB)
 *   Sectors 20..21  (0x14000..0x15FFF): journal index ping-pong (8 KB, up to 128 days)
 *   Offset  0x16000 .. (traj_bytes - 1): archived LGD1 day packages (scales to 32M or 64M)
 */
#define LGPS_ARCHIVE_CONFIG_SECTOR_START 18U
#define LGPS_ARCHIVE_JOURNAL_SECTOR_A 20U
#define LGPS_ARCHIVE_JOURNAL_SECTOR_B 21U
#define LGPS_ARCHIVE_DATA_OFFSET (22U * 4096U)

#define LGPS_JOURNAL_MAX_DAYS 128U
#define LGPS_JOURNAL_ENTRY_SIZE 28U
#define LGPS_JOURNAL_MAGIC 0x4A4E4C31U /* "JNL1" */

struct lgps_day_header {
	uint16_t header_ver;
	uint16_t codec_id;
	uint32_t day_id; /* YYYYMMDD */
	uint32_t record_count;
	uint32_t reserved0;
	uint32_t traj_raw_len;
	uint32_t traj_packed_len;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t prev_day_id;
	uint32_t reserved3;
	uint16_t flags;
	uint16_t header_crc16;
};

/* One archived day entry in the journal index (28 bytes serialized). */
struct lgps_journal_entry {
	uint32_t day_id;        /* YYYYMMDD */
	uint32_t flash_offset;  /* byte offset inside external NOR >= LGPS_ARCHIVE_DATA_OFFSET */
	uint32_t package_len;   /* total LGD1 package length (48 + packed + 8) */
	uint32_t record_count;
	uint32_t traj_crc32;    /* CRC-32 of uncompressed trajectory records */
	uint32_t first_seq_id;
	uint16_t codec_id;
	uint16_t flags;
};

struct lgps_journal {
	uint32_t index_version;
	uint32_t flash_limit_bytes; /* SFDP traj_bytes bound (works for 32 MB & 64 MB) */
	uint32_t next_write_offset;
	uint16_t count;
	struct lgps_journal_entry entries[LGPS_JOURNAL_MAX_DAYS];
};

/* --- Byte-plane delta codec (32-byte record stride) ------------------------ */

int lgps_delta_encode(const uint8_t *raw, size_t len, size_t stride, uint8_t *out);
int lgps_delta_decode(const uint8_t *encoded, size_t len, size_t stride, uint8_t *out);

/* --- Heatshrink LZSS codec (W=8, L=4, MSB-first bitstream) ----------------- */

int lgps_heatshrink_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
			   size_t *out_len);

int lgps_heatshrink_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t expected_len);

/* --- LGD1 day package codec (plaintext) ------------------------------------ */

int lgps_day_pkg_parse_header(const uint8_t *buf, size_t len, struct lgps_day_header *out);

/*
 * Encode @p raw_records (multiple of 32 B) into a complete LGD1 package.
 * @p scratch must be at least @p raw_len bytes when codec_id == BYTE_DELTA_HEATSHRINK.
 */
int lgps_day_pkg_encode(uint32_t day_id, uint32_t prev_day_id, uint16_t codec_id,
			const uint8_t *raw_records, size_t raw_len, uint8_t *scratch,
			uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Decode & validate a complete LGD1 package (header CRC-16 + payload CRC-32).
 * @p scratch must be at least header.traj_raw_len bytes for compressed packages.
 */
int lgps_day_pkg_decode(const uint8_t *pkg, size_t pkg_len, uint8_t *scratch,
			struct lgps_day_header *header_out, uint8_t *raw_out, size_t raw_cap,
			size_t *raw_len_out);

/* --- Pure-C Journal index helpers (SFDP-bounded for 32 MB & 64 MB) --------- */

void lgps_journal_init(struct lgps_journal *j, uint32_t flash_limit_bytes);

/* Allocate next 4 KB-aligned region within SFDP flash_limit_bytes and append entry. */
int lgps_journal_add(struct lgps_journal *j, const struct lgps_journal_entry *entry);

const struct lgps_journal_entry *lgps_journal_find(const struct lgps_journal *j, uint32_t day_id);

int lgps_journal_remove(struct lgps_journal *j, uint32_t day_id);

/* Serialize one entry into 28 bytes (for GET_DAY_INDEX 0x23 response & flash persistence). */
void lgps_journal_entry_encode(const struct lgps_journal_entry *in, uint8_t out[LGPS_JOURNAL_ENTRY_SIZE]);
void lgps_journal_entry_decode(const uint8_t in[LGPS_JOURNAL_ENTRY_SIZE], struct lgps_journal_entry *out);

/* --- Runtime NOR journal & archive backend (journal_store.c) --------------- */

/* Mount ping-pong journal index from NOR sectors 20..21 bounded by SFDP traj_bytes. */
int lgps_archive_init(void);

/* Snapshot current journal metadata (thread-safe). */
void lgps_archive_get_journal(struct lgps_journal *out);

/* Write a complete encoded LGD1 package to NOR and commit journal entry. */
int lgps_archive_write_day(uint32_t day_id, uint32_t record_count, uint32_t traj_crc32,
			   uint32_t first_seq_id, uint16_t codec_id, const uint8_t *pkg_data,
			   size_t pkg_len);

/* Read a chunk of an archived LGD1 package by day_id + byte offset. */
int lgps_archive_read_chunk(uint32_t day_id, uint32_t offset, uint8_t *out, uint16_t max_len,
			    uint16_t *out_len, uint32_t *total_len_out);

/* Remove a day from the journal index and erase its flash sectors. */
int lgps_archive_erase_day(uint32_t day_id);

/* Erase journal index sectors and reset next_write_offset to LGPS_ARCHIVE_DATA_OFFSET. */
int lgps_archive_factory_reset(void);

/* Storage usage in permille (0..1000) across SFDP traj_bytes (32 MB or 64 MB). */
uint16_t lgps_archive_used_permille(void);

#endif /* LIFEGPS_ARCHIVE_H_ */
