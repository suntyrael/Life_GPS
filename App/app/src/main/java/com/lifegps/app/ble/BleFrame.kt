package com.lifegps.app.ble

import java.util.Locale

/**
 * Offset table of the Status snapshot (82-byte base, 100-byte extended).
 *
 * The values are transcribed one-to-one from `Doc/Software/BLE_Wire_v1.md` §4
 * (the contract, not this file, is the authority). Keeping them as named
 * constants makes the byte-for-byte comparison with the document mechanical.
 */
object StatusOffset {
    const val VER = 0
    const val FLAGS = 1
    const val GNSS_STATE = 2
    const val FIX_TYPE = 3
    const val SATELLITES = 4
    const val KEY_LEVELS = 5
    const val SOC_PCT = 6
    const val RESERVED0 = 7
    const val PDOP_1E2 = 8
    const val SPEED_CMS = 10
    const val HEADING_DDEG = 12
    const val RESERVED1 = 14
    const val LAT_E7 = 16
    const val LON_E7 = 20
    const val HMSL_MM = 24
    const val BAT_MV = 28
    const val UTC_EPOCH_S = 32
    const val UPTIME_MS = 36
    const val GNSS_AGE_MS = 40
    const val STEPS = 44
    const val ACC_LSB = 48
    const val GYRO_LSB = 54
    const val MAG_LSB = 60
    const val BARO_PRESS_RAW = 66
    const val BARO_TEMP_RAW = 70
    const val RING_SECTOR_SEQ = 74
    const val RING_RECORDS = 78
    const val GRAVITY_MG = 82
    const val LINEAR_MG = 88
    const val IMU_TEMP_CC = 94
    const val MAG_TEMP_CC = 96
    const val SENSOR_FLAGS = 98

    /** Old firmware bodies stop here; the fields above are always present. */
    const val BASE_SIZE = 82
    const val SIZE = 100
}

/** Offset table of the GET_CAPS response body (`BLE_Wire_v1.md` §6.1). */
object CapsOffset {
    const val VER = 0
    const val PROTO_MAJOR = 1
    const val PROTO_MINOR = 2
    const val PROTO_PATCH = 3
    const val FW_MAJOR = 4
    const val FW_MINOR = 5
    const val FW_PATCH = 6
    const val RESERVED = 7
    const val ATT_MTU = 8
    const val MAX_CMD_PAYLOAD = 10
    const val CAPS = 12
    const val TELEMETRY_MASK = 16
    const val STATUS_SIZE = 20
    const val RESERVED2 = 22
    const val SIZE = 24
}

/** Offset table of the FLASH_INFO response body (`BLE_Wire_v1.md` §6.2, 24 bytes). */
/**
 * GET_CAPS `caps` bits. A set bit means the opcode behind it really works on this
 * firmware; the App gates its calibration UI on these instead of guessing from the
 * protocol version alone.
 */
object CapsBits {
    const val STATUS_NOTIFY = 0x00000001L
    const val LOG_NOTIFY = 0x00000002L
    const val POI = 0x00000004L
    const val FLASH_INFO = 0x00000008L
    const val REBOOT = 0x00000010L
    const val DEBUG_UNLOCK = 0x00000020L
    const val DATA_SYNC = 0x00000040L
    const val MAG_CAL = 0x00000080L
    const val ACCEL_CAL = 0x00000100L
    const val BARO_BASELINE = 0x00000200L
}

object FlashInfoOffset {
    const val FLASH_BYTES = 0
    const val OTA_SLOT1_BYTES = 4
    const val TRAJ_BYTES = 8
    const val SECTOR_SEQ = 12
    const val RECORDS_WRITTEN = 16
    const val RECORDS_IN_SECTOR = 20
    const val ERASE_COUNT_MAX = 21
    const val SECTOR_COUNT = 22
    const val RESERVED = 23
    const val SIZE = 24
}

/** Status.flags bit mask (`BLE_Wire_v1.md` §4, offset 1). */
object StatusFlags {
    const val GNSS_FIX_VALID = 0x01
    const val CHARGING = 0x02
    const val ZUPT = 0x04
    const val IMU_VALID = 0x08
    const val MAG_VALID = 0x10
    const val BARO_VALID = 0x20
    const val RING_READY = 0x40
    const val UTC_VALID = 0x80
}

/** Status.sensor_flags bit mask (`BLE_Wire_v1.md` §4, offset 98). */
object SensorFlags {
    const val GRAVITY_VALID = 0x01
    const val IMU_TEMP_VALID = 0x02
    const val MAG_TEMP_VALID = 0x04
    const val BARO_TEMP_VALID = 0x08
}

