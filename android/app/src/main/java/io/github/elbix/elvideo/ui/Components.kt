package io.github.elbix.elvideo.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import coil3.video.videoFramePercent

// Everything pressable shrinks a little while held instead of showing a ripple.
@Composable
fun Modifier.pressable(
    enabled: Boolean = true,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
): Modifier {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) PressScale else 1f, tween(140), label = "press")
    return this
        .graphicsLayer { scaleX = scale; scaleY = scale }
        .combinedClickable(interactionSource = source, indication = null, enabled = enabled,
            onLongClick = onLongClick, onClick = onClick)
}

enum class ButtonStyle { Primary, Secondary, Plain }

@Composable
fun PillButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    style: ButtonStyle = ButtonStyle.Primary,
    icon: ImageVector? = null,
    height: Dp = 40.dp,
    enabled: Boolean = true,
) {
    val c = Elv.colors
    val bg = when (style) {
        ButtonStyle.Primary -> c.accent
        ButtonStyle.Secondary -> c.fill
        ButtonStyle.Plain -> Color.Transparent
    }
    val fg = when (style) {
        ButtonStyle.Primary -> Color.White
        else -> c.accent
    }
    Row(
        modifier
            .height(height)
            .graphicsLayer { alpha = if (enabled) 1f else 0.4f }
            .clip(RoundedCornerShape(50))
            .background(bg)
            .pressable(enabled = enabled, onClick = onClick)
            .padding(horizontal = if (style == ButtonStyle.Plain) 8.dp else 18.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
    ) {
        if (icon != null) Icon(icon, null, Modifier.size(15.dp), tint = fg)
        Text(text, style = Type.bodyStrong, color = fg, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

// A white (dark: tile) group with a hairline edge; no shadow.
@Composable
fun Card(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    val c = Elv.colors
    Column(
        modifier
            .clip(RoundedCornerShape(Radius.card))
            .background(c.surface)
            .border(1.dp, c.border, RoundedCornerShape(Radius.card)),
        content = content,
    )
}

@Composable
fun Hairline(modifier: Modifier = Modifier, inset: Dp = 0.dp) {
    Box(modifier.padding(start = inset).fillMaxWidth().height(1.dp).background(Elv.colors.separator))
}

@Composable
fun ThinProgress(fraction: Float, modifier: Modifier = Modifier, height: Dp = 4.dp, track: Color = Elv.colors.fill, color: Color = Elv.colors.accent) {
    Box(modifier.fillMaxWidth().height(height).clip(RoundedCornerShape(50)).background(track)) {
        if (fraction > 0f) Box(Modifier.fillMaxHeight().fillMaxWidth(fraction.coerceIn(0f, 1f)).clip(RoundedCornerShape(50)).background(color))
    }
}

// The watched mark: an empty ring, or a filled blue circle with a check.
@Composable
fun CheckCircle(checked: Boolean, onToggle: () -> Unit, modifier: Modifier = Modifier) {
    val c = Elv.colors
    val bg by animateColorAsState(if (checked) c.accent else Color.Transparent, tween(140), label = "check")
    Box(
        modifier
            .size(44.dp)
            .pressable(onClick = onToggle),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.size(22.dp).clip(CircleShape).background(bg)
                .then(if (checked) Modifier else Modifier.border(1.5.dp, c.textFaint, CircleShape)),
            contentAlignment = Alignment.Center,
        ) {
            if (checked) Icon(ElvIcons.Check, "다 봄", Modifier.size(13.dp), tint = Color.White)
        }
    }
}

// A frame from the video with how far it has been watched along the bottom.
@Composable
fun VideoThumb(uri: String, fraction: Float, modifier: Modifier = Modifier, radius: Dp = Radius.small) {
    val c = Elv.colors
    val context = LocalContext.current
    val request = remember(uri) {
        ImageRequest.Builder(context).data(uri).videoFramePercent(0.1).crossfade(true).build()
    }
    Box(modifier.clip(RoundedCornerShape(radius)).background(c.mediaBackground)) {
        AsyncImage(request, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        // Hairline edge so light frames do not melt into a white card.
        Box(Modifier.fillMaxSize().border(1.dp, Color.Black.copy(alpha = if (c.dark) 0f else 0.05f), RoundedCornerShape(radius)))
        if (fraction > 0f) {
            Box(Modifier.align(Alignment.BottomStart).fillMaxWidth().height(3.dp).background(Color.Black.copy(alpha = 0.35f)))
            Box(Modifier.align(Alignment.BottomStart).fillMaxWidth(fraction).height(3.dp).background(c.accent))
        }
    }
}

@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(text, style = Type.captionStrong, color = Elv.colors.textDim, modifier = modifier)
}

// Pill-shaped segmented control; the selection is a raised thumb.
@Composable
fun <T> SegmentedControl(options: List<Pair<String, T>>, selected: T, onSelect: (T) -> Unit, modifier: Modifier = Modifier) {
    val c = Elv.colors
    Row(
        modifier.height(32.dp).clip(RoundedCornerShape(50)).background(c.fill).padding(2.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        for ((label, value) in options) {
            val on = value == selected
            Box(
                Modifier.fillMaxHeight().clip(RoundedCornerShape(50))
                    .background(if (on) c.thumb else Color.Transparent)
                    .then(if (on && !c.dark) Modifier.border(0.5.dp, Color.Black.copy(alpha = 0.06f), RoundedCornerShape(50)) else Modifier)
                    .pressable { onSelect(value) }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = if (on) Type.captionStrong else Type.caption, color = c.text, maxLines = 1)
            }
        }
    }
}

@Composable
fun ToggleSwitch(checked: Boolean, onChange: (Boolean) -> Unit) {
    val c = Elv.colors
    val track by animateColorAsState(if (checked) Color(0xFF34C759) else c.fill, tween(140), label = "track")
    val x by animateDpAsState(if (checked) 20.dp else 2.dp, tween(140), label = "knob")
    Box(
        Modifier.size(width = 51.dp, height = 31.dp).clip(RoundedCornerShape(50)).background(track)
            .pressable { onChange(!checked) },
    ) {
        Box(Modifier.offset(x = x, y = 2.dp).size(27.dp).clip(CircleShape).background(Color.White)
            .border(0.5.dp, Color.Black.copy(alpha = 0.04f), CircleShape))
    }
}

// A grouped settings row: label on the left, control on the right.
@Composable
fun SettingRow(label: String, detail: String? = null, control: @Composable RowScope.() -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, style = Type.body, color = Elv.colors.text)
            if (detail != null) Text(detail, style = Type.caption, color = Elv.colors.textDim)
        }
        Spacer(Modifier.width(12.dp))
        control()
    }
}

@Composable
fun PageTitle(text: String, modifier: Modifier = Modifier) {
    Text(text, style = Type.largeTitle, color = Elv.colors.text, modifier = modifier)
}

// The quiet empty state: a symbol, one line, one action.
@Composable
fun EmptyState(icon: ImageVector, title: String, body: String, modifier: Modifier = Modifier, action: (@Composable () -> Unit)? = null) {
    Column(
        modifier.fillMaxWidth().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Icon(icon, null, Modifier.size(44.dp), tint = Elv.colors.textFaint)
        Text(title, style = Type.headline, color = Elv.colors.text)
        Text(body, style = Type.callout, color = Elv.colors.textDim, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        if (action != null) { Spacer(Modifier.height(6.dp)); action() }
    }
}

@Composable
fun IconButtonPlain(icon: ImageVector, description: String, onClick: () -> Unit, tint: Color = Elv.colors.accent, size: Dp = 22.dp, modifier: Modifier = Modifier) {
    Box(modifier.size(44.dp).pressable(onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, description, Modifier.size(size), tint = tint)
    }
}
