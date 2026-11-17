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
static char reset_text[48] = "unknown";
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

	int ret = hwinfo_get_reset_cause(&reset_cause);

	if (ret < 0) {
		LOG_ERR("hwinfo_get_reset_cause failed (%d)", ret);
		return;
	}

	reset_cause_valid = true;
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
		(void)strncpy(reset_text, "none", sizeof(reset_text) - 1U);
	}
}

const char *lgps_diag_reset_reason(void)
{
	return reset_text;
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
	 * Known deviation, tracked as pinmap pending item "crystal CL": the fitted
	 * crystals (X1 CL 12.5 pF, X2 CL 12 pF) sit above the nRF54L15 internal
	 * trim range, so BLE carrier accuracy must be measured before RF bring-up.
	 */
	LOG_WRN("crystal CL out of spec: X1 12.5 pF / X2 12 pF vs 6..9 pF, "
		"internal trim set to max (LFXO 18000 fF, HFXO 17000 fF)");
}

void lgps_diag_hexdump(const char *tag, const uint8_t *data, size_t len_bytes)
{
	if (tag == NULL || data == NULL) {
		return;
	}

	LOG_HEXDUMP_INF(data, len_bytes, tag);
}