/** Command opcodes of the protocol (`protocol/lifegps_protocol.yaml`, §6 of the wire doc). */
enum class Opcode(val code: Int, val label: String) {
    GET_CAPS(0x01, "GET_CAPS"),
    GET_STATUS(0x02, "GET_STATUS"),
    SET_MODE(0x10, "SET_MODE"),
    SET_TIME(0x11, "SET_TIME"),
    POI_MARK(0x12, "POI_MARK"),
    SET_CONFIG(0x13, "SET_CONFIG"),
    GET_CONFIG(0x14, "GET_CONFIG"),
    SYNC_BEGIN(0x20, "SYNC_BEGIN"),
    SYNC_READ(0x21, "SYNC_READ"),
    SYNC_END(0x22, "SYNC_END"),
    GET_DAY_INDEX(0x23, "GET_DAY_INDEX"),
    GET_ARCHIVE(0x24, "GET_ARCHIVE"),
    LOG_SUBSCRIBE(0x30, "LOG_SUBSCRIBE"),
    GNSS_SNAPSHOT(0x31, "GNSS_SNAPSHOT"),
    /* 0.2.0: GNSS debugging (see BLE_Wire_v1.md §6.6 / §6.7). */
    GNSS_CFG(0x35, "GNSS_CFG"),
    GNSS_DEBUG(0x36, "GNSS_DEBUG"),
    IMU_SNAPSHOT(0x32, "IMU_SNAPSHOT"),
    BUTTON_EVENT(0x33, "BUTTON_EVENT"),
    SELFTEST(0x40, "SELFTEST"),
    MAG_CAL(0x41, "MAG_CAL"),
    BARO_BASELINE(0x42, "BARO_BASELINE"),
    ACCEL_CAL(0x43, "ACCEL_CAL"),
    ERASE_DAY(0x50, "ERASE_DAY"),
    FACTORY_RESET(0x51, "FACTORY_RESET"),
    REBOOT(0x52, "REBOOT"),
    FLASH_INFO(0x60, "FLASH_INFO"),
    FLASH_READ(0x61, "FLASH_READ"),
    FLASH_ERASE(0x62, "FLASH_ERASE"),
    GET_FAULT_LOG(0x70, "GET_FAULT_LOG"),
    POWER_TEST_MARK(0x71, "POWER_TEST_MARK"),
    DEBUG_UNLOCK(0x7F, "DEBUG_UNLOCK"),
    ;

    companion object {
        /** Resolves the UI dropdown label; unknown names return null (shown as UNKNOWN). */
        fun fromName(name: String): Opcode? {
            val trimmed = name.trim()
            return entries.firstOrNull { it.label.equals(trimmed, ignoreCase = true) }
        }

        fun fromCode(code: Int): Opcode? = entries.firstOrNull { it.code == code }
    }
}

/** Response status codes (`BLE_Wire_v1.md` §3.1). */
enum class StatusCode(val code: Int, val label: String) {
    OK(0x00, "OK"),
    INVALID_ARGUMENT(0x01, "INVALID_ARGUMENT"),
    UNKNOWN_OPCODE(0x02, "UNKNOWN_OPCODE"),
    NOT_SUPPORTED(0x03, "NOT_SUPPORTED"),
    BUSY(0x04, "BUSY"),
    INTERNAL_ERROR(0x05, "INTERNAL_ERROR"),
    UNAUTHORIZED(0x06, "UNAUTHORIZED"),
    ;

    companion object {
        fun fromCode(code: Int): StatusCode? = entries.firstOrNull { it.code == code }

        fun labelOf(code: Int): String = fromCode(code)?.label ?: "UNKNOWN_STATUS"
    }
}

/** One parsed command frame (request or response). */
data class Frame(
    val opcodeCode: Int,
    val opcode: Opcode?,
    val isResponse: Boolean,
    val seq: Int,
    val payload: ByteArray,
)

