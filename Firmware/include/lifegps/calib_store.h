/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - calibration record storage (external NOR).
 *
 * Layout, and why it is here instead of CONFIG_SETTINGS/NVS:
 *
 * The record is 80 bytes and lives in one dedicated 4 KB sector of the W25Q512,
 * immediately after the ring's 16 sectors (offset 0x10000 = sector 16, see
 * Firmware/docs/PARTITIONS.md). It is append-only: each save programs the next
 * erased slot, so a save never erases the sector that holds the current value
 * until all 51 slots are used. A power loss during an append leaves the previous
 * record intact; the only unprotected window is the sector erase that happens
 * once every 51 saves, and the next boot then simply falls back to the defaults.
 *
 * CONFIG_SETTINGS + CONFIG_SETTINGS_NVS was the alternative. It needs a
 * `settings_partition` in the board devicetree, and the internal RRAM partition
 * table in Firmware/docs/PARTITIONS.md still documents the nRF52840-era layout
 * that it says must be recomputed before use. Spending that risk on 80 bytes was
 * not worth it; the interface below keeps the migration a backend swap.
 *
 * The unit test (Firmware/tests/lifegps.calib) provides its own stub of these
 * three functions, which is why the maths module never includes a driver header.
 */

#ifndef LIFEGPS_CALIB_STORE_H_
#define LIFEGPS_CALIB_STORE_H_

#include <stdint.h>

#include <lifegps/calib.h>

/* Sector holding the record, and how many records fit in it. */
#define LGPS_CALIB_SECTOR_SIZE 4096U
#define LGPS_CALIB_SLOT_COUNT (LGPS_CALIB_SECTOR_SIZE / LGPS_CALIB_RECORD_SIZE)

/*
 * Finds the newest valid record and copies it into @p coeffs.
 * Returns 0 on success and a negative errno when no usable record exists
 * (-ENOENT, -EBADMSG, -ENODEV); the caller keeps the compiled-in defaults then.
 */
int lgps_calib_store_load(struct lgps_calib_coeffs *coeffs, uint8_t *flags, uint32_t *seq,
			  uint32_t *unix_s);

/* Appends one record, erasing the sector when it is full. */
int lgps_calib_store_save(const struct lgps_calib_coeffs *coeffs, uint8_t flags,
			  uint32_t unix_s);

/* Erases the whole sector (used when every coefficient is back to default). */
int lgps_calib_store_erase(void);

#endif /* LIFEGPS_CALIB_STORE_H_ */
