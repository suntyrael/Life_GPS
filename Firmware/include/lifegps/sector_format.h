/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - staging-ring sector format (pure, driver free).
 *
 * Split out of flash_ring.c so the two rules that were wrong on the bench can be
 * unit tested on native_sim without a flash driver:
 *
 *   1. the header CRC must NOT cover commit_flags. Sealing a sector has to stay a
 *      pure 1 -> 0 bit update, because NOR programming can only clear bits. When
 *      the CRC covered the flags, sealing had to rewrite the CRC in place; every
 *      bit that needed 0 -> 1 stayed wrong, the sector failed its CRC check on the
 *      next boot and the ring was silently reformatted.
 *   2. the write cursor of a sector is recovered from the media (first fully
 *      erased 32-byte slot), not from a RAM counter that resets to 0 on reboot.
 *
 * Nothing here calls the flash API, keeps state or needs a kernel.
 */

#ifndef LIFEGPS_SECTOR_FORMAT_H_
#define LIFEGPS_SECTOR_FORMAT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#include <lifegps/crc.h>
#include <lifegps/flash_ring.h>

/* Sector header, exact layout documented in lifegps/flash_ring.h. */
struct lgps_sector_header {
	uint32_t magic;
	uint32_t sector_seq;
	uint32_t erase_count;
	uint16_t commit_flags;
	uint16_t header_crc16;
} __packed;

_Static_assert(sizeof(struct lgps_sector_header) == LGPS_RING_HEADER_SIZE,
	       "sector header must be exactly 16 bytes");
_Static_assert(offsetof(struct lgps_sector_header, commit_flags) == 12U,
	       "commit_flags must sit outside the CRC coverage");

/* CRC-16/CCITT-FALSE over magic/sector_seq/erase_count, i.e. bytes 0..11. */
static inline uint16_t lgps_sector_header_crc(const struct lgps_sector_header *header)
{
	return lgps_crc16_ccitt_false((const uint8_t *)header,
				      offsetof(struct lgps_sector_header, commit_flags));
}

/* Usable header: magic matches and the stored CRC verifies. */
static inline bool lgps_sector_header_valid(const struct lgps_sector_header *header)
{
	return (header->magic == LGPS_RING_HEADER_MAGIC) &&
	       (header->header_crc16 == lgps_sector_header_crc(header));
}

/* Publish a fresh "being written" header on a freshly erased sector. */
static inline void lgps_sector_header_init(struct lgps_sector_header *header, uint32_t sector_seq,
					   uint32_t erase_count)
{
	header->magic = LGPS_RING_HEADER_MAGIC;
	header->sector_seq = sector_seq;
	header->erase_count = erase_count;
	header->commit_flags = LGPS_RING_FLAGS_WRITING;
	header->header_crc16 = 0U;
	header->header_crc16 = lgps_sector_header_crc(header);
}

/*
 * Archive a sector. commit_flags is outside the CRC, so this clears bits only
 * (0xFFFF -> 0x55AA) and the CRC written at lgps_sector_header_init() still holds.
 */
static inline void lgps_sector_header_seal(struct lgps_sector_header *header)
{
	header->commit_flags = LGPS_RING_FLAGS_ARCHIVED;
}

/* An erased NOR slot reads back as all ones. */
static inline bool lgps_sector_slot_erased(const uint8_t *slot)
{
	for (size_t i = 0U; i < LGPS_RING_RECORD_SIZE; i++) {
		if (slot[i] != 0xFFU) {
			return false;
		}
	}

	return true;
}

/*
 * Records stored in a sector image (the record area, i.e. what follows the header).
 *
 * Records are written strictly in order from the first slot, so the first erased
 * slot is where the next record belongs. A serialised record always carries its
 * CRC-8 byte and can therefore never be all-0xFF. Returns a value in
 * 0..LGPS_RING_RECORDS_PER_SECTOR.
 */
static inline uint16_t lgps_sector_used_records(const uint8_t *image, size_t len_bytes)
{
	uint16_t used = 0U;
	size_t slot_offset;

	for (slot_offset = 0U; (slot_offset + LGPS_RING_RECORD_SIZE) <= len_bytes;
	     slot_offset += LGPS_RING_RECORD_SIZE) {
		if (lgps_sector_slot_erased(&image[slot_offset])) {
			break;
		}

		used++;
	}

	return used;
}

#endif /* LIFEGPS_SECTOR_FORMAT_H_ */
