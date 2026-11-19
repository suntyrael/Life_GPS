/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - barometric baseline record (external NOR).
 *
 * One value, one sector: sector CONFIG_LGPS_BARO_SECTOR_INDEX (default 17, the
 * one right after the calibration sector 16), 20-byte append-only records with
 * magic / version / CRC-16, like calib_store.c but deliberately a *separate*
 * sector and a separate record type. Sharing the calibration sector would mean
 * that "erase all coefficients" (which erases that whole sector) silently drops
 * the barometric reference as well, and that a full calibration log would erase
 * the baseline.
 *
 * The newest record wins by slot index: the sector is only erased when all 204
 * slots are used, and after that the first slot is the newest again.
 */

#ifndef LIFEGPS_BARO_STORE_H_
#define LIFEGPS_BARO_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#define LGPS_BARO_RECORD_MAGIC 0x5241424CU /* "LBAR" in little endian */
#define LGPS_BARO_RECORD_VERSION 1U
#define LGPS_BARO_RECORD_SIZE 20U
#define LGPS_BARO_SECTOR_SIZE 4096U
#define LGPS_BARO_SLOT_COUNT (LGPS_BARO_SECTOR_SIZE / LGPS_BARO_RECORD_SIZE)

/*
 * Reads the newest valid record. Returns 0 and fills the outputs, -ENOENT when
 * no record was ever written, -EBADMSG when none of them is intact, -ENODEV when
 * the NOR is not ready.
 */
int lgps_baro_store_load(uint32_t *pa_q4, bool *automatic, uint32_t *unix_s);

/* Appends one record, erasing the sector once it is full. */
int lgps_baro_store_save(uint32_t pa_q4, bool automatic, uint32_t unix_s);

#endif /* LIFEGPS_BARO_STORE_H_ */
