// SPDX-License-Identifier: MIT
package com.lifegps.app.data

import android.content.Context
import com.lifegps.app.ble.BleDebugRepository
import com.lifegps.app.ble.SyncForegroundService
import com.lifegps.core.data.ConfigTlvCodec
import com.lifegps.core.data.DeviceConfig
import com.lifegps.core.data.ExportFormat
import com.lifegps.core.data.LocalDayStore
import com.lifegps.core.data.PoiExportMeta
import com.lifegps.core.data.RemoteDayIndexEntry
import com.lifegps.core.data.SyncChannel
import com.lifegps.core.data.SyncCommandReply
import com.lifegps.core.data.SyncEngine
import com.lifegps.core.data.SyncProtocol
import com.lifegps.core.data.TrackExporter
import com.lifegps.core.model.DayTrackSummary
import com.lifegps.core.model.DeviceOperatingMode
import com.lifegps.core.model.PoiRecord
import com.lifegps.core.model.SyncProgress
import com.lifegps.core.protocol.DataFrame
import com.lifegps.core.protocol.GeneratedProtocol
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/**
 * Product-tier offline-first repository uniting [LocalDayStore] (`.lgd`/`.lgs`),
 * [TrackIndexDb] (SQLite WAL), [SyncEngine], Config TLV, and WGS-84 export.
 */
