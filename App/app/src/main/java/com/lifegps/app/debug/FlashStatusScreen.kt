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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain
import java.util.Locale

@Composable
fun FlashStatusScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
) {
    val flash by repo.flash.collectAsStateWithLifecycle()
    val usage = if (flash.totalMb > 0f) (flash.usedMb / flash.totalMb).coerceIn(0f, 1f) else 0f

    DebugScaffold(title = "Flash / Journal", onBack = onBack) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            item {
                CardBlock("外部 Flash") {
                    MetricRow(
                        "JEDEC ID",
                        flash.jedec.ifBlank { "固件 v1 未提供" },
                        if (flash.jedec.isBlank()) TextDim else Green,
                    )
                    MetricRow(
                        "容量",
                        if (flash.totalMb > 0f) "${flash.totalMb.toInt()} MB（FLASH_INFO flash_bytes）" else "—",
                    )
                    MetricRow("已用", if (flash.usedMb > 0f) "${fmtOne(flash.usedMb)} MB" else "—")
                    Spacer(Modifier.height(4.dp))
                    LinearProgressIndicator(
                        progress = { usage },
                        modifier = Modifier.fillMaxWidth(),
                        color = if (usage > 0.85f) Red else Cyan,
                    )
                    Spacer(Modifier.height(8.dp))
                    MetricRow("轨迹记录", if (flash.totalMb > 0f) "${flash.recordCount}" else "—")
                    MetricRow(
                        "Journal",
                        flash.journal.ifBlank { "固件 v1 未提供" },
                        if (flash.journal == "COMMITTED") Green else TextDim,
                    )
                    MetricRow("OTA slot1", "固件 v1 未提供", TextDim)
                }
            }

            item {
                SectionHeader("扇区图", "来自 FLASH_INFO(0x60)")
                CardBlock {
                    if (flash.sectors.isEmpty()) {
                        Text(
                            "尚未收到 FLASH_INFO，点下方“刷新扇区图”发送 0x60。",
                            color = TextDim,
                            fontSize = 12.sp,
                        )
                    } else {
                        val rows = flash.sectors.chunked(4)
                        rows.forEachIndexed { rowIndex, row ->
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                            ) {
                                row.forEachIndexed { colIndex, state ->
                                    val index = rowIndex * 4 + colIndex
                                    SectorCell(
                                        index = index,
                                        state = state,
                                        modifier = Modifier.weight(1f),
                                    )
                                }
                            }
                            if (rowIndex < rows.lastIndex) {
                                Spacer(Modifier.height(6.dp))
                            }
                        }
                        Spacer(Modifier.height(10.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            Legend("ARCHIVED", Green)
                            Legend("WRITING", Amber)
                            Legend("ERASED", TextDim)
                        }
                    }
                }
            }

            item {
                CardBlock("归档 / 掉电安全") {
                    MetricRow("暂存区", "扇区数与 sector_seq 来自 FLASH_INFO")
                    MetricRow("归档区", "Flash 总量来自 FLASH_INFO")
                    MetricRow("原子提交", "WRITE → VERIFY → COMMIT → ERASE")
                    MetricRow("掉电恢复", "Journal 未 COMMITTED 则回滚")
                }
            }

            item {
                Button(
                    onClick = { repo.sendCommand("FLASH_INFO", "") },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("刷新扇区图") }
            }

            item {
                Text(
                    "FLASH_INFO(0x60) 返回 flash_bytes / traj_bytes / sector_seq / sector_count / records_written；JEDEC、已用字节与 Journal 不在 v1 响应内，保持 “—”。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }
}

@Composable
private fun SectorCell(
    index: Int,
    state: String,
    modifier: Modifier = Modifier,
) {
    val color = when (state) {
        "ARCHIVED" -> Green
        "WRITING" -> Amber
        "ERASED" -> TextDim
        else -> Red
    }
    Card(
        modifier = modifier,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(8.dp),
    ) {
        Column(
            modifier = Modifier.padding(8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                "%02X".format(index),
                color = color,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(3.dp))
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(4.dp)
                    .background(color, RoundedCornerShape(50)),
            )
            Spacer(Modifier.height(3.dp))
            Text(state.take(3), color = TextDim, fontSize = 8.sp)
        }
    }
}

@Composable
private fun Legend(label: String, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .width(8.dp)
                .height(8.dp)
                .background(color, RoundedCornerShape(50)),
        )
        Spacer(Modifier.width(4.dp))
        Text(label, color = TextDim, fontSize = 10.sp)
    }
}

private fun fmtOne(value: Float): String = String.format(Locale.US, "%.1f", value)
