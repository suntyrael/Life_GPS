package com.lifegps.app.debug

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ble.GnssCodec
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain
import java.util.Locale

/**
 * GPSTest layout, one screen: the 7x2 status block, the 7-column satellite table,
 * two buttons at the bottom (NMEA, receiver settings).
 *
 * Labels, field order, units and number formats come from GPSTest
 * (`LocationCard.kt` + `do_not_translate.xml` of barbeau/gpstest):
 * `%.7f°`, `%.1f m`, `%.1f m/s`, `%.1f°`, `%.1f`, `%d/%d/%d`, `N sec`,
 * satellite columns `ID | GNSS | CF | C/N0 | Flags | Elev | Azim`, C/N0 `%.1f`,
 * `AEU` flags, `%.0f°`.
 *
 * A GPSTest field is printed as an empty string when the device does not report
 * it - never as a fake zero. Every column is filled since 0.4.0: `CF` comes from
 * NAV-SIG (the signal id behind the C/N0) and `Azim` from NAV-SAT, both reduced
 * into the 7-byte satellite row of BLE_Wire_v1.md §5.1.
 *
 * The DOP pair is `pDOP` (NAV-PVT) next to `hDOP/vDOP` (UBX-NAV-DOP, requested at
 * the same rate since 0.4.0). pDOP is not hDOP: it is never smaller, so the two
 * columns are not interchangeable.
 *
 * Two things are deliberately *not* GPSTest's: the `Time` row is rendered in the
 * product's default zone (UTC+8, labelled with the offset) instead of device-local
 * time, and the row order is used -> tracked -> unused, strongest C/N0 first
 * ([sortSatellites] in DebugModels). The old order (GNSS system, svid) re-shuffled
 * itself whenever the receiver moved a satellite in or out of the solution.
 */
@Composable
fun GnssStatusScreen(
    repo: DebugRepository,
    onBack: () -> Unit,
    onOpenNmea: () -> Unit = {},
    onOpenConfig: () -> Unit = {},
) {
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val link by repo.gnssLink.collectAsStateWithLifecycle()
    val debug by repo.gnssDebug.collectAsStateWithLifecycle()
    val device by repo.device.collectAsStateWithLifecycle()

    val fixColor = if (gnss.fix) Green else Red
    val sessionOn = debug.flags != 0
    val hint = diagnosticHint(gnss, link)

    val inView = gnss.satellites.size

    DebugScaffold(
        title = "GPS",
        onBack = onBack,
        actions = {
            StatusPill(
                text = if (gnss.fix) "FIX" else "NO FIX",
                color = fixColor,
                filled = true,
            )
            /* The one control GPSTest does not have: without a held rail the module
             * is cut 60 s after a cold start, so the table could never fill indoors.
             */
            TextButton(
                onClick = {
                    if (sessionOn) {
                        repo.setGnssDebug(0, 0)
                    } else {
                        repo.setGnssDebug(
                            GnssCodec.FLAG_HOLD_POWER or
                                GnssCodec.FLAG_NAV_SAT or
                                GnssCodec.FLAG_NMEA,
                            300,
                        )
                    }
                },
            ) {
                Text(
                    text = if (sessionOn) "会话 ${debug.remainingSeconds}s" else "保持上电",
                    color = if (sessionOn) Green else TextDim,
                    fontSize = 12.sp,
                )
            }
        },
        bottomBar = { GnssBottomBar(onOpenNmea, onOpenConfig) },
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .padding(padding),
            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            item {
                CardBlock {
                    Row(Modifier.horizontalScroll(rememberScrollState())) {
                        LabelColumn1()
                        ValueColumn1(gnss)
                        LabelColumn2()
                        ValueColumn2(gnss, inView)
                    }
                }
            }

            /* One line, only while something is actually wrong: the receive path is
             * the first thing to look at when the table stays empty, but it has no
             * business on screen while a fix is up. Full counters: NMEA 日志 page.
             */
            if (hint != null) {
                item {
                    Text(
                        text = hint,
                        color = Amber,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier.padding(horizontal = 4.dp),
                    )
                }
            }

            item {
                CardBlock {
                    if (gnss.satellites.isEmpty()) {
                        Text(
                            "还没有 NAV-SAT 行（接收机上电即 1 Hz 采集，不需要开调试会话）。" +
                                "为空只有三种原因：模组未上电 / 周视无星（室内、天线未接）/ 接收机 NAK 了输出率。",
                            color = TextDim,
                            fontSize = 11.sp,
                        )
                    } else {
                        SatHeader()
                        Spacer(Modifier.height(2.dp))
                        // Already ordered by the repository: used -> tracked -> rest,
                        // then C/N0 descending (sortSatellites in DebugModels).
                        gnss.satellites.forEach { satellite ->
                            SatRow(satellite)
                        }
                    }
                }
            }

            debug.error?.let { message ->
                item { Text(message, color = Red, fontSize = 11.sp) }
            }

            item {
                val footer = buildString {
                    if (link.ttffNote.isNotEmpty()) append(link.ttffNote).append(" · ")
                    append("卫星 ${gnss.sats} 参与解算（NAV-PVT numSV） · 设备 ${device.name}")
                }
                Text(
                    text = footer,
                    color = TextDim,
                    fontSize = 10.sp,
                    modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp),
                )
                Text(
                    text = "空 = 接收机未上报该值，不补 0",
                    color = TextDim,
                    fontSize = 10.sp,
                    modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp),
                )
            }
        }
    }
}

