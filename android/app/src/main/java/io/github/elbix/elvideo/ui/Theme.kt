package io.github.elbix.elvideo.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import io.github.elbix.elvideo.R

// The desktop app's Theme.qml, after DESIGN.md (Apple): parchment and white
// (dark: near-black tiles) in layers, hairlines instead of shadows, one Action
// Blue for everything interactive. Keep the two in step.
@Immutable
data class ElvColors(
    val dark: Boolean,
    val background: Color,
    val surface: Color,
    val surfaceRaised: Color,
    val pressed: Color,
    val fill: Color,
    val thumb: Color,
    val border: Color,
    val separator: Color,
    val mediaBackground: Color,
    val sidebar: Color,
    val bar: Color,        // bottom tabs: a little translucent over the page
    val text: Color,
    val textDim: Color,
    val textFaint: Color,
    val accent: Color,
    val accentSoft: Color,
    val danger: Color,
)

val LightColors = ElvColors(
    dark = false,
    background = Color(0xFFF5F5F7),
    surface = Color(0xFFFFFFFF),
    surfaceRaised = Color(0xFFFFFFFF),
    pressed = Color(0xFFE3E3E6),
    fill = Color(0xFFE8E8ED),
    thumb = Color(0xFFFFFFFF),
    border = Color(0xFFE0E0E0),
    separator = Color(0xFFF0F0F3),
    mediaBackground = Color(0xFFE8E8ED),
    sidebar = Color(0xFFECECF0),
    bar = Color(0xF0FAFAFC),
    text = Color(0xFF1D1D1F),
    textDim = Color(0xFF6E6E73),
    textFaint = Color(0xFFAEAEB2),
    accent = Color(0xFF0066CC),
    accentSoft = Color(0x1F0066CC),
    danger = Color(0xFFFF3B30),
)

val DarkColors = ElvColors(
    dark = true,
    background = Color(0xFF1D1D1F),
    surface = Color(0xFF272729),
    surfaceRaised = Color(0xFF2A2A2C),
    pressed = Color(0xFF3A3A3C),
    fill = Color(0xFF333336),
    thumb = Color(0xFF5C5C60),
    border = Color(0xFF333335),
    separator = Color(0xFF38383A),
    mediaBackground = Color(0xFF000000),
    sidebar = Color(0xFF232325),
    bar = Color(0xF0232325),
    text = Color(0xFFFFFFFF),
    textDim = Color(0xFFA1A1A6),
    textFaint = Color(0xFF636366),
    accent = Color(0xFF2997FF),
    accentSoft = Color(0x332997FF),
    danger = Color(0xFFFF453A),
)

// Floating controls over video are dark in both modes.
object Hud {
    val background = Color(0xB81C1C1F)
    val border = Color(0x24FFFFFF)
    val text = Color(0xFFF5F5F7)
    val textDim = Color(0xFFA1A1A6)
    val accent = Color(0xFF2997FF)
    val panel = Color(0xFF1D1D1F)
}

// Pretendard (bundled, SIL OFL): Inter's Latin with matching Hangul.
val Pretendard = FontFamily(
    Font(R.font.pretendard_regular, FontWeight.Normal),
    Font(R.font.pretendard_semibold, FontWeight.SemiBold),
)

// Weights 400 and 600 only; from 17 sp up the tracking is tight.
private fun tracking(size: Float): TextUnit = if (size >= 17f) (-0.022f).em else 0.em

fun elvText(size: Float, weight: FontWeight = FontWeight.Normal, lineHeight: Float = size * 1.3f) = TextStyle(
    fontFamily = Pretendard,
    fontWeight = weight,
    fontSize = size.sp,
    lineHeight = lineHeight.sp,
    letterSpacing = tracking(size),
)

object Type {
    val largeTitle = elvText(28f, FontWeight.SemiBold, 34f)
    val title = elvText(21f, FontWeight.SemiBold, 26f)
    val headline = elvText(17f, FontWeight.SemiBold, 22f)
    val body = elvText(15f)
    val bodyStrong = elvText(15f, FontWeight.SemiBold)
    val callout = elvText(14f)
    val caption = elvText(12f)
    val captionStrong = elvText(12f, FontWeight.SemiBold)
    val fine = elvText(11f)
}

// Three radii and the pill; nothing in between.
object Radius {
    val small = 8.dp
    val medium = 11.dp
    val card = 18.dp
}

const val PressScale = 0.95f

val LocalElvColors = staticCompositionLocalOf { LightColors }

object Elv {
    val colors: ElvColors
        @Composable get() = LocalElvColors.current
}

@Composable
fun ElvTheme(appearance: String, content: @Composable () -> Unit) {
    val dark = when (appearance) {
        "light" -> false
        "dark" -> true
        else -> isSystemInDarkTheme()
    }
    ElvTheme(dark, content)
}

@Composable
fun ElvTheme(dark: Boolean, content: @Composable () -> Unit) {
    val c = if (dark) DarkColors else LightColors
    // Material is only the base for text fields and selection handles.
    val scheme = if (dark) {
        darkColorScheme(primary = c.accent, background = c.background, surface = c.surface,
            onBackground = c.text, onSurface = c.text)
    } else {
        lightColorScheme(primary = c.accent, background = c.background, surface = c.surface,
            onBackground = c.text, onSurface = c.text)
    }
    CompositionLocalProvider(LocalElvColors provides c) {
        MaterialTheme(colorScheme = scheme, typography = MaterialTheme.typography, content = content)
    }
}

// Seconds as a clock: 38:20, 1:02:10.
fun formatClock(seconds: Double): String {
    val s = seconds.coerceAtLeast(0.0).toLong()
    val h = s / 3600
    val m = (s / 60) % 60
    val sec = s % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, sec) else "%d:%02d".format(m, sec)
}

// Remaining time in words: 25분, 3시간 20분.
fun formatRemaining(seconds: Double): String {
    val min = (seconds / 60).toLong().coerceAtLeast(1)
    return if (min >= 60) "${min / 60}시간 ${min % 60}분".removeSuffix(" 0분") else "${min}분"
}
