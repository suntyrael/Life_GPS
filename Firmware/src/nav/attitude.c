/*
 * Life_GPS firmware - attitude estimation and step detection.
 *
 * Split out of nav.c so the PDR/interaction maths stays readable on its own, but
 * still pure arithmetic: no drivers, no logging, no allocation, no global state,
 * so it compiles and runs on native_sim (Firmware/tests/lifegps.nav).
 *
 * Frames and angles (frozen in lifegps/nav.h):
 *   body  : X right, Y forward, Z out of the front face (det = +1)
 *   world : X east, Y north, Z up
 *   heading: compass angle of the body +Y axis, atan2(east, north), 0 = north,
 *            growing clockwise seen from above - so a positive rotation of the
 *            device towards the east *increases* the heading, while a positive
 *            gyro rate about +Z_body (right-handed, i.e. turning towards the
 *            west) *decreases* it. That sign is asserted in the unit tests
 *            because getting it wrong silently mirrors every trail.
 *
 * The accelerometer measures the reaction to gravity, so at rest the normalised
 * accelerometer vector is the world +Z axis expressed in the body frame - that
 * is what the complementary filter corrects against.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <lifegps/nav.h>

/* M_PI is POSIX, not ISO C; spell it out so the module needs no feature macros. */
#define LGPS_PI 3.14159265358979323846F

/* --- small quaternion helpers ---------------------------------------------- */

static float clampf(float value, float low, float high)
{
	if (value < low) {
		return low;
	}

	if (value > high) {
		return high;
	}

	return value;
}

/* Wrap an angle into (-pi, pi]. */
static float wrap_pi(float angle)
{
	while (angle > LGPS_PI) {
		angle -= 2.0F * LGPS_PI;
	}

	while (angle <= -LGPS_PI) {
		angle += 2.0F * LGPS_PI;
	}

	return angle;
}

static void quat_normalize(float q[4])
{
	float norm = sqrtf((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));

	if (norm < 1e-6F) {
		q[0] = 1.0F;
		q[1] = 0.0F;
		q[2] = 0.0F;
		q[3] = 0.0F;

		return;
	}

	for (uint8_t index = 0U; index < 4U; index++) {
		q[index] /= norm;
	}
}

/* out = a * b (Hamilton product, applying b first then a). */
static void quat_mul(const float a[4], const float b[4], float out[4])
{
	float result[4];

	result[0] = (a[0] * b[0]) - (a[1] * b[1]) - (a[2] * b[2]) - (a[3] * b[3]);
	result[1] = (a[0] * b[1]) + (a[1] * b[0]) + (a[2] * b[3]) - (a[3] * b[2]);
	result[2] = (a[0] * b[2]) - (a[1] * b[3]) + (a[2] * b[0]) + (a[3] * b[1]);
	result[3] = (a[0] * b[3]) + (a[1] * b[2]) - (a[2] * b[1]) + (a[3] * b[0]);

	memcpy(out, result, sizeof(result));
}

/* v_world = R(q) * v_body */
static void quat_rotate(const float q[4], const float v[3], float out[3])
{
	float x = q[1];
	float y = q[2];
	float z = q[3];
	float w = q[0];
	float tv[3];
	float cross[3];

	cross[0] = (y * v[2]) - (z * v[1]);
	cross[1] = (z * v[0]) - (x * v[2]);
	cross[2] = (x * v[1]) - (y * v[0]);

	tv[0] = 2.0F * cross[0];
	tv[1] = 2.0F * cross[1];
	tv[2] = 2.0F * cross[2];

	out[0] = v[0] + (w * tv[0]) + ((y * tv[2]) - (z * tv[1]));
	out[1] = v[1] + (w * tv[1]) + ((z * tv[0]) - (x * tv[2]));
	out[2] = v[2] + (w * tv[2]) + ((x * tv[1]) - (y * tv[0]));
}

/* v_body = R(q)^T * v_world */
static void quat_rotate_inverse(const float q[4], const float v[3], float out[3])
{
	float inverse[4] = { q[0], -q[1], -q[2], -q[3] };

	quat_rotate(inverse, v, out);
}