/* --- GPSTest status block ------------------------------------------------- */

@Composable
private fun LabelColumn1() {
    Column(
        modifier = Modifier.padding(top = 2.dp, bottom = 2.dp, end = 4.dp),
        horizontalAlignment = Alignment.End,
    ) {
        LocationLabel("Lat:")
        LocationLabel("Long:")
        LocationLabel("Alt:")
        LocationLabel("Alt (MSL):")
        LocationLabel("Speed:")
        LocationLabel("S. Acc:")
        LocationLabel("PDOP:")
    }
}

@Composable
private fun LabelColumn2() {
    Column(
        modifier = Modifier.padding(top = 2.dp, bottom = 2.dp, start = 10.dp, end = 4.dp),
        horizontalAlignment = Alignment.End,
    ) {
        // GPSTest prints device-local time. The receiver reports UTC and this
        // product's default zone is UTC+8, so the value is UTC+8 (see
        // BleDebugRepository.DEFAULT_UTC_OFFSET_HOURS), not the phone's zone.
        LocationLabel("Time (UTC+8):")
        LocationLabel("TTFF:")
        LocationLabel("E H/V Acc:")
        LocationLabel("# Sats:")
        LocationLabel("Bearing:")
        LocationLabel("B. Acc:")
        LocationLabel("H/V DOP:")
    }
}

@Composable
private fun ValueColumn1(gnss: GnssStatus) {
    Column(Modifier.padding(top = 2.dp, bottom = 2.dp)) {
        LocationValue(if (gnss.fix) fmtLatLon(gnss.latitude) else "")
        LocationValue(if (gnss.fix) fmtLatLon(gnss.longitude) else "")
        // Ellipsoid height (HAE) - GPSTest's "Alt", not the MSL row below.
        LocationValue(if (gnss.fix && gnss.hasAccuracy) "${fmtOne(gnss.ellipsoidAltM)} m" else "")
        LocationValue(if (gnss.fix) "${fmtOne(gnss.altitudeM)} m" else "")
        LocationValue(if (gnss.fix) "${fmtOne(speedMs(gnss))} m/s" else "")
        LocationValue(if (gnss.hasAccuracy && gnss.sAccMs > 0.0) "${fmtOne(gnss.sAccMs)} m/s" else "")
        LocationValue(if (gnss.pdop > 0.0) fmtOne(gnss.pdop) else "")
    }
}

@Composable
private fun ValueColumn2(gnss: GnssStatus, inView: Int) {
    Column(Modifier.padding(top = 2.dp, bottom = 2.dp)) {
        LocationValue(if (gnss.utcTime == "--") "" else gnss.utcTime)
        LocationValue(gnss.ttff)
        // GPSTest `%1$.1f/%2$.1f m`: horizontal first, vertical second.
        LocationValue(
            if (gnss.hasAccuracy && gnss.hAccM > 0.0 && gnss.vAccM > 0.0) {
                "${fmtOne(gnss.hAccM)}/${fmtOne(gnss.vAccM)} m"
            } else {
                ""
            },
        )
        LocationValue(numSats(gnss, inView))
        LocationValue(if (gnss.fix) "${fmtOne(gnss.headingDeg)}°" else "")
        LocationValue(if (gnss.hasAccuracy && gnss.bAccDeg > 0.0) "${fmtOne(gnss.bAccDeg)}°" else "")
        // From UBX-NAV-DOP; absent on a 0.3.0 firmware.
        LocationValue(
            if (gnss.hdop > 0.0 && gnss.vdop > 0.0) "${fmtOne(gnss.hdop)}/${fmtOne(gnss.vdop)}" else "",
        )
    }
}

@Composable
private fun LocationLabel(text: String) {
    Text(
        text = text,
        fontWeight = FontWeight.Bold,
        fontSize = 11.sp,
        color = TextDim,
        textAlign = TextAlign.End,
        modifier = Modifier.padding(vertical = 2.dp),
    )
}

@Composable
private fun LocationValue(text: String) {
    Text(
        text = text,
        fontSize = 12.sp,
        fontFamily = FontFamily.Monospace,
        color = TextMain,
        modifier = Modifier.padding(vertical = 2.dp),
    )
}

/* --- GPSTest satellite table --------------------------------------------- */

