/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - sensor calibration (accelerometer / gyroscope / magnetometer).
 *
 * Contract: Doc/Software/BLE_Wire_v1.md sections 6.3 and 6.4. The byte layout on
 * the air is implemented in src/ble/ble_frame.c; the coefficient maths, the
 * session state machine and the record codec live here so they can be unit
 * tested on native_sim (Firmware/tests/lifegps.calib) without a sensor, a flash
 * driver or a radio.
 *
 * Design notes that are not obvious from the API:
 *
 * 1. The coefficients are applied in software, in one place: [lgps_calib_apply_imu]
 *    rewrites the scaled values of a sample before anything else sees them. The
 *    alternative - writing X/Y/Z_OFS_USR (0x73..0x75) and the LIS2MDL
 *    OFFSET_* registers - is documented in the wire doc and rejected here:
 *    - the accelerometer register is an int8 in units of 2^-10 g, i.e. +/-124 mg
 *      with a 0.98 mg quantum, while the scale error this exists to fix is a
 *      gain error that the register cannot represent at all;
 *    - the magnetometer offsets sit behind the sensor hub, so every write costs
 *      one hub cycle (~150 ms) and destroys the three-slave streaming layout,
 *      which then has to be rebuilt: about 1.7 s of dead time at every boot
 *      (the registers are volatile);
 *    - min/max hard-iron estimation needs the *raw* output, so an in-sensor
 *      offset would have to be iterated instead of computed in one pass.
 *
 * 2. Persistence is an 80-byte record appended to one dedicated 4 KB sector of the
 *    external NOR (see lifegps/calib_store.h and Firmware/docs/PARTITIONS.md).
 *    CONFIG_SETTINGS/NVS is deliberately not used: the internal RRAM partition
 *    table has never been recomputed for the nRF54L15, and a settings backend
 *    would touch it for 80 bytes of state.
 *
 * 3. Units. Coefficients are stored in engineering units (ug, ppm, mdps, LSB)
 *    rather than in register units, because the register units are exactly what
 *    cannot round-trip the measurement.
 */

#ifndef LIFEGPS_CALIB_H_
#define LIFEGPS_CALIB_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/imu.h>

/* --- record ---------------------------------------------------------------- */

#define LGPS_CALIB_RECORD_MAGIC 0x4C43414CU /* "LCAL" in little endian */
#define LGPS_CALIB_RECORD_VERSION 1U
#define LGPS_CALIB_RECORD_SIZE 80U

/* --- wire values (Doc/Software/BLE_Wire_v1.md 6.3 / 6.4) ------------------- */

enum lgps_calib_action {
	LGPS_CALIB_ACTION_START = 0,
	LGPS_CALIB_ACTION_FINISH_SAVE = 1,
	LGPS_CALIB_ACTION_CLEAR = 2,
	LGPS_CALIB_ACTION_QUERY = 3,
	LGPS_CALIB_ACTION_CANCEL = 4,
};

enum lgps_calib_state {
	LGPS_CALIB_STATE_IDLE = 0,
	LGPS_CALIB_STATE_COLLECTING = 1,
	LGPS_CALIB_STATE_RESULT_PENDING = 2,
};

#define LGPS_CALIB_FLAG_FROM_STORE 0x01U
#define LGPS_CALIB_FLAG_DEFAULTS 0x02U
#define LGPS_CALIB_FLAG_QUICK 0x04U
#define LGPS_CALIB_FLAG_SAVE_FAILED 0x08U

enum lgps_calib_face_state {
	LGPS_CALIB_FACE_WAIT_REST = 0,
	LGPS_CALIB_FACE_COLLECTING = 1,
	LGPS_CALIB_FACE_DONE = 2,
	LGPS_CALIB_FACE_JITTER_TIMEOUT = 3,
};

/* Six faces 0..5, then the single-face quick mode, then "identify it yourself". */
#define LGPS_CALIB_POSITION_COUNT 6U
#define LGPS_CALIB_POSITION_QUICK 6U
#define LGPS_CALIB_POSITION_AUTO 0xFFU
#define LGPS_CALIB_FACE_NONE 0xFFU

/* Samples per face at the 30 Hz IMU rate: 60 samples is 2.000 s. */
#define LGPS_CALIB_FACE_SAMPLES 60U

/* Samples in the stillness window that drives rest_pct (15 samples = 0.5 s). */
#define LGPS_CALIB_REST_WINDOW 15U

/* A face that has not settled after this long is reported as a jitter timeout. */
#define LGPS_CALIB_FACE_TIMEOUT_MS 15000U

/*
 * Magnetometer axis coverage is measured as the raw min/max span per axis against
 * this target. The Earth field is ~50 uT = 333 LSB at 0.15 uT/LSB, so a full
 * rotation spans ~666 LSB; 600 makes "waved through the whole range" reach 100 %
 * with a little headroom for a weaker field indoors.
 */
#define LGPS_CALIB_MAG_SPAN_TARGET_LSB 600U

/* Minimum coverage per axis before FINISH_SAVE is accepted. */
#define LGPS_CALIB_MAG_COVERAGE_MIN_PCT 80U

/* --- coefficients ---------------------------------------------------------- */

struct lgps_calib_coeffs {
	int32_t accel_bias_ug[3];
	int32_t accel_scale_ppm[3];
	int32_t gyro_bias_mdps[3];
	int16_t mag_bias_lsb[3];
	int32_t mag_scale_ppm[3];
	uint8_t mag_coverage_pct[3];
	uint8_t accel_face_mask;
};

/* What the BLE layer needs to build a MAG_CAL / ACCEL_CAL response. */
struct lgps_calib_snapshot {
	struct lgps_calib_coeffs coeffs;
	uint8_t flags;
	uint32_t last_calib_unix_s;

