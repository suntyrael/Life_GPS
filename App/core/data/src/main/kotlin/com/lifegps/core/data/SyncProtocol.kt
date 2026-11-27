// SPDX-License-Identifier: MIT
package com.lifegps.core.data

/**
 * Binary wire codecs for M3 bulk sync, day archive index, erase, and self-test commands.
 * Offsets match `Firmware/src/ble/ble_gatt.c` and `Firmware/src/storage/archive_codec.c`.
 */
data class SyncBeginResponse(
    val transferId: Long,
    val totalRecords: Long,
    val firstSeqId: Long,
    val lastSeqId: Long,
    val chunkRecords: Int,
)

data class SyncReadResponse(
    val nextSeqId: Long,
    val chunksSent: Int,
    val done: Boolean,
)

data class RemoteDayIndexEntry(
    val dayId: Int,
    val flashOffset: Long,
    val packageLen: Long,
    val recordCount: Long,
    val trajCrc32: Long,
    val firstSeqId: Long,
    val codecId: Int,
    val flags: Int,
)

data class DayIndexPage(
    val indexVersion: Long,
    val totalDays: Int,
    val returnedDays: Int,
    val startIdx: Int,
    val entries: List<RemoteDayIndexEntry>,
)

data class GetArchiveResponse(
    val totalLen: Long,
    val nextOffset: Long,
    val chunksSent: Int,
    val done: Boolean,
)

object SyncProtocol {
    const val JOURNAL_ENTRY_SIZE = 28

    fun encodeSyncBeginReq(fromSeqId: Long): ByteArray = ByteArray(4).also { putU32(it, 0, fromSeqId) }

    fun decodeSyncBeginRsp(body: ByteArray): SyncBeginResponse? {
        if (body.size < 18) return null
        return SyncBeginResponse(
            transferId = getU32(body, 0),
            totalRecords = getU32(body, 4),
            firstSeqId = getU32(body, 8),
            lastSeqId = getU32(body, 12),
            chunkRecords = getU16(body, 16),
        )
    }

    fun encodeSyncReadReq(transferId: Long, cursorSeqId: Long, maxChunks: Int = 8): ByteArray =
        ByteArray(10).also {
            putU32(it, 0, transferId)
            putU32(it, 4, cursorSeqId)
            putU16(it, 8, maxChunks)
        }

    fun decodeSyncReadRsp(body: ByteArray): SyncReadResponse? {
        if (body.size < 7) return null
        return SyncReadResponse(
            nextSeqId = getU32(body, 0),
            chunksSent = getU16(body, 4),
            done = (body[6].toInt() and 0xFF) != 0,
        )
    }

    fun encodeSyncEndReq(transferId: Long, ackSeqId: Long): ByteArray =
        ByteArray(8).also {
            putU32(it, 0, transferId)
            putU32(it, 4, ackSeqId)
        }

    fun encodeGetDayIndexReq(startIdx: Int = 0): ByteArray =
        if (startIdx in 0..255) {
            byteArrayOf(startIdx.toByte())
        } else {
            ByteArray(2).also { putU16(it, 0, startIdx) }
        }

    fun decodeDayIndexPage(body: ByteArray): DayIndexPage? {
        if (body.size < 8) return null
        val ver = getU32(body, 0)
        val ext = body[7].toInt() and 0xFF
        val total = (body[4].toInt() and 0xFF) or ((ext and 0x0F) shl 8)
        val returned = body[5].toInt() and 0xFF
        val start = (body[6].toInt() and 0xFF) or (((ext ushr 4) and 0x0F) shl 8)
        val entries = ArrayList<RemoteDayIndexEntry>(returned)
        var off = 8
        for (i in 0 until returned) {
            if (off + JOURNAL_ENTRY_SIZE > body.size) break
            entries.add(
                RemoteDayIndexEntry(
                    dayId = getU32(body, off).toInt(),
                    flashOffset = getU32(body, off + 4),
                    packageLen = getU32(body, off + 8),
                    recordCount = getU32(body, off + 12),
                    trajCrc32 = getU32(body, off + 16),
                    firstSeqId = getU32(body, off + 20),
                    codecId = getU16(body, off + 24),
                    flags = getU16(body, off + 26),
                ),
            )
            off += JOURNAL_ENTRY_SIZE
        }
        return DayIndexPage(
            indexVersion = ver,
            totalDays = total,
            returnedDays = entries.size,
            startIdx = start,
            entries = entries,
        )
    }

    fun encodeGetArchiveReq(dayId: Int, byteOffset: Long, maxChunks: Int = 8): ByteArray =
        ByteArray(10).also {
            putU32(it, 0, dayId.toLong() and 0xFFFFFFFFL)
            putU32(it, 4, byteOffset)
            putU16(it, 8, maxChunks)
        }

    fun decodeGetArchiveRsp(body: ByteArray): GetArchiveResponse? {
        if (body.size < 11) return null
        return GetArchiveResponse(
            totalLen = getU32(body, 0),
            nextOffset = getU32(body, 4),
            chunksSent = getU16(body, 8),
            done = (body[10].toInt() and 0xFF) != 0,
        )
    }

    fun encodeEraseDayReq(dayId: Int): ByteArray =
        ByteArray(4).also { putU32(it, 0, dayId.toLong() and 0xFFFFFFFFL) }

    private fun getU16(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    private fun getU32(b: ByteArray, off: Int): Long =
        (b[off].toLong() and 0xFFL) or
            ((b[off + 1].toLong() and 0xFFL) shl 8) or
            ((b[off + 2].toLong() and 0xFFL) shl 16) or
            ((b[off + 3].toLong() and 0xFFL) shl 24)

    private fun putU16(b: ByteArray, off: Int, v: Int) {
        b[off] = (v and 0xFF).toByte()
        b[off + 1] = ((v ushr 8) and 0xFF).toByte()
    }

    private fun putU32(b: ByteArray, off: Int, v: Long) {
        b[off] = (v and 0xFFL).toByte()
        b[off + 1] = ((v ushr 8) and 0xFFL).toByte()
        b[off + 2] = ((v ushr 16) and 0xFFL).toByte()
        b[off + 3] = ((v ushr 24) and 0xFFL).toByte()
    }
}
