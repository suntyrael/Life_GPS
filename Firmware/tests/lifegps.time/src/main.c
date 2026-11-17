/*
 * ztest suite for Firmware/src/util/time.c.
 *
 * Expected values are derived from the proleptic Gregorian calendar anchored at
 * 2020-01-01T00:00:00Z; 2156-02-07T06:28:15Z is the last second representable
 * in uint32_t and must map to LGPS_TIME_MAX_S.
 *
 * SPDX-License-Identifier: Apache-2.0
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

ZTEST_SUITE(lifegps_time, NULL, NULL, NULL, NULL, NULL);
