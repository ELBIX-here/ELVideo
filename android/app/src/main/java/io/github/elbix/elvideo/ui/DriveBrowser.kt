package io.github.elbix.elvideo.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.elbix.elvideo.Crumb
import io.github.elbix.elvideo.data.Drive
import io.github.elbix.elvideo.data.Folders
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

// Signing in to Drive needs the activity (its consent screen), so the main
// screen provides it to everything below; null until it is set up.
val LocalConnectDrive = compositionLocalOf<((onConnected: () -> Unit) -> Unit)?> { null }

private data class DriveFolder(val id: String, val name: String, val videos: Int)

// Picks a Drive folder for the library: My Drive, files shared with me and
// shared drives at the top, then folder by folder. Drive has no system picker
// for folders on Android, so this is the app's own.
@Composable
fun DriveBrowser(onPick: (id: String, name: String) -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    val c = Elv.colors
    val path = remember { mutableStateListOf<Crumb>() }
    val here = path.lastOrNull()
    var folders by remember { mutableStateOf<List<DriveFolder>?>(null) }
    var failure by remember { mutableStateOf<String?>(null) }
    var attempt by remember { mutableIntStateOf(0) }

    LaunchedEffect(here?.docId, attempt) {
        folders = null
        failure = null
        folders = runCatching {
            withContext(Dispatchers.IO) {
                if (here == null) {
                    listOf(DriveFolder(Drive.ROOT, "내 드라이브", -1), DriveFolder(Drive.SHARED_WITH_ME, "공유 문서함", -1)) +
                        Drive.sharedDrives(context).map { DriveFolder(it.id, it.name, -1) }
                } else {
                    val kids = Drive.children(context, here.docId)
                    kids.filter { it.isFolder }.sortedWith(compareBy(io.github.elbix.elvideo.data.NaturalOrder) { it.name })
                        .map { DriveFolder(it.id, it.name, -1) }
                }
            }
        }.onFailure {
            failure = if (it is Drive.NotSignedIn) "Google Drive에 로그인해야 합니다." else "Drive를 읽을 수 없습니다. 연결을 확인하세요."
        }.getOrNull()
    }
    // How many videos are right here, so the user can tell the lecture folder.
    var videosHere by remember { mutableStateOf<Int?>(null) }
    LaunchedEffect(here?.docId) {
        videosHere = null
        if (here == null || here.docId == Drive.SHARED_WITH_ME) return@LaunchedEffect
        videosHere = runCatching {
            withContext(Dispatchers.IO) { Drive.children(context, here.docId).count { !it.isFolder && Folders.isVideo(it.name, it.mime) } }
        }.getOrNull()
    }

    val canPick = here != null && here.docId != Drive.SHARED_WITH_ME
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item {
            Row(Modifier.pressable { if (path.isEmpty()) onClose() else path.removeAt(path.lastIndex) }.padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Icon(ElvIcons.Back, null, Modifier.size(18.dp), tint = c.accent)
                Text(if (path.size > 1) path[path.lastIndex - 1].name else if (path.isEmpty()) "폴더" else "Google Drive",
                    style = Type.body, color = c.accent)
            }
        }
        item {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                PageTitle(here?.name ?: "Google Drive")
                Text(
                    when {
                        here == null -> "강의가 들어 있는 폴더로 들어가서 추가하세요."
                        !canPick -> "다른 사람이 공유한 폴더"
                        videosHere == null -> "읽는 중…"
                        else -> "이 폴더의 동영상 ${videosHere}개 · 하위 폴더까지 함께 추가됩니다"
                    },
                    style = Type.callout, color = c.textDim,
                )
            }
        }
        if (canPick) {
            item {
                PillButton("이 폴더 추가", { onPick(here!!.docId, here.name) }, Modifier.fillMaxWidth(), icon = ElvIcons.Plus, height = 46.dp)
            }
        }
        val list = folders
        when {
            failure != null -> item {
                EmptyState(ElvIcons.Cloud, "Drive를 열 수 없어요", failure!!) { PillButton("다시 시도", { attempt++ }) }
            }
            list == null -> item { Text("읽는 중…", style = Type.callout, color = c.textDim, modifier = Modifier.padding(4.dp)) }
            list.isEmpty() -> item { Text("하위 폴더가 없습니다", style = Type.callout, color = c.textDim, modifier = Modifier.padding(4.dp)) }
            else -> item {
                Card(Modifier.fillMaxWidth()) {
                    list.forEachIndexed { i, f ->
                        if (i > 0) Hairline(inset = 52.dp)
                        Row(
                            Modifier.fillMaxWidth().pressable { path += Crumb(f.id, f.name) }.padding(horizontal = 16.dp, vertical = 14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(if (here == null && f.id == Drive.ROOT) ElvIcons.Cloud else ElvIcons.Folder, null, Modifier.size(22.dp), tint = c.accent)
                            Spacer(Modifier.width(14.dp))
                            Text(f.name, style = Type.body, color = c.text, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            Icon(ElvIcons.Chevron, null, Modifier.size(16.dp), tint = c.textFaint)
                        }
                    }
                }
            }
        }
    }
}
