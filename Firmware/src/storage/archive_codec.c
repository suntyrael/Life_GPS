/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - Byte-Plane Delta, Heatshrink LZSS (W=8, L=4), LGD1 day
 * package codec, and SFDP-bounded journal index.
 *
 * Zero dynamic allocation; bit-exact with tools/protocol-gen/schema.py.
 */

#include <errno.h>
#include <string.h>

#include <lifegps/archive.h>
#include <lifegps/crc.h>

/* --- little-endian helpers ------------------------------------------------- */

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

/* --- Byte-plane delta ------------------------------------------------------ */

int lgps_delta_encode(const uint8_t *raw, size_t len, size_t stride, uint8_t *out)
{
	if ((raw == NULL) || (out == NULL) || (stride == 0U) || ((len % stride) != 0U)) {
		return -EINVAL;
	}
	if (len == 0U) {
		return 0;
	}

	memcpy(out, raw, stride);
	for (size_t off = stride; off < len; off += stride) {
		size_t prev = off - stride;

		for (size_t i = 0U; i < stride; i++) {
			out[off + i] = (uint8_t)(raw[off + i] - raw[prev + i]);
		}
	}

	return 0;
}

int lgps_delta_decode(const uint8_t *encoded, size_t len, size_t stride, uint8_t *out)
{
	if ((encoded == NULL) || (out == NULL) || (stride == 0U) || ((len % stride) != 0U)) {
		return -EINVAL;
	}
	if (len == 0U) {
		return 0;
	}

	memcpy(out, encoded, stride);
	for (size_t off = stride; off < len; off += stride) {
		size_t prev = off - stride;

		for (size_t i = 0U; i < stride; i++) {
			out[off + i] = (uint8_t)(out[prev + i] + encoded[off + i]);
		}
	}

	return 0;
}

/* --- Heatshrink LZSS (W=8, L=4, MSB-first) --------------------------------- */

struct bit_writer {
	uint8_t *buf;
	size_t cap;
	size_t pos;
	uint8_t cur;
	uint8_t bits;
	bool overflow;
};

static void bw_push(struct bit_writer *bw, uint32_t value, uint8_t count)
{
	for (int shift = (int)count - 1; shift >= 0; shift--) {
		uint8_t bit = (uint8_t)((value >> shift) & 1U);

		bw->cur = (uint8_t)((bw->cur << 1) | bit);
		bw->bits++;
		if (bw->bits == 8U) {
			if (bw->pos >= bw->cap) {
				bw->overflow = true;
				return;
			}
			bw->buf[bw->pos++] = bw->cur;
			bw->cur = 0U;
			bw->bits = 0U;
		}
	}
}

static void bw_finish(struct bit_writer *bw)
{
	if (bw->overflow) {
		return;
	}
	if (bw->bits > 0U) {
		if (bw->pos >= bw->cap) {
			bw->overflow = true;
			return;
		}
		bw->buf[bw->pos++] = (uint8_t)(bw->cur << (8U - bw->bits));
		bw->cur = 0U;
		bw->bits = 0U;
	}
}

struct bit_reader {
	const uint8_t *buf;
	size_t total_bits;
	size_t bit_pos;
};

static bool br_pull(struct bit_reader *br, uint8_t count, uint32_t *out)
{
	uint32_t val = 0U;

	if ((br->total_bits - br->bit_pos) < count) {
		return false;
	}

	for (uint8_t i = 0U; i < count; i++) {
		size_t byte_idx = br->bit_pos >> 3;
		uint8_t bit_idx = (uint8_t)(7U - (br->bit_pos & 7U));

		val = (val << 1) | ((br->buf[byte_idx] >> bit_idx) & 1U);
		br->bit_pos++;
	}

	*out = val;
	return true;
}

