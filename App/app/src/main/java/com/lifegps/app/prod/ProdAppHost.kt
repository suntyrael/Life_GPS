// SPDX-License-Identifier: MIT
package com.lifegps.app.prod

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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
import com.lifegps.core.model.BondState
import com.lifegps.core.model.CoordSystem
import com.lifegps.core.model.CoordTransform
import java.util.Locale

private enum class ProdTab(val label: String, val badge: String) {
    HOME("首页", "设备"),
    MAP("地图", "轨迹"),
    TIMELINE("时间轴", "日历"),
    SETTINGS("设置", "关于"),
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProdAppHost(
    container: AppContainer,
    topBarExtraAction: (@Composable () -> Unit)? = null,
) {
    val repo = container.repo
    val device by repo.device.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val linkState by repo.linkState.collectAsStateWithLifecycle()

    var currentTab by rememberSaveable { mutableStateOf(ProdTab.HOME) }
    var showDeviceScanner by rememberSaveable { mutableStateOf(false) }

    if (showDeviceScanner) {
        ProdDeviceScanScreen(
            container = container,
            onBack = { showDeviceScanner = false },
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
                )
                ProdTab.MAP -> ProdMapTab(container = container)
                ProdTab.TIMELINE -> ProdTimelineTab(container = container)
                ProdTab.SETTINGS -> ProdSettingsTab(
                    container = container,
                    onOpenScanner = { showDeviceScanner = true },
                )
            }
        }
    }
}

@Composable
private fun ProdHomeTab(
    container: AppContainer,
    onOpenScanner: () -> Unit,
) {
    val repo = container.repo
    val device by repo.device.collectAsStateWithLifecycle()
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val imu by repo.imu.collectAsStateWithLifecycle()
    val flash by repo.flash.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val pois by repo.pois.collectAsStateWithLifecycle()

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
                    MetricRow("记录模式", device.mode, Cyan)
                    MetricRow("GNSS 状态", if (gnss.fix) "已定位 (${gnss.sats} 颗星)" else gnss.state)
                    MetricRow(
                        "设备存储",
                        if (flash.totalMb > 0f) {
                            String.format(Locale.US, "%.2f / %.0f MB (%d 条)", flash.usedMb, flash.totalMb, flash.recordCount)
                        } else {
                            "—"
                        },
                    )
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

        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatTile(
                    label = "今日步数",
                    value = if (device.connected) "${imu.steps}" else "--",
                    accent = Amber,
                    modifier = Modifier.weight(1f),
                )
                StatTile(
                    label = "定位精度",
                    value = if (gnss.fix && gnss.pdop > 0.0) String.format(Locale.US, "%.1f", gnss.pdop) else "--",
                    unit = "pDOP",
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
            SectionHeader("快捷操作", "纯本地 BLE 通信")
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

        item {
            CardBlock {
                Text("本地数据存储说明", color = TextMain, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Spacer(Modifier.height(6.dp))
                Text(
                    text = "所有轨迹记录均采用 WGS-84 标准仅保存于本机，支持 32 MB / 64 MB 外置 Flash 设备，不上传任何云端服务器。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
            }
        }
    }
}

@Composable
private fun ProdMapTab(container: AppContainer) {
    val repo = container.repo
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val trail by repo.geoTrail.collectAsStateWithLifecycle()
    val pois by repo.pois.collectAsStateWithLifecycle()
    val coordSystem by container.displayCoordSystem.collectAsStateWithLifecycle()

    val displayPos = if (gnss.fix) {
        CoordTransform.forDisplay(gnss.latitude, gnss.longitude, coordSystem)
    } else {
        null
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("实时轨迹预览", color = TextMain, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                CoordSystem.entries.forEach { sys ->
                    FilterChip(
                        selected = coordSystem == sys,
                        onClick = { container.setDisplayCoordSystem(sys) },
                        label = { Text(sys.name, fontSize = 11.sp) },
                    )
                }
            }
        }

        Card(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
            shape = RoundedCornerShape(16.dp),
        ) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                if (trail.size < 2) {
                    Text(
                        text = if (gnss.fix) {
                            "已获取定位，移动后将在此绘制实时轨迹\n(MapLibre 矢量/瓦片底图将在 M4 接入)"
                        } else {
                            "等待设备 GNSS 定位或同步历史日包"
                        },
                        color = TextDim,
                        fontSize = 13.sp,
                        modifier = Modifier.padding(24.dp),
                    )
                } else {
                    Canvas(modifier = Modifier.fillMaxSize().padding(20.dp)) {
                        val pts = trail.map { CoordTransform.forDisplay(it.latitude, it.longitude, coordSystem) }
                        val minLat = pts.minOf { it.latitude }
                        val maxLat = pts.maxOf { it.latitude }
                        val minLon = pts.minOf { it.longitude }
                        val maxLon = pts.maxOf { it.longitude }
                        val spanLat = (maxLat - minLat).coerceAtLeast(1e-5)
                        val spanLon = (maxLon - minLon).coerceAtLeast(1e-5)

                        val path = Path()
                        pts.forEachIndexed { idx, p ->
                            val x = ((p.longitude - minLon) / spanLon).toFloat() * size.width
                            val y = (1f - ((p.latitude - minLat) / spanLat).toFloat()) * size.height
                            if (idx == 0) path.moveTo(x, y) else path.lineTo(x, y)
                        }
                        drawPath(
                            path = path,
                            color = Cyan,
                            style = Stroke(width = 6f, cap = StrokeCap.Round),
                        )
                        val last = pts.last()
                        val lx = ((last.longitude - minLon) / spanLon).toFloat() * size.width
                        val ly = (1f - ((last.latitude - minLat) / spanLat).toFloat()) * size.height
                        drawCircle(color = Green, radius = 10f, center = Offset(lx, ly))
                    }
                }
            }
        }

        CardBlock {
            MetricRow(
                "当前位置 (${coordSystem.name})",
                if (displayPos != null) {
                    String.format(Locale.US, "%.6f, %.6f", displayPos.latitude, displayPos.longitude)
                } else {
                    "—"
                },
                Cyan,
            )
            MetricRow("海拔 / 速度", if (gnss.fix) String.format(Locale.US, "%.1f m  ·  %.1f km/h", gnss.altitudeM, gnss.speedKmh) else "—")
            MetricRow("轨迹点数 / POI", "${trail.size} 点  ·  ${pois.size} 个 POI")
        }
    }
}

