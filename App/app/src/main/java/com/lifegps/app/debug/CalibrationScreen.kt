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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ble.AccelFaceStateCode
import com.lifegps.app.ble.CalibCodec
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.components.ValueBar
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import java.util.Locale
import kotlin.math.abs
import kotlinx.coroutines.launch

/**
 * Sensor calibration wizard (MAG_CAL 0x41 / ACCEL_CAL 0x43 / BARO_BASELINE 0x42).
 *
 * The screen never computes a coefficient: the firmware owns the sampling, the
 * maths and the persistence, and the App mirrors the state it reports. What the
 * App adds is the part the device cannot do by itself — telling the user which
 * pose to hold, whether the pose is actually held (live gravity vector) and
 * whether the result is worth keeping ([CalibJudge]).
 */
@Composable
fun CalibrationScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
) {
    val calib by repo.calib.collectAsStateWithLifecycle()
    val imu by repo.imu.collectAsStateWithLifecycle()
    val mag by repo.mag.collectAsStateWithLifecycle()
    val baro by repo.baro.collectAsStateWithLifecycle()
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val view = LocalView.current
    var confirmLeave by remember { mutableStateOf(false) }

    // A capture asks the user to hold a pose for seconds: letting the screen sleep
    // in the middle of it looks like a hang.
    DisposableEffect(view) {
        view.keepScreenOn = true
        onDispose { view.keepScreenOn = false }
    }

    // The device keeps calibrating while the App is backgrounded, so the page
    // re-syncs on entry instead of assuming a fresh session. The barometer has no
    // query command (`0x42` action 0 is a side effect), so its state is re-rendered
    // from the last BARO telemetry packet without any BLE traffic.
    LaunchedEffect(repo) {
        repo.refreshCalibration()
        repo.refreshBaroBaseline()
    }

    val collecting = calib.magPhase == CalibPhase.COLLECTING ||
        calib.accelPhase == CalibPhase.COLLECTING

    DebugScaffold(
        title = "传感器校准",
        onBack = { if (collecting) confirmLeave = true else onBack() },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            item {
                CardBlock("当前状态") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        StatusPill(
                            text = if (calib.magSupported) "MAG_CAL 可用" else "MAG_CAL 不可用",
                            color = if (calib.magSupported) Green else TextDim,
                            filled = calib.magSupported,
                        )
                        StatusPill(
                            text = if (calib.accelSupported) "ACCEL_CAL 可用" else "ACCEL_CAL 不可用",
                            color = if (calib.accelSupported) Green else TextDim,
                            filled = calib.accelSupported,
                        )
                    }
                    Spacer(Modifier.height(6.dp))
                    MetricRow(
                        "磁力计",
                        calib.magResult?.let {
                            CalibFormat.lastCalib(it.lastCalibUnixS, it.fromStore)
                        } ?: "未校准",
                        if (calib.magResult?.fromStore == true) Green else TextDim,
                    )
                    MetricRow(
                        "加速度计",
                        calib.accelResult?.let {
                            CalibFormat.lastCalib(it.lastCalibUnixS, it.fromStore)
                        } ?: "未校准",
                        if (calib.accelResult?.fromStore == true) Green else TextDim,
                    )
                    MetricRow(
                        "最近一次校准时间",
                        if (calib.lastCalibUnixS > 0L) {
                            CalibFormat.lastCalib(calib.lastCalibUnixS, true)
                        } else {
                            "—"
                        },
                    )
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = { repo.refreshCalibration() },
                        enabled = !calib.busy,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("重新读取设备状态") }
                }
            }

            item {
                AccelCalCard(
                    calib = calib,
                    gravityG = listOf(imu.gravityXg, imu.gravityYg, imu.gravityZg),
                    gravityValid = imu.gravityValid,
                    onCapturePose = repo::captureAccelPose,
                    onCaptureQuick = repo::captureAccelQuick,
                    onCancel = repo::cancelAccelFace,
                    onFinish = repo::finishAccelCal,
                    onClear = repo::clearAccelCal,
                )
            }

            item {
                MagCalCard(
                    calib = calib,
                    magNormUt = mag.norm,
                    onStart = repo::startMagCal,
                    onFinish = repo::finishMagCal,
                    onCancel = repo::cancelMagCal,
                    onClear = repo::clearMagCal,
                )
            }

            item {
                BaroBaselineCard(
                    baro = baro,
                    gnss = gnss,
                    onTakeCurrent = repo::takeBaroBaseline,
                    onSetManual = repo::setBaroBaselineManual,
                    onCalibrateFromGnss = repo::calibrateBaroFromGnss,
                )
            }

            item {
                Text(
                    "校准系数由固件计算并保存在设备内，App 只负责引导与判定。" +
                        "阈值：加速度计零偏 ±30 mg / 刻度 ±3 % / 陀螺零偏 ±2 °/s；" +
                        "磁力计硬铁 ±800 LSB / 刻度 ±15 % / 轴覆盖 ≥ 80 %。" +
                        "不达标时重做比“凑合保存”更省事——偏置会直接进 PDR 的航向漂移。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }

    if (confirmLeave) {
        AlertDialog(
            onDismissRequest = { confirmLeave = false },
            title = { Text("正在采集中") },
            text = { Text("离开会中断本次采集。设备上已保存的系数不受影响。") },
            confirmButton = {
                TextButton(
                    onClick = {
                        confirmLeave = false
                        repo.cancelMagCal()
                        repo.cancelAccelFace()
                        onBack()
                    },
                ) { Text("中断并离开") }
            },
            dismissButton = {
                TextButton(onClick = { confirmLeave = false }) { Text("继续校准") }
            },
        )
    }
}

@Composable
private fun AccelCalCard(
    calib: CalibState,
    gravityG: List<Float>,
    gravityValid: Boolean,
    onCapturePose: () -> Unit,
    onCaptureQuick: () -> Unit,
    onCancel: () -> Unit,
    onFinish: () -> Unit,
    onClear: () -> Unit,
) {
    val predictedFace = CalibFaces.detect(gravityG)
    CardBlock(
        title = "加速度计 · 六面法",
        hint = "把设备换一个面平放，按一次“采集当前姿态”。是哪一面由设备根据重力向量自动识别，不需要你选面。",
    ) {
        when (calib.accelPhase) {
            CalibPhase.UNSUPPORTED -> {
                Text(
                    "固件未提供 ACCEL_CAL（0x43）。采集与系数计算都在固件侧完成，App 无法替代。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
            }

            CalibPhase.COLLECTING -> {
                if (calib.accelActivePosition == CalibCodec.POSITION_QUICK) {
                    MetricRow("模式", "单面快速（不参与六面法）", Amber)
                }
                MetricRow(
                    "本机预测姿态",
                    if (predictedFace == CalibFaces.NONE) "姿态不够平" else CalibFaces.label(predictedFace),
                    if (predictedFace == CalibFaces.NONE) Amber else Cyan,
                )
                Spacer(Modifier.height(4.dp))
                if (!gravityValid) {
                    Text(
                        "等待固件给出重力向量（Status 帧 sensor_flags.bit0）",
                        color = Amber,
                        fontSize = 12.sp,
                    )
                } else {
                    Text(
                        text = gravityVectorText(gravityG),
                        color = if (predictedFace == CalibFaces.NONE) Amber else Green,
                        fontSize = 12.sp,
                    )
                    Text(
                        text = if (predictedFace == CalibFaces.NONE) {
                            "请把设备平放在某一面上，让一条轴接近 ±1 g；否则设备无法判断这是第几面"
                        } else {
                            "姿态可识别；保持静止，设备会用 30 Hz 采样自己判定静止"
                        },
                        color = if (predictedFace == CalibFaces.NONE) Amber else TextDim,
                        fontSize = 12.sp,
                    )
                }
                Spacer(Modifier.height(6.dp))
                ValueBar(
                    label = "静止余量",
                    value = calib.accelRestPct.toFloat(),
                    max = 100f,
                    color = if (calib.accelRestPct >= 70) Green else Amber,
                    valueText = "${calib.accelRestPct} %",
                )
                ValueBar(
                    label = "本面采集进度",
                    value = calib.accelProgressPermille.toFloat(),
                    max = 1000f,
                    color = Cyan,
                    valueText = String.format(Locale.US, "%.1f %%", calib.accelProgressPermille / 10f),
                )
                MetricRow("固件状态", faceStateText(calib.accelFaceState))
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onCancel, modifier = Modifier.fillMaxWidth()) {
                    Text("取消本次采集")
                }
            }

            CalibPhase.SAVING -> {
                Text("正在计算系数并写入设备 flash…", color = Cyan, fontSize = 12.sp)
            }

            CalibPhase.REVIEW, CalibPhase.SAVED, CalibPhase.FAILED, CalibPhase.IDLE -> {
                if (calib.accelMessage.isNotEmpty()) {
                    Text(
                        calib.accelMessage,
                        color = if (calib.accelPhase == CalibPhase.FAILED) Red else TextDim,
                        fontSize = 12.sp,
                    )
                    Spacer(Modifier.height(6.dp))
                }

                Text("六面进度（自动识别）", color = TextDim, fontSize = 12.sp)
                Spacer(Modifier.height(4.dp))
                for (position in 0 until CalibCodec.POSITION_COUNT) {
                    val captured = (calib.accelFaceMask and (1 shl position)) != 0
                    FaceRow(position = position, captured = captured)
                }

                if (calib.accelDetectedFace != CalibFaces.NONE) {
                    Spacer(Modifier.height(4.dp))
                    MetricRow(
                        "设备最近识别",
                        CalibFaces.label(calib.accelDetectedFace),
                        Green,
                    )
                }

                Spacer(Modifier.height(8.dp))
                Button(
                    onClick = onCapturePose,
                    enabled = !calib.busy,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("采集当前姿态（自动识别）") }
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(
                        onClick = onCaptureQuick,
                        enabled = !calib.busy,
                        modifier = Modifier.weight(1f),
                    ) { Text("单面快速", fontSize = 12.sp) }
                    Button(
                        onClick = onFinish,
                        enabled = !calib.busy &&
                            (CalibJudge.allSixFaces(calib.accelFaceMask) ||
                                calib.accelResult?.quickMode == true),
                        modifier = Modifier.weight(1f),
                    ) { Text("计算并保存", fontSize = 12.sp) }
                }

                calib.accelResult?.let { result ->
                    Spacer(Modifier.height(10.dp))
                    AccelResultBlock(result)
                    calib.accelVerdict?.let { VerdictBlock(it) }
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = onClear,
                        enabled = !calib.busy,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("清除校准（恢复默认值）") }
                }
            }
        }
    }
}

