// SPDX-License-Identifier: MIT
package com.lifegps.app.prod

import android.content.Context
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.AppContainer
import com.lifegps.app.ble.LinkState
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatTile
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain
import com.lifegps.core.data.ExportFormat
import com.lifegps.core.data.RemoteDayIndexEntry
import com.lifegps.core.model.BondState
import com.lifegps.core.model.CoordTransform
import com.lifegps.core.model.DayTrackSummary
import com.lifegps.core.model.DeviceOperatingMode
import com.lifegps.core.model.LatLng
import com.lifegps.core.model.PoiRecord
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.File
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class ProdTab(val label: String, val badge: String) {
    HOME("首页", "设备"),
    MAP("地图", "轨迹"),
    TIMELINE("时间轴", "日历"),
    SETTINGS("设置", "配置"),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProdAppHost(
    container: AppContainer,
    topBarExtraAction: (@Composable () -> Unit)? = null,
    onOpenOta: (() -> Unit)? = null,
) {
    val appContext = LocalContext.current.applicationContext
    val repo = container.repo
    val device by repo.device.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val linkState by repo.linkState.collectAsStateWithLifecycle()

    var currentTab by rememberSaveable { mutableStateOf(ProdTab.HOME) }
    var selectedDayId by rememberSaveable { mutableIntStateOf(0) }
    var showDeviceScanner by rememberSaveable { mutableStateOf(false) }
    var showOtaScreen by rememberSaveable { mutableStateOf(false) }
    var tileProvider by remember { mutableStateOf(loadSavedTileProvider(appContext)) }
    val onSelectTileProvider: (MapTileProvider) -> Unit = { provider ->
        tileProvider = provider
        saveTileProvider(appContext, provider)
        container.setDisplayCoordSystem(provider.coordSystem)
    }

    LaunchedEffect(tileProvider) {
        container.setDisplayCoordSystem(tileProvider.coordSystem)
    }

    if (showDeviceScanner) {
        ProdDeviceScanScreen(
            container = container,
            onBack = { showDeviceScanner = false },
        )
        return
    }

    if (showOtaScreen) {
        OtaUpdateScreen(
            container = container,
            onBack = { showOtaScreen = false },
        )
        return
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text("Life_GPS", fontWeight = FontWeight.Bold) },
                actions = {
                    topBarExtraAction?.invoke()
                    Spacer(Modifier.width(4.dp))
                    StatusPill(
                        text = when {
                            device.connected && bondState == BondState.BONDED -> "已配对连接"
                            device.connected -> "已连接"
                            linkState is LinkState.CONNECTING -> "连接中"
                            else -> "未连接"
                        },
                        color = when {
                            device.connected -> Green
                            linkState is LinkState.CONNECTING -> Amber
                            else -> Red
                        },
                        filled = true,
                    )
                    Spacer(Modifier.width(8.dp))
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                    titleContentColor = TextMain,
                ),
            )
        },
        bottomBar = {
            NavigationBar(containerColor = MaterialTheme.colorScheme.surface) {
                ProdTab.entries.forEach { tab ->
                    NavigationBarItem(
                        selected = currentTab == tab,
                        onClick = { currentTab = tab },
                        icon = {
                            Text(
                                text = tab.badge,
                                fontSize = 11.sp,
                                fontWeight = if (currentTab == tab) FontWeight.Bold else FontWeight.Normal,
                            )
                        },
                        label = { Text(tab.label, fontSize = 12.sp) },
                    )
                }
            }
        },
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            when (currentTab) {
                ProdTab.HOME -> ProdHomeTab(
                    container = container,
                    onOpenScanner = { showDeviceScanner = true },
                    onSelectDay = { dayId ->
                        selectedDayId = dayId
                        currentTab = ProdTab.MAP
                    },
                )
                ProdTab.MAP -> ProdMapTab(
                    container = container,
                    selectedDayId = selectedDayId,
                    onSelectDay = { selectedDayId = it },
                    tileProvider = tileProvider,
                    onSelectTileProvider = onSelectTileProvider,
                )
                ProdTab.TIMELINE -> ProdTimelineTab(
                    container = container,
                    onOpenDayOnMap = { dayId ->
                        selectedDayId = dayId
                        currentTab = ProdTab.MAP
                    },
                )
                ProdTab.SETTINGS -> ProdSettingsTab(
                    container = container,
                    tileProvider = tileProvider,
                    onSelectTileProvider = onSelectTileProvider,
                    onOpenScanner = { showDeviceScanner = true },
                    onOpenOta = onOpenOta ?: { showOtaScreen = true },
                )
            }
        }
    }
}

