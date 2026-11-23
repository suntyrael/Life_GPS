/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - "Life_GPS 2020 epoch" civil time conversion.
 *
 * The trajectory records store whole seconds counted from
 * 2020-01-01T00:00:00Z (see README section 8.2), which keeps uint32_t valid
 * until 2156. This module owns the only place that converts between that
 * counter and a UTC calendar date; no other file may re-implement it.
 *
 * Proleptic Gregorian calendar, no leap seconds (UTC day = 86400 s), valid
 * range 2020-01-01T00:00:00Z .. 2156-02-07T06:28:15Z.
 */

#ifndef LIFEGPS_TIME_H_
#define LIFEGPS_TIME_H_

#include <stdbool.h>
#include <stdint.h>

/* Seconds between the Unix epoch and the Life_GPS epoch (10 years, 2 leap days). */
#define LGPS_EPOCH_UNIX_OFFSET_S 1577836800ULL

/* Largest representable timestamp in the Life_GPS epoch (uint32_t saturated). */
#define LGPS_TIME_MAX_S UINT32_MAX

struct lgps_utc {
	uint16_t year;   /* 2020 .. 2156 */
	uint8_t month;   /* 1 .. 12 */
	uint8_t day;     /* 1 .. 31 */
	uint8_t hour;    /* 0 .. 23 */
	uint8_t minute;  /* 0 .. 59 */
	uint8_t second;  /* 0 .. 59 */
};

/*
 * Convert a UTC calendar time into Life_GPS epoch seconds.
 *
 * Returns -EINVAL when a field is out of range or the date is before the
 * Life_GPS epoch. Returns 0 on success.
 */
int lgps_time_from_utc(const struct lgps_utc *utc, uint32_t *epoch_s);

/*
 * Convert Life_GPS epoch seconds into a UTC calendar time.
 * Returns -EINVAL when @p epoch_s cannot be decomposed (never happens for
 * uint32_t, kept for symmetry and to reject a NULL pointer).
 */
int lgps_time_to_utc(uint32_t epoch_s, struct lgps_utc *utc);

/* True when the year is a leap year in the proleptic Gregorian calendar. */
bool lgps_time_is_leap_year(uint16_t year);

/* Days in @p month (1..12) of @p year; 0 for an invalid month. */
uint8_t lgps_time_days_in_month(uint16_t year, uint8_t month);

#endif /* LIFEGPS_TIME_H_ */
