/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Life_GPS contributors
 *
 * Barometric baseline and relative altitude. See lifegps/baro.h for the contract
 * and for the datasheet / README references behind the numbers.
 *
 * Two threads touch this module: the main loop feeds it once per second, and the
 * Bluetooth RX thread runs BARO_BASELINE (0x42) through it. Every read of the
 * shared state therefore takes baro_lock; persist() is called outside the lock
 * because a NOR write must not block the 1 Hz cadence for longer than necessary.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <math.h>
#include <string.h>

#include <lifegps/baro.h>
#include <lifegps/baro_store.h>

LOG_MODULE_REGISTER(lgps_baro, LOG_LEVEL_INF);

static K_MUTEX_DEFINE(baro_lock);

static struct {
	uint32_t baseline_pa_q4;
	bool baseline_set;
	bool baseline_auto;
	float pressure_pa;
	bool pressure_valid;
	int32_t rel_alt_mm;
	/* Automatic-baseline window (Welford, so the variance does not cancel). */
	uint8_t window_samples;
	float window_mean;
	float window_m2;
	/* Dating of the persisted record; written by the main loop only. */
	uint32_t utc_epoch_s;
} baro;

/* Pa x 16 -> whole Pa, exported because the wire format needs the pair. */
static uint32_t pa_q4_from_pa(float pressure_pa)
{
	if (!(pressure_pa > 0.0F)) {
		return LGPS_BARO_BASELINE_UNSET;
	}

	return (uint32_t)((pressure_pa * 16.0F) + 0.5F);
}

static bool pa_q4_in_range(uint32_t pa_q4)
{
	const uint32_t min_q4 = LGPS_BARO_BASELINE_MIN_PA * 16U;
	const uint32_t max_q4 = LGPS_BARO_BASELINE_MAX_PA * 16U;

	return (pa_q4 >= min_q4) && (pa_q4 <= max_q4);
}

int32_t lgps_baro_altitude_mm(float pressure_pa, uint32_t baseline_pa_q4)
{
	float baseline_pa;
	float ratio;
	float altitude_m;
	float altitude_mm;

	if (baseline_pa_q4 == LGPS_BARO_BASELINE_UNSET) {
		return 0;
	}

	if (!(pressure_pa > 0.0F)) {
		return 0;
	}

	baseline_pa = (float)baseline_pa_q4 / 16.0F;
	if (!(baseline_pa > 0.0F)) {
		return 0;
	}

	/* h = 44330 * (1 - (p/p0)^0.1902632), ISA with a 6.5 K/km lapse rate. */
	ratio = pressure_pa / baseline_pa;
	altitude_m = LGPS_BARO_ALT_SCALE_M * (1.0F - powf(ratio, LGPS_BARO_ALT_EXPONENT));
	altitude_mm = altitude_m * 1000.0F;

	if (altitude_mm > (float)LGPS_BARO_ALT_LIMIT_MM) {
		altitude_mm = (float)LGPS_BARO_ALT_LIMIT_MM;
	} else if (altitude_mm < -(float)LGPS_BARO_ALT_LIMIT_MM) {
		altitude_mm = -(float)LGPS_BARO_ALT_LIMIT_MM;
	}

	return (int32_t)altitude_mm;
}

static void refresh_rel_alt_locked(void)
{
	if (baro.pressure_valid && baro.baseline_set) {
		baro.rel_alt_mm = lgps_baro_altitude_mm(baro.pressure_pa, baro.baseline_pa_q4);
	} else {
		baro.rel_alt_mm = 0;
	}
}

void lgps_baro_note_utc(uint32_t utc_epoch_s)
{
	baro.utc_epoch_s = utc_epoch_s;
}

void lgps_baro_init(void)
{
	k_mutex_lock(&baro_lock, K_FOREVER);
	memset(&baro, 0, sizeof(baro));
	k_mutex_unlock(&baro_lock);
}

