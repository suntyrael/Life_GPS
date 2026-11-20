/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Life_GPS contributors
 *
 * Barometric reference and altitude. See lifegps/baro.h for the contract and for
 * the README / datasheet references behind the numbers.
 *
 * Two reference kinds share one stored pressure:
 *
 *   LOCAL  the pressure at the point where the reference was taken, so the
 *          instrument reports height *since then* ("relative altitude").
 *   MSL    the sea-level pressure QNH derived from a GNSS height, so the same
 *          formula reports an absolute altitude that still works indoors and
 *          after the GNSS link is gone.
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

/* Averaging window for the pressure used by a GNSS calibration: ~10 s at 1 Hz.
 * One sample carries several Pa of noise, and at sea level 1 Pa is 8 cm, so an
 * unaveraged calibration would throw away most of the sensor's resolution.
 */
#define PRESSURE_AVG_ALPHA 0.1F

static K_MUTEX_DEFINE(baro_lock);

static struct {
	uint32_t baseline_pa_q4;
	bool baseline_set;
	bool baseline_auto;
	/* What the reference means, and the height it sits at (0 for LOCAL). */
	enum lgps_baro_reference ref_kind;
	int32_t ref_alt_mm;
	float pressure_pa;
	bool pressure_valid;
	/* Height since the reference was set, and (MSL mode only) the absolute one. */
	int32_t rel_alt_mm;
	int32_t abs_alt_mm;
	/* Slow average of the compensated pressure, for GNSS calibration. */
	float pressure_avg;
	bool pressure_avg_valid;
	/* Automatic-reference window (Welford, so the variance does not cancel). */
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

/* Sea-level pressure QNH from a station pressure and a height: the inverse of the
 * ISA height formula, i.e. "what would this sensor read at mean sea level".
 * Returns 0 when the inputs cannot produce a pressure.
 */
float lgps_baro_sealevel_pressure_pa(float pressure_pa, int32_t height_mm)
{
	const float height_m = (float)height_mm / 1000.0F;
	const float ratio = 1.0F - (height_m / LGPS_BARO_ALT_SCALE_M);
	float sea_level_pa;

	if (!(pressure_pa > 0.0F)) {
		return 0.0F;
	}

	/* The base of the power must stay positive. A real station is at most a few
	 * km up, so anything near 44 km is a corrupt height, not a measurement.
	 */
	if (!(ratio > 0.05F)) {
		return 0.0F;
	}

	/* p/p0 = (1 - h/44330)^5.25588, so p0 = p / that. Using the 0.1902632
	 * exponent itself here would answer "pressure at 5 km", not sea level.
	 */
	sea_level_pa = pressure_pa / powf(ratio, LGPS_BARO_ISA_EXPONENT);

	if (!(sea_level_pa > 0.0F)) {
		return 0.0F;
	}

	return sea_level_pa;
}

/*
 * Caller holds baro_lock.
 *
 * The stored pressure is what the height formula is anchored on, and both kinds
 * differ in what that anchor means: in LOCAL mode it is the pressure where the
 * user stood, so the formula yields the height since then; in MSL mode it is the
 * sea-level pressure, so the *formula itself* already yields an absolute altitude
 * and the height since calibration is the difference to the height it was
 * calibrated at. Doing it the other way round (storing the station pressure and
 * adding the GNSS height) would double-count the reference height.
 */
static void refresh_alt_locked(void)
{
	const int32_t above_reference_mm =
		(baro.pressure_valid && baro.baseline_set)
			? lgps_baro_altitude_mm(baro.pressure_pa, baro.baseline_pa_q4)
			: 0;

	if (baro.pressure_valid && baro.baseline_set && (baro.ref_kind == LGPS_BARO_REF_MSL)) {
		baro.abs_alt_mm = above_reference_mm;
		baro.rel_alt_mm = above_reference_mm - baro.ref_alt_mm;
	} else {
		baro.abs_alt_mm = 0;
		baro.rel_alt_mm = above_reference_mm;
	}
}

/* Caller holds baro_lock. */
static void apply_reference_locked(uint32_t pa_q4, enum lgps_baro_reference kind, int32_t ref_alt_mm,
				   bool automatic)
{
	baro.baseline_pa_q4 = pa_q4;
	baro.baseline_set = true;
	baro.baseline_auto = automatic;
	baro.ref_kind = kind;
	baro.ref_alt_mm = (kind == LGPS_BARO_REF_MSL) ? ref_alt_mm : 0;
	refresh_alt_locked();
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
	int32_t ref_alt_mm = 0;
	bool automatic = false;
	enum lgps_baro_reference kind = LGPS_BARO_REF_LOCAL;
	int ret = lgps_baro_store_load(&pa_q4, &automatic, &kind, &ref_alt_mm, &unix_s);

	if (ret < 0) {
		return ret;
	}

	if (!pa_q4_in_range(pa_q4)) {
		LOG_WRN("stored reference %u Pa*16 is out of range, ignoring it",
			(unsigned int)pa_q4);
		return -ERANGE;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	apply_reference_locked(pa_q4, kind, ref_alt_mm, automatic);
	k_mutex_unlock(&baro_lock);

	LOG_INF("barometric reference restored: %u Pa*16 (%s, %s, dated %u)", (unsigned int)pa_q4,
		(kind == LGPS_BARO_REF_MSL) ? "sea level" : "local", automatic ? "automatic" : "manual",
		(unsigned int)unix_s);

	return 0;
}

/* Persists the reference that was just set. Called without baro_lock held. */
static void persist(uint32_t pa_q4, bool automatic, enum lgps_baro_reference kind,
		    int32_t ref_alt_mm)
{
	int ret = lgps_baro_store_save(pa_q4, automatic, kind, ref_alt_mm, baro.utc_epoch_s);

	if (ret < 0) {
		LOG_WRN("reference record not saved (%d); it survives only until reboot", ret);
	}
}

int lgps_baro_set_baseline(uint32_t pa_q4, bool automatic)
{
	if (!pa_q4_in_range(pa_q4)) {
		return -EINVAL;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	/* A reference taken by hand is the pressure where the user stood, so the
	 * height it defines is 0 there: relative mode, never an absolute claim.
	 */
	apply_reference_locked(pa_q4, LGPS_BARO_REF_LOCAL, 0, automatic);
	k_mutex_unlock(&baro_lock);

	LOG_INF("barometric baseline set: %u Pa (%u Pa*16, %s, local reference)",
		(unsigned int)(pa_q4 / 16U), (unsigned int)pa_q4,
		automatic ? "automatic" : "manual");

	persist(pa_q4, automatic, LGPS_BARO_REF_LOCAL, 0);

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

static bool gnss_height_usable(const struct lgps_gnss_fix *fix)
{
	if (fix == NULL) {
		return false;
	}

	if (!fix->valid || (fix->fix_type < LGPS_BARO_GNSS_MIN_FIX_TYPE)) {
		return false;
	}

	if (fix->satellites < LGPS_BARO_GNSS_MIN_SATELLITES) {
		return false;
	}

	/* pDOP = 0 means "not reported", which is not the same as a perfect 0. */
	if ((fix->pdop_1e2 == 0U) || (fix->pdop_1e2 > LGPS_BARO_GNSS_MAX_PDOP_1E2)) {
		return false;
	}

	if (fix->speed_cms > LGPS_BARO_GNSS_MAX_SPEED_CMS) {
		return false;
	}

	/* vAcc is the receiver's own 1-sigma vertical estimate. A height that is
	 * worse than 20 m cannot calibrate an altimeter that resolves 0.8 m.
	 */
	return (fix->v_acc_mm != 0U) && (fix->v_acc_mm <= LGPS_BARO_GNSS_MAX_VACC_MM);
}

int lgps_baro_calibrate_from_gnss(const struct lgps_gnss_fix *fix)
{
	float station_pa;
	float sea_level_pa;
	uint32_t pa_q4;

	if (!gnss_height_usable(fix)) {
		return -EINVAL;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	if (!baro.pressure_valid) {
		k_mutex_unlock(&baro_lock);
		return -ENODATA;
	}

	station_pa = baro.pressure_avg_valid ? baro.pressure_avg : baro.pressure_pa;
	k_mutex_unlock(&baro_lock);

	sea_level_pa = lgps_baro_sealevel_pressure_pa(station_pa, fix->hmsl_mm);
	pa_q4 = pa_q4_from_pa(sea_level_pa);
	if (!pa_q4_in_range(pa_q4)) {
		return -EINVAL;
	}

	k_mutex_lock(&baro_lock, K_FOREVER);
	apply_reference_locked(pa_q4, LGPS_BARO_REF_MSL, fix->hmsl_mm, false);
	k_mutex_unlock(&baro_lock);

	LOG_INF("barometer calibrated from GNSS: hMSL %d mm (vAcc %u mm), station %u Pa -> "
		"sea level %u Pa",
		fix->hmsl_mm, (unsigned int)fix->v_acc_mm, (unsigned int)(station_pa + 0.5F),
		(unsigned int)(pa_q4 / 16U));

	persist(pa_q4, false, LGPS_BARO_REF_MSL, fix->hmsl_mm);

	return 0;
}

int32_t lgps_baro_rel_alt_mm(void)
{
	int32_t rel_alt_mm;

	k_mutex_lock(&baro_lock, K_FOREVER);
	rel_alt_mm = baro.rel_alt_mm;
	k_mutex_unlock(&baro_lock);

	return rel_alt_mm;
}

int32_t lgps_baro_msl_altitude_mm(void)
{
	int32_t altitude_mm = 0;

	k_mutex_lock(&baro_lock, K_FOREVER);
	if ((baro.ref_kind == LGPS_BARO_REF_MSL) && baro.baseline_set && baro.pressure_valid) {
		altitude_mm = baro.abs_alt_mm;
	}
	k_mutex_unlock(&baro_lock);

	return altitude_mm;
}

bool lgps_baro_msl_valid(void)
{
	bool valid;

	k_mutex_lock(&baro_lock, K_FOREVER);
	valid = (baro.ref_kind == LGPS_BARO_REF_MSL) && baro.baseline_set && baro.pressure_valid;
	k_mutex_unlock(&baro_lock);

	return valid;
}

enum lgps_baro_reference lgps_baro_reference_kind(void)
{
	enum lgps_baro_reference kind;

	k_mutex_lock(&baro_lock, K_FOREVER);
	kind = baro.ref_kind;
	k_mutex_unlock(&baro_lock);

	return kind;
}

int32_t lgps_baro_reference_height_mm(void)
{
	int32_t ref_alt_mm;

	k_mutex_lock(&baro_lock, K_FOREVER);
	ref_alt_mm = (baro.ref_kind == LGPS_BARO_REF_MSL) ? baro.ref_alt_mm : 0;
	k_mutex_unlock(&baro_lock);

	return ref_alt_mm;
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

/*
 * The automatic reference. Two outcomes, decided by whether the receiver also
 * reports a usable vertical accuracy:
 *
 *   vAcc usable  -> MSL reference: QNH from the window mean and the fix's hMSL,
 *                   i.e. an absolute altimeter that keeps working without GNSS.
 *   vAcc unusable -> LOCAL reference (what the module did before): relative
 *                   height only, because an absolute claim from a +/-30 m height
 *                   would be a lie.
 */
bool lgps_baro_feed(bool pressure_valid, float pressure_pa, const struct lgps_gnss_fix *fix)
{
	uint32_t candidate_q4 = LGPS_BARO_BASELINE_UNSET;
	enum lgps_baro_reference candidate_kind = LGPS_BARO_REF_LOCAL;
	int32_t candidate_alt_mm = 0;
	float accepted_sigma = 0.0F;
	bool changed = false;
	bool open;

	k_mutex_lock(&baro_lock, K_FOREVER);

	baro.pressure_valid = pressure_valid;
	if (pressure_valid) {
		baro.pressure_pa = pressure_pa;

		if (baro.pressure_avg_valid) {
			baro.pressure_avg += PRESSURE_AVG_ALPHA * (pressure_pa - baro.pressure_avg);
		} else {
			baro.pressure_avg = pressure_pa;
			baro.pressure_avg_valid = true;
		}
	}
	refresh_alt_locked();

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
			const bool msl = gnss_height_usable(fix);
			const float reference_pa =
				msl ? lgps_baro_sealevel_pressure_pa(baro.window_mean, fix->hmsl_mm)
				    : baro.window_mean;
			const uint32_t reference_q4 = pa_q4_from_pa(reference_pa);

			/* Hysteresis: only move the reference when it really moved. */
			const uint32_t diff = (reference_q4 > baro.baseline_pa_q4)
						      ? (reference_q4 - baro.baseline_pa_q4)
						      : (baro.baseline_pa_q4 - reference_q4);

			if (pa_q4_in_range(reference_q4) &&
			    (!baro.baseline_set ||
			     (diff > (LGPS_BARO_AUTO_REBASE_MIN_PA * 16U)))) {
				apply_reference_locked(reference_q4,
						       msl ? LGPS_BARO_REF_MSL : LGPS_BARO_REF_LOCAL,
						       msl ? fix->hmsl_mm : 0, true);
				candidate_q4 = reference_q4;
				candidate_kind = msl ? LGPS_BARO_REF_MSL : LGPS_BARO_REF_LOCAL;
				candidate_alt_mm = msl ? fix->hmsl_mm : 0;
				accepted_sigma = sigma;
				changed = true;
			}
		}

		window_reset_locked();
	}

	k_mutex_unlock(&baro_lock);

	if (changed) {
		LOG_INF("barometric reference from GNSS gate: %u Pa (%u Pa*16, %s, %u samples, "
			"sigma %.1f Pa, hMSL %d mm)",
			(unsigned int)(candidate_q4 / 16U), (unsigned int)candidate_q4,
			(candidate_kind == LGPS_BARO_REF_MSL) ? "sea level" : "local",
			(unsigned int)LGPS_BARO_AUTO_WINDOW_SAMPLES, (double)accepted_sigma,
			candidate_alt_mm);
		persist(candidate_q4, true, candidate_kind, candidate_alt_mm);
	}

	return changed;
}
