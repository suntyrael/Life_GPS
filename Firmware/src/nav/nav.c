/*
 * Life_GPS firmware - sensor-to-body-frame alignment, ZUPT and PDR maths.
 *
 * Pure arithmetic only: no device tree, no drivers, no logging, no global
 * mutable state, so every entry point is safe to call from an ISR. All
 * thresholds and constants come from the frozen header (README 1.3/1.4,
 * 7.2, 7.5 and the magnetic anomaly rejection rule); none are redefined here.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lifegps/nav.h>

#include <float.h>
#include <math.h>
#include <string.h>

/*
 * True for finite values only. Implemented without isfinite() so the module
 * stays free of math-library helpers beyond sqrtf()/fabsf().
 */
static bool lgps_float_is_finite(float value)
{
	return (value == value) && (fabsf(value) <= FLT_MAX);
}

void lgps_nav_body_from_imu(const float in[3], float out[3])
{
	if ((in == NULL) || (out == NULL)) {
		return;
	}

	/* X_body = -Y_sensor, Y_body = +X_sensor, Z_body = -Z_sensor. */
	out[0] = -in[1];
	out[1] = in[0];
	out[2] = -in[2];
}

void lgps_nav_body_from_mag(const float in[3], float out[3])
{
	/* The magnetometer is mounted 1:1 with the IMU, so the matrix is shared. */
	lgps_nav_body_from_imu(in, out);
}

void lgps_zupt_reset(struct lgps_zupt *zupt, uint32_t now_ms)
{
	if (zupt == NULL) {
		return;
	}

	memset(zupt, 0, sizeof(*zupt));
	zupt->quiet_since_ms = now_ms;
}

float lgps_zupt_accel_variance_g2(const struct lgps_zupt *zupt)
{
	uint8_t samples;
	uint8_t index;
	float mean_g = 0.0F;
	float sum_sq = 0.0F;

	if (zupt == NULL) {
		return 0.0F;
	}

	/* Only the filled part of the ring buffer is meaningful. */
	samples = zupt->fill;
	if (samples == 0U) {
		return 0.0F;
	}

	for (index = 0U; index < samples; index++) {
		mean_g += zupt->accel_norm_g[index];
	}
	mean_g /= (float)samples;

	for (index = 0U; index < samples; index++) {
		float delta_g = zupt->accel_norm_g[index] - mean_g;

		sum_sq += delta_g * delta_g;
	}

	return sum_sq / (float)samples;
}

void lgps_zupt_update(struct lgps_zupt *zupt, const float accel_g[3], const float gyro_rps[3],
		      uint32_t now_ms)
{
	uint32_t now = now_ms;
	float accel_sq_g2;
	float gyro_sq_rps2;
	bool was_quiet;
	bool is_quiet;

	if ((zupt == NULL) || (accel_g == NULL) || (gyro_rps == NULL)) {
		return;
	}

	accel_sq_g2 = (accel_g[0] * accel_g[0]) + (accel_g[1] * accel_g[1]) +
		      (accel_g[2] * accel_g[2]);
	zupt->accel_norm_g[zupt->head] = sqrtf(accel_sq_g2);
	zupt->head = (uint8_t)((zupt->head + 1U) % LGPS_ZUPT_WINDOW_SAMPLES);
	if (zupt->fill < LGPS_ZUPT_WINDOW_SAMPLES) {
		zupt->fill++;
	}

	gyro_sq_rps2 = (gyro_rps[0] * gyro_rps[0]) + (gyro_rps[1] * gyro_rps[1]) +
		       (gyro_rps[2] * gyro_rps[2]);
	zupt->gyro_norm_rps = sqrtf(gyro_sq_rps2);

	was_quiet = zupt->quiet;
	is_quiet = (lgps_zupt_accel_variance_g2(zupt) < LGPS_ZUPT_ACCEL_VARIANCE_MAX_G2) &&
		   (zupt->gyro_norm_rps < LGPS_ZUPT_GYRO_NORM_MAX_RPS);
	zupt->quiet = is_quiet;

	if (!is_quiet) {
		/* Motion breaks the lock immediately and restarts the dwell timer. */
		zupt->quiet_since_ms = now;
		zupt->locked = false;
	} else if (!was_quiet) {
		zupt->quiet_since_ms = now;
	} else if ((uint32_t)(now - zupt->quiet_since_ms) >= LGPS_ZUPT_DWELL_TIME_MS) {
		zupt->locked = true;
	}
}