class ProductTrackRepository(
    context: Context,
    private val bleRepo: BleDebugRepository,
) {
    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    val dayStore = LocalDayStore(File(appContext.filesDir, "lifegps_store"))
    val indexDb = TrackIndexDb(appContext)

    private val syncChannel = object : SyncChannel {
        override suspend fun sendCommand(
            opcode: Int,
            payload: ByteArray,
            timeoutMs: Long,
        ): SyncCommandReply? = bleRepo.sendProductCommand(opcode, payload, timeoutMs)

        override fun setDataFrameListener(listener: ((DataFrame) -> Unit)?) {
            bleRepo.setProductDataListener(listener)
        }
    }

    private val syncEngine = SyncEngine(dayStore, syncChannel)
    val syncProgress: StateFlow<SyncProgress> = syncEngine.progress

    private val _days = MutableStateFlow<List<DayTrackSummary>>(emptyList())
    val days: StateFlow<List<DayTrackSummary>> = _days.asStateFlow()

    private val _pois = MutableStateFlow<List<PoiRecord>>(emptyList())
    val pois: StateFlow<List<PoiRecord>> = _pois.asStateFlow()

    private val _deviceConfig = MutableStateFlow(DeviceConfig())
    val deviceConfig: StateFlow<DeviceConfig> = _deviceConfig.asStateFlow()

    private val _remoteDays = MutableStateFlow<List<RemoteDayIndexEntry>>(emptyList())
    val remoteDays: StateFlow<List<RemoteDayIndexEntry>> = _remoteDays.asStateFlow()

    private val _statusMessage = MutableStateFlow<String?>(null)
    val statusMessage: StateFlow<String?> = _statusMessage.asStateFlow()

    init {
        scope.launch { reloadLocalIndex() }
    }

    suspend fun reloadLocalIndex() = withContext(Dispatchers.IO) {
        val diskDays = dayStore.listDays()
        for (summary in diskDays) {
            indexDb.upsertDaySummary(summary)
        }
        _days.value = indexDb.listDaySummaries()
        _pois.value = indexDb.listPois()
    }

    fun loadDayRecords(dayId: Int): List<TrajectoryRecord> = dayStore.loadDayRecords(dayId)

    fun triggerSync() {
        if (syncProgress.value.running) return
        scope.launch {
            SyncForegroundService.start(appContext, "正在连接并同步 Life_GPS 轨迹…")
            val progressJob = launch {
                var lastText: String? = null
                var lastPct = Int.MIN_VALUE
                syncEngine.progress.collect { p ->
                    if (!p.running) return@collect
                    val pct = if (p.totalBytes > 0L) {
                        ((p.receivedBytes * 100L) / p.totalBytes).toInt().coerceIn(0, 100)
                    } else {
                        -1
                    }
                    val text = p.stage.ifBlank { "正在同步 Life_GPS 轨迹…" }
                    if (text != lastText || pct != lastPct) {
                        lastText = text
                        lastPct = pct
                        SyncForegroundService.update(appContext, text, pct)
                    }
                }
            }
            try {
                val tzMin = _deviceConfig.value.tzOffsetMin
                val watermark = indexDb.getLastSyncedSeqId()
                val dev = bleRepo.device.value
                val batteryPct = if (dev.batteryMv > 0) dev.batteryPct else null
                val result = syncEngine.syncAll(
                    fromSeqId = watermark,
                    tzOffsetMinutes = tzMin,
                    deviceBatteryPct = batteryPct,
                    deviceCharging = dev.charging,
                )
                if (result.newWatermarkSeqId != watermark &&
                    (result.error == null || result.newWatermarkSeqId > watermark)
                ) {
                    indexDb.setLastSyncedSeqId(result.newWatermarkSeqId)
                }
                for (dayId in result.touchedDayIds) {
                    dayStore.summarizeDay(dayId)?.let { indexDb.upsertDaySummary(it) }
                    val recs = dayStore.loadDayRecords(dayId)
                    for (r in recs) {
                        if (r.isPoi) {
                            indexDb.upsertPoi(
                                PoiRecord(
                                    id = 0L,
                                    dayId = dayId,
                                    seqId = r.seqId,
                                    timestampMs = r.unixEpochMs,
                                    latitude = r.latitudeDeg,
                                    longitude = r.longitudeDeg,
                                    altitudeM = r.altitudeM,
                                    floorLevel = r.floorLevel,
                                    title = "POI #${r.seqId}",
                                ),
                            )
                        }
                    }
                }
                _days.value = indexDb.listDaySummaries()
                _pois.value = indexDb.listPois()
                if (result.error == null || result.touchedDayIds.isNotEmpty() || result.failedDays.isNotEmpty()) {
                    _remoteDays.value = syncEngine.fetchRemoteDayIndex()
                }
                _statusMessage.value = result.error
                    ?: "同步完成：${result.liveRecordsSynced} 条记录，${result.archivedDaysDownloaded} 个日包"
            } finally {
                progressJob.cancel()
                SyncForegroundService.stop(appContext)
            }
        }
    }

    fun refreshDeviceConfig() {
        scope.launch {
            val reply = bleRepo.sendProductCommand(
                GeneratedProtocol.CommandOpcode.GET_CONFIG,
                ByteArray(0),
            )
            if (reply != null && reply.status == GeneratedProtocol.StatusCode.OK) {
                _deviceConfig.value = ConfigTlvCodec.decode(reply.body, _deviceConfig.value)
                _statusMessage.value = "已读取设备配置"
            }
        }
    }

    fun applyDeviceConfig(updated: DeviceConfig) {
        scope.launch {
            val diff = ConfigTlvCodec.encodeDiff(_deviceConfig.value, updated)
            val payload = if (diff.isNotEmpty()) diff else ConfigTlvCodec.encodeAll(updated)
            val reply = bleRepo.sendProductCommand(
                GeneratedProtocol.CommandOpcode.SET_CONFIG,
                payload,
            )
            if (reply != null && reply.status == GeneratedProtocol.StatusCode.OK) {
                _deviceConfig.value = updated
                _statusMessage.value = "设备配置已保存"
            } else {
                _statusMessage.value = "保存配置失败 (status=${reply?.status ?: "timeout"})"
            }
        }
    }

    fun setOperatingMode(mode: DeviceOperatingMode) {
        scope.launch {
            val reply = bleRepo.sendProductCommand(
                GeneratedProtocol.CommandOpcode.SET_MODE,
                byteArrayOf(mode.wireCode.toByte()),
            )
            if (reply != null && reply.status == GeneratedProtocol.StatusCode.OK) {
                _deviceConfig.value = _deviceConfig.value.copy(recordMode = mode.wireCode)
                _statusMessage.value = "已切换为 ${mode.label}"
            } else {
                _statusMessage.value = "切换模式失败 (status=${reply?.status ?: "timeout"})"
            }
        }
    }

    fun eraseRemoteDay(dayId: Int) {
        scope.launch {
            val reply = bleRepo.sendProductCommand(
                GeneratedProtocol.CommandOpcode.ERASE_DAY,
                SyncProtocol.encodeEraseDayReq(dayId),
            )
            if (reply != null && reply.status == GeneratedProtocol.StatusCode.OK) {
                _remoteDays.value = syncEngine.fetchRemoteDayIndex()
                _statusMessage.value = "已清除设备端日包 $dayId"
            } else {
                _statusMessage.value = "清除设备日包失败 (status=${reply?.status ?: "timeout"})"
            }
        }
    }

    fun factoryResetDevice() {
        scope.launch {
            val confirm = byteArrayOf('R'.code.toByte(), 'S'.code.toByte(), 'T'.code.toByte(), '!'.code.toByte())
            val reply = bleRepo.sendProductCommand(
                GeneratedProtocol.CommandOpcode.FACTORY_RESET,
                confirm,
                timeoutMs = 8_000L,
            )
            if (reply != null && reply.status == GeneratedProtocol.StatusCode.OK) {
                indexDb.setLastSyncedSeqId(0L)
                _remoteDays.value = emptyList()
                bleRepo.clearRememberedDevice()
                _statusMessage.value = "设备已执行恢复出厂设置并重启，请在系统蓝牙中忽略旧配对后重新连接"
            } else {
                _statusMessage.value = "恢复出厂设置失败 (status=${reply?.status ?: "timeout"})"
            }
        }
    }

    fun deleteLocalDay(dayId: Int) {
        scope.launch {
            dayStore.deleteDay(dayId)
            indexDb.deleteDaySummary(dayId)
            _days.value = indexDb.listDaySummaries()
            _pois.value = indexDb.listPois()
            _statusMessage.value = "已删除本机轨迹 $dayId"
        }
    }

    fun updatePoi(id: Long, title: String, note: String) {
        scope.launch {
            indexDb.updatePoiText(id, title.trim(), note.trim())
            _pois.value = indexDb.listPois()
        }
    }

    fun deletePoi(id: Long) {
        scope.launch {
            indexDb.deletePoi(id)
            _pois.value = indexDb.listPois()
        }
    }

    suspend fun exportDayToCache(dayId: Int, format: ExportFormat): File? = withContext(Dispatchers.IO) {
        val records = dayStore.loadDayRecords(dayId)
        if (records.isEmpty()) return@withContext null
        val poiMetaBySeq = indexDb.listPois(dayId).associate { poi ->
            poi.seqId to PoiExportMeta(title = poi.title, note = poi.note)
        }
        val exportDir = File(appContext.cacheDir, "exports").apply { mkdirs() }
        val file = File(exportDir, "lifegps_${dayId}.${format.extension}")
        file.bufferedWriter(Charsets.UTF_8).use { writer ->
            TrackExporter.export(format, "Life_GPS $dayId", records, writer, poiMetaBySeq)
        }
        file
    }

    /**
     * Exports all local `.lgd`/`.lgs` day files (under `days/<filename>`) and POI annotations
     * (`pois.json`) into a standard ZIP archive. Returns the count of distinct days backed up.
     */
    suspend fun exportBackupZip(outputStream: OutputStream): Result<Int> = withContext(Dispatchers.IO) {
        try {
            val rawFiles = dayStore.listRawDayFiles()
            val allPois = indexDb.listAllPois()
            val distinctDayIds = LinkedHashSet<Int>()

            ZipOutputStream(BufferedOutputStream(outputStream)).use { zos ->
                for (file in rawFiles) {
                    file.name.substringBefore('.').toIntOrNull()?.let { distinctDayIds.add(it) }
                    zos.putNextEntry(ZipEntry("days/${file.name}"))
                    file.inputStream().use { input -> input.copyTo(zos) }
                    zos.closeEntry()
                }

                val poisArray = JSONArray()
                for (poi in allPois) {
                    poisArray.put(
                        JSONObject().apply {
                            put("dayId", poi.dayId)
                            put("seqId", poi.seqId)
                            put("timestampMs", poi.timestampMs)
                            put("latitude", poi.latitude)
                            put("longitude", poi.longitude)
                            put("altitudeM", poi.altitudeM)
                            put("floorLevel", poi.floorLevel)
                            put("title", poi.title)
                            put("note", poi.note)
                        },
                    )
                }
                zos.putNextEntry(ZipEntry(BACKUP_POIS_ENTRY))
                zos.write(poisArray.toString().toByteArray(Charsets.UTF_8))
                zos.closeEntry()
            }

            Result.success(distinctDayIds.size)
        } catch (ce: CancellationException) {
            throw ce
        } catch (t: Throwable) {
            Result.failure(t)
        }
    }

    /**
     * Restores local `.lgd`/`.lgs` day files and `pois.json` annotations from a ZIP backup,
     * rejecting path traversal / oversized entries, and refreshes the local SQLite index.
     * Returns the count of distinct days imported.
     */
    suspend fun importBackupZip(inputStream: InputStream): Result<Int> = withContext(Dispatchers.IO) {
        try {
            val importedDayIds = LinkedHashSet<Int>()
            var poisJsonBytes: ByteArray? = null
            var entryCount = 0

            ZipInputStream(BufferedInputStream(inputStream)).use { zis ->
                while (true) {
                    val entry = zis.nextEntry ?: break
                    entryCount++
                    if (entryCount > MAX_ZIP_ENTRIES) {
                        throw IllegalArgumentException("备份包条目数超过上限 ($MAX_ZIP_ENTRIES)")
                    }
                    val name = entry.name
                    val unsafePath = entry.isDirectory ||
                        name.startsWith("/") ||
                        name.startsWith("\\") ||
                        name.contains("..") ||
                        name.contains("\\")
                    if (!unsafePath) {
                        when {
                            name == BACKUP_POIS_ENTRY -> {
                                poisJsonBytes = readBoundedBytes(zis, MAX_ZIP_ENTRY_BYTES)
                            }
                            else -> {
                                val match = BACKUP_DAY_ENTRY_REGEX.matchEntire(name)
                                if (match != null) {
                                    val fileName = match.groupValues[1]
                                    val bytes = readBoundedBytes(zis, MAX_ZIP_ENTRY_BYTES)
                                    if (dayStore.importRawFile(fileName, bytes)) {
                                        fileName.substringBefore('.').toIntOrNull()?.let { importedDayIds.add(it) }
                                    }
                                }
                            }
                        }
                    }
                    zis.closeEntry()
                }
            }

            if (entryCount == 0) {
                throw IllegalArgumentException("无效或空的备份 ZIP 文件")
            }

            for (dayId in importedDayIds) {
                dayStore.summarizeDay(dayId)?.let { indexDb.upsertDaySummary(it) }
                for (r in dayStore.loadDayRecords(dayId)) {
                    if (r.isPoi) {
                        indexDb.upsertPoi(
                            PoiRecord(
                                id = 0L,
                                dayId = dayId,
                                seqId = r.seqId,
                                timestampMs = r.unixEpochMs,
                                latitude = r.latitudeDeg,
                                longitude = r.longitudeDeg,
                                altitudeM = r.altitudeM,
                                floorLevel = r.floorLevel,
                                title = "POI #${r.seqId}",
                            ),
                        )
                    }
                }
            }

            poisJsonBytes?.let { raw ->
                val arr = JSONArray(String(raw, Charsets.UTF_8))
                for (i in 0 until arr.length()) {
                    val obj = arr.optJSONObject(i) ?: continue
                    val dayId = obj.optInt("dayId", 0)
                    val seqId = obj.optLong("seqId", -1L)
                    if (dayId <= 0 || seqId < 0L) continue
                    val lat = obj.optDouble("latitude", 0.0).let { if (it.isFinite()) it else 0.0 }
                    val lon = obj.optDouble("longitude", 0.0).let { if (it.isFinite()) it else 0.0 }
                    val title = obj.optString("title", "").ifBlank { "POI #$seqId" }
                    val note = obj.optString("note", "")
                    indexDb.restorePoi(
                        PoiRecord(
                            id = 0L,
                            dayId = dayId,
                            seqId = seqId,
                            timestampMs = obj.optLong("timestampMs", 0L),
                            latitude = lat,
                            longitude = lon,
                            altitudeM = obj.optInt("altitudeM", 0),
                            floorLevel = obj.optInt("floorLevel", 0),
                            title = title,
                            note = note,
                        ),
                    )
                }
            }

            _days.value = indexDb.listDaySummaries()
            _pois.value = indexDb.listPois()
            Result.success(importedDayIds.size)
        } catch (ce: CancellationException) {
            throw ce
        } catch (t: Throwable) {
            Result.failure(t)
        }
    }

    private fun readBoundedBytes(input: InputStream, maxBytes: Int): ByteArray {
        val out = ByteArrayOutputStream()
        val buf = ByteArray(8192)
        var total = 0
        while (true) {
            val read = input.read(buf)
            if (read == -1) break
            total += read
            if (total > maxBytes) {
                throw IllegalArgumentException("备份条目超过大小上限 ($maxBytes B)")
            }
            out.write(buf, 0, read)
        }
        return out.toByteArray()
    }

    fun clearStatusMessage() {
        _statusMessage.value = null
    }

    fun close() {
        scope.cancel()
        indexDb.close()
    }

    private companion object {
        const val BACKUP_POIS_ENTRY = "pois.json"
        const val MAX_ZIP_ENTRY_BYTES = 32 * 1024 * 1024
        const val MAX_ZIP_ENTRIES = 4096
        val BACKUP_DAY_ENTRY_REGEX = Regex("^days/(\\d{8}\\.(lgd|lgs))$")
    }
}
