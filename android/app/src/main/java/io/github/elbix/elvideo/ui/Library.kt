package io.github.elbix.elvideo.ui

import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.elbix.elvideo.Crumb
import io.github.elbix.elvideo.app
import io.github.elbix.elvideo.data.Drive
import io.github.elbix.elvideo.data.Folders
import io.github.elbix.elvideo.data.LibraryFolder
import io.github.elbix.elvideo.data.Listing
import io.github.elbix.elvideo.data.Video
import io.github.elbix.elvideo.data.WatchProgress
import io.github.elbix.elvideo.openVideo

private val PagePadding = 20.dp

// ---- 이어서 보기 -------------------------------------------------------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ContinueScreen(wide: Boolean, onBrowse: () -> Unit) {
    val context = LocalContext.current
    val progress = context.app.progress
    val revision by progress.revision.collectAsState()
    val list = remember(revision) { progress.inProgress() }
    val open = { e: WatchProgress.Entry ->
        openVideo(context, e.uri, e.treeUri.ifEmpty { null }, e.parentDocId.ifEmpty { null }, e.folder.ifEmpty { null })
    }

    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(PagePadding), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item { PageTitle("이어서 보기", Modifier.padding(top = 8.dp)) }
        if (list.isEmpty()) {
            item {
                EmptyState(ElvIcons.Continue, "보던 동영상이 없어요", "폴더에서 강의를 재생하면 멈춘 곳이 여기에 모입니다.",
                    Modifier.padding(top = 48.dp)) { PillButton("폴더 둘러보기", onBrowse) }
            }
        } else if (wide) {
            item {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    for (e in list) ContinueCard(e, Modifier.width(300.dp)) { open(e) }
                }
            }
        } else {
            item { ContinueCard(list.first(), Modifier.fillMaxWidth()) { open(list.first()) } }
            if (list.size > 1) {
                item { SectionLabel("최근") }
                item {
                    Card(Modifier.fillMaxWidth()) {
                        list.drop(1).forEachIndexed { i, e ->
                            if (i > 0) Hairline(inset = 112.dp)
                            Row(
                                Modifier.fillMaxWidth().pressable { open(e) }.padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                VideoThumb(e.uri, e.fraction, Modifier.size(width = 88.dp, height = 50.dp))
                                Spacer(Modifier.width(12.dp))
                                Column(Modifier.weight(1f)) {
                                    Text(e.name.substringBeforeLast('.'), style = Type.bodyStrong, color = Elv.colors.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    Text(continueMeta(e), style = Type.caption, color = Elv.colors.textDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

private fun continueMeta(e: WatchProgress.Entry): String = buildList {
    if (e.folder.isNotEmpty()) add(e.folder)
    add("${formatClock(e.position)}까지 봄")
    if (e.duration > 0) add("${formatRemaining(e.remaining)} 남음")
}.joinToString(" · ")

@Composable
private fun ContinueCard(e: WatchProgress.Entry, modifier: Modifier, onClick: () -> Unit) {
    val c = Elv.colors
    Card(modifier.pressable(onClick = onClick)) {
        Box(Modifier.fillMaxWidth().aspectRatio(16f / 9f)) {
            VideoThumb(e.uri, e.fraction, Modifier.fillMaxSize(), radius = 0.dp)
            Box(
                Modifier.align(Alignment.Center).size(52.dp).clip(CircleShape).background(Color.White.copy(alpha = 0.92f)),
                contentAlignment = Alignment.Center,
            ) { Icon(ElvIcons.Play, "재생", Modifier.size(20.dp).padding(start = 2.dp), tint = Color(0xFF1D1D1F)) }
        }
        Column(Modifier.padding(horizontal = 14.dp, vertical = 12.dp), verticalArrangement = Arrangement.spacedBy(3.dp)) {
            Text(e.name.substringBeforeLast('.'), style = Type.headline, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(continueMeta(e), style = Type.caption, color = c.textDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

// ---- 폴더 목록 (휴대폰) --------------------------------------------------------

@Composable
fun FoldersScreen(
    folders: List<LibraryFolder>,
    onOpen: (LibraryFolder) -> Unit,
    onAdd: () -> Unit,
    onAddDrive: () -> Unit,
    onOpenFile: () -> Unit,
    onRemove: (LibraryFolder) -> Unit,
) {
    val c = Elv.colors
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(PagePadding), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item { PageTitle("폴더", Modifier.padding(top = 8.dp)) }
        if (folders.isEmpty()) {
            item {
                EmptyState(ElvIcons.Folder, "강의 폴더를 추가하세요",
                    "휴대폰, SD 카드나 Google Drive의 동영상 폴더를 고르면 하위 폴더까지 강의 목록으로 보여 줍니다.",
                    Modifier.padding(top = 32.dp))
            }
        } else {
            item {
                Card(Modifier.fillMaxWidth()) {
                    folders.forEachIndexed { i, f ->
                        if (i > 0) Hairline(inset = 52.dp)
                        var menu by remember { mutableStateOf(false) }
                        Box {
                            Row(
                                Modifier.fillMaxWidth().pressable(onLongClick = { menu = true }) { onOpen(f) }
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Icon(if (Drive.isDriveTree(f.treeUri)) ElvIcons.Cloud else ElvIcons.Folder, null, Modifier.size(22.dp), tint = c.accent)
                                Spacer(Modifier.width(14.dp))
                                Column(Modifier.weight(1f)) {
                                    Text(f.name, style = Type.body, color = c.text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    if (Drive.isDriveTree(f.treeUri)) Text("Google Drive", style = Type.caption, color = c.textDim)
                                }
                                Icon(ElvIcons.Chevron, null, Modifier.size(16.dp), tint = c.textFaint)
                            }
                            ElvMenu(menu, { menu = false }) {
                                ElvMenuItem("목록에서 빼기", destructive = true) { menu = false; onRemove(f) }
                            }
                        }
                    }
                }
            }
        }
        item {
            Card(Modifier.fillMaxWidth()) {
                ActionRow("이 기기의 폴더 추가…", ElvIcons.Phone, onAdd)
                Hairline(inset = 52.dp)
                ActionRow("Google Drive 폴더 추가…", ElvIcons.Cloud, onAddDrive)
                Hairline(inset = 52.dp)
                ActionRow("파일 열기…", ElvIcons.File, onOpenFile)
            }
        }
        if (folders.isNotEmpty()) {
            item { Text("폴더를 길게 누르면 목록에서 뺄 수 있습니다. 파일은 지워지지 않습니다.", style = Type.caption, color = c.textDim, modifier = Modifier.padding(horizontal = 4.dp)) }
        }
    }
}

@Composable
private fun ActionRow(label: String, icon: androidx.compose.ui.graphics.vector.ImageVector, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().pressable(onClick = onClick).padding(horizontal = 16.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, null, Modifier.size(22.dp), tint = Elv.colors.accent)
        Spacer(Modifier.width(14.dp))
        Text(label, style = Type.body, color = Elv.colors.accent)
    }
}

// Menus: a raised surface with a hairline, items in body text.
@Composable
fun ElvMenu(expanded: Boolean, onDismiss: () -> Unit, content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit) {
    DropdownMenu(
        expanded = expanded, onDismissRequest = onDismiss,
        shape = RoundedCornerShape(Radius.medium),
        containerColor = Elv.colors.surfaceRaised,
        tonalElevation = 0.dp, shadowElevation = 8.dp,
        border = androidx.compose.foundation.BorderStroke(1.dp, Elv.colors.border),
        content = content,
    )
}

@Composable
fun ElvMenuItem(label: String, destructive: Boolean = false, onClick: () -> Unit) {
    DropdownMenuItem(
        text = { Text(label, style = Type.body, color = if (destructive) Elv.colors.danger else Elv.colors.text) },
        onClick = onClick,
    )
}

// ---- 강의 폴더 -------------------------------------------------------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun FolderScreen(
    folder: LibraryFolder,
    path: List<Crumb>,
    wide: Boolean,
    onOpenSub: (Crumb) -> Unit,
    onCrumb: (Int) -> Unit,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    val progress = context.app.progress
    val c = Elv.colors
    val here = path.last()
    var listing by remember(here.docId) { mutableStateOf<Listing?>(null) }
    var failure by remember(here.docId) { mutableStateOf<String?>(null) }
    var attempt by remember(here.docId) { mutableStateOf(0) }
    LaunchedEffect(here.docId, attempt) {
        failure = null
        val l = runCatching { Folders.list(context, folder.treeUri, here.docId, progress) }
            .onFailure { failure = if (it is Drive.NotSignedIn) "Google Drive에 로그인해야 이 폴더를 볼 수 있습니다." else "폴더를 읽을 수 없습니다. 연결을 확인하세요." }
            .getOrNull() ?: return@LaunchedEffect
        listing = l
        Folders.probeDurations(context, l.videos, progress)
    }
    val revision by progress.revision.collectAsState()
    var query by remember(here.docId) { mutableStateOf("") }

    val l = listing
    val videos = l?.videos.orEmpty()
    val entries = remember(videos, revision) { videos.associate { it.uri to progress.get(it.uri) } }
    val watched = entries.values.count { it?.watched == true }
    val remaining = videos.sumOf { v ->
        val e = entries[v.uri]
        when {
            e == null -> 0.0
            e.watched -> 0.0
            e.duration > 0 -> e.duration - e.position
            else -> 0.0
        }
    }
    // The lecture to continue: the one stopped part-way most recently, else the first not watched.
    val next = remember(videos, revision) {
        videos.filter { entries[it.uri]?.let { e -> !e.watched && e.position >= 5 } == true }
            .maxByOrNull { entries[it.uri]!!.updated }
            ?: videos.firstOrNull { entries[it.uri]?.watched != true }
    }
    val shown = if (query.isBlank()) videos else videos.filter { it.title.contains(query.trim(), ignoreCase = true) }
    val play = { v: Video -> openVideo(context, v.uri, folder.treeUri, here.docId, here.name) }

    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(PagePadding), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        // Where we are: the parent as a back link on phones, the whole path when wide.
        item {
            if (wide) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    path.forEachIndexed { i, crumb ->
                        if (i > 0) Text(" › ", style = Type.caption, color = c.textFaint)
                        val last = i == path.lastIndex
                        Text(crumb.name, style = Type.caption, color = if (last) c.textDim else c.accent,
                            modifier = if (last) Modifier else Modifier.pressable { onCrumb(i) })
                    }
                }
            } else {
                Row(Modifier.pressable(onClick = onBack).padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(ElvIcons.Back, null, Modifier.size(18.dp), tint = c.accent)
                    Text(if (path.size > 1) path[path.lastIndex - 1].name else "폴더", style = Type.body, color = c.accent)
                }
            }
        }
        item {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        PageTitle(here.name)
                        val stats = buildList {
                            if (l == null) add(if (failure != null) "읽지 못함" else "읽는 중…")
                            else {
                                add("동영상 ${videos.size}개")
                                if (videos.isNotEmpty()) add(if (watched == videos.size) "다 봄" else "${watched}개 다 봄")
                                if (remaining > 0) add("약 ${formatRemaining(remaining)} 남음")
                            }
                        }
                        Text(stats.joinToString(" · "), style = Type.callout, color = c.textDim)
                    }
                }
                if (videos.isNotEmpty()) ThinProgress(watched.toFloat() / videos.size, Modifier.padding(top = 8.dp))
            }
        }
        // Under the title, not beside it: lecture names are long and would squeeze the title.
        if (next != null) {
            item {
                PillButton(continueLabel(next, entries[next.uri]), { play(next) },
                    if (wide) Modifier.widthIn(max = 480.dp) else Modifier.fillMaxWidth(), icon = ElvIcons.Play, height = 46.dp)
            }
        }
        if (l != null && l.subfolders.isNotEmpty()) {
            item { SectionLabel("하위 폴더") }
            item {
                if (wide) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        for (s in l.subfolders) SubfolderTile(s.name, s.videoUris, revision, Modifier.width(220.dp)) { onOpenSub(Crumb(s.docId, s.name)) }
                    }
                } else {
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        items(l.subfolders, key = { it.docId }) { s ->
                            SubfolderTile(s.name, s.videoUris, revision, Modifier.width(160.dp)) { onOpenSub(Crumb(s.docId, s.name)) }
                        }
                    }
                }
            }
        }
        if (videos.isNotEmpty()) {
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    SectionLabel("강의", Modifier.weight(1f))
                    if (videos.size > 8) SearchField(query, { query = it }, Modifier.widthIn(max = 220.dp))
                }
            }
            item {
                Card(Modifier.fillMaxWidth()) {
                    shown.forEachIndexed { i, v ->
                        if (i > 0) Hairline()
                        LectureRow(videos.indexOf(v) + 1, v, entries[v.uri], v.uri == next?.uri, wide,
                            onClick = { play(v) },
                            onToggle = {
                                val ctx = WatchProgress.Entry(v.uri, name = v.name, folder = here.name, treeUri = folder.treeUri, parentDocId = here.docId)
                                progress.setWatched(v.uri, entries[v.uri]?.watched != true, ctx)
                            })
                    }
                    if (shown.isEmpty()) Text("찾는 강의가 없습니다", style = Type.callout, color = c.textDim, modifier = Modifier.padding(16.dp))
                }
            }
        } else if (l != null && l.subfolders.isEmpty()) {
            item { EmptyState(ElvIcons.Folder, "동영상이 없어요", "이 폴더에는 재생할 수 있는 동영상이 없습니다.") }
        }
        val problem = failure
        if (problem != null && l == null) {
            item {
                val connect = LocalConnectDrive.current
                EmptyState(ElvIcons.Cloud, "폴더를 열 수 없어요", problem) {
                    if (Drive.isDriveTree(folder.treeUri) && connect != null) {
                        PillButton("Google 계정 연결", { connect { attempt++ } })
                    } else {
                        PillButton("다시 시도", { attempt++ })
                    }
                }
            }
        }
    }
}

private fun continueLabel(v: Video, e: WatchProgress.Entry?): String =
    if (e != null && e.position >= 5 && !e.watched) "이어서 재생 · ${v.title} ${formatClock(e.position)}부터"
    else "재생 · ${v.title}"

@Composable
private fun SubfolderTile(name: String, uris: List<String>, revision: Int, modifier: Modifier, onClick: () -> Unit) {
    val c = Elv.colors
    val progress = LocalContext.current.app.progress
    val done = remember(uris, revision) { uris.count { progress.get(it)?.watched == true } }
    Card(modifier.pressable(onClick = onClick)) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(ElvIcons.Folder, null, Modifier.size(24.dp), tint = c.accent)
            Text(name, style = Type.bodyStrong, color = c.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
            ThinProgress(if (uris.isEmpty()) 0f else done.toFloat() / uris.size, height = 3.dp)
            Text(
                when {
                    uris.isEmpty() -> "동영상 없음"
                    done == uris.size -> "${uris.size}개 · 다 봄"
                    done == 0 -> "${uris.size}개 · 아직 안 봄"
                    else -> "${uris.size}개 · ${done}개 다 봄"
                },
                style = Type.fine, color = c.textDim,
            )
        }
    }
}

