package com.lifegps.app.debug

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ble.GnssCfgKey
import com.lifegps.app.ble.GnssCfgKeys
import com.lifegps.app.ble.GnssCfgValue
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

/**
 * Whitelisted receiver settings (opcode 0x35).
 *
 * Every row shows the value the *receiver* reported after the last read or write,
 * not the value the App asked for; a rejected key or a modul that is not powered
 * shows its error in place of a number. The key table lives in
 * `com.lifegps.app.ble.GnssCfgKeys` and mirrors the firmware whitelist.
 */
@Composable
fun GnssConfigScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
) {
    val cfg by repo.gnssCfg.collectAsStateWithLifecycle()
    var editing by remember { mutableStateOf<GnssCfgKey?>(null) }
    var persist by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        if (cfg.isEmpty()) repo.refreshGnssCfg()
    }

    DebugScaffold(
        title = "GNSS 设置",
        onBack = onBack,
        actions = {
            TextButton(onClick = { repo.refreshGnssCfg() }) { Text("读取", color = Cyan) }
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
                CardBlock("写入策略") {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = persist, onCheckedChange = { persist = it })
                        Text(
                            "同时写入接收机 Flash（掉电后仍生效，消耗一次擦写）",
                            color = TextMain,
                            fontSize = 12.sp,
                        )
                    }
                    Text(
                        "默认写入 RAM + BBR：跨 GNSS 断电保留，但整机完全掉电（V_BCKP 断）后回到出厂值。" +
                            "所有数值都来自接收机回读，不是 App 记住的请求值。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item { SectionHeader("解算参数", "键 ID 与范围见 BLE_Wire_v1.md §6.6") }
            item {
                CardBlock {
                    CfgRow(GnssCfgKeys.DYNMODEL, cfg[GnssCfgKeys.DYNMODEL.key]) { editing = it }
                    CfgRow(GnssCfgKeys.MINELEV, cfg[GnssCfgKeys.MINELEV.key]) { editing = it }
                    CfgRow(GnssCfgKeys.MINCNO, cfg[GnssCfgKeys.MINCNO.key]) { editing = it }
                    CfgRow(GnssCfgKeys.PDOP, cfg[GnssCfgKeys.PDOP.key]) { editing = it }
                    CfgRow(GnssCfgKeys.RATE_MEAS, cfg[GnssCfgKeys.RATE_MEAS.key]) { editing = it }
                    CfgRow(GnssCfgKeys.RATE_NAV, cfg[GnssCfgKeys.RATE_NAV.key]) { editing = it }
                }
            }

            item { SectionHeader("星座开关", "关掉一个星座会减少可见卫星数，也会降低功耗") }
            item {
                CardBlock {
                    GnssCfgKeys.SIGNALS.forEach { key ->
                        CfgRow(key, cfg[key.key]) { editing = it }
                    }
                    Text(
                        "MAX-M10S 的北斗 B1I 不能与北斗 B1C 或 GLONASS L1OF 同时开启（集成手册 §2.1.3），" +
                            "接收机会直接回 ACK-NAK。所以开 GLONASS 是一个顺序：先把「北斗 B1I」关掉，" +
                            "再开「北斗 B1C」，最后开「GLONASS 星座 / L1」；每一步本身都必须是合法组合。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item { SectionHeader("功耗与运动检测", "CFG-PM / CFG-MOT：接收机自己的省电与静止判定") }
            item {
                CardBlock {
                    CfgRow(GnssCfgKeys.PM_OPERATEMODE, cfg[GnssCfgKeys.PM_OPERATEMODE.key]) { editing = it }
                    CfgRow(GnssCfgKeys.PM_POSUPDATEPERIOD, cfg[GnssCfgKeys.PM_POSUPDATEPERIOD.key]) { editing = it }
                    CfgRow(GnssCfgKeys.PM_ACQPERIOD, cfg[GnssCfgKeys.PM_ACQPERIOD.key]) { editing = it }
                    CfgRow(GnssCfgKeys.PM_ONTIME, cfg[GnssCfgKeys.PM_ONTIME.key]) { editing = it }
                    CfgRow(GnssCfgKeys.PM_DONOTENTEROFF, cfg[GnssCfgKeys.PM_DONOTENTEROFF.key]) { editing = it }
                    CfgRow(GnssCfgKeys.MOT_GNSSSPEED_THRS, cfg[GnssCfgKeys.MOT_GNSSSPEED_THRS.key]) { editing = it }
                    CfgRow(GnssCfgKeys.MOT_GNSSDIST_THRS, cfg[GnssCfgKeys.MOT_GNSSDIST_THRS.key]) { editing = it }
                    Text(
                        "周期类字段只在「功耗模式」为 PSMOO/PSMCT 时才起作用；FULL 下接收机连续跟踪。" +
                            "静止判定两项与固件 PDR 的 ZUPT 判据同源，0 表示用出厂默认值。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }

            item { SectionHeader("星历加速", "APO = AssistNow Autonomous：接收机自己预测轨道") }
            item {
                CardBlock {
                    CfgRow(GnssCfgKeys.ANA_USE, cfg[GnssCfgKeys.ANA_USE.key]) { editing = it }
                    CfgRow(GnssCfgKeys.ANA_ORBMAXERR, cfg[GnssCfgKeys.ANA_ORBMAXERR.key]) { editing = it }
                    Text(
                        "开启后预测轨道存在接收机的 BBR（本板 V_BCKP 常供），跨 GNSS 断电保留，" +
                            "下次冷启动可直接用，不需要联网。",
                        color = TextDim,
                        fontSize = 11.sp,
                    )
                }
            }
        }
    }

    editing?.let { key ->
        CfgEditDialog(
            key = key,
            current = cfg[key.key],
            persist = persist,
            onDismiss = { editing = null },
            onApply = { value ->
                repo.setGnssCfg(key.key, value, persist)
                editing = null
            },
        )
    }
}

@Composable
private fun CfgRow(key: GnssCfgKey, value: GnssCfgValue?, onEdit: (GnssCfgKey) -> Unit) {
    val (text, color) = when {
        value == null -> "读取中…" to TextDim
        value.error != null -> value.error to Red
        key.isBoolean -> if (value.value != 0L) "开" to Green else "关" to TextDim
        else -> {
            val named = key.choices.firstOrNull { it.first.toLong() == value.value }?.second
            val shown = if (named != null) "$named (${value.value})" else "${value.value}"
            (if (key.unit.isEmpty()) shown else "$shown ${key.unit}") to TextMain
        }
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(key.label, color = TextDim, fontSize = 13.sp)
            Text(
                text = text,
                color = color,
                fontSize = 13.sp,
                fontFamily = FontFamily.Monospace,
            )
        }
        TextButton(onClick = { onEdit(key) }) { Text("改", color = Cyan) }
    }
}

@Composable
private fun CfgEditDialog(
    key: GnssCfgKey,
    current: GnssCfgValue?,
    persist: Boolean,
    onDismiss: () -> Unit,
    onApply: (Long) -> Unit,
) {
    val initial = current?.value?.toString() ?: ""
    var text by remember { mutableStateOf(initial) }
    var toggle by remember { mutableStateOf((current?.value ?: 0L) != 0L) }
    val parsed = text.trim().toLongOrNull()
    val inRange = parsed != null && parsed >= key.min && parsed <= key.max

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(key.label, fontSize = 15.sp) },
        text = {
            Column {
                Text(
                    "当前值：" + (current?.let { if (it.error != null) it.error else it.value.toString() } ?: "—"),
                    color = TextDim,
                    fontSize = 12.sp,
                )
                Text(
                    "范围 ${key.min}..${key.max} ${key.unit} · 键 ID 0x%08X".format(key.key),
                    color = TextDim,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                )
                Spacer(Modifier.height(8.dp))
                if (key.isBoolean) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Switch(checked = toggle, onCheckedChange = { toggle = it })
                        Text(if (toggle) "  开" else "  关", color = TextMain)
                    }
                } else {
                    OutlinedTextField(
                        value = text,
                        onValueChange = { text = it },
                        label = { Text("数值") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    if (key.choices.isNotEmpty()) {
                        Spacer(Modifier.height(6.dp))
                        Text(
                            key.choices.joinToString(" / ") { "${it.first}=${it.second}" },
                            color = TextDim,
                            fontSize = 11.sp,
                        )
                    }
                    if (!inRange && text.isNotBlank()) {
                        Text("超出范围，固件也会拒绝", color = Amber, fontSize = 11.sp)
                    }
                }
                if (persist) {
                    Text("将写入接收机 Flash", color = Amber, fontSize = 11.sp)
                }
            }
        },
        confirmButton = {
            Button(
                onClick = { onApply(if (key.isBoolean) (if (toggle) 1L else 0L) else (parsed ?: 0L)) },
                enabled = key.isBoolean || inRange,
            ) { Text("写入") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("取消") }
        },
    )
}