/** Decoded Status snapshot; the last 18 bytes are absent on old firmware. */
data class StatusFrame(
    val ver: Int,
    val flags: Int,
    val gnssState: Int,
    val fixType: Int,
    val satellites: Int,
    val keyLevels: Int,
    val socPct: Int,
    val pdop1e2: Int,
    val speedCms: Int,
    val headingDdeg: Int,
    val latE7: Int,
    val lonE7: Int,
    val hmslMm: Int,
    val batMv: Int,
    val utcEpochS: Long,
    val uptimeMs: Long,
    val gnssAgeMs: Long,
    val steps: Long,
    val accLsb: List<Int>,
    val gyroLsb: List<Int>,
    val magLsb: List<Int>,
    val baroPressRaw: Long,
    val baroTempRaw: Long,
    val ringSectorSeq: Long,
    val ringRecords: Long,
    val gravityMg: List<Int>,
    val linearMg: List<Int>,
    val imuTempCc: Int,
    val magTempCc: Int,
    val sensorFlags: Int,
    val extendedFields: Boolean,
) {
    val gnssFixValid: Boolean get() = flags and StatusFlags.GNSS_FIX_VALID != 0
    val charging: Boolean get() = flags and StatusFlags.CHARGING != 0
    val zupt: Boolean get() = flags and StatusFlags.ZUPT != 0
    val imuValid: Boolean get() = flags and StatusFlags.IMU_VALID != 0
    val magValid: Boolean get() = flags and StatusFlags.MAG_VALID != 0
    val baroValid: Boolean get() = flags and StatusFlags.BARO_VALID != 0
    val ringReady: Boolean get() = flags and StatusFlags.RING_READY != 0
    val utcValid: Boolean get() = flags and StatusFlags.UTC_VALID != 0

    val gravityValid: Boolean get() = sensorFlags and SensorFlags.GRAVITY_VALID != 0
    val imuTempValid: Boolean get() = sensorFlags and SensorFlags.IMU_TEMP_VALID != 0
    val magTempValid: Boolean get() = sensorFlags and SensorFlags.MAG_TEMP_VALID != 0
    val baroTempValid: Boolean get() = sensorFlags and SensorFlags.BARO_TEMP_VALID != 0

    /** True when the firmware sent the appended motion/thermal fields. */
    val hasMotionFields: Boolean get() = extendedFields
    val hasTempFields: Boolean get() = extendedFields
}

/** Decoded GET_CAPS response body (status byte already stripped). */
data class CapsInfo(
    val ver: Int,
    val protoMajor: Int,
    val protoMinor: Int,
    val protoPatch: Int,
    val fwMajor: Int,
    val fwMinor: Int,
    val fwPatch: Int,
    val attMtu: Int,
    val maxCmdPayload: Int,
    val caps: Long,
    val telemetryMask: Long,
    val statusSize: Int,
) {
    val firmwareText: String get() = "$fwMajor.$fwMinor.$fwPatch"
    val protocolText: String get() = "$protoMajor.$protoMinor.$protoPatch"
}

/** Decoded FLASH_INFO response body (status byte already stripped). */
data class FlashInfo(
    val flashBytes: Long,
    val otaSlot1Bytes: Long,
    val trajBytes: Long,
    val sectorSeq: Long,
    val recordsWritten: Long,
    val recordsInSector: Int,
    val eraseCountMax: Int,
    val sectorCount: Int,
)

/** Offset table of a Telemetry packet (`BLE_Wire_v1.md` §5.1). */
object TelemetryOffset {
    const val ID = 0
    const val VER = 1
    const val SEQ = 2
    const val PAYLOAD = 4
}

/** Telemetry group ids, mirroring `protocol/lifegps_protocol.yaml`. */
object TelemetryId {
    const val GNSS = 0x01
    const val IMU = 0x02
    const val MAG = 0x03
    const val BARO = 0x04
    const val PDR = 0x05
    const val POWER = 0x06
    const val SYSTEM = 0x07
    const val NMEA = 0x08
    const val SAT = 0x09
}

/**
 * Satellite table packet (telemetry 0x09): `u8 count, u8 total, u16 reserved`,
 * then [SatTelemetry.SIZE_ENTRY] bytes per satellite.
 *
 * [state] packs `qualityInd` in bits 2..0 and the `svUsed` flag in bit 3, exactly
 * as UBX-NAV-SAT reports them.
 */
data class SatTelemetry(
    val rows: List<SatTelemetryRow>,
    val total: Int,
) {
    companion object {
        const val HEADER = 4
        const val SIZE_ENTRY = 5
        const val MAX = 32
    }
}

data class SatTelemetryRow(
    val gnssId: Int,
    val svId: Int,
    val cno: Int,
    val elevation: Int,
    val state: Int,
) {
    val quality: Int get() = state and 0x07
    val used: Boolean get() = state and 0x08 != 0

    /** Human wording for `qualityInd` (UBX-NAV-SAT §3.15.13.1), never "fix". */
    val qualityLabel: String
        get() = when (quality) {
            0 -> "无信号"
            1 -> "搜索中"
            2 -> "已捕获"
            3 -> "不可用"
            else -> "跟踪"
        }
}