@Composable
private fun MagCalCard(
    calib: CalibState,
    magNormUt: Float,
    onStart: () -> Unit,
    onFinish: () -> Unit,
    onCancel: () -> Unit,
    onClear: () -> Unit,
) {
    CardBlock(
        title = "磁力计 · 硬铁/软铁",
        hint = "远离磁铁、扬声器和金属桌面；手持设备缓慢画 8 字，把三条轴都翻一遍。",
    ) {
        when (calib.magPhase) {
            CalibPhase.UNSUPPORTED -> {
                Text(
                    "固件未提供 MAG_CAL（0x41）。校准数学在固件侧，App 不自行积分。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
            }

            CalibPhase.COLLECTING -> {
                ValueBar(
                    label = "采集进度",
                    value = calib.magProgressPermille.toFloat(),
                    max = 1000f,
                    color = Cyan,
                    valueText = String.format(Locale.US, "%.1f %%", calib.magProgressPermille / 10f),
                )
                Spacer(Modifier.height(4.dp))
                for (axis in 0 until 3) {
                    val coverage = calib.magCoveragePct.getOrElse(axis) { 0 }
                    ValueBar(
                        label = "${axisName(axis)} 轴覆盖",
                        value = coverage.toFloat(),
                        max = 100f,
                        color = when {
                            coverage >= CalibJudge.MAG_COVERAGE_MIN_PCT -> Green
                            coverage >= 50 -> Amber
                            else -> Red
                        },
                        valueText = "$coverage %",
                    )
                }
                MetricRow("样本数", "${calib.magSampleCount}")
                MetricRow("当前磁场模长", String.format(Locale.US, "%.1f µT（未校准原始值）", magNormUt))
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(
                        onClick = onCancel,
                        modifier = Modifier.weight(1f),
                    ) { Text("取消", fontSize = 12.sp) }
                    Button(
                        onClick = onFinish,
                        enabled = !calib.busy &&
                            calib.magCoveragePct.minOrNull()
                                ?.let { it >= CalibJudge.MAG_COVERAGE_MIN_PCT } == true,
                        modifier = Modifier.weight(1f),
                    ) { Text("完成并计算", fontSize = 12.sp) }
                }
            }

            CalibPhase.SAVING -> {
                Text("正在计算系数并写入设备 flash…", color = Cyan, fontSize = 12.sp)
            }

            CalibPhase.REVIEW, CalibPhase.SAVED, CalibPhase.FAILED, CalibPhase.IDLE -> {
                if (calib.magMessage.isNotEmpty()) {
                    Text(
                        calib.magMessage,
                        color = if (calib.magPhase == CalibPhase.FAILED) Red else TextDim,
                        fontSize = 12.sp,
                    )
                    Spacer(Modifier.height(6.dp))
                }
                calib.magResult?.let { result ->
                    MagResultBlock(result)
                    calib.magVerdict?.let { VerdictBlock(it) }
                    Spacer(Modifier.height(8.dp))
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onStart,
                        enabled = !calib.busy,
                        modifier = Modifier.weight(1f),
                    ) { Text("开始采集", fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = onClear,
                        enabled = !calib.busy && (calib.magResult?.fromStore == true),
                        modifier = Modifier.weight(1f),
                    ) { Text("清除校准", fontSize = 12.sp) }
                }
            }
        }
    }
}

