package io.github.elbix.elvideo

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
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
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.elbix.elvideo.data.Folders
import io.github.elbix.elvideo.data.LibraryFolder
import io.github.elbix.elvideo.player.PlayerActivity
import io.github.elbix.elvideo.ui.ContinueScreen
import io.github.elbix.elvideo.ui.Elv
import io.github.elbix.elvideo.ui.ElvIcons
import io.github.elbix.elvideo.ui.ElvTheme
import io.github.elbix.elvideo.ui.FolderScreen
import io.github.elbix.elvideo.ui.FoldersScreen
import io.github.elbix.elvideo.ui.LogoMark
import io.github.elbix.elvideo.ui.Radius
import io.github.elbix.elvideo.ui.SettingsScreen
import io.github.elbix.elvideo.ui.Type
import io.github.elbix.elvideo.ui.pressable

enum class Tab { Continue, Folders, Settings }

data class Crumb(val docId: String, val name: String)

// Where the user is: a tab, and inside "폴더" the folder they opened and the
// subfolders they went into. Lives in a ViewModel so it outlasts recreation.
class NavState : ViewModel() {
    var tab by mutableStateOf(Tab.Continue)
    var folder by mutableStateOf<LibraryFolder?>(null)
    val path = mutableStateListOf<Crumb>()

    fun openFolder(f: LibraryFolder) {
        tab = Tab.Folders
        folder = f
        path.clear()
        path += Crumb(Folders.rootDocId(Uri.parse(f.treeUri)), f.name)
    }

    fun back(): Boolean {
        if (tab == Tab.Folders && path.size > 1) { path.removeAt(path.lastIndex); return true }
        if (tab == Tab.Folders && folder != null) { folder = null; path.clear(); return true }
        if (tab != Tab.Continue) { tab = Tab.Continue; return true }
        return false
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen()
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val appearance by app.settings.appearance.collectAsState()
            ElvTheme(appearance) { MainScreen() }
        }
    }
}

fun openVideo(context: android.content.Context, uri: String, treeUri: String? = null, parentDocId: String? = null, folderName: String? = null) {
    context.startActivity(Intent(context, PlayerActivity::class.java).apply {
        data = Uri.parse(uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        treeUri?.let { putExtra(PlayerActivity.EXTRA_TREE, it) }
        parentDocId?.let { putExtra(PlayerActivity.EXTRA_PARENT, it) }
        folderName?.let { putExtra(PlayerActivity.EXTRA_FOLDER_NAME, it) }
    })
}

@Composable
private fun MainScreen(nav: NavState = viewModel()) {
    val context = LocalContext.current
    val settings = context.app.settings
    val folders by settings.folders.collectAsState()
    val revision by context.app.progress.revision.collectAsState()
    val continueCount = remember(revision) { context.app.progress.inProgress().size }

    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        runCatching {
            context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val docUri = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri))
        val name = Folders.displayName(context, docUri) ?: uri.lastPathSegment ?: "폴더"
        val f = LibraryFolder(uri.toString(), name)
        settings.addFolder(f)
        nav.openFolder(f)
    }
    val pickFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        openVideo(context, uri.toString())
    }
    val addFolder = { pickFolder.launch(null) }
    val openFile = { pickFile.launch(arrayOf("video/*")) }

    BackHandler(enabled = nav.tab != Tab.Continue || nav.folder != null) { nav.back() }

    val c = Elv.colors
    BoxWithConstraints(Modifier.fillMaxSize().background(c.background)) {
        val wide = maxWidth >= 600.dp
        val content: @Composable (Modifier) -> Unit = { m ->
            Box(m) {
                when (nav.tab) {
                    Tab.Continue -> ContinueScreen(wide = wide, onBrowse = { nav.tab = Tab.Folders })
                    Tab.Folders -> {
                        val f = nav.folder
                        if (f != null && nav.path.isNotEmpty()) {
                            FolderScreen(
                                folder = f, path = nav.path.toList(), wide = wide,
                                onOpenSub = { nav.path += it },
                                onCrumb = { i -> while (nav.path.size > i + 1) nav.path.removeAt(nav.path.lastIndex) },
                                onBack = { nav.back() },
                            )
                        } else {
                            FoldersScreen(folders = folders, onOpen = nav::openFolder, onAdd = addFolder, onOpenFile = openFile,
                                onRemove = { settings.removeFolder(it.treeUri) })
                        }
                    }
                    Tab.Settings -> SettingsScreen(wide = wide)
                }
            }
        }
        if (wide) {
            Row(Modifier.fillMaxSize()) {
                Sidebar(nav, folders, continueCount, addFolder, openFile)
                content(Modifier.weight(1f).fillMaxHeight().windowInsetsPadding(WindowInsets.safeDrawing))
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                content(Modifier.weight(1f).fillMaxWidth().windowInsetsPadding(WindowInsets.statusBars))
                BottomTabs(nav)
            }
        }
    }
}

