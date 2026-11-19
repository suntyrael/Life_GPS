/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - sensor calibration maths and session state.
 *
 * No Zephyr APIs are used here on purpose: everything in this file is a pure
 * function of its arguments plus one static state block, so Firmware/tests/
 * lifegps.calib can drive it on native_sim. The flash backend is reached through
 * lifegps/calib_store.h, which the test replaces with a stub.
 *
 * All register and unit facts come from the datasheets quoted next to the code:
 * LSM6DSR DS11976 Rev 2 (0.122 mg/LSB at +/-4 g, 35 mdps/LSB at +/-1000 dps) and
 * LIS2MDL DS12144 Rev 6 (1.5 mG/LSB = 0.15 uT/LSB).
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <lifegps/calib.h>
#include <lifegps/calib_store.h>
#include <lifegps/crc.h>

/* Accel sensitivity: LGPS_IMU_ACCEL_MG_LSB = 0.122 mg/LSB -> 122 ug/LSB. */
#define ACCEL_UG_PER_LSB 122.0F
#define ACCEL_G_PER_LSB (ACCEL_UG_PER_LSB / 1000000.0F)

/* Gyro sensitivity: 35 mdps/LSB. */
#define GYRO_MDPS_PER_LSB 35.0F

/*
 * Face identification thresholds. These are the numbers frozen in
 * Doc/Software/BLE_Wire_v1.md 6.4 and mirrored by the App's CalibFaces.detect();
 * changing one without the other makes the App's preview disagree with the
 * device, which is exactly the confusion the feature exists to remove.
 */
#define FACE_DOMINANT_MIN_G 0.7F
#define FACE_OFF_AXIS_MAX_G 0.45F

/* Stillness: 0.001 g^2 is a device on a desk, 0.020 g^2 is a hand. */
#define REST_VAR_BEST_G2 0.001F
#define REST_VAR_WORST_G2 0.020F

/* Below this the accumulated samples are not trusted to describe a pose. */
#define REST_PCT_USABLE 70U

#define SIX_FACE_MASK 0x3FU

/* Sanity limits: anything beyond these is a bug, not a measurement. */
#define BIAS_UG_LIMIT 4000000 /* +/-4 g full scale */
#define SCALE_PPM_LIMIT 500000 /* +/-50 % */
#define GYRO_MDPS_LIMIT 1000000 /* +/-1000 dps full scale */

/* --- little-endian helpers (dependency free on purpose) -------------------- */

static void put_u16(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put_u32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value & 0xFFU);
	out[1] = (uint8_t)((value >> 8) & 0xFFU);
	out[2] = (uint8_t)((value >> 16) & 0xFFU);
	out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void put_i16(uint8_t *out, int16_t value)
{
	put_u16(out, (uint16_t)value);
}

static void put_i32(uint8_t *out, int32_t value)
{
	put_u32(out, (uint32_t)value);
}