@Composable
private fun ProdHomeTab(
    container: AppContainer,
    onOpenScanner: () -> Unit,
    onSelectDay: (Int) -> Unit,
) {
    val repo = container.repo
    val trackRepo = container.trackRepo
    val device by repo.device.collectAsStateWithLifecycle()
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val imu by repo.imu.collectAsStateWithLifecycle()
    val flash by repo.flash.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val syncProgress by trackRepo.syncProgress.collectAsStateWithLifecycle()
    val days by trackRepo.days.collectAsStateWithLifecycle()
    val pois by trackRepo.pois.collectAsStateWithLifecycle()
    val cfg by trackRepo.deviceConfig.collectAsStateWithLifecycle()
    val statusMsg by trackRepo.statusMessage.collectAsStateWithLifecycle()

    val batteryColor = when {
        device.batteryMv <= 0 -> TextDim
        device.batteryPct > 50 -> Green
        device.batteryPct > 20 -> Amber
        else -> Red
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                shape = RoundedCornerShape(16.dp),
            ) {
                Column(Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                text = device.name,
                                color = TextMain,
                                fontSize = 20.sp,
                                fontWeight = FontWeight.Bold,
                            )
                            Spacer(Modifier.height(2.dp))
                            Text(
                                text = "${device.firmware}  ·  ${bondState.label}",
                                color = TextDim,
                                fontSize = 12.sp,
                            )
                        }
                        StatusPill(
                            text = when {
                                device.charging -> "充电中"
                                device.batteryMv > 0 -> "${device.batteryPct}%"
                                else -> "—"
                            },
                            color = batteryColor,
                            filled = true,
                        )
                    }
                    Spacer(Modifier.height(12.dp))
                    HorizontalDivider(color = MaterialTheme.colorScheme.background)
                    Spacer(Modifier.height(10.dp))
                    val activeModeCode = if (device.workMode >= 0) device.workMode else cfg.recordMode
                    MetricRow("记录模式", DeviceOperatingMode.fromWire(activeModeCode).label, Cyan)
                    MetricRow("GNSS 状态", if (gnss.fix) "已定位 (${gnss.sats} 颗星)" else gnss.state)
                    val storageText = when {
                        flash.totalMb > 0f && device.workMode >= 0 -> String.format(
                            Locale.US,
                            "%.2f / %.0f MB (%.1f%% · %d 天归档)",
                            flash.usedMb,
                            flash.totalMb,
                            device.storageUsedPermille / 10.0,
                            device.archiveDaysCount,
                        )
                        flash.totalMb > 0f -> String.format(
                            Locale.US,
                            "%.2f / %.0f MB (%d 条)",
                            flash.usedMb,
                            flash.totalMb,
                            flash.recordCount,
                        )
                        device.workMode >= 0 -> String.format(
                            Locale.US,
                            "已用 %.1f%% · %d 天归档",
                            device.storageUsedPermille / 10.0,
                            device.archiveDaysCount,
                        )
                        else -> "—"
                    }
                    MetricRow("设备存储 (32/64M)", storageText)
                    Spacer(Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Button(
                            onClick = onOpenScanner,
                            modifier = Modifier.weight(1f),
                        ) { Text(if (device.connected) "切换设备" else "扫描连接") }
                        if (device.connected) {
                            OutlinedButton(
                                onClick = { repo.disconnect() },
                                modifier = Modifier.weight(1f),
                            ) { Text("断开连接") }
                        }
                    }
                }
            }
        }

        // One-tap Sync & Progress Card
        item {
            CardBlock {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text("BLE 轨迹同步", color = TextMain, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                        Text(
                            text = statusMsg ?: syncProgress.stage.ifBlank { "将设备实时环与归档日包同步至本机" },
                            color = if (syncProgress.error != null) Red else TextDim,
                            fontSize = 12.sp,
                        )
                    }
                    Button(
                        onClick = { trackRepo.triggerSync() },
                        enabled = device.connected && !syncProgress.running,
                    ) {
                        Text(if (syncProgress.running) "同步中…" else "同步轨迹")
                    }
                }
                if (syncProgress.running) {
                    Spacer(Modifier.height(10.dp))
                    if (syncProgress.totalBytes > 0L) {
                        LinearProgressIndicator(
                            progress = { (syncProgress.receivedBytes.toFloat() / syncProgress.totalBytes.toFloat()).coerceIn(0f, 1f) },
                            modifier = Modifier.fillMaxWidth(),
                        )
                    } else {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        }

        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatTile(
                    label = "今日步数",
                    value = if (device.connected) "${imu.steps}" else "--",
                    accent = Amber,
                    modifier = Modifier.weight(1f),
                )
                StatTile(
                    label = "本机归档",
                    value = "${days.size}",
                    unit = "天",
                    accent = Cyan,
                    modifier = Modifier.weight(1f),
                )
                StatTile(
                    label = "POI 标记",
                    value = "${pois.size}",
                    accent = Green,
                    modifier = Modifier.weight(1f),
                )
            }
        }

        item {
            SectionHeader("快捷控制", "工作模式与时间同步")
            CardBlock {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    listOf(DeviceOperatingMode.ACTIVE, DeviceOperatingMode.HIGH_PRECISION).forEach { mode ->
                        FilterChip(
                            selected = cfg.recordMode == mode.wireCode,
                            onClick = { trackRepo.setOperatingMode(mode) },
                            enabled = device.connected,
                            label = { Text(mode.label, fontSize = 12.sp) },
                        )
                    }
                }
                Spacer(Modifier.height(10.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { repo.syncTimeNow() },
                        enabled = device.connected,
                        modifier = Modifier.weight(1f),
                    ) { Text("同步时间") }
                    OutlinedButton(
                        onClick = { repo.markPoi() },
                        enabled = device.connected && gnss.fix,
                        modifier = Modifier.weight(1f),
                    ) { Text("打点 POI") }
                }
            }
        }

        if (days.isNotEmpty()) {
            item {
                SectionHeader("最近轨迹", "点击在地图查看")
            }
            items(days.take(3), key = { it.dayId }) { day ->
                DaySummaryCard(
                    summary = day,
                    onClick = { onSelectDay(day.dayId) },
                )
            }
        }
    }
}

