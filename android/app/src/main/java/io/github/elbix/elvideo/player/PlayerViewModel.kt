package io.github.elbix.elvideo.player

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.text.Cue
import androidx.media3.common.text.CueGroup
import androidx.media3.datasource.DefaultDataSource
import androidx.media3.datasource.DefaultHttpDataSource
import androidx.media3.datasource.ResolvingDataSource
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import io.github.elbix.elvideo.app
import io.github.elbix.elvideo.data.Drive
import io.github.elbix.elvideo.data.Folders
import io.github.elbix.elvideo.data.WatchProgress
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

// One lecture in the playlist: the videos of the folder it was opened from.
data class QueueItem(val uri: String, val name: String, val title: String, val subtitleUri: String?, val subtitleMime: String? = null)

// Owns the player so it survives folding, unfolding and rotating, and keeps
// the watch progress (WatchProgress) current: every few seconds while playing,
// on pause, on leaving a lecture and when the player closes.
class PlayerViewModel(application: Application) : AndroidViewModel(application) {
    private val settings = application.app.settings
    private val progress = application.app.progress

    // drive://<id> becomes the Drive API's media URL with the account's token;
    // everything else (content://, file://) goes to the default sources.
    private val dataSources = ResolvingDataSource.Factory(
        DefaultDataSource.Factory(application, DefaultHttpDataSource.Factory().setAllowCrossProtocolRedirects(true)),
    ) { spec ->
        if (spec.uri.scheme != Drive.SCHEME) spec
        else {
            val token = Drive.tokenBlocking(application) ?: throw Drive.NotSignedIn()
            spec.buildUpon()
                .setUri(Uri.parse(Drive.mediaUrl(spec.uri.host ?: "")))
                .setHttpRequestHeaders(spec.httpRequestHeaders + ("Authorization" to "Bearer $token"))
                .build()
        }
    }

