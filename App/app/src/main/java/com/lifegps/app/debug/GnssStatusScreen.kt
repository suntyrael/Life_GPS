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
import com.lifegps.app.ble.GnssCodec
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
    repo: DebugRepository,
    onBack: () -> Unit,
    onOpenNmea: () -> Unit = {},
    onOpenConfig: () -> Unit = {},
) {
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val link by repo.gnssLink.collectAsStateWithLifecycle()
    val debug by repo.gnssDebug.collectAsStateWithLifecycle()
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
                    StatusPill(if (gnss.fix) "HDOP ${fmtOne(gnss.hdop)}" else "HDOP --", hdopColor, filled = true)
                }
            }

            item {
                CardBlock("定位概览") {
                    MetricRow("状态机", gnss.state, Cyan)
                    MetricRow("定位", if (gnss.fix) "YES" else "NO", fixColor)
                    MetricRow("有效卫星", "${gnss.sats}", if (gnss.sats >= 8) Green else Amber)
                    ValueBar("HDOP", gnss.hdop.toFloat(), 5f, hdopColor, fmtOne(gnss.hdop))
                    MetricRow("参与解算", if (gnss.satellites.isEmpty()) "等待 NAV-SAT" else "$used / ${gnss.satellites.size}")
                    MetricRow("平均 C/N0", if (gnss.satellites.isEmpty()) "等待 NAV-SAT" else "$avgCno dB-Hz")
                    MetricRow("TTFF", gnss.ttff)
                    MetricRow("UTC 时间", gnss.utcTime)
                    MetricRow("设备", device.name)
                }
            }

            /* Receive path, i.e. "is the receiver talking at all" (telemetry 0x01).
             * Kept above the solution on purpose: when nothing works, these five
             * counters say which of the five causes it is.
             */
            item {
                val linkColor = when {
                    !link.received -> TextDim
                    link.ubxFrames > 0 -> Green
                    link.badChecksum > 0 -> Red
                    link.rxBytes > 0 -> Amber
                    else -> Red
                }
                CardBlock("接收链路（UBX / NMEA）") {
                    MetricRow("判定", link.verdict, linkColor)
                    MetricRow("状态机", link.state, Cyan)
                    MetricRow("接收字节", "${link.rxBytes}")
                    MetricRow("UBX 帧", "${link.ubxFrames}", if (link.ubxFrames > 0) Green else TextDim)
                    MetricRow("校验错误", "${link.badChecksum}", if (link.badChecksum > 0) Red else TextDim)
                    MetricRow("NMEA 起始符计数", "${link.nmeaStarts}")
                    MetricRow("环形丢字节", "${link.rxDropped}", if (link.rxDropped > 0) Amber else TextDim)
                    MetricRow("波特率", if (link.baud > 0) "${link.baud} 8N1" else "—")
                    MetricRow("PVT 龄期", if (link.received) "${link.ageMs} ms" else "—")
                }
            }

            item {
                CardBlock("位置 / 运动") {
                    MetricRow("纬度", if (gnss.fix) fmtSix(gnss.latitude) else "—")
                    MetricRow("经度", if (gnss.fix) fmtSix(gnss.longitude) else "—")
                    MetricRow("海拔", if (gnss.fix) "${fmtOne(gnss.altitudeM)} m" else "—")
                    MetricRow("地速", if (gnss.fix) "${fmtOne(gnss.speedKmh)} km/h" else "—")
                    MetricRow("航向", if (gnss.fix) "${fmtOne(gnss.headingDeg)}°" else "—")
                }
            }

            item {
                SectionHeader("卫星信息", "系统 / SVID / C/N0 / 仰角 / 状态")
                CardBlock {
                    if (gnss.satellites.isEmpty()) {
                        Text(
                            "单星信息来自 UBX-NAV-SAT，接收机上电后一直以 1 Hz 采集，不需要开调试会话。" +
                                "这里为空只有三种可能：模组当前未上电（退避中）、还没有周视卫星（室内 / 天线未接），" +
                                "或者接收机拒绝了 NAV-SAT 输出率（看串口日志里的 ACK-NAK）。",
                            color = TextDim,
                            fontSize = 12.sp,
                        )
                    } else {
                        SatHeader()
                        Spacer(Modifier.height(4.dp))
                        sorted.forEach { satellite ->
                            SatRow(satellite)
                        }
                    }
                }
            }

            /* The normal power policy cuts the rail 60 s after a cold start without a
             * fix and retries minutes later, so the satellite table can never fill in
             * while debugging indoors. A bounded session suspends that policy and
             * switches the extra outputs on.
             */
            item {
                CardBlock("调试会话") {
                    MetricRow(
                        "状态",
                        if (debug.flags != 0) "运行中，剩余 ${debug.remainingSeconds} s" else "关闭",
                        if (debug.flags != 0) Green else TextDim,
                    )
                    MetricRow("保持上电", if (debug.holdPower) "是" else "否")
                    MetricRow("单星 NAV-SAT", "常开 1 Hz（bit1 已无作用）")
                    MetricRow("NMEA 直通", if (debug.nmea) "是" else "否")
                    debug.error?.let { message ->
                        Text(message, color = Red, fontSize = 12.sp)
                    }
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(
                            onClick = {
                                repo.setGnssDebug(
                                    GnssCodec.FLAG_HOLD_POWER or
                                        GnssCodec.FLAG_NAV_SAT or
                                        GnssCodec.FLAG_NMEA,
                                    300,
                                )
                            },
                            modifier = Modifier.weight(1f),
                        ) { Text("开启 5 分钟") }
                        OutlinedButton(
                            onClick = { repo.setGnssDebug(0, 0) },
                            modifier = Modifier.weight(1f),
                        ) { Text("关闭") }
                    }
                    Text(
                        "会话会让模组持续上电（不按 60 s 冷启动超时断电），到期自动恢复省电策略。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(
                        onClick = onOpenNmea,
                        modifier = Modifier.weight(1f),
                    ) { Text("NMEA 日志") }
                    OutlinedButton(
                        onClick = onOpenConfig,
                        modifier = Modifier.weight(1f),
                    ) { Text("GNSS 设置") }
                }
            }

            item {
                Text(
                    "定位数值来自 Status 帧的 gnss_state / satellites / pdop / 位置字段；接收链路卡片来自 1 Hz 遥测组 0x01（rx/ubx/ckerr/nmea/丢字节/波特率）。单星 C/N0、仰角方位与 TTFF 仍不在当前帧内，因此显示 “—”，不伪造。",
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
        SatCell("SVID", 0.8f, TextAlign.Start, TextDim, header = true)
        SatCell("SYS", 0.7f, TextAlign.Start, TextDim, header = true)
        SatCell("C/N0", 0.7f, TextAlign.End, TextDim, header = true)
        SatCell("EL", 0.6f, TextAlign.End, TextDim, header = true)
        SatCell("状态", 0.9f, TextAlign.End, TextDim, header = true)
        SatCell("USED", 0.7f, TextAlign.End, TextDim, header = true)
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
            weight = 0.8f,
            align = TextAlign.Start,
            color = TextMain,
            bold = satellite.used,
        )
        SatCell(satellite.system, 0.7f, TextAlign.Start, TextDim)
        SatCell("${satellite.cno}", 0.7f, TextAlign.End, cnoColor, bold = true)
        SatCell("${satellite.elevation}°", 0.6f, TextAlign.End, TextMain)
        SatCell(
            text = satellite.tracking.ifBlank { "—" },
            weight = 0.9f,
            align = TextAlign.End,
            color = if (satellite.used) Green else TextDim,
        )
        SatCell(
            text = if (satellite.used) "YES" else "-",
            weight = 0.7f,
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
