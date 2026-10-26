package com.lifegps.app.debug

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.EmptyHint
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

private val OPCODES = listOf(
    "GET_STATUS",
    "GET_CAPS",
    "SET_TIME",
    "POI_MARK",
    "SELFTEST",
    "GET_DAY_INDEX",
)

@Composable
fun CommandConsoleScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val console by repo.console.collectAsStateWithLifecycle()
    var expanded by remember { mutableStateOf(false) }
    var opcode by remember { mutableStateOf(OPCODES.first()) }
    var payload by remember { mutableStateOf("") }

    DebugScaffold(
        title = "命令终端",
        onBack = onBack,
        actions = {
            TextButton(onClick = { repo.clearConsole() }) { Text("清空") }
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
                CardBlock("发送命令") {
                    Text(
                        "Control 特征：0xA5 + opcode + seq + len + payload + CRC16",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                    Spacer(Modifier.height(8.dp))
                    Box {
                        OutlinedButton(
                            onClick = { expanded = true },
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(opcode, color = Cyan, fontFamily = FontFamily.Monospace)
                        }
                        DropdownMenu(
                            expanded = expanded,
                            onDismissRequest = { expanded = false },
                        ) {
                            OPCODES.forEach { item ->
                                DropdownMenuItem(
                                    text = { Text(item, fontFamily = FontFamily.Monospace) },
                                    onClick = {
                                        opcode = item
                                        expanded = false
                                    },
                                )
                            }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = payload,
                        onValueChange = { payload = it },
                        label = { Text("payload hex（可空）") },
                        placeholder = { Text("例如 01 00 00 00") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = { repo.sendCommand(opcode, payload) },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("发送") }
                }
            }

            item {
                Text(
                    "响应历史",
                    color = TextMain,
                    fontWeight = FontWeight.SemiBold,
                )
            }

            if (console.isEmpty()) {
                item { EmptyHint("还没有命令记录") }
            } else {
                items(console.reversed()) { entry ->
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        ),
                    ) {
                        Column(Modifier.padding(12.dp)) {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text(
                                    entry.time,
                                    color = TextDim,
                                    fontSize = 10.sp,
                                    fontFamily = FontFamily.Monospace,
                                )
                                Text(
                                    entry.opcode,
                                    color = Cyan,
                                    fontSize = 11.sp,
                                    fontWeight = FontWeight.Bold,
                                    fontFamily = FontFamily.Monospace,
                                )
                            }
                            Spacer(Modifier.height(4.dp))
                            Text(
                                "req: ${entry.request}",
                                color = TextDim,
                                fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace,
                            )
                            Text(
                                "res: ${entry.response}",
                                color = if (entry.response.startsWith("00")) Green else TextMain,
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace,
                                lineHeight = 15.sp,
                            )
                        }
                    }
                }
            }

            item {
                Text(
                    "真实接入后：从 protocol YAML 自动生成 opcode 列表与 payload 表单；响应按命令模型解码，不再显示裸 hex。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(top = 6.dp, bottom = 20.dp),
                )
            }
        }
    }
}
