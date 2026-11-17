/*
 * Life_GPS firmware - "Life_GPS 2020 epoch" civil time conversion.
 *
 * Epoch anchor: 2020-01-01T00:00:00Z is Unix 1577836800, spelled
 * LGPS_EPOCH_UNIX_OFFSET_S in lifegps/time.h; nothing here needs libc <time.h>.
 *
 * Calendar rules are the proleptic Gregorian ones: one UTC day is 86400 s (no
 * leap seconds), and a year is leap when divisible by 4 but not by 100, unless
 * it is divisible by 400. All arithmetic is integer maths; lgps_time_from_utc()
 * and lgps_time_to_utc() are exact inverses over uint32_t.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <stddef.h>

#include <lifegps/time.h>

#define LGPS_SECONDS_PER_MINUTE 60U
#define LGPS_SECONDS_PER_HOUR   3600U
#define LGPS_SECONDS_PER_DAY    86400U

/* First year representable in the Life_GPS epoch. */
#define LGPS_EPOCH_YEAR 2020U

/* Days in a common (non-leap) Gregorian year. */
#define LGPS_DAYS_PER_YEAR 365U

bool lgps_time_is_leap_year(uint16_t year)
{
	uint32_t value = (uint32_t)year;

	if ((value % 4U) != 0U) {
		return false;
	}
	if ((value % 100U) != 0U) {
		return true;
	}

	return (value % 400U) == 0U;
}

uint8_t lgps_time_days_in_month(uint16_t year, uint8_t month)
{
	static const uint8_t days_by_month[12] = {
		31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
	};

	if ((month < 1U) || (month > 12U)) {
		return 0U;
	}
	/* February gains its 29th day only in a leap year. */
	if ((month == 2U) && lgps_time_is_leap_year(year)) {
		return 29U;
	}

	return days_by_month[month - 1U];
}

/* Days in @p year under the proleptic Gregorian rules. */
static uint16_t days_in_year(uint16_t year)
{
	if (lgps_time_is_leap_year(year)) {
		return (uint16_t)(LGPS_DAYS_PER_YEAR + 1U);
	}

	return (uint16_t)LGPS_DAYS_PER_YEAR;
}

/*
 * Leap years in the closed range [1, year - 1]: the Gregorian count of years
 * divisible by 4, minus the centuries, plus the 400-year corrections.
 */
static uint32_t leap_years_before(uint16_t year)
{
	uint32_t y = (uint32_t)year - 1U;

	return (y / 4U) - (y / 100U) + (y / 400U);
}

/* Whole days from 2020-01-01 up to (not including) 01-01 of @p year. */
static uint32_t days_before_year(uint16_t year)
{
	uint32_t years = (uint32_t)year - LGPS_EPOCH_YEAR;
	uint32_t leap_days = leap_years_before(year) - leap_years_before(LGPS_EPOCH_YEAR);

	return (years * LGPS_DAYS_PER_YEAR) + leap_days;
}

int lgps_time_from_utc(const struct lgps_utc *utc, uint32_t *epoch_s)
{
	uint32_t days_before_month = 0U;
	uint64_t total_s;
	uint8_t month;
	uint8_t max_day;

	if ((utc == NULL) || (epoch_s == NULL)) {
		return -EINVAL;
	}
	if ((utc->month < 1U) || (utc->month > 12U)) {
		return -EINVAL;
	}
	max_day = lgps_time_days_in_month(utc->year, utc->month);
	if ((utc->day < 1U) || (utc->day > max_day)) {
		return -EINVAL;
	}
	if ((utc->hour > 23U) || (utc->minute > 59U) || (utc->second > 59U)) {
		return -EINVAL;
	}
	/* Anything earlier than the anchor is by definition not representable. */
	if (utc->year < LGPS_EPOCH_YEAR) {
		return -EINVAL;
	}

	for (month = 1U; month < utc->month; month++) {
		days_before_month += lgps_time_days_in_month(utc->year, month);
	}

	total_s = (uint64_t)days_before_year(utc->year) * LGPS_SECONDS_PER_DAY;
	total_s += ((uint64_t)days_before_month + (uint64_t)(utc->day - 1U)) *
		   LGPS_SECONDS_PER_DAY;
	total_s += (uint64_t)utc->hour * LGPS_SECONDS_PER_HOUR;
	total_s += (uint64_t)utc->minute * LGPS_SECONDS_PER_MINUTE;
	total_s += (uint64_t)utc->second;

	/* Dates past 2156-02-07T06:28:15Z do not fit in the wire field. */
	if (total_s > (uint64_t)LGPS_TIME_MAX_S) {
		return -EINVAL;
	}

	*epoch_s = (uint32_t)total_s;

	return 0;
}

int lgps_time_to_utc(uint32_t epoch_s, struct lgps_utc *utc)
{
	uint32_t days_since_epoch;
	uint32_t second_of_day;
	uint16_t year = (uint16_t)LGPS_EPOCH_YEAR;
	uint8_t month;
	uint8_t days_in_month;

	if (utc == NULL) {
		return -EINVAL;
	}

	days_since_epoch = epoch_s / LGPS_SECONDS_PER_DAY;
	second_of_day = epoch_s % LGPS_SECONDS_PER_DAY;

	utc->hour = (uint8_t)(second_of_day / LGPS_SECONDS_PER_HOUR);
	second_of_day %= LGPS_SECONDS_PER_HOUR;
	utc->minute = (uint8_t)(second_of_day / LGPS_SECONDS_PER_MINUTE);
	utc->second = (uint8_t)(second_of_day % LGPS_SECONDS_PER_MINUTE);

	/* At most 137 iterations for the whole uint32_t range. */
	while (days_since_epoch >= (uint32_t)days_in_year(year)) {
		days_since_epoch -= (uint32_t)days_in_year(year);
		year++;
	}

	for (month = 1U; month < 12U; month++) {
		days_in_month = lgps_time_days_in_month(year, month);
		if (days_since_epoch < (uint32_t)days_in_month) {
			break;
		}
		days_since_epoch -= (uint32_t)days_in_month;
	}

	utc->year = year;
	utc->month = month;
	utc->day = (uint8_t)(days_since_epoch + 1U);

	return 0;
}
