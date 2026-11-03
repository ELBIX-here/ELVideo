package io.github.elbix.elvideo.player

import android.content.Context
import android.media.AudioManager
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.media3.common.C
import androidx.media3.ui.compose.ContentFrame
import androidx.window.layout.FoldingFeature
import io.github.elbix.elvideo.app
import io.github.elbix.elvideo.ui.ElvIcons
import io.github.elbix.elvideo.ui.ElvMenu
import io.github.elbix.elvideo.ui.ElvMenuItem
import io.github.elbix.elvideo.ui.Hud
import io.github.elbix.elvideo.ui.Radius
import io.github.elbix.elvideo.ui.Type
import io.github.elbix.elvideo.ui.formatClock
import io.github.elbix.elvideo.ui.pressable
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.roundToInt

private val Speeds = listOf(0.5f, 0.75f, 1f, 1.25f, 1.5f, 1.75f, 2f)

private fun speedLabel(v: Float) = (if (v == v.toInt().toFloat()) "${v.toInt()}" else "$v".trimEnd('0')) + "×"

// The controls' visibility: shown on a tap, hidden a few seconds after the last
// touch while playing (as on the desktop, where the bar hides after 3 s).
private class HudState {
    var visible by mutableStateOf(true)
    var touched by mutableIntStateOf(0)
    var playlistOpen by mutableStateOf(false)
    fun poke() { visible = true; touched++ }
}

