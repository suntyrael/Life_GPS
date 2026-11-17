/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - sensor-to-body-frame alignment, ZUPT and PDR maths.
 *
 * Inputs and outputs are plain floats in SI/engineering units so the module is
 * testable on native_sim without any device tree or driver.
 *
 * Rotation matrices are the ones frozen in README section 1.3 / 1.4 (the PCB
 * places LSM6DSV16X sideways and LIS2MDL rotated by 90 degrees, which makes both
 * matrices identical):
 *
 *     [ 0 -1  0 ]   X_body = -Y_sensor
 *     [ 1  0  0 ]   Y_body = +X_sensor
 *     [ 0  0 -1 ]   Z_body = -Z_sensor
 *
 * Thresholds are the ones frozen in README section 7.5 (ZUPT) and 7.4 (magnetic
 * anomaly rejection); they are not tunable constants of this module.
 */

#ifndef LIFEGPS_NAV_H_
#define LIFEGPS_NAV_H_

#include <stdbool.h>
#include <stdint.h>

/* ZUPT window length: 20 samples at 20 Hz = 1.0 s (README 7.5). */
#define LGPS_ZUPT_WINDOW_SAMPLES 20U

/* Stationary thresholds and dwell time (README 7.5). */
#define LGPS_ZUPT_ACCEL_VARIANCE_MAX_G2 0.005F
#define LGPS_ZUPT_GYRO_NORM_MAX_RPS 0.08F
#define LGPS_ZUPT_DWELL_TIME_MS 10000U

/* Magnetic anomaly rejection: relative |B| deviation limit (README 8.4 -> 7.x). */
#define LGPS_MAG_ANOMALY_RATIO_MAX 0.30F

/* Weinberg step-length model exponent and factory prior (README 7.2). */
#define LGPS_WEINBERG_EXPONENT 0.25F
#define LGPS_WEINBERG_K_PRIOR 0.415F

/* Rotate a sensor-frame vector into the body frame (README 1.3 / 1.4). */
void lgps_nav_body_from_imu(const float in[3], float out[3]);

/* Same matrix for the magnetometer (hardware is aligned 1:1 with the IMU). */
void lgps_nav_body_from_mag(const float in[3], float out[3]);

struct lgps_zupt {
	float accel_norm_g[LGPS_ZUPT_WINDOW_SAMPLES];
	uint8_t fill;
	uint8_t head;
	float gyro_norm_rps;
	uint32_t quiet_since_ms;
	bool quiet;
	bool locked;
};

/* Reset the window; @p now_ms is the timestamp of the next update. */
void lgps_zupt_reset(struct lgps_zupt *zupt, uint32_t now_ms);

/*
 * Push one sample (window is 1.0 s at 20 Hz). @p accel_g and @p gyro_rps are
 * body-frame vectors. Call at the configured IMU sample rate.
 */
void lgps_zupt_update(struct lgps_zupt *zupt, const float accel_g[3], const float gyro_rps[3],
		      uint32_t now_ms);

/* Sample variance of the acceleration norm inside the window, in g^2. */
float lgps_zupt_accel_variance_g2(const struct lgps_zupt *zupt);

/* True once both thresholds held for LGPS_ZUPT_DWELL_TIME_MS. */
bool lgps_zupt_locked(const struct lgps_zupt *zupt);

/* Weinberg step length L = K * (delta_a)^0.25, delta_a in g, L in metres. */
float lgps_weinberg_step_m(float k, float delta_accel_g);

/* Relative deviation of a measured field norm from the calibrated reference. */
float lgps_nav_mag_anomaly_ratio(float measured_ut, float reference_ut);

/* True when the measured field deviates beyond LGPS_MAG_ANOMALY_RATIO_MAX. */
bool lgps_nav_mag_anomaly(float measured_ut, float reference_ut);

struct lgps_pdr {
	float k;              /* Weinberg gain, always > 0 */
	float position_x_m;
	float position_y_m;
	uint32_t steps_total;
	/* True while ZUPT holds the position: no integration, no step counting. */
	bool frozen;
};

void lgps_pdr_init(struct lgps_pdr *pdr);

/* Online recalibration of K from a GNSS-derived step length (least squares hook). */
void lgps_pdr_set_k(struct lgps_pdr *pdr, float k);

/*
 * Integrate one detected step: advance the planar position along @p heading_rad
 * (0 = +Y body, clockwise positive) using the Weinberg step length.
 * Returns the step length in metres.
 */
float lgps_pdr_step(struct lgps_pdr *pdr, float delta_accel_g, float heading_rad);

/* Freeze the position (ZUPT lock): no further integration until unfrozen. */
void lgps_pdr_set_frozen(struct lgps_pdr *pdr, bool frozen);

bool lgps_pdr_is_frozen(const struct lgps_pdr *pdr);

#endif /* LIFEGPS_NAV_H_ */
