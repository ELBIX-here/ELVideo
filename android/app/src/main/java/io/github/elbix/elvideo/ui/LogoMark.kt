package io.github.elbix.elvideo.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.cos
import kotlin.math.sin

// The ELVideo hourglass (app/resources/elvideo.svg and LogoMark.qml): two film
// reels in the bulbs and the dot at the waist, white and red on a dark tile.
@Composable
fun LogoMark(size: Dp = 26.dp, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val k = this.size.width / 512f
        fun o(x: Float, y: Float) = Offset(x * k, y * k)
        val tile = Color(0xFF1D1D1F)
        val paper = Color.White
        drawRoundRect(tile, cornerRadius = CornerRadius(114 * k))
        drawRoundRect(paper, o(164f, 64f), Size(184 * k, 18 * k), CornerRadius(9 * k))
        drawRoundRect(paper, o(164f, 430f), Size(184 * k, 18 * k), CornerRadius(9 * k))
        fun reel(cy: Float, r: Float, hole: Float) {
            drawCircle(paper, r * k, o(256f, cy))
            for (i in 0 until 3) {
                val a = Math.toRadians((i * 120 - 90).toDouble())
                drawCircle(tile, hole * k, o(256f + (cos(a) * r * 0.55).toFloat(), cy + (sin(a) * r * 0.55).toFloat()))
            }
        }
        reel(186f, 44f, 9f)
        reel(326f, 56f, 11f)
        drawCircle(paper, 80 * k, o(256f, 186f), style = Stroke(20 * k))
        drawCircle(paper, 80 * k, o(256f, 326f), style = Stroke(20 * k))
        drawCircle(tile, 28 * k, o(256f, 256f))
        drawCircle(Color(0xFFFF3B30), 24 * k, o(256f, 256f))
    }
}
