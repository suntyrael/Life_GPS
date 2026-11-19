/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - Bluetooth LE link (the only user interface of a device
 * without a screen).
 *
 * Layout of what goes on the air: Doc/Software/BLE_Wire_v1.md. This file only
 * moves bytes: the GATT table, advertising, the status notifications and the
 * command dispatcher. Everything it can get wrong in a way a test can catch
 * lives in lifegps/ble_frame.h instead.
 *
 * Threading:
 *   main thread      lgps_ble_publish() once per second (see lgps_ble.h)
 *   command thread   parses one queued Control write, may touch flash (POI)
 *   log thread       drains lifegps/ble_log.c into the Log characteristic
 *   BT RX thread     only copies the frame and hands it over (control_write)
 *
 * Rationale: the BT RX thread must never block on flash or on a mutex held by
 * the main loop, and bt_gatt_notify() is not ISR safe - the log backend runs in
 * whatever context produced the log line (CONFIG_LOG_MODE_IMMEDIATE), so it
 * only writes to a ring buffer and the log thread does the notifying.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include <generated/lifegps_protocol.h>
#include <lifegps/ble.h>
#include <lifegps/ble_frame.h>
#include <lifegps/ble_uuid.h>
#include <lifegps/diag.h>
#include <lifegps/flash_ring.h>
#include <lifegps/led.h>
#include <lifegps/gnss.h>
#include <lifegps/telemetry.h>
#include <lifegps/traj.h>

#include "ble_internal.h"

LOG_MODULE_REGISTER(lgps_ble, LOG_LEVEL_INF);

/* Eight bytes of headroom below the 247-byte ATT MTU: the 100-byte Status frame
 * plus the 6-byte command response header must always fit without a long write.
 */
BUILD_ASSERT(LGPS_BLE_STATUS_SIZE <= LGPS_PROTO_MTU - 8,
	     "status frame must fit in one notification");

#define LGPS_BLE_NAME_SIZE 16
/*
 * Stack sizing, measured against what each thread actually runs:
 *   command thread: traj encode + flash append (mutex, erase possible) + GATT
 *                   notify chain + LOG formatting
 *   log thread:     240-byte packet buffer + GATT notify chain
 * The notify path goes ATT -> L2CAP -> HCI -> SDC and the log formatter
 * (CBPRINTF_COMPLETE + FP) is called in the caller's context because
 * CONFIG_LOG_MODE_IMMEDIATE is on, so 1 KB was not enough: the bench showed a
 * stack overflow in the notify-error path right after a disconnect.
 */
#define LGPS_BLE_CMD_THREAD_STACK 3072
#define LGPS_BLE_LOG_THREAD_STACK 3072
#define LGPS_BLE_THREAD_PRIO K_PRIO_PREEMPT(10)
#define LGPS_BLE_LOG_DRAIN_MS 20
#define LGPS_BLE_REBOOT_DELAY_MS 200
/*
 * Restart advertising *after* the disconnected callback returns, never inside it.
 * Connectable advertising needs a struct bt_conn slot (adv.c: le_adv_start_add_conn
 * -> bt_conn_add_le -> bt_conn_new over acl_conns[CONFIG_BT_MAX_CONN], default 1),
 * and the dying connection still holds that slot while callbacks run. Starting it
 * inline therefore fails with -ENOMEM (-12) and the device never becomes visible
 * again - which is exactly what happened on the bench.
 */
#define LGPS_BLE_ADV_RESTART_MS 250U
#define LGPS_BLE_ADV_RETRY_MS 1000U

/* --- state ----------------------------------------------------------------- */

static struct bt_uuid_128 lgps_svc_uuid = {
	.uuid = { .type = BT_UUID_TYPE_128 },
	.val = { LGPS_UUID_SERVICE_LE },
};
static struct bt_uuid_128 status_uuid = {
	.uuid = { .type = BT_UUID_TYPE_128 },
	.val = { LGPS_UUID_STATUS_LE },
};
static struct bt_uuid_128 control_uuid = {
	.uuid = { .type = BT_UUID_TYPE_128 },
	.val = { LGPS_UUID_CONTROL_LE },
};
static struct bt_uuid_128 log_uuid = {
	.uuid = { .type = BT_UUID_TYPE_128 },
	.val = { LGPS_UUID_LOG_LE },
};
static struct bt_uuid_128 telem_uuid = {
	.uuid = { .type = BT_UUID_TYPE_128 },
	.val = { LGPS_UUID_TELEMETRY_LE },
};

static ssize_t status_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset);
static ssize_t control_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static void status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void control_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void log_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void telem_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);

