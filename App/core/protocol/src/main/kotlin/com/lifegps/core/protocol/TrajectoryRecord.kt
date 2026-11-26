// SPDX-License-Identifier: MIT
package com.lifegps.core.protocol

/**
 * 32-byte trajectory record (`records.trajectory_record` in `protocol/lifegps_protocol.yaml`).
 * All multi-byte fields are little-endian; byte 31 holds CRC-8 over bytes 0..30.
 */
data class TrajectoryRecord(
    val seqId: Long,
    val timestampSec: Long,
    val subsecondMs: Int,
    val latitudeE7: Int,
    val longitudeE7: Int,
    val altitudeM: Int,
    val speedCms: Int,
    val headingDeciDeg: Int,
    val stepCount: Int,
    val hdopDeci: Int,
    val satellites: Int,
    val statusFlags: Int,
    val batteryLevel: Int,
    val floorLevel: Int,
) {
    val latitudeDeg: Double get() = latitudeE7 / 1e7
    val longitudeDeg: Double get() = longitudeE7 / 1e7
    val speedMs: Double get() = speedCms / 100.0
    val headingDeg: Double get() = headingDeciDeg / 10.0
    val hdop: Double get() = hdopDeci / 10.0

    val unixEpochMs: Long
        get() = (timestampSec + LIFE_GPS_EPOCH_UNIX_S) * 1000L + subsecondMs

    val isGnssValid: Boolean get() = (statusFlags and FLAG_GNSS_VALID) != 0
    val isPdrSource: Boolean get() = (statusFlags and FLAG_PDR_SOURCE) != 0
    val isMoving: Boolean get() = (statusFlags and FLAG_MOVING) != 0
    val isIndoor: Boolean get() = (statusFlags and FLAG_INDOOR) != 0
    val isVehicle: Boolean get() = (statusFlags and FLAG_VEHICLE) != 0
    val isCharging: Boolean get() = (statusFlags and FLAG_CHARGING) != 0
    val isTimeInvalid: Boolean get() = (statusFlags and FLAG_TIME_INVALID) != 0
    val isPoi: Boolean get() = (statusFlags and FLAG_POI) != 0

    fun encode(): ByteArray {
        val out = ByteArray(GeneratedProtocol.TrajectoryRecord.SIZE)
        putU32(out, GeneratedProtocol.TrajectoryRecord.OFF_SEQ_ID, seqId)
        putU32(out, GeneratedProtocol.TrajectoryRecord.OFF_TIMESTAMP, timestampSec)
        putU16(out, GeneratedProtocol.TrajectoryRecord.OFF_SUBSECOND_MS, subsecondMs)
        putI32(out, GeneratedProtocol.TrajectoryRecord.OFF_LATITUDE, latitudeE7)
        putI32(out, GeneratedProtocol.TrajectoryRecord.OFF_LONGITUDE, longitudeE7)
        putI16(out, GeneratedProtocol.TrajectoryRecord.OFF_ALTITUDE, altitudeM)
        putU16(out, GeneratedProtocol.TrajectoryRecord.OFF_SPEED_CMS, speedCms)
        putU16(out, GeneratedProtocol.TrajectoryRecord.OFF_HEADING, headingDeciDeg)
        putU16(out, GeneratedProtocol.TrajectoryRecord.OFF_STEP_COUNT, stepCount)
        out[GeneratedProtocol.TrajectoryRecord.OFF_HDOP] = (hdopDeci and 0xFF).toByte()
        out[GeneratedProtocol.TrajectoryRecord.OFF_SATELLITES] = (satellites and 0xFF).toByte()
        out[GeneratedProtocol.TrajectoryRecord.OFF_STATUS_FLAGS] = (statusFlags and 0xFF).toByte()
        out[GeneratedProtocol.TrajectoryRecord.OFF_BATTERY_LEVEL] = (batteryLevel and 0xFF).toByte()
        out[GeneratedProtocol.TrajectoryRecord.OFF_FLOOR_LEVEL] = floorLevel.toByte()
        out[GeneratedProtocol.TrajectoryRecord.OFF_CHECKSUM] =
            ProtocolCrc.crc8(out, 0, GeneratedProtocol.TrajectoryRecord.OFF_CHECKSUM).toByte()
        return out
    }

    companion object {
        /** Life_GPS epoch origin: 2020-01-01T00:00:00Z in Unix seconds. */
        const val LIFE_GPS_EPOCH_UNIX_S: Long = 1_577_836_800L

        const val FLAG_GNSS_VALID = 1 shl 0
        const val FLAG_PDR_SOURCE = 1 shl 1
        const val FLAG_MOVING = 1 shl 2
        const val FLAG_INDOOR = 1 shl 3
        const val FLAG_VEHICLE = 1 shl 4
        const val FLAG_CHARGING = 1 shl 5
        const val FLAG_TIME_INVALID = 1 shl 6
        const val FLAG_POI = 1 shl 7

        fun decode(bytes: ByteArray, offset: Int = 0): TrajectoryRecord? {
            val size = GeneratedProtocol.TrajectoryRecord.SIZE
            if (offset < 0 || offset + size > bytes.size) return null
            val expectedCrc = ProtocolCrc.crc8(bytes, offset, GeneratedProtocol.TrajectoryRecord.OFF_CHECKSUM)
            val actualCrc = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_CHECKSUM].toInt() and 0xFF
            if (expectedCrc != actualCrc) return null
            return TrajectoryRecord(
                seqId = getU32(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_SEQ_ID),
                timestampSec = getU32(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_TIMESTAMP),
                subsecondMs = getU16(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_SUBSECOND_MS),
                latitudeE7 = getI32(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_LATITUDE),
                longitudeE7 = getI32(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_LONGITUDE),
                altitudeM = getI16(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_ALTITUDE),
                speedCms = getU16(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_SPEED_CMS),
                headingDeciDeg = getU16(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_HEADING),
                stepCount = getU16(bytes, offset + GeneratedProtocol.TrajectoryRecord.OFF_STEP_COUNT),
                hdopDeci = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_HDOP].toInt() and 0xFF,
                satellites = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_SATELLITES].toInt() and 0xFF,
                statusFlags = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_STATUS_FLAGS].toInt() and 0xFF,
                batteryLevel = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_BATTERY_LEVEL].toInt() and 0xFF,
                floorLevel = bytes[offset + GeneratedProtocol.TrajectoryRecord.OFF_FLOOR_LEVEL].toInt(),
            )
        }

        fun decodeStream(bytes: ByteArray): List<TrajectoryRecord> {
            val size = GeneratedProtocol.TrajectoryRecord.SIZE
            require(bytes.size % size == 0) {
                "Trajectory stream length ${bytes.size} is not a multiple of $size"
            }
            val count = bytes.size / size
            val result = ArrayList<TrajectoryRecord>(count)
            for (i in 0 until count) {
                val rec = decode(bytes, i * size)
                    ?: throw IllegalArgumentException("CRC-8 mismatch at trajectory record index $i")
                result.add(rec)
            }
            return result
        }
    }
}

