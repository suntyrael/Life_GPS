package com.lifegps.app.debug

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.EmptyHint
import com.lifegps.app.ui.components.logLevelColor
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SerialLogScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val logs by repo.logs.collectAsStateWithLifecycle()
    var paused by remember { mutableStateOf(false) }
    var level by remember { mutableStateOf<LogLevel?>(null) }
    var source by remember { mutableStateOf<LogSource?>(null) }
    var query by remember { mutableStateOf("") }

    val filtered = logs.filter { entry ->
        (level == null || entry.level == level) &&
            (source == null || entry.source == source) &&
            (query.isBlank() || entry.message.contains(query, ignoreCase = true) ||
                entry.tag.contains(query, ignoreCase = true))
    }
    val listState = rememberLazyListState()

    LaunchedEffect(filtered.size, paused) {
        if (!paused && filtered.isNotEmpty()) {
            listState.animateScrollToItem(filtered.lastIndex)
        }
    }

    DebugScaffold(
        title = "串口 / BLE 日志",
        onBack = onBack,
        actions = {
            TextButton(onClick = { paused = !paused }) {
                Text(if (paused) "继续" else "暂停", color = Cyan)
            }
            TextButton(onClick = { repo.clearLogs() }) { Text("清空") }
            TextButton(onClick = { repo.exportLogs() }) { Text("导出") }
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 12.dp),
        ) {
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                label = { Text("过滤 tag / 内容") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                FilterChip(
                    selected = source == null,
                    onClick = { source = null },
                    label = { Text("全部来源") },
                )
                LogSource.entries.forEach { item ->
                    FilterChip(
                        selected = source == item,
                        onClick = { source = if (source == item) null else item },
                        label = { Text(item.label) },
                    )
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                FilterChip(
                    selected = level == null,
                    onClick = { level = null },
                    label = { Text("全部级别") },
                )
                LogLevel.entries.forEach { item ->
                    FilterChip(
                        selected = level == item,
                        onClick = { level = if (level == item) null else item },
                        label = { Text(item.short) },
                    )
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "${filtered.size} / ${logs.size} 条",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f),
                )
                Text(
                    if (paused) "已暂停" else "自动滚动",
                    color = if (paused) TextDim else Cyan,
                    fontSize = 11.sp,
                )
            }
            if (filtered.isEmpty()) {
                EmptyHint("没有匹配的日志")
            } else {
                LazyColumn(
                    state = listState,
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    contentPadding = PaddingValues(bottom = 20.dp),
                ) {
                    items(filtered) { entry ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 2.dp),
                            verticalAlignment = Alignment.Top,
                        ) {
                            Text(
                                entry.time,
                                color = TextDim,
                                fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace,
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                entry.level.short,
                                color = logLevelColor(entry.level),
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace,
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                entry.source.label,
                                color = TextDim,
                                fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace,
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                "${entry.tag}: ${entry.message}",
                                color = TextMain,
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace,
                                lineHeight = 15.sp,
                            )
                        }
                    }
                }
            }
        }
    }
}
