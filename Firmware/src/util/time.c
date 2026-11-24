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

/* --- system clock (RTC) ---------------------------------------------------- */

/*
 * State of the software clock. The GNSS thread is the only writer after boot
 * (lgps_time_init() runs in the main thread before that thread starts), the
 * main loop and the telemetry formatter are readers. The anchor fields are
 * naturally aligned 32-bit scalars, so a single load/store is atomic on
 * Cortex-M; volatile keeps both the accesses and the uptime-before-epoch write
 * order. The pair is re-anchored once per navigation epoch, and a reader that
 * lands between the two stores gets the previous epoch with the new uptime,
 * i.e. a value at most one sync interval behind - never a torn 32-bit value.
 */
static struct {
	volatile uint32_t anchor_epoch_s;  /* Life_GPS epoch seconds at anchor_uptime_ms */
	volatile uint32_t anchor_uptime_ms; /* caller uptime when the anchor was taken */
	volatile int32_t local_offset_s;
	volatile bool seeded; /* build time parsed, so the getters are meaningful */
	volatile bool synced; /* at least one accepted GNSS UTC this boot */
} clock_state = {
	.local_offset_s = LGPS_TIME_LOCAL_OFFSET_DEFAULT_S,
};

static bool is_digit(char value)
{
	return (value >= '0') && (value <= '9');
}

static int parse_two_digits(const char *text)
{
	if ((text == NULL) || !is_digit(text[0]) || !is_digit(text[1])) {
		return -1;
	}

	return ((text[0] - '0') * 10) + (text[1] - '0');
}

static int parse_four_digits(const char *text)
{
	int high;
	int low;

	if (text == NULL) {
		return -1;
	}

	high = parse_two_digits(text);
	low = parse_two_digits(text + 2);
	if ((high < 0) || (low < 0)) {
		return -1;
	}

	return (high * 100) + low;
}

/*
 * __DATE__ spells the month as the three English letters of asctime(), fixed by
 * C11 6.10.8.1; the table is that spelling, not a locale lookup.
 */
static int month_from_name(const char *name)
{
	static const char months[12][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};

	if (name == NULL) {
		return 0;
	}

	for (int index = 0; index < 12; index++) {
		if ((name[0] == months[index][0]) && (name[1] == months[index][1]) &&
		    (name[2] == months[index][2])) {
			return index + 1;
		}
	}

	return 0;
}

/*
 * Day field of __DATE__: C11 6.10.8.1 specifies a leading space for days 1..9,
 * so "Jan  1 2026" and "Jan 11 2026" are both legal and the single-digit form
 * must not be rejected.
 */
static int parse_build_day(const char *text)
{
	if (text == NULL) {
		return -1;
	}

	if (text[0] == ' ') {
		if (!is_digit(text[1])) {
			return -1;
		}

		return text[1] - '0';
	}

	return parse_two_digits(text);
}

int lgps_time_build_utc(struct lgps_utc *out)
{
	const char *date = __DATE__;
	const char *time = __TIME__;
	struct lgps_utc parsed;
	uint32_t epoch_s = 0U;
	int month;
	int day;
	int year;
	int hour;
	int minute;
	int second;
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	/* "Mmm dd yyyy": the separators make a truncated macro visible here. */
	if ((date[3] != ' ') || (date[6] != ' ')) {
		return -EINVAL;
	}

	month = month_from_name(date);
	day = parse_build_day(&date[4]);
	year = parse_four_digits(&date[7]);

	/* "hh:mm:ss": same idea, one check per separator. */
	if ((time[2] != ':') || (time[5] != ':')) {
		return -EINVAL;
	}

	hour = parse_two_digits(time);
	minute = parse_two_digits(&time[3]);
	second = parse_two_digits(&time[6]);

	if ((month == 0) || (day < 0) || (year < 0) || (hour < 0) || (minute < 0) ||
	    (second < 0)) {
		return -EINVAL;
	}

	parsed.year = (uint16_t)year;
	parsed.month = (uint8_t)month;
	parsed.day = (uint8_t)day;
	parsed.hour = (uint8_t)hour;
	parsed.minute = (uint8_t)minute;
	parsed.second = (uint8_t)second;

	/* Reuse the calendar validator instead of duplicating the epoch range. */
	ret = lgps_time_from_utc(&parsed, &epoch_s);
	if (ret < 0) {
		return ret;
	}

	*out = parsed;

	return 0;
}

