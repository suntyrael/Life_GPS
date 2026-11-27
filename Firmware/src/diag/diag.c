/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * UART diagnostics: the only instrument this board has during bring-up.
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include <lifegps/diag.h>

#include <generated/lifegps_protocol.h>

LOG_MODULE_REGISTER(lgps_diag, LOG_LEVEL_INF);

static uint32_t reset_cause;
/*
 * Worst case, all twelve names joined by '+':
 * "reset-pin+software+brownout+power-on+watchdog+debug+security+
 *  wake-from-system-off+cpu-lockup+clock+hardware+temperature" = 119 characters.
 * The previous 48-byte buffer silently truncated the list (and strncat() with
 * sizeof - strlen - 1 kept it legal, so nothing complained).
 */
static char reset_text[128] = "unknown";
static bool reset_cause_valid;

uint32_t lgps_diag_uptime_ms(void)
{
	return k_uptime_get_32();
}

static void decode_reset_cause(void)
{
	static const struct {
		uint32_t flag;
		const char *name;
	} causes[] = {
		/*
		 * Not every flag is produced by the nRF54L15 HWINFO driver (brownout /
		 * hardware / temperature have no RESETREAS source on this part); the table
		 * stays complete so a port to another SoC needs no edit, and the banner prints
		 * whatever the driver reports as a hex mask as well.
		 */
		{ RESET_PIN, "reset-pin" },
		{ RESET_SOFTWARE, "software" },
		{ RESET_BROWNOUT, "brownout" },
		{ RESET_POR, "power-on" },
		{ RESET_WATCHDOG, "watchdog" },
		{ RESET_DEBUG, "debug" },
		{ RESET_SECURITY, "security" },
		{ RESET_LOW_POWER_WAKE, "wake-from-system-off" },
		{ RESET_CPU_LOCKUP, "cpu-lockup" },
		{ RESET_CLOCK, "clock" },
		{ RESET_HARDWARE, "hardware" },
		{ RESET_TEMPERATURE, "temperature" },
	};

	int ret;

	if (reset_cause_valid) {
		return;
	}

	ret = hwinfo_get_reset_cause(&reset_cause);

	if (ret < 0) {
		LOG_ERR("hwinfo_get_reset_cause failed (%d)", ret);
		return;
	}

	reset_cause_valid = true;

	/*
	 * Clear RESETREAS after reading it. The nRF reset-reason registers accumulate:
	 * a watchdog reset followed later by a pin reset reports both, so without this
	 * every boot logs the union of all previous resets instead of this one. A driver
	 * without the clear operation (-ENOSYS) is not fatal - the banner then keeps
	 * showing the accumulated mask.
	 */
	ret = hwinfo_clear_reset_cause();
	if (ret < 0) {
		LOG_DBG("hwinfo_clear_reset_cause unsupported (%d): reset causes accumulate",
			ret);
	}

	reset_text[0] = '\0';

	ARRAY_FOR_EACH(causes, i) {
		if ((reset_cause & causes[i].flag) == 0U) {
			continue;
		}

		if (reset_text[0] != '\0') {
			(void)strncat(reset_text, "+", sizeof(reset_text) - strlen(reset_text) - 1U);
		}

		(void)strncat(reset_text, causes[i].name,
			      sizeof(reset_text) - strlen(reset_text) - 1U);
	}

	if (reset_text[0] == '\0') {
		/* RESETREAS came back empty: report "unreported" instead of claiming that
		 * nothing happened, because it may also mean the cause was already cleared.
		 */
		(void)strncpy(reset_text, (reset_cause == 0U) ? "unreported" : "no-flag-matched",
			      sizeof(reset_text) - 1U);
	}
}

const char *lgps_diag_reset_reason(void)
{
	return reset_text;
}

uint32_t lgps_diag_reset_cause_raw(void)
{
	if (!reset_cause_valid) {
		decode_reset_cause();
	}

	return reset_cause_valid ? reset_cause : 0U;
}

void lgps_diag_banner(void)
{
	decode_reset_cause();

	LOG_INF("=========================================================");
	LOG_INF(" Life_GPS firmware M1 (bring-up skeleton)");
	LOG_INF(" board  : Main_V0.2, nRF54L15-QFAA (QFN48), 1.9 V rails");
	LOG_INF(" build  : protocol %u.%u.%u, NCS locked by versions.yaml", LGPS_PROTO_VERSION_MAJOR,
		LGPS_PROTO_VERSION_MINOR, LGPS_PROTO_VERSION_PATCH);
	LOG_INF(" reset  : %s (0x%08x), uptime %u ms", reset_text,
		reset_cause_valid ? reset_cause : 0U, lgps_diag_uptime_ms());
	LOG_INF(" console: DEBUG_UART P0.00 TX / P0.01 RX (UARTE30) 115200 8N1");
	LOG_INF(" buses  : GPSSPI=UARTE20 P1.04/P1.05, FLASH=SPIM00 P2.01/02/04/05, "
		"IMU=SPIM22 P1.03/02/13/14");
	LOG_INF("=========================================================");

	/*
	 * STATIC CONFIG CHECK, NOT A MEASUREMENT: the numbers come from the BOM /
	 * devicetree (X1 = FC-135R CL 12.5 pF, X2 = 32 MHz CL 12 pF) compared with the
	 * nRF54L15 internal-trim window (CL_LFXO/CL_HFXO = 6..9 pF, datasheet section
	 * 11). Nothing on this board measures the crystal; only a frequency counter or
	 * a BLE carrier test can confirm the actual ppm error.
	 */
	LOG_WRN("static config check (NOT measured): X1 CL 12.5 pF / X2 CL 12 pF sit above "
		"the nRF54L15 6..9 pF trim window; trim set to max (LFXO 18000 fF, HFXO 17000 fF). "
		"Confirm with a frequency counter before RF bring-up");
}

void lgps_diag_hexdump(const char *tag, const uint8_t *data, size_t len_bytes)
{
	if (tag == NULL || data == NULL) {
		return;
	}

	LOG_HEXDUMP_INF(data, len_bytes, tag);
}
