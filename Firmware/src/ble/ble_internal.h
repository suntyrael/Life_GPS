/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - private interface between the BLE link and its log
 * forwarder. Not part of the application API: only src/ble/ includes it.
 *
 * The log backend runs in the context that produced the log line, which with
 * CONFIG_LOG_MODE_IMMEDIATE includes ISRs, so it may only copy bytes into a
 * lock-protected slot ring. ble_gatt.c owns the thread that pops the ring and
 * performs the GATT notifications.
 */

#ifndef LIFEGPS_BLE_INTERNAL_H_
#define LIFEGPS_BLE_INTERNAL_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_LGPS_BLE_LOG)
/* Register the backend with the logging core (no-op if CONFIG_LOG is off). */
void lgps_ble_log_backend_init(void);

/* Block until at least one log slot is queued or @p timeout expires. */
int lgps_ble_log_wait(k_timeout_t timeout);

/*
 * Pop one encoded log packet: [level u8][src_len u8][source][text].
 * Returns the packet length, or 0 when the ring is empty.
 */
size_t lgps_ble_log_pop(uint8_t *out, size_t capacity);

/*
 * Set the forwarded level: 0 = off, 1 = ERR, 2 = +WRN, 3 = +INF, 4 = +DBG.
 * Returns 0 on success, -EINVAL for an out-of-range level.
 */
int lgps_ble_log_set_level(uint8_t level);
#else
static inline void lgps_ble_log_backend_init(void)
{
}

static inline int lgps_ble_log_wait(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	return -ENOTSUP;
}

static inline size_t lgps_ble_log_pop(uint8_t *out, size_t capacity)
{
	ARG_UNUSED(out);
	ARG_UNUSED(capacity);
	return 0U;
}

static inline int lgps_ble_log_set_level(uint8_t level)
{
	ARG_UNUSED(level);
	return -ENOTSUP;
}
#endif

#endif /* LIFEGPS_BLE_INTERNAL_H_ */
