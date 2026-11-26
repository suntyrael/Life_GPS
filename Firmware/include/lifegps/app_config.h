/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - device configuration TLV model (tags 0x01..0x0B).
 *
 * Pure C encode/decode and bounds validation (unit tested on native_sim);
 * NOR sector 18 persistence lives in app_config_store.c.
 */

#ifndef LIFEGPS_APP_CONFIG_H_
#define LIFEGPS_APP_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <generated/lifegps_protocol.h>

#define LGPS_APP_CONFIG_RECORD_MAGIC 0x43464731U /* "CFG1" */
#define LGPS_APP_CONFIG_RECORD_SIZE 48U

struct lgps_app_config {
	uint8_t record_mode;          /* 0=DEEP_SLEEP, 1=ACTIVE, 2=HIGH_PRECISION */
	uint8_t gnss_rate_hz;         /* 1..10 */
	uint16_t gnss_cold_timeout_s; /* 30..300 */
	uint8_t archive_hour_utc;     /* 0..23 */
	uint8_t led_enable;           /* 0 or 1 */
	uint16_t auto_off_min;        /* 0..1440 (0 = disabled) */
	uint16_t pdr_step_k_mm;       /* 300..550 (0.30..0.55) */
	uint16_t mag_anomaly_ut;      /* 20..120 */
	int16_t tz_offset_min;        /* -720..+840 */
	uint16_t ble_adv_interval_ms; /* 100..4000 */
	uint8_t log_level_default;    /* 0..4 */
};

/* Populate factory defaults. */
void lgps_app_config_defaults(struct lgps_app_config *cfg);

/* Apply one or more [tag:1][len:1][val:len] TLVs atomically (all-or-nothing validation). */
int lgps_app_config_apply_tlv(struct lgps_app_config *cfg, const uint8_t *tlv, size_t len);

/* Serialize all 11 TLVs into @p out (returns encoded byte count or negative errno). */
int lgps_app_config_encode_all_tlv(const struct lgps_app_config *cfg, uint8_t *out, size_t cap);

/* Encode/decode 48-byte NOR persistence record with CRC-16. */
int lgps_app_config_record_encode(const struct lgps_app_config *cfg, uint32_t seq,
				  uint8_t out[LGPS_APP_CONFIG_RECORD_SIZE]);
int lgps_app_config_record_decode(const uint8_t in[LGPS_APP_CONFIG_RECORD_SIZE],
				  struct lgps_app_config *cfg, uint32_t *seq_out);

/* Runtime singleton backed by external NOR sector 18 (app_config_store.c). */
int lgps_app_config_init(void);
void lgps_app_config_get(struct lgps_app_config *out);
int lgps_app_config_update_tlv(const uint8_t *tlv, size_t len);
int lgps_app_config_factory_reset(void);

#endif /* LIFEGPS_APP_CONFIG_H_ */