@Composable
private fun ProdMapTab(
    container: AppContainer,
    selectedDayId: Int,
    onSelectDay: (Int) -> Unit,
    tileProvider: MapTileProvider,
    onSelectTileProvider: (MapTileProvider) -> Unit,
) {
    val repo = container.repo
    val trackRepo = container.trackRepo
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val liveTrail by repo.geoTrail.collectAsStateWithLifecycle()
    val days by trackRepo.days.collectAsStateWithLifecycle()
    val coordSystem = tileProvider.coordSystem

    val dayRecords = remember(selectedDayId, days) {
        if (selectedDayId != 0) trackRepo.loadDayRecords(selectedDayId) else emptyList()
    }
    var playbackFraction by remember(selectedDayId) { mutableFloatStateOf(1f) }
    var playing by remember(selectedDayId) { mutableStateOf(false) }

    LaunchedEffect(playing, dayRecords) {
        if (!playing || dayRecords.size < 2) return@LaunchedEffect
        if (playbackFraction >= 0.999f) playbackFraction = 0f
        while (playing && playbackFraction < 1f) {
            delay(80)
            playbackFraction = (playbackFraction + 0.015f).coerceAtMost(1f)
            if (playbackFraction >= 1f) playing = false
        }
    }

    val validRecords = remember(dayRecords) { dayRecords.filter { it.isFixValid } }
    val activeRecord: TrajectoryRecord? = if (validRecords.isNotEmpty()) {
        val idx = ((validRecords.lastIndex) * playbackFraction).toInt().coerceIn(0, validRecords.lastIndex)
        validRecords[idx]
    } else {
        null
    }

    val cursorPoint: LatLng? = when {
        activeRecord != null -> LatLng(activeRecord.latitudeDeg, activeRecord.longitudeDeg)
        gnss.fix -> LatLng(gnss.latitude, gnss.longitude)
        else -> null
    }
    val fallbackPts = remember(liveTrail) {
        liveTrail.map { LatLng(it.latitude, it.longitude) }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            FilterChip(
                selected = selectedDayId == 0,
                onClick = { onSelectDay(0) },
                label = { Text("实时", fontSize = 11.sp) },
            )
            days.take(7).forEach { day ->
                FilterChip(
                    selected = selectedDayId == day.dayId,
                    onClick = { onSelectDay(day.dayId) },
                    label = { Text(formatDayId(day.dayId), fontSize = 11.sp) },
                )
            }
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            MapTileProvider.entries.forEach { provider ->
                FilterChip(
                    selected = tileProvider == provider,
                    onClick = { onSelectTileProvider(provider) },
                    label = { Text(provider.label, fontSize = 11.sp) },
                )
            }
        }

        Card(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        ) {
            Box(Modifier.fillMaxSize()) {
                MapLibreTrackView(
                    records = dayRecords,
                    fallbackTrail = if (selectedDayId == 0) fallbackPts else emptyList(),
                    cursorPoint = cursorPoint,
                    cameraResetKey = selectedDayId,
                    modifier = Modifier.fillMaxSize(),
                    tileProvider = tileProvider,
                )
                // Mode legend pill
                Row(
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(8.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    StatusPill("GNSS", Cyan, filled = true)
                    StatusPill("PDR", Amber, filled = true)
                    StatusPill("POI", Green, filled = true)
                }
            }
        }

        if (validRecords.size >= 2) {
            CardBlock {
                // Linked elevation profile strip
                ElevationStrip(
                    records = validRecords,
                    progress = playbackFraction,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(44.dp),
                )
                Spacer(Modifier.height(6.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    TextButton(onClick = { playing = !playing }) {
                        Text(if (playing) "暂停" else "回放", color = Cyan)
                    }
                    Slider(
                        value = playbackFraction,
                        onValueChange = {
                            playing = false
                            playbackFraction = it
                        },
                        modifier = Modifier.weight(1f),
                    )
                }
                activeRecord?.let { r ->
                    val disp = CoordTransform.forDisplay(r.latitudeDeg, r.longitudeDeg, coordSystem)
                    MetricRow(
                        "时间 / 坐标 (${coordSystem.name})",
                        String.format(
                            Locale.US,
                            "%s · %.5f, %.5f",
                            formatTimeLocal(r.unixEpochMs),
                            disp.latitude,
                            disp.longitude,
                        ),
                        Cyan,
                    )
                    MetricRow(
                        "海拔 / 楼层 / 速度",
                        String.format(Locale.US, "%d m · F%d · %.1f km/h", r.altitudeM, r.floorLevel, r.speedMs * 3.6),
                    )
                }
            }
        } else {
            CardBlock {
                val disp = cursorPoint?.let { CoordTransform.forDisplay(it.latitude, it.longitude, coordSystem) }
                MetricRow(
                    "实时位置 (${coordSystem.name})",
                    if (disp != null) String.format(Locale.US, "%.6f, %.6f", disp.latitude, disp.longitude) else "—",
                    Cyan,
                )
                MetricRow(
                    "海拔 / 速度",
                    if (gnss.fix) String.format(Locale.US, "%.1f m · %.1f km/h", gnss.altitudeM, gnss.speedKmh) else "等待定位或选择历史日期",
                )
            }
        }
    }
}

@Composable
private fun ElevationStrip(
    records: List<TrajectoryRecord>,
    progress: Float,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier) {
        if (records.size < 2) return@Canvas
        val minAlt = records.minOf { it.altitudeM }.toFloat()
        val maxAlt = records.maxOf { it.altitudeM }.toFloat()
        val span = (maxAlt - minAlt).coerceAtLeast(10f)
        val path = Path()
        records.forEachIndexed { i, r ->
            val x = (i.toFloat() / records.lastIndex) * size.width
            val y = size.height - (((r.altitudeM - minAlt) / span) * (size.height - 8f)) - 4f
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path = path, color = Cyan, style = Stroke(width = 3f, cap = StrokeCap.Round))
        val cx = progress.coerceIn(0f, 1f) * size.width
        drawLine(color = Red, start = Offset(cx, 0f), end = Offset(cx, size.height), strokeWidth = 3f)
    }
}