@Composable
fun PlayerScreen(
    vm: PlayerViewModel,
    fold: FoldingFeature?,
    inPip: Boolean,
    onBack: () -> Unit,
    onPip: () -> Unit,
    onRotate: () -> Unit,
    onImmersive: (Boolean) -> Unit,
    onBrightness: (Float) -> Unit,
    currentBrightness: () -> Float,
) {
    val hud = remember { HudState() }
    val view = LocalView.current
    // The screen stays on while a lecture plays.
    LaunchedEffect(vm.isPlaying) { view.keepScreenOn = vm.isPlaying }
    LaunchedEffect(hud.visible, hud.touched, vm.isPlaying, hud.playlistOpen) {
        if (hud.visible && vm.isPlaying && !hud.playlistOpen) { delay(3000); hud.visible = false }
    }
    BackHandler(hud.playlistOpen) { hud.playlistOpen = false }

    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        if (inPip) {
            ContentFrame(player = vm.player, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
            return@BoxWithConstraints
        }
        val density = LocalDensity.current
        val halfOpen = fold?.state == FoldingFeature.State.HALF_OPENED
        val tabletop = halfOpen && fold?.orientation == FoldingFeature.Orientation.HORIZONTAL
        val book = halfOpen && fold?.orientation == FoldingFeature.Orientation.VERTICAL
        val landscape = maxWidth > maxHeight

        LaunchedEffect(landscape, tabletop, book) { onImmersive(landscape && !tabletop && !book) }

        val stage: @Composable (Modifier, Boolean) -> Unit = { m, overlay ->
            VideoStage(vm, hud, m, overlayControls = overlay, fullscreen = landscape && !book,
                onBack = onBack, onPip = onPip, onRotate = onRotate, onBrightness = onBrightness, currentBrightness = currentBrightness)
        }

        when {
            // Half folded on a table: the picture on the upper half, everything
            // you touch on the lower half, nothing across the hinge.
            tabletop -> {
                val top = with(density) { fold!!.bounds.top.toDp() }
                val bottom = with(density) { fold!!.bounds.bottom.toDp() }
                Column(Modifier.fillMaxSize()) {
                    stage(Modifier.fillMaxWidth().height(top).windowInsetsPadding(WindowInsets.statusBars), false)
                    Spacer(Modifier.height(bottom - top))
                    Column(Modifier.fillMaxWidth().weight(1f).background(Hud.panel).windowInsetsPadding(WindowInsets.navigationBars)) {
                        ControlsPanel(vm, onBack, onPip, Modifier.padding(horizontal = 20.dp, vertical = 14.dp))
                        PlaylistList(vm, Modifier.weight(1f))
                    }
                }
            }
            // Half open like a book: the picture on one page, the playlist on the other.
            book -> {
                val left = with(density) { fold!!.bounds.left.toDp() }
                val right = with(density) { fold!!.bounds.right.toDp() }
                Row(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
                    stage(Modifier.width(left).fillMaxHeight(), true)
                    Spacer(Modifier.width(right - left))
                    Column(Modifier.weight(1f).fillMaxHeight().background(Hud.panel)) {
                        TitleBlock(vm, Modifier.padding(20.dp))
                        PlaylistList(vm, Modifier.weight(1f))
                    }
                }
            }
            landscape -> {
                Box(Modifier.fillMaxSize()) {
                    stage(Modifier.fillMaxSize(), true)
                    AnimatedVisibility(
                        hud.playlistOpen, Modifier.align(Alignment.CenterEnd),
                        enter = slideInHorizontally { it } + fadeIn(), exit = slideOutHorizontally { it } + fadeOut(),
                    ) {
                        Column(
                            Modifier.fillMaxHeight().width(320.dp).windowInsetsPadding(WindowInsets.safeDrawing)
                                .padding(12.dp).clip(RoundedCornerShape(Radius.card)).background(Hud.background)
                                .border(1.dp, Hud.border, RoundedCornerShape(Radius.card)),
                        ) {
                            Row(Modifier.padding(start = 16.dp, end = 4.dp, top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text("재생목록", style = Type.headline, color = Hud.text, modifier = Modifier.weight(1f))
                                HudIcon(ElvIcons.Close, "닫기") { hud.playlistOpen = false }
                            }
                            PlaylistList(vm, Modifier.weight(1f))
                        }
                    }
                }
            }
            // Portrait: the picture on top, the lecture and its folder below.
            else -> {
                Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.statusBars)) {
                    val ratio = vm.player.videoSize.let { if (it.width > 0 && it.height > 0) it.width.toFloat() / it.height else 16f / 9f }
                    stage(Modifier.fillMaxWidth().aspectRatio(ratio.coerceIn(1f, 2.4f)), true)
                    Column(Modifier.fillMaxWidth().weight(1f).background(Hud.panel).windowInsetsPadding(WindowInsets.navigationBars)) {
                        TitleBlock(vm, Modifier.padding(start = 20.dp, end = 20.dp, top = 16.dp, bottom = 8.dp))
                        PlaylistList(vm, Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

// ---- the picture, with gestures and the overlay controls ----------------------

private enum class DragKind { None, Seek, Brightness, Volume }

@Composable
private fun VideoStage(
    vm: PlayerViewModel,
    hud: HudState,
    modifier: Modifier,
    overlayControls: Boolean,
    fullscreen: Boolean,
    onBack: () -> Unit,
    onPip: () -> Unit,
    onRotate: () -> Unit,
    onBrightness: (Float) -> Unit,
    currentBrightness: () -> Float,
) {
    val context = LocalContext.current
    val audio = remember { context.getSystemService(Context.AUDIO_SERVICE) as AudioManager }
    val seekStep by context.app.settings.seekStep.collectAsState()
    val player = vm.player

    // Feedback for gestures: "+10초", the seek target, brightness and volume levels.
    var tapSeek by remember { mutableIntStateOf(0) }
    var tapSeekTick by remember { mutableIntStateOf(0) }
    var drag by remember { mutableStateOf(DragKind.None) }
    var dragTarget by remember { mutableLongStateOf(0L) }
    var level by remember { mutableFloatStateOf(0f) }
    var boosted by remember { mutableStateOf(false) }

    LaunchedEffect(tapSeekTick) { if (tapSeekTick > 0) { delay(700); tapSeek = 0 } }
    LaunchedEffect(vm.resumedAt) { if (vm.resumedAt != null) { delay(5000); vm.resumedAt = null } }

    BoxWithConstraints(modifier.background(Color.Black).clip(RoundedCornerShape(0.dp))) {
        val widthPx = constraints.maxWidth.toFloat()
        val heightPx = constraints.maxHeight.toFloat()
        ContentFrame(player = player, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit)

        Box(
            Modifier.fillMaxSize()
                .pointerInput(seekStep) {
                    detectTapGestures(
                        onTap = { if (hud.visible) hud.visible = false else hud.poke() },
                        onDoubleTap = { p: Offset ->
                            when {
                                p.x < widthPx / 3 -> { vm.seekBy(-seekStep); tapSeek = if (tapSeek < 0) tapSeek - seekStep else -seekStep; tapSeekTick++ }
                                p.x > widthPx * 2 / 3 -> { vm.seekBy(seekStep); tapSeek = if (tapSeek > 0) tapSeek + seekStep else seekStep; tapSeekTick++ }
                                else -> { vm.togglePlay(); hud.poke() }
                            }
                        },
                        // Hold for double speed, let go for the normal speed.
                        onLongPress = { if (player.isPlaying) { boosted = true; player.setPlaybackSpeed(2f) } },
                        onPress = {
                            tryAwaitRelease()
                            if (boosted) { boosted = false; player.setPlaybackSpeed(vm.speed) }
                        },
                    )
                }
                .pointerInput(Unit) {
                    var startX = 0f
                    var startPos = 0L
                    var acc = Offset.Zero
                    var startLevel = 0f
                    detectDragGestures(
                        onDragStart = { p -> startX = p.x; acc = Offset.Zero; drag = DragKind.None; startPos = player.currentPosition },
                        onDragEnd = {
                            if (drag == DragKind.Seek) player.seekTo(dragTarget)
                            drag = DragKind.None
                        },
                        onDragCancel = { drag = DragKind.None },
                    ) { change, amount ->
                        change.consume()
                        acc += amount
                        if (drag == DragKind.None) {
                            drag = when {
                                abs(acc.x) > abs(acc.y) -> DragKind.Seek
                                startX < widthPx / 2 -> DragKind.Brightness
                                else -> DragKind.Volume
                            }
                            startLevel = when (drag) {
                                DragKind.Brightness -> currentBrightness().let { if (it < 0) 0.5f else it }
                                DragKind.Volume -> audio.getStreamVolume(AudioManager.STREAM_MUSIC).toFloat() / audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
                                else -> 0f
                            }
                        }
                        when (drag) {
                            // A full swipe across the picture moves two minutes.
                            DragKind.Seek -> {
                                val d = player.duration.takeIf { it != C.TIME_UNSET && it > 0 } ?: 0L
                                dragTarget = (startPos + (acc.x / widthPx * 120_000).toLong()).coerceIn(0, d)
                            }
                            DragKind.Brightness -> {
                                level = (startLevel - acc.y / heightPx).coerceIn(0.01f, 1f)
                                onBrightness(level)
                            }
                            DragKind.Volume -> {
                                level = (startLevel - acc.y / heightPx).coerceIn(0f, 1f)
                                val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
                                audio.setStreamVolume(AudioManager.STREAM_MUSIC, (level * max).roundToInt(), 0)
                            }
                            DragKind.None -> {}
                        }
                    }
                },
        )

        // Subtitles, lifted above the controls while they show.
        val lift by animateDpAsState(if (hud.visible && overlayControls) 96.dp else 16.dp, tween(240), label = "subs")
        if (vm.cues.isNotEmpty()) {
            Text(
                vm.cues.joinToString("\n") { it.text?.toString().orEmpty() }.trim(),
                style = Type.body.copy(fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center),
                color = Color.White,
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = lift, start = 24.dp, end = 24.dp)
                    .clip(RoundedCornerShape(Radius.small)).background(Color.Black.copy(alpha = 0.55f))
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            )
        }

        // Gesture feedback in the middle of the picture.
        val bubble: String? = when {
            drag == DragKind.Seek -> "${formatClock(dragTarget / 1000.0)} / ${formatClock(player.duration.coerceAtLeast(0) / 1000.0)}"
            drag == DragKind.Brightness -> "밝기 ${(level * 100).roundToInt()}%"
            drag == DragKind.Volume -> "볼륨 ${(level * 100).roundToInt()}%"
            tapSeek != 0 -> (if (tapSeek > 0) "+" else "−") + "${abs(tapSeek)}초"
            boosted -> "2× 빠르게"
            else -> null
        }
        if (bubble != null) {
            Text(bubble, style = Type.headline, color = Hud.text,
                modifier = Modifier.align(if (tapSeek != 0 && drag == DragKind.None) (if (tapSeek > 0) Alignment.CenterEnd else Alignment.CenterStart) else Alignment.Center)
                    .padding(horizontal = 40.dp)
                    .clip(RoundedCornerShape(50)).background(Hud.background).border(1.dp, Hud.border, RoundedCornerShape(50))
                    .padding(horizontal = 16.dp, vertical = 8.dp))
        }

        if (overlayControls) {
            AnimatedVisibility(hud.visible && drag == DragKind.None, Modifier.fillMaxSize(), enter = fadeIn(tween(140)), exit = fadeOut(tween(240))) {
                OverlayControls(vm, hud, fullscreen, onBack, onPip, onRotate)
            }
        }

        val resumed = vm.resumedAt
        if (resumed != null) {
            Row(
                (if (fullscreen) Modifier.align(Alignment.TopCenter).padding(top = 64.dp) else Modifier.align(Alignment.BottomCenter).padding(bottom = 52.dp))
                    .clip(RoundedCornerShape(50)).background(Hud.background).border(1.dp, Hud.border, RoundedCornerShape(50))
                    .padding(start = 16.dp, end = 6.dp, top = 6.dp, bottom = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("${formatClock(resumed)}부터 이어서 재생", style = Type.callout, color = Hud.text)
                Spacer(Modifier.width(10.dp))
                Text("처음부터", style = Type.callout.copy(fontWeight = FontWeight.SemiBold), color = Hud.accent,
                    modifier = Modifier.clip(RoundedCornerShape(50)).pressable { vm.restart() }.padding(horizontal = 10.dp, vertical = 4.dp))
            }
        }

        val error = vm.error
        if (error != null) {
            Text(error, style = Type.body, color = Hud.text, textAlign = TextAlign.Center,
                modifier = Modifier.align(Alignment.Center).padding(32.dp))
        }
    }
}

@Composable
private fun rememberPosition(vm: PlayerViewModel): Pair<Long, Long> {
    val pos by produceState(0L to 0L, vm.player) {
        while (true) {
            val d = vm.player.duration.takeIf { it != C.TIME_UNSET && it > 0 } ?: 0L
            value = vm.player.currentPosition to d
            delay(250)
        }
    }
    return pos
}

@Composable
private fun OverlayControls(vm: PlayerViewModel, hud: HudState, fullscreen: Boolean, onBack: () -> Unit, onPip: () -> Unit, onRotate: () -> Unit) {
    val seekStep by LocalContext.current.app.settings.seekStep.collectAsState()
    // A light dim over the picture, darker along the top and bottom edges so the
    // white controls read on bright slides. (Legibility, not decoration.)
    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.18f))) {
        Box(Modifier.align(Alignment.TopCenter).fillMaxWidth().height(120.dp)
            .background(Brush.verticalGradient(listOf(Color.Black.copy(alpha = 0.6f), Color.Transparent))))
        Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(140.dp)
            .background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.6f)))))
    }
    Box(Modifier.fillMaxSize()
        .then(if (fullscreen) Modifier.windowInsetsPadding(WindowInsets.safeDrawing) else Modifier)) {
        // Top: back, the lecture, and the window tools.
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            HudIcon(ElvIcons.Back, "뒤로", onClick = onBack)
            Column(Modifier.weight(1f)) {
                Text(vm.current?.title.orEmpty(), style = Type.bodyStrong, color = Hud.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (fullscreen && vm.queue.size > 1) {
                    Text(listOfNotNull(vm.folderName.ifEmpty { null }, "${vm.index + 1} / ${vm.queue.size}").joinToString(" · "),
                        style = Type.caption, color = Hud.textDim, maxLines = 1)
                }
            }
            SpeedButton(vm) { hud.poke() }
            if (fullscreen && vm.queue.size > 1) HudIcon(ElvIcons.Playlist, "재생목록") { hud.playlistOpen = !hud.playlistOpen; hud.poke() }
            HudIcon(ElvIcons.Pip, "작은 화면", onClick = onPip)
            HudIcon(ElvIcons.Rotate, "화면 회전", onClick = onRotate)
        }

        // Middle: back a step, play / pause, forward a step.
        Row(Modifier.align(Alignment.Center), horizontalArrangement = Arrangement.spacedBy(36.dp), verticalAlignment = Alignment.CenterVertically) {
            StepButton(ElvIcons.Rewind, seekStep, "${seekStep}초 뒤로") { vm.seekBy(-seekStep); hud.poke() }
            Box(
                Modifier.size(64.dp).clip(CircleShape).background(Hud.background).border(1.dp, Hud.border, CircleShape)
                    .pressable { vm.togglePlay(); hud.poke() },
                contentAlignment = Alignment.Center,
            ) {
                Icon(if (vm.isPlaying) ElvIcons.Pause else ElvIcons.Play, if (vm.isPlaying) "일시정지" else "재생",
                    Modifier.size(26.dp).padding(start = if (vm.isPlaying) 0.dp else 3.dp), tint = Hud.text)
            }
            StepButton(ElvIcons.Forward, seekStep, "${seekStep}초 앞으로") { vm.seekBy(seekStep); hud.poke() }
        }

        // Bottom: the seek bar, and in full screen the previous and next lecture.
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)) {
            SeekRow(vm) { hud.poke() }
            if (fullscreen && vm.queue.size > 1) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    HudIcon(ElvIcons.Previous, "이전 강의", enabled = vm.index > 0) { vm.previous(); hud.poke() }
                    HudIcon(ElvIcons.Next, "다음 강의", enabled = vm.index < vm.queue.lastIndex) { vm.next(); hud.poke() }
                    vm.queue.getOrNull(vm.index + 1)?.let {
                        Text("다음: ${it.title}", style = Type.caption, color = Hud.textDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
    }
}

@Composable
private fun SeekRow(vm: PlayerViewModel, onTouch: () -> Unit) {
    val (position, duration) = rememberPosition(vm)
    var scrub by remember { mutableStateOf<Long?>(null) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(formatClock((scrub ?: position) / 1000.0), style = Type.caption, color = Hud.text, modifier = Modifier.widthIn(min = 44.dp))
        SeekBar(scrub ?: position, duration, Modifier.weight(1f).padding(horizontal = 8.dp),
            onScrub = { scrub = it; onTouch() },
            onSeek = { vm.player.seekTo(it); scrub = null; onTouch() })
        Text(formatClock(duration / 1000.0), style = Type.caption, color = Hud.textDim, modifier = Modifier.widthIn(min = 44.dp), textAlign = TextAlign.End)
    }
}

// White played part on a translucent track; it thickens while dragged.
@Composable
private fun SeekBar(position: Long, duration: Long, modifier: Modifier, onScrub: (Long) -> Unit, onSeek: (Long) -> Unit) {
    var dragging by remember { mutableStateOf(false) }
    val thick by animateDpAsState(if (dragging) 8.dp else 4.dp, tween(140), label = "seek")
    BoxWithConstraints(
        modifier.height(32.dp)
            .pointerInput(duration) {
                detectTapGestures { p -> if (duration > 0) onSeek((p.x / size.width * duration).toLong().coerceIn(0, duration)) }
            }
            .pointerInput(duration) {
                var target = 0L
                detectDragGestures(
                    onDragStart = { p -> dragging = true; target = (p.x / size.width * duration).toLong(); onScrub(target) },
                    onDragEnd = { dragging = false; onSeek(target) },
                    onDragCancel = { dragging = false; onSeek(target) },
                ) { change, _ ->
                    change.consume()
                    target = (change.position.x / size.width * duration).toLong().coerceIn(0, duration)
                    onScrub(target)
                }
            },
        contentAlignment = Alignment.CenterStart,
    ) {
        val f = if (duration > 0) (position.toFloat() / duration).coerceIn(0f, 1f) else 0f
        Box(Modifier.fillMaxWidth().height(thick).clip(RoundedCornerShape(50)).background(Color.White.copy(alpha = 0.28f)))
        Box(Modifier.fillMaxWidth(f).height(thick).clip(RoundedCornerShape(50)).background(Color.White))
        Box(Modifier.padding(start = (maxWidth - 14.dp) * f).size(14.dp).clip(CircleShape).background(Color.White))
    }
}

@Composable
private fun SpeedButton(vm: PlayerViewModel, onTouch: () -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box {
        Text(speedLabel(vm.speed), style = Type.captionStrong, color = Hud.text,
            modifier = Modifier.padding(horizontal = 6.dp).clip(RoundedCornerShape(50)).border(1.dp, Hud.border, RoundedCornerShape(50))
                .pressable { open = true; onTouch() }.padding(horizontal = 10.dp, vertical = 5.dp))
        ElvMenu(open, { open = false }) {
            for (s in Speeds) ElvMenuItem(speedLabel(s) + if (s == vm.speed) "  ✓" else "") { vm.changeSpeed(s); open = false }
        }
    }
}

@Composable
private fun StepButton(icon: ImageVector, step: Int, description: String, onClick: () -> Unit) {
    Box(Modifier.size(52.dp).pressable(onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, description, Modifier.size(34.dp), tint = Hud.text)
        Text("$step", style = Type.fine.copy(fontWeight = FontWeight.SemiBold), color = Hud.text)
    }
}

@Composable
private fun HudIcon(icon: ImageVector, description: String, enabled: Boolean = true, size: Dp = 22.dp, onClick: () -> Unit) {
    Box(Modifier.size(44.dp).pressable(enabled = enabled, onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, description, Modifier.size(size), tint = if (enabled) Hud.text else Hud.textDim.copy(alpha = 0.5f))
    }
}

// ---- below or beside the picture ---------------------------------------------

@Composable
private fun TitleBlock(vm: PlayerViewModel, modifier: Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(vm.current?.title.orEmpty(), style = Type.title, color = Hud.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
        val meta = listOfNotNull(vm.folderName.ifEmpty { null }, if (vm.queue.size > 1) "${vm.index + 1} / ${vm.queue.size}" else null)
        if (meta.isNotEmpty()) Text(meta.joinToString(" · "), style = Type.caption, color = Hud.textDim)
    }
}

// The lower half in tabletop posture: what is playing, the seek bar and the buttons.
@Composable
private fun ControlsPanel(vm: PlayerViewModel, onBack: () -> Unit, onPip: () -> Unit, modifier: Modifier) {
    val seekStep by LocalContext.current.app.settings.seekStep.collectAsState()
    Column(modifier, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            HudIcon(ElvIcons.Back, "뒤로", onClick = onBack)
            TitleBlock(vm, Modifier.weight(1f))
            SpeedButton(vm) {}
            HudIcon(ElvIcons.Pip, "작은 화면", onClick = onPip)
        }
        SeekRow(vm) {}
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
            HudIcon(ElvIcons.Previous, "이전 강의", enabled = vm.index > 0) { vm.previous() }
            Spacer(Modifier.width(12.dp))
            StepButton(ElvIcons.Rewind, seekStep, "${seekStep}초 뒤로") { vm.seekBy(-seekStep) }
            Spacer(Modifier.width(20.dp))
            Box(Modifier.size(56.dp).clip(CircleShape).background(Hud.text).pressable { vm.togglePlay() }, contentAlignment = Alignment.Center) {
                Icon(if (vm.isPlaying) ElvIcons.Pause else ElvIcons.Play, null,
                    Modifier.size(22.dp).padding(start = if (vm.isPlaying) 0.dp else 3.dp), tint = Hud.panel)
            }
            Spacer(Modifier.width(20.dp))
            StepButton(ElvIcons.Forward, seekStep, "${seekStep}초 앞으로") { vm.seekBy(seekStep) }
            Spacer(Modifier.width(12.dp))
            HudIcon(ElvIcons.Next, "다음 강의", enabled = vm.index < vm.queue.lastIndex) { vm.next() }
        }
    }
}

