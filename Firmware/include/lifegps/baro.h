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
 *   fix_valid, fix_type >= 3, satellites >= 8, known pDOP <= 2.0, speed < 0.3 m/s
 *
 * 30 consecutive 1 Hz samples (30 s) and a window standard deviation below
 * 0.3 hPa (30 Pa, ~2.5 m) reject a moving or gusting moment. pDOP = 0 means the
 * receiver did not report it, which is treated as "not good enough" rather than
 * as a perfect zero.
 */
#define LGPS_BARO_AUTO_MIN_FIX_TYPE 3U
#define LGPS_BARO_AUTO_MIN_SATELLITES 8U
#define LGPS_BARO_AUTO_MAX_PDOP_1E2 200U /* pDOP <= 2.00 */
#define LGPS_BARO_AUTO_MAX_SPEED_CMS 30U /* 0.3 m/s */
#define LGPS_BARO_AUTO_WINDOW_SAMPLES 30U
#define LGPS_BARO_AUTO_MAX_SIGMA_PA 30.0F

/*
 * Hysteresis: an automatic re-baseline only happens when it differs from the
 * current one by more than 1 hPa (~8 m). Without it the device would rewrite
 * the record (and the flash slot) every 30 s for sensor noise.
 */
#define LGPS_BARO_AUTO_REBASE_MIN_PA 100U

/* ICAO standard atmosphere (README 7.4). */
#define LGPS_BARO_ALT_SCALE_M 44330.0F
#define LGPS_BARO_ALT_EXPONENT 0.1902632F

/* Relative height clamp, keeps the int32 wire field and the App slider sane. */
#define LGPS_BARO_ALT_LIMIT_MM 10000000 /* +/- 10000 m */

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

/* Last accepted pressure in Pa; false when the sensor has not reported yet. */
bool lgps_baro_pressure(float *pressure_pa);

/* BARO_BASELINE action 0: take the current pressure. -ENODATA without one. */
int lgps_baro_take_current(void);

/* BARO_BASELINE action 1: set a given baseline (Pa x 16). -EINVAL out of range. */
int lgps_baro_set_baseline(uint32_t pa_q4, bool automatic);

uint32_t lgps_baro_baseline_pa_q4(void);
bool lgps_baro_baseline_set(void);
bool lgps_baro_baseline_auto(void);

/*
 * The height formula on its own, so the unit test can pin it without any state.
 * Returns 0 when @p baseline_pa_q4 is unset, @p pressure_pa is not positive, or
 * either value is outside the accepted range.
 */
int32_t lgps_baro_altitude_mm(float pressure_pa, uint32_t baseline_pa_q4);

#endif /* LIFEGPS_BARO_H_ */