/* Rotation matrix R (body -> world), row major: out[row][col]. */
static void quat_to_matrix(const float q[4], float out[3][3])
{
	float w = q[0];
	float x = q[1];
	float y = q[2];
	float z = q[3];

	out[0][0] = 1.0F - (2.0F * ((y * y) + (z * z)));
	out[0][1] = 2.0F * ((x * y) - (w * z));
	out[0][2] = 2.0F * ((x * z) + (w * y));
	out[1][0] = 2.0F * ((x * y) + (w * z));
	out[1][1] = 1.0F - (2.0F * ((x * x) + (z * z)));
	out[1][2] = 2.0F * ((y * z) - (w * x));
	out[2][0] = 2.0F * ((x * z) - (w * y));
	out[2][1] = 2.0F * ((y * z) + (w * x));
	out[2][2] = 1.0F - (2.0F * ((x * x) + (y * y)));
}

/* Shortest-arc rotation that maps unit vector @p from onto unit vector @p to. */
static void quat_from_two_vectors(const float from[3], const float to[3], float q[4])
{
	float dot = (from[0] * to[0]) + (from[1] * to[1]) + (from[2] * to[2]);

	if (dot < -0.999999F) {
		/* Antiparallel: any axis perpendicular to @p from works. */
		float axis[3] = { 1.0F, 0.0F, 0.0F };

		if (fabsf(from[0]) > fabsf(from[1])) {
			axis[0] = -from[2];
			axis[1] = 0.0F;
			axis[2] = from[0];
		} else {
			axis[0] = 0.0F;
			axis[1] = from[2];
			axis[2] = -from[1];
		}

		q[0] = 0.0F;
		q[1] = axis[0];
		q[2] = axis[1];
		q[3] = axis[2];
		quat_normalize(q);

		return;
	}

	q[0] = 1.0F + dot;
	q[1] = (from[1] * to[2]) - (from[2] * to[1]);
	q[2] = (from[2] * to[0]) - (from[0] * to[2]);
	q[3] = (from[0] * to[1]) - (from[1] * to[0]);
	quat_normalize(q);
}

/* q = qz(angle) * q  - a yaw change expressed in the world frame. */
static void quat_yaw_world(float q[4], float angle)
{
	float half = angle * 0.5F;
	float yaw[4] = { cosf(half), 0.0F, 0.0F, sinf(half) };
	float result[4];

	quat_mul(yaw, q, result);
	memcpy(q, result, sizeof(result));
	quat_normalize(q);
}

static bool accel_is_gravity(const float accel_g[3], float unit[3])
{
	float norm = sqrtf((accel_g[0] * accel_g[0]) + (accel_g[1] * accel_g[1]) +
			   (accel_g[2] * accel_g[2]));

	if ((norm < LGPS_ATT_ACCEL_NORM_MIN_G) || (norm > LGPS_ATT_ACCEL_NORM_MAX_G)) {
		return false;
	}

	unit[0] = accel_g[0] / norm;
	unit[1] = accel_g[1] / norm;
	unit[2] = accel_g[2] / norm;

	return true;
}

/* --- attitude -------------------------------------------------------------- */

void lgps_attitude_reset(struct lgps_attitude *att, const float accel_g[3])
{
	float unit[3];
	float world_up[3] = { 0.0F, 0.0F, 1.0F };

	if (att == NULL) {
		return;
	}

	memset(att, 0, sizeof(*att));
	att->q[0] = 1.0F;

	if ((accel_g != NULL) && accel_is_gravity(accel_g, unit)) {
		/* Start with roll/pitch already right, so the first second of gyro
		 * integration does not fight a 180 degree error.
		 */
		quat_from_two_vectors(unit, world_up, att->q);
		quat_normalize(att->q);
		att->valid = true;
	}
}

