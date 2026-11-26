package com.lifegps.app.ble

import com.lifegps.app.ble.generated.GeneratedProtocol
import java.util.UUID

/**
 * GATT identifiers of the Life_GPS wire contract (`protocol/lifegps_protocol.yaml`).
 *
 * All UUID values are delegated to [GeneratedProtocol.Uuids] so Kotlin and the
 * firmware C header stay generated from the same YAML source of truth.
 */
object BleUuids {
    /** Primary Life_GPS service. */
    val SERVICE: UUID = GeneratedProtocol.Uuids.SERVICE

    /** Status snapshot (116-byte accuracy / 100-byte motion / 82-byte base). */
    val STATUS: UUID = GeneratedProtocol.Uuids.STATUS

    /** Command frames are written here; responses come back as notifications. */
    val CONTROL: UUID = GeneratedProtocol.Uuids.CONTROL

    /** Bulk data transfer frames (`0x5A`, trajectory staging & LGD1 archives). */
    val DATA: UUID = GeneratedProtocol.Uuids.DATA

    /** Binary telemetry stream: GNSS, IMU, BARO, PDR, NMEA and SAT packets. */
    val TELEMETRY: UUID = GeneratedProtocol.Uuids.TELEMETRY

    /** Firmware log forwarding (UTF-8 after a level + source prefix). */
    val LOG: UUID = GeneratedProtocol.Uuids.LOG

    /** Versioned TLV configuration characteristic. */
    val CONFIG: UUID = GeneratedProtocol.Uuids.CONFIG

    /** MCUboot / SMP OTA service and characteristic. */
    val SMP_SERVICE: UUID = GeneratedProtocol.Uuids.SMP_SERVICE
    val SMP_CHARACTERISTIC: UUID = GeneratedProtocol.Uuids.SMP_CHARACTERISTIC

    /** Client Characteristic Configuration Descriptor shared by all notified characteristics. */
    val CCCD: UUID = GeneratedProtocol.Uuids.CCCD
}
