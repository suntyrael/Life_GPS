package com.lifegps.app.debug

import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.lifegps.app.ui.components.CardBlock
import com.lifegps.app.ui.components.DebugScaffold
import com.lifegps.app.ui.components.MetricRow
import com.lifegps.app.ui.components.SectionHeader
import com.lifegps.app.ui.components.StatusPill
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.SurfaceHigh
import com.lifegps.app.ui.theme.TextDim
import kotlin.math.sqrt

@Composable
fun SpatialTrailScreen(
    repo: FakeDebugRepository,
    onBack: () -> Unit,
) {
    val trail by repo.trail.collectAsStateWithLifecycle()
    val running by repo.trailRunning.collectAsStateWithLifecycle()
    val imu by repo.imu.collectAsStateWithLifecycle()

    val distance = trail.zipWithNext().sumOf { (a, b) ->
        val dx = (a.x - b.x).toDouble()
        val dy = (a.y - b.y).toDouble()
        val dz = (a.z - b.z).toDouble()
        sqrt(dx * dx + dy * dy + dz * dz)
    }
    val current = trail.lastOrNull()

    DebugScaffold(
        title = "传感器空间轨迹",
        onBack = onBack,
        actions = {
            StatusPill(if (running) "LIVE" else "PAUSED", if (running) Green else Amber, filled = true)
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
                CardBlock("IMU / PDR 局部轨迹") {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(300.dp)
                            .background(SurfaceHigh, RoundedCornerShape(12.dp)),
                    ) {
                        Canvas(modifier = Modifier.fillMaxSize()) {
                            val center = Offset(size.width / 2f, size.height * 0.64f)
                            val scale = size.minDimension / 9f
                            fun project(x: Float, y: Float, z: Float): Offset {
                                val sx = center.x + (x - y) * scale * 0.72f
                                val sy = center.y + (x + y) * scale * 0.36f - z * scale
                                return Offset(sx, sy)
                            }

                            val gridColor = Color(0xFF23324D)
                            for (i in -3..3) {
                                drawLine(
                                    color = gridColor,
                                    start = project(-3f, i.toFloat(), 0f),
                                    end = project(3f, i.toFloat(), 0f),
                                    strokeWidth = 1f,
                                )
                                drawLine(
                                    color = gridColor,
                                    start = project(i.toFloat(), -3f, 0f),
                                    end = project(i.toFloat(), 3f, 0f),
                                    strokeWidth = 1f,
                                )
                            }

                            drawLine(Red, project(0f, 0f, 0f), project(3.6f, 0f, 0f), 3f)
                            drawLine(Green, project(0f, 0f, 0f), project(0f, 3.6f, 0f), 3f)
                            drawLine(Cyan, project(0f, 0f, 0f), project(0f, 0f, 3.6f), 3f)

                            if (trail.size > 1) {
                                val path = Path()
                                trail.forEachIndexed { index, point ->
                                    val offset = project(point.x, point.y, point.z)
                                    if (index == 0) path.moveTo(offset.x, offset.y)
                                    else path.lineTo(offset.x, offset.y)
                                }
                                drawPath(
                                    path = path,
                                    color = Cyan,
                                    style = Stroke(width = 4f, cap = StrokeCap.Round),
                                )

                                val start = project(trail.first().x, trail.first().y, trail.first().z)
                                drawCircle(Green, 7f, start)
                                trail.forEach { point ->
                                    drawCircle(
                                        color = Cyan.copy(alpha = 0.5f),
                                        radius = 2.5f,
                                        center = project(point.x, point.y, point.z),
                                    )
                                }
                            }

                            current?.let { point ->
                                val tip = project(point.x, point.y, point.z)
                                drawCircle(Cyan.copy(alpha = 0.22f), 16f, tip)
                                drawCircle(Cyan, 8f, tip)
                                drawCircle(Color.White, 3f, tip)
                            }

                            val paint = Paint().apply {
                                isAntiAlias = true
                                textSize = 28f
                                color = android.graphics.Color.WHITE
                            }
                            drawContext.canvas.nativeCanvas.drawText(
                                "X",
                                project(3.85f, 0f, 0f).x,
                                project(3.85f, 0f, 0f).y,
                                paint,
                            )
                            paint.color = android.graphics.Color.GREEN
                            drawContext.canvas.nativeCanvas.drawText(
                                "Y",
                                project(0f, 3.85f, 0f).x,
                                project(0f, 3.85f, 0f).y,
                                paint,
                            )
                            paint.color = android.graphics.Color.CYAN
                            drawContext.canvas.nativeCanvas.drawText(
                                "Z",
                                project(0f, 0f, 3.85f).x,
                                project(0f, 0f, 3.85f).y,
                                paint,
                            )
                        }
                    }
                    Spacer(Modifier.height(10.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        AxisLegend("X", Red)
                        AxisLegend("Y", Green)
                        AxisLegend("Z", Cyan)
                    }
                }
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = { repo.setTrailRunning(!running) },
                        modifier = Modifier.weight(1f),
                    ) { Text(if (running) "暂停轨迹" else "继续轨迹") }
                    OutlinedButton(
                        onClick = { repo.clearTrail() },
                        modifier = Modifier.weight(1f),
                    ) { Text("清空") }
                }
            }

            item {
                SectionHeader("轨迹统计", "局部坐标，单位米")
                CardBlock {
                    MetricRow("采样点", "${trail.size}")
                    MetricRow("累计距离", "%.2f m".format(distance))
                    MetricRow("当前 X", current?.let { "%.2f m".format(it.x) } ?: "--")
                    MetricRow("当前 Y", current?.let { "%.2f m".format(it.y) } ?: "--")
                    MetricRow("当前 Z", current?.let { "%.2f m".format(it.z) } ?: "--")
                    MetricRow("累计步数", "${imu.steps}")
                    MetricRow("ZUPT", if (imu.zupt) "LOCKED" else "MOVING", if (imu.zupt) Amber else Green)
                }
            }

            item {
                Text(
                    "当前是假数据：用 IMU/PDR 局部坐标画 3D 轨迹。真实接入后使用 PDR 位移 + 四元数姿态，可用于检查航向漂移、ZUPT 和步长。",
                    color = TextDim,
                    fontSize = 11.sp,
                    modifier = Modifier.padding(bottom = 20.dp),
                )
            }
        }
    }
}

@Composable
private fun AxisLegend(label: String, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .width(8.dp)
                .height(8.dp)
                .background(color, RoundedCornerShape(50)),
        )
        Spacer(Modifier.width(4.dp))
        Text(label, color = TextDim, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
    }
}
