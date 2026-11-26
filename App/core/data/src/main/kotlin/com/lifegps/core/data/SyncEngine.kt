// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.model.SyncProgress
import com.lifegps.core.protocol.DataFrame
import com.lifegps.core.protocol.GeneratedProtocol
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.ByteArrayOutputStream
import java.util.concurrent.CopyOnWriteArrayList
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class SyncCommandReply(
    val status: Int,
    val body: ByteArray,
)

/**
 * Minimal command + data channel abstraction implemented by `BleDebugRepository` on Android
 * and by deterministic fakes in pure-JVM unit tests.
 */
interface SyncChannel {
    suspend fun sendCommand(opcode: Int, payload: ByteArray, timeoutMs: Long = 3500L): SyncCommandReply?
    fun setDataFrameListener(listener: ((DataFrame) -> Unit)?)
}

data class SyncResult(
    val liveRecordsSynced: Int,
    val archivedDaysDownloaded: Int,
    val newWatermarkSeqId: Long,
    val touchedDayIds: Set<Int>,
    val error: String? = null,
)

/**
 * Resumable dual-channel sync coordinator:
 * 1. Pulls unarchived live records from the device staging ring (`SYNC_BEGIN` -> `SYNC_READ` -> `SYNC_END`)
 *    and appends them into local `.lgs` files via [LocalDayStore].
 * 2. Fetches the remote journal index (`GET_DAY_INDEX`) and downloads any missing archived `LGD1`
 *    day packages (`GET_ARCHIVE` -> verify CRC-16/CRC-32 -> atomic `.lgd` commit).
 */
