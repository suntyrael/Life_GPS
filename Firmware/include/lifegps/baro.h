/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - barometric baseline and relative altitude (BMP388).
 *
 * A pressure sensor has no absolute height: 1013 hPa is sea level on one day and
 * a different height on the next. Every altitude therefore needs a *baseline*
 * pressure, and this module owns exactly one such baseline:
 *
 *   - set by the user (App -> BARO_BASELINE 0x42): "take the current pressure",
 *     or a manually entered value;
 *   - or set automatically once the GNSS fix is good enough for long enough
 *     (see LGPS_BARO_AUTO_*), which is the only way to get a baseline that was
 *     not typed in by hand;
 *   - persisted in its own NOR sector (lifegps/baro_store.h) so a reboot does
 *     not throw the reference away.
 *
 * The height itself uses the ICAO standard atmosphere, the same formula the
 * README section 7.4 assumes:
 *
 *   h = 44330 m * (1 - (p / p0) ^ 0.1902632)
 *
 * which is the ISA solution of dp/p = -g*dh/(R*T) with a 6.5 K/km lapse rate.
 * It is a *relative* height (metres above the baseline pressure), not an
 * ellipsoid or orthometric height; nothing here may be confused with the GNSS
 * hmsl_mm field.
 */

#ifndef LIFEGPS_BARO_H_
#define LIFEGPS_BARO_H_

#include <stdbool.h>
#include <stdint.h>

#include <lifegps/gnss.h>

/*
 * The baseline travels as Pa x 16 ("pa_q4") so the wire format, the NOR record
 * and the RAM state all use one unit and no float ever reaches a boundary.
 * 0 means "no baseline yet" and is never a valid pressure.
 */
#define LGPS_BARO_BASELINE_UNSET 0U

/* Accepted baseline range in Pa (BMP388 datasheet table 1: 300..1250 hPa). */
#define LGPS_BARO_BASELINE_MIN_PA 30000U
#define LGPS_BARO_BASELINE_MAX_PA 125000U

/*
 * Gate for the automatic baseline. All five conditions must hold for a whole
 * window before the current pressure is accepted as the reference:
 *
 *   fix_valid, fix_type >= 3, satellites >= 6, known pDOP <= 5.0, speed < 0.5 m/s
 *
 * 30 consecutive 1 Hz samples (30 s) and a window standard deviation below
 * 0.3 hPa (30 Pa, ~2.5 m) reject a moving or gusting moment. pDOP = 0 means the
 * receiver did not report it, which is treated as "not good enough" rather than
 * as a perfect zero.
 */
#define LGPS_BARO_AUTO_MIN_FIX_TYPE 3U
#define LGPS_BARO_AUTO_MIN_SATELLITES 6U
#define LGPS_BARO_AUTO_MAX_PDOP_1E2 500U /* pDOP <= 5.00 */
#define LGPS_BARO_AUTO_MAX_SPEED_CMS 50U /* 0.5 m/s */
#define LGPS_BARO_AUTO_WINDOW_SAMPLES 30U
#define LGPS_BARO_AUTO_MAX_SIGMA_PA 30.0F

/*
 * Hysteresis: an automatic re-baseline only happens when it differs from the
 * current one by more than 1 hPa (~8 m). Without it the device would rewrite
 * the record (and the flash slot) every 30 s for sensor noise.
 */
#define LGPS_BARO_AUTO_REBASE_MIN_PA 100U

/* ICAO standard atmosphere (README 7.4):
 *   h = 44330 m * (1 - (p/p0)^0.1902632)
 * The exponent is R*L/(g*M) = 0.1902632, so its reciprocal 5.25588 is what the
 * inverted form needs (p/p0 = (1 - h/44330)^5.25588).
 */
#define LGPS_BARO_ALT_SCALE_M 44330.0F
#define LGPS_BARO_ALT_EXPONENT 0.1902632F
#define LGPS_BARO_ISA_EXPONENT 5.25588F

/* Relative height clamp, keeps the int32 wire field and the App slider sane. */
#define LGPS_BARO_ALT_LIMIT_MM 10000000 /* +/- 10000 m */

/*
 * What the stored pressure ("reference") refers to. This is the difference
 * between a relative and an absolute altimeter:
 *
 *   LOCAL  the pressure measured at the reference point. Height 0 is that point,
 *          so the instrument reports how far you moved since it was set.
 *   MSL    the sea-level pressure QNH derived from a GNSS height. Height 0 is
 *          mean sea level, so the same pressure formula now reports an absolute
 *          altitude that stays valid after the GNSS link is lost.
 *
 * MSL mode is what makes the barometer useful for indoor floors: the reference
 * survives in NOR, the GNSS height is only needed once, at calibration time.
 */