/**
 * Barometric baseline card.
 *
 * `BARO_BASELINE` (0x42) has no query variant - action 0 means "take the pressure
 * you measure right now" - so the live state comes from the `0x04` telemetry packet
 * and the buttons are disabled while there is no valid pressure or while the
 * firmware answered NOT_SUPPORTED (no BMP388 / no TPG compensation).
 *
 * Action 2 replaces the "relative to where the button was pressed" reference with a
 * sea-level pressure derived from the device's own GNSS height: after it the
 * barometer keeps reporting an absolute altitude through a GNSS outage, while
 * `rel_alt_mm` still starts at 0 at the calibration point.
 */
@Composable
private fun BaroBaselineCard(
    baro: BaroStatus,
    gnss: GnssStatus,
    onTakeCurrent: suspend () -> BaroBaselineResult,
    onSetManual: suspend (Long) -> BaroBaselineResult,
    onCalibrateFromGnss: suspend () -> BaroBaselineResult,
) {
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf(false) }
    var unsupported by remember { mutableStateOf(false) }
    var lastOk by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf("") }
    var manualDialog by remember { mutableStateOf(false) }

    fun applyResult(result: BaroBaselineResult) {
        busy = false
        unsupported = !result.supported
        lastOk = result.ok
        message = result.message
    }

    val canOperate = baro.valid && !unsupported && !busy

    // Client-side guard only, mirroring the conditions the wire document lists for
    // action 2 (fix, ≥ 8 satellites, pDOP ≤ 2.0, speed < 0.3 m/s = 1.08 km/h). The
    // device is still the authority and adds the vertical accuracy it knows.
    val gnssUsable = gnss.fix &&
        gnss.sats >= 8 &&
        gnss.pdop <= 2.0 &&
        gnss.speedKmh < 1.08

    CardBlock(
        title = "气压基准 · BARO_BASELINE",
        hint = "相对高度 = 当前气压 − 基准气压，由固件计算；基准存在设备里，App 只负责设置。",
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            StatusPill(
                text = if (baro.valid) "气压有效" else "无有效气压",
                color = if (baro.valid) Green else TextDim,
                filled = baro.valid,
            )
            StatusPill(
                text = when {
                    unsupported -> "0x42 不可用"
                    !baro.baselineValid -> "未设基准"
                    baro.baselineAuto -> "基准：固件自动"
                    else -> "基准：手动"
                },
                color = if (baro.baselineValid && !unsupported) Cyan else TextDim,
                filled = baro.baselineValid && !unsupported,
            )
        }
        Spacer(Modifier.height(6.dp))
        MetricRow(
            "气压",
            if (baro.valid) String.format(Locale.US, "%.1f hPa", baro.pressureHpa) else "—",
            if (baro.valid) Cyan else TextDim,
        )
        if (baro.tempValid) {
            MetricRow("温度", String.format(Locale.US, "%.1f °C", baro.temperatureC))
        } else {
            MetricRow("温度", "—", TextDim)
        }
        MetricRow(
            "相对高度",
            if (baro.baselineValid) String.format(Locale.US, "%+.1f m", baro.relAltM) else "—",
            if (baro.baselineValid) Cyan else TextDim,
        )
        MetricRow(
            "基准气压",
            if (baro.baselineValid) String.format(Locale.US, "%.1f hPa", baro.baselineHpa) else "—",
            if (baro.baselineValid) Cyan else TextDim,
        )
        if (baro.baselineValid) {
            MetricRow(
                "基准来源",
                if (baro.baselineAuto) "固件自动（GNSS 静止）" else "取当前值 / 手动输入",
            )
            ValueBar(
                label = "偏离基准（量程 ±100 m）",
                value = abs(baro.relAltM),
                max = 100f,
                color = Cyan,
                valueText = String.format(Locale.US, "%+.1f m", baro.relAltM),
            )
        } else {
            MetricRow("基准来源", "—", TextDim)
        }

        Spacer(Modifier.height(6.dp))
        MetricRow(
            "GNSS 海拔",
            if (gnss.fix) {
                String.format(Locale.US, "%.1f m（%d 星）", gnss.altitudeM, gnss.sats)
            } else {
                "— 无固定"
            },
            if (gnss.fix) Cyan else TextDim,
        )
        MetricRow(
            "气压海拔（MSL）",
            if (baro.mslValid) String.format(Locale.US, "%.1f m", baro.altMslM) else "—",
            if (baro.mslValid) Cyan else TextDim,
        )
        if (baro.mslValid) {
            MetricRow(
                "差值（气压 − GNSS）",
                if (gnss.fix) {
                    String.format(Locale.US, "%+.1f m", baro.altMslM - gnss.altitudeM.toFloat())
                } else {
                    "— 无固定"
                },
                if (gnss.fix && abs(baro.altMslM - gnss.altitudeM.toFloat()) <= MSL_AGREE_M) {
                    Green
                } else {
                    Amber
                },
            )
        }

        if (message.isNotEmpty()) {
            Spacer(Modifier.height(6.dp))
            Text(
                message,
                color = when {
                    unsupported -> Red
                    lastOk -> Green
                    else -> Amber
                },
                fontSize = 12.sp,
            )
        }
        if (unsupported) {
            Spacer(Modifier.height(4.dp))
            Text(
                "固件返回 03 NOT_SUPPORTED：BMP388 未应答，或 TPG 补偿不可用。" +
                    "没有 Pa 就没有相对高度，按钮保持禁用。",
                color = Red,
                fontSize = 11.sp,
            )
        } else if (!baro.valid) {
            Spacer(Modifier.height(4.dp))
            Text(
                "还没有收到有效气压（Telemetry 0x04 flags.bit0 = 0）。BARO 包只在 " +
                    "Telemetry 特征被订阅时以 1 Hz 下发，没有 Pa 时不能取基准。",
                color = Amber,
                fontSize = 11.sp,
            )
        } else if (!gnssUsable) {
            Spacer(Modifier.height(4.dp))
            Text(
                "没有可用的 GNSS 高度（需要 fix、卫星 ≥ 8、pDOP ≤ 2.0、地速 < 0.3 m/s，" +
                    "固件还会检查垂直精度），“用 GNSS 海拔校准”因此保持禁用。",
                color = Amber,
                fontSize = 11.sp,
            )
        }

        Spacer(Modifier.height(8.dp))
        Button(
            onClick = {
                busy = true
                message = ""
                scope.launch { applyResult(onTakeCurrent()) }
            },
            enabled = canOperate,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("取当前为基准") }
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
            onClick = {
                busy = true
                message = ""
                scope.launch { applyResult(onCalibrateFromGnss()) }
            },
            enabled = canOperate && gnssUsable,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("用 GNSS 海拔校准") }
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
            onClick = { manualDialog = true },
            enabled = canOperate,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("手动输入基准气压…") }
        Spacer(Modifier.height(4.dp))
        Text(
            "用 GNSS 海拔校准后，基准是按当前 GNSS 高度换算出的海平面气压，" +
                "之后即使 GNSS 中断，气压计仍给出绝对海拔（海拔 MSL）；" +
                "相对高度（rel_alt）仍然从校准这一刻的 0 起算。",
            color = TextDim,
            fontSize = 11.sp,
        )
    }

    if (manualDialog) {
        BaroManualDialog(
            current = if (baro.valid) baro.pressureHpa else 0f,
            busy = busy,
            onDismiss = { manualDialog = false },
            onApply = { hpa ->
                manualDialog = false
                busy = true
                message = ""
                scope.launch { applyResult(onSetManual(CalibCodec.hpaToPaQ4(hpa))) }
            },
        )
    }
}

