package com.lifegps.app.debug

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
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
import com.lifegps.app.ui.theme.TextMain
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
    val used = gnss.satellites.count { it.used }
    val avgCno = if (gnss.satellites.isEmpty()) 0 else gnss.satellites.sumOf { it.cno } / gnss.satellites.size
    val sorted = gnss.satellites.sortedWith(
        compareByDescending<SatelliteInfo> { it.used }.thenByDescending { it.cno }
    )

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
                    StatusPill("HDOP ${fmtOne(gnss.hdop)}", hdopColor, filled = true)
                }
            }

            item {
                CardBlock("定位概览") {
                    MetricRow("状态机", gnss.state, Cyan)
                    MetricRow("定位", if (gnss.fix) "YES" else "NO", fixColor)
                    MetricRow("有效卫星", "${gnss.sats}", if (gnss.sats >= 8) Green else Amber)
                    ValueBar("HDOP", gnss.hdop.toFloat(), 5f, hdopColor, fmtOne(gnss.hdop))
                    MetricRow("参与解算", "$used / ${gnss.satellites.size}")
                    MetricRow("平均 C/N0", "$avgCno dB-Hz")
                    MetricRow("TTFF", gnss.ttff)
                    MetricRow("UTC 时间", gnss.utcTime)
                    MetricRow("设备", device.name)
                }
            }

            item {
                CardBlock("位置 / 运动") {
                    MetricRow("纬度", fmtSix(gnss.latitude))
                    MetricRow("经度", fmtSix(gnss.longitude))
                    MetricRow("海拔", "${fmtOne(gnss.altitudeM)} m")
                    MetricRow("地速", "${fmtOne(gnss.speedKmh)} km/h")
                    MetricRow("航向", "${fmtOne(gnss.headingDeg)}°")
                }
            }

            item {
                SectionHeader("卫星信息", "SVID / 系统 / C/N0 / 仰角 / 方位 / 参与解算")
                CardBlock {
                    SatHeader()
                    Spacer(Modifier.height(4.dp))
                    sorted.forEach { satellite ->
                        SatRow(satellite)
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
                    "真实接入后：解析 UBX NAV-PVT / MON-VER，显示卫星列表、C/N0、仰角/方位与参与解算状态；柱状图不再作为主视图。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }
}

@Composable
private fun SatHeader() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SatCell("SVID", 0.9f, TextAlign.Start, TextDim, header = true)
        SatCell("SYS", 0.8f, TextAlign.Start, TextDim, header = true)
        SatCell("C/N0", 0.8f, TextAlign.End, TextDim, header = true)
        SatCell("EL", 0.7f, TextAlign.End, TextDim, header = true)
        SatCell("AZ", 0.8f, TextAlign.End, TextDim, header = true)
        SatCell("USED", 0.8f, TextAlign.End, TextDim, header = true)
    }
}

@Composable
private fun SatRow(satellite: SatelliteInfo) {
    val cnoColor = when {
        satellite.cno >= 35 -> Green
        satellite.cno >= 25 -> Amber
        else -> Red
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SatCell(
            text = satellite.svid.toString().padStart(2, '0'),
            weight = 0.9f,
            align = TextAlign.Start,
            color = TextMain,
            bold = satellite.used,
        )
        SatCell(satellite.system, 0.8f, TextAlign.Start, TextDim)
        SatCell("${satellite.cno}", 0.8f, TextAlign.End, cnoColor, bold = true)
        SatCell("${satellite.elevation}°", 0.7f, TextAlign.End, TextMain)
        SatCell("${satellite.azimuth}°", 0.8f, TextAlign.End, TextDim)
        SatCell(
            text = if (satellite.used) "YES" else "-",
            weight = 0.8f,
            align = TextAlign.End,
            color = if (satellite.used) Green else TextDim,
        )
    }
}

@Composable
private fun androidx.compose.foundation.layout.RowScope.SatCell(
    text: String,
    weight: Float,
    align: TextAlign,
    color: androidx.compose.ui.graphics.Color,
    header: Boolean = false,
    bold: Boolean = false,
) {
    Text(
        text = text,
        modifier = Modifier.weight(weight),
        color = color,
        fontSize = if (header) 10.sp else 11.sp,
        fontFamily = FontFamily.Monospace,
        fontWeight = if (header || bold) FontWeight.SemiBold else FontWeight.Normal,
        textAlign = align,
    )
}

private fun fmtOne(value: Double): String = String.format(Locale.US, "%.1f", value)

private fun fmtSix(value: Double): String = String.format(Locale.US, "%.6f", value)
