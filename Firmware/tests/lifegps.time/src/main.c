/*
 * ztest suite for Firmware/src/util/time.c.
 *
 * Expected values are derived from the proleptic Gregorian calendar anchored at
 * 2020-01-01T00:00:00Z; 2156-02-07T06:28:15Z is the last second representable
 * in uint32_t and must map to LGPS_TIME_MAX_S.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <errno.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <lifegps/time.h>

ZTEST(lifegps_time, test_leap_year_rule)
{
	zassert_true(lgps_time_is_leap_year(2020U), "2020 is a leap year");
	zassert_false(lgps_time_is_leap_year(2021U), "2021 is not a leap year");
	zassert_true(lgps_time_is_leap_year(2000U), "2000 is a leap year");
	zassert_false(lgps_time_is_leap_year(1900U), "1900 is a common year");
	zassert_false(lgps_time_is_leap_year(2100U), "2100 is a common year");
	zassert_true(lgps_time_is_leap_year(2400U), "2400 is a leap year");
}

ZTEST(lifegps_time, test_days_in_month)
{
	zassert_equal((uint32_t)lgps_time_days_in_month(2024U, 1U), 31U, "January");
	zassert_equal((uint32_t)lgps_time_days_in_month(2024U, 2U), 29U, "February 2024");
	zassert_equal((uint32_t)lgps_time_days_in_month(2023U, 2U), 28U, "February 2023");
	zassert_equal((uint32_t)lgps_time_days_in_month(2023U, 4U), 30U, "April");
	zassert_equal((uint32_t)lgps_time_days_in_month(2023U, 12U), 31U, "December");
	zassert_equal((uint32_t)lgps_time_days_in_month(2023U, 0U), 0U, "month 0 is invalid");
	zassert_equal((uint32_t)lgps_time_days_in_month(2023U, 13U), 0U, "month 13 is invalid");
}

ZTEST(lifegps_time, test_epoch_anchor)
{
	const struct lgps_utc anchor = {2020U, 1U, 1U, 0U, 0U, 0U};
	uint32_t epoch_s = 0xFFFFFFFFU;

	zassert_ok(lgps_time_from_utc(&anchor, &epoch_s), "anchor must convert");
	zassert_equal(epoch_s, 0U, "2020-01-01T00:00:00Z is Life_GPS epoch 0");
}

ZTEST(lifegps_time, test_known_offsets)
{
	static const struct {
		struct lgps_utc utc;
		uint32_t epoch_s;
	} cases[] = {
		{{2020U, 1U, 1U, 0U, 0U, 1U}, 1U},
		{{2020U, 1U, 2U, 0U, 0U, 0U}, 86400U},
		{{2021U, 1U, 1U, 0U, 0U, 0U}, 31622400U},
		{{2020U, 3U, 1U, 0U, 0U, 0U}, 5184000U},
		{{2024U, 2U, 29U, 23U, 59U, 59U}, 131414399U},
		{{2024U, 6U, 15U, 12U, 30U, 45U}, 140617845U},
		{{2069U, 12U, 31U, 0U, 0U, 0U}, 1577836800U},
		{{2156U, 2U, 7U, 6U, 28U, 15U}, LGPS_TIME_MAX_S},
	};
	uint32_t epoch_s;
	size_t index;

	for (index = 0U; index < ARRAY_SIZE(cases); index++) {
		epoch_s = 0U;
		zassert_ok(lgps_time_from_utc(&cases[index].utc, &epoch_s),
			   "case %u must convert", (uint32_t)index);
		zassert_equal(epoch_s, cases[index].epoch_s, "case %u epoch seconds",
			      (uint32_t)index);
	}
}

ZTEST(lifegps_time, test_to_utc_fields)
{
	static const struct {
		uint32_t epoch_s;
		struct lgps_utc utc;
	} cases[] = {
		{0U, {2020U, 1U, 1U, 0U, 0U, 0U}},
		{86400U, {2020U, 1U, 2U, 0U, 0U, 0U}},
		{31622400U, {2021U, 1U, 1U, 0U, 0U, 0U}},
		{131414399U, {2024U, 2U, 29U, 23U, 59U, 59U}},
		{LGPS_TIME_MAX_S, {2156U, 2U, 7U, 6U, 28U, 15U}},
	};
	struct lgps_utc utc;
	size_t index;

	for (index = 0U; index < ARRAY_SIZE(cases); index++) {
		zassert_ok(lgps_time_to_utc(cases[index].epoch_s, &utc),
			   "case %u must decompose", (uint32_t)index);
		zassert_equal((uint32_t)utc.year, (uint32_t)cases[index].utc.year,
			      "case %u year", (uint32_t)index);
		zassert_equal((uint32_t)utc.month, (uint32_t)cases[index].utc.month,
			      "case %u month", (uint32_t)index);
		zassert_equal((uint32_t)utc.day, (uint32_t)cases[index].utc.day,
			      "case %u day", (uint32_t)index);
		zassert_equal((uint32_t)utc.hour, (uint32_t)cases[index].utc.hour,
			      "case %u hour", (uint32_t)index);
		zassert_equal((uint32_t)utc.minute, (uint32_t)cases[index].utc.minute,
			      "case %u minute", (uint32_t)index);
		zassert_equal((uint32_t)utc.second, (uint32_t)cases[index].utc.second,
			      "case %u second", (uint32_t)index);
	}
}

ZTEST(lifegps_time, test_round_trip)
{
	static const uint32_t samples[] = {
		0U,
		1U,
		(uint32_t)(LGPS_EPOCH_UNIX_OFFSET_S - LGPS_EPOCH_UNIX_OFFSET_S),
		86400U,
		5184000U,
		140617845U,
		1577836800U,
		LGPS_TIME_MAX_S - 1U,
		LGPS_TIME_MAX_S,
	};
	struct lgps_utc utc;
	uint32_t restored_s;
	size_t index;

	for (index = 0U; index < ARRAY_SIZE(samples); index++) {
		restored_s = 0U;
		zassert_ok(lgps_time_to_utc(samples[index], &utc),
			   "to_utc(%u) must succeed", samples[index]);
		zassert_ok(lgps_time_from_utc(&utc, &restored_s),
			   "from_utc must succeed for case %u", (uint32_t)index);
		zassert_equal(restored_s, samples[index],
			      "round trip of %u must be exact", samples[index]);
	}
}

ZTEST(lifegps_time, test_from_utc_rejects_out_of_range_fields)
{
	struct lgps_utc utc = {2024U, 6U, 15U, 12U, 30U, 45U};
	uint32_t epoch_s = 0U;

	zassert_equal(lgps_time_from_utc(NULL, &epoch_s), -EINVAL, "NULL utc");
	zassert_equal(lgps_time_from_utc(&utc, NULL), -EINVAL, "NULL output");

	utc.month = 0U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "month 0");
	utc.month = 13U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "month 13");
	utc.month = 6U;

	utc.day = 0U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "day 0");
	utc.day = 31U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "June 31");
	utc.day = 15U;

	utc.hour = 24U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "hour 24");
	utc.hour = 12U;

	utc.minute = 60U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "minute 60");
	utc.minute = 30U;

	utc.second = 60U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "second 60");
	utc.second = 45U;

	utc.month = 2U;
	utc.day = 29U;
	utc.year = 2023U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "2023-02-29");
	utc.year = 2024U;
	zassert_ok(lgps_time_from_utc(&utc, &epoch_s), "2024-02-29 is valid");
}

ZTEST(lifegps_time, test_from_utc_rejects_dates_outside_epoch_range)
{
	struct lgps_utc utc = {2019U, 12U, 31U, 23U, 59U, 59U};
	uint32_t epoch_s = 0U;

	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "before the epoch");

	utc.year = 2156U;
	utc.month = 2U;
	utc.day = 7U;
	utc.hour = 6U;
	utc.minute = 28U;
	utc.second = 16U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL,
		      "one second past uint32_t");

	utc.year = 2157U;
	utc.month = 1U;
	utc.day = 1U;
	utc.hour = 0U;
	utc.minute = 0U;
	utc.second = 0U;
	zassert_equal(lgps_time_from_utc(&utc, &epoch_s), -EINVAL, "2157 overflows");
}

ZTEST(lifegps_time, test_to_utc_rejects_null)
{
	zassert_equal(lgps_time_to_utc(0U, NULL), -EINVAL, "NULL output");
}

static void time_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Every clock test starts from the same state as a fresh boot. */
	lgps_time_init();
}