/**
 * Manual baseline dialog. The field is in **hPa** (300.0..1250.0), the unit printed
 * on every weather report; it is converted to the wire unit (Pa × 16) only when the
 * button is pressed.
 */
@Composable
private fun BaroManualDialog(
    current: Float,
    busy: Boolean,
    onDismiss: () -> Unit,
    onApply: (Float) -> Unit,
) {
    var text by remember {
        mutableStateOf(if (current > 0f) String.format(Locale.US, "%.1f", current) else "")
    }
    val value = text.trim().replace(',', '.').toFloatOrNull()
    val inRange = value != null && value >= BARO_BASELINE_HPA_MIN && value <= BARO_BASELINE_HPA_MAX

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("手动设置气压基准", fontSize = 15.sp) },
        text = {
            Column {
                Text(
                    "输入站点/海平面气压，单位 hPa（1 hPa = 100 Pa）。",
                    color = TextDim,
                    fontSize = 12.sp,
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    label = { Text("基准气压（hPa）") },
                    placeholder = { Text("例如 1013.2") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(6.dp))
                Text(
                    "范围 300.0..1250.0 hPa；发送时换算成 u32 的 Pa × 16，" +
                        "超出范围固件会回 01 INVALID_ARGUMENT。",
                    color = TextDim,
                    fontSize = 11.sp,
                )
                if (text.isNotBlank() && !inRange) {
                    Text("不是数字或超出范围", color = Amber, fontSize = 11.sp)
                }
            }
        },
        confirmButton = {
            Button(
                onClick = { value?.let(onApply) },
                enabled = inRange && !busy,
            ) { Text("写入基准") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("取消") }
        },
    )
}