BT_GATT_SERVICE_DEFINE(lgps_svc,
	BT_GATT_PRIMARY_SERVICE(&lgps_svc_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&status_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ,
			       status_read, NULL, NULL),
	BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&control_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
				       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL, control_write, NULL),
	BT_GATT_CCC(control_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&log_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(log_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&telem_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(telem_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static struct bt_gatt_attr *status_attr;
static struct bt_gatt_attr *control_attr;
static struct bt_gatt_attr *log_attr;
static struct bt_gatt_attr *telem_attr;

/* Written by the BT RX thread (CCC callback), read by the main loop and by the
 * command thread. A single-byte store is atomic on Cortex-M, but it is shared
 * across threads, so it is declared volatile on purpose (no ISR is involved).
 */
static volatile bool status_subscribed;
static bool control_subscribed;
static bool log_subscribed;
static volatile bool telem_subscribed;
static bool debug_unlocked;
static bool reboot_pending;
static bool link_up;

static struct k_work_delayable reboot_work;
static struct k_work_delayable adv_restart_work;

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("rebooting on BLE request");
	sys_reboot(SYS_REBOOT_COLD);
}

/* Latest encoded Status frame. Written by the main thread, read by the GATT
 * read callback (BT host thread) and by the command thread (GET_STATUS), so the
 * 100-byte copy is short enough to sit behind a spinlock.
 */
static uint8_t status_frame[LGPS_BLE_STATUS_SIZE];

/* Free running per-id sequence numbers of the telemetry packets. Only touched by
 * the main thread, so no locking is needed.
 */
static uint16_t telem_seq_imu;
static uint16_t telem_seq_pdr;
static bool status_frame_valid;
static struct k_spinlock status_lock;

/* Published by lgps_ble_publish(); read by POI_MARK. Atomic because the command
 * thread must not see a half-updated pair.
 */
static atomic_t published_steps;

/* Counted instead of logged inside the notify path; reported from publish(), in
 * the main thread, where the log backend is not on the notification path.
 */
static atomic_t notify_failures;

static char device_name[LGPS_BLE_NAME_SIZE];

/* --- advertising ----------------------------------------------------------- */

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, LGPS_UUID_SERVICE_LE),
};

static struct bt_data scan_rsp_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, LGPS_BLE_NAME_SIZE),
};

static int start_advertising(void)
{
	scan_rsp_data[0].data_len = (uint8_t)strlen(device_name);
	/* BT_LE_ADV_CONN_FAST_1 is the GAP-recommended undirected connectable
	 * parameter set in Zephyr 4.4 (the generic BT_LE_ADV_CONN macro no longer
	 * exists there). The interval is the fast one on purpose: a screenless
	 * device must be discoverable in seconds, and the link is expected to be
	 * up for the whole debug session.
	 */
	return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data), scan_rsp_data,
			       ARRAY_SIZE(scan_rsp_data));
}

static void schedule_advertising(uint32_t delay_ms)
{
	(void)k_work_schedule(&adv_restart_work, K_MSEC(delay_ms));
}

static void adv_restart_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (link_up) {
		/* A central won the race and is already connected; advertising now
		 * would only fail with -EALREADY, and disconnected() will ask again
		 * when the link really goes down.
		 */
		return;
	}

	err = start_advertising();
	if (err == 0) {
		LOG_INF("advertising as %s", device_name);

		return;
	}

	/* Anything left here is a genuine failure: report it once per retry cycle
	 * instead of spamming every connection attempt.
	 */
	LOG_WRN("advertising restart failed (%d), retrying in %u ms", err,
		(unsigned int)LGPS_BLE_ADV_RETRY_MS);
	schedule_advertising(LGPS_BLE_ADV_RETRY_MS);
}

/* --- notifications --------------------------------------------------------- */

static void notify_guarded(struct bt_gatt_attr *attr, const uint8_t *data, uint16_t len)
{
	int err;

	if ((attr == NULL) || !link_up) {
		return;
	}

	err = bt_gatt_notify(NULL, attr, data, len);
	if (err != 0) {
		/* -ENOMEM (no ATT buffer) and -ENOTCONN (link gone between the CCC
		 * and this call) are both normal while a connection is draining or
		 * dying. Count them and nothing else: logging from inside the notify
		 * path re-enters the log backend in the caller's context (immediate
		 * mode), which both feeds itself - a failed log notification would
		 * produce another log line - and costs the stack exactly where the
		 * notify chain is already deepest.
		 */
		(void)atomic_inc(&notify_failures);
	}
}

/* --- GATT ------------------------------------------------------------------ */

static ssize_t status_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	uint8_t copy[LGPS_BLE_STATUS_SIZE];
	k_spinlock_key_t key;
	ssize_t result;

	key = k_spin_lock(&status_lock);
	memcpy(copy, status_frame, sizeof(copy));
	result = (ssize_t)status_frame_valid;
	k_spin_unlock(&status_lock, key);

	if (result == 0) {
		/* Before the first telemetry frame there is nothing truthful to
		 * report; returning zeros would look like a valid fix.
		 */
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, copy, sizeof(copy));
}

