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
import androidx.compose.ui.Modifier
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
import kotlin.math.sqrt

@Composable
fun SensorStatusScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
    onOpenTrail: () -> Unit,
) {
    val imu by repo.imu.collectAsStateWithLifecycle()
    val mag by repo.mag.collectAsStateWithLifecycle()

    val accelMag = sqrt(imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az)
    val gyroMag = sqrt(imu.gx * imu.gx + imu.gy * imu.gy + imu.gz * imu.gz)

    DebugScaffold(title = "传感器状态", onBack = onBack) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    StatusPill("IMU 26Hz", Green, filled = true)
                    StatusPill(if (imu.zupt) "ZUPT 锁死" else "运动中", if (imu.zupt) Amber else Cyan, filled = true)
                    StatusPill("磁异常检测：固件未提供", TextDim, filled = true)
                }
            }

            item {
                CardBlock("IMU · LSM6DSR") {
                    MetricRow("加速度 X", f(imu.ax, "g"))
                    MetricRow("加速度 Y", f(imu.ay, "g"))
                    MetricRow("加速度 Z", f(imu.az, "g"))
                    ValueBar("合加速度", accelMag, 2f, Cyan, f(accelMag, "g"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("角速度 X", f(imu.gx, "°/s"))
                    MetricRow("角速度 Y", f(imu.gy, "°/s"))
                    MetricRow("角速度 Z", f(imu.gz, "°/s"))
                    ValueBar("角速度模长", gyroMag, 300f, Amber, f(gyroMag, "°/s"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("四元数", "固件 v1 未提供", TextDim)
                }
            }

            item {
                CardBlock("磁力计 · LIS2MDL") {
                    MetricRow("X", f(mag.mx, "µT"))
                    MetricRow("Y", f(mag.my, "µT"))
                    MetricRow("Z", f(mag.mz, "µT"))
                    ValueBar("磁场模长", mag.norm, 100f, if (mag.anomaly) Red else Green, f(mag.norm, "µT"))
                    MetricRow("异常检测", "固件 v1 未提供", TextDim)
                }
            }

            item {
                CardBlock("气压计 · BMP388") {
                    MetricRow("气压", "—")
                    MetricRow("温度", "—")
                    MetricRow("相对高度", "—")
                    MetricRow("楼层", "—")
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Status 帧只带 BMP388 未补偿原始值，固件 v1 不下发 hPa / °C / 高度，App 不做换算。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item {
                CardBlock("PDR / 计步") {
                    MetricRow("累计步数", "${imu.steps}")
                    MetricRow("ZUPT", if (imu.zupt) "LOCKED" else "MOVING", if (imu.zupt) Amber else Green)
                    MetricRow("数据源", "ZUPT + PDR 计步（固件）")
                }
            }

            item {
                CardBlock("空间轨迹") {
                    Text(
                        "用 IMU/PDR 局部坐标实时绘制 3D 轨迹，检查航向漂移、ZUPT 与步长。",
                        color = TextDim,
                        fontSize = 12.sp,
                    )
                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = onOpenTrail,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("打开 3D 空间轨迹") }
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { repo.sendCommand("MAG_CAL", "") },
                        modifier = Modifier.weight(1f),
                    ) { Text("磁校准") }
                    OutlinedButton(
                        onClick = { repo.sendCommand("BARO_BASELINE", "") },
                        modifier = Modifier.weight(1f),
                    ) { Text("气压基准") }
                }
            }

            item {
                Text(
                    "Status 帧以 1 Hz 推送；加速度按 ±4 g（0.122 mg/LSB）、角速度按 ±1000 dps（35 mdps/LSB）、磁力计按 1.5 mG/LSB 换算。四元数与 hPa 不在 v1 帧内。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }
}

private fun f(value: Float, unit: String): String {
    val text = String.format(Locale.US, "%.3f", value)
    return if (unit.isBlank()) text else "$text $unit"
}