@Composable
private fun FaceRow(
    position: Int,
    captured: Boolean,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = CalibFaces.label(position),
            color = if (captured) Green else TextDim,
            fontSize = 12.sp,
            modifier = Modifier.weight(1f),
        )
        StatusPill(
            text = if (captured) "已采" else "未采",
            color = if (captured) Green else TextDim,
            filled = captured,
        )
    }
}

@Composable
private fun AccelResultBlock(result: AccelCalResult) {
    Text("加速度计系数", color = TextDim, fontSize = 12.sp)
    for (axis in 0 until 3) {
        MetricRow(
            "${axisName(axis)} 轴零偏",
            CalibFormat.microG(result.biasUg.getOrElse(axis) { 0 }),
        )
    }
    for (axis in 0 until 3) {
        MetricRow(
            "${axisName(axis)} 轴刻度",
            CalibFormat.ppm(result.scalePpm.getOrElse(axis) { 0 }),
        )
    }
    for (axis in 0 until 3) {
        MetricRow(
            "陀螺 ${axisName(axis)} 轴零偏",
            CalibFormat.milliDps(result.gyroBiasMdps.getOrElse(axis) { 0 }),
        )
    }
    MetricRow("已采面", faceMaskText(result.faceMask), if (result.quickMode) Amber else TextDim)
    if (result.quickMode) {
        Text("单面快速模式：只修正当前姿态的偏置，精度低于六面法。", color = Amber, fontSize = 11.sp)
    }
    MetricRow(
        "来源",
        if (result.fromStore) "设备 flash 中的记录" else "尚未保存（仅本次生效）",
        if (result.fromStore) Green else Amber,
    )
}