static void status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	uint8_t copy[LGPS_BLE_STATUS_SIZE];
	k_spinlock_key_t key;
	bool valid;

	ARG_UNUSED(attr);

	status_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("status notify %s", status_subscribed ? "on" : "off");

	if (!status_subscribed) {
		return;
	}

	key = k_spin_lock(&status_lock);
	memcpy(copy, status_frame, sizeof(copy));
	valid = status_frame_valid;
	k_spin_unlock(&status_lock, key);

	if (valid) {
		notify_guarded(status_attr, copy, (uint16_t)sizeof(copy));
	}
}

static void control_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	control_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("command responses %s", control_subscribed ? "on" : "off");
}

static void log_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	log_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("log stream %s", log_subscribed ? "on" : "off");
}

static void telem_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	telem_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("telemetry stream %s", telem_subscribed ? "on" : "off");
}

/* --- command queue --------------------------------------------------------- */

static struct k_thread command_thread;
K_THREAD_STACK_DEFINE(command_stack, LGPS_BLE_CMD_THREAD_STACK);
static struct k_thread log_drain_thread;
K_THREAD_STACK_DEFINE(log_drain_stack, LGPS_BLE_LOG_THREAD_STACK);

#define LGPS_BLE_CMD_QUEUE_DEPTH 2

static uint8_t pending_frames[LGPS_BLE_CMD_QUEUE_DEPTH][LGPS_BLE_CMD_MAX_FRAME];
static uint16_t pending_lengths[LGPS_BLE_CMD_QUEUE_DEPTH];
static uint32_t queue_head;
static uint32_t queue_tail;
static struct k_spinlock queue_lock;
static struct k_sem queue_sem;

