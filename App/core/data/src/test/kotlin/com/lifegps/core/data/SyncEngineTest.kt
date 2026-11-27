// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.DataFrame
import com.lifegps.core.protocol.GeneratedProtocol
import com.lifegps.core.protocol.ProtocolCrc
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncEngineTest {

    private fun loadVector(name: String): ByteArray {
        val stream = requireNotNull(javaClass.classLoader?.getResourceAsStream(name))
        return stream.use { it.readBytes() }
    }

    @Test
    fun syncsLiveStagingRecordsAndArchivedGoldenDayPackage() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_sync").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val liveBytes = loadVector("trajectory_basic.bin")
            val archiveRawBytes = loadVector("trajectory_delta_wrap.bin")
            val hsPkg = loadVector("day_package_hs.bin")
            val archiveDayId = 20261126

            val fakeChannel = object : SyncChannel {
                private var listener: ((DataFrame) -> Unit)? = null

                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
                    this.listener = listener
                }

                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN -> {
                            val body = ByteArray(18)
                            putU32(body, 0, 7L)     // transferId
                            putU32(body, 4, 3L)     // totalRecords
                            putU32(body, 8, 100L)   // firstSeq
                            putU32(body, 12, 102L)  // lastSeq
                            putU16(body, 16, 7)     // chunkRecords
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_READ -> {
                            listener?.invoke(
                                DataFrame(
                                    stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                    flags = 0x01,
                                    transferId = 7L,
                                    cursor = 100L,
                                    payload = liveBytes,
                                ),
                            )
                            val body = ByteArray(7)
                            putU32(body, 0, 103L)
                            putU16(body, 4, 1)
                            body[6] = 1
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX -> {
                            val out = ByteArrayOutputStream()
                            val hdr = ByteArray(8)
                            putU32(hdr, 0, 1L)
                            hdr[4] = 1
                            hdr[5] = 1
                            hdr[6] = 0
                            hdr[7] = 0
                            out.write(hdr)
                            out.write(encodeJournalEntry(archiveDayId, hsPkg.size.toLong(), 8L, 0xCC798479L))
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, out.toByteArray())
                        }
                        GeneratedProtocol.CommandOpcode.GET_ARCHIVE -> {
                            listener?.invoke(
                                DataFrame(
                                    stream = GeneratedProtocol.StreamId.ARCHIVE_DAY,
                                    flags = 0x01,
                                    transferId = archiveDayId.toLong(),
                                    cursor = 0L,
                                    payload = hsPkg,
                                ),
                            )
                            val body = ByteArray(11)
                            putU32(body, 0, hsPkg.size.toLong())
                            putU32(body, 4, hsPkg.size.toLong())
                            putU16(body, 8, 1)
                            body[10] = 1
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val engine = SyncEngine(store, fakeChannel)
            val result = engine.syncAll(fromSeqId = 0L, tzOffsetMinutes = 480)
            assertNull(result.error)
            assertEquals(1, result.archivedDaysDownloaded)
            assertEquals(103L, result.newWatermarkSeqId)
            assertTrue(store.hasArchivedDay(archiveDayId))
            val loaded = store.loadDayRecords(archiveDayId)
            assertEquals(TrajectoryRecord.decodeStream(archiveRawBytes), loaded)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun recoversFromFactoryResetSeqRollbackWithoutFalseResetWhenUpToDate() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_rollback").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val liveBytes = loadVector("trajectory_basic.bin")
            val requestedCursors = mutableListOf<Long>()

            // Case 1: Phone has stale watermark=500 after device factory reset (device lastSeqId=102)
            val resetChannel = object : SyncChannel {
                private var listener: ((DataFrame) -> Unit)? = null
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
                    this.listener = listener
                }

                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN -> {
                            // Mimics firmware cmd_sync_begin when from_seq (500) > last_seq (102):
                            // totalRecords=0, firstSeq=500, lastSeq=102.
                            val body = ByteArray(18)
                            putU32(body, 0, 11L)
                            putU32(body, 4, 0L)
                            putU32(body, 8, 500L)
                            putU32(body, 12, 102L)
                            putU16(body, 16, 7)
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_READ -> {
                            requestedCursors.add(getU32(payload, 4))
                            listener?.invoke(
                                DataFrame(
                                    stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                    flags = 0x01,
                                    transferId = 11L,
                                    cursor = 100L,
                                    payload = liveBytes,
                                ),
                            )
                            val body = ByteArray(7)
                            putU32(body, 0, 103L)
                            putU16(body, 4, 1)
                            body[6] = 1
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptyDayIndexBody())
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val result = SyncEngine(store, resetChannel).syncAll(fromSeqId = 500L, tzOffsetMinutes = 480)
            assertNull(result.error)
            assertEquals(listOf(1L), requestedCursors)
            assertEquals(3, result.liveRecordsSynced)
            assertEquals(103L, result.newWatermarkSeqId)

            // Case 2: Already up-to-date (fromSeqId = lastSeqId + 1 = 103) must NOT trigger rollback
            var readCalledWhenUpToDate = false
            val upToDateChannel = object : SyncChannel {
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) = Unit
                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN -> {
                            val body = ByteArray(18)
                            putU32(body, 0, 12L)
                            putU32(body, 4, 0L)
                            putU32(body, 8, 103L)
                            putU32(body, 12, 102L)
                            putU16(body, 16, 7)
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_READ -> {
                            readCalledWhenUpToDate = true
                            SyncCommandReply(GeneratedProtocol.StatusCode.INTERNAL_ERROR, ByteArray(0))
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptyDayIndexBody())
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val noopResult = SyncEngine(store, upToDateChannel).syncAll(fromSeqId = 103L, tzOffsetMinutes = 480)
            assertNull(noopResult.error)
            assertFalse(readCalledWhenUpToDate)
            assertEquals(103L, noopResult.newWatermarkSeqId)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun retriesDroppedLiveChunkWithoutAdvancingWatermarkPastGap() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_live_drop").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val recs = TrajectoryRecord.decodeStream(loadVector("trajectory_basic.bin"))
            assertEquals(3, recs.size)
            val dayId = LocalDayStore.dayIdFromEpochMs(recs.first().unixEpochMs)
            val requestedCursors = mutableListOf<Long>()
            var callCount = 0

            val dropOnceChannel = object : SyncChannel {
                private var listener: ((DataFrame) -> Unit)? = null
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
                    this.listener = listener
                }

                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN -> {
                            val body = ByteArray(18)
                            putU32(body, 0, 21L)
                            putU32(body, 4, 3L)
                            putU32(body, 8, 100L)
                            putU32(body, 12, 102L)
                            putU16(body, 16, 1)
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_READ -> {
                            val cursor = getU32(payload, 4)
                            requestedCursors.add(cursor)
                            callCount++
                            if (callCount == 1) {
                                // Claim 3 chunks sent (100, 101, 102), but drop middle chunk 101!
                                listener?.invoke(
                                    DataFrame(
                                        stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                        flags = 0,
                                        transferId = 21L,
                                        cursor = 100L,
                                        payload = recs[0].encode(),
                                    ),
                                )
                                listener?.invoke(
                                    DataFrame(
                                        stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                        flags = 0x01,
                                        transferId = 21L,
                                        cursor = 102L,
                                        payload = recs[2].encode(),
                                    ),
                                )
                                val body = ByteArray(7)
                                putU32(body, 0, 103L)
                                putU16(body, 4, 3)
                                body[6] = 1
                                SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                            } else {
                                // Retry from cursor=101 delivers 101 and 102 intact.
                                listener?.invoke(
                                    DataFrame(
                                        stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                        flags = 0,
                                        transferId = 21L,
                                        cursor = 101L,
                                        payload = recs[1].encode(),
                                    ),
                                )
                                listener?.invoke(
                                    DataFrame(
                                        stream = GeneratedProtocol.StreamId.TRAJECTORY,
                                        flags = 0x01,
                                        transferId = 21L,
                                        cursor = 102L,
                                        payload = recs[2].encode(),
                                    ),
                                )
                                val body = ByteArray(7)
                                putU32(body, 0, 103L)
                                putU16(body, 4, 2)
                                body[6] = 1
                                SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                            }
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptyDayIndexBody())
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val result = SyncEngine(store, dropOnceChannel).syncAll(fromSeqId = 0L, tzOffsetMinutes = 480)
            assertNull(result.error)
            assertEquals(listOf(100L, 101L), requestedCursors)
            assertEquals(103L, result.newWatermarkSeqId)
            assertEquals(listOf(100L, 101L, 102L), store.loadDayRecords(dayId).map { it.seqId })

            // Persistent loss must abort with error and never skip watermark past missing chunk
            val alwaysDropChannel = object : SyncChannel {
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) = Unit
                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN -> {
                            val body = ByteArray(18)
                            putU32(body, 0, 22L)
                            putU32(body, 4, 3L)
                            putU32(body, 8, 100L)
                            putU32(body, 12, 102L)
                            putU16(body, 16, 3)
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_READ -> {
                            val body = ByteArray(7)
                            putU32(body, 0, 103L)
                            putU16(body, 4, 1)
                            body[6] = 1
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val failed = SyncEngine(store, alwaysDropChannel).syncAll(fromSeqId = 0L, tzOffsetMinutes = 480)
            assertNotNull(failed.error)
            assertEquals(0L, failed.newWatermarkSeqId)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun downloadsArchivesNewestFirstAndRedownloadsWhenRemoteCrcChanges() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_newest_crc").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val hsPkg = loadVector("day_package_hs.bin")
            val basicRaw = loadVector("trajectory_basic.bin")
            val staleRawPkg = buildRawLgdPackage(dayId = 20261125, rawRecords = basicRaw)

            // Pre-populate 20261124 with exact matching archive (should be skipped)
            // and 20261125 with an older/smaller archive (different CRC + recordCount -> must re-download).
            store.commitArchivedPackage(20261124, hsPkg)
            store.commitArchivedPackage(20261125, staleRawPkg)

            val downloadOrder = mutableListOf<Int>()
            val channel = object : SyncChannel {
                private var listener: ((DataFrame) -> Unit)? = null
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
                    this.listener = listener
                }

                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptySyncBeginBody(30L))
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX -> {
                            val out = ByteArrayOutputStream()
                            val hdr = ByteArray(8)
                            putU32(hdr, 0, 2L)
                            hdr[4] = 3
                            hdr[5] = 3
                            hdr[6] = 0
                            hdr[7] = 0
                            out.write(hdr)
                            // Intentionally unsorted/oldest-first from remote
                            out.write(encodeJournalEntry(20261124, hsPkg.size.toLong(), 8L, 0xCC798479L))
                            out.write(encodeJournalEntry(20261126, hsPkg.size.toLong(), 8L, 0xCC798479L))
                            out.write(encodeJournalEntry(20261125, hsPkg.size.toLong(), 8L, 0xCC798479L))
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, out.toByteArray())
                        }
                        GeneratedProtocol.CommandOpcode.GET_ARCHIVE -> {
                            val dayId = getU32(payload, 0).toInt()
                            downloadOrder.add(dayId)
                            listener?.invoke(
                                DataFrame(
                                    stream = GeneratedProtocol.StreamId.ARCHIVE_DAY,
                                    flags = 0x01,
                                    transferId = dayId.toLong(),
                                    cursor = 0L,
                                    payload = hsPkg,
                                ),
                            )
                            val body = ByteArray(11)
                            putU32(body, 0, hsPkg.size.toLong())
                            putU32(body, 4, hsPkg.size.toLong())
                            putU16(body, 8, 1)
                            body[10] = 1
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                        }
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val result = SyncEngine(store, channel).syncAll(fromSeqId = 0L, tzOffsetMinutes = 480)
            assertNull(result.error)
            assertEquals(listOf(20261126, 20261125), downloadOrder)
            assertEquals(2, result.archivedDaysDownloaded)
            assertEquals(0xCC798479L, store.readArchivedDayCrc32(20261125))
            assertEquals(8, store.loadDayRecords(20261125).size)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun retriesIncompleteArchiveChunksAndReportsCorruptDayFailure() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_arch_retry").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val hsPkg = loadVector("day_package_hs.bin")
            val splitAt = 100
            val corruptPkg = hsPkg.copyOf().also { it[it.size - 10] = (it[it.size - 10].toInt() xor 0xFF).toByte() }
            val goodOffsets = mutableListOf<Long>()
            var goodCall = 0

            val channel = object : SyncChannel {
                private var listener: ((DataFrame) -> Unit)? = null
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
                    this.listener = listener
                }

                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptySyncBeginBody(40L))
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX -> {
                            val out = ByteArrayOutputStream()
                            val hdr = ByteArray(8)
                            putU32(hdr, 0, 3L)
                            hdr[4] = 2
                            hdr[5] = 2
                            hdr[6] = 0
                            hdr[7] = 0
                            out.write(hdr)
                            out.write(encodeJournalEntry(20261126, hsPkg.size.toLong(), 8L, 0xCC798479L))
                            out.write(encodeJournalEntry(20261125, corruptPkg.size.toLong(), 8L, 0xCC798479L))
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, out.toByteArray())
                        }
                        GeneratedProtocol.CommandOpcode.GET_ARCHIVE -> {
                            val dayId = getU32(payload, 0).toInt()
                            val off = getU32(payload, 4)
                            if (dayId == 20261126) {
                                goodOffsets.add(off)
                                goodCall++
                                if (goodCall == 1) {
                                    // Drop second chunk while claiming nextOffset = full length and done = true
                                    listener?.invoke(
                                        DataFrame(
                                            stream = GeneratedProtocol.StreamId.ARCHIVE_DAY,
                                            flags = 0,
                                            transferId = dayId.toLong(),
                                            cursor = 0L,
                                            payload = hsPkg.copyOfRange(0, splitAt),
                                        ),
                                    )
                                    val body = ByteArray(11)
                                    putU32(body, 0, hsPkg.size.toLong())
                                    putU32(body, 4, hsPkg.size.toLong())
                                    putU16(body, 8, 2)
                                    body[10] = 1
                                    SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                                } else {
                                    listener?.invoke(
                                        DataFrame(
                                            stream = GeneratedProtocol.StreamId.ARCHIVE_DAY,
                                            flags = 0x01,
                                            transferId = dayId.toLong(),
                                            cursor = off,
                                            payload = hsPkg.copyOfRange(off.toInt(), hsPkg.size),
                                        ),
                                    )
                                    val body = ByteArray(11)
                                    putU32(body, 0, hsPkg.size.toLong())
                                    putU32(body, 4, hsPkg.size.toLong())
                                    putU16(body, 8, 1)
                                    body[10] = 1
                                    SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                                }
                            } else {
                                listener?.invoke(
                                    DataFrame(
                                        stream = GeneratedProtocol.StreamId.ARCHIVE_DAY,
                                        flags = 0x01,
                                        transferId = dayId.toLong(),
                                        cursor = 0L,
                                        payload = corruptPkg,
                                    ),
                                )
                                val body = ByteArray(11)
                                putU32(body, 0, corruptPkg.size.toLong())
                                putU32(body, 4, corruptPkg.size.toLong())
                                putU16(body, 8, 1)
                                body[10] = 1
                                SyncCommandReply(GeneratedProtocol.StatusCode.OK, body)
                            }
                        }
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.NOT_SUPPORTED, ByteArray(0))
                    }
                }
            }

            val result = SyncEngine(store, channel).syncAll(fromSeqId = 0L, tzOffsetMinutes = 480)
            assertEquals(listOf(0L, splitAt.toLong()), goodOffsets)
            assertEquals(1, result.archivedDaysDownloaded)
            assertTrue(store.hasArchivedDay(20261126))
            assertFalse(store.hasArchivedDay(20261125))
            assertNotNull(result.error)
            assertEquals(listOf(20261125), result.failedDays)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun lowBatteryGuardAbortsUnlessCharging() = runBlocking {
        val tmpDir = Files.createTempDirectory("lgps_low_bat").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            var commandsSent = 0
            val channel = object : SyncChannel {
                override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) = Unit
                override suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long): SyncCommandReply {
                    commandsSent++
                    return when (opcode) {
                        GeneratedProtocol.CommandOpcode.SYNC_BEGIN ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptySyncBeginBody(50L))
                        GeneratedProtocol.CommandOpcode.SYNC_END ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                        GeneratedProtocol.CommandOpcode.GET_DAY_INDEX ->
                            SyncCommandReply(GeneratedProtocol.StatusCode.OK, emptyDayIndexBody())
                        else -> SyncCommandReply(GeneratedProtocol.StatusCode.OK, ByteArray(0))
                    }
                }
            }

            val engine = SyncEngine(store, channel)
            val blocked = engine.syncAll(fromSeqId = 10L, deviceBatteryPct = 8, deviceCharging = false)
            assertNotNull(blocked.error)
            assertEquals(0, commandsSent)
            assertEquals(10L, blocked.newWatermarkSeqId)

            val allowedWhileCharging = engine.syncAll(fromSeqId = 10L, deviceBatteryPct = 8, deviceCharging = true)
            assertNull(allowedWhileCharging.error)
            assertTrue(commandsSent > 0)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun syncProtocolEncodesAndDecodesDayIndexBeyond255Days() {
        assertArrayEquals(byteArrayOf(0x00), SyncProtocol.encodeGetDayIndexReq(0))
        assertArrayEquals(byteArrayOf(0xFF.toByte()), SyncProtocol.encodeGetDayIndexReq(255))
        assertArrayEquals(byteArrayOf(0x00, 0x01), SyncProtocol.encodeGetDayIndexReq(256))
        assertArrayEquals(byteArrayOf(0x2C, 0x01), SyncProtocol.encodeGetDayIndexReq(300))

        // Legacy ext=0 page
        val legacyBody = ByteArray(8)
        putU32(legacyBody, 0, 5L)
        legacyBody[4] = 200.toByte()
        legacyBody[5] = 0
        legacyBody[6] = 16
        legacyBody[7] = 0
        val legacy = SyncProtocol.decodeDayIndexPage(legacyBody)
        assertNotNull(legacy)
        assertEquals(200, legacy!!.totalDays)
        assertEquals(16, legacy.startIdx)

        // Extended >255 days page: total = 300 (0x12C), start = 264 (0x108) -> ext = 0x11
        val extBody = ByteArray(8)
        putU32(extBody, 0, 9L)
        extBody[4] = 0x2C
        extBody[5] = 0
        extBody[6] = 0x08
        extBody[7] = 0x11
        val extended = SyncProtocol.decodeDayIndexPage(extBody)
        assertNotNull(extended)
        assertEquals(300, extended!!.totalDays)
        assertEquals(264, extended.startIdx)
    }

    private fun emptySyncBeginBody(transferId: Long): ByteArray = ByteArray(18).also {
        putU32(it, 0, transferId)
        putU32(it, 4, 0L)
        putU32(it, 8, 0L)
        putU32(it, 12, 0L)
        putU16(it, 16, 7)
    }

    private fun emptyDayIndexBody(): ByteArray = ByteArray(8).also {
        putU32(it, 0, 1L)
    }

    private fun encodeJournalEntry(
        dayId: Int,
        packageLen: Long,
        recordCount: Long,
        trajCrc32: Long,
    ): ByteArray = ByteArray(SyncProtocol.JOURNAL_ENTRY_SIZE).also { entry ->
        putU32(entry, 0, dayId.toLong())
        putU32(entry, 4, 0x16000L)
        putU32(entry, 8, packageLen)
        putU32(entry, 12, recordCount)
        putU32(entry, 16, trajCrc32)
        putU32(entry, 20, 200L)
        putU16(entry, 24, GeneratedProtocol.DayPackage.CODEC_BYTE_DELTA_HEATSHRINK)
        putU16(entry, 26, 0)
    }

    private fun buildRawLgdPackage(dayId: Int, rawRecords: ByteArray): ByteArray {
        val headerSize = GeneratedProtocol.DayPackage.HEADER_SIZE
        val footerSize = GeneratedProtocol.DayPackage.FOOTER_SIZE
        val out = ByteArray(headerSize + rawRecords.size + footerSize)
        out[0] = 'L'.code.toByte()
        out[1] = 'G'.code.toByte()
        out[2] = 'D'.code.toByte()
        out[3] = '1'.code.toByte()
        putU16(out, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_VER, GeneratedProtocol.DayPackage.HEADER_VER)
        putU16(out, GeneratedProtocol.DayPackage.HEADER_OFF_CODEC, GeneratedProtocol.DayPackage.CODEC_RAW)
        putU32(out, GeneratedProtocol.DayPackage.HEADER_OFF_DAY_ID, dayId.toLong())
        putU32(
            out,
            GeneratedProtocol.DayPackage.HEADER_OFF_RECORD_COUNT,
            (rawRecords.size / GeneratedProtocol.TrajectoryRecord.SIZE).toLong(),
        )
        putU32(out, GeneratedProtocol.DayPackage.HEADER_OFF_TRAJ_RAW_LEN, rawRecords.size.toLong())
        putU32(out, GeneratedProtocol.DayPackage.HEADER_OFF_TRAJ_PACKED_LEN, rawRecords.size.toLong())
        val hdrCrc = ProtocolCrc.crc16CcittFalse(out, 0, GeneratedProtocol.DayPackage.HEADER_CRC16_LEN)
        putU16(out, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_CRC16, hdrCrc)
        rawRecords.copyInto(out, headerSize)
        val crc32 = ProtocolCrc.crc32IsoHdlc(rawRecords)
        putU32(out, headerSize + rawRecords.size, crc32)
        return out
    }

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