enum lgps_baro_reference {
	LGPS_BARO_REF_LOCAL = 0,
	LGPS_BARO_REF_MSL = 1,
};

/*
 * Gate for a GNSS height calibration. A GNSS height is only as good as its
 * vertical accuracy: the BMP388 resolve about 0.1 hPa (0.8 m), so a height that
 * is tens of metres off cannot be allowed to define the reference.
 *
 * The limits are deliberately loose enough for a normal 3D fix in the open:
 * a hand-held MAX-M10S typically reports pDOP 2..4 with vAcc of a few metres,
 * and the previous pDOP <= 2.0 / satellites >= 8 combination rejected exactly
 * those fixes, so the calibration could never run on a device that was working
 * correctly. vAcc is the only number that actually describes the height, and it
 * stays the binding limit (30 m, roughly 3 sigma of a marginal urban fix).
 */
#define LGPS_BARO_GNSS_MIN_FIX_TYPE 3U
#define LGPS_BARO_GNSS_MIN_SATELLITES 6U
#define LGPS_BARO_GNSS_MAX_PDOP_1E2 500U  /* pDOP <= 5.00 */
#define LGPS_BARO_GNSS_MAX_SPEED_CMS 50U  /* 0.5 m/s */
#define LGPS_BARO_GNSS_MAX_VACC_MM 30000U /* vAcc <= 30 m */

/*
 * State machine init. lgps_baro_load() additionally restores the baseline from
 * the external NOR; without it the module simply starts with no baseline.
 */
void lgps_baro_init(void);
int lgps_baro_load(void);

/* Dates the next record with the GNSS UTC epoch (0 = unknown). */
void lgps_baro_note_utc(uint32_t utc_epoch_s);

/*
 * One update per second, called from the telemetry cadence with the newest
 * compensated pressure and the current fix. Runs the automatic gate and persists
 * a baseline it decides on. Returns true when the baseline changed.
 *
 * @p pressure_valid is the BMP388 "compensated && present" verdict, so a sensor
 * that never answered can never define a baseline.
 */
bool lgps_baro_feed(bool pressure_valid, float pressure_pa, const struct lgps_gnss_fix *fix);

/* Relative height above the baseline in mm; 0 when no baseline or no pressure. */
int32_t lgps_baro_rel_alt_mm(void);

/*
 * Absolute (mean sea level) altitude in mm, i.e. reference height plus relative
 * height. Only meaningful in LGPS_BARO_REF_MSL mode; lgps_baro_msl_valid() says
 * whether it is.
 */
int32_t lgps_baro_msl_altitude_mm(void);
bool lgps_baro_msl_valid(void);

/* What the stored reference means (LOCAL or MSL) and the height it sits at. */
enum lgps_baro_reference lgps_baro_reference_kind(void);
int32_t lgps_baro_reference_height_mm(void);

/* Last accepted pressure in Pa; false when the sensor has not reported yet. */
bool lgps_baro_pressure(float *pressure_pa);

/* BARO_BASELINE action 0: take the current pressure. -ENODATA without one. */
int lgps_baro_take_current(void);

/* BARO_BASELINE action 1: set a given baseline (Pa x 16). -EINVAL out of range. */
int lgps_baro_set_baseline(uint32_t pa_q4, bool automatic);

/*
 * BARO_BASELINE action 2: calibrate against the GNSS height of @p fix.
 *
 * Computes the sea-level pressure QNH from the averaged station pressure and the
 * fix's hMSL (NAV-PVT offset 36, the orthometric height, not the ellipsoid one),
 * stores it as an MSL reference and persists it. Returns 0 on success, -ENODATA
 * when there is no pressure to calibrate, -EINVAL when the fix is not good
 * enough (see LGPS_BARO_GNSS_*) or the resulting sea-level pressure is outside
 * the sensor range.
 */
int lgps_baro_calibrate_from_gnss(const struct lgps_gnss_fix *fix);

uint32_t lgps_baro_baseline_pa_q4(void);
bool lgps_baro_baseline_set(void);
bool lgps_baro_baseline_auto(void);

/*
 * The height formula on its own, so the unit test can pin it without any state.
 * Returns 0 when @p baseline_pa_q4 is unset, @p pressure_pa is not positive, or
 * either value is outside the accepted range.
 */
int32_t lgps_baro_altitude_mm(float pressure_pa, uint32_t baseline_pa_q4);

/*
 * Inverse of the same formula: the sea-level pressure that puts @p height_mm at
 * @p pressure_pa. Returns 0 when either argument is unusable; the caller checks
 * the result against LGPS_BARO_BASELINE_MIN_PA / _MAX_PA.
 */
float lgps_baro_sealevel_pressure_pa(float pressure_pa, int32_t height_mm);

#endif /* LIFEGPS_BARO_H_ */
