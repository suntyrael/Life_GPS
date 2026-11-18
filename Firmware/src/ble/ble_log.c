/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - forward Zephyr log output to the BLE Log characteristic.
 *
 * Why a log backend instead of a second formatting path: the DEBUG_UART console
 * stays the primary instrument (README section 9.2), and duplicating "log this
 * line" calls at every site would certainly drift. The backend sees exactly the
 * lines the console sees.
 *
 * Why a slot ring and not a direct notify: CONFIG_LOG_MODE_IMMEDIATE calls the
 * backend in the producer's context, which may be an ISR, and bt_gatt_notify()
 * is not ISR safe. The backend therefore only copies into a spinlock-protected
 * slot ring; src/ble/ble_gatt.c drains it from a thread.
 *
 * This file must not log: any LOG_* call here would re-enter the backend.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>

#include <lifegps/ble_frame.h>

#include "ble_internal.h"

/* Log formats a line into chunks; the console output buffer is the same size. */
#define LOG_LINE_BYTES LGPS_BLE_LOG_MAX_PACKET
#define LOG_SLOT_COUNT 8U

static char line_buf[LOG_LINE_BYTES];
static size_t line_len;
static uint8_t line_level;

static uint8_t slots[LOG_SLOT_COUNT][LGPS_BLE_LOG_MAX_PACKET];
static uint8_t slot_len[LOG_SLOT_COUNT];
static uint32_t slot_head;
static uint32_t slot_tail;
static struct k_spinlock slot_lock;

/* 0 = off, 1 = ERR .. 4 = DBG. INFO is the default: the boot banner and the
 * subsystem bring-up lines are what a phone user needs first. Atomic because the
 * backend reads it in the producer's context (possibly an ISR) while the
 * command thread writes it.
 */
static atomic_t subscribed_level = ATOMIC_INIT(3);

/* --- slot ring ------------------------------------------------------------- */

static void slot_push(const char *source, size_t source_len, const char *text, size_t text_len)
{
	k_spinlock_key_t key;
	uint32_t slot;
	size_t head_len;

	/* [level][src_len][source][text] must fit in one notification. */
	if (source_len > 32U) {
		source_len = 32U;
	}

	if (source_len + text_len + 2U > LGPS_BLE_LOG_MAX_PACKET) {
		text_len = LGPS_BLE_LOG_MAX_PACKET - 2U - source_len;
	}

	key = k_spin_lock(&slot_lock);
	if ((slot_head - slot_tail) >= LOG_SLOT_COUNT) {
		/* Dropping the newest line keeps the ordering of what is queued;
		 * the counter that would report it is not wired to a UI yet, and
		 * inventing one would cost RAM on a 256 KB device.
		 */
		k_spin_unlock(&slot_lock, key);
		return;
	}

	slot = slot_head % LOG_SLOT_COUNT;
	slots[slot][0] = (uint8_t)(line_level - 1U);
	slots[slot][1] = (uint8_t)source_len;
	memcpy(&slots[slot][2], source, source_len);
	memcpy(&slots[slot][2U + source_len], text, text_len);
	head_len = 2U + source_len + text_len;
	slot_len[slot] = (uint8_t)head_len;
	slot_head++;
	k_spin_unlock(&slot_lock, key);
}

size_t lgps_ble_log_pop(uint8_t *out, size_t capacity)
{
	k_spinlock_key_t key;
	uint32_t slot;
	size_t len;

	if ((out == NULL) || (capacity == 0U)) {
		return 0U;
	}

	key = k_spin_lock(&slot_lock);
	if (slot_head == slot_tail) {
		k_spin_unlock(&slot_lock, key);

		return 0U;
	}

	slot = slot_tail % LOG_SLOT_COUNT;
	len = slot_len[slot];
	if (len > capacity) {
		len = capacity;
	}

	memcpy(out, slots[slot], len);
	slot_tail++;
	k_spin_unlock(&slot_lock, key);

	return len;
}

int lgps_ble_log_set_level(uint8_t level)
{
	if (level > 4U) {
		return -EINVAL;
	}

	atomic_set(&subscribed_level, (atomic_val_t)level);

	return 0;
}

/* --- formatting ------------------------------------------------------------ */

/*
 * Zephyr's text format is "<inf> module_name: message". The module name is
 * parsed out of the formatted line instead of through log_msg_get_source():
 * the source accessors live behind CONFIG_LOG_ALWAYS_RUNTIME and changed shape
 * between Zephyr releases, while the text format is the frozen contract of the
 * console this firmware already ships.
 */
static void emit_line(void)
{
	const char *source = "";
	size_t source_len = 0U;
	const char *text = line_buf;
	size_t text_len = line_len;

	for (size_t index = 0U; index + 1U < line_len; index++) {
		if ((line_buf[index] == '>') && (line_buf[index + 1U] == ' ')) {
			size_t start = index + 2U;
			size_t end = start;

			while ((end < line_len) && (line_buf[end] != ':')) {
				end++;
			}

			if (end < line_len) {
				source = &line_buf[start];
				source_len = end - start;
				/* Skip the ": " that follows the module name. */
				text = &line_buf[end + 1U];
				if ((line_len > (end + 2U)) && (line_buf[end + 1U] == ' ')) {
					text = &line_buf[end + 2U];
				}
				text_len = (size_t)(&line_buf[line_len] - text);
			}
			break;
		}
	}

	slot_push(source, source_len, text, text_len);

	line_len = 0U;
}

static int char_out(uint8_t *buf, size_t size, void *ctx)
{
	ARG_UNUSED(ctx);

	if (buf == NULL) {
		return 0;
	}

	for (size_t index = 0U; index < size; index++) {
		char character = (char)buf[index];

		if (character == '\r') {
			continue;
		}

		if (character == '\n') {
			emit_line();
			continue;
		}

		if ((line_len + 1U) < sizeof(line_buf)) {
			line_buf[line_len] = character;
			line_len++;
		}
		/* Longer lines are silently cut at the buffer size: the console
		 * keeps the full line, and a truncated BLE copy must not turn into
		 * a second log line that lies about what came next.
		 */
	}

	/* Must report every byte as processed: log_output_write() loops until the
	 * return value reaches zero and would spin forever on a short count.
	 */
	return (int)size;
}

static uint8_t output_buffer[LOG_LINE_BYTES];

LOG_OUTPUT_DEFINE(lgps_ble_log_output, char_out, output_buffer, sizeof(output_buffer));

/* --- backend --------------------------------------------------------------- */

static void backend_process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	uint8_t level;

	ARG_UNUSED(backend);

	level = log_msg_get_level(&msg->log);
	if (level > (uint8_t)atomic_get(&subscribed_level)) {
		return;
	}

	line_level = level;

	log_output_msg_process(&lgps_ble_log_output, &msg->log, LOG_OUTPUT_FLAG_LEVEL);
}

static void backend_dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(cnt);

	/* Nothing to do: a dropped log line must not become a log line itself. */
}

static const struct log_backend_api lgps_ble_backend_api = {
	.process = backend_process,
	.dropped = backend_dropped,
};

LOG_BACKEND_DEFINE(lgps_ble_backend, lgps_ble_backend_api, false);

void lgps_ble_log_backend_init(void)
{
	log_output_ctx_set(&lgps_ble_log_output, NULL);
	log_backend_enable(&lgps_ble_backend, NULL, CONFIG_LOG_MAX_LEVEL);
}