void lgps_attitude_update(struct lgps_attitude *att, const float accel_g[3], const float gyro_rps[3],
			  float dt_s)
{
	float unit[3];
	float world_up[3] = { 0.0F, 0.0F, 1.0F };
	float predicted[3];
	float omega[3];
	float half[4];
	float result[4];

	if ((att == NULL) || (gyro_rps == NULL)) {
		return;
	}

	dt_s = clampf(dt_s, 0.0F, LGPS_ATT_MAX_DT_S);
	if (dt_s <= 0.0F) {
		return;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		omega[axis] = gyro_rps[axis] - att->gyro_bias_rps[axis];
	}

	if ((accel_g != NULL) && accel_is_gravity(accel_g, unit)) {
		att->valid = true;

		/* Where the filter thinks "up" is, expressed in the body frame. */
		quat_rotate_inverse(att->q, world_up, predicted);

		/* Cross product of measured and predicted up is the rotation error
		 * (small-angle), proportional correction keeps roll/pitch bounded.
		 */
		float error[3];

		error[0] = (unit[1] * predicted[2]) - (unit[2] * predicted[1]);
		error[1] = (unit[2] * predicted[0]) - (unit[0] * predicted[2]);
		error[2] = (unit[0] * predicted[1]) - (unit[1] * predicted[0]);

		for (uint8_t axis = 0U; axis < 3U; axis++) {
			omega[axis] += LGPS_ATT_KP_PER_S * error[axis];
		}
	}

	/* q <- q * exp(0.5 * omega * dt) */
	half[0] = 1.0F;
	half[1] = 0.5F * omega[0] * dt_s;
	half[2] = 0.5F * omega[1] * dt_s;
	half[3] = 0.5F * omega[2] * dt_s;
	quat_normalize(half);
	quat_mul(att->q, half, result);
	memcpy(att->q, result, sizeof(result));
	quat_normalize(att->q);
}

void lgps_attitude_accumulate_bias(struct lgps_attitude *att, const float gyro_rps[3])
{
	if ((att == NULL) || (gyro_rps == NULL)) {
		return;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		att->gyro_bias_rps[axis] +=
			LGPS_GYRO_BIAS_ALPHA * (gyro_rps[axis] - att->gyro_bias_rps[axis]);
	}
}

float lgps_attitude_heading_rad(const struct lgps_attitude *att)
{
	float forward_body[3] = { 0.0F, 1.0F, 0.0F };
	float forward_world[3];

	if (att == NULL) {
		return 0.0F;
	}

	quat_rotate(att->q, forward_body, forward_world);

	return atan2f(forward_world[0], forward_world[1]);
}

void lgps_attitude_align_heading(struct lgps_attitude *att, float heading_rad)
{
	float delta;

	if (att == NULL) {
		return;
	}

	delta = wrap_pi(heading_rad - lgps_attitude_heading_rad(att));

	/* Rotating the estimate by +angle about world +Z decreases the compass
	 * heading by the same angle, hence the sign flip.
	 */
	quat_yaw_world(att->q, -delta);
}

void lgps_attitude_euler_deg(const struct lgps_attitude *att, float euler_deg[3])
{
	float matrix[3][3];
	const float rad_to_deg = 57.2957795F;

	if ((att == NULL) || (euler_deg == NULL)) {
		return;
	}

	quat_to_matrix(att->q, matrix);

	euler_deg[0] = atan2f(-matrix[2][0], matrix[2][2]) * rad_to_deg; /* roll */
	euler_deg[1] = asinf(clampf(matrix[2][1], -1.0F, 1.0F)) * rad_to_deg; /* pitch */
	euler_deg[2] = atan2f(matrix[0][1], matrix[1][1]) * rad_to_deg; /* heading */
}

void lgps_attitude_quat_q15(const struct lgps_attitude *att, int16_t q_q15[4])
{
	if ((att == NULL) || (q_q15 == NULL)) {
		return;
	}

	for (uint8_t index = 0U; index < 4U; index++) {
		float scaled = clampf(att->q[index], -1.0F, 1.0F) * 32767.0F;

		q_q15[index] = (int16_t)scaled;
	}
}