ZTEST(lifegps_time, test_build_time_parse)
{
	struct lgps_utc utc;
	uint32_t epoch_s = 0U;

	zassert_equal(lgps_time_build_utc(NULL), -EINVAL, "NULL output");
	zassert_ok(lgps_time_build_utc(&utc), "__DATE__/__TIME__ must parse");

	zassert_true(utc.year >= 2020U, "build date is inside the Life_GPS epoch");
	zassert_true(utc.year <= 2156U, "build date is inside the Life_GPS epoch");
	zassert_true((utc.month >= 1U) && (utc.month <= 12U), "month range");
	zassert_true((utc.day >= 1U) && (utc.day <= 31U), "day range");
	zassert_true(utc.hour <= 23U, "hour range");
	zassert_true(utc.minute <= 59U, "minute range");
	zassert_true(utc.second <= 59U, "second range");

	zassert_ok(lgps_time_from_utc(&utc, &epoch_s), "parsed date must convert");
	zassert_equal(lgps_time_now_utc_s(0U), epoch_s,
		      "boot value is the build timestamp at uptime 0");
}

ZTEST(lifegps_time, test_clock_runs_from_build_then_gnss_overrides)
{
	struct lgps_utc build;
	uint32_t build_s = 0U;

	zassert_ok(lgps_time_build_utc(&build), "build timestamp");
	zassert_ok(lgps_time_from_utc(&build, &build_s), "build epoch");

	zassert_false(lgps_time_synced(), "seeding from build time is not a sync");
	zassert_equal(lgps_time_now_utc_s(0U), build_s, "seed value");
	zassert_equal(lgps_time_now_utc_s(4999U), build_s + 4U, "sub-second uptime");
	zassert_equal(lgps_time_now_utc_s(5000U), build_s + 5U, "five seconds later");

	zassert_true(lgps_time_sync(200000000U, 60000U), "GNSS override accepted");
	zassert_true(lgps_time_synced(), "accepted sync sets the flag");
	zassert_equal(lgps_time_now_utc_s(60000U), 200000000U, "anchored exactly");
	zassert_equal(lgps_time_now_utc_s(60999U), 200000000U, "still the same second");
	zassert_equal(lgps_time_now_utc_s(61000U), 200000001U, "clock advances");

	/* Losing the fix only stops re-anchoring; the clock keeps running. */
	zassert_equal(lgps_time_now_utc_s(161000U), 200000101U,
		      "no fix, still running on uptime");
}