void lgps_time_init(void)
{
	struct lgps_utc build_utc;
	uint32_t build_epoch_s = 0U;

	clock_state.anchor_epoch_s = 0U;
	clock_state.anchor_uptime_ms = 0U;
	clock_state.local_offset_s = LGPS_TIME_LOCAL_OFFSET_DEFAULT_S;
	clock_state.synced = false;
	clock_state.seeded = false;

	if (lgps_time_build_utc(&build_utc) != 0) {
		/* No usable build timestamp: stay unavailable until GNSS provides one. */
		return;
	}

	if (lgps_time_from_utc(&build_utc, &build_epoch_s) != 0) {
		return;
	}

	clock_state.anchor_epoch_s = build_epoch_s;
	/* Uptime is counted from boot, and init runs during boot, so the build
	 * timestamp is the clock's value at uptime 0.
	 */
	clock_state.anchor_uptime_ms = 0U;
	clock_state.seeded = true;
}

bool lgps_time_sync(uint32_t utc_epoch_s, uint32_t uptime_ms)
{
	uint32_t now_s;
	uint32_t skew_s;

	/* Zero is the receiver's "no date yet" value; a uint32_t cannot exceed
	 * LGPS_TIME_MAX_S, so only the zero case is reachable here.
	 */
	if (utc_epoch_s == 0U) {
		return false;
	}

	if (clock_state.seeded) {
		now_s = lgps_time_now_utc_s(uptime_ms);
		if (now_s != 0U) {
			skew_s = (utc_epoch_s >= now_s) ? (utc_epoch_s - now_s)
						       : (now_s - utc_epoch_s);
			if (skew_s > (uint32_t)LGPS_TIME_MAX_SYNC_SKEW_S) {
				return false;
			}
		}
	}

	clock_state.anchor_uptime_ms = uptime_ms;
	clock_state.anchor_epoch_s = utc_epoch_s;
	clock_state.seeded = true;
	clock_state.synced = true;

	return true;
}

bool lgps_time_synced(void)
{
	return clock_state.synced;
}

uint32_t lgps_time_now_utc_s(uint32_t uptime_ms)
{
	uint32_t elapsed_s;

	if (!clock_state.seeded) {
		return 0U;
	}

	/* uint32_t subtraction is the wrap contract for the 49.7-day uptime. */
	elapsed_s = (uint32_t)(uptime_ms - clock_state.anchor_uptime_ms) / 1000U;

	return clock_state.anchor_epoch_s + elapsed_s;
}

uint32_t lgps_time_now_local_s(uint32_t uptime_ms)
{
	uint32_t utc_s = lgps_time_now_utc_s(uptime_ms);
	int32_t offset_s = clock_state.local_offset_s;

	if (utc_s == 0U) {
		return 0U;
	}

	if (offset_s < 0) {
		uint32_t back_s = (uint32_t)(-offset_s);

		/* A negative offset must not wrap below the Life_GPS epoch. The clock
		 * range starts in 2020 and the largest offset is 12 h, so this only
		 * guards a deliberately unset/zero clock.
		 */
		if (utc_s < back_s) {
			return 0U;
		}

		return utc_s - back_s;
	}

	return utc_s + (uint32_t)offset_s;
}

bool lgps_time_now_local(struct lgps_utc *out, uint32_t uptime_ms)
{
	uint32_t local_s;

	if (out == NULL) {
		return false;
	}

	local_s = lgps_time_now_local_s(uptime_ms);
	if (local_s == 0U) {
		return false;
	}

	return lgps_time_to_utc(local_s, out) == 0;
}

int32_t lgps_time_local_offset_s(void)
{
	return clock_state.local_offset_s;
}

int lgps_time_set_local_offset_s(int32_t offset_s)
{
	if ((offset_s < LGPS_TIME_LOCAL_OFFSET_MIN_S) ||
	    (offset_s > LGPS_TIME_LOCAL_OFFSET_MAX_S)) {
		return -EINVAL;
	}

	clock_state.local_offset_s = offset_s;

	return 0;
}
