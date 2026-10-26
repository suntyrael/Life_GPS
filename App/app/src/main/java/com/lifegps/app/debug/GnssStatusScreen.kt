package com.lifegps.app.debug

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.components.ValueBar
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import java.util.Locale

@Composable
fun GnssStatusScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val device by repo.device.collectAsStateWithLifecycle()

    val fixColor = if (gnss.fix) Green else Red
    val hdopColor = when {
        gnss.hdop <= 1.0 -> Green
        gnss.hdop <= 2.0 -> Amber
        else -> Red
    }

    DebugScaffold(
        title = "GPS 状态",
        onBack = onBack,
        actions = {
            StatusPill(
                text = if (gnss.fix) "FIX" else "NO FIX",
                color = fixColor,
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
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    StatusPill(gnss.state, Cyan, filled = true)
                    StatusPill("${gnss.sats} sats", if (gnss.sats >= 8) Green else Amber, filled = true)
                    StatusPill("HDOP ${one(gnss.hdop)}", hdopColor, filled = true)
                }
            }

            item {
                CardBlock("定位概览") {
                    MetricRow("状态机", gnss.state, Cyan)
                    MetricRow("定位", if (gnss.fix) "YES" else "NO", fixColor)
                    MetricRow("有效卫星", "${gnss.sats}", if (gnss.sats >= 8) Green else Amber)
                    ValueBar("HDOP", gnss.hdop.toFloat(), 5f, hdopColor, one(gnss.hdop))
                    MetricRow("TTFF", gnss.ttff)
                    MetricRow("UTC 时间", gnss.utcTime)
                    MetricRow("设备", device.name)
                }
            }

            item {
                CardBlock("位置 / 运动") {
                    MetricRow("纬度", six(gnss.latitude))
                    MetricRow("经度", six(gnss.longitude))
                    MetricRow("海拔", "${one(gnss.altitudeM)} m")
                    MetricRow("地速", "${one(gnss.speedKmh)} km/h")
                    MetricRow("航向", "${one(gnss.headingDeg)}°")
                }
            }

            item {
                SectionHeader("C/N0", "每颗卫星载噪比（模拟）")
                CardBlock {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(96.dp),
                        verticalAlignment = Alignment.Bottom,
                        horizontalArrangement = Arrangement.spacedBy(3.dp),
                    ) {
                        gnss.cno.forEachIndexed { index, value ->
                            val ratio = (value / 50f).coerceIn(0.05f, 1f)
                            val color = when {
                                value >= 35 -> Green
                                value >= 25 -> Amber
                                else -> Red
                            }
                            Column(
                                modifier = Modifier.weight(1f),
                                horizontalAlignment = Alignment.CenterHorizontally,
                            ) {
                                Text("$value", color = TextDim, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
                                Box(
                                    Modifier
                                        .width(8.dp)
                                        .height((70f * ratio).dp)
                                        .background(color),
                                )
                                Spacer(Modifier.height(2.dp))
                                Text("${index + 1}", color = TextDim, fontSize = 8.sp)
                            }
                        }
                    }
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { repo.addLog(LogLevel.INFO, LogSource.BLE, "GNSS", "cold start requested（模拟）") },
                        modifier = Modifier.weight(1f),
                    ) { Text("冷启动") }
                    OutlinedButton(
                        onClick = { repo.addLog(LogLevel.INFO, LogSource.BLE, "GNSS", "hot start requested（模拟）") },
                        modifier = Modifier.weight(1f),
                    ) { Text("热启动") }
                }
            }

            item {
                Text(
                    "真实接入后：这里会解析 UBX NAV-PVT / MON-VER，并显示六状态机、卫星列表、C/N0、PPS 与上下电时序。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }
}

private fun one(value: Double): String = String.format(Locale.US, "%.1f", value)

private fun six(value: Double): String = String.format(Locale.US, "%.6f", value)
