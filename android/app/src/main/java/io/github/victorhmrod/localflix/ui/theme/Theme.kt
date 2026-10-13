package io.github.victorhmrod.localflix.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/** Same palette as the desktop theme (src/ui/Theme.h). */
object LocalFlixColors {
    val Background = Color(0xFF0F1115)
    val Surface = Color(0xFF171A21)
    val SurfaceRaised = Color(0xFF1F232C)
    val Border = Color(0xFF2B303B)
    val Text = Color(0xFFE8EAF0)
    val TextMuted = Color(0xFF9AA3B2)
    val Accent = Color(0xFF5B8DEF)
    val AccentHover = Color(0xFF749FF3)
    val Success = Color(0xFF3ECF8E)
    val Warning = Color(0xFFF5A524)
    val Danger = Color(0xFFEF5350)
}

private val DarkScheme = darkColorScheme(
    primary = LocalFlixColors.Accent,
    onPrimary = Color.White,
    background = LocalFlixColors.Background,
    onBackground = LocalFlixColors.Text,
    surface = LocalFlixColors.Surface,
    onSurface = LocalFlixColors.Text,
    surfaceVariant = LocalFlixColors.SurfaceRaised,
    onSurfaceVariant = LocalFlixColors.TextMuted,
    outline = LocalFlixColors.Border,
    error = LocalFlixColors.Danger,
)

@Composable
fun LocalFlixTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = DarkScheme, content = content)
}
