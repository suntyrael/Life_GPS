package com.lifegps.app.debug

import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.EmptyHint
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.SurfaceHigh
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain
import java.util.Locale
import kotlin.math.cos

@Composable
fun MapLiveScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val gnss by repo.gnss.collectAsStateWithLifecycle()
    val trail by repo.geoTrail.collectAsStateWithLifecycle()
    val pois by repo.pois.collectAsStateWithLifecycle()
    val live by repo.liveTracking.collectAsStateWithLifecycle()

    val distance = trail.zipWithNext().sumOf { (a, b) ->
        distanceMeters(a.latitude, a.longitude, b.latitude, b.longitude)
    }

    DebugScaffold(
        title = "地图实时轨迹",
        onBack = onBack,
        actions = {
            StatusPill(if (live) "LIVE" else "PAUSED", if (live) Green else Amber, filled = true)
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
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    StatusPill("${trail.size} pts", Cyan, filled = true)
                    StatusPill("%.0f m".format(distance), Green, filled = true)
                    StatusPill("${pois.size} POI", Amber, filled = true)
                }
            }

            item {
                CardBlock("实时轨迹", "点击地图任意位置打点；当前位置始终居中") {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(330.dp)
                            .background(SurfaceHigh, RoundedCornerShape(12.dp)),
                    ) {
                        LiveTrackMap(
                            gnss = gnss,
                            trail = trail,
                            pois = pois,
                            onAddPoi = { lat, lon -> repo.addPoi(lat, lon, "地图点击打点") },
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        MapLegend("GNSS 轨迹", Cyan)
                        MapLegend("当前位置", Green)
                        MapLegend("POI", Amber)
                    }
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { repo.setLiveTracking(!live) },
                        modifier = Modifier.weight(1f),
                    ) { Text(if (live) "暂停轨迹" else "继续轨迹") }
                    OutlinedButton(
                        onClick = { repo.addPoi(gnss.latitude, gnss.longitude, "当前定位打点") },
                        modifier = Modifier.weight(1f),
                    ) { Text("当前定位打点") }
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(
                    onClick = { repo.clearGeoTrail() },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("清空实时轨迹") }
            }

            item {
                SectionHeader("当前位置", "WGS-84")
                CardBlock {
                    MetricRow("纬度", fmtSix(gnss.latitude))
                    MetricRow("经度", fmtSix(gnss.longitude))
                    MetricRow("海拔", "%.1f m".format(gnss.altitudeM))
                    MetricRow("速度", "%.1f km/h".format(gnss.speedKmh))
                    MetricRow("航向", "%.1f°".format(gnss.headingDeg))
                    MetricRow("定位", if (gnss.fix) "FIX / ${gnss.sats} sats" else "NO FIX", if (gnss.fix) Green else Red)
                }
            }

            item {
                SectionHeader("POI 打点", "点击地图或“当前定位打点”")
            }

            if (pois.isEmpty()) {
                item { EmptyHint("还没有 POI") }
            } else {
                items(pois, key = { it.id }) { poi ->
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Text(
                                        poi.label,
                                        color = Amber,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace,
                                    )
                                    Spacer(Modifier.width(6.dp))
                                    Text(poi.note, color = TextDim, fontSize = 11.sp)
                                }
                                Spacer(Modifier.height(2.dp))
                                Text(
                                    String.format(Locale.US, "%.6f, %.6f  ·  %s", poi.latitude, poi.longitude, poi.time),
                                    color = TextMain,
                                    fontSize = 11.sp,
                                    fontFamily = FontFamily.Monospace,
                                )
                            }
                            TextButton(onClick = { repo.removePoi(poi.id) }) {
                                Text("删除", color = Red)
                            }
                        }
                    }
                }
            }

            item {
                Text(
                    "当前是 Canvas 实时轨迹占位：用 GNSS 经纬度实时画线 + 点击打点。下一步替换为 MapLibre 瓦片，轨迹/POI 数据模型与交互保持不变。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(top = 6.dp, bottom = 20.dp),
                )
            }
        }
    }
}