static ssize_t control_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			     uint16_t len, uint16_t offset, uint8_t flags)
{
	k_spinlock_key_t key;
	uint32_t slot;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if ((buf == NULL) || (len == 0U) || (len > (uint16_t)LGPS_BLE_CMD_MAX_FRAME)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	key = k_spin_lock(&queue_lock);
	if ((queue_head - queue_tail) >= LGPS_BLE_CMD_QUEUE_DEPTH) {
		k_spin_unlock(&queue_lock, key);
		LOG_WRN("command queue full, frame dropped");
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	slot = queue_head % LGPS_BLE_CMD_QUEUE_DEPTH;
	memcpy(pending_frames[slot], buf, len);
	pending_lengths[slot] = len;
	queue_head++;
	k_spin_unlock(&queue_lock, key);

	k_sem_give(&queue_sem);

	return (ssize_t)len;
}

/* --- command dispatch ------------------------------------------------------ */

static void status_from_fix(struct lgps_ble_status *status,
			    const struct lgps_telemetry_snapshot *snap)
{
	struct lgps_ring_capacity capacity = { 0 };

	memset(status, 0, sizeof(*status));

	status->flags = 0U;
	if (snap->fix.valid) {
		status->flags |= LGPS_BLE_STAT_FIX_VALID;
	}

	if (snap->charging) {
		status->flags |= LGPS_BLE_STAT_CHARGING;
	}

	if (snap->zupt_locked) {
		status->flags |= LGPS_BLE_STAT_ZUPT;
	}

	if (snap->imu_valid) {
		status->flags |= LGPS_BLE_STAT_IMU_VALID;
	}

	/* The sensor hub clocks both external devices out in one transaction, but
	 * each device still reports on its own: a barometer that never answered its
	 * identification read must not be advertised as valid (bench 2026-11-19).
	 */
	if (snap->mag_valid) {
		status->flags |= LGPS_BLE_STAT_MAG_VALID;
	}

	if (snap->baro_valid) {
		status->flags |= LGPS_BLE_STAT_BARO_VALID;
	}

	if (snap->fix.utc_valid) {
		status->flags |= LGPS_BLE_STAT_UTC_VALID;
	}

	lgps_ring_get_capacity(&capacity);
	if (capacity.flash_bytes != 0U) {
		status->flags |= LGPS_BLE_STAT_RING_READY;
	}

	status->gnss_state = (uint8_t)snap->gnss_state;
	status->fix_type = snap->fix.fix_type;
	status->satellites = snap->fix.satellites;
	status->key_levels = snap->key_levels;
	status->soc_pct = LGPS_BLE_SOC_UNKNOWN;
	status->pdop_1e2 = snap->fix.pdop_1e2;
	status->speed_cms = snap->fix.valid ? snap->fix.speed_cms : 0U;
	status->heading_ddeg = snap->fix.valid ? snap->fix.heading_ddeg : 0U;
	status->lat_e7 = snap->fix.valid ? snap->fix.lat_e7 : 0;
	status->lon_e7 = snap->fix.valid ? snap->fix.lon_e7 : 0;
	status->hmsl_mm = snap->fix.valid ? snap->fix.hmsl_mm : 0;
	status->bat_mv = snap->bat_valid ? snap->bat_mv : -1;
	status->utc_epoch_s = snap->fix.utc_epoch_s;
	status->uptime_ms = lgps_diag_uptime_ms();
	status->gnss_age_ms = snap->gnss_age_ms;
	status->steps = snap->steps;
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		status->acc_lsb[axis] = snap->acc_lsb[axis];
		status->gyro_lsb[axis] = snap->gyro_lsb[axis];
		status->mag_lsb[axis] = snap->mag_lsb[axis];
	}
	status->baro_press_raw = snap->baro_press_raw;
	status->baro_temp_raw = snap->baro_temp_raw;
	status->ring_sector_seq = snap->ring_sector_seq;
	status->ring_records = snap->ring_records;

	/* Appended motion/temperature block (offsets 82..99). */
	status->sensor_flags = 0U;
	if (snap->gravity_valid) {
		status->sensor_flags |= LGPS_BLE_SENSOR_GRAV_VALID;
	}

	if (snap->imu_temp_valid) {
		status->sensor_flags |= LGPS_BLE_SENSOR_IMU_TEMP_VALID;
	}

	if (snap->mag_temp_valid) {
		status->sensor_flags |= LGPS_BLE_SENSOR_MAG_TEMP_VALID;
	}

	if (snap->baro_valid) {
		status->sensor_flags |= LGPS_BLE_SENSOR_BARO_TEMP_VALID;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		status->gravity_mg[axis] = snap->gravity_valid ? snap->gravity_mg[axis] : 0;
		status->linear_mg[axis] = snap->gravity_valid ? snap->linear_mg[axis] : 0;
	}

	status->imu_temp_cc = snap->imu_temp_valid ? snap->imu_temp_cc : 0;
	status->mag_temp_cc = snap->mag_temp_valid ? snap->mag_temp_cc : 0;
}

static uint8_t reply_not_supported(uint8_t opcode)
{
	switch (opcode) {
	case LGPS_CMD_SET_MODE:
	case LGPS_CMD_SET_TIME:
	case LGPS_CMD_SET_CONFIG:
	case LGPS_CMD_GET_CONFIG:
	case LGPS_CMD_SYNC_BEGIN:
	case LGPS_CMD_SYNC_READ:
	case LGPS_CMD_SYNC_END:
	case LGPS_CMD_GET_DAY_INDEX:
	case LGPS_CMD_GET_ARCHIVE:
	case LGPS_CMD_GNSS_SNAPSHOT:
	case LGPS_CMD_IMU_SNAPSHOT:
	case LGPS_CMD_BUTTON_EVENT:
	case LGPS_CMD_SELFTEST:
	case LGPS_CMD_MAG_CAL:
	case LGPS_CMD_BARO_BASELINE:
	case LGPS_CMD_ERASE_DAY:
	case LGPS_CMD_FACTORY_RESET:
	case LGPS_CMD_FLASH_READ:
	case LGPS_CMD_FLASH_ERASE:
	case LGPS_CMD_GET_FAULT_LOG:
	case LGPS_CMD_POWER_TEST_MARK:
		return LGPS_BLE_NOT_SUPPORTED;
	default:
		return LGPS_BLE_UNKNOWN_OPCODE;
	}
}

static uint8_t cmd_get_caps(uint8_t *payload, size_t capacity, size_t *out_len)
{
	struct lgps_ble_caps caps = {
		.caps = LGPS_BLE_CAP_STATUS_NOTIFY | LGPS_BLE_CAP_LOG_NOTIFY | LGPS_BLE_CAP_POI |
			LGPS_BLE_CAP_FLASH_INFO | LGPS_BLE_CAP_REBOOT |
			LGPS_BLE_CAP_DEBUG_UNLOCK,
		.telemetry_mask = 0U,
		.att_mtu = (uint16_t)LGPS_PROTO_MTU,
	};
	int len;

	len = lgps_ble_caps_encode(&caps, &payload[1], capacity - 1U);
	if (len < 0) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	*out_len = 1U + (size_t)len;

	return LGPS_BLE_OK;
}

static uint8_t cmd_get_status(uint8_t *payload, size_t capacity, size_t *out_len)
{
	k_spinlock_key_t key;
	bool valid;

	if (capacity < 1U + LGPS_BLE_STATUS_SIZE) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	key = k_spin_lock(&status_lock);
	memcpy(&payload[1], status_frame, LGPS_BLE_STATUS_SIZE);
	valid = status_frame_valid;
	k_spin_unlock(&status_lock, key);

	if (!valid) {
		return LGPS_BLE_BUSY;
	}

	*out_len = 1U + LGPS_BLE_STATUS_SIZE;

	return LGPS_BLE_OK;
}

static uint8_t cmd_flash_info(uint8_t *payload, size_t capacity, size_t *out_len)
{
	struct lgps_ring_capacity capacity_mem = { 0 };
	struct lgps_ring_stats stats = { 0 };
	struct lgps_ble_flash_info info = { 0 };
	int len;

	if (capacity < 1U + LGPS_BLE_FLASH_INFO_SIZE) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	lgps_ring_get_capacity(&capacity_mem);
	lgps_ring_get_stats(&stats);

	if (capacity_mem.flash_bytes == 0U) {
		/* The NOR did not answer at boot; reporting zeros as a capacity
		 * would let the app believe the ring is simply empty.
		 */
		return LGPS_BLE_INTERNAL_ERROR;
	}

	info.flash_bytes = capacity_mem.flash_bytes;
	info.ota_slot1_bytes = capacity_mem.ota_slot1_bytes;
	info.traj_bytes = capacity_mem.traj_bytes;
	info.sector_seq = stats.sector_seq;
	info.records_written = stats.records_written;
	info.records_in_sector = stats.records_in_sector;
	info.erase_count_max = stats.erase_count_max;
	/* The ring manages exactly LGPS_RING_SECTOR_COUNT sectors at the start of the
	 * trajectory area (flash_ring.h), so this is the sector map size the app must
	 * draw - not traj_bytes / sector_size, which would be 16128 for a 64 MB part
	 * and does not fit the byte anyway.
	 */
	info.sector_count = (uint8_t)LGPS_RING_SECTOR_COUNT;

	len = lgps_ble_flash_info_encode(&info, &payload[1], capacity - 1U);
	if (len < 0) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	*out_len = 1U + (size_t)len;

	return LGPS_BLE_OK;
}

static uint8_t cmd_poi_mark(uint8_t *payload, size_t capacity, size_t *out_len)
{
	struct lgps_gnss_fix fix;
	struct lgps_ring_stats stats = { 0 };
	struct lgps_traj_sample sample = { 0 };
	uint8_t record[LGPS_RECORD_TRAJECTORY_SIZE];
	int err;

	if (capacity < 2U) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	lgps_gnss_get_fix(&fix);
	if (!fix.valid) {
		/* A POI without a position is not a point of interest; saying OK
		 * here would make the app drop a pin at 0,0.
		 */
		return LGPS_BLE_INVALID_ARGUMENT;
	}

	lgps_ring_get_stats(&stats);

	sample.seq_id = stats.records_written;
	sample.timestamp_s = fix.utc_epoch_s;
	sample.subsecond_ms = 0U;
	sample.lat_e7 = fix.lat_e7;
	sample.lon_e7 = fix.lon_e7;
	/* The record field is int16 metres: a ballistic GNSS altitude jump must not
	 * wrap the sign and place the POI below sea level.
	 */
	{
		int32_t altitude_m = fix.hmsl_mm / 1000;

		if (altitude_m > 32767) {
			altitude_m = 32767;
		} else if (altitude_m < -32768) {
			altitude_m = -32768;
		}

		sample.alt_m = (int16_t)altitude_m;
	}
	sample.speed_cms = fix.speed_cms;
	sample.heading_ddeg = fix.heading_ddeg;
	sample.step_count = (uint16_t)atomic_get(&published_steps);
	sample.hdop_d1 = (uint8_t)((fix.pdop_1e2 + 5U) / 10U);
	sample.satellites = fix.satellites;
	sample.status_flags = (uint8_t)(LGPS_FLAG_FIX_VALID | LGPS_FLAG_POI_EVENT);
	sample.battery_pct = 0U;
	sample.floor_level = 0;

	err = lgps_traj_encode(&sample, record);
	if (err != 0) {
		LOG_ERR("POI record encode failed (%d)", err);
		return LGPS_BLE_INVALID_ARGUMENT;
	}

	if (!lgps_traj_record_valid(record)) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	err = lgps_ring_append(record);
	if (err != 0) {
		LOG_ERR("POI record append failed (%d)", err);
		return LGPS_BLE_INTERNAL_ERROR;
	}

	payload[1] = 1U;
	*out_len = 2U;

	return LGPS_BLE_OK;
}

static uint8_t cmd_log_subscribe(const struct lgps_ble_cmd *cmd)
{
	if (cmd->payload_len != 1U) {
		return LGPS_BLE_INVALID_ARGUMENT;
	}

	if (cmd->payload[0] > 4U) {
		return LGPS_BLE_INVALID_ARGUMENT;
	}

	if (lgps_ble_log_set_level(cmd->payload[0]) != 0) {
		return LGPS_BLE_INTERNAL_ERROR;
	}

	return LGPS_BLE_OK;
}

static uint8_t cmd_debug_unlock(const struct lgps_ble_cmd *cmd)
{
	static const uint8_t key[4] = { 'L', 'G', 'P', 'S' };

	if (cmd->payload_len != sizeof(key)) {
		return LGPS_BLE_INVALID_ARGUMENT;
	}

	if (memcmp(cmd->payload, key, sizeof(key)) != 0) {
		return LGPS_BLE_UNAUTHORIZED;
	}

	debug_unlocked = true;
	LOG_WRN("debug commands unlocked over BLE");

	return LGPS_BLE_OK;
}

static uint8_t cmd_reboot(void)
{
	if (!debug_unlocked) {
		return LGPS_BLE_UNAUTHORIZED;
	}

	/* The response has to leave the radio before the reset. The command thread
	 * cannot sleep here (the notification below would never be sent), so the
	 * reset is scheduled after the response is on its way.
	 */
	reboot_pending = true;

	return LGPS_BLE_OK;
}

static uint8_t dispatch(const struct lgps_ble_cmd *cmd, uint8_t *payload, size_t capacity,
			size_t *out_len)
{
	*out_len = 1U;

	switch (cmd->opcode) {
	case LGPS_CMD_GET_CAPS:
		return cmd_get_caps(payload, capacity, out_len);
	case LGPS_CMD_GET_STATUS:
		return cmd_get_status(payload, capacity, out_len);
	case LGPS_CMD_POI_MARK:
		return cmd_poi_mark(payload, capacity, out_len);
	case LGPS_CMD_FLASH_INFO:
		return cmd_flash_info(payload, capacity, out_len);
	case LGPS_CMD_LOG_SUBSCRIBE:
		return cmd_log_subscribe(cmd);
	case LGPS_CMD_DEBUG_UNLOCK:
		return cmd_debug_unlock(cmd);
	case LGPS_CMD_REBOOT:
		if (!debug_unlocked) {
			return LGPS_BLE_UNAUTHORIZED;
		}
		return cmd_reboot();
	default:
		return reply_not_supported(cmd->opcode);
	}
}

static void command_handler(void *unused1, void *unused2, void *unused3)
{
	uint8_t frame[LGPS_BLE_CMD_MAX_FRAME];
	uint8_t payload[1U + LGPS_BLE_STATUS_SIZE];
	uint8_t response[LGPS_FRAME_COMMAND_HEADER_SIZE + sizeof(payload) +
			 LGPS_FRAME_COMMAND_TRAILER_SIZE];

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	for (;;) {
		struct lgps_ble_cmd cmd;
		size_t out_len = 1U;
		uint16_t len;
		uint32_t slot;
		uint8_t status;
		int frame_len;
		k_spinlock_key_t key;

		k_sem_take(&queue_sem, K_FOREVER);

		key = k_spin_lock(&queue_lock);
		slot = queue_tail % LGPS_BLE_CMD_QUEUE_DEPTH;
		len = pending_lengths[slot];
		memcpy(frame, pending_frames[slot], len);
		queue_tail++;
		k_spin_unlock(&queue_lock, key);

		if (lgps_ble_cmd_parse(frame, len, &cmd) != 0) {
			LOG_WRN("malformed command frame (%u bytes) dropped", (unsigned int)len);
			continue;
		}

		status = dispatch(&cmd, payload, sizeof(payload), &out_len);
		payload[0] = status;

		frame_len = lgps_ble_cmd_build(cmd.opcode, LGPS_BLE_CMD_FLAG_RESPONSE, cmd.seq,
					       payload, (uint16_t)out_len, response,
					       sizeof(response));
		if (frame_len < 0) {
			LOG_ERR("response frame build failed (%d)", frame_len);
			continue;
		}

		LOG_DBG("cmd 0x%02x seq %u -> status 0x%02x", cmd.opcode, (unsigned int)cmd.seq,
			status);

		if (control_subscribed) {
			notify_guarded(control_attr, response, (uint16_t)frame_len);
		}

		if (reboot_pending) {
			reboot_pending = false;
			(void)k_work_schedule(&reboot_work, K_MSEC(LGPS_BLE_REBOOT_DELAY_MS));
		}
	}
}

/* --- log forwarding -------------------------------------------------------- */

static void log_handler(void *unused1, void *unused2, void *unused3)
{
	uint8_t packet[LGPS_BLE_LOG_MAX_PACKET];

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	for (;;) {
		size_t len = lgps_ble_log_pop(packet, sizeof(packet));

		if (len == 0U) {
			k_msleep(LGPS_BLE_LOG_DRAIN_MS);
			continue;
		}

		if (log_subscribed) {
			notify_guarded(log_attr, packet, (uint16_t)len);
		}
	}
}

/* --- connection ------------------------------------------------------------ */

/*
 * Human-readable disconnect reason. 0x08 is the one that matters on this bench:
 * it means the controller stopped hearing the phone for the whole supervision
 * timeout, which is a radio/link property and not a firmware fault (the device
 * keeps running and keeps logging).
 */
static const char *disconnect_reason_name(uint8_t reason)
{
	switch (reason) {
	case 0x05U:
		return "authentication failure";
	case 0x06U:
		return "PIN or key missing";
	case 0x08U:
		return "connection timeout (supervision)";
	case 0x13U:
		return "remote user terminated";
	case 0x14U:
		return "remote terminated, resources";
	case 0x15U:
		return "remote terminated, power off";
	case 0x16U:
		return "local host terminated";
	case 0x22U:
		return "LL response timeout";
	case 0x28U:
		return "instant passed";
	case 0x3BU:
		return "unacceptable connection parameters";
	case 0x3DU:
		return "MIC failure";
	case 0x3EU:
		return "connection failed to be established";
	default:
		return "other HCI error";
	}
}

/*
 * The negotiated parameters are what a supervision timeout is measured against,
 * so they belong in the log: without them a 0x08 is indistinguishable from
 * "the phone moved away" versus "the timeout is 400 ms".
 */
static void log_conn_params(struct bt_conn *conn)
{
	struct bt_conn_info info;
	int err;

	err = bt_conn_get_info(conn, &info);
	if (err != 0) {
		LOG_WRN("connection info unavailable (%d)", err);

		return;
	}

	LOG_INF("link: interval %u us, latency %u, supervision %u ms",
		(unsigned int)info.le.interval_us, (unsigned int)info.le.latency,
		(unsigned int)info.le.timeout * 10U);
}

/*
 * The parameters logged at connect time are the ones the central picked. The
 * peripheral's own request (BT_GAP_AUTO_UPDATE_CONN_PARAMS) is only sent five
 * seconds later, and the central may answer with something else entirely - this
 * callback is what proves whether the 8 s supervision timeout actually took
 * effect.
 */
static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	ARG_UNUSED(conn);

	LOG_INF("link updated: interval %u us, latency %u, supervision %u ms",
		(unsigned int)interval * 1250U, (unsigned int)latency, (unsigned int)timeout * 10U);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		LOG_ERR("connection failed (0x%02x)", err);
		schedule_advertising(LGPS_BLE_ADV_RESTART_MS);

		return;
	}

	LOG_INF("central connected");
	link_up = true;
	log_conn_params(conn);