@Composable
private fun ProdTimelineTab(
    container: AppContainer,
    onOpenDayOnMap: (Int) -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val trackRepo = container.trackRepo
    val days by trackRepo.days.collectAsStateWithLifecycle()
    val pois by trackRepo.pois.collectAsStateWithLifecycle()

    var exportTargetDay by remember { mutableStateOf<DayTrackSummary?>(null) }
    var deleteLocalDayTarget by remember { mutableStateOf<DayTrackSummary?>(null) }
    var editPoiTarget by remember { mutableStateOf<PoiRecord?>(null) }

    exportTargetDay?.let { target ->
        AlertDialog(
            onDismissRequest = { exportTargetDay = null },
            title = { Text("导出 ${formatDayId(target.dayId)}（WGS-84）") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("所有导出格式严格使用标准 WGS-84 坐标，可直接导入两步路 / 六只脚 / QGIS / Google Earth。", fontSize = 12.sp, color = TextDim)
                    ExportFormat.entries.forEach { fmt ->
                        Button(
                            onClick = {
                                exportTargetDay = null
                                scope.launch {
                                    val file = trackRepo.exportDayToCache(target.dayId, fmt)
                                    if (file != null) shareExportedFile(context, file, fmt)
                                }
                            },
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text("导出 ${fmt.label} (.${fmt.extension})")
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { exportTargetDay = null }) { Text("关闭") }
            },
        )
    }

    deleteLocalDayTarget?.let { target ->
        val dayStr = formatDayId(target.dayId)
        AlertDialog(
            onDismissRequest = { deleteLocalDayTarget = null },
            title = { Text("确认删除 $dayStr 本机轨迹？") },
            text = {
                Text("将永久删除本机保存的 $dayStr 轨迹记录（${target.recordCount} 个点）及相关摘要，删除后无法恢复。")
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        deleteLocalDayTarget = null
                        trackRepo.deleteLocalDay(target.dayId)
                    },
                ) { Text("确认删除", color = Red) }
            },
            dismissButton = {
                TextButton(onClick = { deleteLocalDayTarget = null }) { Text("取消") }
            },
        )
    }

    editPoiTarget?.let { poi ->
        var title by remember(poi.id) { mutableStateOf(poi.title) }
        var note by remember(poi.id) { mutableStateOf(poi.note) }
        AlertDialog(
            onDismissRequest = { editPoiTarget = null },
            title = { Text("编辑 POI 标记") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(value = title, onValueChange = { title = it }, label = { Text("名称") })
                    OutlinedTextField(value = note, onValueChange = { note = it }, label = { Text("备注") })
                }
            },
            confirmButton = {
                Button(onClick = {
                    trackRepo.updatePoi(poi.id, title, note)
                    editPoiTarget = null
                }) { Text("保存") }
            },
            dismissButton = {
                TextButton(onClick = {
                    trackRepo.deletePoi(poi.id)
                    editPoiTarget = null
                }) { Text("删除", color = Red) }
            },
        )
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            SectionHeader("每日轨迹归档", "共 ${days.size} 天 · 纯本地存储")
        }
        if (days.isEmpty()) {
            item {
                CardBlock {
                    Text(
                        text = "本机尚无已同步轨迹。连接设备后在首页点击「同步轨迹」，即可导入实时环记录或 LGD1 压缩日包。",
                        color = TextDim,
                        fontSize = 13.sp,
                    )
                }
            }
        } else {
            items(days, key = { it.dayId }) { day ->
                DaySummaryCard(
                    summary = day,
                    onClick = { onOpenDayOnMap(day.dayId) },
                    onExport = { exportTargetDay = day },
                    onDelete = { deleteLocalDayTarget = day },
                )
            }
        }

        item {
            SectionHeader("POI 地点标记", "${pois.size} 条 · 点击可编辑")
        }
        if (pois.isEmpty()) {
            item {
                CardBlock {
                    Text("暂无 POI 记录，短按硬件 KEY1 或点击首页「打点 POI」并同步后将显示在此。", color = TextDim, fontSize = 13.sp)
                }
            }
        } else {
            items(pois, key = { it.id }) { poi ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { editPoiTarget = poi },
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                    shape = RoundedCornerShape(14.dp),
                ) {
                    Column(Modifier.padding(14.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                        ) {
                            Text(poi.title, color = TextMain, fontWeight = FontWeight.SemiBold)
                            Text("${formatDayId(poi.dayId)} ${formatTimeLocal(poi.timestampMs)}", color = TextDim, fontSize = 12.sp)
                        }
                        if (poi.note.isNotBlank()) {
                            Spacer(Modifier.height(4.dp))
                            Text(poi.note, color = TextDim, fontSize = 12.sp)
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            String.format(Locale.US, "WGS-84: %.6f, %.6f · %d m · F%d", poi.latitude, poi.longitude, poi.altitudeM, poi.floorLevel),
                            color = Cyan,
                            fontSize = 12.sp,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ProdSettingsTab(
    container: AppContainer,
    tileProvider: MapTileProvider,
    onSelectTileProvider: (MapTileProvider) -> Unit,
    onOpenScanner: () -> Unit,
    onOpenOta: (() -> Unit)?,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val repo = container.repo
    val trackRepo = container.trackRepo
    val device by repo.device.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val cfg by trackRepo.deviceConfig.collectAsStateWithLifecycle()
    val remoteDays by trackRepo.remoteDays.collectAsStateWithLifecycle()

    var draftCfg by remember(cfg) { mutableStateOf(cfg) }
    var eraseRemoteDayTarget by remember { mutableStateOf<RemoteDayIndexEntry?>(null) }
    var backupBusy by remember { mutableStateOf(false) }
    var backupMessage by remember { mutableStateOf<String?>(null) }
    var backupIsError by remember { mutableStateOf(false) }

    val exportBackupLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/zip"),
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        backupBusy = true
        backupMessage = "正在导出备份…"
        backupIsError = false
        scope.launch {
            val result: Result<Int> = withContext(Dispatchers.IO) {
                try {
                    val os = context.contentResolver.openOutputStream(uri)
                        ?: error("无法打开目标文件进行写入")
                    os.use { stream -> trackRepo.exportBackupZip(stream) }
                } catch (ce: CancellationException) {
                    throw ce
                } catch (t: Throwable) {
                    Result.failure(t)
                }
            }
            backupBusy = false
            result.fold(
                onSuccess = { count ->
                    backupMessage = "已导出 $count 天轨迹与 POI 备份"
                    backupIsError = false
                },
                onFailure = { err ->
                    backupMessage = "导出备份失败：${err.message ?: err.javaClass.simpleName}"
                    backupIsError = true
                },
            )
        }
    }

    val importBackupLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        backupBusy = true
        backupMessage = "正在从备份恢复…"
        backupIsError = false
        scope.launch {
            val result: Result<Int> = withContext(Dispatchers.IO) {
                try {
                    val ins = context.contentResolver.openInputStream(uri)
                        ?: error("无法打开所选备份文件")
                    ins.use { stream -> trackRepo.importBackupZip(stream) }
                } catch (ce: CancellationException) {
                    throw ce
                } catch (t: Throwable) {
                    Result.failure(t)
                }
            }
            backupBusy = false
            result.fold(
                onSuccess = { count ->
                    backupMessage = "已恢复 $count 天轨迹与 POI"
                    backupIsError = false
                },
                onFailure = { err ->
                    backupMessage = "恢复备份失败：${err.message ?: err.javaClass.simpleName}"
                    backupIsError = true
                },
            )
        }
    }

    eraseRemoteDayTarget?.let { target ->
        val dayStr = formatDayId(target.dayId)
        AlertDialog(
            onDismissRequest = { eraseRemoteDayTarget = null },
            title = { Text("确认擦除设备端 $dayStr 归档？") },
            text = {
                Text("将擦除设备外置 Flash 中该日归档（$dayStr · ${target.recordCount} 条 · ${target.packageLen} B），删除后无法恢复（不影响已同步到本机的轨迹）。")
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        eraseRemoteDayTarget = null
                        trackRepo.eraseRemoteDay(target.dayId)
                    },
                ) { Text("确认擦除", color = Red) }
            },
            dismissButton = {
                TextButton(onClick = { eraseRemoteDayTarget = null }) { Text("取消") }
            },
        )
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            SectionHeader("设备连接与配对")
            CardBlock {
                MetricRow("当前设备", device.name)
                MetricRow("配对加密", bondState.label, if (bondState == BondState.BONDED) Green else Amber)
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onOpenScanner, modifier = Modifier.weight(1f)) {
                        Text("管理连接")
                    }
                    OutlinedButton(
                        onClick = { trackRepo.refreshDeviceConfig() },
                        enabled = device.connected,
                        modifier = Modifier.weight(1f),
                    ) { Text("读取配置") }
                }
                if (onOpenOta != null) {
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = onOpenOta,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("固件无线升级 (MCUboot SMP OTA)") }
                }
            }
        }

        // Live Config TLV Editor (0x13 SET_CONFIG / 0x14 GET_CONFIG)
        item {
            SectionHeader("设备参数配置 (Config TLV)", "写入外部 NOR 扇区 18 掉电保存")
            CardBlock {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("启用 RGB LED 状态指示灯", color = TextMain, fontSize = 13.sp)
                    Switch(
                        checked = draftCfg.ledEnable,
                        onCheckedChange = { draftCfg = draftCfg.copy(ledEnable = it) },
                    )
                }
                Spacer(Modifier.height(8.dp))
                Text("GNSS 采样率：${draftCfg.gnssRateHz} Hz", color = TextDim, fontSize = 12.sp)
                Slider(
                    value = draftCfg.gnssRateHz.toFloat(),
                    onValueChange = { draftCfg = draftCfg.copy(gnssRateHz = it.toInt().coerceIn(1, 10)) },
                    valueRange = 1f..10f,
                    steps = 8,
                )
                Text("静止自动关机：${if (draftCfg.autoOffMin == 0) "关闭" else "${draftCfg.autoOffMin} 分钟"}", color = TextDim, fontSize = 12.sp)
                Slider(
                    value = draftCfg.autoOffMin.toFloat(),
                    onValueChange = { draftCfg = draftCfg.copy(autoOffMin = (it.toInt() / 15) * 15) },
                    valueRange = 0f..240f,
                )
                Text("本地时区偏移：UTC${if (draftCfg.tzOffsetMin >= 0) "+" else ""}${draftCfg.tzOffsetMin / 60}", color = TextDim, fontSize = 12.sp)
                Slider(
                    value = (draftCfg.tzOffsetMin / 60).toFloat(),
                    onValueChange = { draftCfg = draftCfg.copy(tzOffsetMin = it.toInt() * 60) },
                    valueRange = -12f..14f,
                    steps = 25,
                )
                Text("每日归档时刻 (UTC)：${draftCfg.archiveHourUtc}:00", color = TextDim, fontSize = 12.sp)
                Slider(
                    value = draftCfg.archiveHourUtc.toFloat(),
                    onValueChange = { draftCfg = draftCfg.copy(archiveHourUtc = it.toInt().coerceIn(0, 23)) },
                    valueRange = 0f..23f,
                )
                Spacer(Modifier.height(6.dp))
                Button(
                    onClick = { trackRepo.applyDeviceConfig(draftCfg) },
                    enabled = device.connected,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("保存配置到设备") }
            }
        }

        item {
            SectionHeader("地图图源与坐标系", "图源与坐标系自动配对；存储与导出始终为 WGS-84")
            CardBlock {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    MapTileProvider.entries.forEach { provider ->
                        FilterChip(
                            selected = tileProvider == provider,
                            onClick = { onSelectTileProvider(provider) },
                            label = { Text(provider.label, fontSize = 12.sp) },
                        )
                    }
                }
                Spacer(Modifier.height(6.dp))
                Text(
                    text = if (tileProvider.usesGcj02) {
                        "当前：${tileProvider.label}（已自动启用显示层 GCJ-02 纠偏，匹配国内底图）"
                    } else {
                        "当前：${tileProvider.label}（直接使用原始 WGS-84 坐标，匹配国际底图）"
                    },
                    color = TextDim,
                    fontSize = 12.sp,
                )
            }
        }

        item {
            SectionHeader("本地备份与迁移", "纯离线 ZIP 打包本机全部轨迹与 POI")
            CardBlock {
                Text(
                    text = "所有数据仅保存在本机。换机或重装前可导出全部轨迹与 POI 为 ZIP 备份文件，随时离线恢复。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
                Spacer(Modifier.height(10.dp))
                Button(
                    onClick = {
                        val stamp = BACKUP_NAME_FMT
                            .withZone(ZoneId.systemDefault())
                            .format(Instant.now())
                        exportBackupLauncher.launch("lifegps_backup_$stamp.zip")
                    },
                    enabled = !backupBusy,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text(if (backupBusy) "处理中…" else "导出全部轨迹备份 (.zip)")
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(
                    onClick = {
                        importBackupLauncher.launch(arrayOf("application/zip", "application/octet-stream"))
                    },
                    enabled = !backupBusy,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("从备份恢复 (.zip)")
                }
                backupMessage?.let { msg ->
                    Spacer(Modifier.height(8.dp))
                    Text(
                        text = msg,
                        color = if (backupIsError) Red else Green,
                        fontSize = 12.sp,
                    )
                }
            }
        }

        if (remoteDays.isNotEmpty()) {
            item {
                SectionHeader("设备端外置 Flash 归档", "${remoteDays.size} 个日包")
            }
            items(remoteDays, key = { it.dayId }) { rday ->
                CardBlock {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column {
                            Text(formatDayId(rday.dayId), color = TextMain, fontWeight = FontWeight.SemiBold)
                            Text("${rday.recordCount} 条 · ${rday.packageLen} B", color = TextDim, fontSize = 12.sp)
                        }
                        TextButton(
                            onClick = { eraseRemoteDayTarget = rday },
                            enabled = device.connected,
                        ) { Text("擦除", color = Red) }
                    }
                }
            }
        }

        item {
            SectionHeader("硬件按键与指示灯速查")
            CardBlock {
                MetricRow("KEY1 短按", "标记当前位置 POI")
                MetricRow("KEY1 长按 3 秒", "开启 BLE 配对窗口（60 秒）")
                MetricRow("KEY1 长按 8 秒", "强制关机（进入 SYSTEM_OFF）")
                MetricRow("KEY2 短按", "切换标准记录 / 高精度连续模式")
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp), color = MaterialTheme.colorScheme.background)
                MetricRow("双闪绿灯", "定位成功 / POI 标记完成", Green)
                MetricRow("蓝灯闪烁", "BLE 就绪 / 数据同步 / 配对窗口", Cyan)
                MetricRow("快闪红灯", "低电量警告（≤10%）或存储告急", Red)
            }
        }

        item {
            var confirmFactoryReset by remember { mutableStateOf(false) }
            SectionHeader("危险操作")
            CardBlock {
                Text(
                    "恢复出厂设置将擦除设备端外置 Flash 中的暂存轨迹、历史归档、传感器标定、气压基准、运行配置及 BLE 绑定表并自动重启（不影响已同步到本机的轨迹）。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
                Spacer(Modifier.height(10.dp))
                OutlinedButton(
                    onClick = { confirmFactoryReset = true },
                    enabled = device.connected,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("恢复设备出厂设置", color = Red)
                }
            }
            if (confirmFactoryReset) {
                AlertDialog(
                    onDismissRequest = { confirmFactoryReset = false },
                    title = { Text("确认恢复设备出厂设置？") },
                    text = {
                        Text("此操作不可逆，将清空设备端全部未同步轨迹、校准参数与配对信息并立即重启设备。")
                    },
                    confirmButton = {
                        TextButton(
                            onClick = {
                                confirmFactoryReset = false
                                trackRepo.factoryResetDevice()
                            },
                        ) { Text("确认清除并重启", color = Red) }
                    },
                    dismissButton = {
                        TextButton(onClick = { confirmFactoryReset = false }) { Text("取消") }
                    },
                )
            }
        }
    }
}

