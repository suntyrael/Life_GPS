// SPDX-License-Identifier: MIT
// Generated from protocol/lifegps_protocol.yaml by tools/protocol-gen/gen_kt.py - do not edit.
package com.lifegps.core.protocol

import java.util.UUID

/**
 * Single-source-of-truth BLE protocol constants generated from
 * `protocol/lifegps_protocol.yaml`. Multi-byte fields are little-endian.
 */
object GeneratedProtocol {

    object Meta {
        const val NAME: String = "LIFE GPS BLE Protocol"
        const val VERSION: String = "1.0.0"
        const val VERSION_MAJOR: Int = 1
        const val VERSION_MINOR: Int = 0
        const val VERSION_PATCH: Int = 0
        const val MTU: Int = 247
        const val MAX_COMMAND_PAYLOAD: Int = 235
        const val MAX_DATA_PAYLOAD: Int = 228
    }

    object Crc {
        const val FRAME_ALGORITHM: String = "crc16-ccitt-false"
        const val FRAME_POLY: Long = 0x1021L
        const val FRAME_INIT: Long = 0xFFFFL
        const val FRAME_XOROUT: Long = 0x0L
        const val FRAME_REFIN: Boolean = false
        const val FRAME_REFOUT: Boolean = false
        const val PACKAGE_ALGORITHM: String = "crc32-iso-hdlc"
        const val PACKAGE_POLY: Long = 0x4C11DB7L
        const val PACKAGE_INIT: Long = 0xFFFFFFFFL
        const val PACKAGE_XOROUT: Long = 0xFFFFFFFFL
        const val PACKAGE_REFIN: Boolean = true
        const val PACKAGE_REFOUT: Boolean = true
        const val RECORD_ALGORITHM: String = "crc8"
        const val RECORD_POLY: Long = 0x7L
        const val RECORD_INIT: Long = 0x0L
        const val RECORD_XOROUT: Long = 0x0L
        const val RECORD_REFIN: Boolean = false
        const val RECORD_REFOUT: Boolean = false
    }

    object Uuids {
        val CONFIG: UUID = UUID.fromString("4C475053-0007-4C47-5053-000000000001")
        val CONTROL: UUID = UUID.fromString("4C475053-0003-4C47-5053-000000000001")
        val DATA: UUID = UUID.fromString("4C475053-0004-4C47-5053-000000000001")
        val LOG: UUID = UUID.fromString("4C475053-0006-4C47-5053-000000000001")
        val SERVICE: UUID = UUID.fromString("4C475053-0001-4C47-5053-000000000001")
        val SMP_CHARACTERISTIC: UUID = UUID.fromString("DA2E7828-FBCE-4E01-AE9E-261174997C48")
        val SMP_SERVICE: UUID = UUID.fromString("8D53DC1D-1DB7-4CD3-868B-8A527460AA84")
        val STATUS: UUID = UUID.fromString("4C475053-0002-4C47-5053-000000000001")
        val TELEMETRY: UUID = UUID.fromString("4C475053-0005-4C47-5053-000000000001")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    object CommandFrame {
        const val MAGIC: Int = 0xA5
        const val HEADER_SIZE: Int = 8
        const val TRAILER_SIZE: Int = 2
        const val OFF_MAGIC: Int = 0
        const val OFF_VER: Int = 1
        const val OFF_OPCODE: Int = 2
        const val OFF_FLAGS: Int = 3
        const val OFF_SEQ: Int = 4
        const val OFF_PAYLOAD_LEN: Int = 6
    }

    object DataFrame {
        const val MAGIC: Int = 0x5A
        const val HEADER_SIZE: Int = 14
        const val TRAILER_SIZE: Int = 2
        const val OFF_MAGIC: Int = 0
        const val OFF_VER: Int = 1
        const val OFF_STREAM: Int = 2
        const val OFF_FLAGS: Int = 3
        const val OFF_TRANSFER_ID: Int = 4
        const val OFF_CURSOR: Int = 8
        const val OFF_PAYLOAD_LEN: Int = 12
    }