/** One NMEA sentence (telemetry 0x08): length prefix plus ASCII text. */
data class NmeaTelemetry(val text: String)

/** One telemetry packet; [payload] excludes the 4-byte header. */
data class TelemetryPacket(
    val id: Int,
    val ver: Int,
    val seq: Int,
    val payload: ByteArray,
)

/**
 * IMU telemetry: body-frame raw counts plus the attitude quaternion in Q15.
 *
 * Q15 means 32767 = 1.0, so a component is `q15 / 32767f`. The quaternion maps the
 * body frame to the world frame (X east, Y north, Z up), which is the same
 * convention the wire document freezes for the firmware-side filter.
 */
data class ImuTelemetry(
    val accLsb: List<Int>,
    val gyroLsb: List<Int>,
    val quatQ15: List<Int>,
) {
    val qw: Float get() = quatQ15[0] / 32767f
    val qx: Float get() = quatQ15[1] / 32767f
    val qy: Float get() = quatQ15[2] / 32767f
    val qz: Float get() = quatQ15[3] / 32767f
}

/** PDR telemetry: dead-reckoned position in millimetres plus the step counter. */
data class PdrTelemetry(
    val xMm: Int,
    val yMm: Int,
    val headingDdeg: Int,
    val steps: Int,
)

/**
 * GNSS receive-path diagnostics (`BLE_Wire_v1.md` §5.1, telemetry id 0x01).
 *
 * Counters for the UART link to the receiver, not a solution: the fix still comes
 * from the Status frame. They exist because "no fix" has several different
 * causes and only these numbers tell them apart (silent module, NMEA-only stream,
 * corrupt framing, overflowing RX ring, wrong baud).
 */
data class GnssTelemetry(
    val state: Int,
    val fixType: Int,
    val satellites: Int,
    val fixValid: Boolean,
    val utcValid: Boolean,
    val pdop1e2: Int,
    val ageMs: Long,
    val rxBytes: Long,
    val ubxFrames: Long,
    val badChecksum: Long,
    val nmeaStarts: Long,
    val rxDropped: Long,
    val baud: Int,
)

/** Offsets of the `0x01` GNSS telemetry payload (`BLE_Wire_v1.md` §5.1). */
object GnssTelemetryOffset {
    const val STATE = 0
    const val FIX_TYPE = 1
    const val SATELLITES = 2
    const val FLAGS = 3
    const val PDOP_1E2 = 4
    const val AGE_MS = 6
    const val RX_BYTES = 10
    const val UBX_FRAMES = 14
    const val BAD_CHECKSUM = 18
    const val NMEA_STARTS = 22
    const val RX_DROPPED = 26
    const val BAUD = 30
    const val SIZE = 34

    const val FLAG_FIX_VALID = 0x01
    const val FLAG_UTC_VALID = 0x02
}

/**
 * BMP388 barometer telemetry (`BLE_Wire_v1.md` §5.1, telemetry id 0x04, 20 bytes).
 *
 * Every value is already compensated on the device: [pressPa] is a real pressure in
 * pascals and [relAltMm] is the height above the baseline the firmware holds, both 0
 * when the matching validity flag is clear. The App deliberately does not turn the
 * uncompensated `baro_press_raw` / `baro_temp_raw` of the Status frame into hPa —
 * without the TPG coefficients that would be a plausible-looking wrong number.
 *
 * The payload was extended from 16 to 20 bytes by appending `alt_msl_mm` at offset
 * 16; offsets 0..15 are unchanged. Firmware that predates the append sends 16 bytes
 * and never sets [mslValid], so the field stays 0 there.
 */
data class BaroTelemetry(
    val pressPa: Long,
    val relAltMm: Int,
    val tempCc: Int,
    val flags: Int,
    val baselinePaQ4: Long,
    val altMslMm: Int,
    val mslValid: Boolean,
) {
    val pressValid: Boolean get() = flags and BaroTelemetryOffset.FLAG_PRESS_VALID != 0
    val tempValid: Boolean get() = flags and BaroTelemetryOffset.FLAG_TEMP_VALID != 0
    val baselineValid: Boolean get() = flags and BaroTelemetryOffset.FLAG_BASELINE_VALID != 0
    val baselineAuto: Boolean get() = flags and BaroTelemetryOffset.FLAG_BASELINE_AUTO != 0

    val pressureHpa: Float get() = pressPa / 100f
    val temperatureC: Float get() = tempCc / 100f
    val relAltM: Float get() = relAltMm / 1000f

    /** Absolute altitude above mean sea level; only meaningful while [mslValid]. */
    val altMslM: Float get() = altMslMm / 1000f

    /** `baseline_pa_q4` is Pa × 16 (`BLE_Wire_v1.md` §6.5), i.e. 1/1600 hPa per count. */
    val baselineHpa: Float get() = baselinePaQ4 / 16f / 100f
}

