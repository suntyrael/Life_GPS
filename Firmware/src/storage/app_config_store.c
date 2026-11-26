/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - append-only NOR persistence for device Config TLV
 * (external NOR sector 18, 0x12000; compatible with both 32 MB and 64 MB Flash).
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>

#include <lifegps/app_config.h>
#include <lifegps/archive.h>

LOG_MODULE_REGISTER(lgps_cfg, LOG_LEVEL_INF);

#define CONFIG_SECTOR_SIZE 4096U
#define CONFIG_SLOT_COUNT (CONFIG_SECTOR_SIZE / LGPS_APP_CONFIG_RECORD_SIZE)

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(w25q512));
static K_MUTEX_DEFINE(cfg_lock);

static struct lgps_app_config current_cfg;
static uint32_t next_seq = 1U;
static uint32_t next_slot;

static off_t slot_offset(uint32_t slot)
{
	return ((off_t)LGPS_ARCHIVE_CONFIG_SECTOR_START * (off_t)CONFIG_SECTOR_SIZE) +
	       (off_t)(slot * LGPS_APP_CONFIG_RECORD_SIZE);
}

static bool slot_is_erased(const uint8_t rec[LGPS_APP_CONFIG_RECORD_SIZE])
{
	return (rec[0] == 0xFFU) && (rec[1] == 0xFFU) && (rec[2] == 0xFFU) && (rec[3] == 0xFFU);
}

static int persist_locked(void)
{
	uint8_t rec[LGPS_APP_CONFIG_RECORD_SIZE];
	int ret;

	if (!device_is_ready(nor)) {
		return -ENODEV;
	}

	if (next_slot >= CONFIG_SLOT_COUNT) {
		ret = flash_erase(nor, slot_offset(0U), CONFIG_SECTOR_SIZE);
		if (ret < 0) {
			return ret;
		}
		next_slot = 0U;
	}

	ret = lgps_app_config_record_encode(&current_cfg, next_seq, rec);
	if (ret < 0) {
		return ret;
	}

	ret = flash_write(nor, slot_offset(next_slot), rec, sizeof(rec));
	if (ret == 0) {
		next_seq++;
		next_slot++;
	}

	return ret;
}

int lgps_app_config_init(void)
{
	uint8_t rec[LGPS_APP_CONFIG_RECORD_SIZE];
	struct lgps_app_config candidate;
	uint32_t best_seq = 0U;
	bool found = false;

	k_mutex_lock(&cfg_lock, K_FOREVER);
	lgps_app_config_defaults(&current_cfg);
	next_seq = 1U;
	next_slot = 0U;

	if (device_is_ready(nor)) {
		for (uint32_t slot = 0U; slot < CONFIG_SLOT_COUNT; slot++) {
			uint32_t seq = 0U;

			if (flash_read(nor, slot_offset(slot), rec, sizeof(rec)) < 0) {
				break;
			}
			if (slot_is_erased(rec)) {
				next_slot = slot;
				break;
			}
			next_slot = slot + 1U;
			if (lgps_app_config_record_decode(rec, &candidate, &seq) == 0) {
				if (!found || (seq >= best_seq)) {
					best_seq = seq;
					current_cfg = candidate;
					found = true;
				}
			}
		}
		if (found) {
			next_seq = best_seq + 1U;
			LOG_INF("config restored from NOR sector %u (seq=%u, mode=%u, tz=%d min)",
				LGPS_ARCHIVE_CONFIG_SECTOR_START, best_seq,
				current_cfg.record_mode, current_cfg.tz_offset_min);
		}
	}

	k_mutex_unlock(&cfg_lock);
	return 0;
}

void lgps_app_config_get(struct lgps_app_config *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&cfg_lock, K_FOREVER);
	*out = current_cfg;
	k_mutex_unlock(&cfg_lock);
}

int lgps_app_config_update_tlv(const uint8_t *tlv, size_t len)
{
	int ret;

	k_mutex_lock(&cfg_lock, K_FOREVER);
	ret = lgps_app_config_apply_tlv(&current_cfg, tlv, len);
	if (ret == 0) {
		int p = persist_locked();

		if (p < 0) {
			LOG_WRN("config updated in RAM, NOR persist failed (%d)", p);
		}
	}
	k_mutex_unlock(&cfg_lock);
	return ret;
}

int lgps_app_config_factory_reset(void)
{
	int ret = 0;

	k_mutex_lock(&cfg_lock, K_FOREVER);
	lgps_app_config_defaults(&current_cfg);
	next_seq = 1U;
	next_slot = 0U;
	if (device_is_ready(nor)) {
		ret = flash_erase(nor, slot_offset(0U), CONFIG_SECTOR_SIZE);
	}
	k_mutex_unlock(&cfg_lock);
	return ret;
}