@Composable
private fun MagResultBlock(result: MagCalResult) {
    Text("磁力计系数", color = TextDim, fontSize = 12.sp)
    for (axis in 0 until 3) {
        MetricRow(
            "${axisName(axis)} 轴硬铁",
            CalibFormat.magLsb(result.biasLsb.getOrElse(axis) { 0 }),
        )
    }
    for (axis in 0 until 3) {
        MetricRow(
            "${axisName(axis)} 轴刻度",
            CalibFormat.ppm(result.scalePpm.getOrElse(axis) { 0 }),
        )
    }
    MetricRow(
        "轴覆盖 X/Y/Z",
        result.coveragePct.joinToString(" / ") { "$it %" },
    )
    MetricRow("样本数", "${result.sampleCount}")
    MetricRow(
        "来源",
        if (result.fromStore) "设备 flash 中的记录" else "尚未保存（仅本次生效）",
        if (result.fromStore) Green else Amber,
    )
}

@Composable
private fun VerdictBlock(verdict: CalibVerdict) {
    if (verdict.ok) {
        Text("判定：合格，可以保存。", color = Green, fontSize = 12.sp)
        return
    }
    Text("判定：建议重做", color = Amber, fontSize = 12.sp)
    for (note in verdict.notes) {
        Text("· $note", color = Amber, fontSize = 11.sp)
    }
}