    object TrajectoryRecord {
        const val SIZE: Int = 32
        const val OFF_SEQ_ID: Int = 0
        const val OFF_TIMESTAMP: Int = 4
        const val OFF_SUBSECOND_MS: Int = 8
        const val OFF_LATITUDE: Int = 10
        const val OFF_LONGITUDE: Int = 14
        const val OFF_ALTITUDE: Int = 18
        const val OFF_SPEED_CMS: Int = 20
        const val OFF_HEADING: Int = 22
        const val OFF_STEP_COUNT: Int = 24
        const val OFF_HDOP: Int = 26
        const val OFF_SATELLITES: Int = 27
        const val OFF_STATUS_FLAGS: Int = 28
        const val OFF_BATTERY_LEVEL: Int = 29
        const val OFF_FLOOR_LEVEL: Int = 30
        const val OFF_CHECKSUM: Int = 31
    }

    object DayPackage {
        const val MAGIC_ASCII: String = "LGD1"
        const val HEADER_VER: Int = 1
        const val HEADER_SIZE: Int = 48
        const val HEADER_CRC16_LEN: Int = 46
        const val FOOTER_SIZE: Int = 8
        const val CODEC_NAME: String = "byte_delta_heatshrink"
        const val CODEC_BYTE_DELTA_HEATSHRINK: Int = 1
        const val CODEC_RAW: Int = 0
        const val HS_WINDOW_SZ2: Int = 8
        const val HS_LOOKAHEAD_SZ2: Int = 4
        const val HEADER_OFF_MAGIC: Int = 0
        const val HEADER_OFF_HEADER_VER: Int = 4
        const val HEADER_OFF_CODEC: Int = 6
        const val HEADER_OFF_DAY_ID: Int = 8
        const val HEADER_OFF_RECORD_COUNT: Int = 12
        const val HEADER_OFF_RESERVED0: Int = 16
        const val HEADER_OFF_TRAJ_RAW_LEN: Int = 20
        const val HEADER_OFF_TRAJ_PACKED_LEN: Int = 24
        const val HEADER_OFF_RESERVED1: Int = 28
        const val HEADER_OFF_RESERVED2: Int = 32
        const val HEADER_OFF_PREV_DAY_ID: Int = 36
        const val HEADER_OFF_RESERVED3: Int = 40
        const val HEADER_OFF_FLAGS: Int = 44
        const val HEADER_OFF_HEADER_CRC16: Int = 46
        const val FOOTER_OFF_TRAJ_CRC32: Int = 0
        const val FOOTER_OFF_RESERVED: Int = 4
    }

    object StreamId {
        const val TRAJECTORY: Int = 0x00
        const val ARCHIVE_DAY: Int = 0x02
        const val FLASH_RAW: Int = 0x03
        const val LOG_REPLAY: Int = 0x04
    }

    object CommandOpcode {
        const val GET_CAPS: Int = 0x01
        const val GET_STATUS: Int = 0x02
        const val SET_MODE: Int = 0x10
        const val SET_TIME: Int = 0x11
        const val POI_MARK: Int = 0x12
        const val SET_CONFIG: Int = 0x13
        const val GET_CONFIG: Int = 0x14
        const val SYNC_BEGIN: Int = 0x20
        const val SYNC_READ: Int = 0x21
        const val SYNC_END: Int = 0x22
        const val GET_DAY_INDEX: Int = 0x23
        const val GET_ARCHIVE: Int = 0x24
        const val LOG_SUBSCRIBE: Int = 0x30
        const val GNSS_SNAPSHOT: Int = 0x31
        const val IMU_SNAPSHOT: Int = 0x32
        const val BUTTON_EVENT: Int = 0x33
        const val GNSS_CFG: Int = 0x35
        const val GNSS_DEBUG: Int = 0x36
        const val GNSS_RESTART: Int = 0x37
        const val SELFTEST: Int = 0x40
        const val MAG_CAL: Int = 0x41
        const val BARO_BASELINE: Int = 0x42
        const val ACCEL_CAL: Int = 0x43
        const val ERASE_DAY: Int = 0x50
        const val FACTORY_RESET: Int = 0x51
        const val REBOOT: Int = 0x52
        const val FLASH_INFO: Int = 0x60
        const val FLASH_READ: Int = 0x61
        const val FLASH_ERASE: Int = 0x62
        const val GET_FAULT_LOG: Int = 0x70
        const val POWER_TEST_MARK: Int = 0x71
        const val DEBUG_UNLOCK: Int = 0x7F
    }

