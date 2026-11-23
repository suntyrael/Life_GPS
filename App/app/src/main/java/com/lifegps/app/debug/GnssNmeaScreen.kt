package com.lifegps.app.debug

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.EmptyHint
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

/**
 * Raw NMEA stream as the receiver sends it (telemetry group 0x08).
 *
 * The lines are shown verbatim - no field is re-parsed here. NMEA is the one view
 * that shows what the receiver thinks before any UBX decoding happens, which is
 * exactly what is needed when GSV shows no satellites at all.
 */
@Composable
fun GnssNmeaScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
) {
    val lines by repo.nmea.collectAsStateWithLifecycle()
    val debug by repo.gnssDebug.collectAsStateWithLifecycle()
    val link by repo.gnssLink.collectAsStateWithLifecycle()
    var paused by remember { mutableStateOf(false) }
    var talker by remember { mutableStateOf<String?>(null) }

    val talkers = remember(lines) {
        lines.mapNotNull { line ->
            line.substringAfter('$', "").take(2).takeIf { it.isNotBlank() }
        }
            .distinct()
            .sorted()
    }

    val shown = remember(lines, talker) {
        if (talker == null) lines else lines.filter { it.startsWith("\$$talker") }
    }

    val listState = rememberLazyListState()
    LaunchedEffect(shown.size, paused) {
        if (!paused && shown.isNotEmpty()) {
            listState.animateScrollToItem(shown.lastIndex)
        }
    }

    DebugScaffold(
        title = "NMEA 原始流",
        onBack = onBack,
        actions = {
            TextButton(onClick = { paused = !paused }) {
                Text(if (paused) "继续" else "暂停", color = Cyan)
            }
            TextButton(onClick = { repo.clearNmea() }) { Text("清空") }
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(12.dp),
            state = listState,
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            item {
                CardBlock("采集状态") {
                    Text(
                        if (debug.nmea) {
                            "正在转发（调试会话剩余 ${debug.remainingSeconds} s，保留最近 ${lines.size} 句）"
                        } else {
                            "未开启。NMEA 直通属于调试会话的一部分，请在「GPS」页右上角点「保持上电」（会话 300 s）。"
                        },
                        color = if (debug.nmea) TextMain else TextDim,
                        fontSize = 12.sp,
                    )
                }
            }

            /* The receive-path counters live here, not on the GPS page: they answer
             * "is the receiver talking at all", which is the question this page is
             * for. Same source as before, telemetry group 0x01.
             */
            item {
                CardBlock("接收链路") {
                    MetricRow("判定", link.verdict, if (link.ubxFrames > 0) Green else TextDim)
                    MetricRow("接收字节", "${link.rxBytes}")
                    MetricRow("UBX 帧", "${link.ubxFrames}", if (link.ubxFrames > 0) Green else TextDim)
                    MetricRow("校验错误", "${link.badChecksum}", if (link.badChecksum > 0) Red else TextDim)
                    MetricRow("NMEA 起始符", "${link.nmeaStarts}")
                    MetricRow("环形丢字节", "${link.rxDropped}", if (link.rxDropped > 0) Amber else TextDim)
                    MetricRow("波特率", if (link.baud > 0) "${link.baud} 8N1" else "—")
                }
            }

            if (talkers.isNotEmpty()) {
                item {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        FilterChip(
                            selected = talker == null,
                            onClick = { talker = null },
                            label = { Text("全部") },
                        )
                        talkers.forEach { item ->
                            FilterChip(
                                selected = talker == item,
                                onClick = { talker = if (talker == item) null else item },
                                label = { Text(item, fontFamily = FontFamily.Monospace) },
                            )
                        }
                    }
                }
            }

            if (shown.isEmpty()) {
                item { EmptyHint("还没有 NMEA 语句。开启调试会话后，接收机默认的 1 Hz GGA/GSA/GSV/RMC 会出现在这里。") }
            } else {
                items(shown) { line ->
                    Text(
                        text = line,
                        color = TextMain,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }
        }
    }
}
