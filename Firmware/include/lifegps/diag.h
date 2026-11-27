/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - UART diagnostic channel.
 *
 * DEBUG_TX = P0.00 / DEBUG_RX = P0.01 on the 7-pin H1 header (pinmap Main_V0.2
 * section 2), driven by UARTE30 at 115200 8N1: the LP-domain UART instance is the
 * only one whose pins live on port P0 (sdk-zephyr nrf54l_05_10_15.dtsi:
 * uart30@104000).
 *
 * Every log line carries the uptime in milliseconds and the module tag, because
 * the device has no screen: this channel is the primary bring-up instrument.
 */

#ifndef LIFEGPS_DIAG_H_
#define LIFEGPS_DIAG_H_

#include <stddef.h>
#include <stdint.h>

/* Milliseconds since boot, monotonic, wraps after 49.7 days. */
uint32_t lgps_diag_uptime_ms(void);

/*
 * Emit the boot banner: reset cause, build identity, pinmap revision and the
 * per-peripheral clock/power facts recorded in docs/M1_BRINGUP.md.
 * Must be called once, after the logging subsystem is up.
 */
void lgps_diag_banner(void);

/* Human readable reset cause as reported by the nRF54L15 RESETREAS registers. */
const char *lgps_diag_reset_reason(void);

/* Raw Zephyr hwinfo reset-cause bitmask cached before RESETREAS is cleared. */
uint32_t lgps_diag_reset_cause_raw(void);

/* Hex dump (tag + offset + 16 bytes per line) through the log backend. */
void lgps_diag_hexdump(const char *tag, const uint8_t *data, size_t len_bytes);

#endif /* LIFEGPS_DIAG_H_ */