#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
	{
		int ret = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);

		if (ret != 0) {
			LOG_WRN("data length update failed (%d)", ret);
		}
	}
#endif

	/* The 2M PHY request is left to CONFIG_BT_AUTO_PHY_PERIPHERAL_2M. Asking for
	 * it here as well made both requests land in the same few milliseconds and
	 * the controller answered HCI 0x3A (Controller Busy), see the bench log.
	 */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("central disconnected (0x%02x: %s)", reason, disconnect_reason_name(reason));

	link_up = false;
	status_subscribed = false;
	control_subscribed = false;
	log_subscribed = false;
	telem_subscribed = false;

	schedule_advertising(LGPS_BLE_ADV_RESTART_MS);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_updated = le_param_updated,
};

/* --- public API ------------------------------------------------------------ */

bool lgps_ble_status_subscribed(void)
{
	return status_subscribed;
}

void lgps_ble_publish(const struct lgps_telemetry_snapshot *snap)
{
	struct lgps_ble_status status;
	uint8_t frame[LGPS_BLE_STATUS_SIZE];
	static atomic_val_t reported_failures;
	atomic_val_t failures;
	k_spinlock_key_t key;
	int len;

	if (snap == NULL) {
		return;
	}

	failures = atomic_get(&notify_failures);
	if (failures != reported_failures) {
		reported_failures = failures;
		LOG_WRN("%d notification(s) dropped so far (link busy or gone)", (int)failures);
	}

	status_from_fix(&status, snap);
	len = lgps_ble_status_encode(&status, frame, sizeof(frame));
	if (len != (int)LGPS_BLE_STATUS_SIZE) {
		return;
	}

	key = k_spin_lock(&status_lock);
	memcpy(status_frame, frame, sizeof(frame));
	status_frame_valid = true;
	k_spin_unlock(&status_lock, key);

	atomic_set(&published_steps, (atomic_val_t)status.steps);

	if (status_subscribed) {
		notify_guarded(status_attr, frame, (uint16_t)len);
	}
}

