package com.lifegps.app.debug

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.navigation.Routes
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatTile
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.components.ToolCard
import com.lifegps.app.ui.components.logLevelColor
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@Composable
fun DebugHomeScreen(
    repo: FakeDebugRepository,
    onNavigate: (String) -> Unit,
) {
    val device by repo.device.collectAsStateWithLifecycle()
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val imu by repo.imu.collectAsStateWithLifecycle()
    val flash by repo.flash.collectAsStateWithLifecycle()
    val logs by repo.logs.collectAsStateWithLifecycle()

    val batteryColor = when {
        device.batteryPct > 50 -> Green
        device.batteryPct > 20 -> Amber
        else -> Red
    }

    DebugScaffold(
        title = "LIFE GPS 硬件调试",
        actions = {
            StatusPill(
                text = if (device.connected) "已连接" else "未连接",
                color = if (device.connected) Green else Red,
                filled = true,
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                    ),
                ) {
                    Column(Modifier.padding(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(
                                    device.name,
                                    color = TextMain,
                                    fontSize = 20.sp,
                                    fontWeight = FontWeight.Bold,
                                )
                                Spacer(Modifier.height(3.dp))
                                Text(
                                    "${device.firmware}  ·  ${device.protocol}",
                                    color = TextDim,
                                    fontSize = 12.sp,
                                )
                            }
                            StatusPill(
                                text = if (device.charging) "充电中" else "电池 ${device.batteryPct}%",
                                color = batteryColor,
                                filled = true,
                            )
                        }
                        Spacer(Modifier.height(12.dp))
                        HorizontalDivider(color = MaterialTheme.colorScheme.background)
                        Spacer(Modifier.height(10.dp))
                        MetricRow("工作模式", device.mode, Cyan)
                        MetricRow("电池电压", "${device.batteryMv} mV")
                        MetricRow("运行时间", formatUptime(device.uptimeSeconds))
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 10.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Button(
                                onClick = { onNavigate(Routes.DEVICE) },
                                modifier = Modifier.weight(1f),
                            ) { Text("扫描 / 连接") }
                            OutlinedButton(
                                onClick = { repo.disconnect() },
                                modifier = Modifier.weight(1f),
                            ) { Text("断开") }
                        }
                    }
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatTile(
                        "GNSS",
                        if (gnss.fix) "${gnss.sats}" else "--",
                        "sats",
                        if (gnss.fix) Green else Red,
                        Modifier.weight(1f),
                    )
                    StatTile("HDOP", String.format("%.1f", gnss.hdop), "", Cyan, Modifier.weight(1f))
                    StatTile("步数", "${imu.steps}", "", Amber, Modifier.weight(1f))
                }
            }

            item {
                SectionHeader("调试工具", "硬件 bring-up 优先")
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ToolCard(
                        "串口日志",
                        "USB / BLE Log\n级别过滤、暂停、导出",
                        { onNavigate(Routes.SERIAL) },
                        Modifier.weight(1f),
                    )
                    ToolCard(
                        "传感器状态",
                        "IMU / 磁力计 / 气压计\n实时数值与质量",
                        { onNavigate(Routes.SENSORS) },
                        Modifier.weight(1f),
                    )
                }
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ToolCard(
                        "GPS 状态",
                        "六状态机 / 星数 / HDOP\n经纬度 / C/N0",
                        { onNavigate(Routes.GNSS) },
                        Modifier.weight(1f),
                        Green,
                    )
                    ToolCard(
                        "硬件自检",
                        "Flash / 传感器 / GNSS\n按键 / LED / 充电",
                        { onNavigate(Routes.CHECK) },
                        Modifier.weight(1f),
                        Amber,
                    )
                }
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ToolCard(
                        "Flash 检查",
                        "扇区图 / Journal\n记录数与容量",
                        { onNavigate(Routes.FLASH) },
                        Modifier.weight(1f),
                        Cyan,
                    )
                    ToolCard(
                        "命令终端",
                        "opcode + hex\n响应与历史",
                        { onNavigate(Routes.CONSOLE) },
                        Modifier.weight(1f),
                        Cyan,
                    )
                }
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    ToolCard(
                        "空间轨迹",
                        "IMU / PDR 局部坐标\n3D 实时轨迹",
                        { onNavigate(Routes.SPATIAL) },
                        Modifier.weight(1f),
                        Green,
                    )
                    ToolCard(
                        "地图实时轨迹",
                        "GNSS 实时画线\n点击地图打点",
                        { onNavigate(Routes.MAP) },
                        Modifier.weight(1f),
                        Cyan,
                    )
                }
            }

            item {
                SectionHeader("快捷操作")
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { repo.runSelfTest() },
                        modifier = Modifier.weight(1f),
                    ) { Text("运行自检") }
                    OutlinedButton(
                        onClick = { repo.markPoi() },
                        modifier = Modifier.weight(1f),
                    ) { Text("打点 POI") }
                    OutlinedButton(
                        onClick = { repo.addLog(LogLevel.INFO, LogSource.BLE, "SYNC", "同步请求已排队（模拟）") },
                        modifier = Modifier.weight(1f),
                    ) { Text("同步") }
                }
            }

            item {
                SectionHeader("最近日志", "点击进入完整控制台")
                CardBlock {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onNavigate(Routes.SERIAL) },
                    ) {
                        if (logs.isEmpty()) {
                            Text("暂无日志", color = TextDim, fontSize = 13.sp)
                        } else {
                            logs.takeLast(5).forEach { entry ->
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 3.dp),
                                    verticalAlignment = Alignment.Top,
                                ) {
                                    Text(
                                        entry.time.take(8),
                                        color = TextDim,
                                        fontSize = 10.sp,
                                        fontFamily = FontFamily.Monospace,
                                    )
                                    Spacer(Modifier.width(8.dp))
                                    Text(
                                        entry.level.short,
                                        color = logLevelColor(entry.level),
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace,
                                    )
                                    Spacer(Modifier.width(6.dp))
                                    Text(
                                        "${entry.tag}: ${entry.message}",
                                        color = TextMain,
                                        fontSize = 11.sp,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                    )
                                }
                            }
                        }
                    }
                }
            }

            item {
                Text(
                    "提示：当前为假数据（FakeDebugRepository）。真实 BLE / USB 串口接入后，替换数据源即可。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(top = 4.dp, bottom = 20.dp),
                )
            }
        }
    }
}

private fun formatUptime(seconds: Long): String {
    val h = seconds / 3600
    val m = (seconds % 3600) / 60
    val s = seconds % 60
    return if (h > 0) "${h}h ${m}m" else "${m}m ${s}s"
}
