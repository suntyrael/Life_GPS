/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - sensor-to-body-frame alignment, ZUPT and PDR maths.
 *
 * Inputs and outputs are plain floats in SI/engineering units so the module is
 * testable on native_sim without any device tree or driver.
 *
 * Sensor-to-body alignment (measured on Main_V0.2, see lgps_nav_body_from_imu()):
 *
 *     [ 0 -1  0 ]   X_body = -Y_sensor      X_body: device right side
 *     [-1  0  0 ]   Y_body = -X_sensor      Y_body: device front / top, direction of travel
 *     [ 0  0 -1 ]   Z_body = -Z_sensor      Z_body: front-face normal, pointing out
 *
 * The frame is right handed: X x Y = Z, and det(R) = +1 is a hard requirement -
 * a rigid sensor mount can only produce a proper rotation, and only a proper one
 * transforms vectors (accelerometer, magnetometer) and axial vectors (gyroscope
 * angular rate) with the same matrix. A matrix with det = -1 silently inverts the
 * gyroscope's sense of rotation relative to gravity; that is what the earlier
 * (README 1.3/1.4) matrix did, and a three-placement bench test exposed it.
 *
 * Heading is measured from +Y_body, clockwise seen from +Z_body (compass style).
 *
 * Thresholds are the ones frozen in README section 7.5 (ZUPT) and 7.4 (magnetic
 * anomaly rejection); they are not tunable constants of this module.
 */

#ifndef LIFEGPS_NAV_H_
#define LIFEGPS_NAV_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * ZUPT window: the LSM6DSR ODR set is 12.5 / 26 / 52 / 104 / 208 Hz, so there is
 * no 20 Hz channel. 26 Hz is the closest native rate; 26 samples cover
 * 26 x (1 / 26 s) = 1.000 s using the same N / fs convention as README 7.5, so the
 * variance thresholds stay valid without any decimation or window distortion.
 */
#define LGPS_ZUPT_WINDOW_SAMPLES 26U

/* Stationary thresholds and dwell time (README 7.5). */
#define LGPS_ZUPT_ACCEL_VARIANCE_MAX_G2 0.005F
#define LGPS_ZUPT_GYRO_NORM_MAX_RPS 0.08F
#define LGPS_ZUPT_DWELL_TIME_MS 10000U

/* Magnetic anomaly rejection: relative |B| deviation limit (README 8.4 -> 7.x). */
#define LGPS_MAG_ANOMALY_RATIO_MAX 0.30F

/* Weinberg step-length model exponent and factory prior (README 7.2). */
#define LGPS_WEINBERG_EXPONENT 0.25F
#define LGPS_WEINBERG_K_PRIOR 0.415F

/*
 * Rotate a sensor-frame vector into the body frame.
 *
 * Accepts any 3-vector: the accelerometer and the magnetometer (true vectors) and
 * the gyroscope angular rate (axial vector) all use this same proper rotation.
 * The gyroscope must be fed deg/s -> rad/s by the caller.
 */
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
 * Push one sample (window is exactly 1.0 s at the 26 Hz ODR). @p accel_g and @p gyro_rps are
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

/* --- attitude estimation (attitude.c) -------------------------------------- */

/* Gravity correction gain in 1/s: about one second to converge at 26 Hz, well
 * inside the accelerometer's own noise bandwidth.
 */
#define LGPS_ATT_KP_PER_S 1.5F

/* A sample outside this window is not gravity: the device is being moved. */
#define LGPS_ATT_ACCEL_NORM_MIN_G 0.60F
#define LGPS_ATT_ACCEL_NORM_MAX_G 1.40F

/* Longest integration step accepted; anything bigger means a stalled loop. */
#define LGPS_ATT_MAX_DT_S 0.25F

/* Gyro bias tracking rate while the device is stationary (ZUPT locked). */
#define LGPS_GYRO_BIAS_ALPHA 0.02F

/* Below this horizontal field strength a magnetometer cannot give a heading. */
#define LGPS_MAG_HORIZONTAL_MIN_UT 5.0F