void lgps_ble_publish_imu(const int16_t acc_lsb[3], const int16_t gyro_lsb[3],
			  const int16_t quat_q15[4])
{
	struct lgps_ble_telem_imu imu;
	uint8_t packet[LGPS_BLE_TELEM_IMU_SIZE];
	int len;

	if (!telem_subscribed) {
		return;
	}

	if ((acc_lsb == NULL) || (gyro_lsb == NULL) || (quat_q15 == NULL)) {
		return;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		imu.acc_lsb[axis] = acc_lsb[axis];
		imu.gyro_lsb[axis] = gyro_lsb[axis];
	}

	for (uint8_t index = 0U; index < 4U; index++) {
		imu.quat_q15[index] = quat_q15[index];
	}

	len = lgps_ble_telem_imu_encode(telem_seq_imu, &imu, packet, sizeof(packet));
	telem_seq_imu++;

	if (len == (int)LGPS_BLE_TELEM_IMU_SIZE) {
		notify_guarded(telem_attr, packet, (uint16_t)len);
	}
}

void lgps_ble_publish_pdr(int32_t x_mm, int32_t y_mm, int16_t heading_ddeg, uint16_t steps)
{
	struct lgps_ble_telem_pdr pdr;
	uint8_t packet[LGPS_BLE_TELEM_PDR_SIZE];
	int len;

	if (!telem_subscribed) {
		return;
	}

	pdr.x_mm = x_mm;
	pdr.y_mm = y_mm;
	pdr.heading_ddeg = heading_ddeg;
	pdr.steps = steps;

	len = lgps_ble_telem_pdr_encode(telem_seq_pdr, &pdr, packet, sizeof(packet));
	telem_seq_pdr++;

	if (len == (int)LGPS_BLE_TELEM_PDR_SIZE) {
		notify_guarded(telem_attr, packet, (uint16_t)len);
	}
}