ZTEST(lifegps_time, test_local_offset)
{
	uint32_t utc_s = 200000000U;
	struct lgps_utc local;
	uint32_t expected_local_s;

	zassert_equal(lgps_time_local_offset_s(), LGPS_TIME_LOCAL_OFFSET_DEFAULT_S,
		      "default offset is UTC+8");
	zassert_true(lgps_time_sync(utc_s, 1000U), "sync for local-time test");

	zassert_equal(lgps_time_now_local_s(1000U),
		      utc_s + (uint32_t)LGPS_TIME_LOCAL_OFFSET_DEFAULT_S, "default local");

	zassert_ok(lgps_time_set_local_offset_s(0), "UTC offset");
	zassert_equal(lgps_time_now_local_s(1000U), utc_s, "UTC local time");

	zassert_ok(lgps_time_set_local_offset_s(3600), "UTC+1");
	zassert_equal(lgps_time_now_local_s(1000U), utc_s + 3600U, "UTC+1 local time");

	zassert_ok(lgps_time_set_local_offset_s(-7200), "UTC-2");
	zassert_equal(lgps_time_now_local_s(1000U), utc_s - 7200U, "UTC-2 local time");

	zassert_ok(lgps_time_set_local_offset_s(LGPS_TIME_LOCAL_OFFSET_MIN_S),
		   "minimum offset accepted");
	zassert_ok(lgps_time_set_local_offset_s(LGPS_TIME_LOCAL_OFFSET_MAX_S),
		   "maximum offset accepted");

	expected_local_s = utc_s + (uint32_t)LGPS_TIME_LOCAL_OFFSET_MAX_S;
	zassert_true(lgps_time_now_local(&local, 1000U), "decompose local time");
	zassert_equal((uint32_t)local.year, 2026U, "local year");
	zassert_equal((uint32_t)local.month, 5U, "local month");
	zassert_equal((uint32_t)local.day, 4U, "local day");

	zassert_equal(lgps_time_set_local_offset_s(LGPS_TIME_LOCAL_OFFSET_MIN_S - 1),
		      -EINVAL, "below UTC-12h rejected");
	zassert_equal(lgps_time_set_local_offset_s(LGPS_TIME_LOCAL_OFFSET_MAX_S + 1),
		      -EINVAL, "above UTC+14h rejected");
	zassert_false(lgps_time_now_local(NULL, 1000U), "NULL local output");
	zassert_equal(lgps_time_now_local_s(1000U), expected_local_s,
		      "offset stays at the last accepted value");
}

