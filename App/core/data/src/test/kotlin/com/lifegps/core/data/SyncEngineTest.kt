// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.DataFrame
import com.lifegps.core.protocol.GeneratedProtocol
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
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
            val hsPkg = loadVector("day_package_hs.bin")
            val archiveDayId = 20260705

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
                            val entry = ByteArray(SyncProtocol.JOURNAL_ENTRY_SIZE)
                            putU32(entry, 0, archiveDayId.toLong())
                            putU32(entry, 4, 0x16000L)
                            putU32(entry, 8, hsPkg.size.toLong())
                            putU32(entry, 12, 3L)
                            putU32(entry, 16, 0L)
                            putU32(entry, 20, 100L)
                            putU16(entry, 24, GeneratedProtocol.DayPackage.CODEC_BYTE_DELTA_HEATSHRINK)
                            putU16(entry, 26, 0)
                            out.write(entry)
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
            assertEquals(TrajectoryRecord.decodeStream(liveBytes), loaded)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

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
