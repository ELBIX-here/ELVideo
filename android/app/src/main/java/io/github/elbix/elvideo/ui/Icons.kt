package io.github.elbix.elvideo.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.unit.dp

// Line icons on a 24 grid, drawn like the desktop sidebar's (1.7 strokes, round
// ends). Tinted by the Icon that shows them.
object ElvIcons {
    private fun icon(name: String, vararg paths: Pair<String, Boolean>): ImageVector =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f).apply {
            for ((d, filled) in paths) {
                addPath(
                    pathData = addPathNodes(d),
                    fill = if (filled) SolidColor(Color.Black) else null,
                    stroke = if (filled) null else SolidColor(Color.Black),
                    strokeLineWidth = 1.7f,
                    strokeLineCap = StrokeCap.Round,
                    strokeLineJoin = StrokeJoin.Round,
                )
            }
        }.build()

    private fun line(d: String) = d to false
    private fun fill(d: String) = d to true

    val Continue = icon("continue", line("M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z"), fill("M10 8.5l5 3.5-5 3.5z"))
    val Folder = icon("folder", line("M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"))
    val Settings = icon("settings", line("M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z"),
        line("M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1M5.3 18.7l2.1-2.1M16.6 7.4l2.1-2.1"))
    val Plus = icon("plus", line("M12 5v14M5 12h14"))
    val Back = icon("back", line("M15 5l-7 7 7 7"))
    val Chevron = icon("chevron", line("M9 5l7 7-7 7"))
    val Check = icon("check", line("M5 12.5l4.5 4.5L19 7.5"))
    val Play = icon("play", fill("M7 4.5v15a1 1 0 0 0 1.5.86l12.4-7.5a1 1 0 0 0 0-1.72L8.5 3.64A1 1 0 0 0 7 4.5z"))
    val Pause = icon("pause", fill("M6.5 4h3.5v16H6.5zM14 4h3.5v16H14z"))
    val Rewind = icon("rewind", line("M4 12a8 8 0 1 0 2.4-5.7"), line("M4 4v4.5h4.5"))
    val Forward = icon("forward", line("M20 12a8 8 0 1 1-2.4-5.7"), line("M20 4v4.5h-4.5"))
    val Previous = icon("previous", fill("M6 5h2v14H6z"), fill("M19 5.8v12.4a.8.8 0 0 1-1.2.7L9.4 12.7a.8.8 0 0 1 0-1.4l8.4-6.2a.8.8 0 0 1 1.2.7z"))
    val Next = icon("next", fill("M16 5h2v14h-2z"), fill("M5 5.8v12.4a.8.8 0 0 0 1.2.7l8.4-6.2a.8.8 0 0 0 0-1.4L6.2 5.1A.8.8 0 0 0 5 5.8z"))
    val Playlist = icon("playlist", line("M4 6h12M4 11h12M4 16h7"), fill("M15 14v6l5-3z"))
    val Pip = icon("pip", line("M3 6a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"), fill("M12 12h7v6h-7z"))
    val Close = icon("close", line("M6 6l12 12M18 6L6 18"))
    val Search = icon("search", line("M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14z"), line("M20 20l-4-4"))
    val File = icon("file", line("M6 3h8l4 4v14H6z"), line("M14 3v4h4"), fill("M10 11v6l5-3z"))
    val Subtitles = icon("subtitles", line("M3 6a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"), line("M7 12h4M13 12h4M7 16h7"))
    val Rotate = icon("rotate", line("M7 4h10a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z"), line("M10 17h4"))
    val Brightness = icon("brightness", line("M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8z"), line("M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"))
    val Volume = icon("volume", fill("M4 9h4l5-4v14l-5-4H4z"), line("M16.5 8.5a5 5 0 0 1 0 7M19 6a8.5 8.5 0 0 1 0 12"))
    val More = icon("more", fill("M5 10.5a1.5 1.5 0 1 0 0 3a1.5 1.5 0 1 0 0-3zM12 10.5a1.5 1.5 0 1 0 0 3a1.5 1.5 0 1 0 0-3zM19 10.5a1.5 1.5 0 1 0 0 3a1.5 1.5 0 1 0 0-3z"))
}