@Composable
private fun LiveTrackMap(
    gnss: GnssStatus,
    trail: List<GeoPoint>,
    pois: List<PoiMarker>,
    onAddPoi: (Double, Double) -> Unit,
) {
    val centerLat = gnss.latitude
    val centerLon = gnss.longitude
    val metersPerPixel = 1.2
    val metersPerDegLat = 111_320.0
    val metersPerDegLon = 111_320.0 * cos(Math.toRadians(centerLat))

    Canvas(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(centerLat, centerLon) {
                detectTapGestures { offset ->
                    val dLon = (offset.x - size.width / 2f) * metersPerPixel / metersPerDegLon
                    val dLat = -(offset.y - size.height / 2f) * metersPerPixel / metersPerDegLat
                    onAddPoi(centerLat + dLat, centerLon + dLon)
                }
            },
    ) {
        val cx = size.width / 2f
        val cy = size.height / 2f

        val gridColor = Color(0xFF23324D)
        var gx = cx % 60f
        while (gx < size.width) {
            drawLine(gridColor, Offset(gx, 0f), Offset(gx, size.height), 1f)
            gx += 60f
        }
        var gy = cy % 60f
        while (gy < size.height) {
            drawLine(gridColor, Offset(0f, gy), Offset(size.width, gy), 1f)
            gy += 60f
        }

        fun project(lat: Double, lon: Double): Offset {
            val dx = (lon - centerLon) * metersPerDegLon / metersPerPixel
            val dy = -(lat - centerLat) * metersPerDegLat / metersPerPixel
            return Offset(cx + dx.toFloat(), cy + dy.toFloat())
        }

        if (trail.size > 1) {
            val path = Path()
            trail.forEachIndexed { index, point ->
                val offset = project(point.latitude, point.longitude)
                if (index == 0) path.moveTo(offset.x, offset.y)
                else path.lineTo(offset.x, offset.y)
            }
            drawPath(
                path = path,
                color = Cyan,
                style = Stroke(width = 4f, cap = StrokeCap.Round),
            )
        }

        val current = project(centerLat, centerLon)
        drawCircle(Cyan.copy(alpha = 0.18f), 20f, current)
        drawCircle(Green, 8f, current)
        drawCircle(Color.White, 3f, current)

        val paint = Paint().apply {
            isAntiAlias = true
            textSize = 26f
            color = android.graphics.Color.WHITE
        }
        pois.forEach { poi ->
            val offset = project(poi.latitude, poi.longitude)
            drawCircle(Amber, 7f, offset)
            drawCircle(Amber.copy(alpha = 0.25f), 14f, offset)
            drawContext.canvas.nativeCanvas.drawText(poi.label, offset.x + 12f, offset.y + 8f, paint)
        }

        paint.color = android.graphics.Color.GREEN
        drawContext.canvas.nativeCanvas.drawText("N", size.width - 26f, 30f, paint)
    }
}

@Composable
private fun MapLegend(label: String, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .width(8.dp)
                .height(8.dp)
                .background(color, RoundedCornerShape(50)),
        )
        Spacer(Modifier.width(4.dp))
        Text(label, color = TextDim, fontSize = 10.sp)
    }
}

private fun distanceMeters(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
    val dLat = Math.toRadians(lat2 - lat1)
    val dLon = Math.toRadians(lon2 - lon1)
    val a = kotlin.math.sin(dLat / 2) * kotlin.math.sin(dLat / 2) +
        cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) *
        kotlin.math.sin(dLon / 2) * kotlin.math.sin(dLon / 2)
    return 6_371_000.0 * 2.0 * kotlin.math.atan2(kotlin.math.sqrt(a), kotlin.math.sqrt(1 - a))
}

private fun fmtSix(value: Double): String = String.format(Locale.US, "%.6f", value)
