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

/*
 * System clock (RTC) on the Life_GPS epoch.
 *
 * The clock is seeded from this image's compile time (__DATE__ / __TIME__),
 * runs on caller-supplied uptime and is re-anchored by GNSS UTC whenever a
 * NAV-PVT validates its date/time. Nothing here touches an RTC peripheral or
 * libc <time.h>, so the module stays usable on native_sim.
 *
 * Semantics:
 * - after lgps_time_init() the clock is valid immediately and is NOT "synced";
 * - every accepted lgps_time_sync() re-anchors it and sets synced = true;
 * - losing the GNSS fix does not stop the clock, it keeps running on uptime;
 * - uptime is milliseconds from the caller (lgps_diag_uptime_ms()), uint32_t
 *   wrap is part of the contract and is handled with unsigned subtraction;
 * - a return value of 0 from the epoch getters means "clock unavailable"
 *   (build-time parse failed and no GNSS UTC arrived yet).
 */

/* Local time defaults to UTC+8, the product's target market. */
#define LGPS_TIME_LOCAL_OFFSET_DEFAULT_S (8 * 3600)
#define LGPS_TIME_LOCAL_OFFSET_MIN_S (-12 * 3600)
#define LGPS_TIME_LOCAL_OFFSET_MAX_S (14 * 3600)

/* Reject a GNSS UTC that differs from the running clock by more than this. */
#define LGPS_TIME_MAX_SYNC_SKEW_S (10ULL * 365ULL * 86400ULL)

/*
 * Parse __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss") into UTC.
 *
 * Returns -EINVAL for a NULL pointer or a malformed/out-of-epoch timestamp
 * (the C standard fixes the two formats, including the space-padded day and
 * the English three-letter month abbreviation).
 */
int lgps_time_build_utc(struct lgps_utc *out);

/*
 * Seed the clock from the build timestamp. Call once at boot, from thread
 * context, after the logging backend is up. A build timestamp outside the
 * Life_GPS epoch leaves the clock unavailable (getters return 0) until the
 * first GNSS sync; a later GNSS-only clock is still a valid clock.
 */
void lgps_time_init(void);

/*
 * Anchor the clock to GNSS UTC (Life_GPS epoch seconds) at @p uptime_ms.
 *
 * Returns true when applied. Returns false and leaves the clock untouched when
 * @p utc_epoch_s is 0/out of range or when it is more than
 * LGPS_TIME_MAX_SYNC_SKEW_S away from the current clock (implausible data);
 * the caller owns any warning log. Seeding via lgps_time_init() does not count
 * as a sync.
 */
bool lgps_time_sync(uint32_t utc_epoch_s, uint32_t uptime_ms);

enum lgps_time_source {
	LGPS_TIME_SRC_BUILD = 0,
	LGPS_TIME_SRC_EXTERNAL = 1,
	LGPS_TIME_SRC_GNSS = 2,
};

/*
 * Set the clock from an external host (BLE SET_TIME 0x11) at @p uptime_ms.
 *
 * Returns true when applied (or accepted while GNSS remains authoritative).
 * When @p out_drift_s is non-NULL it receives (previous_utc - epoch_s), or
 * INT32_MAX when the clock was previously unavailable.
 */
bool lgps_time_set_external(uint32_t epoch_s, uint32_t uptime_ms, int32_t *out_drift_s);

/* Current clock authority: BUILD (0), EXTERNAL (1) or GNSS (2). */
enum lgps_time_source lgps_time_source(void);

/* True only after at least one accepted GNSS sync in this boot. */
bool lgps_time_synced(void);

/* Current UTC / local epoch seconds, or 0 when the clock is unavailable. */
uint32_t lgps_time_now_utc_s(uint32_t uptime_ms);
uint32_t lgps_time_now_local_s(uint32_t uptime_ms);

/* Local time decomposition (UTC + offset) for log formatting. */
bool lgps_time_now_local(struct lgps_utc *out, uint32_t uptime_ms);

/* Current local offset in seconds. */
int32_t lgps_time_local_offset_s(void);

/*
 * Set the local offset; valid range is
 * LGPS_TIME_LOCAL_OFFSET_MIN_S .. LGPS_TIME_LOCAL_OFFSET_MAX_S (UTC-12h..UTC+14h).
 * Returns -EINVAL outside that range.
 */
int lgps_time_set_local_offset_s(int32_t offset_s);

#endif /* LIFEGPS_TIME_H_ */