	uint8_t accel_state;
	uint8_t accel_face_state;
	uint8_t accel_face_mask;
	uint8_t accel_detected_face;
	uint16_t accel_progress_permille;
	uint8_t accel_rest_pct;

	uint8_t mag_state;
	uint8_t mag_sample_count;
	uint8_t mag_coverage_pct[3];
	uint16_t mag_progress_permille;
};

/* --- lifecycle ------------------------------------------------------------- */

/*
 * Load the newest valid record from the calibration sector and apply it. A
 * missing, erased or CRC-bad record is not an error: the compiled-in defaults
 * stay in effect and the snapshot reports LGPS_CALIB_FLAG_DEFAULTS.
 */
int lgps_calib_init(void);

/* Coefficients currently in effect (never NULL). */
const struct lgps_calib_coeffs *lgps_calib_current(void);

/*
 * Remember the current UTC so a saved record can be dated. The calibration code
 * has no clock; main.c calls this whenever the GNSS reports a valid fix. Zero is
 * ignored, so a record written before the first fix keeps last_calib_unix_s = 0
 * ("calibrated, but the device never knew the time").
 */
void lgps_calib_note_utc(uint32_t unix_s);

void lgps_calib_snapshot(struct lgps_calib_snapshot *out);

/* --- accelerometer / gyroscope session ------------------------------------- */

/*
 * Start capturing one pose. @p position is normally LGPS_CALIB_POSITION_AUTO: the
 * pose is identified from the gravity vector while resting, so the user only
 * places the device and presses again. A concrete 0..5 forces a face (bench only)
 * and LGPS_CALIB_POSITION_QUICK selects the single-face quick mode.
 * Returns 0, -EBUSY when a capture is already running, -EINVAL on a bad index.
 */
int lgps_calib_accel_start(uint8_t position);

/* Abandon the running capture. Stored coefficients are untouched. */
int lgps_calib_accel_cancel(void);

/*
 * Compute the coefficients from the captured faces and persist them.
 * Returns 0, -EAGAIN when not enough faces were captured with enough coverage,
 * or a negative errno from the store (the coefficients still take effect, and the
 * snapshot reports LGPS_CALIB_FLAG_SAVE_FAILED).
 */
int lgps_calib_accel_finish(void);

/* Drop the coefficients: back to the compiled-in defaults, record erased. */
int lgps_calib_accel_clear(void);

/* --- magnetometer session --------------------------------------------------- */

int lgps_calib_mag_start(void);
int lgps_calib_mag_cancel(void);
int lgps_calib_mag_finish(void);
int lgps_calib_mag_clear(void);

/* --- feeding --------------------------------------------------------------- */

/*
 * Called from the main loop for every new IMU sample (30 Hz): drives the
 * stillness window, the per-face accumulation and the timeout.
 */
void lgps_calib_feed_imu(const struct lgps_imu_sample *sample, uint32_t now_ms);

/*
 * Called wherever a hub read produced new magnetometer counts (the BLE
 * publication rate: 10 Hz while a host is subscribed). Only affects an active
 * magnetometer session; otherwise it is a no-op so the normal path pays nothing.
 */
void lgps_calib_feed_mag(const int16_t raw_lsb[3]);

/* --- applying -------------------------------------------------------------- */

/* Rewrites sample->accel_g and sample->gyro_dps in place. */
void lgps_calib_apply_imu(struct lgps_imu_sample *sample);

/* Rewrites the three magnetometer components in place (uT). */
void lgps_calib_apply_mag(float mag_ut[3]);

/* --- pure helpers (unit tested) -------------------------------------------- */

/*
 * Which face is up, from the body-frame gravity vector in g. At rest the
 * accelerometer measures the reaction to gravity, so the axis pointing up reads
 * +1 g. Returns 0..5, or LGPS_CALIB_FACE_NONE when the pose is too tilted to be
 * identified (a dominant axis below 0.7 g, or another axis above 0.45 g) - in
 * that case nothing may be recorded, because a guessed face poisons the maths.
 */
int lgps_calib_detect_face(const float gravity_g[3]);

/*
 * Six-face solve. @p face_mean_lsb is indexed by position (0..5), @p face_mask
 * uses the same bit order as face_mask on the wire. Returns 0, or -EAGAIN when a
 * face is missing or a +/- pair is degenerate.
 */
int lgps_calib_face_math(const float face_mean_lsb[LGPS_CALIB_POSITION_COUNT][3],
			 uint8_t face_mask, int32_t bias_ug[3], int32_t scale_ppm[3]);

/* Hard-iron centre and per-axis gain from the raw min/max of a rotation. */
int lgps_calib_mag_math(const int16_t min_lsb[3], const int16_t max_lsb[3],
			int16_t bias_lsb[3], int32_t scale_ppm[3], uint8_t coverage_pct[3]);

/* 0..100 stillness percentage from the short-window |acc| variance in g^2. */
uint8_t lgps_calib_rest_pct(float variance_g2);

/* Record codec. Returns 0 or -EINVAL. */
int lgps_calib_record_encode(const struct lgps_calib_coeffs *coeffs, uint8_t flags,
			     uint32_t seq, uint32_t unix_s,
			     uint8_t out[LGPS_CALIB_RECORD_SIZE]);
int lgps_calib_record_decode(const uint8_t in[LGPS_CALIB_RECORD_SIZE],
			     struct lgps_calib_coeffs *coeffs, uint8_t *flags, uint32_t *seq,
			     uint32_t *unix_s);

#endif /* LIFEGPS_CALIB_H_ */
