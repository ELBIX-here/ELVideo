package io.github.elbix.elvideo.data

import android.content.Context
import android.util.AtomicFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

// How far each video has been watched, as on the desktop (WatchProgress.cpp):
// one entry per video, written to progress.json a moment after it changes.
// Screens read `revision` so they recompose when anything changes.
class WatchProgress(context: Context) {

    data class Entry(
        val uri: String,
        val name: String = "",
        val folder: String = "",       // display name of the folder it is in
        val treeUri: String = "",      // the library folder it was opened from, for the playlist
        val parentDocId: String = "",
        val position: Double = 0.0,    // seconds
        val duration: Double = -1.0,   // seconds, -1 unknown
        val watched: Boolean = false,
        val updated: Long = 0,
    ) {
        val remaining: Double get() = if (duration > 0) (duration - position).coerceAtLeast(0.0) else 0.0
        val fraction: Float get() = when {
            watched -> 1f
            duration > 0 -> (position / duration).toFloat().coerceIn(0f, 1f)
            else -> 0f
        }
    }

    private val file = AtomicFile(File(context.filesDir, "progress.json"))
    private val entries = HashMap<String, Entry>()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var saveJob: Job? = null

    private val _revision = MutableStateFlow(0)
    val revision: StateFlow<Int> = _revision.asStateFlow()

    init { load() }

    @Synchronized
    fun get(uri: String): Entry? = entries[uri]

    fun update(uri: String, position: Double, duration: Double, context: Entry? = null) {
        if (position < 0) return
        touch(uri, context) { e ->
            val d = if (duration > 0) duration else e.duration
            e.copy(position = position, duration = d,
                watched = e.watched || nearEnd(position, d),
                updated = System.currentTimeMillis())
        }
    }

    fun setWatched(uri: String, watched: Boolean, context: Entry? = null) = touch(uri, context) { e ->
        e.copy(watched = watched, position = if (watched) e.position else 0.0, updated = System.currentTimeMillis())
    }

    // A duration probed by a folder listing; leaves the progress alone.
    fun setDuration(uri: String, duration: Double) {
        if (duration <= 0) return
        synchronized(this) { if (entries[uri]?.duration == duration) return }
        touch(uri, null) { it.copy(duration = duration) }
    }

    // Started but not finished, most recent first.
    @Synchronized
    fun inProgress(limit: Int = 30): List<Entry> =
        entries.values.filter { !it.watched && it.position >= 5 }
            .sortedByDescending { it.updated }
            .take(limit)

    fun forget(uri: String) {
        val removed = synchronized(this) { entries.remove(uri) != null }
        if (removed) changed()
    }

    private fun touch(uri: String, context: Entry?, change: (Entry) -> Entry) {
        synchronized(this) {
            var e = entries[uri] ?: Entry(uri)
            if (context != null) {
                e = e.copy(
                    name = context.name.ifEmpty { e.name },
                    folder = context.folder.ifEmpty { e.folder },
                    treeUri = context.treeUri.ifEmpty { e.treeUri },
                    parentDocId = context.parentDocId.ifEmpty { e.parentDocId },
                )
            }
            entries[uri] = change(e)
        }
        changed()
    }

    private fun changed() {
        _revision.value++
        saveJob?.cancel()
        saveJob = scope.launch { delay(1500); save() }
    }

    // Writes now (the player calls this when it stops).
    fun flush() {
        saveJob?.cancel()
        scope.launch { save() }
    }

    @Synchronized
    private fun load() {
        entries.clear()
        val text = runCatching { file.readFully().decodeToString() }.getOrNull() ?: return
        val arr = runCatching { JSONObject(text).getJSONArray("videos") }.getOrNull() ?: return
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i) ?: continue
            val uri = o.optString("uri")
            if (uri.isEmpty()) continue
            entries[uri] = Entry(
                uri = uri,
                name = o.optString("name"),
                folder = o.optString("folder"),
                treeUri = o.optString("treeUri"),
                parentDocId = o.optString("parentDocId"),
                position = o.optDouble("position", 0.0),
                duration = o.optDouble("duration", -1.0),
                watched = o.optBoolean("watched", false),
                updated = o.optLong("updated", 0),
            )
        }
    }

    @Synchronized
    private fun save() {
        val arr = JSONArray()
        for (e in entries.values) {
            arr.put(JSONObject()
                .put("uri", e.uri).put("name", e.name).put("folder", e.folder)
                .put("treeUri", e.treeUri).put("parentDocId", e.parentDocId)
                .put("position", e.position).put("duration", e.duration)
                .put("watched", e.watched).put("updated", e.updated))
        }
        val out = file.startWrite()
        try {
            out.write(JSONObject().put("videos", arr).toString().encodeToByteArray())
            file.finishWrite(out)
        } catch (t: Throwable) {
            file.failWrite(out)
        }
    }

    companion object {
        // Near the end counts as watched: the last 5 %, or for a lecture-length
        // video (10 minutes and up) the last half minute.
        fun nearEnd(position: Double, duration: Double): Boolean {
            if (duration <= 0) return false
            return position >= duration * 0.95 || (duration >= 600 && duration - position <= 30)
        }
    }
}