int lgps_heatshrink_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
			   size_t *out_len)
{
	const uint8_t w_sz2 = LGPS_PACKAGE_DAY_HS_WINDOW_SZ2;
	const uint8_t l_sz2 = LGPS_PACKAGE_DAY_HS_LOOKAHEAD_SZ2;
	const size_t window_size = 1U << w_sz2;
	const size_t max_match = 1U << l_sz2;
	const size_t min_match = ((1U + w_sz2 + l_sz2) / 9U) + 1U;
	struct bit_writer bw = { .buf = out, .cap = out_cap };
	size_t pos = 0U;

	if ((out == NULL) || (out_len == NULL) || ((in_len > 0U) && (in == NULL))) {
		return -EINVAL;
	}

	while (pos < in_len) {
		size_t best_len = 0U;
		size_t best_dist = 0U;
		size_t start = (pos > window_size) ? (pos - window_size) : 0U;
		size_t limit = ((in_len - pos) < max_match) ? (in_len - pos) : max_match;

		for (size_t cand = pos; cand > start;) {
			cand--;
			if (in[cand] != in[pos]) {
				continue;
			}
			size_t match_len = 1U;

			while ((match_len < limit) && (in[cand + match_len] == in[pos + match_len])) {
				match_len++;
			}
			if (match_len > best_len) {
				best_len = match_len;
				best_dist = pos - cand;
				if (best_len == limit) {
					break;
				}
			}
		}

		if (best_len >= min_match) {
			bw_push(&bw, 0U, 1U);
			bw_push(&bw, (uint32_t)(best_dist - 1U), w_sz2);
			bw_push(&bw, (uint32_t)(best_len - 1U), l_sz2);
			pos += best_len;
		} else {
			bw_push(&bw, 1U, 1U);
			bw_push(&bw, in[pos], 8U);
			pos++;
		}

		if (bw.overflow) {
			return -ENOMEM;
		}
	}

	bw_finish(&bw);
	if (bw.overflow) {
		return -ENOMEM;
	}

	*out_len = bw.pos;
	return 0;
}

int lgps_heatshrink_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t expected_len)
{
	const uint8_t w_sz2 = LGPS_PACKAGE_DAY_HS_WINDOW_SZ2;
	const uint8_t l_sz2 = LGPS_PACKAGE_DAY_HS_LOOKAHEAD_SZ2;
	struct bit_reader br = { .buf = in, .total_bits = in_len * 8U, .bit_pos = 0U };
	size_t pos = 0U;

	if ((expected_len > 0U) && ((in == NULL) || (out == NULL))) {
		return -EINVAL;
	}

	while (pos < expected_len) {
		uint32_t flag = 0U;

		if (!br_pull(&br, 1U, &flag)) {
			return -EBADMSG;
		}

		if (flag == 1U) {
			uint32_t lit = 0U;

			if (!br_pull(&br, 8U, &lit)) {
				return -EBADMSG;
			}
			out[pos++] = (uint8_t)lit;
		} else {
			uint32_t dist_m1 = 0U;
			uint32_t len_m1 = 0U;
			size_t dist;
			size_t match_len;

			if (!br_pull(&br, w_sz2, &dist_m1) || !br_pull(&br, l_sz2, &len_m1)) {
				return -EBADMSG;
			}
			dist = (size_t)dist_m1 + 1U;
			match_len = (size_t)len_m1 + 1U;
			if (dist > pos) {
				return -EBADMSG;
			}
			for (size_t k = 0U; (k < match_len) && (pos < expected_len); k++) {
				out[pos] = out[pos - dist];
				pos++;
			}
		}
	}

	return 0;
}

/* --- LGD1 day package codec ------------------------------------------------ */

