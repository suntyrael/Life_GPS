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
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.components.checkStateColor
import com.lifegps.app.ui.components.checkStateLabel
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@Composable
fun HardwareCheckScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val checks by repo.checks.collectAsStateWithLifecycle()
    val running by repo.checkRunning.collectAsStateWithLifecycle()

    val passCount = checks.count { it.state == CheckState.PASS }
    val failCount = checks.count { it.state == CheckState.FAIL }
    val pendingCount = checks.count { it.state == CheckState.PENDING }

    DebugScaffold(title = "硬件自检", onBack = onBack) { padding ->
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
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                ) {
                    Column(Modifier.padding(16.dp)) {
                        Text("Bring-up Checklist", color = TextMain, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "读取 MCU / Flash / 传感器 / GNSS 的真实 ID 与状态。真实固件接入后由 SELFTEST 命令返回。",
                            color = TextDim,
                            fontSize = 12.sp,
                        )
                        Spacer(Modifier.height(10.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            StatusPill("$passCount PASS", checkStateColor(CheckState.PASS), filled = true)
                            StatusPill("$pendingCount PENDING", checkStateColor(CheckState.PENDING), filled = true)
                            StatusPill("$failCount FAIL", checkStateColor(CheckState.FAIL), filled = true)
                        }
                        Spacer(Modifier.height(12.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(
                                onClick = { repo.runSelfTest() },
                                enabled = !running,
                                modifier = Modifier.weight(1f),
                            ) { Text(if (running) "自检中..." else "运行自检") }
                            OutlinedButton(
                                onClick = { repo.exportLogs() },
                                modifier = Modifier.weight(1f),
                            ) { Text("导出报告") }
                        }
                        if (running) {
                            Spacer(Modifier.height(10.dp))
                            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                        }
                    }
                }
            }

            item { SectionHeader("检查项", "PASS / FAIL / PENDING") }

            items(checks, key = { it.name }) { item ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(item.name, color = TextMain, fontWeight = FontWeight.SemiBold)
                            Spacer(Modifier.height(2.dp))
                            Text(item.detail, color = TextDim, fontSize = 12.sp)
                        }
                        StatusPill(
                            text = checkStateLabel(item.state),
                            color = checkStateColor(item.state),
                            filled = true,
                        )
                    }
                }
            }

            item {
                Text(
                    "真实命令建议：SELFTEST(0x40) 一次返回所有 ID；FLASH_INFO(0x60) 返回扇区图；GET_FAULT_LOG(0x70) 返回复位原因。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(top = 6.dp, bottom = 20.dp),
                )
            }
        }
    }
}
