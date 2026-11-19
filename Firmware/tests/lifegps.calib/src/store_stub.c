/* SPDX-License-Identifier: MIT */
/*
 * In-RAM stand-in for the calibration record sector.
 *
 * calib.c talks to lifegps/calib_store.h, so the maths and the session state can
 * be tested on native_sim without a flash driver - which is the whole reason the
 * backend was put behind that header. The stub keeps one encoded record, so the
 * save -> init -> load round trip exercises the real codec.
 */

#include <errno.h>
#include <stdbool.h>

#include <lifegps/calib_store.h>

static uint8_t stored[LGPS_CALIB_RECORD_SIZE];
static bool stored_valid;
static uint32_t stored_seq;

int lgps_calib_store_load(struct lgps_calib_coeffs *coeffs, uint8_t *flags, uint32_t *seq,
			  uint32_t *unix_s)
{
	if (!stored_valid) {
		return -ENOENT;
	}

	return lgps_calib_record_decode(stored, coeffs, flags, seq, unix_s);
}

int lgps_calib_store_save(const struct lgps_calib_coeffs *coeffs, uint8_t flags, uint32_t unix_s)
{
	int ret;

	stored_seq++;
	ret = lgps_calib_record_encode(coeffs, flags, stored_seq, unix_s, stored);
	if (ret == 0) {
		stored_valid = true;
	}

	return ret;
}

int lgps_calib_store_erase(void)
{
	stored_valid = false;

	return 0;
}

/* --- hooks used by the test case ------------------------------------------- */

bool lgps_calib_store_test_has_record(void)
{
	return stored_valid;
}

void lgps_calib_store_test_corrupt(void)
{
	if (stored_valid) {
		/* Flip a byte inside accel_scale_ppm: only the CRC can catch it. */
		stored[30] ^= 0xFFU;
	}
}
