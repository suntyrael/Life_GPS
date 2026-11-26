// SPDX-License-Identifier: MIT
package com.lifegps.core.protocol

/**
 * Plaintext LGD1 day-package parser & codec (`packages.day` in `protocol/lifegps_protocol.yaml`).
 * Supports:
 * - codec 0 (`RAW`): uncompressed 32-byte trajectory record stream
 * - codec 1 (`BYTE_DELTA_HEATSHRINK`): byte-plane delta + Heatshrink LZSS (W=8, L=4)
 *
 * Integrity is verified twice:
 * 1. Header CRC-16/CCITT-FALSE over bytes 0..45 compared with bytes 46..47.
 * 2. Footer CRC-32/ISO-HDLC compared against the uncompressed trajectory byte stream.
 */
data class DayPackageHeader(
    val headerVer: Int,
    val codecId: Int,
    val dayId: Long,
    val recordCount: Long,
    val flags: Long,
    val rawLen: Long,
    val packedLen: Long,
    val poiCount: Long,
    val totalSteps: Long,
    val prevDayId: Long,
    val reserved1: Long,
    val reserved2: Int,
)

data class DecodedDayPackage(
    val header: DayPackageHeader,
    val rawBytes: ByteArray,
    val records: List<TrajectoryRecord>,
    val crc32: Long,
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is DecodedDayPackage) return false
        return header == other.header &&
            rawBytes.contentEquals(other.rawBytes) &&
            records == other.records &&
            crc32 == other.crc32
    }

    override fun hashCode(): Int {
        var result = header.hashCode()
        result = 31 * result + rawBytes.contentHashCode()
        result = 31 * result + records.hashCode()
        result = 31 * result + crc32.hashCode()
        return result
    }
}

object DayPackageCodec {

    private val MAGIC = byteArrayOf('L'.code.toByte(), 'G'.code.toByte(), 'D'.code.toByte(), '1'.code.toByte())

