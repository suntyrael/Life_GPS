// SPDX-License-Identifier: MIT
package com.lifegps.app.prod

import android.bluetooth.BluetoothManager
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.AppContainer
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain
import io.runtime.mcumgr.ble.McuMgrBleTransport
import io.runtime.mcumgr.dfu.FirmwareUpgradeCallback
import io.runtime.mcumgr.dfu.FirmwareUpgradeController
import io.runtime.mcumgr.dfu.mcuboot.FirmwareUpgradeManager
import io.runtime.mcumgr.exception.McuMgrException
import java.security.MessageDigest
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Parsed MCUboot image header (`zephyr.signed.bin`). */
data class McubootImageInfo(
    val fileName: String,
    val bytes: ByteArray,
    val versionLabel: String,
    val sha256Short: String,
)

private const val MCUBOOT_MAGIC: Long = 0x96F3B83DL
private const val OTA_MIN_BYTES = 4 * 1024
private const val OTA_MAX_BYTES = 720 * 1024

internal fun parseMcubootSignedImage(fileName: String, raw: ByteArray): Result<McubootImageInfo> {
    if (raw.size < OTA_MIN_BYTES || raw.size > OTA_MAX_BYTES) {
        return Result.failure(
            IllegalArgumentException("固件体积异常（${raw.size} B），需在 4 KB ~ 720 KB 之间"),
        )
    }
    val magic =
        (raw[0].toLong() and 0xFF) or
            ((raw[1].toLong() and 0xFF) shl 8) or
            ((raw[2].toLong() and 0xFF) shl 16) or
            ((raw[3].toLong() and 0xFF) shl 24)
    if (magic != MCUBOOT_MAGIC) {
        return Result.failure(
            IllegalArgumentException(
                String.format(
                    Locale.US,
                    "不是合法的 MCUboot 签名固件（魔数 0x%08X != 0x96F3B83D），请选择 zephyr.signed.bin",
                    magic,
                ),
            ),
        )
    }
    val major = raw[20].toInt() and 0xFF
    val minor = raw[21].toInt() and 0xFF
    val rev = (raw[22].toInt() and 0xFF) or ((raw[23].toInt() and 0xFF) shl 8)
    val build =
        (raw[24].toLong() and 0xFF) or
            ((raw[25].toLong() and 0xFF) shl 8) or
            ((raw[26].toLong() and 0xFF) shl 16) or
            ((raw[27].toLong() and 0xFF) shl 24)
    val versionLabel = if (build > 0L) "$major.$minor.$rev+$build" else "$major.$minor.$rev"

    val digest = MessageDigest.getInstance("SHA-256").digest(raw)
    val shaShort = digest.take(8).joinToString("") { b -> String.format(Locale.US, "%02x", b) }

    return Result.success(
        McubootImageInfo(
            fileName = fileName,
            bytes = raw,
            versionLabel = versionLabel,
            sha256Short = shaShort,
        ),
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OtaUpdateScreen(
    container: AppContainer,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val repo = container.repo
    val device by repo.device.collectAsStateWithLifecycle()
    val lastAddress by repo.lastDeviceAddress.collectAsStateWithLifecycle()

    var selectedImage by remember { mutableStateOf<McubootImageInfo?>(null) }
    var parseError by remember { mutableStateOf<String?>(null) }
    var ignoreBatteryCheck by remember { mutableStateOf(false) }

    var otaRunning by remember { mutableStateOf(false) }
    var otaStage by remember { mutableStateOf("等待选择固件文件") }
    var otaProgress by remember { mutableFloatStateOf(0f) }
    var otaSpeedKbps by remember { mutableFloatStateOf(0f) }
    var otaStartMs by remember { mutableLongStateOf(0L) }
    var otaResultMessage by remember { mutableStateOf<String?>(null) }
    var otaErrorMessage by remember { mutableStateOf<String?>(null) }

    var activeTransport by remember { mutableStateOf<McuMgrBleTransport?>(null) }
    var activeController by remember { mutableStateOf<FirmwareUpgradeController?>(null) }

    DisposableEffect(Unit) {
        onDispose {
            try {
                activeController?.cancel()
            } catch (_: Throwable) {
            }
            try {
                activeTransport?.release()
            } catch (_: Throwable) {
            }
        }
    }

    val filePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            val loaded = withContext(Dispatchers.IO) {
                runCatching {
                    val name = queryDisplayName(context, uri) ?: "zephyr.signed.bin"
                    val raw = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
                        ?: throw IllegalStateException("无法读取所选文件")
                    parseMcubootSignedImage(name, raw).getOrThrow()
                }
            }
            loaded.onSuccess { info ->
                selectedImage = info
                parseError = null
                otaErrorMessage = null
                otaResultMessage = null
                otaStage = "固件已校验，准备升级"
            }.onFailure { err ->
                selectedImage = null
                parseError = err.message ?: "读取固件失败"
            }
        }
    }

    val batteryOk = device.charging || device.batteryPct >= 50 || ignoreBatteryCheck
    val targetAddress = lastAddress
    val canStart = !otaRunning && selectedImage != null && targetAddress != null && batteryOk

    fun startOta() {
        val image = selectedImage ?: return
        val address = targetAddress ?: return
        val btManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        val btDevice = try {
            btManager?.adapter?.getRemoteDevice(address)
        } catch (_: IllegalArgumentException) {
            null
        }
        if (btDevice == null) {
            otaErrorMessage = "无法解析蓝牙设备地址：$address"
            return
        }

        otaRunning = true
        otaProgress = 0f
        otaSpeedKbps = 0f
        otaErrorMessage = null
        otaResultMessage = null
        otaStage = "正在切换至 SMP 通道…"

        scope.launch {
            // Release our custom GATT link first so Android BLE stack hands clean control to McuMgrBleTransport.
            repo.disconnect()
            delay(650L)

            val transport = McuMgrBleTransport(context.applicationContext, btDevice)
            activeTransport = transport
            val manager = FirmwareUpgradeManager(transport)
            manager.setMode(FirmwareUpgradeManager.Mode.TEST_AND_CONFIRM)
            manager.setCallbackOnUiThread(true)
            manager.setFirmwareUpgradeCallback(object : FirmwareUpgradeCallback<FirmwareUpgradeManager.State> {
                override fun onUpgradeStarted(controller: FirmwareUpgradeController) {
                    activeController = controller
                    otaStartMs = System.currentTimeMillis()
                    otaStage = "已连接 SMP，正在校验槽位…"
                }

                override fun onStateChanged(
                    prevState: FirmwareUpgradeManager.State,
                    newState: FirmwareUpgradeManager.State,
                ) {
                    otaStage = when (newState) {
                        FirmwareUpgradeManager.State.VALIDATE -> "校验设备槽位…"
                        FirmwareUpgradeManager.State.UPLOAD -> "正在上传镜像到内部 RRAM slot1…"
                        FirmwareUpgradeManager.State.TEST -> "标记测试启动（TEST）…"
                        FirmwareUpgradeManager.State.RESET -> "重启设备并由 MCUboot 交换镜像…"
                        FirmwareUpgradeManager.State.CONFIRM -> "重连并确认新固件（CONFIRM）…"
                        FirmwareUpgradeManager.State.SUCCESS -> "升级完成"
                        else -> newState.name
                    }
                }

                override fun onUploadProgressChanged(bytesSent: Int, imageSize: Int, timestamp: Long) {
                    if (imageSize > 0) {
                        otaProgress = (bytesSent.toFloat() / imageSize.toFloat()).coerceIn(0f, 1f)
                    }
                    val elapsedSec = ((System.currentTimeMillis() - otaStartMs).coerceAtLeast(1L)) / 1000f
                    otaSpeedKbps = (bytesSent / 1024f) / elapsedSec
                }

                override fun onUpgradeCompleted() {
                    otaRunning = false
                    otaProgress = 1f
                    otaStage = "固件升级成功"
                    otaResultMessage = "新固件 v${image.versionLabel} 已交换并确认，返回首页重新连接设备即可查看新版本。"
                    transport.release()
                    activeTransport = null
                    activeController = null
                }

                override fun onUpgradeFailed(
                    state: FirmwareUpgradeManager.State,
                    error: McuMgrException,
                ) {
                    otaRunning = false
                    otaStage = "升级失败（阶段 ${state.name}）"
                    otaErrorMessage = error.message ?: error.javaClass.simpleName
                    transport.release()
                    activeTransport = null
                    activeController = null
                }

                override fun onUpgradeCanceled(state: FirmwareUpgradeManager.State) {
                    otaRunning = false
                    otaStage = "已取消升级"
                    transport.release()
                    activeTransport = null
                    activeController = null
                }
            })

            val settings = FirmwareUpgradeManager.Settings.Builder()
                .setEstimatedSwapTime(8_000)
                .setWindowCapacity(4)
                .setMemoryAlignment(4)
                .setEraseAppSettings(false)
                .build()

            try {
                manager.start(image.bytes, settings)
            } catch (t: Throwable) {
                otaRunning = false
                otaErrorMessage = t.message ?: "启动 SMP 升级失败"
                transport.release()
                activeTransport = null
            }
        }
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text("BLE 固件升级（MCUboot OTA）", fontWeight = FontWeight.SemiBold) },
                navigationIcon = {
                    TextButton(onClick = onBack, enabled = !otaRunning) { Text("返回") }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                    titleContentColor = TextMain,
                ),
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                CardBlock {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        SectionHeader("目标设备状态")
                        StatusPill(
                            text = if (targetAddress != null) "已定位" else "未选择设备",
                            color = if (targetAddress != null) Green else Amber,
                            filled = false,
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                    MetricRow("设备名称", device.name.ifBlank { "—" })
                    MetricRow("MAC 地址", targetAddress ?: "请先在首页连接一次设备")
                    MetricRow("当前固件", device.firmware.ifBlank { "—" })
                    MetricRow(
                        "当前电量",
                        if (device.batteryPct in 0..100) {
                            "${device.batteryPct}% (${device.batteryMv} mV)${if (device.charging) " 充电中" else ""}"
                        } else {
                            "未知"
                        },
                    )
                    if (!device.charging && device.batteryPct < 50) {
                        Spacer(Modifier.height(6.dp))
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Checkbox(
                                checked = ignoreBatteryCheck,
                                onCheckedChange = { ignoreBatteryCheck = it },
                                enabled = !otaRunning,
                            )
                            Text(
                                "电量低于 50% 或未知，我确认处于开发板供电/可接受风险状态",
                                color = Amber,
                                fontSize = 12.sp,
                            )
                        }
                    }
                }
            }

            item {
                CardBlock {
                    SectionHeader("选择签名固件（zephyr.signed.bin）")
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "支持从 GitHub Actions 产物 `lifegps-firmware-prod` 或 `lifegps-firmware-debug` 中解压得到的 `zephyr.signed.bin`（内部 RRAM 双槽，兼容 32M/64M Flash，升级失败自动回滚）。",
                        color = TextDim,
                        fontSize = 12.sp,
                    )
                    Spacer(Modifier.height(10.dp))
                    OutlinedButton(
                        onClick = { filePicker.launch(arrayOf("application/octet-stream", "*/*")) },
                        enabled = !otaRunning,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(if (selectedImage == null) "选择 zephyr.signed.bin" else "重新选择固件文件")
                    }
                    parseError?.let { err ->
                        Spacer(Modifier.height(8.dp))
                        Text(err, color = Red, fontSize = 12.sp)
                    }
                    selectedImage?.let { img ->
                        Spacer(Modifier.height(8.dp))
                        MetricRow("文件名", img.fileName)
                        MetricRow("镜像版本", "v${img.versionLabel}")
                        MetricRow("体积", String.format(Locale.US, "%.1f KB (%d B)", img.bytes.size / 1024f, img.bytes.size))
                        MetricRow("SHA-256", "${img.sha256Short}…")
                    }
                }
            }

            item {
                CardBlock {
                    SectionHeader("升级进度（SMP over BLE）")
                    Spacer(Modifier.height(8.dp))
                    Text(otaStage, color = TextMain, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                    Spacer(Modifier.height(8.dp))
                    LinearProgressIndicator(
                        progress = { otaProgress },
                        modifier = Modifier.fillMaxWidth(),
                        color = Cyan,
                    )
                    Spacer(Modifier.height(6.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text(
                            String.format(Locale.US, "%.1f%%", otaProgress * 100f),
                            color = TextDim,
                            fontSize = 12.sp,
                        )
                        if (otaSpeedKbps > 0f) {
                            Text(
                                String.format(Locale.US, "%.1f KB/s", otaSpeedKbps),
                                color = Cyan,
                                fontSize = 12.sp,
                            )
                        }
                    }

                    otaResultMessage?.let { msg ->
                        Spacer(Modifier.height(8.dp))
                        Text(msg, color = Green, fontSize = 13.sp)
                    }
                    otaErrorMessage?.let { err ->
                        Spacer(Modifier.height(8.dp))
                        Text("错误：$err", color = Red, fontSize = 13.sp)
                    }

                    Spacer(Modifier.height(12.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
                        Button(
                            onClick = { startOta() },
                            enabled = canStart,
                            modifier = Modifier.weight(1f),
                        ) {
                            Text(if (otaRunning) "升级中…" else "开始 BLE OTA")
                        }
                        if (otaRunning) {
                            OutlinedButton(
                                onClick = { activeController?.cancel() },
                            ) {
                                Text("取消", color = Red)
                            }
                        }
                    }
                }
            }
        }
    }
}

private fun queryDisplayName(context: Context, uri: Uri): String? {
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) {
            val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0) return c.getString(idx)
        }
    }
    return uri.lastPathSegment
}