@Composable
private fun PlaylistList(vm: PlayerViewModel, modifier: Modifier, state: LazyListState = rememberLazyListState()) {
    val progress = LocalContext.current.app.progress
    val revision by progress.revision.collectAsState()
    if (vm.queue.size <= 1) { Spacer(modifier); return }
    LaunchedEffect(vm.index) { state.animateScrollToItem((vm.index - 1).coerceAtLeast(0)) }
    LazyColumn(modifier, state = state) {
        item { Text("재생목록", style = Type.captionStrong, color = Hud.textDim, modifier = Modifier.padding(start = 20.dp, top = 8.dp, bottom = 6.dp)) }
        itemsIndexed(vm.queue, key = { _, it -> it.uri }) { i, item ->
            val e = remember(item.uri, revision) { progress.get(item.uri) }
            val now = i == vm.index
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 8.dp).clip(RoundedCornerShape(Radius.small))
                    .background(if (now) Hud.accent.copy(alpha = 0.16f) else Color.Transparent)
                    .pressable { vm.playIndex(i) }
                    .padding(horizontal = 12.dp, vertical = 11.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.width(28.dp)) {
                    when {
                        now -> Icon(ElvIcons.Play, null, Modifier.size(12.dp), tint = Hud.accent)
                        e?.watched == true -> Icon(ElvIcons.Check, "다 봄", Modifier.size(14.dp), tint = Hud.textDim)
                        else -> Text("${i + 1}", style = Type.caption, color = Hud.textDim)
                    }
                }
                Column(Modifier.weight(1f)) {
                    Text(item.title, style = if (now) Type.callout.copy(fontWeight = FontWeight.SemiBold) else Type.callout,
                        color = if (now) Hud.accent else if (e?.watched == true) Hud.textDim else Hud.text,
                        maxLines = 2, overflow = TextOverflow.Ellipsis)
                    val meta = when {
                        e == null -> null
                        e.watched -> "다 봄"
                        e.position >= 5 -> "${formatClock(e.position)}까지 봄"
                        else -> null
                    }
                    if (meta != null && !now) Text(meta, style = Type.fine, color = Hud.textDim)
                }
                e?.duration?.takeIf { it > 0 }?.let { Text(formatClock(it), style = Type.caption, color = Hud.textDim) }
            }
        }
        item { Spacer(Modifier.height(16.dp)) }
    }
}