private fun axisName(axis: Int): String = when (axis) {
    0 -> "X"
    1 -> "Y"
    else -> "Z"
}

private fun faceStateText(state: Int): String = when (state) {
    AccelFaceStateCode.WAIT_REST -> "等待静止"
    AccelFaceStateCode.COLLECTING -> "采集中"
    AccelFaceStateCode.FACE_DONE -> "本面完成"
    AccelFaceStateCode.JITTER_TIMEOUT -> "抖动超时"
    else -> "未知($state)"
}

private fun faceMaskText(mask: Int): String =
    (0 until CalibCodec.POSITION_COUNT)
        .map { if ((mask and (1 shl it)) != 0) CalibFaces.shortLabel(it) else "—" }
        .joinToString(" ")

private fun gravityVectorText(gravityG: List<Float>): String = String.format(
    Locale.US,
    "重力向量 X=%+.2f Y=%+.2f Z=%+.2f g",
    gravityG.getOrElse(0) { 0f },
    gravityG.getOrElse(1) { 0f },
    gravityG.getOrElse(2) { 0f },
)

/**
 * hPa window the wizard accepts for a manual baseline, i.e. the firmware's
 * 30000..125000 Pa (`BLE_Wire_v1.md` §6.5) divided by 100.
 */
private const val BARO_BASELINE_HPA_MIN = 300f
private const val BARO_BASELINE_HPA_MAX = 1250f

/**
 * Baro-vs-GNSS height difference the card paints green. GNSS vertical accuracy is
 * metres, so this is a sanity band for "the calibration did something plausible",
 * not a pass/fail gate.
 */
private const val MSL_AGREE_M = 15f