@Composable
private fun DaySummaryCard(
    summary: DayTrackSummary,
    onClick: () -> Unit,
    onExport: (() -> Unit)? = null,
    onDelete: (() -> Unit)? = null,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(14.dp),
    ) {
        Column(Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(formatDayId(summary.dayId), color = TextMain, fontSize = 16.sp, fontWeight = FontWeight.Bold)
                StatusPill(
                    text = if (summary.isArchived) "LGD1 归档" else "LGS 实时",
                    color = if (summary.isArchived) Green else Cyan,
                    filled = false,
                )
            }
            Spacer(Modifier.height(6.dp))
            Text(
                text = String.format(
                    Locale.US,
                    "%.2f km · %d 步 · %d 点 · %d POI · 海拔 %d~%d m",
                    summary.distanceMeters / 1000.0,
                    summary.totalSteps,
                    summary.recordCount,
                    summary.poiCount,
                    summary.minAltM,
                    summary.maxAltM,
                ),
                color = TextDim,
                fontSize = 12.sp,
            )
            if (onExport != null || onDelete != null) {
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = onClick, modifier = Modifier.weight(1f)) {
                        Text("地图回放", fontSize = 12.sp)
                    }
                    if (onExport != null) {
                        Button(onClick = onExport, modifier = Modifier.weight(1f)) {
                            Text("导出", fontSize = 12.sp)
                        }
                    }
                    if (onDelete != null) {
                        TextButton(onClick = onDelete) {
                            Text("删除", color = Red, fontSize = 12.sp)
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProdDeviceScanScreen(
    container: AppContainer,
    onBack: () -> Unit,
) {
    val repo = container.repo
    val devices by repo.devices.collectAsStateWithLifecycle()
    val scanning by repo.scanning.collectAsStateWithLifecycle()

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text("扫描 Life_GPS 设备", fontWeight = FontWeight.SemiBold) },
                navigationIcon = {
                    TextButton(onClick = onBack) { Text("返回") }
                },
                actions = {
                    TextButton(onClick = { repo.scan() }, enabled = !scanning) {
                        Text(if (scanning) "扫描中…" else "重新扫描", color = Cyan)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                    titleContentColor = TextMain,
                ),
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (devices.isEmpty()) {
                item {
                    CardBlock {
                        Text(
                            text = if (scanning) "正在扫描附近的 LGPS 设备…" else "点击右上角「重新扫描」搜索设备；如需新配对请先长按设备 KEY1 3 秒。",
                            color = TextDim,
                            fontSize = 13.sp,
                        )
                    }
                }
            } else {
                items(devices, key = { it.address }) { dev ->
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                repo.connect(dev)
                                onBack()
                            },
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                        shape = RoundedCornerShape(14.dp),
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(
                                    dev.name.ifBlank { "未命名 BLE 设备" },
                                    color = TextMain,
                                    fontWeight = FontWeight.SemiBold,
                                )
                                Text(dev.address, color = TextDim, fontSize = 12.sp)
                            }
                            Text(
                                "${dev.rssi} dBm",
                                color = if (dev.rssi > -75) Green else Amber,
                                fontSize = 13.sp,
                                fontWeight = FontWeight.SemiBold,
                            )
                        }
                    }
                }
            }
        }
    }
}