static void build_device_name(void)
{
	uint8_t id[8] = { 0 };
	uint16_t short_id = 0U;
	ssize_t len;

	len = hwinfo_get_device_id(id, sizeof(id));
	if (len >= 2) {
		short_id = (uint16_t)(((uint16_t)id[len - 2] << 8) | id[len - 1]);
	}

	(void)snprintk(device_name, sizeof(device_name), "LGPS-%04X", short_id);
}

int lgps_ble_init(void)
{
	int err;

	build_device_name();

	status_attr = NULL;
	control_attr = NULL;
	log_attr = NULL;
	debug_unlocked = false;
	status_frame_valid = false;
	atomic_clear(&published_steps);

	/* One token per queued frame, otherwise a second Control write would find
	 * the only token already consumed and wait for a wakeup that never comes.
	 */
	k_sem_init(&queue_sem, 0, LGPS_BLE_CMD_QUEUE_DEPTH);

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth enable failed (%d)", err);
		return err;
	}

	k_work_init_delayable(&reboot_work, reboot_work_handler);
	k_work_init_delayable(&adv_restart_work, adv_restart_handler);

	err = bt_set_name(device_name);
	if (err != 0) {
		LOG_WRN("device name not set (%d)", err);
	}

	status_attr = bt_gatt_find_by_uuid(lgps_svc.attrs, (uint16_t)lgps_svc.attr_count,
					   &status_uuid.uuid);
	control_attr = bt_gatt_find_by_uuid(lgps_svc.attrs, (uint16_t)lgps_svc.attr_count,
					    &control_uuid.uuid);
	log_attr = bt_gatt_find_by_uuid(lgps_svc.attrs, (uint16_t)lgps_svc.attr_count,
					&log_uuid.uuid);
	telem_attr = bt_gatt_find_by_uuid(lgps_svc.attrs, (uint16_t)lgps_svc.attr_count,
					  &telem_uuid.uuid);
	if ((status_attr == NULL) || (control_attr == NULL) || (log_attr == NULL) ||
	    (telem_attr == NULL)) {
		LOG_ERR("GATT attribute lookup failed, notifications disabled");
		return -ENOENT;
	}

	lgps_ble_log_backend_init();

	k_thread_create(&command_thread, command_stack, K_THREAD_STACK_SIZEOF(command_stack),
			command_handler, NULL, NULL, NULL, LGPS_BLE_THREAD_PRIO, 0, K_NO_WAIT);

	k_thread_create(&log_drain_thread, log_drain_stack, K_THREAD_STACK_SIZEOF(log_drain_stack),
			log_handler, NULL, NULL, NULL, LGPS_BLE_THREAD_PRIO, 0, K_NO_WAIT);

	err = start_advertising();
	if (err != 0) {
		LOG_ERR("advertising start failed (%d)", err);

		return err;
	}

	LOG_INF("advertising as %s", device_name);

	/* README section 9.1: one blue flash means "advertising, ready to pair".
	 * It is the only feedback the device can give without a screen.
	 */
	(void)lgps_led_play(LGPS_LED_PATTERN_BLE_READY);

	return 0;
}