// Phones: three tabs on a flat bar with a hairline above, no shadow.
@Composable
private fun BottomTabs(nav: NavState) {
    val c = Elv.colors
    Column(Modifier.fillMaxWidth().background(c.bar)) {
        Box(Modifier.fillMaxWidth().height(1.dp).background(c.border))
        Row(
            Modifier.fillMaxWidth().padding(WindowInsets.navigationBars.asPaddingValues()).height(58.dp),
            horizontalArrangement = Arrangement.SpaceAround,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TabItem("이어서 보기", ElvIcons.Continue, nav.tab == Tab.Continue) { nav.tab = Tab.Continue }
            TabItem("폴더", ElvIcons.Folder, nav.tab == Tab.Folders) {
                // A second tap on the tab goes back to the folder list.
                if (nav.tab == Tab.Folders) { nav.folder = null; nav.path.clear() }
                nav.tab = Tab.Folders
            }
            TabItem("설정", ElvIcons.Settings, nav.tab == Tab.Settings) { nav.tab = Tab.Settings }
        }
    }
}

@Composable
private fun TabItem(label: String, icon: ImageVector, selected: Boolean, onClick: () -> Unit) {
    val color = if (selected) Elv.colors.accent else Elv.colors.textDim
    Column(
        Modifier.width(96.dp).pressable(onClick = onClick).padding(vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        Icon(icon, null, Modifier.size(24.dp), tint = color)
        Text(label, style = if (selected) Type.fine.copy(fontWeight = FontWeight.SemiBold) else Type.fine, color = color)
    }
}

// Tablets and unfolded foldables: the desktop's sidebar.
@Composable
private fun Sidebar(nav: NavState, folders: List<LibraryFolder>, continueCount: Int, onAdd: () -> Unit, onOpenFile: () -> Unit) {
    val c = Elv.colors
    Row(Modifier.fillMaxHeight()) {
        Column(
            Modifier.width(232.dp).fillMaxHeight().background(c.sidebar)
                .windowInsetsPadding(WindowInsets.safeDrawing)
                .padding(horizontal = 12.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Row(Modifier.padding(start = 8.dp, bottom = 18.dp, top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                LogoMark(26.dp)
                Spacer(Modifier.width(10.dp))
                Text("ELVideo", style = Type.headline, color = c.text)
            }
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                SidebarItem("이어서 보기", ElvIcons.Continue, nav.tab == Tab.Continue, badge = continueCount.takeIf { it > 0 }?.toString()) { nav.tab = Tab.Continue }
                Text("폴더", style = Type.fine.copy(fontWeight = FontWeight.SemiBold), color = c.textDim,
                    modifier = Modifier.padding(start = 10.dp, top = 16.dp, bottom = 4.dp))
                for (f in folders) {
                    SidebarItem(f.name, ElvIcons.Folder, nav.tab == Tab.Folders && nav.folder?.treeUri == f.treeUri) { nav.openFolder(f) }
                }
                SidebarItem("폴더 추가…", ElvIcons.Plus, false, accent = true, onClick = onAdd)
            }
            SidebarItem("파일 열기…", ElvIcons.File, false, onClick = onOpenFile)
            SidebarItem("설정", ElvIcons.Settings, nav.tab == Tab.Settings) { nav.tab = Tab.Settings }
        }
        Box(Modifier.width(1.dp).fillMaxHeight().background(c.border))
    }
}

@Composable
private fun SidebarItem(label: String, icon: ImageVector, selected: Boolean, badge: String? = null, accent: Boolean = false, onClick: () -> Unit) {
    val c = Elv.colors
    val fg = if (selected || accent) c.accent else c.text
    Row(
        Modifier.fillMaxWidth().height(40.dp).clip(RoundedCornerShape(Radius.small))
            .background(if (selected) c.accentSoft else Color.Transparent)
            .pressable(onClick = onClick).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, null, Modifier.size(18.dp), tint = fg)
        Spacer(Modifier.width(10.dp))
        Text(label, style = if (selected) Type.callout.copy(fontWeight = FontWeight.SemiBold) else Type.callout,
            color = fg, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        if (badge != null) Text(badge, style = Type.caption, color = c.textDim)
    }
}
