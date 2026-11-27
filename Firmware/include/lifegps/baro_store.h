/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - barometric reference record (external NOR).
 *
 * One reference, one sector: sector CONFIG_LGPS_BARO_SECTOR_INDEX (default 17,
 * the one right after the calibration sector 16), append-only 20-byte records
 * with magic / version / CRC-16, like calib_store.c but deliberately a *separate*
 * sector and a separate record type. Sharing the calibration sector would mean
 * that "erase all coefficients" (which erases that whole sector) silently drops
 * the barometric reference as well, and that a full calibration log would erase
 * the reference.
 *
 * The newest record wins by slot index: the sector is only erased when all 204
 * slots are used, and after that the first slot is the newest again.
 *
 * Record layout, version 2 (the format the current firmware writes):
 *
 *   0  u32 magic "LBAR"
 *   4  u8  version = 2
 *   5  u8  flags   bit0 automatic, bit1 sea-level (MSL) reference
 *   6  u32 reference_pa_q4   Pa x 16
 *   10 i32 reference_alt_mm  hMSL the reference was calibrated against (MSL only)
 *   14 u32 unix_s            UTC when it was set, 0 when unknown
 *   18 u16 crc16             over bytes 0..17
 *
 * Version 1 used the same 20 bytes with `u16 version` at 4, `u16 flags` at 6,
 * `u32 reference_pa_q4` at 8, `u32 unix_s` at 12 and its CRC at 16. It is still
 * readable (a LOCAL reference, no height) so a unit that ran the first build
 * keeps its relative baseline instead of starting from nothing. Byte 4 tells the
 * two apart: 1 for the old layout, 2 for this one.
 */

#ifndef LIFEGPS_BARO_STORE_H_
#define LIFEGPS_BARO_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/baro.h>

#define LGPS_BARO_RECORD_MAGIC 0x5241424CU /* "LBAR" in little endian */
#define LGPS_BARO_RECORD_VERSION 2U
#define LGPS_BARO_RECORD_SIZE 20U
#define LGPS_BARO_SECTOR_SIZE 4096U
#define LGPS_BARO_SLOT_COUNT (LGPS_BARO_SECTOR_SIZE / LGPS_BARO_RECORD_SIZE)

/*
 * Reads the newest valid record. Returns 0 and fills the outputs, -ENOENT when
 * no record was ever written, -EBADMSG when none of them is intact, -ENODEV when
 * the NOR is not ready.
 */
int lgps_baro_store_load(uint32_t *pa_q4, bool *automatic, enum lgps_baro_reference *kind,
			 int32_t *ref_alt_mm, uint32_t *unix_s);

/* Appends one record, erasing the sector once it is full. */
int lgps_baro_store_save(uint32_t pa_q4, bool automatic, enum lgps_baro_reference kind,
			 int32_t ref_alt_mm, uint32_t unix_s);

/* Erase the barometric baseline sector (used by FACTORY_RESET 0x51). */
int lgps_baro_store_erase(void);

#endif /* LIFEGPS_BARO_STORE_H_ */
