// SPDX-License-Identifier: MIT
package com.lifegps.core.protocol

/**
 * Pure-JVM CRC helpers matching `protocol/lifegps_protocol.yaml` and `Firmware/src/util/crc.c`:
 * - CRC-8: poly 0x07, init 0x00 (32-byte trajectory record integrity)
 * - CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF (command/data frames and LGD1 header)
 * - CRC-32/ISO-HDLC: reflected poly 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF (LGD1 footer)
 */
object ProtocolCrc {

    fun crc8(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Int {
        require(offset >= 0 && length >= 0 && offset + length <= data.size) {
            "Invalid slice: offset=$offset length=$length size=${data.size}"
        }
        var crc = 0
        for (i in offset until offset + length) {
            crc = (crc xor (data[i].toInt() and 0xFF)) and 0xFF
            repeat(8) {
                crc = if ((crc and 0x80) != 0) {
                    ((crc shl 1) xor 0x07) and 0xFF
                } else {
                    (crc shl 1) and 0xFF
                }
            }
        }
        return crc
    }

    fun crc16CcittFalse(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Int {
        require(offset >= 0 && length >= 0 && offset + length <= data.size) {
            "Invalid slice: offset=$offset length=$length size=${data.size}"
        }
        var crc = 0xFFFF
        for (i in offset until offset + length) {
            crc = crc xor ((data[i].toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if ((crc and 0x8000) != 0) {
                    ((crc shl 1) xor 0x1021) and 0xFFFF
                } else {
                    (crc shl 1) and 0xFFFF
                }
            }
        }
        return crc
    }

    fun crc32IsoHdlc(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Long {
        require(offset >= 0 && length >= 0 && offset + length <= data.size) {
            "Invalid slice: offset=$offset length=$length size=${data.size}"
        }
        var crc = 0xFFFFFFFFL
        for (i in offset until offset + length) {
            crc = crc xor (data[i].toLong() and 0xFFL)
            repeat(8) {
                crc = if ((crc and 1L) != 0L) {
                    (crc ushr 1) xor 0xEDB88320L
                } else {
                    crc ushr 1
                }
            }
        }
        return (crc xor 0xFFFFFFFFL) and 0xFFFFFFFFL
    }
}