private fun shareExportedFile(context: Context, file: File, format: ExportFormat) {
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = format.mimeType
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_SUBJECT, file.name)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(intent, "分享 ${format.label} 轨迹"))
}

private const val MAP_PREFS_NAME = "lifegps_ui_prefs"
private const val KEY_MAP_TILE_PROVIDER = "map_tile_provider"

private fun loadSavedTileProvider(context: Context): MapTileProvider {
    val prefs = context.getSharedPreferences(MAP_PREFS_NAME, Context.MODE_PRIVATE)
    return MapTileProvider.fromPrefKey(prefs.getString(KEY_MAP_TILE_PROVIDER, null))
}

private fun saveTileProvider(context: Context, provider: MapTileProvider) {
    context.getSharedPreferences(MAP_PREFS_NAME, Context.MODE_PRIVATE)
        .edit()
        .putString(KEY_MAP_TILE_PROVIDER, provider.prefKey)
        .apply()
}

private fun formatDayId(dayId: Int): String {
    val y = dayId / 10_000
    val m = (dayId / 100) % 100
    val d = dayId % 100
    return String.format(Locale.US, "%04d-%02d-%02d", y, m, d)
}

private val TIME_FMT: DateTimeFormatter =
    DateTimeFormatter.ofPattern("HH:mm:ss", Locale.US)

private val BACKUP_NAME_FMT: DateTimeFormatter =
    DateTimeFormatter.ofPattern("yyyyMMdd_HHmm", Locale.US)

private fun formatTimeLocal(epochMs: Long): String =
    TIME_FMT.withZone(ZoneId.systemDefault()).format(Instant.ofEpochMilli(epochMs))