    val player: ExoPlayer = ExoPlayer.Builder(application)
        .setMediaSourceFactory(DefaultMediaSourceFactory(dataSources))
        .setAudioAttributes(AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), true)
        .setHandleAudioBecomingNoisy(true)
        .build()

    var queue by mutableStateOf<List<QueueItem>>(emptyList())
        private set
    var index by mutableIntStateOf(0)
        private set
    var folderName by mutableStateOf("")
        private set
    var isPlaying by mutableStateOf(false)
        private set
    var cues by mutableStateOf<List<Cue>>(emptyList())
        private set
    var error by mutableStateOf<String?>(null)
        private set
    // Set when a lecture starts part-way through, for the "이어서 재생" note.
    var resumedAt by mutableStateOf<Double?>(null)
    var speed by mutableStateOf(settings.speed.value)
        private set

    private var loadedUri: String? = null
    private var treeUri = ""
    private var parentDocId = ""

    val current: QueueItem? get() = queue.getOrNull(index)

    init {
        player.addListener(object : Player.Listener {
            override fun onIsPlayingChanged(playing: Boolean) {
                isPlaying = playing
                if (!playing) record()
            }

            override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
                val previous = current
                index = player.currentMediaItemIndex
                if (reason == Player.MEDIA_ITEM_TRANSITION_REASON_AUTO && previous != null) {
                    progress.setWatched(previous.uri, true, context(previous))
                }
                if (reason == Player.MEDIA_ITEM_TRANSITION_REASON_AUTO) resumeIfStarted(index)
                cues = emptyList()
            }

            override fun onPlaybackStateChanged(state: Int) {
                if (state == Player.STATE_ENDED) current?.let { progress.setWatched(it.uri, true, context(it)) }
            }

            override fun onCues(cueGroup: CueGroup) { cues = cueGroup.cues }

            override fun onPlayerError(e: PlaybackException) {
                error = if (e.cause is Drive.NotSignedIn || e.cause?.cause is Drive.NotSignedIn) "Google Drive에 다시 로그인해야 합니다 (설정 → Google Drive)"
                else if (e.errorCode in 2000..2999) "동영상을 받아 오지 못했습니다. 연결을 확인하세요 (${e.errorCodeName})"
                else "재생할 수 없는 파일입니다 (${e.errorCodeName})"
            }
        })
        viewModelScope.launch {
            while (isActive) {
                delay(5000)
                if (player.isPlaying) record()
            }
        }
    }

    // Opens `uri`; with the library folder it is in, the folder's videos become the playlist.
    fun load(uri: String, tree: String?, parent: String?, folder: String?) {
        if (uri == loadedUri) return
        record()
        loadedUri = uri
        error = null
        treeUri = tree.orEmpty()
        parentDocId = parent.orEmpty()
        viewModelScope.launch {
            val ctx = getApplication<Application>()
            var items = emptyList<QueueItem>()
            if (tree != null && parent != null) {
                items = runCatching {
                    Folders.list(ctx, tree, parent, progress).videos.map { QueueItem(it.uri, it.name, it.title, it.subtitleUri, it.subtitleMime) }
                }.getOrDefault(emptyList())
            }
            var start = items.indexOfFirst { it.uri == uri }
            if (start < 0) {
                val name = progress.get(uri)?.name?.ifEmpty { null } ?: Folders.displayName(ctx, Uri.parse(uri))
                    ?: Uri.parse(uri).lastPathSegment?.substringAfterLast('/') ?: "동영상"
                items = listOf(QueueItem(uri, name, name.substringBeforeLast('.'), null))
                start = 0
            }
            folderName = folder.orEmpty()
            queue = items
            index = start
            player.setMediaItems(items.map(::mediaItem), start, startPositionMs(items[start]))
            player.pauseAtEndOfMediaItems = !settings.autoNext.value
            changeSpeed(settings.speed.value)
            player.prepare()
            player.play()
        }
    }

    fun playIndex(i: Int) {
        if (i !in queue.indices) return
        record()
        player.seekTo(i, startPositionMs(queue[i]))
        player.play()
    }

    fun next() = playIndex(index + 1)
    fun previous() = if (player.currentPosition > 5000) player.seekTo(0) else playIndex(index - 1)

    fun togglePlay() {
        if (player.playbackState == Player.STATE_ENDED) player.seekTo(0)
        if (player.isPlaying) player.pause() else player.play()
    }

    fun seekBy(seconds: Int) {
        val target = (player.currentPosition + seconds * 1000L).coerceIn(0, player.duration.takeIf { it > 0 } ?: Long.MAX_VALUE)
        player.seekTo(target)
    }

    fun changeSpeed(v: Float) {
        speed = v
        player.setPlaybackSpeed(v)
    }

    // Plays from the start, for the note shown when a lecture resumed part-way.
    fun restart() {
        resumedAt = null
        player.seekTo(0)
    }

    private fun startPositionMs(item: QueueItem): Long {
        val e = progress.get(item.uri)
        if (!settings.resume.value || e == null || e.watched || e.position < 5) { resumedAt = null; return 0 }
        resumedAt = e.position
        return (e.position * 1000).toLong()
    }

    private fun resumeIfStarted(i: Int) {
        val item = queue.getOrNull(i) ?: return
        val ms = startPositionMs(item)
        if (ms > 0) player.seekTo(ms)
    }

    private fun context(item: QueueItem) = WatchProgress.Entry(
        uri = item.uri, name = item.name, folder = folderName, treeUri = treeUri, parentDocId = parentDocId,
    )

    fun record() {
        val item = current ?: return
        if (player.playbackState == Player.STATE_IDLE || player.playbackState == Player.STATE_ENDED) return
        val duration = player.duration.takeIf { it != C.TIME_UNSET && it > 0 }?.div(1000.0) ?: -1.0
        progress.update(item.uri, player.currentPosition / 1000.0, duration, context(item))
    }

    private fun mediaItem(item: QueueItem): MediaItem {
        val b = MediaItem.Builder().setUri(item.uri).setMediaId(item.uri)
        item.subtitleUri?.let { sub ->
            val mime = item.subtitleMime ?: if (sub.lowercase().endsWith("vtt")) MimeTypes.TEXT_VTT else MimeTypes.APPLICATION_SUBRIP
            b.setSubtitleConfigurations(listOf(
                MediaItem.SubtitleConfiguration.Builder(Uri.parse(sub)).setMimeType(mime)
                    .setSelectionFlags(C.SELECTION_FLAG_DEFAULT).build()
            ))
        }
        return b.build()
    }

    override fun onCleared() {
        record()
        progress.flush()
        player.release()
    }
}