int lgps_day_pkg_parse_header(const uint8_t *buf, size_t len, struct lgps_day_header *out)
{
	uint16_t stored_crc;
	uint16_t computed_crc;

	if ((buf == NULL) || (out == NULL) || (len < LGPS_PACKAGE_DAY_HEADER_SIZE)) {
		return -EINVAL;
	}

	if ((buf[0] != 'L') || (buf[1] != 'G') || (buf[2] != 'D') || (buf[3] != '1')) {
		return -EBADMSG;
	}

	stored_crc = get_u16(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16]);
	computed_crc = lgps_crc16_ccitt_false(buf, LGPS_PACKAGE_DAY_HEADER_CRC16_LEN);
	if (stored_crc != computed_crc) {
		return -EBADMSG;
	}

	out->header_ver = get_u16(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_VER]);
	if (out->header_ver != LGPS_PACKAGE_DAY_HEADER_VER) {
		return -ENOTSUP;
	}

	out->codec_id = get_u16(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_CODEC]);
	out->day_id = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_DAY_ID]);
	out->record_count = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_RECORD_COUNT]);
	out->reserved0 = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED0]);
	out->traj_raw_len = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_RAW_LEN]);
	out->traj_packed_len = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_PACKED_LEN]);
	out->reserved1 = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED1]);
	out->reserved2 = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED2]);
	out->prev_day_id = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_PREV_DAY_ID]);
	out->reserved3 = get_u32(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_RESERVED3]);
	out->flags = get_u16(&buf[LGPS_PACKAGE_DAY_HEADER_OFF_FLAGS]);
	out->header_crc16 = stored_crc;

	return 0;
}