static uint16_t get_u16(const uint8_t *in)
{
	return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static int16_t get_i16(const uint8_t *in)
{
	return (int16_t)get_u16(in);
}

static int32_t get_i32(const uint8_t *in)
{
	return (int32_t)get_u32(in);
}

/* --- small helpers --------------------------------------------------------- */

static int32_t clamp_i32(int32_t value, int32_t limit)
{
	if (value > limit) {
		return limit;
	}

	if (value < -limit) {
		return -limit;
	}

	return value;
}

static uint8_t clamp_pct(float value)
{
	if (!(value > 0.0F)) { /* also catches NaN */
		return 0U;
	}

	if (value >= 100.0F) {
		return 100U;
	}

	return (uint8_t)value;
}

static int32_t round_i32(float value)
{
	/* Round half away from zero without relying on the current rounding mode. */
	return (int32_t)((value >= 0.0F) ? (value + 0.5F) : (value - 0.5F));
}

/* --- state ----------------------------------------------------------------- */

static struct lgps_calib_coeffs coeffs;
static uint8_t flags = LGPS_CALIB_FLAG_DEFAULTS;
static uint32_t last_calib_unix_s;

/* Last UTC the GNSS supplied; 0 until the first valid fix. */
static uint32_t known_unix_s;

void lgps_calib_note_utc(uint32_t unix_s)
{
	if (unix_s != 0U) {
		known_unix_s = unix_s;
	}
}

static struct {
	uint8_t state;
	uint8_t face_state;
	uint8_t position;
	uint8_t face_mask;
	uint8_t detected_face;
	uint16_t samples;
	float accel_sum_lsb[3];
	float gyro_sum_lsb[3];
	float face_mean_lsb[LGPS_CALIB_POSITION_COUNT][3];
	float face_gyro_mdps[LGPS_CALIB_POSITION_COUNT][3];
	bool face_valid[LGPS_CALIB_POSITION_COUNT];
	/* Single-face quick mode: only the identified axis has a known reference. */
	bool quick_valid;
	int32_t quick_bias_ug[3];
	/* Stillness window. */
	float window_values[LGPS_CALIB_REST_WINDOW];
	float window_sum;
	float window_sumsq;
	uint8_t window_index;
	uint8_t window_count;
	uint8_t rest_pct;
	bool started;
	uint32_t started_ms;
} accel;

static struct {
	uint8_t state;
	uint16_t samples;
	int16_t min_lsb[3];
	int16_t max_lsb[3];
	bool have_sample;
} mag;

/* --- pure helpers ---------------------------------------------------------- */

uint8_t lgps_calib_rest_pct(float variance_g2)
{
	if (!(variance_g2 > 0.0F)) {
		return 100U;
	}

	if (variance_g2 <= REST_VAR_BEST_G2) {
		return 100U;
	}

	if (variance_g2 >= REST_VAR_WORST_G2) {
		return 0U;
	}

	return clamp_pct((REST_VAR_WORST_G2 - variance_g2) * 100.0F /
			 (REST_VAR_WORST_G2 - REST_VAR_BEST_G2));
}

int lgps_calib_detect_face(const float gravity_g[3])
{
	int axis = 0;
	float dominant;
	uint8_t index;

	if (gravity_g == NULL) {
		return LGPS_CALIB_FACE_NONE;
	}

	dominant = fabsf(gravity_g[0]);

	for (index = 1U; index < 3U; index++) {
		float magnitude = fabsf(gravity_g[index]);

		if (magnitude > dominant) {
			dominant = magnitude;
			axis = (int)index;
		}
	}

	if (dominant <= FACE_DOMINANT_MIN_G) {
		return LGPS_CALIB_FACE_NONE;
	}

	for (index = 0U; index < 3U; index++) {
		if ((int)index == axis) {
			continue;
		}

		if (fabsf(gravity_g[index]) > FACE_OFF_AXIS_MAX_G) {
			return LGPS_CALIB_FACE_NONE;
		}
	}

	return (axis * 2) + ((gravity_g[axis] > 0.0F) ? 0 : 1);
}

int lgps_calib_face_math(const float face_mean_lsb[LGPS_CALIB_POSITION_COUNT][3],
			 uint8_t face_mask, int32_t bias_ug[3], int32_t scale_ppm[3])
{
	uint8_t axis;

	if ((face_mean_lsb == NULL) || (bias_ug == NULL) || (scale_ppm == NULL)) {
		return -EINVAL;
	}

	if ((face_mask & SIX_FACE_MASK) != SIX_FACE_MASK) {
		return -EAGAIN;
	}

	for (axis = 0U; axis < 3U; axis++) {
		/* Position 2*axis has the axis pointing up, 2*axis+1 pointing down. */
		const float up_lsb = face_mean_lsb[axis * 2U][axis];
		const float down_lsb = face_mean_lsb[(axis * 2U) + 1U][axis];
		const float bias_lsb = (up_lsb + down_lsb) * 0.5F;
		const float gain_g = (up_lsb - down_lsb) * 0.5F * ACCEL_G_PER_LSB;

		if (!(gain_g > 0.5F) || !(gain_g < 1.5F)) {
			/* A +/- pair that spans less than half a g (or more than 1.5 g)
			 * means one of the two poses was not what it claimed to be.
			 */
			return -EAGAIN;
		}

		bias_ug[axis] = clamp_i32(round_i32(bias_lsb * ACCEL_UG_PER_LSB),
					  BIAS_UG_LIMIT);
		scale_ppm[axis] = clamp_i32(round_i32(((1.0F / gain_g) - 1.0F) * 1000000.0F),
					    SCALE_PPM_LIMIT);
	}

	return 0;
}

int lgps_calib_mag_math(const int16_t min_lsb[3], const int16_t max_lsb[3],
			int16_t bias_lsb[3], int32_t scale_ppm[3], uint8_t coverage_pct[3])
{
	float span_lsb[3];
	float mean_span = 0.0F;
	uint8_t axis;

	if ((min_lsb == NULL) || (max_lsb == NULL) || (bias_lsb == NULL) ||
	    (scale_ppm == NULL) || (coverage_pct == NULL)) {
		return -EINVAL;
	}

	for (axis = 0U; axis < 3U; axis++) {
		const int32_t span = (int32_t)max_lsb[axis] - (int32_t)min_lsb[axis];

		if (span <= 0) {
			/* An axis that never moved carries no information; refusing here is
			 * what stops a "calibration" from a device that sat still.
			 */
			return -EINVAL;
		}

		span_lsb[axis] = (float)span * 0.5F;
		mean_span += span_lsb[axis];
	}

	mean_span /= 3.0F;

	if (!(mean_span > 1.0F)) {
		return -EINVAL;
	}

	for (axis = 0U; axis < 3U; axis++) {
		const int32_t span = (int32_t)max_lsb[axis] - (int32_t)min_lsb[axis];
		const int32_t centre = ((int32_t)max_lsb[axis] + (int32_t)min_lsb[axis]) / 2;

		bias_lsb[axis] = (int16_t)clamp_i32(centre, 32767);
		scale_ppm[axis] = clamp_i32(round_i32(((mean_span / span_lsb[axis]) - 1.0F) *
						      1000000.0F),
					    SCALE_PPM_LIMIT);
		coverage_pct[axis] = clamp_pct((float)span * 100.0F /
					       (float)LGPS_CALIB_MAG_SPAN_TARGET_LSB);
	}

	return 0;
}

/* --- record codec ---------------------------------------------------------- */

int lgps_calib_record_encode(const struct lgps_calib_coeffs *in, uint8_t record_flags,
			     uint32_t seq, uint32_t unix_s,
			     uint8_t out[LGPS_CALIB_RECORD_SIZE])
{
	uint8_t axis;
	uint16_t crc;

	if ((in == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	memset(out, 0, LGPS_CALIB_RECORD_SIZE);
	put_u32(&out[0], LGPS_CALIB_RECORD_MAGIC);
	put_u16(&out[4], (uint16_t)LGPS_CALIB_RECORD_VERSION);
	put_u16(&out[6], (uint16_t)record_flags);
	put_u32(&out[8], seq);

	for (axis = 0U; axis < 3U; axis++) {
		put_i32(&out[12U + (4U * axis)], in->accel_bias_ug[axis]);
		put_i32(&out[24U + (4U * axis)], in->accel_scale_ppm[axis]);
		put_i32(&out[36U + (4U * axis)], in->gyro_bias_mdps[axis]);
		put_i16(&out[48U + (2U * axis)], in->mag_bias_lsb[axis]);
		put_i32(&out[54U + (4U * axis)], in->mag_scale_ppm[axis]);
		out[67U + axis] = in->mag_coverage_pct[axis];
	}

	out[66] = in->accel_face_mask;
	put_u32(&out[72], unix_s);
	put_u32(&out[76], 0xFFFFFFFFU);

	crc = lgps_crc16_ccitt_false(out, 70U);
	put_u16(&out[70], crc);

	return 0;
}

int lgps_calib_record_decode(const uint8_t in[LGPS_CALIB_RECORD_SIZE],
			     struct lgps_calib_coeffs *out, uint8_t *record_flags,
			     uint32_t *seq, uint32_t *unix_s)
{
	uint8_t axis;

	if ((in == NULL) || (out == NULL) || (record_flags == NULL) || (seq == NULL) ||
	    (unix_s == NULL)) {
		return -EINVAL;
	}

	if (get_u32(&in[0]) != LGPS_CALIB_RECORD_MAGIC) {
		return -EBADMSG;
	}

	if (get_u16(&in[4]) != (uint16_t)LGPS_CALIB_RECORD_VERSION) {
		/* A future format must not be interpreted with today's field map. */
		return -EBADMSG;
	}

	if (get_u16(&in[70]) != lgps_crc16_ccitt_false(in, 70U)) {
		return -EBADMSG;
	}

	memset(out, 0, sizeof(*out));

	for (axis = 0U; axis < 3U; axis++) {
		out->accel_bias_ug[axis] = get_i32(&in[12U + (4U * axis)]);
		out->accel_scale_ppm[axis] = get_i32(&in[24U + (4U * axis)]);
		out->gyro_bias_mdps[axis] = get_i32(&in[36U + (4U * axis)]);
		out->mag_bias_lsb[axis] = get_i16(&in[48U + (2U * axis)]);
		out->mag_scale_ppm[axis] = get_i32(&in[54U + (4U * axis)]);
		out->mag_coverage_pct[axis] = in[67U + axis];
	}

	out->accel_face_mask = in[66];
	*record_flags = (uint8_t)get_u16(&in[6]);
	*seq = get_u32(&in[8]);
	*unix_s = get_u32(&in[72]);

	return 0;
}

/* --- applying -------------------------------------------------------------- */

void lgps_calib_apply_imu(struct lgps_imu_sample *sample)
{
	uint8_t axis;

	if (sample == NULL) {
		return;
	}

	for (axis = 0U; axis < 3U; axis++) {
		const float nominal_g = (float)sample->raw_accel[axis] * ACCEL_G_PER_LSB;
		const float gain = 1.0F + ((float)coeffs.accel_scale_ppm[axis] / 1000000.0F);
		const float bias_g = (float)coeffs.accel_bias_ug[axis] / 1000000.0F;

		/* Centre first, then correct the gain. The other order scales the bias
		 * itself by the gain and leaves a bias_g * (1 - 1/gain) residual, which is
		 * 2.3 mg for a 100 mg offset and a 2.4 % gain error - small, but this
		 * correction exists precisely to stop that kind of residual from reaching
		 * the PDR.
		 */
		sample->accel_g[axis] = (nominal_g - bias_g) * gain;
		sample->gyro_dps[axis] =
			((float)sample->raw_gyro[axis] * GYRO_MDPS_PER_LSB / 1000.0F) -
			((float)coeffs.gyro_bias_mdps[axis] / 1000.0F);
	}
}

void lgps_calib_apply_mag(float mag_ut[3])
{
	uint8_t axis;

	if (mag_ut == NULL) {
		return;
	}

	for (axis = 0U; axis < 3U; axis++) {
		/*
		 * The wire and the record carry raw LSB; the conversion to uT happens
		 * here so a single place owns the sensitivity (0.15 uT/LSB).
		 */
		const float raw_lsb = mag_ut[axis] / LGPS_LIS2MDL_UT_PER_LSB;
		const float centred = raw_lsb - (float)coeffs.mag_bias_lsb[axis];
		const float gain = 1.0F + ((float)coeffs.mag_scale_ppm[axis] / 1000000.0F);

		mag_ut[axis] = centred * gain * LGPS_LIS2MDL_UT_PER_LSB;
	}
}

/* --- session helpers ------------------------------------------------------- */

static void accel_reset_session(uint8_t position)
{
	memset(&accel.accel_sum_lsb, 0, sizeof(accel.accel_sum_lsb));
	memset(&accel.gyro_sum_lsb, 0, sizeof(accel.gyro_sum_lsb));
	memset(accel.window_values, 0, sizeof(accel.window_values));
	accel.window_sum = 0.0F;
	accel.window_sumsq = 0.0F;
	accel.window_index = 0U;
	accel.window_count = 0U;
	accel.samples = 0U;
	accel.rest_pct = 0U;
	accel.position = position;
	accel.detected_face = LGPS_CALIB_FACE_NONE;
	accel.started = false;
	accel.started_ms = 0U;
	accel.state = LGPS_CALIB_STATE_COLLECTING;
	accel.face_state = LGPS_CALIB_FACE_WAIT_REST;
}

static void window_push(float value)
{
	float mean;
	float variance;
	float count;

	if (accel.window_count == LGPS_CALIB_REST_WINDOW) {
		const float old = accel.window_values[accel.window_index];

		accel.window_sum -= old;
		accel.window_sumsq -= old * old;
	} else {
		accel.window_count++;
	}

	accel.window_values[accel.window_index] = value;
	accel.window_sum += value;
	accel.window_sumsq += value * value;
	accel.window_index = (uint8_t)((accel.window_index + 1U) % LGPS_CALIB_REST_WINDOW);

	count = (float)accel.window_count;
	mean = accel.window_sum / count;
	variance = (accel.window_sumsq / count) - (mean * mean);

	if (variance < 0.0F) {
		variance = 0.0F;
	}

	accel.rest_pct = lgps_calib_rest_pct(variance);
}

static void accel_store_face(uint8_t face)
{
	float gyro_mdps[3];
	float count = (float)accel.samples;
	uint8_t axis;

	if (face >= LGPS_CALIB_POSITION_COUNT) {
		/* Defensive: a face index is a table index below, and this module is feed
		 * from a value the user's device produces by moving around.
		 */
		return;
	}

	for (axis = 0U; axis < 3U; axis++) {
		accel.face_mean_lsb[face][axis] = accel.accel_sum_lsb[axis] / count;
		gyro_mdps[axis] = (accel.gyro_sum_lsb[axis] / count) * GYRO_MDPS_PER_LSB;
		accel.face_gyro_mdps[face][axis] = gyro_mdps[axis];
	}

	accel.face_valid[face] = true;
	if (accel.position != LGPS_CALIB_POSITION_QUICK) {
		/* A quick capture must not claim a six-face slot: the mask decides which
		 * solve runs, and a mixed set would be extrapolated as if it were full.
		 */
		accel.face_mask |= (uint8_t)(1U << face);
	}
	accel.detected_face = face;
	accel.face_state = LGPS_CALIB_FACE_DONE;
	accel.state = LGPS_CALIB_STATE_IDLE;

	/* The quick mode keeps its own single-face bias instead of a face mean. */
	if (accel.position == LGPS_CALIB_POSITION_QUICK) {
		for (axis = 0U; axis < 3U; axis++) {
			accel.quick_bias_ug[axis] = 0;
		}

		{
			const int expected = (face % 2) ? -1 : 1;

			accel.quick_bias_ug[face / 2] = clamp_i32(
				round_i32(((accel.face_mean_lsb[face][face / 2] *
					    ACCEL_G_PER_LSB) -
					   (float)expected) *
					  1000000.0F),
				BIAS_UG_LIMIT);
		}

		accel.quick_valid = true;
	}
}

/* --- lifecycle ------------------------------------------------------------- */

/*
 * Writes the coefficients back, or erases the record when everything is back at
 * the defaults. Clearing one sensor must not throw away the other's
 * coefficients, so the decision is made once for the whole record.
 */
static int persist_current(void)
{
	bool any = false;
	uint8_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		if ((coeffs.accel_bias_ug[axis] != 0) || (coeffs.accel_scale_ppm[axis] != 0) ||
		    (coeffs.gyro_bias_mdps[axis] != 0) || (coeffs.mag_bias_lsb[axis] != 0) ||
		    (coeffs.mag_scale_ppm[axis] != 0)) {
			any = true;
		}
	}

	if (!any) {
		flags = LGPS_CALIB_FLAG_DEFAULTS;
		return lgps_calib_store_erase();
	}

	flags |= LGPS_CALIB_FLAG_FROM_STORE;
	flags &= (uint8_t)~LGPS_CALIB_FLAG_DEFAULTS;
	flags &= (uint8_t)~LGPS_CALIB_FLAG_SAVE_FAILED;

	if (lgps_calib_store_save(&coeffs, flags, known_unix_s) != 0) {
		flags |= LGPS_CALIB_FLAG_SAVE_FAILED;
		flags &= (uint8_t)~LGPS_CALIB_FLAG_FROM_STORE;
		return -EIO;
	}

	last_calib_unix_s = known_unix_s;

	return 0;
}

static void load_defaults(void)
{
	memset(&coeffs, 0, sizeof(coeffs));
	flags = LGPS_CALIB_FLAG_DEFAULTS;
	last_calib_unix_s = 0U;
}

int lgps_calib_init(void)
{
	struct lgps_calib_coeffs stored;
	uint8_t record_flags = 0U;
	uint32_t seq = 0U;
	uint32_t unix_s = 0U;

	load_defaults();
	memset(&accel, 0, sizeof(accel));
	memset(&mag, 0, sizeof(mag));
	accel.state = LGPS_CALIB_STATE_IDLE;
	accel.face_state = LGPS_CALIB_FACE_WAIT_REST;
	accel.position = LGPS_CALIB_POSITION_AUTO;
	accel.detected_face = LGPS_CALIB_FACE_NONE;
	mag.state = LGPS_CALIB_STATE_IDLE;

	if (lgps_calib_store_load(&stored, &record_flags, &seq, &unix_s) != 0) {
		return 0;
	}

	coeffs = stored;
	last_calib_unix_s = unix_s;
	flags = (uint8_t)(record_flags & (LGPS_CALIB_FLAG_FROM_STORE | LGPS_CALIB_FLAG_QUICK));
	if ((flags & LGPS_CALIB_FLAG_FROM_STORE) == 0U) {
		flags |= LGPS_CALIB_FLAG_DEFAULTS;
	}

	return 0;
}

const struct lgps_calib_coeffs *lgps_calib_current(void)
{
	return &coeffs;
}

void lgps_calib_snapshot(struct lgps_calib_snapshot *out)
{
	uint8_t axis;

	if (out == NULL) {
		return;
	}

	memset(out, 0, sizeof(*out));
	out->coeffs = coeffs;
	out->flags = flags;
	out->last_calib_unix_s = last_calib_unix_s;

	out->accel_state = accel.state;
	out->accel_face_state = accel.face_state;
	out->accel_face_mask = accel.face_mask;
	out->accel_detected_face =
		((accel.detected_face < LGPS_CALIB_POSITION_COUNT) &&
		 accel.face_valid[accel.detected_face])
			? accel.detected_face
			: LGPS_CALIB_FACE_NONE;
	out->accel_rest_pct = accel.rest_pct;
	if (accel.samples >= LGPS_CALIB_FACE_SAMPLES) {
		out->accel_progress_permille = 1000U;
	} else {
		out->accel_progress_permille =
			(uint16_t)(((uint32_t)accel.samples * 1000U) / LGPS_CALIB_FACE_SAMPLES);
	}

	out->mag_state = mag.state;
	out->mag_sample_count = (mag.samples > 255U) ? 255U : (uint8_t)mag.samples;

	for (axis = 0U; axis < 3U; axis++) {
		out->mag_coverage_pct[axis] = 0U;
	}

	if (mag.state == LGPS_CALIB_STATE_COLLECTING && mag.have_sample) {
		int32_t coverage_min = 100;

		for (axis = 0U; axis < 3U; axis++) {
			const int32_t span = (int32_t)mag.max_lsb[axis] - (int32_t)mag.min_lsb[axis];
			int32_t coverage = (span * 100) / (int32_t)LGPS_CALIB_MAG_SPAN_TARGET_LSB;

			if (coverage < 0) {
				coverage = 0;
			} else if (coverage > 100) {
				coverage = 100;
			}

			out->mag_coverage_pct[axis] = (uint8_t)coverage;
			if (coverage < coverage_min) {
				coverage_min = coverage;
			}
		}

		/* The progress bar follows the weakest axis: that is the one holding the
		 * calibration back, so averaging would hide the problem.
		 */
		out->mag_progress_permille = (uint16_t)(coverage_min * 10);
	} else {
		/* Not collecting: report the coverage that produced the stored
		 * coefficients, otherwise the App would judge a finished calibration as
		 * "axis coverage insufficient" the moment it is saved.
		 */
		for (axis = 0U; axis < 3U; axis++) {
			out->mag_coverage_pct[axis] = coeffs.mag_coverage_pct[axis];
		}

		out->mag_progress_permille =
			((flags & LGPS_CALIB_FLAG_FROM_STORE) != 0U) ? 1000U : 0U;
	}
}

/* --- accelerometer session ------------------------------------------------- */

int lgps_calib_accel_start(uint8_t position)
{
	if (position > LGPS_CALIB_POSITION_QUICK && position != LGPS_CALIB_POSITION_AUTO) {
		return -EINVAL;
	}

	if (accel.state == LGPS_CALIB_STATE_COLLECTING) {
		return -EBUSY;
	}

	accel_reset_session(position);

	return 0;
}

int lgps_calib_accel_cancel(void)
{
	if (accel.state != LGPS_CALIB_STATE_COLLECTING) {
		return 0;
	}

	accel.state = LGPS_CALIB_STATE_IDLE;
	accel.face_state = LGPS_CALIB_FACE_WAIT_REST;
	accel.samples = 0U;
	accel.rest_pct = 0U;

	return 0;
}

int lgps_calib_accel_finish(void)
{
	int32_t bias_ug[3];
	int32_t scale_ppm[3];
	int32_t gyro_mdps[3];
	uint8_t axis;
	uint8_t faces = 0U;
	int ret;

	/*
	 * FINISH_SAVE is expected while the capture is still running (the host polls
	 * the progress and then finishes), so a collecting state is not an error: the
	 * solver below decides from what has actually been captured.
	 */
	if (accel.quick_valid && ((accel.face_mask & SIX_FACE_MASK) != SIX_FACE_MASK)) {
		memcpy(coeffs.accel_bias_ug, accel.quick_bias_ug, sizeof(coeffs.accel_bias_ug));
		for (axis = 0U; axis < 3U; axis++) {
			coeffs.accel_scale_ppm[axis] = 0;
		}
		coeffs.accel_face_mask = accel.face_mask;
		flags |= LGPS_CALIB_FLAG_QUICK;
	} else {
		ret = lgps_calib_face_math(accel.face_mean_lsb, accel.face_mask, bias_ug,
					   scale_ppm);
		if (ret < 0) {
			return ret;
		}

		for (axis = 0U; axis < 3U; axis++) {
			gyro_mdps[axis] = 0;
		}

		for (uint8_t face = 0U; face < LGPS_CALIB_POSITION_COUNT; face++) {
			if (!accel.face_valid[face]) {
				continue;
			}

			faces++;
			for (axis = 0U; axis < 3U; axis++) {
				gyro_mdps[axis] +=
					round_i32(accel.face_gyro_mdps[face][axis]);
			}
		}

		if (faces == 0U) {
			return -EAGAIN;
		}

		memcpy(coeffs.accel_bias_ug, bias_ug, sizeof(coeffs.accel_bias_ug));
		memcpy(coeffs.accel_scale_ppm, scale_ppm, sizeof(coeffs.accel_scale_ppm));
		for (axis = 0U; axis < 3U; axis++) {
			coeffs.gyro_bias_mdps[axis] =
				clamp_i32(gyro_mdps[axis] / (int32_t)faces, GYRO_MDPS_LIMIT);
		}
		coeffs.accel_face_mask = accel.face_mask;
		flags &= (uint8_t)~LGPS_CALIB_FLAG_QUICK;
	}

	accel.face_mask = 0U;
	memset(accel.face_valid, 0, sizeof(accel.face_valid));
	accel.quick_valid = false;
	accel.detected_face = LGPS_CALIB_FACE_NONE;

	return persist_current();
}

int lgps_calib_accel_clear(void)
{
	memset(coeffs.accel_bias_ug, 0, sizeof(coeffs.accel_bias_ug));
	memset(coeffs.accel_scale_ppm, 0, sizeof(coeffs.accel_scale_ppm));
	memset(coeffs.gyro_bias_mdps, 0, sizeof(coeffs.gyro_bias_mdps));
	coeffs.accel_face_mask = 0U;
	flags = LGPS_CALIB_FLAG_DEFAULTS;
	accel.face_mask = 0U;
	memset(accel.face_valid, 0, sizeof(accel.face_valid));
	accel.quick_valid = false;
	accel.detected_face = LGPS_CALIB_FACE_NONE;

	return persist_current();
}

/* --- magnetometer session -------------------------------------------------- */

int lgps_calib_mag_start(void)
{
	if (mag.state == LGPS_CALIB_STATE_COLLECTING) {
		return -EBUSY;
	}

	mag.state = LGPS_CALIB_STATE_COLLECTING;
	mag.samples = 0U;
	mag.have_sample = false;
	memset(mag.min_lsb, 0, sizeof(mag.min_lsb));
	memset(mag.max_lsb, 0, sizeof(mag.max_lsb));

	return 0;
}

int lgps_calib_mag_cancel(void)
{
	if (mag.state != LGPS_CALIB_STATE_COLLECTING) {
		return 0;
	}

	mag.state = LGPS_CALIB_STATE_IDLE;
	mag.samples = 0U;
	mag.have_sample = false;

	return 0;
}

int lgps_calib_mag_finish(void)
{
	int16_t bias_lsb[3];
	int32_t scale_ppm[3];
	uint8_t coverage_pct[3];
	uint8_t axis;
	int ret;

	/* Same as the accelerometer: finishing during collection is the normal flow. */
	ret = lgps_calib_mag_math(mag.min_lsb, mag.max_lsb, bias_lsb, scale_ppm, coverage_pct);
	if (ret < 0) {
		return -EAGAIN;
	}

	for (axis = 0U; axis < 3U; axis++) {
		if (coverage_pct[axis] < LGPS_CALIB_MAG_COVERAGE_MIN_PCT) {
			return -EAGAIN;
		}
	}

	memcpy(coeffs.mag_bias_lsb, bias_lsb, sizeof(coeffs.mag_bias_lsb));
	memcpy(coeffs.mag_scale_ppm, scale_ppm, sizeof(coeffs.mag_scale_ppm));
	memcpy(coeffs.mag_coverage_pct, coverage_pct, sizeof(coeffs.mag_coverage_pct));

	mag.state = LGPS_CALIB_STATE_IDLE;
	mag.have_sample = false;

	return persist_current();
}

int lgps_calib_mag_clear(void)
{
	memset(coeffs.mag_bias_lsb, 0, sizeof(coeffs.mag_bias_lsb));
	memset(coeffs.mag_scale_ppm, 0, sizeof(coeffs.mag_scale_ppm));
	memset(coeffs.mag_coverage_pct, 0, sizeof(coeffs.mag_coverage_pct));

	return persist_current();
}

/* --- feeding --------------------------------------------------------------- */

void lgps_calib_feed_imu(const struct lgps_imu_sample *sample, uint32_t now_ms)
{
	float magnitude_g2;
	float accel_g[3];
	uint8_t axis;
	int face;

	if (sample == NULL) {
		return;
	}

	if (accel.state != LGPS_CALIB_STATE_COLLECTING) {
		return;
	}

	if (!accel.started) {
		accel.started = true;
		accel.started_ms = now_ms;
	}

	for (axis = 0U; axis < 3U; axis++) {
		accel_g[axis] = (float)sample->raw_accel[axis] * ACCEL_G_PER_LSB;
	}

	magnitude_g2 = (accel_g[0] * accel_g[0]) + (accel_g[1] * accel_g[1]) +
		       (accel_g[2] * accel_g[2]);
	window_push(magnitude_g2);

	if (accel.face_state == LGPS_CALIB_FACE_WAIT_REST) {
		if (accel.rest_pct >= REST_PCT_USABLE) {
			accel.face_state = LGPS_CALIB_FACE_COLLECTING;
			accel.samples = 0U;
			for (axis = 0U; axis < 3U; axis++) {
				accel.accel_sum_lsb[axis] = 0.0F;
				accel.gyro_sum_lsb[axis] = 0.0F;
			}
		} else if ((uint32_t)(now_ms - accel.started_ms) > LGPS_CALIB_FACE_TIMEOUT_MS) {
			accel.face_state = LGPS_CALIB_FACE_JITTER_TIMEOUT;
			accel.state = LGPS_CALIB_STATE_IDLE;
		}

		return;
	}

	if (accel.face_state != LGPS_CALIB_FACE_COLLECTING) {
		return;
	}

	if (accel.rest_pct < REST_PCT_USABLE) {
		/* The device moved mid-capture: keep what was collected (the pose is a
		 * mean, so a few noisy samples cannot move it far) but do not add more.
		 */
		return;
	}

	for (axis = 0U; axis < 3U; axis++) {
		/* Raw counts: lgps_calib_face_math() owns the LSB -> g conversion for the
		 * face means, so accumulating grams here would apply it twice.
		 */
		accel.accel_sum_lsb[axis] += (float)sample->raw_accel[axis];
		accel.gyro_sum_lsb[axis] += (float)sample->raw_gyro[axis];
	}

	accel.samples++;

	if (accel.samples < LGPS_CALIB_FACE_SAMPLES) {
		return;
	}

	{
		float mean_g[3];

		for (axis = 0U; axis < 3U; axis++) {
			mean_g[axis] = (accel.accel_sum_lsb[axis] / (float)accel.samples) *
				       ACCEL_G_PER_LSB;
		}

		face = lgps_calib_detect_face(mean_g);
	}

	/*
	 * A forced face index is a bench override (the wire doc keeps it for that
	 * reason); otherwise the detected face is the only one that may be recorded.
	 * Guessing a face for an ambiguous pose would corrupt the +/- pair the whole
	 * six-face solve depends on.
	 */
	if (accel.position <= LGPS_CALIB_POSITION_COUNT - 1U) {
		face = (int)accel.position;
	}

	if ((face == LGPS_CALIB_FACE_NONE) || (face >= (int)LGPS_CALIB_POSITION_COUNT)) {
		accel.face_state = LGPS_CALIB_FACE_JITTER_TIMEOUT;
		accel.state = LGPS_CALIB_STATE_IDLE;
		return;
	}

	accel_store_face((uint8_t)face);
}

void lgps_calib_feed_mag(const int16_t raw_lsb[3])
{
	uint8_t axis;

	if ((raw_lsb == NULL) || (mag.state != LGPS_CALIB_STATE_COLLECTING)) {
		return;
	}

	if (!mag.have_sample) {
		for (axis = 0U; axis < 3U; axis++) {
			mag.min_lsb[axis] = raw_lsb[axis];
			mag.max_lsb[axis] = raw_lsb[axis];
		}

		mag.have_sample = true;
	} else {
		for (axis = 0U; axis < 3U; axis++) {
			if (raw_lsb[axis] < mag.min_lsb[axis]) {
				mag.min_lsb[axis] = raw_lsb[axis];
			}

			if (raw_lsb[axis] > mag.max_lsb[axis]) {
				mag.max_lsb[axis] = raw_lsb[axis];
			}
		}
	}

	if (mag.samples < UINT16_MAX) {
		mag.samples++;
	}
}