class SyncEngine(
    private val store: LocalDayStore,
    private val channel: SyncChannel,
) {
    private val _progress = MutableStateFlow(SyncProgress())
    val progress: StateFlow<SyncProgress> = _progress.asStateFlow()

    suspend fun syncAll(
        fromSeqId: Long,
        tzOffsetMinutes: Int = 480,
        chunksPerWindow: Int = 8,
    ): SyncResult {
        val incomingFrames = CopyOnWriteArrayList<DataFrame>()
        channel.setDataFrameListener { frame -> incomingFrames.add(frame) }
        val touchedDays = LinkedHashSet<Int>()
        var totalLiveRecords = 0
        var archivedCount = 0
        var watermark = fromSeqId

        try {
            _progress.value = SyncProgress(running = true, stage = "检查实时轨迹增量…")

            // --- Phase 1: Live staging ring sync (0x20 / 0x21 / 0x22) ---
            val beginReply = channel.sendCommand(
                GeneratedProtocol.CommandOpcode.SYNC_BEGIN,
                SyncProtocol.encodeSyncBeginReq(fromSeqId),
            )
            if (beginReply != null && beginReply.status == GeneratedProtocol.StatusCode.OK) {
                val begin = SyncProtocol.decodeSyncBeginRsp(beginReply.body)
                if (begin != null && begin.lastSeqId >= fromSeqId && begin.totalRecords > 0L) {
                    var cursor = if (fromSeqId == 0L) begin.firstSeqId else fromSeqId
                    val totalBytesEst = begin.totalRecords * GeneratedProtocol.TrajectoryRecord.SIZE
                    var rxBytes = 0L

                    var guard = 0
                    while (guard++ < 512) {
                        incomingFrames.clear()
                        _progress.value = SyncProgress(
                            running = true,
                            stage = "同步实时轨迹 ($totalLiveRecords/${begin.totalRecords})",
                            receivedBytes = rxBytes,
                            totalBytes = totalBytesEst,
                            syncedRecords = totalLiveRecords,
                        )

                        val readReply = channel.sendCommand(
                            GeneratedProtocol.CommandOpcode.SYNC_READ,
                            SyncProtocol.encodeSyncReadReq(begin.transferId, cursor, chunksPerWindow),
                        ) ?: break
                        if (readReply.status != GeneratedProtocol.StatusCode.OK) break
                        val readRsp = SyncProtocol.decodeSyncReadRsp(readReply.body) ?: break

                        val batch = ArrayList<TrajectoryRecord>()
                        for (df in incomingFrames) {
                            if (df.stream != GeneratedProtocol.StreamId.TRAJECTORY) continue
                            if (df.transferId != begin.transferId) continue
                            val stride = GeneratedProtocol.TrajectoryRecord.SIZE
                            val n = df.payload.size / stride
                            for (i in 0 until n) {
                                TrajectoryRecord.decode(df.payload, i * stride)?.let { batch.add(it) }
                            }
                            rxBytes += df.payload.size
                        }

                        if (batch.isNotEmpty()) {
                            touchedDays.addAll(store.appendLiveRecords(batch, tzOffsetMinutes))
                            totalLiveRecords += batch.size
                            watermark = maxOf(watermark, batch.maxOf { it.seqId } + 1L)
                        }

                        cursor = readRsp.nextSeqId
                        if (readRsp.done || readRsp.chunksSent == 0) break
                    }

                    channel.sendCommand(
                        GeneratedProtocol.CommandOpcode.SYNC_END,
                        SyncProtocol.encodeSyncEndReq(begin.transferId, watermark),
                    )
                }
            }

            // --- Phase 2: Archived LGD1 day packages (0x23 / 0x24) ---
            _progress.value = SyncProgress(
                running = true,
                stage = "查询设备历史日包索引…",
                syncedRecords = totalLiveRecords,
            )
            val remoteDays = fetchRemoteDayIndex()
            val missingDays = remoteDays.filter { !store.hasArchivedDay(it.dayId) }

            for ((idx, entry) in missingDays.withIndex()) {
                val pkgBuf = ByteArrayOutputStream(entry.packageLen.toInt().coerceAtLeast(256))
                var offset = 0L
                var guard = 0
                while (guard++ < 2048) {
                    incomingFrames.clear()
                    _progress.value = SyncProgress(
                        running = true,
                        stage = "下载日包 ${entry.dayId} (${idx + 1}/${missingDays.size})",
                        receivedBytes = offset,
                        totalBytes = entry.packageLen,
                        syncedRecords = totalLiveRecords,
                    )
                    val rsp = channel.sendCommand(
                        GeneratedProtocol.CommandOpcode.GET_ARCHIVE,
                        SyncProtocol.encodeGetArchiveReq(entry.dayId, offset, chunksPerWindow),
                    ) ?: break
                    if (rsp.status != GeneratedProtocol.StatusCode.OK) break
                    val archRsp = SyncProtocol.decodeGetArchiveRsp(rsp.body) ?: break

                    val ordered = incomingFrames
                        .filter {
                            it.stream == GeneratedProtocol.StreamId.ARCHIVE_DAY &&
                                it.transferId == (entry.dayId.toLong() and 0xFFFFFFFFL)
                        }
                        .sortedBy { it.cursor }

                    for (df in ordered) {
                        if (df.cursor == pkgBuf.size().toLong()) {
                            pkgBuf.write(df.payload)
                        }
                    }

                    offset = pkgBuf.size().toLong()
                    if (archRsp.done || archRsp.chunksSent == 0 || offset >= archRsp.totalLen) break
                }

                val fullBytes = pkgBuf.toByteArray()
                if (fullBytes.size.toLong() == entry.packageLen && fullBytes.isNotEmpty()) {
                    val decoded = store.commitArchivedPackage(entry.dayId, fullBytes)
                    archivedCount++
                    touchedDays.add(entry.dayId)
                    totalLiveRecords += decoded.records.size
                }
            }

            val nowMs = System.currentTimeMillis()
            _progress.value = SyncProgress(
                running = false,
                stage = "同步完成",
                syncedRecords = totalLiveRecords,
                lastSyncEpochMs = nowMs,
            )
            return SyncResult(
                liveRecordsSynced = totalLiveRecords,
                archivedDaysDownloaded = archivedCount,
                newWatermarkSeqId = watermark,
                touchedDayIds = touchedDays,
            )
        } catch (t: Throwable) {
            val msg = t.message ?: "同步异常"
            _progress.value = SyncProgress(
                running = false,
                stage = "同步失败",
                syncedRecords = totalLiveRecords,
                error = msg,
            )
            return SyncResult(
                liveRecordsSynced = totalLiveRecords,
                archivedDaysDownloaded = archivedCount,
                newWatermarkSeqId = watermark,
                touchedDayIds = touchedDays,
                error = msg,
            )
        } finally {
            channel.setDataFrameListener(null)
        }
    }

    suspend fun fetchRemoteDayIndex(): List<RemoteDayIndexEntry> {
        val all = ArrayList<RemoteDayIndexEntry>()
        var startIdx = 0
        var guard = 0
        while (guard++ < 32) {
            val reply = channel.sendCommand(
                GeneratedProtocol.CommandOpcode.GET_DAY_INDEX,
                SyncProtocol.encodeGetDayIndexReq(startIdx),
            ) ?: break
            if (reply.status != GeneratedProtocol.StatusCode.OK) break
            val page = SyncProtocol.decodeDayIndexPage(reply.body) ?: break
            all.addAll(page.entries)
            startIdx += page.returnedDays
            if (page.returnedDays == 0 || startIdx >= page.totalDays) break
        }
        return all
    }
}