int lgps_day_pkg_encode(uint32_t day_id, uint32_t prev_day_id, uint16_t codec_id,
			const uint8_t *raw_records, size_t raw_len, uint8_t *scratch,
			uint8_t *out, size_t out_cap, size_t *out_len)
{
	size_t packed_len = 0U;
	size_t total_len;
	uint32_t crc32;
	uint16_t hdr_crc;
	uint8_t *payload_dst;
	size_t payload_cap;

	if ((out == NULL) || (out_len == NULL) || ((raw_len % LGPS_RECORD_TRAJECTORY_SIZE) != 0U)) {
		return -EINVAL;
	}
	if ((raw_len > 0U) && (raw_records == NULL)) {
		return -EINVAL;
	}
	if (out_cap < (size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + LGPS_PACKAGE_DAY_FOOTER_SIZE) {
		return -ENOMEM;
	}

	payload_dst = &out[LGPS_PACKAGE_DAY_HEADER_SIZE];
	payload_cap = out_cap - LGPS_PACKAGE_DAY_HEADER_SIZE - LGPS_PACKAGE_DAY_FOOTER_SIZE;

	if (codec_id == LGPS_PACKAGE_DAY_CODEC_RAW) {
		if (raw_len > payload_cap) {
			return -ENOMEM;
		}
		if (raw_len > 0U) {
			memcpy(payload_dst, raw_records, raw_len);
		}
		packed_len = raw_len;
	} else if (codec_id == LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK) {
		int ret;

		if ((raw_len > 0U) && (scratch == NULL)) {
			return -EINVAL;
		}
		ret = lgps_delta_encode(raw_records, raw_len, LGPS_RECORD_TRAJECTORY_SIZE, scratch);
		if (ret < 0) {
			return ret;
		}
		ret = lgps_heatshrink_encode(scratch, raw_len, payload_dst, payload_cap,
					     &packed_len);
		if (ret < 0) {
			return ret;
		}
	} else {
		return -ENOTSUP;
	}

	memset(out, 0, LGPS_PACKAGE_DAY_HEADER_SIZE);
	out[0] = 'L';
	out[1] = 'G';
	out[2] = 'D';
	out[3] = '1';
	put_u16(&out[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_VER], LGPS_PACKAGE_DAY_HEADER_VER);
	put_u16(&out[LGPS_PACKAGE_DAY_HEADER_OFF_CODEC], codec_id);
	put_u32(&out[LGPS_PACKAGE_DAY_HEADER_OFF_DAY_ID], day_id);
	put_u32(&out[LGPS_PACKAGE_DAY_HEADER_OFF_RECORD_COUNT],
		(uint32_t)(raw_len / LGPS_RECORD_TRAJECTORY_SIZE));
	put_u32(&out[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_RAW_LEN], (uint32_t)raw_len);
	put_u32(&out[LGPS_PACKAGE_DAY_HEADER_OFF_TRAJ_PACKED_LEN], (uint32_t)packed_len);
	put_u32(&out[LGPS_PACKAGE_DAY_HEADER_OFF_PREV_DAY_ID], prev_day_id);
	hdr_crc = lgps_crc16_ccitt_false(out, LGPS_PACKAGE_DAY_HEADER_CRC16_LEN);
	put_u16(&out[LGPS_PACKAGE_DAY_HEADER_OFF_HEADER_CRC16], hdr_crc);

	crc32 = lgps_crc32_iso_hdlc(raw_records, raw_len);
	total_len = (size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + packed_len;
	put_u32(&out[total_len + LGPS_PACKAGE_DAY_FOOTER_OFF_TRAJ_CRC32], crc32);
	put_u32(&out[total_len + LGPS_PACKAGE_DAY_FOOTER_OFF_RESERVED], 0U);

	*out_len = total_len + LGPS_PACKAGE_DAY_FOOTER_SIZE;
	return 0;
}

int lgps_day_pkg_decode(const uint8_t *pkg, size_t pkg_len, uint8_t *scratch,
			struct lgps_day_header *header_out, uint8_t *raw_out, size_t raw_cap,
			size_t *raw_len_out)
{
	struct lgps_day_header hdr;
	size_t expected_pkg_len;
	const uint8_t *packed;
	uint32_t stored_crc32;
	uint32_t computed_crc32;
	int ret;

	if ((pkg == NULL) || (raw_out == NULL) || (raw_len_out == NULL)) {
		return -EINVAL;
	}

	ret = lgps_day_pkg_parse_header(pkg, pkg_len, &hdr);
	if (ret < 0) {
		return ret;
	}

	expected_pkg_len = (size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + (size_t)hdr.traj_packed_len +
			   LGPS_PACKAGE_DAY_FOOTER_SIZE;
	if (pkg_len != expected_pkg_len) {
		return -EBADMSG;
	}
	if ((hdr.traj_raw_len != (hdr.record_count * LGPS_RECORD_TRAJECTORY_SIZE)) ||
	    (hdr.traj_raw_len > raw_cap)) {
		return -ENOMEM;
	}

	packed = &pkg[LGPS_PACKAGE_DAY_HEADER_SIZE];

	if (hdr.codec_id == LGPS_PACKAGE_DAY_CODEC_RAW) {
		if (hdr.traj_packed_len != hdr.traj_raw_len) {
			return -EBADMSG;
		}
		if (hdr.traj_raw_len > 0U) {
			memcpy(raw_out, packed, hdr.traj_raw_len);
		}
	} else if (hdr.codec_id == LGPS_PACKAGE_DAY_CODEC_BYTE_DELTA_HEATSHRINK) {
		if ((hdr.traj_raw_len > 0U) && (scratch == NULL)) {
			return -EINVAL;
		}
		ret = lgps_heatshrink_decode(packed, hdr.traj_packed_len, scratch,
					     hdr.traj_raw_len);
		if (ret < 0) {
			return ret;
		}
		ret = lgps_delta_decode(scratch, hdr.traj_raw_len, LGPS_RECORD_TRAJECTORY_SIZE,
					raw_out);
		if (ret < 0) {
			return ret;
		}
	} else {
		return -ENOTSUP;
	}

	stored_crc32 = get_u32(&pkg[(size_t)LGPS_PACKAGE_DAY_HEADER_SIZE + hdr.traj_packed_len +
				    LGPS_PACKAGE_DAY_FOOTER_OFF_TRAJ_CRC32]);
	computed_crc32 = lgps_crc32_iso_hdlc(raw_out, hdr.traj_raw_len);
	if (stored_crc32 != computed_crc32) {
		return -EBADMSG;
	}

	if (header_out != NULL) {
		*header_out = hdr;
	}
	*raw_len_out = hdr.traj_raw_len;
	return 0;
}

/* --- Pure-C SFDP-bounded Journal Index ------------------------------------- */

void lgps_journal_init(struct lgps_journal *j, uint32_t flash_limit_bytes)
{
	if (j == NULL) {
		return;
	}

	memset(j, 0, sizeof(*j));
	j->flash_limit_bytes = flash_limit_bytes;
	j->next_write_offset = LGPS_ARCHIVE_DATA_OFFSET;
}

int lgps_journal_add(struct lgps_journal *j, const struct lgps_journal_entry *entry)
{
	uint32_t aligned_len;

	if ((j == NULL) || (entry == NULL) || (entry->day_id == 0U)) {
		return -EINVAL;
	}
	if (j->count >= LGPS_JOURNAL_MAX_DAYS) {
		return -ENOSPC;
	}

	aligned_len = (entry->package_len + 4095U) & ~4095U;
	if ((entry->flash_offset < LGPS_ARCHIVE_DATA_OFFSET) ||
	    (entry->flash_offset > j->flash_limit_bytes) ||
	    (aligned_len > (j->flash_limit_bytes - entry->flash_offset))) {
		return -ENOSPC;
	}

	/* Replace existing day_id if re-archived, else append. */
	for (uint16_t i = 0U; i < j->count; i++) {
		if (j->entries[i].day_id == entry->day_id) {
			j->entries[i] = *entry;
			j->index_version++;
			return 0;
		}
	}

	j->entries[j->count++] = *entry;
	if ((entry->flash_offset + aligned_len) > j->next_write_offset) {
		j->next_write_offset = entry->flash_offset + aligned_len;
	}
	j->index_version++;
	return 0;
}

const struct lgps_journal_entry *lgps_journal_find(const struct lgps_journal *j, uint32_t day_id)
{
	if (j == NULL) {
		return NULL;
	}

	for (uint16_t i = 0U; i < j->count; i++) {
		if (j->entries[i].day_id == day_id) {
			return &j->entries[i];
		}
	}

	return NULL;
}

int lgps_journal_remove(struct lgps_journal *j, uint32_t day_id)
{
	if (j == NULL) {
		return -EINVAL;
	}

	for (uint16_t i = 0U; i < j->count; i++) {
		if (j->entries[i].day_id == day_id) {
			for (uint16_t k = i; (k + 1U) < j->count; k++) {
				j->entries[k] = j->entries[k + 1U];
			}
			j->count--;
			j->index_version++;
			return 0;
		}
	}

	return -ENOENT;
}

void lgps_journal_entry_encode(const struct lgps_journal_entry *in,
			       uint8_t out[LGPS_JOURNAL_ENTRY_SIZE])
{
	if ((in == NULL) || (out == NULL)) {
		return;
	}

	put_u32(&out[0], in->day_id);
	put_u32(&out[4], in->flash_offset);
	put_u32(&out[8], in->package_len);
	put_u32(&out[12], in->record_count);
	put_u32(&out[16], in->traj_crc32);
	put_u32(&out[20], in->first_seq_id);
	put_u16(&out[24], in->codec_id);
	put_u16(&out[26], in->flags);
}

void lgps_journal_entry_decode(const uint8_t in[LGPS_JOURNAL_ENTRY_SIZE],
			       struct lgps_journal_entry *out)
{
	if ((in == NULL) || (out == NULL)) {
		return;
	}

	out->day_id = get_u32(&in[0]);
	out->flash_offset = get_u32(&in[4]);
	out->package_len = get_u32(&in[8]);
	out->record_count = get_u32(&in[12]);
	out->traj_crc32 = get_u32(&in[16]);
	out->first_seq_id = get_u32(&in[20]);
	out->codec_id = get_u16(&in[24]);
	out->flags = get_u16(&in[26]);
}