/*
 * Attitude estimate. The quaternion maps body -> world (X east, Y north, Z up),
 * so lgps_attitude_heading_rad() is the compass angle of the body +Y axis.
 */
struct lgps_attitude {
	float q[4];                 /* w, x, y, z */
	float gyro_bias_rps[3];
	float mag_yaw_offset_rad;   /* learned from GNSS course, see below */
	bool valid;
};

/* Start from the measured gravity, so roll/pitch are right immediately. */
void lgps_attitude_reset(struct lgps_attitude *att, const float accel_g[3]);

/*
 * One IMU sample. @p accel_g and @p gyro_rps are body-frame vectors, @p dt_s is
 * the time since the previous call.
 */
void lgps_attitude_update(struct lgps_attitude *att, const float accel_g[3],
			  const float gyro_rps[3], float dt_s);

/* Track the gyro bias while the device is known to be stationary. */
void lgps_attitude_accumulate_bias(struct lgps_attitude *att, const float gyro_rps[3]);

/* Compass heading of the body +Y axis, radians, 0 = north, clockwise positive. */
float lgps_attitude_heading_rad(const struct lgps_attitude *att);

/* Rotate the estimate so its heading becomes @p heading_rad (GNSS course). */
void lgps_attitude_align_heading(struct lgps_attitude *att, float heading_rad);

/* roll / pitch / heading in degrees, in that order. */
void lgps_attitude_euler_deg(const struct lgps_attitude *att, float euler_deg[3]);

/* Quaternion in Q15 (32767 = 1.0) for the BLE telemetry packet. */
void lgps_attitude_quat_q15(const struct lgps_attitude *att, int16_t q_q15[4]);

/*
 * Nudge yaw towards magnetic north. @p gain in (0, 1] is the fraction of the
 * measured error applied per call, so a noisy field can be smoothed by calling it
 * repeatedly with a small gain.
 */
void lgps_attitude_correct_from_mag(struct lgps_attitude *att, const float mag_body_ut[3],
				    float gain);

/*
 * Learn the magnetometer's yaw offset, so the magnetic heading stays usable after
 * the GNSS fix is lost (the sensor is not calibrated; the offset absorbs
 * hard/soft iron and local declination).
 *
 * Call it only while the estimate is yaw-aligned to a GNSS course, i.e. right
 * after lgps_attitude_align_heading(course): with the estimate aligned, whatever
 * angle the field still reports is the sensor's own offset.
 */
void lgps_attitude_learn_mag_offset(struct lgps_attitude *att, const float mag_body_ut[3],
				    float alpha);

/* --- step detection (attitude.c) ------------------------------------------- */

/* Low-pass coefficient at 26 Hz, roughly a 3 Hz corner. */
#define LGPS_STEP_LP_ALPHA 0.45F

/* Peak / trough thresholds on the low-passed |a| - 1 g signal. */
#define LGPS_STEP_PEAK_G 0.10F

/* Minimum peak-to-trough swing that counts as a step (Weinberg's delta_a). */
#define LGPS_STEP_MIN_DELTA_G 0.15F

/* Physiological limits: 3.8 steps/s and "a peak this old is stale". */
#define LGPS_STEP_MIN_INTERVAL_MS 260U
#define LGPS_STEP_MAX_INTERVAL_MS 2000U

struct lgps_step {
	float lp;              /* low-passed vertical acceleration, g */
	float peak;
	float trough;
	uint32_t peak_ms;
	uint32_t last_step_ms;
	uint8_t phase;         /* 0 idle / 1 tracking peak / 2 tracking trough */
};

void lgps_step_reset(struct lgps_step *step, uint32_t now_ms);

/*
 * Push one |accel| sample (body frame, g). Returns true when a step completed and
 * writes the peak-to-trough swing (Weinberg delta_a) to @p delta_accel_g.
 */
bool lgps_step_update(struct lgps_step *step, float accel_norm_g, uint32_t now_ms,
		      float *delta_accel_g);

#endif /* LIFEGPS_NAV_H_ */