ZTEST(lifegps_time, test_uptime_wraparound)
{
	/* Anchor just before the 32-bit uptime wrap: the unsigned difference is
	 * 0x1000 ms = 4 s, not a huge number.
	 */
	zassert_true(lgps_time_sync(220000000U, 0xFFFFFF00U), "wrap anchor");
	zassert_equal(lgps_time_now_utc_s(0xFFFFFF00U), 220000000U, "at the anchor");
	zassert_equal(lgps_time_now_utc_s(0xFFFFFFFFU), 220000000U, "255 ms later");
	zassert_equal(lgps_time_now_utc_s(0x00000000U), 220000000U, "256 ms later");
	zassert_equal(lgps_time_now_utc_s(0x00000F00U), 220000004U,
		      "0x1000 ms across the wrap is four seconds");
}

ZTEST(lifegps_time, test_sync_rejects_invalid_anchor)
{
	struct lgps_utc build;
	uint32_t build_s = 0U;
	uint32_t before_s;

	zassert_ok(lgps_time_build_utc(&build), "build timestamp");
	zassert_ok(lgps_time_from_utc(&build, &build_s), "build epoch");

	before_s = lgps_time_now_utc_s(0U);
	zassert_equal(before_s, build_s, "still on the build clock");

	zassert_false(lgps_time_sync(0U, 1000U), "epoch 0 is not a date");
	zassert_false(lgps_time_synced(), "rejected sync leaves the flag clear");
	zassert_equal(lgps_time_now_utc_s(0U), before_s, "and leaves the clock alone");

	/* Both cases are anchored at uptime 0 so the running clock is exactly
	 * build_s: at uptime 1000 the clock has already advanced by one second and
	 * the "+1" case would sit exactly on the limit instead of one past it.
	 */
	zassert_false(lgps_time_sync(build_s + (uint32_t)LGPS_TIME_MAX_SYNC_SKEW_S + 1U, 0U),
		      "more than ten years away is untrusted");
	zassert_false(lgps_time_synced(), "second rejection leaves the flag clear");

	zassert_true(lgps_time_sync(build_s + (uint32_t)LGPS_TIME_MAX_SYNC_SKEW_S, 0U),
		     "exactly ten years is still accepted");
	zassert_true(lgps_time_synced(), "accepted boundary sync");
}

ZTEST(lifegps_time, test_external_time_and_gnss_priority)
{
	int32_t drift_s = 0;

	zassert_equal(lgps_time_source(), LGPS_TIME_SRC_BUILD, "starts from build time");
	zassert_false(lgps_time_set_external(0U, 1000U, &drift_s), "epoch 0 rejected");

	zassert_true(lgps_time_set_external(210000000U, 5000U, &drift_s),
		     "external SET_TIME accepted before GNSS");
	zassert_equal(lgps_time_source(), LGPS_TIME_SRC_EXTERNAL, "source is EXTERNAL");
	zassert_false(lgps_time_synced(), "external time does not pretend to be GNSS");
	zassert_equal(lgps_time_now_utc_s(7000U), 210000002U, "external clock runs on uptime");

	/* Once GNSS anchors, GNSS takes precedence and subsequent external writes do not overwrite it. */
	zassert_true(lgps_time_sync(210000100U, 10000U), "GNSS sync accepted");
	zassert_equal(lgps_time_source(), LGPS_TIME_SRC_GNSS, "source is GNSS");
	zassert_true(lgps_time_set_external(210000500U, 12000U, &drift_s),
		     "external SET_TIME still succeeds for drift report");
	zassert_equal(drift_s, -398, "drift reports previous_utc - host_utc");
	zassert_equal(lgps_time_source(), LGPS_TIME_SRC_GNSS, "GNSS remains authoritative");
	zassert_equal(lgps_time_now_utc_s(12000U), 210000102U, "GNSS anchor untouched");
}

ZTEST_SUITE(lifegps_time, NULL, NULL, time_before, NULL, NULL);
