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

/*
 * Version 2: magic / u8 version / u8 flags / reference_pa_q4 / reference_alt_mm /
 * unix_s / CRC-16 over the first 18 bytes (see lifegps/baro_store.h).
 */
static int record_encode(uint32_t pa_q4, bool automatic, enum lgps_baro_reference kind,
			 int32_t ref_alt_mm, uint32_t unix_s, uint8_t out[LGPS_BARO_RECORD_SIZE])
{
	uint8_t flags = 0U;

	if (automatic) {
		flags |= 0x01U;
	}

	if (kind == LGPS_BARO_REF_MSL) {
		flags |= 0x02U;
	}

	memset(out, 0, LGPS_BARO_RECORD_SIZE);
	put_u32(&out[0], LGPS_BARO_RECORD_MAGIC);
	out[4] = (uint8_t)LGPS_BARO_RECORD_VERSION;
	out[5] = flags;
	put_u32(&out[6], pa_q4);
	put_u32(&out[10], (uint32_t)ref_alt_mm);
	put_u32(&out[14], unix_s);
	put_u16(&out[18], lgps_crc16_ccitt_false(out, 18U));

	return 0;
}

static int record_decode(const uint8_t in[LGPS_BARO_RECORD_SIZE], uint32_t *pa_q4,
			 bool *automatic, enum lgps_baro_reference *kind, int32_t *ref_alt_mm,
			 uint32_t *unix_s)
{
	if (get_u32(&in[0]) != LGPS_BARO_RECORD_MAGIC) {
		return -EBADMSG;
	}

	/* Byte 4 is the version in both layouts: v1 wrote it as u16 LE, so its low
	 * byte is 1 and the high byte (flags bit 8) is always 0.
	 */
	if (in[4] == 1U) {
		if (get_u16(&in[16]) != lgps_crc16_ccitt_false(in, 16U)) {
			return -EBADMSG;
		}

		*pa_q4 = get_u32(&in[8]);
		*automatic = (get_u16(&in[6]) & 0x01U) != 0U;
		*kind = LGPS_BARO_REF_LOCAL;
		*ref_alt_mm = 0;
		*unix_s = get_u32(&in[12]);

		return 0;
	}

	if (in[4] != (uint8_t)LGPS_BARO_RECORD_VERSION) {
		return -EBADMSG;
	}

	if (get_u16(&in[18]) != lgps_crc16_ccitt_false(in, 18U)) {
		return -EBADMSG;
	}

	*pa_q4 = get_u32(&in[6]);
	*automatic = (in[5] & 0x01U) != 0U;
	*kind = ((in[5] & 0x02U) != 0U) ? LGPS_BARO_REF_MSL : LGPS_BARO_REF_LOCAL;
	*ref_alt_mm = (int32_t)get_u32(&in[10]);
	*unix_s = get_u32(&in[14]);

	return 0;
}

int lgps_baro_store_load(uint32_t *pa_q4, bool *automatic, enum lgps_baro_reference *kind,
			 int32_t *ref_alt_mm, uint32_t *unix_s)
{
	uint8_t record[LGPS_BARO_RECORD_SIZE];
	bool found = false;
	uint32_t index;
	int ret = 0;

	if ((pa_q4 == NULL) || (automatic == NULL) || (kind == NULL) || (ref_alt_mm == NULL) ||
	    (unix_s == NULL)) {
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
		int32_t slot_ref_alt_mm = 0;
		bool slot_auto = false;
		enum lgps_baro_reference slot_kind = LGPS_BARO_REF_LOCAL;
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
		if (record_decode(record, &slot_pa_q4, &slot_auto, &slot_kind, &slot_ref_alt_mm,
				  &slot_unix) != 0) {
			continue;
		}

		*pa_q4 = slot_pa_q4;
		*automatic = slot_auto;
		*kind = slot_kind;
		*ref_alt_mm = slot_ref_alt_mm;
		*unix_s = slot_unix;
		found = true;
	}

	ret = found ? 0 : -ENOENT;

out:
	k_mutex_unlock(&store_lock);

	return ret;
}

int lgps_baro_store_save(uint32_t pa_q4, bool automatic, enum lgps_baro_reference kind,
			 int32_t ref_alt_mm, uint32_t unix_s)
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

	ret = record_encode(pa_q4, automatic, kind, ref_alt_mm, unix_s, record);
	if (ret < 0) {
		goto out;
	}

	ret = flash_write(nor, slot_offset((uint32_t)slot), record, sizeof(record));
	if (ret < 0) {
		LOG_ERR("slot %d write failed (%d)", (int)slot, ret);
		goto out;
	}

	LOG_INF("reference record saved in slot %d (%s, %s, %d mm)", (int)slot,
		automatic ? "automatic" : "manual",
		(kind == LGPS_BARO_REF_MSL) ? "sea level" : "local", ref_alt_mm);

out:
	k_mutex_unlock(&store_lock);

	return ret;
}

int lgps_baro_store_erase(void)
{
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	k_mutex_lock(&store_lock, K_FOREVER);
	ret = flash_erase(nor, sector_offset(), LGPS_BARO_SECTOR_SIZE);
	k_mutex_unlock(&store_lock);

	if (ret < 0) {
		LOG_ERR("baro baseline sector erase failed (%d)", ret);
	} else {
		LOG_INF("baro baseline sector erased");
	}

	return ret;
}