void lgps_attitude_correct_from_mag(struct lgps_attitude *att, const float mag_body_ut[3],
				    float gain)
{
	float field_world[3];
	float horizontal;

	if ((att == NULL) || (mag_body_ut == NULL)) {
		return;
	}

	quat_rotate(att->q, mag_body_ut, field_world);
	horizontal = sqrtf((field_world[0] * field_world[0]) +
			   (field_world[1] * field_world[1]));

	if (horizontal < LGPS_MAG_HORIZONTAL_MIN_UT) {
		return;
	}

	/* The horizontal field points at magnetic north, i.e. world +Y, so any
	 * residual angle is the yaw error of the estimate. The learned offset
	 * absorbs the uncalibrated sensor (hard/soft iron) and local declination.
	 */
	float error = wrap_pi(atan2f(field_world[0], field_world[1]) - att->mag_yaw_offset_rad);

	quat_yaw_world(att->q, clampf(gain, 0.0F, 1.0F) * error);
}

void lgps_attitude_learn_mag_offset(struct lgps_attitude *att, const float mag_body_ut[3],
				    float alpha)
{
	float field_world[3];
	float horizontal;
	float measured;

	if ((att == NULL) || (mag_body_ut == NULL)) {
		return;
	}

	quat_rotate(att->q, mag_body_ut, field_world);
	horizontal = sqrtf((field_world[0] * field_world[0]) +
			   (field_world[1] * field_world[1]));

	if (horizontal < LGPS_MAG_HORIZONTAL_MIN_UT) {
		return;
	}

	/* With the estimate yaw-aligned to the GNSS course, this angle is the
	 * sensor's own rotation relative to magnetic north; a perfect, calibrated
	 * magnetometer would report exactly zero.
	 */
	measured = wrap_pi(atan2f(field_world[0], field_world[1]));
	att->mag_yaw_offset_rad = wrap_pi(att->mag_yaw_offset_rad +
					  (clampf(alpha, 0.0F, 1.0F) *
					   wrap_pi(measured - att->mag_yaw_offset_rad)));
}

/* --- step detection -------------------------------------------------------- */

void lgps_step_reset(struct lgps_step *step, uint32_t now_ms)
{
	if (step == NULL) {
		return;
	}

	memset(step, 0, sizeof(*step));
	step->last_step_ms = now_ms;
}

bool lgps_step_update(struct lgps_step *step, float accel_norm_g, uint32_t now_ms,
		      float *delta_accel_g)
{
	float deviation;
	bool stepped = false;

	if (step == NULL) {
		return false;
	}

	/* High-pass by subtracting 1 g, then low-pass: the remaining signal is the
	 * vertical body motion of walking, band-limited to a few Hz so a single
	 * spike from handling the board does not count as a step.
	 */
	deviation = accel_norm_g - 1.0F;
	step->lp += LGPS_STEP_LP_ALPHA * (deviation - step->lp);

	if (step->phase == 0U) {
		if (step->lp > LGPS_STEP_PEAK_G) {
			step->peak = step->lp;
			step->peak_ms = now_ms;
			step->phase = 1U;
		}

		return false;
	}

	if (step->phase == 1U) {
		if (step->lp > step->peak) {
			step->peak = step->lp;
			step->peak_ms = now_ms;
		}

		if (step->lp < 0.0F) {
			step->trough = step->lp;
			step->phase = 2U;
		} else if ((uint32_t)(now_ms - step->peak_ms) > LGPS_STEP_MAX_INTERVAL_MS) {
			step->phase = 0U;
		}

		return false;
	}

	/* phase 2: waiting for the signal to come back through zero. */
	if (step->lp < step->trough) {
		step->trough = step->lp;
	}

	if (step->lp > 0.0F) {
		float delta = step->peak - step->trough;

		if ((delta >= LGPS_STEP_MIN_DELTA_G) &&
		    ((uint32_t)(now_ms - step->last_step_ms) >= LGPS_STEP_MIN_INTERVAL_MS)) {
			if (delta_accel_g != NULL) {
				*delta_accel_g = delta;
			}

			step->last_step_ms = now_ms;
			stepped = true;
		}

		step->phase = 0U;
	} else if ((uint32_t)(now_ms - step->peak_ms) > LGPS_STEP_MAX_INTERVAL_MS) {
		step->phase = 0U;
	}

	return stepped;
}