@Composable
private fun SatHeader() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SatCell("ID", 0.5f, TextAlign.Start)
        SatCell("GNSS", 1.0f, TextAlign.Start)
        SatCell("CF", 0.5f, TextAlign.Start)
        SatCell("C/N0", 0.8f, TextAlign.Start)
        SatCell("Flags", 0.8f, TextAlign.Start)
        SatCell("Elev", 0.7f, TextAlign.Start)
        SatCell("Azim", 0.7f, TextAlign.Start)
    }
}

@Composable
private fun SatRow(satellite: SatelliteInfo) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 1.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SatCell("${satellite.svid}", 0.5f, TextAlign.Start)
        SatCell(satellite.system, 1.0f, TextAlign.Start)
        SatCell(satellite.carrier, 0.5f, TextAlign.Start)
        SatCell(if (satellite.cno > 0) fmtOne(satellite.cno.toDouble()) else "", 0.8f, TextAlign.Start)
        // GPSTest's AEU: almanac, ephemeris, used. All three are on the wire since
        // v1.2 (NAV-SAT flags bits 12/11/3).
        SatCell(aeu(satellite), 0.8f, TextAlign.Start)
        SatCell(if (satellite.elevation >= 0) "${satellite.elevation}°" else "", 0.7f, TextAlign.Start)
        SatCell(if (satellite.azimuth >= 0) "${satellite.azimuth}°" else "", 0.7f, TextAlign.Start)
    }
}

@Composable
private fun RowScope.SatCell(text: String, weight: Float, align: TextAlign) {
    Text(
        text = text,
        modifier = Modifier.weight(weight),
        color = TextMain,
        fontSize = 12.sp,
        fontFamily = FontFamily.Monospace,
        textAlign = align,
        maxLines = 1,
    )
}

/* --- helpers ------------------------------------------------------------- */

/** GPSTest `AEU`: position 0 almanac, 1 ephemeris, 2 used; space when unknown. */
private fun aeu(satellite: SatelliteInfo): String {
    val flags = CharArray(3) { ' ' }
    flags[0] = if (satellite.almAvail) 'A' else ' '
    flags[1] = if (satellite.ephAvail) 'E' else ' '
    flags[2] = if (satellite.used) 'U' else ' '
    return String(flags)
}

/** GPSTest `%1$d/%2$d/%3$d`: used / in view / total. */
private fun numSats(gnss: GnssStatus, inView: Int): String {
    if (gnss.satTotal <= 0 && inView <= 0) return ""
    val total = if (gnss.satTotal > 0) gnss.satTotal else inView
    // No filter on this side, so "total" is just "in view" and the third number
    // would repeat the second. GPSTest needs it only to count filtered-out rows.
    return if (total != inView) "${gnss.sats}/$inView/$total" else "${gnss.sats}/$inView"
}

private fun speedMs(gnss: GnssStatus): Double = gnss.speedKmh / 3.6

/**
 * The receive path in one line, or null when the link is healthy.
 *
 * Same five causes the old counter card spelled out, in the order they can be
 * proven: no telemetry -> rail off -> NMEA only -> UBX but no satellites.
 */
private fun diagnosticHint(gnss: GnssStatus, link: GnssLinkInfo): String? {
    if (link.received && gnss.fix && gnss.satellites.isNotEmpty()) return null
    return when {
        !link.received -> "无蓝牙遥测：0x01 组没到（检查订阅/固件是否发布该组）"
        link.rxBytes == 0L -> "rx 0 B：接收机可能未上电（断电退避中），点右上角「保持上电」"
        link.ubxFrames == 0L ->
            "rx ${link.rxBytes} B，无有效 UBX 帧：NMEA 起始 ${link.nmeaStarts}，校验错 ${link.badChecksum}，" +
                "丢字节 ${link.rxDropped}"
        else ->
            "UBX ${link.ubxFrames} 帧正常，${link.state}：卫星 ${gnss.sats}，pDOP " +
                (if (gnss.pdop > 0.0) fmtOne(gnss.pdop) else "") +
                (if (gnss.satellites.isEmpty()) "，NAV-SAT 无行（室内 / 天线未接？）" else "")
    }
}

@Composable
private fun GnssBottomBar(onOpenNmea: () -> Unit, onOpenConfig: () -> Unit) {
    Surface(color = MaterialTheme.colorScheme.background, tonalElevation = 3.dp) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(horizontal = 12.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedButton(onClick = onOpenNmea, modifier = Modifier.weight(1f)) {
                Text("NMEA 日志")
            }
            OutlinedButton(onClick = onOpenConfig, modifier = Modifier.weight(1f)) {
                Text("GPS 设置")
            }
        }
    }
}

/** GPSTest `lat_or_lon` = `%1$.7f°`. */
private fun fmtLatLon(value: Double): String = String.format(Locale.US, "%.7f°", value)

private fun fmtOne(value: Double): String = String.format(Locale.US, "%.1f", value)
