/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - barometric baseline storage on the external NOR.
 *
 * See lifegps/baro_store.h for why this is its own sector and its own record
 * type instead of an extra field in the calibration record.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include <lifegps/baro_store.h>
#include <lifegps/crc.h>
#include <lifegps/flash_ring.h>

LOG_MODULE_REGISTER(lgps_baro_store, LOG_LEVEL_INF);

/*
 * The ring owns sectors 0..15 and the calibration record sector 16. Writing into
 * either would let a trajectory write or a "clear calibration" destroy the
 * barometric reference, so both are build-time failures instead of surprises.
 */
BUILD_ASSERT(CONFIG_LGPS_BARO_SECTOR_INDEX >= LGPS_RING_SECTOR_COUNT,
	     "barometric baseline sector must sit after the trajectory ring");
#if defined(CONFIG_LGPS_CALIB)
BUILD_ASSERT(CONFIG_LGPS_BARO_SECTOR_INDEX != CONFIG_LGPS_CALIB_SECTOR_INDEX,
	     "barometric baseline sector must not be the calibration sector");
#endif
BUILD_ASSERT((LGPS_BARO_SLOT_COUNT * LGPS_BARO_RECORD_SIZE) <= LGPS_BARO_SECTOR_SIZE,
	     "record does not fit the sector");

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(store_lock);

static void put_u16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_u32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
	out[2] = (uint8_t)((value >> 16) & 0xFFU);
	out[3] = (uint8_t)((value >> 24) & 0xFFU);
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

static off_t sector_offset(void)
{
	return (off_t)CONFIG_LGPS_BARO_SECTOR_INDEX * (off_t)LGPS_BARO_SECTOR_SIZE;
}

static off_t slot_offset(uint32_t slot)
{
	return sector_offset() + (off_t)(slot * LGPS_BARO_RECORD_SIZE);
}

static bool slot_is_erased(const uint8_t record[LGPS_BARO_RECORD_SIZE])
{
	return (record[0] == 0xFFU) && (record[1] == 0xFFU);
}

/* magic / version / flags / baseline / unix / CRC-16 over the first 16 bytes. */
static int record_encode(uint32_t pa_q4, bool automatic, uint32_t unix_s,
			 uint8_t out[LGPS_BARO_RECORD_SIZE])
{
	memset(out, 0, LGPS_BARO_RECORD_SIZE);
	put_u32(&out[0], LGPS_BARO_RECORD_MAGIC);
	put_u16(&out[4], (uint16_t)LGPS_BARO_RECORD_VERSION);
	put_u16(&out[6], automatic ? 1U : 0U);
	put_u32(&out[8], pa_q4);
	put_u32(&out[12], unix_s);
	put_u16(&out[16], lgps_crc16_ccitt_false(out, 16U));

	return 0;
}

static int record_decode(const uint8_t in[LGPS_BARO_RECORD_SIZE], uint32_t *pa_q4,
			 bool *automatic, uint32_t *unix_s)
{
	if (get_u32(&in[0]) != LGPS_BARO_RECORD_MAGIC) {
		return -EBADMSG;
	}

	if (get_u16(&in[4]) != (uint16_t)LGPS_BARO_RECORD_VERSION) {
		return -EBADMSG;
	}

	if (get_u16(&in[16]) != lgps_crc16_ccitt_false(in, 16U)) {
		return -EBADMSG;
	}

	*pa_q4 = get_u32(&in[8]);
	*automatic = (get_u16(&in[6]) & 0x01U) != 0U;
	*unix_s = get_u32(&in[12]);

	return 0;
}

int lgps_baro_store_load(uint32_t *pa_q4, bool *automatic, uint32_t *unix_s)
{
	uint8_t record[LGPS_BARO_RECORD_SIZE];
	bool found = false;
	uint32_t index;
	int ret = 0;

	if ((pa_q4 == NULL) || (automatic == NULL) || (unix_s == NULL)) {
		return -EINVAL;
	}

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);

	/* Newest wins: the log only ever appends, so a later slot is younger. */
	for (index = 0U; index < LGPS_BARO_SLOT_COUNT; index++) {
		uint32_t slot_pa_q4 = 0U;
		uint32_t slot_unix = 0U;
		bool slot_auto = false;
		int read_ret = flash_read(nor, slot_offset(index), record, sizeof(record));

		if (read_ret < 0) {
			LOG_ERR("slot %u read failed (%d)", (unsigned int)index, read_ret);
			ret = read_ret;
			goto out;
		}

		if (slot_is_erased(record)) {
			continue;
		}

		/* A half-written or corrupted slot is not an error: the previous record
		 * is still intact, which is the point of the append-only layout.
		 */
		if (record_decode(record, &slot_pa_q4, &slot_auto, &slot_unix) != 0) {
			continue;
		}

		*pa_q4 = slot_pa_q4;
		*automatic = slot_auto;
		*unix_s = slot_unix;
		found = true;
	}

	ret = found ? 0 : -ENOENT;

out:
	k_mutex_unlock(&store_lock);

	return ret;
}

int lgps_baro_store_save(uint32_t pa_q4, bool automatic, uint32_t unix_s)
{
	uint8_t record[LGPS_BARO_RECORD_SIZE];
	uint8_t probe[LGPS_BARO_RECORD_SIZE];
	int32_t slot = -1;
	uint32_t index;
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);

	for (index = 0U; index < LGPS_BARO_SLOT_COUNT; index++) {
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
		 * leaves no baseline, and the device falls back to "no baseline yet"
		 * instead of a stale reference.
		 */
		LOG_INF("baseline sector full, erasing and restarting the log");
		ret = flash_erase(nor, sector_offset(), LGPS_BARO_SECTOR_SIZE);
		if (ret < 0) {
			LOG_ERR("sector erase failed (%d)", ret);
			goto out;
		}

		slot = 0;
	}

	ret = record_encode(pa_q4, automatic, unix_s, record);
	if (ret < 0) {
		goto out;
	}

	ret = flash_write(nor, slot_offset((uint32_t)slot), record, sizeof(record));
	if (ret < 0) {
		LOG_ERR("slot %d write failed (%d)", (int)slot, ret);
		goto out;
	}

	LOG_INF("baseline record saved in slot %d (%s)", (int)slot,
		automatic ? "automatic" : "manual");

out:
	k_mutex_unlock(&store_lock);

	return ret;
}