internal fun getU16(buf: ByteArray, off: Int): Int =
    (buf[off].toInt() and 0xFF) or ((buf[off + 1].toInt() and 0xFF) shl 8)

internal fun getI16(buf: ByteArray, off: Int): Int = getU16(buf, off).toShort().toInt()

internal fun getU32(buf: ByteArray, off: Int): Long =
    (buf[off].toLong() and 0xFFL) or
        ((buf[off + 1].toLong() and 0xFFL) shl 8) or
        ((buf[off + 2].toLong() and 0xFFL) shl 16) or
        ((buf[off + 3].toLong() and 0xFFL) shl 24)

internal fun getI32(buf: ByteArray, off: Int): Int = getU32(buf, off).toInt()

internal fun putU16(buf: ByteArray, off: Int, value: Int) {
    buf[off] = (value and 0xFF).toByte()
    buf[off + 1] = ((value ushr 8) and 0xFF).toByte()
}

internal fun putI16(buf: ByteArray, off: Int, value: Int) = putU16(buf, off, value)

internal fun putU32(buf: ByteArray, off: Int, value: Long) {
    buf[off] = (value and 0xFFL).toByte()
    buf[off + 1] = ((value ushr 8) and 0xFFL).toByte()
    buf[off + 2] = ((value ushr 16) and 0xFFL).toByte()
    buf[off + 3] = ((value ushr 24) and 0xFFL).toByte()
}

internal fun putI32(buf: ByteArray, off: Int, value: Int) = putU32(buf, off, value.toLong())