/**
 * Offsets of the `0x04` BARO telemetry payload (`BLE_Wire_v1.md` §5.1, 20 bytes).
 *
 * [MSL] / [ALT_MSL_MM] are the appended field: firmware older than that append
 * sends only the first 16 bytes ([LEGACY_SIZE]) and the parser keeps the MSL field
 * at 0 with `mslValid = false`.
 */
object BaroTelemetryOffset {
    const val PRESS_PA = 0
    const val REL_ALT_MM = 4
    const val TEMP_CC = 8
    const val FLAGS = 10
    const val RESERVED = 11
    const val BASELINE_PA_Q4 = 12
    const val MSL = 16
    const val ALT_MSL_MM = 16
    const val SIZE = 20

    /** Length of the payload before `alt_msl_mm` was appended. */
    const val LEGACY_SIZE = 16

    const val FLAG_PRESS_VALID = 0x01
    const val FLAG_TEMP_VALID = 0x02
    const val FLAG_BASELINE_VALID = 0x04
    const val FLAG_BASELINE_AUTO = 0x08
    const val FLAG_MSL_VALID = 0x10
}

/** Decoded Log notification (`BLE_Wire_v1.md` §5). */
data class LogPacket(
    val level: Int,
    val source: String,
    val message: String,
)

/**
 * Pure Kotlin codec for the v1 wire format.
 *
 * No Android types are used so the CRC, frame layout and payload parsing can be
 * read (and later unit-tested) independently of the Bluetooth stack.
 */
object BleFrame {
    const val MAGIC = 0xA5
    const val VER = 0x01
    const val FLAG_RESPONSE = 0x01
    const val HEADER_SIZE = 8
    const val TRAILER_SIZE = 2
    const val MAX_PAYLOAD = 235

    /** CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor. */
    fun crc16CcittFalse(data: ByteArray, offset: Int = 0, length: Int = data.size): Int {
        var crc = 0xFFFF
        var index = offset
        val end = offset + length
        while (index < end) {
            crc = crc xor ((data[index].toInt() and 0xFF) shl 8)
            var bit = 0
            while (bit < 8) {
                crc = if (crc and 0x8000 != 0) {
                    ((crc shl 1) xor 0x1021) and 0xFFFF
                } else {
                    (crc shl 1) and 0xFFFF
                }
                bit++
            }
            index++
        }
        return crc and 0xFFFF
    }

    /** Builds a request frame; `seq` is echoed by the response. */
    fun encodeCommand(opcode: Int, seq: Int, payload: ByteArray): ByteArray {
        require(payload.size <= MAX_PAYLOAD) { "payload ${payload.size} > $MAX_PAYLOAD" }
        val frame = ByteArray(HEADER_SIZE + payload.size + TRAILER_SIZE)
        frame[0] = MAGIC.toByte()
        frame[1] = VER.toByte()
        frame[2] = opcode.toByte()
        frame[3] = 0
        putU16(frame, 4, seq)
        putU16(frame, 6, payload.size)
        payload.copyInto(frame, HEADER_SIZE)
        val crc = crc16CcittFalse(frame, 0, HEADER_SIZE + payload.size)
        putU16(frame, HEADER_SIZE + payload.size, crc)
        return frame
    }

    /** Returns null for any malformed, truncated or CRC-mismatched frame. */
    fun parseFrame(bytes: ByteArray): Frame? {
        if (bytes.size < HEADER_SIZE + TRAILER_SIZE) return null
        if ((bytes[0].toInt() and 0xFF) != MAGIC) return null
        if ((bytes[1].toInt() and 0xFF) != VER) return null
        val payloadLen = u16(bytes, 6)
        if (payloadLen > MAX_PAYLOAD) return null
        if (bytes.size != HEADER_SIZE + payloadLen + TRAILER_SIZE) return null
        val expected = crc16CcittFalse(bytes, 0, HEADER_SIZE + payloadLen)
        if (u16(bytes, HEADER_SIZE + payloadLen) != expected) return null
        val opcodeCode = u8(bytes, 2)
        val payload = bytes.copyOfRange(HEADER_SIZE, HEADER_SIZE + payloadLen)
        return Frame(
            opcodeCode = opcodeCode,
            opcode = Opcode.fromCode(opcodeCode),
            isResponse = u8(bytes, 3) and FLAG_RESPONSE != 0,
            seq = u16(bytes, 4),
            payload = payload,
        )
    }

