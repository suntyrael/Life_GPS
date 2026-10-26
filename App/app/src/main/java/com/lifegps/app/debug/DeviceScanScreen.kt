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
import com.lifegps.app.ui.components.EmptyHint
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@Composable
fun DeviceScanScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val current by repo.device.collectAsStateWithLifecycle()
    val devices by repo.devices.collectAsStateWithLifecycle()
    val scanning by repo.scanning.collectAsStateWithLifecycle()

    DebugScaffold(title = "设备扫描 / 连接", onBack = onBack) { padding ->
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
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text("当前设备", color = TextDim, fontSize = 12.sp)
                                Spacer(Modifier.height(3.dp))
                                Text(
                                    current.name,
                                    color = TextMain,
                                    fontWeight = FontWeight.Bold,
                                    fontSize = 18.sp,
                                )
                                Text(current.firmware, color = TextDim, fontSize = 12.sp)
                            }
                            StatusPill(
                                text = if (current.connected) "已连接" else "未连接",
                                color = if (current.connected) Green else Red,
                                filled = true,
                            )
                        }
                        Spacer(Modifier.height(10.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(
                                onClick = { repo.scan() },
                                enabled = !scanning,
                                modifier = Modifier.weight(1f),
                            ) { Text(if (scanning) "扫描中..." else "扫描 BLE") }
                            OutlinedButton(
                                onClick = { repo.disconnect() },
                                enabled = current.connected,
                                modifier = Modifier.weight(1f),
                            ) { Text("断开") }
                        }
                    }
                }
            }

            if (scanning) {
                item {
                    LinearProgressIndicator(
                        modifier = Modifier.fillMaxWidth(),
                        color = Cyan,
                    )
                }
            }

            if (devices.isEmpty() && !scanning) {
                item { EmptyHint("点击“扫描 BLE”查找 LGPS-XXXX 设备") }
            }

            items(devices, key = { it.name }) { device ->
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
                            Text(device.name, color = TextMain, fontWeight = FontWeight.SemiBold)
                            Spacer(Modifier.height(2.dp))
                            Text(
                                "${device.firmware}  ·  RSSI ${device.rssi} dBm",
                                color = TextDim,
                                fontSize = 12.sp,
                            )
                        }
                        Button(onClick = { repo.connect(device) }) { Text("连接") }
                    }
                }
            }

            item {
                Text(
                    "真实 BLE 接入后，这里会显示 Service UUID、广播数据、MTU/PHY 与 bond 状态。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(top = 8.dp, bottom = 20.dp),
                )
            }
        }
    }
}