@Composable
private fun ProdTimelineTab(container: AppContainer) {
    val repo = container.repo
    val pois by repo.pois.collectAsStateWithLifecycle()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            CardBlock {
                Text("每日轨迹归档", color = TextMain, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(6.dp))
                Text(
                    text = "本地 `.lgd` / `.lgs` 日包与 Room 时间轴索引将在下一阶段（M3/M4）接入，届时可按日历查看、回放并导出 GPX / KML / GeoJSON / CSV。",
                    color = TextDim,
                    fontSize = 13.sp,
                )
            }
        }
        item {
            SectionHeader("本次会话 POI 标记", "${pois.size} 条")
        }
        if (pois.isEmpty()) {
            item {
                CardBlock {
                    Text("暂无 POI 标记，连接设备后可点击首页「打点 POI」或短按硬件 KEY1。", color = TextDim, fontSize = 13.sp)
                }
            }
        } else {
            items(pois) { poi ->
                CardBlock {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text("${poi.label}  ${poi.note}", color = TextMain, fontWeight = FontWeight.SemiBold)
                        Text(poi.time, color = TextDim, fontSize = 12.sp)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        String.format(Locale.US, "WGS-84: %.6f, %.6f", poi.latitude, poi.longitude),
                        color = Cyan,
                        fontSize = 12.sp,
                    )
                }
            }
        }
    }
}

@Composable
private fun ProdSettingsTab(
    container: AppContainer,
    onOpenScanner: () -> Unit,
) {
    val repo = container.repo
    val device by repo.device.collectAsStateWithLifecycle()
    val bondState by repo.bondState.collectAsStateWithLifecycle()
    val coordSystem by container.displayCoordSystem.collectAsStateWithLifecycle()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            SectionHeader("设备与配对")
            CardBlock {
                MetricRow("当前设备", device.name)
                MetricRow("配对状态", bondState.label, if (bondState == BondState.BONDED) Green else Amber)
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onOpenScanner, modifier = Modifier.weight(1f)) {
                        Text("管理设备连接")
                    }
                    OutlinedButton(
                        onClick = { repo.syncTimeNow() },
                        enabled = device.connected,
                        modifier = Modifier.weight(1f),
                    ) { Text("校准时间") }
                }
            }
        }

        item {
            SectionHeader("地图显示坐标系", "内部存储与导出始终为 WGS-84")
            CardBlock {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CoordSystem.entries.forEach { sys ->
                        FilterChip(
                            selected = coordSystem == sys,
                            onClick = { container.setDisplayCoordSystem(sys) },
                            label = {
                                Text(if (sys == CoordSystem.WGS84) "WGS-84（原始）" else "GCJ-02（国内底图纠偏）")
                            },
                        )
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
                MetricRow("KEY2 短按", "循环切换工作模式（标准/高精度）")
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp), color = MaterialTheme.colorScheme.background)
                MetricRow("LED 慢闪", "已定位记录中 / 正常运行", Green)
                MetricRow("LED 呼吸", "搜星中或配对窗口开启", Cyan)
                MetricRow("LED 快闪", "低电量警告或异常告警", Red)
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
