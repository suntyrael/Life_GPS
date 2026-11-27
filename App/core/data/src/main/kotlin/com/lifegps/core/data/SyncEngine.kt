// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.model.SyncProgress
import com.lifegps.core.protocol.DataFrame
import com.lifegps.core.protocol.DayPackageCodec
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
    val failedDays: List<Int> = emptyList(),
)

/**
 * Resumable dual-channel sync coordinator:
 * 1. Pulls unarchived live records from the device staging ring (`SYNC_BEGIN` -> `SYNC_READ` -> `SYNC_END`)
 *    and appends them into local `.lgs` files via [LocalDayStore], retrying dropped chunks without
 *    advancing the watermark past gaps and recovering automatically after device seq rollback.
 * 2. Fetches the remote journal index (`GET_DAY_INDEX`) and downloads missing or updated archived
 *    `LGD1` day packages newest-first (`GET_ARCHIVE` -> verify length + CRC-16/CRC-32 -> atomic `.lgd` commit).
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
        deviceBatteryPct: Int? = null,
        deviceCharging: Boolean = false,
    ): SyncResult {
        if (deviceBatteryPct != null && !deviceCharging && deviceBatteryPct < MIN_SYNC_BATTERY_PCT) {
            val msg = "设备电量过低（${deviceBatteryPct}%），请充电或保持电量 ≥${MIN_SYNC_BATTERY_PCT}% 后再同步"
            _progress.value = SyncProgress(
                running = false,
                stage = "电量过低，已取消同步",
                error = msg,
            )
            return SyncResult(
                liveRecordsSynced = 0,
                archivedDaysDownloaded = 0,
                newWatermarkSeqId = fromSeqId,
                touchedDayIds = emptySet(),
                error = msg,
            )
        }

        val incomingFrames = CopyOnWriteArrayList<DataFrame>()
        channel.setDataFrameListener { frame -> incomingFrames.add(frame) }
        val touchedDays = LinkedHashSet<Int>()
        val failedDays = ArrayList<Int>()
        var totalLiveRecords = 0
        var archivedCount = 0
        var watermark = fromSeqId

        try {
            _progress.value = SyncProgress(running = true, stage = "检查实时轨迹增量…")

            // --- Phase 1: Live staging ring sync (0x20 / 0x21 / 0x22) ---
            var liveError: String? = null
            val beginReply = channel.sendCommand(
                GeneratedProtocol.CommandOpcode.SYNC_BEGIN,
                SyncProtocol.encodeSyncBeginReq(fromSeqId),
            )
            if (beginReply != null && beginReply.status == GeneratedProtocol.StatusCode.OK) {
                val begin = SyncProtocol.decodeSyncBeginRsp(beginReply.body)
                if (begin != null) {
                    // Detect factory reset / sequence rollback:
                    // Normal up-to-date state has watermark == lastSeqId + 1, so rollback is
                    // strictly lastSeqId == 0 or lastSeqId + 1 < fromSeqId.
                    val seqRolledBack = fromSeqId > 0L &&
                        (begin.lastSeqId == 0L || begin.lastSeqId + 1L < fromSeqId)

                    val shouldReadLive = if (seqRolledBack) {
                        watermark = 0L
                        begin.lastSeqId > 0L
                    } else {
                        begin.lastSeqId >= fromSeqId && begin.totalRecords > 0L
                    }

                    if (shouldReadLive) {
                        var cursor = when {
                            seqRolledBack -> if (begin.firstSeqId in 1L..begin.lastSeqId) begin.firstSeqId else 1L
                            fromSeqId == 0L -> begin.firstSeqId
                            else -> fromSeqId
                        }
                        val effectiveTotalRecords = if (begin.totalRecords > 0L) {
                            begin.totalRecords
                        } else {
                            (begin.lastSeqId - cursor + 1L).coerceAtLeast(1L)
                        }
                        val totalBytesEst = effectiveTotalRecords * GeneratedProtocol.TrajectoryRecord.SIZE
                        var rxBytes = 0L
                        var liveLossRetries = 0
                        var guard = 0

                        while (guard++ < 512) {
                            incomingFrames.clear()
                            _progress.value = SyncProgress(
                                running = true,
                                stage = "同步实时轨迹 ($totalLiveRecords/$effectiveTotalRecords)",
                                receivedBytes = rxBytes,
                                totalBytes = totalBytesEst,
                                syncedRecords = totalLiveRecords,
                            )

                            val readReply = channel.sendCommand(
                                GeneratedProtocol.CommandOpcode.SYNC_READ,
                                SyncProtocol.encodeSyncReadReq(begin.transferId, cursor, chunksPerWindow),
                            )
                            if (readReply == null || readReply.status != GeneratedProtocol.StatusCode.OK) {
                                liveLossRetries++
                                if (liveLossRetries > MAX_CHUNK_RETRIES) {
                                    liveError = "实时轨迹同步失败 (status=${readReply?.status ?: "timeout"}, seq=$cursor)"
                                    break
                                }
                                continue
                            }
                            val readRsp = SyncProtocol.decodeSyncReadRsp(readReply.body)
                            if (readRsp == null) {
                                liveError = "实时轨迹响应格式无效"
                                break
                            }

                            if (readRsp.chunksSent == 0) {
                                if (readRsp.done) break
                                liveLossRetries++
                                if (liveLossRetries > MAX_CHUNK_RETRIES) {
                                    liveError = "实时轨迹同步停滞（seq=$cursor）"
                                    break
                                }
                                continue
                            }

                            val validFrames = incomingFrames
                                .filter {
                                    it.stream == GeneratedProtocol.StreamId.TRAJECTORY &&
                                        it.transferId == begin.transferId
                                }
                                .sortedBy { it.cursor }

                            val stride = GeneratedProtocol.TrajectoryRecord.SIZE
                            val decodedPerFrame = ArrayList<List<TrajectoryRecord>?>(validFrames.size)
                            val allBatch = ArrayList<TrajectoryRecord>()
                            var anyCorruptRecord = false

                            for (df in validFrames) {
                                if (df.payload.isEmpty() || df.payload.size % stride != 0) {
                                    anyCorruptRecord = true
                                    decodedPerFrame.add(null)
                                    continue
                                }
                                val n = df.payload.size / stride
                                val frameRecs = ArrayList<TrajectoryRecord>(n)
                                var frameOk = true
                                for (i in 0 until n) {
                                    val rec = TrajectoryRecord.decode(df.payload, i * stride)
                                    if (rec == null) {
                                        frameOk = false
                                        anyCorruptRecord = true
                                    } else {
                                        frameRecs.add(rec)
                                        allBatch.add(rec)
                                    }
                                }
                                rxBytes += df.payload.size
                                decodedPerFrame.add(if (frameOk) frameRecs else null)
                            }

                            val allChunksReceived = validFrames.size >= readRsp.chunksSent &&
                                !anyCorruptRecord &&
                                allBatch.isNotEmpty()

                            if (allChunksReceived) {
                                touchedDays.addAll(store.appendLiveRecords(allBatch, tzOffsetMinutes))
                                totalLiveRecords += allBatch.size
                                watermark = maxOf(watermark, allBatch.maxOf { it.seqId } + 1L)
                                liveLossRetries = 0
                                cursor = readRsp.nextSeqId
                                if (readRsp.done) break
                            } else {
                                // Only advance over the contiguous verified prefix starting at cursor;
                                // never skip missing chunks or advance watermark past gaps.
                                val verifiedPrefix = ArrayList<TrajectoryRecord>()
                                var expectedSeq = cursor
                                for (frameRecs in decodedPerFrame) {
                                    if (frameRecs == null || frameRecs.isEmpty()) break
                                    if (frameRecs.first().seqId != expectedSeq) break
                                    var frameContiguous = true
                                    for (rec in frameRecs) {
                                        if (rec.seqId != expectedSeq) {
                                            frameContiguous = false
                                            break
                                        }
                                        verifiedPrefix.add(rec)
                                        expectedSeq = rec.seqId + 1L
                                    }
                                    if (!frameContiguous) break
                                }
                                if (verifiedPrefix.isNotEmpty()) {
                                    touchedDays.addAll(store.appendLiveRecords(verifiedPrefix, tzOffsetMinutes))
                                    totalLiveRecords += verifiedPrefix.size
                                    val nextVerified = verifiedPrefix.last().seqId + 1L
                                    watermark = maxOf(watermark, nextVerified)
                                    cursor = nextVerified
                                }
                                liveLossRetries++
                                if (liveLossRetries > MAX_CHUNK_RETRIES) {
                                    liveError = "实时轨迹同步丢包，已重试 $MAX_CHUNK_RETRIES 次仍不完整（seq=$cursor）"
                                    break
                                }
                            }
                        }
                    }

                    channel.sendCommand(
                        GeneratedProtocol.CommandOpcode.SYNC_END,
                        SyncProtocol.encodeSyncEndReq(begin.transferId, watermark),
                    )
                }
            }

            if (liveError != null) {
                _progress.value = SyncProgress(
                    running = false,
                    stage = "实时同步失败",
                    syncedRecords = totalLiveRecords,
                    error = liveError,
                )
                return SyncResult(
                    liveRecordsSynced = totalLiveRecords,
                    archivedDaysDownloaded = archivedCount,
                    newWatermarkSeqId = watermark,
                    touchedDayIds = touchedDays,
                    error = liveError,
                )
            }

            // --- Phase 2: Archived LGD1 day packages (0x23 / 0x24) ---
            _progress.value = SyncProgress(
                running = true,
                stage = "查询设备历史日包索引…",
                syncedRecords = totalLiveRecords,
            )
            val remoteDays = fetchRemoteDayIndex()
            val daysToDownload = remoteDays
                .sortedByDescending { it.dayId }
                .filter { entry ->
                    !store.hasMatchingArchive(
                        dayId = entry.dayId,
                        expectedCrc32 = entry.trajCrc32,
                        expectedRecordCount = entry.recordCount,
                    )
                }

            var firstDayError: String? = null

            for ((idx, entry) in daysToDownload.withIndex()) {
                var committed = false
                var dayError: String? = null
                var pkgAttempt = 0

                while (pkgAttempt <= MAX_CHUNK_RETRIES && !committed) {
                    pkgAttempt++
                    val pkgBuf = ByteArrayOutputStream(entry.packageLen.toInt().coerceAtLeast(256))
                    var offset = 0L
                    var chunkRetries = 0
                    var guard = 0
                    var transferFailed: String? = null

                    while (guard++ < 2048) {
                        incomingFrames.clear()
                        _progress.value = SyncProgress(
                            running = true,
                            stage = "下载日包 ${entry.dayId} (${idx + 1}/${daysToDownload.size})",
                            receivedBytes = offset,
                            totalBytes = entry.packageLen,
                            syncedRecords = totalLiveRecords,
                        )
                        val rsp = channel.sendCommand(
                            GeneratedProtocol.CommandOpcode.GET_ARCHIVE,
                            SyncProtocol.encodeGetArchiveReq(entry.dayId, offset, chunksPerWindow),
                        )
                        if (rsp == null || rsp.status != GeneratedProtocol.StatusCode.OK) {
                            chunkRetries++
                            if (chunkRetries > MAX_CHUNK_RETRIES) {
                                transferFailed = "日包 ${entry.dayId} 请求失败 (status=${rsp?.status ?: "timeout"})"
                                break
                            }
                            continue
                        }
                        val archRsp = SyncProtocol.decodeGetArchiveRsp(rsp.body)
                        if (archRsp == null) {
                            transferFailed = "日包 ${entry.dayId} 响应格式无效"
                            break
                        }

                        val ordered = incomingFrames
                            .filter {
                                it.stream == GeneratedProtocol.StreamId.ARCHIVE_DAY &&
                                    it.transferId == (entry.dayId.toLong() and 0xFFFFFFFFL)
                            }
                            .sortedBy { it.cursor }

                        val prevSize = pkgBuf.size().toLong()
                        for (df in ordered) {
                            if (df.cursor == pkgBuf.size().toLong()) {
                                pkgBuf.write(df.payload)
                            }
                        }

                        val newSize = pkgBuf.size().toLong()
                        offset = newSize
                        val expectedTotal = if (archRsp.totalLen > 0L) archRsp.totalLen else entry.packageLen

                        if (expectedTotal > 0L && offset >= expectedTotal) {
                            break
                        }

                        val windowShort = (archRsp.chunksSent > 0 && ordered.size < archRsp.chunksSent) ||
                            (archRsp.nextOffset > newSize)

                        if (windowShort || newSize == prevSize) {
                            chunkRetries++
                            if (chunkRetries > MAX_CHUNK_RETRIES) {
                                transferFailed = "日包 ${entry.dayId} 传输不完整 ($offset/$expectedTotal B)"
                                break
                            }
                            continue
                        }

                        if (archRsp.done && expectedTotal > 0L && offset < expectedTotal) {
                            chunkRetries++
                            if (chunkRetries > MAX_CHUNK_RETRIES) {
                                transferFailed = "日包 ${entry.dayId} 提前结束 ($offset/$expectedTotal B)"
                                break
                            }
                            continue
                        }
                        chunkRetries = 0
                        if (archRsp.done || archRsp.chunksSent == 0) break
                    }

                    if (transferFailed != null) {
                        dayError = transferFailed
                        break
                    }

                    val fullBytes = pkgBuf.toByteArray()
                    if (fullBytes.isEmpty() || (entry.packageLen > 0L && fullBytes.size.toLong() != entry.packageLen)) {
                        dayError = "日包 ${entry.dayId} 长度不匹配 (${fullBytes.size}/${entry.packageLen} B)"
                        continue
                    }

                    val decoded = try {
                        DayPackageCodec.decode(fullBytes)
                    } catch (t: Throwable) {
                        dayError = "日包 ${entry.dayId} 校验失败: ${t.message}"
                        if (pkgAttempt > MAX_CHUNK_RETRIES) {
                            runCatching { store.commitArchivedPackage(entry.dayId, fullBytes) }
                        }
                        continue
                    }

                    if (entry.trajCrc32 != 0L && decoded.crc32 != entry.trajCrc32) {
                        dayError = "日包 ${entry.dayId} CRC32 不匹配 (local=0x${decoded.crc32.toString(16)} remote=0x${entry.trajCrc32.toString(16)})"
                        continue
                    }

                    store.commitArchivedPackage(entry.dayId, fullBytes)
                    archivedCount++
                    touchedDays.add(entry.dayId)
                    totalLiveRecords += decoded.records.size
                    committed = true
                }

                if (!committed) {
                    failedDays.add(entry.dayId)
                    if (firstDayError == null) {
                        firstDayError = dayError ?: "日包 ${entry.dayId} 同步失败"
                    }
                }
            }

            val nowMs = System.currentTimeMillis()
            if (firstDayError != null) {
                _progress.value = SyncProgress(
                    running = false,
                    stage = "同步部分失败",
                    syncedRecords = totalLiveRecords,
                    error = firstDayError,
                )
                return SyncResult(
                    liveRecordsSynced = totalLiveRecords,
                    archivedDaysDownloaded = archivedCount,
                    newWatermarkSeqId = watermark,
                    touchedDayIds = touchedDays,
                    error = firstDayError,
                    failedDays = failedDays,
                )
            }

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
                failedDays = failedDays,
            )
        } finally {
            channel.setDataFrameListener(null)
        }
    }

    suspend fun fetchRemoteDayIndex(): List<RemoteDayIndexEntry> {
        val all = ArrayList<RemoteDayIndexEntry>()
        var startIdx = 0
        var guard = 0
        while (guard++ < MAX_INDEX_PAGES) {
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

    private companion object {
        const val MAX_CHUNK_RETRIES = 3
        const val MAX_INDEX_PAGES = 512
        const val MIN_SYNC_BATTERY_PCT = 10
    }
}
