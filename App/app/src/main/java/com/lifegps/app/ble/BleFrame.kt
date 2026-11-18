package com.lifegps.app.ble

import java.util.Locale

/**
 * Offset table of the 82-byte Status snapshot.
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
    const val SIZE = 82
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
    IMU_SNAPSHOT(0x32, "IMU_SNAPSHOT"),
    BUTTON_EVENT(0x33, "BUTTON_EVENT"),
    SELFTEST(0x40, "SELFTEST"),
    MAG_CAL(0x41, "MAG_CAL"),
    BARO_BASELINE(0x42, "BARO_BASELINE"),
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

/** Decoded 82-byte Status snapshot. */
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
) {
    val gnssFixValid: Boolean get() = flags and StatusFlags.GNSS_FIX_VALID != 0
    val charging: Boolean get() = flags and StatusFlags.CHARGING != 0
    val zupt: Boolean get() = flags and StatusFlags.ZUPT != 0
    val imuValid: Boolean get() = flags and StatusFlags.IMU_VALID != 0
    val magValid: Boolean get() = flags and StatusFlags.MAG_VALID != 0
    val baroValid: Boolean get() = flags and StatusFlags.BARO_VALID != 0
    val ringReady: Boolean get() = flags and StatusFlags.RING_READY != 0
    val utcValid: Boolean get() = flags and StatusFlags.UTC_VALID != 0
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

    /** Parses the 82-byte Status body; short frames are reported as missing (null). */
    fun parseStatus(bytes: ByteArray): StatusFrame? {
        if (bytes.size < StatusOffset.SIZE) return null
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
}