int lgps_baro_load(void)
{
	uint32_t pa_q4 = LGPS_BARO_BASELINE_UNSET;
	uint32_t unix_s = 0U;
	bool automatic = false;
	int ret = lgps_baro_store_load(&pa_q4, &automatic, &unix_s);

	if (ret < 0) {
		return ret;
	}

	if (!pa_q4_in_range(pa_q4)) {
		LOG_WRN("stored baseline %u Pa*16 is out of range, ignoring it",
			(unsigned int)pa_q4);
		return -ERANGE;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	baro.baseline_pa_q4 = pa_q4;
	baro.baseline_set = true;
	baro.baseline_auto = automatic;
	refresh_rel_alt_locked();
	k_mutex_unlock(&baro_lock);

	LOG_INF("barometric baseline restored: %u Pa*16 (%s, record dated %u)",
		(unsigned int)pa_q4, automatic ? "automatic" : "manual",
		(unsigned int)unix_s);

	return 0;
}

/* Persists the baseline that was just set. Called without baro_lock held. */
static void persist(uint32_t pa_q4, bool automatic)
{
	int ret = lgps_baro_store_save(pa_q4, automatic, baro.utc_epoch_s);

	if (ret < 0) {
		LOG_WRN("baseline record not saved (%d); it survives only until reboot", ret);
	}
}

int lgps_baro_set_baseline(uint32_t pa_q4, bool automatic)
{
	if (!pa_q4_in_range(pa_q4)) {
		return -EINVAL;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	baro.baseline_pa_q4 = pa_q4;
	baro.baseline_set = true;
	baro.baseline_auto = automatic;
	refresh_rel_alt_locked();
	k_mutex_unlock(&baro_lock);

	LOG_INF("barometric baseline set: %u Pa (%u Pa*16, %s)", (unsigned int)(pa_q4 / 16U),
		(unsigned int)pa_q4, automatic ? "automatic" : "manual");

	persist(pa_q4, automatic);

	return 0;
}

int lgps_baro_take_current(void)
{
	uint32_t pa_q4;

	k_mutex_lock(&baro_lock, K_FOREVER);
	if (!baro.pressure_valid) {
		k_mutex_unlock(&baro_lock);
		return -ENODATA;
	}

	pa_q4 = pa_q4_from_pa(baro.pressure_pa);
	k_mutex_unlock(&baro_lock);

	/* A baseline taken by hand is manual even if the gate would have agreed. */
	return lgps_baro_set_baseline(pa_q4, false);
}

int32_t lgps_baro_rel_alt_mm(void)
{
	int32_t rel_alt_mm;

	k_mutex_lock(&baro_lock, K_FOREVER);
	rel_alt_mm = baro.rel_alt_mm;
	k_mutex_unlock(&baro_lock);

	return rel_alt_mm;
}

bool lgps_baro_pressure(float *pressure_pa)
{
	bool valid;

	if (pressure_pa == NULL) {
		return false;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	valid = baro.pressure_valid;
	if (valid) {
		*pressure_pa = baro.pressure_pa;
	}
	k_mutex_unlock(&baro_lock);

	return valid;
}

uint32_t lgps_baro_baseline_pa_q4(void)
{
	uint32_t pa_q4;

	k_mutex_lock(&baro_lock, K_FOREVER);
	pa_q4 = baro.baseline_set ? baro.baseline_pa_q4 : LGPS_BARO_BASELINE_UNSET;
	k_mutex_unlock(&baro_lock);

	return pa_q4;
}

bool lgps_baro_baseline_set(void)
{
	bool set_flag;

	k_mutex_lock(&baro_lock, K_FOREVER);
	set_flag = baro.baseline_set;
	k_mutex_unlock(&baro_lock);

	return set_flag;
}

bool lgps_baro_baseline_auto(void)
{
	bool auto_flag;

	k_mutex_lock(&baro_lock, K_FOREVER);
	auto_flag = baro.baseline_auto;
	k_mutex_unlock(&baro_lock);

	return auto_flag;
}

static bool gate_open(const struct lgps_gnss_fix *fix, bool pressure_valid)
{
	if ((fix == NULL) || !pressure_valid) {
		return false;
	}

	if (!fix->valid || (fix->fix_type < LGPS_BARO_AUTO_MIN_FIX_TYPE)) {
		return false;
	}

	if (fix->satellites < LGPS_BARO_AUTO_MIN_SATELLITES) {
		return false;
	}

	/* pDOP = 0 means "not reported", which is not the same as a perfect 0. */
	if ((fix->pdop_1e2 == 0U) || (fix->pdop_1e2 > LGPS_BARO_AUTO_MAX_PDOP_1E2)) {
		return false;
	}

	return fix->speed_cms <= LGPS_BARO_AUTO_MAX_SPEED_CMS;
}

static void window_reset_locked(void)
{
	baro.window_samples = 0U;
	baro.window_mean = 0.0F;
	baro.window_m2 = 0.0F;
}

bool lgps_baro_feed(bool pressure_valid, float pressure_pa, const struct lgps_gnss_fix *fix)
{
	uint32_t candidate_q4 = LGPS_BARO_BASELINE_UNSET;
	float accepted_sigma = 0.0F;
	bool changed = false;
	bool open;

	k_mutex_lock(&baro_lock, K_FOREVER);

	baro.pressure_valid = pressure_valid;
	if (pressure_valid) {
		baro.pressure_pa = pressure_pa;
	}
	refresh_rel_alt_locked();

	open = gate_open(fix, pressure_valid);
	if (!open) {
		window_reset_locked();
		k_mutex_unlock(&baro_lock);
		return false;
	}

	/* Welford: mean and M2 stay accurate for 30 samples of ~1e5 Pa, where a
	 * sum-of-squares/mean-square variance would cancel away completely.
	 */
	baro.window_samples++;
	{
		const float delta = pressure_pa - baro.window_mean;
		const float count = (float)baro.window_samples;

		baro.window_mean += delta / count;
		baro.window_m2 += delta * (pressure_pa - baro.window_mean);
	}

	if (baro.window_samples >= LGPS_BARO_AUTO_WINDOW_SAMPLES) {
		const float variance = baro.window_m2 / (float)baro.window_samples;
		const float sigma = sqrtf((variance > 0.0F) ? variance : 0.0F);

		if (sigma <= LGPS_BARO_AUTO_MAX_SIGMA_PA) {
			const uint32_t mean_q4 = pa_q4_from_pa(baro.window_mean);

			/* Hysteresis: only move the reference when it really moved. */
			const uint32_t diff = (mean_q4 > baro.baseline_pa_q4)
						      ? (mean_q4 - baro.baseline_pa_q4)
						      : (baro.baseline_pa_q4 - mean_q4);

			if (pa_q4_in_range(mean_q4) &&
			    (!baro.baseline_set ||
			     (diff > (LGPS_BARO_AUTO_REBASE_MIN_PA * 16U)))) {
				baro.baseline_pa_q4 = mean_q4;
				baro.baseline_set = true;
				baro.baseline_auto = true;
				refresh_rel_alt_locked();
				candidate_q4 = mean_q4;
				accepted_sigma = sigma;
				changed = true;
			}
		}

		window_reset_locked();
	}

	k_mutex_unlock(&baro_lock);

	if (changed) {
		LOG_INF("barometric baseline from GNSS gate: %u Pa (%u Pa*16, "
			"%u samples, sigma %.1f Pa)",
			(unsigned int)(candidate_q4 / 16U), (unsigned int)candidate_q4,
			(unsigned int)LGPS_BARO_AUTO_WINDOW_SAMPLES, (double)accepted_sigma);
		persist(candidate_q4, true);
	}

	return changed;
}
