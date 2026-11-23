/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * Voltage-only state of charge, fitted from one 0.2 C cycle of the
 * 1000 mAh-class pack in 1000mAh电池放电.xlsx (2026-10-19):
 *
 *   CC discharge 4.1424 V -> 3.000 V, 1.1106 Ah at ~0.200 A
 *   CC-CV charge 3.2812 V -> 4.2000 V, 1.1102 Ah at ~0.200 A
 *
 * ocv_mv is the midpoint of the charge and discharge terminal voltages at the
 * same state of charge; the ends are the rested voltages after the discharge
 * (3.247 V after 30 min / 3.2205 V after 10 min, so 3.23 V) and after the full
 * charge (4.173 V after 30 min). The README power model puts the device at
 * ~0.02-11.5 mA, far below the 200 mA test, so its terminal voltage is close to
 * OCV.
 *
 * charge_mv is the measured terminal voltage during the 0.2 C CC charge and is
 * used only while the charger reports current flowing. The board charges at
 * ~0.35 A, so the CC percentage reads a little high; the OCV table takes over
 * once /CHG deasserts.
 *
 * Limits: one cell, one cycle, no temperature log, no rate sweep. Keep these
 * numbers in sync with App/app/src/main/java/com/lifegps/app/ble/BatterySoc.kt
 * until the App fallback for older firmware is removed.
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/sys/util.h>

#include <lifegps/battery.h>

#define SOC_POINTS 21U

/* 0, 5, 10, ... 100 %: the table is linear in voltage between these points. */
static const int16_t ocv_mv[SOC_POINTS] = {
	3230, 3427, 3489, 3553, 3608, 3653, 3704, 3764, 3809, 3842, 3869,
	3894, 3919, 3952, 3994, 4029, 4049, 4068, 4085, 4113, 4173,
};

static const int16_t charge_mv[SOC_POINTS] = {
	3281, 3506, 3558, 3620, 3675, 3718, 3766, 3823, 3866, 3898, 3926,
	3955, 3981, 4014, 4058, 4088, 4105, 4125, 4144, 4182, 4200,
};

static const uint8_t soc_pct[SOC_POINTS] = {
	0U, 5U, 10U, 15U, 20U, 25U, 30U, 35U, 40U, 45U, 50U,
	55U, 60U, 65U, 70U, 75U, 80U, 85U, 90U, 95U, 100U,
};

BUILD_ASSERT(ARRAY_SIZE(ocv_mv) == SOC_POINTS, "OCV table length changed");
BUILD_ASSERT(ARRAY_SIZE(charge_mv) == SOC_POINTS, "charge table length changed");
BUILD_ASSERT(ARRAY_SIZE(soc_pct) == SOC_POINTS, "percent table length changed");

int lgps_battery_soc_pct(int32_t millivolts, bool charging, uint8_t *percent)
{
	const int16_t *curve;
	int32_t span;
	int32_t delta;
	int32_t numerator;
	size_t index;

	if (percent == NULL) {
		return -EINVAL;
	}

	if (millivolts < LGPS_BATT_MV_PLAUSIBLE_MIN) {
		*percent = LGPS_BATT_SOC_UNKNOWN;
		return 0;
	}

	curve = charging ? charge_mv : ocv_mv;

	if (millivolts <= curve[0]) {
		*percent = 0U;
		return 0;
	}

	if (millivolts >= curve[SOC_POINTS - 1U]) {
		*percent = 100U;
		return 0;
	}

	for (index = 0U; index < (SOC_POINTS - 1U); index++) {
		if (millivolts <= curve[index + 1U]) {
			span = (int32_t)curve[index + 1U] - curve[index];
			if (span <= 0) {
				*percent = soc_pct[index];
				return 0;
			}
			delta = (int32_t)soc_pct[index + 1U] - soc_pct[index];
			numerator = (millivolts - curve[index]) * delta;
			*percent = (uint8_t)(soc_pct[index] + (numerator + span / 2) / span);
			return 0;
		}
	}

	*percent = 100U;
	return 0;
}
