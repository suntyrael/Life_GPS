// SPDX-License-Identifier: MIT
package com.lifegps.core.protocol

/**
 * 0x5A Data-channel chunk frame (`frames.data` in `protocol/lifegps_protocol.yaml`).
 * Header = 14 B: magic(1), ver(1), stream(1), flags(1), transfer_id(4), cursor(4), len(2).
 * Trailer = 2 B CRC-16/CCITT-FALSE over header + payload.
 */
data class DataFrame(
    val stream: Int,
    val flags: Int,
    val transferId: Long,
    val cursor: Long,
    val payload: ByteArray,
) {
    val isFirst: Boolean get() = (flags and FLAG_FIRST) != 0
    val isLast: Boolean get() = (flags and FLAG_LAST) != 0

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is DataFrame) return false
        return stream == other.stream &&
            flags == other.flags &&
            transferId == other.transferId &&
            cursor == other.cursor &&
            payload.contentEquals(other.payload)
    }

    override fun hashCode(): Int {
        var result = stream
        result = 31 * result + flags
        result = 31 * result + transferId.hashCode()
        result = 31 * result + cursor.hashCode()
        result = 31 * result + payload.contentHashCode()
        return result
    }

    companion object {
        const val FLAG_FIRST = 1 shl 0
        const val FLAG_LAST = 1 shl 1

        fun encode(frame: DataFrame): ByteArray {
            require(frame.payload.size <= GeneratedProtocol.Meta.MAX_DATA_PAYLOAD) {
                "DataFrame payload ${frame.payload.size} exceeds max ${GeneratedProtocol.Meta.MAX_DATA_PAYLOAD}"
            }
            val hdrLen = GeneratedProtocol.DataFrame.HEADER_SIZE
            val out = ByteArray(hdrLen + frame.payload.size + GeneratedProtocol.DataFrame.TRAILER_SIZE)
            out[0] = GeneratedProtocol.DataFrame.MAGIC.toByte()
            out[1] = GeneratedProtocol.Meta.VERSION_MAJOR.toByte()
            out[2] = (frame.stream and 0xFF).toByte()
            out[3] = (frame.flags and 0xFF).toByte()
            putU32(out, 4, frame.transferId)
            putU32(out, 8, frame.cursor)
            putU16(out, 12, frame.payload.size)
            frame.payload.copyInto(out, destinationOffset = hdrLen)
            val crc = ProtocolCrc.crc16CcittFalse(out, 0, hdrLen + frame.payload.size)
            putU16(out, hdrLen + frame.payload.size, crc)
            return out
        }

        fun decode(bytes: ByteArray): DataFrame? {
            val minLen = GeneratedProtocol.DataFrame.HEADER_SIZE + GeneratedProtocol.DataFrame.TRAILER_SIZE
            if (bytes.size < minLen) return null
            if ((bytes[0].toInt() and 0xFF) != GeneratedProtocol.DataFrame.MAGIC) return null
            if ((bytes[1].toInt() and 0xFF) != GeneratedProtocol.Meta.VERSION_MAJOR) return null
            val payloadLen = getU16(bytes, 12)
            val expectedTotal = GeneratedProtocol.DataFrame.HEADER_SIZE + payloadLen + GeneratedProtocol.DataFrame.TRAILER_SIZE
            if (bytes.size != expectedTotal) return null
            val crcOffset = GeneratedProtocol.DataFrame.HEADER_SIZE + payloadLen
            val expectedCrc = ProtocolCrc.crc16CcittFalse(bytes, 0, crcOffset)
            val actualCrc = getU16(bytes, crcOffset)
            if (expectedCrc != actualCrc) return null
            return DataFrame(
                stream = bytes[2].toInt() and 0xFF,
                flags = bytes[3].toInt() and 0xFF,
                transferId = getU32(bytes, 4),
                cursor = getU32(bytes, 8),
                payload = bytes.copyOfRange(GeneratedProtocol.DataFrame.HEADER_SIZE, crcOffset),
            )
        }
    }
}