    fun parseHeader(bytes: ByteArray): DayPackageHeader {
        require(bytes.size >= GeneratedProtocol.DayPackage.HEADER_SIZE) {
            "LGD1 buffer too short for header: ${bytes.size}"
        }
        for (i in 0 until 4) {
            require(bytes[i] == MAGIC[i]) { "Invalid LGD1 magic" }
        }
        val expectedCrc = ProtocolCrc.crc16CcittFalse(bytes, 0, GeneratedProtocol.DayPackage.HEADER_CRC16_LEN)
        val actualCrc = getU16(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_CRC16)
        require(expectedCrc == actualCrc) {
            "LGD1 header CRC-16 mismatch: expected=0x${expectedCrc.toString(16)} actual=0x${actualCrc.toString(16)}"
        }
        val headerVer = getU16(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_VER)
        require(headerVer == GeneratedProtocol.DayPackage.HEADER_VER) {
            "Unsupported LGD1 header_ver=$headerVer"
        }
        return DayPackageHeader(
            headerVer = headerVer,
            codecId = getU16(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_CODEC_ID),
            dayId = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_DAY_ID),
            recordCount = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_RECORD_COUNT),
            flags = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_FLAGS),
            rawLen = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_RAW_LEN),
            packedLen = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_PACKED_LEN),
            poiCount = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_POI_COUNT),
            totalSteps = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_TOTAL_STEPS),
            prevDayId = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_PREV_DAY_ID),
            reserved1 = getU32(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_RESERVED1),
            reserved2 = getU16(bytes, GeneratedProtocol.DayPackage.HEADER_OFF_RESERVED2),
        )
    }

    fun decode(bytes: ByteArray): DecodedDayPackage {
        val header = parseHeader(bytes)
        val expectedSize = GeneratedProtocol.DayPackage.HEADER_SIZE +
            header.packedLen.toInt() +
            GeneratedProtocol.DayPackage.FOOTER_SIZE
        require(bytes.size == expectedSize) {
            "LGD1 size mismatch: expected=$expectedSize actual=${bytes.size}"
        }
        val expectedRawLen = header.recordCount * GeneratedProtocol.TrajectoryRecord.SIZE
        require(header.rawLen == expectedRawLen) {
            "LGD1 raw_len=${header.rawLen} disagrees with record_count=${header.recordCount}"
        }

        val payloadStart = GeneratedProtocol.DayPackage.HEADER_SIZE
        val payloadEnd = payloadStart + header.packedLen.toInt()
        val packed = bytes.copyOfRange(payloadStart, payloadEnd)

        val rawBytes = when (header.codecId) {
            GeneratedProtocol.DayPackage.CODEC_RAW -> packed
            GeneratedProtocol.DayPackage.CODEC_BYTE_DELTA_HEATSHRINK -> {
                val delta = heatshrinkDecode(
                    data = packed,
                    expectedLen = header.rawLen.toInt(),
                    windowSz2 = GeneratedProtocol.DayPackage.HS_WINDOW_SZ2,
                    lookaheadSz2 = GeneratedProtocol.DayPackage.HS_LOOKAHEAD_SZ2,
                )
                byteDeltaDecode(delta, GeneratedProtocol.TrajectoryRecord.SIZE)
            }
            else -> throw IllegalArgumentException("Unsupported LGD1 codec_id=${header.codecId}")
        }

        require(rawBytes.size == header.rawLen.toInt()) {
            "Decompressed length ${rawBytes.size} != header.rawLen ${header.rawLen}"
        }

        val footerCrc32 = getU32(bytes, payloadEnd)
        val computedCrc32 = ProtocolCrc.crc32IsoHdlc(rawBytes)
        require(footerCrc32 == computedCrc32) {
            "LGD1 payload CRC-32 mismatch: expected=0x${footerCrc32.toString(16)} computed=0x${computedCrc32.toString(16)}"
        }

        val records = TrajectoryRecord.decodeStream(rawBytes)
        return DecodedDayPackage(
            header = header,
            rawBytes = rawBytes,
            records = records,
            crc32 = computedCrc32,
        )
    }

    fun byteDeltaEncode(raw: ByteArray, recordSize: Int = GeneratedProtocol.TrajectoryRecord.SIZE): ByteArray {
        require(raw.size % recordSize == 0) {
            "Raw length ${raw.size} is not a multiple of $recordSize"
        }
        if (raw.size <= recordSize) return raw.copyOf()
        val out = ByteArray(raw.size)
        raw.copyInto(out, 0, 0, recordSize)
        var off = recordSize
        while (off < raw.size) {
            val prev = off - recordSize
            for (i in 0 until recordSize) {
                val currByte = raw[off + i].toInt() and 0xFF
                val prevByte = raw[prev + i].toInt() and 0xFF
                out[off + i] = ((currByte - prevByte) and 0xFF).toByte()
            }
            off += recordSize
        }
        return out
    }

    fun byteDeltaDecode(encoded: ByteArray, recordSize: Int = GeneratedProtocol.TrajectoryRecord.SIZE): ByteArray {
        require(encoded.size % recordSize == 0) {
            "Encoded length ${encoded.size} is not a multiple of $recordSize"
        }
        if (encoded.size <= recordSize) return encoded.copyOf()
        val out = ByteArray(encoded.size)
        encoded.copyInto(out, 0, 0, recordSize)
        var off = recordSize
        while (off < encoded.size) {
            val prev = off - recordSize
            for (i in 0 until recordSize) {
                val prevByte = out[prev + i].toInt() and 0xFF
                val deltaByte = encoded[off + i].toInt() and 0xFF
                out[off + i] = ((prevByte + deltaByte) and 0xFF).toByte()
            }
            off += recordSize
        }
        return out
    }

    /**
     * Heatshrink LZSS decoder matching `atomicobject/heatshrink` MSB-first bitstream
     * (`window_sz2=8`, `lookahead_sz2=4`).
     */
    fun heatshrinkDecode(
        data: ByteArray,
        expectedLen: Int,
        windowSz2: Int = GeneratedProtocol.DayPackage.HS_WINDOW_SZ2,
        lookaheadSz2: Int = GeneratedProtocol.DayPackage.HS_LOOKAHEAD_SZ2,
    ): ByteArray {
        require(windowSz2 in 4..15 && lookaheadSz2 in 3 until windowSz2) {
            "Invalid Heatshrink parameters W=$windowSz2 L=$lookaheadSz2"
        }
        require(expectedLen >= 0) { "Negative expectedLen=$expectedLen" }

        val reader = BitReader(data)
        val out = ByteArray(expectedLen)
        var pos = 0

        while (pos < expectedLen) {
            val flag = reader.pullBits(1) ?: break
            if (flag == 1) {
                val literal = reader.pullBits(8) ?: break
                out[pos++] = literal.toByte()
            } else {
                val distMinus1 = reader.pullBits(windowSz2) ?: break
                val lenMinus1 = reader.pullBits(lookaheadSz2) ?: break
                val dist = distMinus1 + 1
                val matchLen = lenMinus1 + 1
                require(dist <= pos) { "Invalid Heatshrink backreference dist=$dist at pos=$pos" }
                for (k in 0 until matchLen) {
                    if (pos >= expectedLen) break
                    out[pos] = out[pos - dist]
                    pos++
                }
            }
        }
        require(pos == expectedLen) {
            "Truncated Heatshrink stream: decoded=$pos expected=$expectedLen"
        }
        return out
    }

    private class BitReader(private val data: ByteArray) {
        private var bitPos = 0
        private val totalBits = data.size * 8

        fun pullBits(count: Int): Int? {
            if (totalBits - bitPos < count) return null
            var value = 0
            for (i in 0 until count) {
                val byteIdx = bitPos ushr 3
                val bitIdx = 7 - (bitPos and 7)
                value = (value shl 1) or ((data[byteIdx].toInt() ushr bitIdx) and 1)
                bitPos++
            }
            return value
        }
    }
}
