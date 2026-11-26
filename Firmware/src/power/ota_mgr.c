/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - MCUboot OTA image confirmation manager.
 */

#include <lifegps/ota_mgr.h>

#include <zephyr/logging/log.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(lgps_ota, LOG_LEVEL_INF);

bool lgps_ota_is_confirmed(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	return boot_is_img_confirmed();
#else
	return true;
#endif
}

int lgps_ota_confirm_boot(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	int err;

	if (boot_is_img_confirmed()) {
		LOG_INF("MCUboot image already confirmed");
		return 0;
	}

	err = boot_write_img_confirmed();
	if (err != 0) {
		LOG_ERR("MCUboot image confirm failed (%d)", err);
		return err;
	}

	LOG_INF("MCUboot image confirmed after healthy bring-up");
	return 0;
#else
	return 0;
#endif
}