    /** Parses the Status body; base-only (old firmware) bodies keep zero extension fields. */
    fun parseStatus(bytes: ByteArray): StatusFrame? {
        if (bytes.size < StatusOffset.BASE_SIZE) return null
        val extended = bytes.size >= StatusOffset.SIZE
        return StatusFrame(
            ver = u8(bytes, StatusOffset.VER),
            flags = u8(bytes, StatusOffset.FLAGS),
            gnssState = u8(bytes, StatusOffset.GNSS_STATE),
            fixType = u8(bytes, StatusOffset.FIX_TYPE),
            satellites = u8(bytes, StatusOffset.SATELLITES),
            keyLevels = u8(bytes, StatusOffset.KEY_LEVELS),
            socPct = u8(bytes, StatusOffset.SOC_PCT),
            pdop1e2 = u16(bytes, StatusOffset.PDOP_1E2),
            speedCms = u16(bytes, StatusOffset.SPEED_CMS),
            headingDdeg = u16(bytes, StatusOffset.HEADING_DDEG),
            latE7 = i32(bytes, StatusOffset.LAT_E7),
            lonE7 = i32(bytes, StatusOffset.LON_E7),
            hmslMm = i32(bytes, StatusOffset.HMSL_MM),
            batMv = i32(bytes, StatusOffset.BAT_MV),
            utcEpochS = u32(bytes, StatusOffset.UTC_EPOCH_S),
            uptimeMs = u32(bytes, StatusOffset.UPTIME_MS),
            gnssAgeMs = u32(bytes, StatusOffset.GNSS_AGE_MS),
            steps = u32(bytes, StatusOffset.STEPS),
            accLsb = i16Triple(bytes, StatusOffset.ACC_LSB),
            gyroLsb = i16Triple(bytes, StatusOffset.GYRO_LSB),
            magLsb = i16Triple(bytes, StatusOffset.MAG_LSB),
            baroPressRaw = u32(bytes, StatusOffset.BARO_PRESS_RAW),
            baroTempRaw = u32(bytes, StatusOffset.BARO_TEMP_RAW),
            ringSectorSeq = u32(bytes, StatusOffset.RING_SECTOR_SEQ),
            ringRecords = u32(bytes, StatusOffset.RING_RECORDS),
            gravityMg = if (extended) i16Triple(bytes, StatusOffset.GRAVITY_MG) else ZERO_TRIPLE,
            linearMg = if (extended) i16Triple(bytes, StatusOffset.LINEAR_MG) else ZERO_TRIPLE,
            imuTempCc = if (extended) i16(bytes, StatusOffset.IMU_TEMP_CC) else 0,
            magTempCc = if (extended) i16(bytes, StatusOffset.MAG_TEMP_CC) else 0,
            sensorFlags = if (extended) u8(bytes, StatusOffset.SENSOR_FLAGS) else 0,
            extendedFields = extended,
        )
    }

    fun parseCaps(body: ByteArray): CapsInfo? {
        if (body.size < CapsOffset.SIZE) return null
        return CapsInfo(
            ver = u8(body, CapsOffset.VER),
            protoMajor = u8(body, CapsOffset.PROTO_MAJOR),
            protoMinor = u8(body, CapsOffset.PROTO_MINOR),
            protoPatch = u8(body, CapsOffset.PROTO_PATCH),
            fwMajor = u8(body, CapsOffset.FW_MAJOR),
            fwMinor = u8(body, CapsOffset.FW_MINOR),
            fwPatch = u8(body, CapsOffset.FW_PATCH),
            attMtu = u16(body, CapsOffset.ATT_MTU),
            maxCmdPayload = u16(body, CapsOffset.MAX_CMD_PAYLOAD),
            caps = u32(body, CapsOffset.CAPS),
            telemetryMask = u32(body, CapsOffset.TELEMETRY_MASK),
            statusSize = u16(body, CapsOffset.STATUS_SIZE),
        )
    }