    object TelemetryId {
        const val GNSS: Int = 0x01
        const val IMU: Int = 0x02
        const val MAG: Int = 0x03
        const val BARO: Int = 0x04
        const val PDR: Int = 0x05
        const val POWER: Int = 0x06
        const val SYSTEM: Int = 0x07
        const val NMEA: Int = 0x08
        const val SAT: Int = 0x09
    }

    object StatusCode {
        const val OK: Int = 0x00
        const val INVALID_ARGUMENT: Int = 0x01
        const val UNKNOWN_OPCODE: Int = 0x02
        const val NOT_SUPPORTED: Int = 0x03
        const val BUSY: Int = 0x04
        const val INTERNAL_ERROR: Int = 0x05
        const val UNAUTHORIZED: Int = 0x06
        const val OUT_OF_RANGE: Int = 0x07
        const val NOT_FOUND: Int = 0x08
        const val FLASH_ERROR: Int = 0x09
        const val LOW_BATTERY: Int = 0x0A
        const val CRC_MISMATCH: Int = 0x0B
        const val SESSION_EXPIRED: Int = 0x0C
    }

    object CapsBits {
        const val STATUS_NOTIFY: Long = 0x00000001L
        const val LOG_NOTIFY: Long = 0x00000002L
        const val POI: Long = 0x00000004L
        const val FLASH_INFO: Long = 0x00000008L
        const val REBOOT: Long = 0x00000010L
        const val DEBUG_UNLOCK: Long = 0x00000020L
        const val DATA_SYNC: Long = 0x00000040L
        const val MAG_CAL: Long = 0x00000080L
        const val ACCEL_CAL: Long = 0x00000100L
        const val BARO_BASELINE: Long = 0x00000200L
        const val GNSS_DEBUG: Long = 0x00000400L
        const val GNSS_CFG: Long = 0x00000800L
        const val CONFIG_TLV: Long = 0x00001000L
        const val GNSS_RESTART: Long = 0x00002000L
        const val OTA_SMP: Long = 0x00004000L
    }

    object ConfigTlv {
        const val RECORD_MODE: Int = 0x01
        const val RECORD_MODE_LEN: Int = 1
        const val GNSS_RATE_HZ: Int = 0x02
        const val GNSS_RATE_HZ_LEN: Int = 1
        const val GNSS_COLD_TIMEOUT_S: Int = 0x03
        const val GNSS_COLD_TIMEOUT_S_LEN: Int = 2
        const val ARCHIVE_HOUR_UTC: Int = 0x04
        const val ARCHIVE_HOUR_UTC_LEN: Int = 1
        const val LED_ENABLE: Int = 0x05
        const val LED_ENABLE_LEN: Int = 1
        const val AUTO_OFF_MIN: Int = 0x06
        const val AUTO_OFF_MIN_LEN: Int = 2
        const val PDR_STEP_K_MM: Int = 0x07
        const val PDR_STEP_K_MM_LEN: Int = 2
        const val MAG_ANOMALY_UT: Int = 0x08
        const val MAG_ANOMALY_UT_LEN: Int = 2
        const val TZ_OFFSET_MIN: Int = 0x09
        const val TZ_OFFSET_MIN_LEN: Int = 2
        const val BLE_ADV_INTERVAL_MS: Int = 0x0A
        const val BLE_ADV_INTERVAL_MS_LEN: Int = 2
        const val LOG_LEVEL_DEFAULT: Int = 0x0B
        const val LOG_LEVEL_DEFAULT_LEN: Int = 1
    }

    object AdvData {
        const val COMPANY_ID: Int = 0xFFFF
        const val VERSION: Int = 1
        const val SIZE: Int = 10
        const val OFF_ADV_VER: Int = 0
        const val OFF_WORK_MODE: Int = 1
        const val OFF_BATTERY_PCT: Int = 2
        const val OFF_STORAGE_PCT: Int = 3
        const val OFF_ADV_FLAGS: Int = 4
        const val OFF_LAST_SEQ_ID: Int = 6
    }

    object StatusSizes {
        const val BASE: Int = 82
        const val MOTION: Int = 100
        const val ACCURACY: Int = 116
        const val SYNC: Int = 128
        const val OFF_ARCHIVE_INDEX_VER: Int = 116
        const val OFF_LAST_SEQ_ID: Int = 120
        const val OFF_WORK_MODE: Int = 124
        const val OFF_ARCHIVE_DAYS_COUNT: Int = 125
        const val OFF_STORAGE_USED_PERMILLE: Int = 126
    }

}
