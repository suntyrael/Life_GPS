/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - calibration record storage on the external NOR.
 *
 * See lifegps/calib_store.h for why this is a hand-rolled append-only sector and
 * not CONFIG_SETTINGS/NVS.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include <lifegps/calib_store.h>
#include <lifegps/flash_ring.h>

LOG_MODULE_REGISTER(lgps_calib_store, LOG_LEVEL_INF);

/*
 * The ring owns sectors 0..15 (Firmware/docs/PARTITIONS.md). Overlapping them
 * would let a trajectory write silently destroy the coefficients, so this is a
 * build-time failure rather than a runtime surprise.
 */
BUILD_ASSERT(CONFIG_LGPS_CALIB_SECTOR_INDEX >= LGPS_RING_SECTOR_COUNT,
	     "calibration sector must sit after the trajectory ring");
BUILD_ASSERT((LGPS_CALIB_SLOT_COUNT * LGPS_CALIB_RECORD_SIZE) <= LGPS_CALIB_SECTOR_SIZE,
	     "record does not fit the sector");

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(store_lock);

/* Monotonic within a sector erase; the loader advances it past the newest record. */
static uint32_t next_seq = 1U;

static off_t sector_offset(void)
{
	return (off_t)CONFIG_LGPS_CALIB_SECTOR_INDEX * (off_t)LGPS_CALIB_SECTOR_SIZE;
}

static off_t slot_offset(uint32_t slot)
{
	return sector_offset() + (off_t)(slot * LGPS_CALIB_RECORD_SIZE);
}

static bool slot_is_erased(const uint8_t record[LGPS_CALIB_RECORD_SIZE])
{
	/* An erased NOR byte is 0xFF; the magic is the first field, so four bytes
	 * decide whether anything was ever programmed here.
	 */
	return (record[0] == 0xFFU) && (record[1] == 0xFFU) && (record[2] == 0xFFU) &&
	       (record[3] == 0xFFU);
}

int lgps_calib_store_load(struct lgps_calib_coeffs *coeffs, uint8_t *flags, uint32_t *seq,
			  uint32_t *unix_s)
{
	uint8_t record[LGPS_CALIB_RECORD_SIZE];
	struct lgps_calib_coeffs candidate;
	uint32_t index;
	uint32_t best_seq = 0U;
	bool found = false;
	int ret = 0;

	if ((coeffs == NULL) || (flags == NULL) || (seq == NULL) || (unix_s == NULL)) {
		return -EINVAL;
	}

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);

	for (index = 0U; index < LGPS_CALIB_SLOT_COUNT; index++) {
		uint8_t record_flags = 0U;
		uint32_t record_seq = 0U;
		uint32_t record_unix = 0U;
		int read_ret = flash_read(nor, slot_offset(index), record, sizeof(record));

		if (read_ret < 0) {
			LOG_ERR("slot %u read failed (%d)", (unsigned int)index, read_ret);
			ret = read_ret;
			goto out;
		}

		if (slot_is_erased(record)) {
			continue;
		}

		if (lgps_calib_record_decode(record, &candidate, &record_flags, &record_seq,
					     &record_unix) != 0) {
			/* A half-written or corrupted slot is not an error: the append-only
			 * scheme means the previous record is still there.
			 */
			continue;
		}

		if (record_seq >= next_seq) {
			next_seq = record_seq + 1U;
		}

		if (!found || (record_seq > best_seq)) {
			best_seq = record_seq;
			*coeffs = candidate;
			*flags = record_flags;
			*seq = record_seq;
			*unix_s = record_unix;
			found = true;
		}
	}

	ret = found ? 0 : -ENOENT;

out:
	k_mutex_unlock(&store_lock);

	return ret;
}

int lgps_calib_store_save(const struct lgps_calib_coeffs *coeffs, uint8_t flags,
			  uint32_t unix_s)
{
	uint8_t record[LGPS_CALIB_RECORD_SIZE];
	uint8_t probe[LGPS_CALIB_RECORD_SIZE];
	int32_t slot = -1;
	uint32_t index;
	int ret;

	if (coeffs == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);

	for (index = 0U; index < LGPS_CALIB_SLOT_COUNT; index++) {
		ret = flash_read(nor, slot_offset(index), probe, sizeof(probe));
		if (ret < 0) {
			LOG_ERR("slot %u probe failed (%d)", (unsigned int)index, ret);
			goto out;
		}

		if (slot_is_erased(probe)) {
			slot = (int32_t)index;
			break;
		}
	}

	if (slot < 0) {
		/* All slots used: erase and start over. A power loss inside this window
		 * leaves no valid record, and the next boot falls back to the defaults -
		 * the same state a factory-fresh unit is in.
		 */
		LOG_INF("calibration sector full, erasing and restarting the log");
		ret = flash_erase(nor, sector_offset(), LGPS_CALIB_SECTOR_SIZE);
		if (ret < 0) {
			LOG_ERR("sector erase failed (%d)", ret);
			goto out;
		}

		slot = 0;
	}

	ret = lgps_calib_record_encode(coeffs, flags, next_seq, unix_s, record);
	if (ret < 0) {
		goto out;
	}

	ret = flash_write(nor, slot_offset((uint32_t)slot), record, sizeof(record));
	if (ret < 0) {
		LOG_ERR("slot %d write failed (%d)", (int)slot, ret);
		goto out;
	}

	next_seq++;
	LOG_INF("calibration record saved in slot %d (seq %u)", (int)slot,
		(unsigned int)(next_seq - 1U));

out:
	k_mutex_unlock(&store_lock);

	return ret;
}

int lgps_calib_store_erase(void)
{
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);
	ret = flash_erase(nor, sector_offset(), LGPS_CALIB_SECTOR_SIZE);
	k_mutex_unlock(&store_lock);

	if (ret < 0) {
		LOG_ERR("sector erase failed (%d)", ret);
		return ret;
	}

	next_seq = 1U;
	LOG_INF("calibration record erased, defaults in effect");

	return 0;
}