bool lgps_zupt_locked(const struct lgps_zupt *zupt)
{
	if (zupt == NULL) {
		return false;
	}

	return zupt->locked;
}

float lgps_weinberg_step_m(float k, float delta_accel_g)
{
	if (!lgps_float_is_finite(k) || !lgps_float_is_finite(delta_accel_g)) {
		return 0.0F;
	}
	if ((k <= 0.0F) || (delta_accel_g <= 0.0F)) {
		return 0.0F;
	}

	/*
	 * The header freezes LGPS_WEINBERG_EXPONENT at 0.25; x^0.25 is computed
	 * as sqrtf(sqrtf(x)) so the module needs no powf() (soft-float build).
	 */
	return k * sqrtf(sqrtf(delta_accel_g));
}

float lgps_nav_mag_anomaly_ratio(float measured_ut, float reference_ut)
{
	float reference_magnitude_ut = fabsf(reference_ut);
	float measured_magnitude_ut = fabsf(measured_ut);

	if (!lgps_float_is_finite(reference_magnitude_ut) ||
	    !lgps_float_is_finite(measured_magnitude_ut) ||
	    (reference_magnitude_ut <= 0.0F)) {
		/* Uncalibrated reference: report no deviation instead of dividing by 0. */
		return 0.0F;
	}

	return fabsf(measured_magnitude_ut - reference_magnitude_ut) / reference_magnitude_ut;
}

bool lgps_nav_mag_anomaly(float measured_ut, float reference_ut)
{
	return lgps_nav_mag_anomaly_ratio(measured_ut, reference_ut) > LGPS_MAG_ANOMALY_RATIO_MAX;
}

void lgps_pdr_init(struct lgps_pdr *pdr)
{
	if (pdr == NULL) {
		return;
	}

	pdr->k = LGPS_WEINBERG_K_PRIOR;
	pdr->position_x_m = 0.0F;
	pdr->position_y_m = 0.0F;
	pdr->steps_total = 0U;
	pdr->frozen = false;
}

void lgps_pdr_set_k(struct lgps_pdr *pdr, float k)
{
	if (pdr == NULL) {
		return;
	}
	if (!lgps_float_is_finite(k) || (k <= 0.0F)) {
		return;
	}

	pdr->k = k;
}

float lgps_pdr_step(struct lgps_pdr *pdr, float delta_accel_g, float heading_rad)
{
	float step_m;

	if (pdr == NULL) {
		return 0.0F;
	}
	if (pdr->frozen) {
		/* ZUPT lock: the step is dropped, the position is left untouched. */
		return 0.0F;
	}

	step_m = lgps_weinberg_step_m(pdr->k, delta_accel_g);

	/* Heading is measured from +Y body, clockwise positive. */
	pdr->position_x_m += step_m * sinf(heading_rad);
	pdr->position_y_m += step_m * cosf(heading_rad);
	pdr->steps_total++;

	return step_m;
}

void lgps_pdr_set_frozen(struct lgps_pdr *pdr, bool frozen)
{
	if (pdr == NULL) {
		return;
	}

	/* Position and accumulated steps are preserved across the lock. */
	pdr->frozen = frozen;
}

bool lgps_pdr_is_frozen(const struct lgps_pdr *pdr)
{
	if (pdr == NULL) {
		return false;
	}

	return pdr->frozen;
}
