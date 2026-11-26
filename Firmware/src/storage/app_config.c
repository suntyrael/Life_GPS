/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - pure-C Config TLV codec & validation (tags 0x01..0x0B).
 */

#include <errno.h>
#include <string.h>

#include <lifegps/app_config.h>
#include <lifegps/crc.h>

static void put_u16(uint8_t *out, uint16_t v)
{
	out[0] = (uint8_t)(v & 0xFFU);
	out[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void put_u32(uint8_t *out, uint32_t v)
{
	out[0] = (uint8_t)(v & 0xFFU);
	out[1] = (uint8_t)((v >> 8) & 0xFFU);
	out[2] = (uint8_t)((v >> 16) & 0xFFU);
	out[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint16_t get_u16(const uint8_t *in)
{
	return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

void lgps_app_config_defaults(struct lgps_app_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	memset(cfg, 0, sizeof(*cfg));
	cfg->record_mode = 1U;
	cfg->gnss_rate_hz = 1U;
	cfg->gnss_cold_timeout_s = 60U;
	cfg->archive_hour_utc = 19U; /* 03:00 UTC+8 */
	cfg->led_enable = 1U;
	cfg->auto_off_min = 30U;
	cfg->pdr_step_k_mm = 415U;
	cfg->mag_anomaly_ut = 48U;
	cfg->tz_offset_min = 480; /* UTC+8 */
	cfg->ble_adv_interval_ms = 500U;
	cfg->log_level_default = 2U;
}

int lgps_app_config_apply_tlv(struct lgps_app_config *cfg, const uint8_t *tlv, size_t len)
{
	struct lgps_app_config staging;
	size_t pos = 0U;

	if ((cfg == NULL) || (tlv == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	staging = *cfg;

	while (pos < len) {
		uint8_t tag;
		uint8_t vlen;
		const uint8_t *val;

		if ((len - pos) < 2U) {
			return -EINVAL;
		}
		tag = tlv[pos];
		vlen = tlv[pos + 1U];
		pos += 2U;
		if ((len - pos) < vlen) {
			return -EINVAL;
		}
		val = &tlv[pos];
		pos += vlen;

		switch (tag) {
		case LGPS_TLV_RECORD_MODE:
			if ((vlen != LGPS_TLV_RECORD_MODE_LEN) || (val[0] > 2U)) {
				return -ERANGE;
			}
			staging.record_mode = val[0];
			break;
		case LGPS_TLV_GNSS_RATE_HZ:
			if ((vlen != LGPS_TLV_GNSS_RATE_HZ_LEN) || (val[0] < 1U) || (val[0] > 10U)) {
				return -ERANGE;
			}
			staging.gnss_rate_hz = val[0];
			break;
		case LGPS_TLV_GNSS_COLD_TIMEOUT_S: {
			uint16_t v;

			if (vlen != LGPS_TLV_GNSS_COLD_TIMEOUT_S_LEN) {
				return -EINVAL;
			}
			v = get_u16(val);
			if ((v < 30U) || (v > 300U)) {
				return -ERANGE;
			}
			staging.gnss_cold_timeout_s = v;
			break;
		}
		case LGPS_TLV_ARCHIVE_HOUR_UTC:
			if ((vlen != LGPS_TLV_ARCHIVE_HOUR_UTC_LEN) || (val[0] > 23U)) {
				return -ERANGE;
			}
			staging.archive_hour_utc = val[0];
			break;
		case LGPS_TLV_LED_ENABLE:
			if ((vlen != LGPS_TLV_LED_ENABLE_LEN) || (val[0] > 1U)) {
				return -ERANGE;
			}
			staging.led_enable = val[0];
			break;
		case LGPS_TLV_AUTO_OFF_MIN: {
			uint16_t v;

			if (vlen != LGPS_TLV_AUTO_OFF_MIN_LEN) {
				return -EINVAL;
			}
			v = get_u16(val);
			if (v > 1440U) {
				return -ERANGE;
			}
			staging.auto_off_min = v;
			break;
		}
		case LGPS_TLV_PDR_STEP_K_MM: {
			uint16_t v;

			if (vlen != LGPS_TLV_PDR_STEP_K_MM_LEN) {
				return -EINVAL;
			}
			v = get_u16(val);
			if ((v < 300U) || (v > 550U)) {
				return -ERANGE;
			}
			staging.pdr_step_k_mm = v;
			break;
		}
		case LGPS_TLV_MAG_ANOMALY_UT: {
			uint16_t v;

			if (vlen != LGPS_TLV_MAG_ANOMALY_UT_LEN) {
				return -EINVAL;
			}
			v = get_u16(val);
			if ((v < 20U) || (v > 120U)) {
				return -ERANGE;
			}
			staging.mag_anomaly_ut = v;
			break;
		}
		case LGPS_TLV_TZ_OFFSET_MIN: {
			int16_t v;

			if (vlen != LGPS_TLV_TZ_OFFSET_MIN_LEN) {
				return -EINVAL;
			}
			v = (int16_t)get_u16(val);
			if ((v < -720) || (v > 840)) {
				return -ERANGE;
			}
			staging.tz_offset_min = v;
			break;
		}
		case LGPS_TLV_BLE_ADV_INTERVAL_MS: {
			uint16_t v;

			if (vlen != LGPS_TLV_BLE_ADV_INTERVAL_MS_LEN) {
				return -EINVAL;
			}
			v = get_u16(val);
			if ((v < 100U) || (v > 4000U)) {
				return -ERANGE;
			}
			staging.ble_adv_interval_ms = v;
			break;
		}
		case LGPS_TLV_LOG_LEVEL_DEFAULT:
			if ((vlen != LGPS_TLV_LOG_LEVEL_DEFAULT_LEN) || (val[0] > 4U)) {
				return -ERANGE;
			}
			staging.log_level_default = val[0];
			break;
		default:
			return -ENOTSUP;
		}
	}

	*cfg = staging;
	return 0;
}

static size_t append_tlv_u8(uint8_t *out, size_t pos, uint8_t tag, uint8_t v)
{
	out[pos++] = tag;
	out[pos++] = 1U;
	out[pos++] = v;
	return pos;
}

static size_t append_tlv_u16(uint8_t *out, size_t pos, uint8_t tag, uint16_t v)
{
	out[pos++] = tag;
	out[pos++] = 2U;
	put_u16(&out[pos], v);
	return pos + 2U;
}

int lgps_app_config_encode_all_tlv(const struct lgps_app_config *cfg, uint8_t *out, size_t cap)
{
	size_t pos = 0U;

	/* 5 x (2+1) + 6 x (2+2) = 15 + 24 = 39 bytes total. */
	if ((cfg == NULL) || (out == NULL) || (cap < 39U)) {
		return -EINVAL;
	}

	pos = append_tlv_u8(out, pos, LGPS_TLV_RECORD_MODE, cfg->record_mode);
	pos = append_tlv_u8(out, pos, LGPS_TLV_GNSS_RATE_HZ, cfg->gnss_rate_hz);
	pos = append_tlv_u16(out, pos, LGPS_TLV_GNSS_COLD_TIMEOUT_S, cfg->gnss_cold_timeout_s);
	pos = append_tlv_u8(out, pos, LGPS_TLV_ARCHIVE_HOUR_UTC, cfg->archive_hour_utc);
	pos = append_tlv_u8(out, pos, LGPS_TLV_LED_ENABLE, cfg->led_enable);
	pos = append_tlv_u16(out, pos, LGPS_TLV_AUTO_OFF_MIN, cfg->auto_off_min);
	pos = append_tlv_u16(out, pos, LGPS_TLV_PDR_STEP_K_MM, cfg->pdr_step_k_mm);
	pos = append_tlv_u16(out, pos, LGPS_TLV_MAG_ANOMALY_UT, cfg->mag_anomaly_ut);
	pos = append_tlv_u16(out, pos, LGPS_TLV_TZ_OFFSET_MIN, (uint16_t)cfg->tz_offset_min);
	pos = append_tlv_u16(out, pos, LGPS_TLV_BLE_ADV_INTERVAL_MS, cfg->ble_adv_interval_ms);
	pos = append_tlv_u8(out, pos, LGPS_TLV_LOG_LEVEL_DEFAULT, cfg->log_level_default);

	return (int)pos;
}

int lgps_app_config_record_encode(const struct lgps_app_config *cfg, uint32_t seq,
				  uint8_t out[LGPS_APP_CONFIG_RECORD_SIZE])
{
	if ((cfg == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	memset(out, 0, LGPS_APP_CONFIG_RECORD_SIZE);
	put_u32(&out[0], LGPS_APP_CONFIG_RECORD_MAGIC);
	put_u32(&out[4], seq);
	out[8] = cfg->record_mode;
	out[9] = cfg->gnss_rate_hz;
	put_u16(&out[10], cfg->gnss_cold_timeout_s);
	out[12] = cfg->archive_hour_utc;
	out[13] = cfg->led_enable;
	put_u16(&out[14], cfg->auto_off_min);
	put_u16(&out[16], cfg->pdr_step_k_mm);
	put_u16(&out[18], cfg->mag_anomaly_ut);
	put_u16(&out[20], (uint16_t)cfg->tz_offset_min);
	put_u16(&out[22], cfg->ble_adv_interval_ms);
	out[24] = cfg->log_level_default;
	put_u16(&out[46], lgps_crc16_ccitt_false(out, 46U));
	return 0;
}

int lgps_app_config_record_decode(const uint8_t in[LGPS_APP_CONFIG_RECORD_SIZE],
				  struct lgps_app_config *cfg, uint32_t *seq_out)
{
	if ((in == NULL) || (cfg == NULL) || (seq_out == NULL)) {
		return -EINVAL;
	}

	if (get_u32(&in[0]) != LGPS_APP_CONFIG_RECORD_MAGIC) {
		return -EBADMSG;
	}
	if (get_u16(&in[46]) != lgps_crc16_ccitt_false(in, 46U)) {
		return -EBADMSG;
	}

	*seq_out = get_u32(&in[4]);
	cfg->record_mode = in[8];
	cfg->gnss_rate_hz = in[9];
	cfg->gnss_cold_timeout_s = get_u16(&in[10]);
	cfg->archive_hour_utc = in[12];
	cfg->led_enable = in[13];
	cfg->auto_off_min = get_u16(&in[14]);
	cfg->pdr_step_k_mm = get_u16(&in[16]);
	cfg->mag_anomaly_ut = get_u16(&in[18]);
	cfg->tz_offset_min = (int16_t)get_u16(&in[20]);
	cfg->ble_adv_interval_ms = get_u16(&in[22]);
	cfg->log_level_default = in[24];
	return 0;
}
