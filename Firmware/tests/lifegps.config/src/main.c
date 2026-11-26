/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS - Config TLV codec & NOR record persistence unit tests (native_sim).
 */

#include <errno.h>
#include <zephyr/ztest.h>

#include <lifegps/app_config.h>

ZTEST(lifegps_config, test_encode_decode_all_11_tags)
{
	struct lgps_app_config orig;
	struct lgps_app_config parsed;
	uint8_t tlv[64];
	int len;

	lgps_app_config_defaults(&orig);
	orig.record_mode = 2U;
	orig.gnss_rate_hz = 5U;
	orig.gnss_cold_timeout_s = 90U;
	orig.archive_hour_utc = 19U;
	orig.led_enable = 0U;
	orig.auto_off_min = 60U;
	orig.pdr_step_k_mm = 425U;
	orig.mag_anomaly_ut = 55U;
	orig.tz_offset_min = -300;
	orig.ble_adv_interval_ms = 500U;
	orig.log_level_default = 3U;

	len = lgps_app_config_encode_all_tlv(&orig, tlv, sizeof(tlv));
	zassert_true(len > 0, "encoded non-empty TLV");

	lgps_app_config_defaults(&parsed);
	zassert_equal(lgps_app_config_apply_tlv(&parsed, tlv, (size_t)len), 0, "apply all TLVs");
	zassert_equal(parsed.record_mode, 2U, "record_mode");
	zassert_equal(parsed.gnss_rate_hz, 5U, "gnss_rate_hz");
	zassert_equal(parsed.gnss_cold_timeout_s, 90U, "gnss_cold_timeout_s");
	zassert_equal(parsed.archive_hour_utc, 19U, "archive_hour_utc");
	zassert_equal(parsed.led_enable, 0U, "led_enable");
	zassert_equal(parsed.auto_off_min, 60U, "auto_off_min");
	zassert_equal(parsed.pdr_step_k_mm, 425U, "pdr_step_k_mm");
	zassert_equal(parsed.mag_anomaly_ut, 55U, "mag_anomaly_ut");
	zassert_equal(parsed.tz_offset_min, -300, "tz_offset_min");
	zassert_equal(parsed.ble_adv_interval_ms, 500U, "ble_adv_interval_ms");
	zassert_equal(parsed.log_level_default, 3U, "log_level_default");
}

ZTEST(lifegps_config, test_atomic_rollback_on_out_of_range)
{
	struct lgps_app_config cfg;
	/* Tag 1 valid (mode=2), then Tag 2 invalid (gnss_rate_hz=25 > 10). */
	const uint8_t bad_tlv[] = {
		LGPS_TLV_RECORD_MODE, 1U, 2U,
		LGPS_TLV_GNSS_RATE_HZ, 1U, 25U,
	};

	lgps_app_config_defaults(&cfg);
	zassert_equal(lgps_app_config_apply_tlv(&cfg, bad_tlv, sizeof(bad_tlv)), -ERANGE,
		      "out of range rejected");
	zassert_equal(cfg.record_mode, 1U, "previous valid tag rolled back atomically");
}

ZTEST(lifegps_config, test_nor_record_roundtrip_and_crc)
{
	struct lgps_app_config orig;
	struct lgps_app_config decoded;
	uint8_t rec[LGPS_APP_CONFIG_RECORD_SIZE];
	uint32_t seq = 0U;

	lgps_app_config_defaults(&orig);
	orig.tz_offset_min = 480;
	orig.pdr_step_k_mm = 412U;

	zassert_equal(lgps_app_config_record_encode(&orig, 77U, rec), 0, "record encode");
	zassert_equal(lgps_app_config_record_decode(rec, &decoded, &seq), 0, "record decode");
	zassert_equal(seq, 77U, "sequence preserved");
	zassert_equal(decoded.tz_offset_min, 480, "tz preserved");
	zassert_equal(decoded.pdr_step_k_mm, 412U, "step_k preserved");

	/* Flip a bit -> CRC must fail */
	rec[12] ^= 0x80U;
	zassert_equal(lgps_app_config_record_decode(rec, &decoded, &seq), -EBADMSG,
		      "corrupted record rejected");
}

ZTEST_SUITE(lifegps_config, NULL, NULL, NULL, NULL, NULL);
