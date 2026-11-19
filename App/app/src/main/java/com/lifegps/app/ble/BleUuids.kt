package com.lifegps.app.ble

import java.util.UUID

/**
 * GATT identifiers of the Life_GPS v1 wire contract (`Doc/Software/BLE_Wire_v1.md` §2).
 *
 * These are the same 128-bit values the firmware exposes as little-endian byte
 * macros; they are kept in one place so the transport and the repository cannot
 * drift apart.
 */
object BleUuids {
    /** Primary Life_GPS service. */
    val SERVICE: UUID = UUID.fromString("4C475053-0001-4C47-5053-000000000001")

    /** Status snapshot (100-byte extended / 82-byte base), notified at 10 Hz while subscribed (1 Hz idle) and readable. */
    val STATUS: UUID = UUID.fromString("4C475053-0002-4C47-5053-000000000001")

    /** Command frames are written here; responses come back as notifications. */
    val CONTROL: UUID = UUID.fromString("4C475053-0003-4C47-5053-000000000001")

    /** Firmware log forwarding (UTF-8 after a level + source prefix). */
    val LOG: UUID = UUID.fromString("4C475053-0006-4C47-5053-000000000001")

    /** Binary telemetry stream: IMU (with the attitude quaternion) and PDR packets. */
    val TELEMETRY: UUID = UUID.fromString("4C475053-0005-4C47-5053-000000000001")

    /** Client Characteristic Configuration Descriptor shared by all notified characteristics. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}