@Composable
private fun LectureRow(no: Int, v: Video, e: WatchProgress.Entry?, isNext: Boolean, wide: Boolean, onClick: () -> Unit, onToggle: () -> Unit) {
    val c = Elv.colors
    val watched = e?.watched == true
    val meta = when {
        watched -> "다 봄"
        e != null && e.position >= 5 -> "${formatClock(e.position)}까지 봄" + if (isNext) " · 이어서 재생" else ""
        else -> "안 봄"
    }
    Row(
        Modifier.fillMaxWidth()
            .background(if (isNext) c.accentSoft.copy(alpha = c.accentSoft.alpha * 0.5f) else Color.Transparent)
            .pressable(onClick = onClick)
            .padding(start = 14.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("$no", style = Type.caption, color = c.textDim, modifier = Modifier.width(24.dp))
        VideoThumb(v.uri, e?.fraction ?: 0f, Modifier.size(width = if (wide) 96.dp else 80.dp, height = if (wide) 54.dp else 45.dp))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(v.title, style = Type.bodyStrong, color = if (isNext) c.accent else c.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
            val duration = e?.duration?.takeIf { it > 0 }?.let { formatClock(it) }
            Text(listOfNotNull(meta, if (!wide) duration else null).joinToString(" · "), style = Type.caption, color = c.textDim, maxLines = 1)
        }
        if (wide) {
            val duration = e?.duration?.takeIf { it > 0 }?.let { formatClock(it) } ?: ""
            Spacer(Modifier.width(16.dp))
            Text(duration, style = Type.caption, color = c.textDim)
        }
        CheckCircle(watched, onToggle)
    }
}

@Composable
fun SearchField(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier) {
    val c = Elv.colors
    Row(
        modifier.height(34.dp).clip(RoundedCornerShape(50)).background(c.fill).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(ElvIcons.Search, null, Modifier.size(14.dp), tint = c.textDim)
        Spacer(Modifier.width(6.dp))
        Box(Modifier.weight(1f)) {
            if (value.isEmpty()) Text("강의 찾기", style = Type.callout, color = c.textFaint)
            BasicTextField(value, onChange, singleLine = true, textStyle = Type.callout.copy(color = c.text),
                cursorBrush = SolidColor(c.accent), modifier = Modifier.fillMaxWidth())
        }
    }
}
