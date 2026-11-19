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
    onOpenCalibration: () -> Unit,
) {
    val imu by repo.imu.collectAsStateWithLifecycle()
    val mag by repo.mag.collectAsStateWithLifecycle()
    val baro by repo.baro.collectAsStateWithLifecycle()
    val attitude by repo.attitude.collectAsStateWithLifecycle()
    val calib by repo.calib.collectAsStateWithLifecycle()

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
                    MetricRow("加速度 X（body frame, 26 Hz）", f(imu.ax, "g"))
                    MetricRow("加速度 Y（body frame, 26 Hz）", f(imu.ay, "g"))
                    MetricRow("加速度 Z（body frame, 26 Hz）", f(imu.az, "g"))
                    ValueBar("合加速度（body frame）", accelMag, 2f, Cyan, f(accelMag, "g"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("加速度 X（sensor frame, 1 Hz）", f(imu.axSensor, "g"))
                    MetricRow("加速度 Y（sensor frame, 1 Hz）", f(imu.aySensor, "g"))
                    MetricRow("加速度 Z（sensor frame, 1 Hz）", f(imu.azSensor, "g"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("重力 X", valueOrDash(imu.gravityValid, imu.gravityXg, "g"))
                    MetricRow("重力 Y", valueOrDash(imu.gravityValid, imu.gravityYg, "g"))
                    MetricRow("重力 Z", valueOrDash(imu.gravityValid, imu.gravityZg, "g"))
                    MetricRow("线性加速度 X", valueOrDash(imu.gravityValid, imu.linearXg, "g"))
                    MetricRow("线性加速度 Y", valueOrDash(imu.gravityValid, imu.linearYg, "g"))
                    MetricRow("线性加速度 Z", valueOrDash(imu.gravityValid, imu.linearZg, "g"))
                    MetricRow("IMU 温度", tempOrDash(imu.imuTempValid, imu.imuTempC))
                    MetricRow("磁力计温度", tempOrDash(imu.magTempValid, imu.magTempC))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("角速度 X（body frame, 26 Hz）", f(imu.gx, "°/s"))
                    MetricRow("角速度 Y（body frame, 26 Hz）", f(imu.gy, "°/s"))
                    MetricRow("角速度 Z（body frame, 26 Hz）", f(imu.gz, "°/s"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("角速度 X（sensor frame, 1 Hz）", f(imu.gxSensor, "°/s"))
                    MetricRow("角速度 Y（sensor frame, 1 Hz）", f(imu.gySensor, "°/s"))
                    MetricRow("角速度 Z（sensor frame, 1 Hz）", f(imu.gzSensor, "°/s"))
                    ValueBar("角速度模长", gyroMag, 300f, Amber, f(gyroMag, "°/s"))
                    Spacer(Modifier.height(6.dp))
                    MetricRow("四元数 W", String.format(Locale.US, "%.3f", attitude.qw))
                    MetricRow("四元数 X", String.format(Locale.US, "%.3f", attitude.qx))
                    MetricRow("四元数 Y", String.format(Locale.US, "%.3f", attitude.qy))
                    MetricRow("四元数 Z", String.format(Locale.US, "%.3f", attitude.qz))
                    MetricRow(
                        "横滚 / 俯仰",
                        String.format(Locale.US, "%.1f° / %.1f°", attitude.rollDeg, attitude.pitchDeg),
                        Cyan,
                    )
                    MetricRow(
                        "航向",
                        if (attitude.valid) String.format(Locale.US, "%.1f°", attitude.headingDeg) else "--",
                        Green,
                    )
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
                    MetricRow(
                        "气压（已补偿）",
                        if (baro.valid) String.format(Locale.US, "%.1f hPa", baro.pressureHpa) else "—",
                        if (baro.valid) Cyan else TextDim,
                    )
                    if (baro.tempValid) {
                        MetricRow("温度", String.format(Locale.US, "%.1f °C", baro.temperatureC))
                    } else {
                        MetricRow("温度", "—", TextDim)
                    }
                    MetricRow(
                        "相对高度（相对基准）",
                        if (baro.baselineValid) {
                            String.format(Locale.US, "%+.1f m", baro.relAltM)
                        } else {
                            "—"
                        },
                        if (baro.baselineValid) Cyan else TextDim,
                    )
                    MetricRow(
                        "基准气压",
                        if (baro.baselineValid) {
                            String.format(Locale.US, "%.1f hPa", baro.baselineHpa)
                        } else {
                            "—"
                        },
                        if (baro.baselineValid) Cyan else TextDim,
                    )
                    MetricRow(
                        "基准来源",
                        when {
                            !baro.baselineValid -> "—"
                            baro.baselineAuto -> "固件自动（GNSS 静止）"
                            else -> "取当前值 / 手动输入"
                        },
                        if (baro.baselineValid) Cyan else TextDim,
                    )
                    MetricRow("楼层", "—", TextDim)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "数值来自 Telemetry 0x04（BMP388，1 Hz，固件已做 TPG 温度/压力补偿）；" +
                            "Status 帧里的未补偿原始值不参与换算。楼层没有固件来源，不提供。",
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
                CardBlock("传感器校准", "磁力计硬铁/软铁、加速度计六面法、气压基准") {
                    MetricRow(
                        "磁力计",
                        calib.magResult?.let {
                            CalibFormat.lastCalib(it.lastCalibUnixS, it.fromStore)
                        } ?: if (calib.magSupported) "已支持，尚未校准" else "固件未提供 MAG_CAL",
                        if (calib.magResult?.fromStore == true) Green else TextDim,
                    )
                    MetricRow(
                        "加速度计",
                        calib.accelResult?.let {
                            CalibFormat.lastCalib(it.lastCalibUnixS, it.fromStore)
                        } ?: if (calib.accelSupported) "已支持，尚未校准" else "固件未提供 ACCEL_CAL",
                        if (calib.accelResult?.fromStore == true) Green else TextDim,
                    )
                    if (calib.magPhase == CalibPhase.COLLECTING ||
                        calib.accelPhase == CalibPhase.COLLECTING
                    ) {
                        MetricRow("进行中", "设备正在采集，点进去可接入会话", Cyan)
                    }
                    Spacer(Modifier.height(8.dp))
                    Button(onClick = onOpenCalibration, modifier = Modifier.fillMaxWidth()) {
                        Text("打开校准向导")
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "气压基准（0x42）在向导里可设置：取当前气压或手动输入 hPa；" +
                            "固件返回 03 NOT_SUPPORTED 时按钮保持禁用。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item {
                Text(
                    "Status 帧在有订阅时以 10 Hz 推送（无订阅 1 Hz）；加速度按 ±4 g（0.122 mg/LSB）、角速度按 ±1000 dps（35 mdps/LSB）、磁力计按 1.5 mG/LSB 换算。姿态四元数由 Telemetry 特征以 26 Hz 下发（界面限流到 15 Hz 渲染）；气压/温度由 Telemetry 0x04 以 1 Hz 下发（已补偿），不在 Status 帧内。",
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

private fun valueOrDash(valid: Boolean, value: Float, unit: String): String =
    if (valid) f(value, unit) else "—"

private fun tempOrDash(valid: Boolean, value: Float): String =
    if (valid) String.format(Locale.US, "%.1f °C", value) else "—"