    fun parseFlashInfo(body: ByteArray): FlashInfo? {
        if (body.size < FlashInfoOffset.SIZE) return null
        return FlashInfo(
            flashBytes = u32(body, FlashInfoOffset.FLASH_BYTES),
            otaSlot1Bytes = u32(body, FlashInfoOffset.OTA_SLOT1_BYTES),
            trajBytes = u32(body, FlashInfoOffset.TRAJ_BYTES),
            sectorSeq = u32(body, FlashInfoOffset.SECTOR_SEQ),
            recordsWritten = u32(body, FlashInfoOffset.RECORDS_WRITTEN),
            recordsInSector = u8(body, FlashInfoOffset.RECORDS_IN_SECTOR),
            eraseCountMax = u8(body, FlashInfoOffset.ERASE_COUNT_MAX),
            sectorCount = u8(body, FlashInfoOffset.SECTOR_COUNT),
        )
    }

    /** Splits a Telemetry notification into id/ver/seq plus the payload. */
    fun parseTelemetry(bytes: ByteArray): TelemetryPacket? {
        if (bytes.size < TelemetryOffset.PAYLOAD) return null
        if ((bytes[TelemetryOffset.VER].toInt() and 0xFF) != VER) return null
        return TelemetryPacket(
            id = u8(bytes, TelemetryOffset.ID),
            ver = u8(bytes, TelemetryOffset.VER),
            seq = u16(bytes, TelemetryOffset.SEQ),
            payload = bytes.copyOfRange(TelemetryOffset.PAYLOAD, bytes.size),
        )
    }

    /** IMU telemetry payload: acc[3], gyro[3], quat[4], all i16 little endian. */
    fun parseImuTelemetry(payload: ByteArray): ImuTelemetry? {
        if (payload.size < 20) return null
        return ImuTelemetry(
            accLsb = i16Triple(payload, 0),
            gyroLsb = i16Triple(payload, 6),
            quatQ15 = listOf(
                i16(payload, 12),
                i16(payload, 14),
                i16(payload, 16),
                i16(payload, 18),
            ),
        )
    }

    /** PDR telemetry payload: x_mm i32, y_mm i32, heading_ddeg i16, steps u16. */
    fun parsePdrTelemetry(payload: ByteArray): PdrTelemetry? {
        if (payload.size < 12) return null
        return PdrTelemetry(
            xMm = i32(payload, 0),
            yMm = i32(payload, 4),
            headingDdeg = i16(payload, 8),
            steps = u16(payload, 10),
        )
    }

    /** GNSS receive-path telemetry payload, 34 bytes (see [GnssTelemetryOffset]). */
    fun parseGnssTelemetry(payload: ByteArray): GnssTelemetry? {
        if (payload.size < GnssTelemetryOffset.SIZE) return null
        val flags = u8(payload, GnssTelemetryOffset.FLAGS)
        return GnssTelemetry(
            state = u8(payload, GnssTelemetryOffset.STATE),
            fixType = u8(payload, GnssTelemetryOffset.FIX_TYPE),
            satellites = u8(payload, GnssTelemetryOffset.SATELLITES),
            fixValid = flags and GnssTelemetryOffset.FLAG_FIX_VALID != 0,
            utcValid = flags and GnssTelemetryOffset.FLAG_UTC_VALID != 0,
            pdop1e2 = u16(payload, GnssTelemetryOffset.PDOP_1E2),
            ageMs = u32(payload, GnssTelemetryOffset.AGE_MS),
            rxBytes = u32(payload, GnssTelemetryOffset.RX_BYTES),
            ubxFrames = u32(payload, GnssTelemetryOffset.UBX_FRAMES),
            badChecksum = u32(payload, GnssTelemetryOffset.BAD_CHECKSUM),
            nmeaStarts = u32(payload, GnssTelemetryOffset.NMEA_STARTS),
            rxDropped = u32(payload, GnssTelemetryOffset.RX_DROPPED),
            baud = u32(payload, GnssTelemetryOffset.BAUD).toInt(),
        )
    }

