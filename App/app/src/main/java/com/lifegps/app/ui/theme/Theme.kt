package com.lifegps.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val Cyan = Color(0xFF38BDF8)
val Green = Color(0xFF34D399)
val Amber = Color(0xFFFBBF24)
val Red = Color(0xFFFB7185)
val Bg = Color(0xFF0B1220)
val Surface = Color(0xFF111A2B)
val SurfaceHigh = Color(0xFF1B2740)
val TextMain = Color(0xFFE2E8F0)
val TextDim = Color(0xFF94A3B8)

private val DarkColors = darkColorScheme(
    primary = Cyan,
    onPrimary = Color(0xFF001018),
    secondary = Green,
    tertiary = Amber,
    error = Red,
    background = Bg,
    onBackground = TextMain,
    surface = Surface,
    onSurface = TextMain,
    surfaceVariant = SurfaceHigh,
    onSurfaceVariant = TextDim,
)

@Composable
fun LifeGpsTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColors,
        content = content,
    )
}
