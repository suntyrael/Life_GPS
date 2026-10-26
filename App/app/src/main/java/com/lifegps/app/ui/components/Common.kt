package com.lifegps.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.lifegps.app.debug.CheckState
import com.lifegps.app.debug.LogLevel
import com.lifegps.app.ui.theme.Amber
import com.lifegps.app.ui.theme.Cyan
import com.lifegps.app.ui.theme.Green
import com.lifegps.app.ui.theme.Red
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DebugScaffold(
    title: String,
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    content: @Composable (PaddingValues) -> Unit,
) {
    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text(title, fontWeight = FontWeight.SemiBold) },
                navigationIcon = {
                    if (onBack != null) {
                        TextButton(onClick = onBack) { Text("返回") }
                    }
                },
                actions = actions,
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                    titleContentColor = TextMain,
                ),
            )
        },
        content = content,
    )
}

@Composable
fun SectionHeader(title: String, hint: String? = null) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 18.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, fontWeight = FontWeight.SemiBold, color = TextMain)
        if (hint != null) {
            Spacer(Modifier.width(8.dp))
            Text(hint, color = TextDim, fontSize = 12.sp)
        }
    }
}

@Composable
fun StatTile(
    label: String,
    value: String,
    unit: String = "",
    accent: Color = Cyan,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = modifier,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(14.dp),
    ) {
        Column(Modifier.padding(12.dp)) {
            Text(label, color = TextDim, fontSize = 12.sp)
            Spacer(Modifier.height(4.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(value, color = accent, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                if (unit.isNotEmpty()) {
                    Spacer(Modifier.width(3.dp))
                    Text(unit, color = TextDim, fontSize = 11.sp, modifier = Modifier.padding(bottom = 3.dp))
                }
            }
        }
    }
}

@Composable
fun StatusPill(text: String, color: Color, filled: Boolean = false) {
    Surface(
        color = if (filled) color.copy(alpha = 0.18f) else Color.Transparent,
        shape = RoundedCornerShape(50),
        border = androidx.compose.foundation.BorderStroke(1.dp, color.copy(alpha = 0.65f)),
    ) {
        Text(
            text = text,
            color = color,
            fontSize = 11.sp,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 3.dp),
        )
    }
}

@Composable
fun MetricRow(label: String, value: String, valueColor: Color = TextMain) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = TextDim, fontSize = 13.sp, modifier = Modifier.weight(1f))
        Text(
            text = value,
            color = valueColor,
            fontSize = 13.sp,
            fontFamily = FontFamily.Monospace,
            textAlign = TextAlign.End,
        )
    }
}

@Composable
fun ValueBar(
    label: String,
    value: Float,
    max: Float,
    color: Color = Cyan,
    valueText: String = "",
) {
    val ratio = if (max <= 0f) 0f else (value / max).coerceIn(0f, 1f)
    Column(Modifier.padding(vertical = 3.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, color = TextDim, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Text(
                text = valueText.ifEmpty { value.toString() },
                color = TextMain,
                fontSize = 12.sp,
                fontFamily = FontFamily.Monospace,
                textAlign = TextAlign.End,
            )
        }
        Spacer(Modifier.height(3.dp))
        Box(
            Modifier
                .fillMaxWidth()
                .height(5.dp)
                .background(MaterialTheme.colorScheme.surface, RoundedCornerShape(50)),
        ) {
            Box(
                Modifier
                    .fillMaxWidth(ratio)
                    .height(5.dp)
                    .background(color, RoundedCornerShape(50)),
            )
        }
    }
}

@Composable
fun ToolCard(
    title: String,
    subtitle: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    accent: Color = Cyan,
) {
    Card(
        modifier = modifier
            .clickable(onClick = onClick),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(14.dp),
    ) {
        Column(Modifier.padding(14.dp)) {
            Text(title, color = accent, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
            Spacer(Modifier.height(4.dp))
            Text(subtitle, color = TextDim, fontSize = 11.sp, lineHeight = 15.sp)
        }
    }
}

@Composable
fun CardBlock(
    title: String? = null,
    hint: String? = null,
    content: @Composable () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(16.dp),
    ) {
        Column(Modifier.padding(14.dp)) {
            if (title != null) {
                Text(title, color = TextMain, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                if (hint != null) {
                    Spacer(Modifier.height(3.dp))
                    Text(hint, color = TextDim, fontSize = 11.sp, lineHeight = 15.sp)
                }
                Spacer(Modifier.height(8.dp))
            }
            content()
        }
    }
}

fun logLevelColor(level: LogLevel): Color = when (level) {
    LogLevel.DEBUG -> TextDim
    LogLevel.INFO -> Green
    LogLevel.WARN -> Amber
    LogLevel.ERROR -> Red
}

fun checkStateColor(state: CheckState): Color = when (state) {
    CheckState.PASS -> Green
    CheckState.FAIL -> Red
    CheckState.PENDING -> Amber
    CheckState.RUNNING -> Cyan
}

fun checkStateLabel(state: CheckState): String = when (state) {
    CheckState.PASS -> "PASS"
    CheckState.FAIL -> "FAIL"
    CheckState.PENDING -> "PENDING"
    CheckState.RUNNING -> "RUNNING"
}

@Composable
fun EmptyHint(text: String) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = TextDim, fontSize = 13.sp)
    }
}

@Composable
fun HorizontalGap(height: Int = 10) {
    Spacer(Modifier.height(height.dp))
}

@Composable
fun RowGap(width: Int = 10) {
    Spacer(Modifier.width(width.dp))
}

@Composable
fun MetricGrid(items: List<Pair<String, String>>) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        items.forEach { (label, value) -> MetricRow(label, value) }
    }
}