    /**
     * BARO telemetry payload: 20 bytes, or 16 on firmware that predates the appended
     * `alt_msl_mm` (see [BaroTelemetryOffset]). The MSL field is only read when the
     * payload is long enough, and `mslValid` additionally requires that it was sent,
     * so a truncated packet cannot report a valid-looking 0 m altitude.
     */
    fun parseBaroTelemetry(payload: ByteArray): BaroTelemetry? {
        if (payload.size < BaroTelemetryOffset.LEGACY_SIZE) return null
        val hasMsl = payload.size >= BaroTelemetryOffset.SIZE
        val flags = u8(payload, BaroTelemetryOffset.FLAGS)
        return BaroTelemetry(
            pressPa = u32(payload, BaroTelemetryOffset.PRESS_PA),
            relAltMm = i32(payload, BaroTelemetryOffset.REL_ALT_MM),
            tempCc = i16(payload, BaroTelemetryOffset.TEMP_CC),
            flags = flags,
            baselinePaQ4 = u32(payload, BaroTelemetryOffset.BASELINE_PA_Q4),
            altMslMm = if (hasMsl) i32(payload, BaroTelemetryOffset.ALT_MSL_MM) else 0,
            mslValid = hasMsl && flags and BaroTelemetryOffset.FLAG_MSL_VALID != 0,
        )
    }

    /** Satellite table payload (see [SatTelemetry]); tolerates a shorter list. */
    fun parseSatTelemetry(payload: ByteArray): SatTelemetry? {
        if (payload.size < SatTelemetry.HEADER) return null
        val count = u8(payload, 0)
        val total = u8(payload, 1)
        val available = (payload.size - SatTelemetry.HEADER) / SatTelemetry.SIZE_ENTRY
        val rows = (minOf(count, available)).coerceAtMost(SatTelemetry.MAX)
        val out = ArrayList<SatTelemetryRow>(rows)
        for (index in 0 until rows) {
            val base = SatTelemetry.HEADER + index * SatTelemetry.SIZE_ENTRY
            out += SatTelemetryRow(
                gnssId = u8(payload, base),
                svId = u8(payload, base + 1),
                cno = u8(payload, base + 2),
                elevation = payload[base + 3].toInt(),
                state = u8(payload, base + 4),
            )
        }
        return SatTelemetry(rows = out, total = if (total == 0) rows else total)
    }

    /** NMEA payload: `u8 len` then that many ASCII bytes (no terminator). */
    fun parseNmeaTelemetry(payload: ByteArray): NmeaTelemetry? {
        if (payload.size < 2) return null
        val len = u8(payload, 0)
        if (len == 0 || payload.size < 1 + len) return null
        return NmeaTelemetry(text = String(payload, 1, len, Charsets.US_ASCII))
    }

    /** Parses a Log notification; returns null when the framing prefix is invalid. */
    fun parseLog(bytes: ByteArray): LogPacket? {
        if (bytes.size < 2) return null
        val level = u8(bytes, 0)
        val srcLen = u8(bytes, 1)
        if (2 + srcLen > bytes.size) return null
        val source = String(bytes, 2, srcLen, Charsets.US_ASCII)
        val message = String(bytes, 2 + srcLen, bytes.size - 2 - srcLen, Charsets.UTF_8)
        return LogPacket(level = level, source = source, message = message)
    }

    /** Contract name of gnss_state (`BLE_Wire_v1.md` §4, offset 2). */
    fun gnssStateName(code: Int): String = when (code) {
        0 -> "OFF"
        1 -> "COLD_START"
        2 -> "FIXED"
        3 -> "EPHEMERIS_HOLD"
        4 -> "BACKOFF"
        else -> "UNKNOWN($code)"
    }

    /** Uppercase hex with single spaces, used for console responses and payload echoes. */
    fun toHex(bytes: ByteArray): String =
        bytes.joinToString(" ") { String.format(Locale.US, "%02X", it.toInt() and 0xFF) }

    fun putU16(dst: ByteArray, offset: Int, value: Int) {
        dst[offset] = (value and 0xFF).toByte()
        dst[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    fun u8(data: ByteArray, offset: Int): Int = data[offset].toInt() and 0xFF

    fun u16(data: ByteArray, offset: Int): Int =
        u8(data, offset) or (u8(data, offset + 1) shl 8)

    fun u32(data: ByteArray, offset: Int): Long =
        u16(data, offset).toLong() or (u16(data, offset + 2).toLong() shl 16)

    fun i16(data: ByteArray, offset: Int): Int = u16(data, offset).toShort().toInt()

    fun i32(data: ByteArray, offset: Int): Int =
        u16(data, offset) or (u16(data, offset + 2) shl 16)

    private fun i16Triple(data: ByteArray, offset: Int): List<Int> = listOf(
        i16(data, offset),
        i16(data, offset + 2),
        i16(data, offset + 4),
    )

    private val ZERO_TRIPLE = listOf(0, 0, 0)
}
