package io.github.elbix.elvideo.data

import android.content.Context
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.text.Collator
import java.util.Locale
import kotlin.coroutines.coroutineContext

data class Video(
    val uri: String,
    val docId: String,
    val name: String,          // file name
    val title: String,         // without the extension
    val size: Long,
    val subtitleUri: String?,  // a same-named .srt / .vtt next to it
)

data class SubFolder(
    val docId: String,
    val name: String,
    val videoUris: List<String>,  // every video below it, for the watched count
)

data class Listing(
    val subfolders: List<SubFolder>,
    val videos: List<Video>,
)

// Reads folders the user picked through the system's document picker (local
// storage, SD cards, USB, any document provider), like FolderListing.cpp on the
// desktop: subfolders first, then videos, both in the order people number them.
object Folders {

    private val videoExtensions = setOf(
        "mp4", "m4v", "mkv", "webm", "mov", "avi", "wmv", "flv", "ts", "m2ts", "mts", "3gp", "mpg", "mpeg", "ogv",
    )
    private val subtitleExtensions = listOf("srt", "vtt")

    fun rootDocId(treeUri: Uri): String = DocumentsContract.getTreeDocumentId(treeUri)

    fun documentUri(treeUri: Uri, docId: String): Uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId)

    private data class Child(val docId: String, val name: String, val mime: String, val size: Long)

    private fun children(context: Context, treeUri: Uri, docId: String): List<Child> {
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId)
        val cols = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE)
        val out = ArrayList<Child>()
        runCatching {
            context.contentResolver.query(uri, cols, null, null, null)?.use { c ->
                while (c.moveToNext()) {
                    out += Child(c.getString(0), c.getString(1) ?: "", c.getString(2) ?: "", if (c.isNull(3)) 0 else c.getLong(3))
                }
            }
        }
        return out
    }

    private fun extension(name: String) = name.substringAfterLast('.', "").lowercase(Locale.ROOT)

    fun isVideo(name: String, mime: String) =
        mime.startsWith("video/") || extension(name) in videoExtensions

    suspend fun list(context: Context, treeUri: Uri, docId: String): Listing = withContext(Dispatchers.IO) {
        val kids = children(context, treeUri, docId)
        val subtitles = kids.filter { extension(it.name) in subtitleExtensions }
            .associateBy { it.name.substringBeforeLast('.').lowercase(Locale.ROOT) }

        val videos = kids.filter { it.mime != Document.MIME_TYPE_DIR && !it.name.startsWith(".") && isVideo(it.name, it.mime) }
            .map {
                val base = it.name.substringBeforeLast('.')
                Video(
                    uri = documentUri(treeUri, it.docId).toString(),
                    docId = it.docId,
                    name = it.name,
                    title = base,
                    size = it.size,
                    subtitleUri = subtitles[base.lowercase(Locale.ROOT)]?.let { s -> documentUri(treeUri, s.docId).toString() },
                )
            }
            .sortedWith(compareBy(NaturalOrder) { it.name })

        val folders = kids.filter { it.mime == Document.MIME_TYPE_DIR && !it.name.startsWith(".") }
            .sortedWith(compareBy(NaturalOrder) { it.name })
            .map {
                coroutineContext.ensureActive()
                SubFolder(it.docId, it.name, videosBelow(context, treeUri, it.docId, depth = 4))
            }
        Listing(folders, videos)
    }

    private fun videosBelow(context: Context, treeUri: Uri, docId: String, depth: Int): List<String> {
        if (depth == 0) return emptyList()
        val out = ArrayList<String>()
        for (c in children(context, treeUri, docId)) {
            if (c.name.startsWith(".")) continue
            if (c.mime == Document.MIME_TYPE_DIR) out += videosBelow(context, treeUri, c.docId, depth - 1)
            else if (isVideo(c.name, c.mime)) out += documentUri(treeUri, c.docId).toString()
            if (out.size >= 2000) break
        }
        return out
    }

    // Durations for the progress bars and "time left"; one file at a time,
    // skipping those already known.
    suspend fun probeDurations(context: Context, videos: List<Video>, progress: WatchProgress) = withContext(Dispatchers.IO) {
        for (v in videos) {
            coroutineContext.ensureActive()
            if ((progress.get(v.uri)?.duration ?: -1.0) > 0) continue
            val seconds = probeDuration(context, Uri.parse(v.uri)) ?: continue
            progress.setDuration(v.uri, seconds)
        }
    }

    // The container's duration. Android reports 0 for fragmented MP4 (what
    // screen recorders, the desktop ELVideo among them, write) even when its
    // movie header holds the length, so then the header is read directly.
    fun probeDuration(context: Context, uri: Uri): Double? {
        val fromRetriever = runCatching {
            MediaMetadataRetriever().use { r ->
                r.setDataSource(context, uri)
                r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()?.let { it / 1000.0 }
            }
        }.getOrNull()
        if (fromRetriever != null && fromRetriever > 0) return fromRetriever
        return runCatching {
            context.contentResolver.openInputStream(uri)?.use { mp4MovieDuration(it.readNBytes(256 * 1024)) }
        }.getOrNull()
    }

    // moov/mvhd: timescale and duration, in the first bytes of the file.
    private fun mp4MovieDuration(b: ByteArray): Double? {
        fun u32(o: Int) = ((b[o].toLong() and 0xFF) shl 24) or ((b[o + 1].toLong() and 0xFF) shl 16) or
            ((b[o + 2].toLong() and 0xFF) shl 8) or (b[o + 3].toLong() and 0xFF)
        fun type(o: Int) = String(b, o + 4, 4, Charsets.ISO_8859_1)
        fun find(start: Int, end: Int, name: String): Int {
            var o = start
            while (o + 8 <= end) {
                var size = u32(o)
                if (size == 1L && o + 16 <= end) size = (u32(o + 8) shl 32) or u32(o + 12)
                if (type(o) == name) return o
                if (size < 8) return -1
                o += size.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            }
            return -1
        }
        val moov = find(0, b.size, "moov").takeIf { it >= 0 } ?: return null
        val mvhd = find(moov + 8, minOf(b.size, moov + u32(moov).toInt()), "mvhd").takeIf { it >= 0 } ?: return null
        val v1 = b[mvhd + 8].toInt() == 1
        val (scale, duration) = if (v1) u32(mvhd + 28) to ((u32(mvhd + 32) shl 32) or u32(mvhd + 36))
        else u32(mvhd + 20) to u32(mvhd + 24)
        return if (scale > 0 && duration > 0 && duration != 0xFFFFFFFFL) duration.toDouble() / scale else null
    }

    fun displayName(context: Context, uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull()
}

// "2강" before "10강": runs of digits compare as numbers, the rest as Korean text.
object NaturalOrder : Comparator<String> {
    private val collator = Collator.getInstance(Locale.KOREAN).apply { strength = Collator.SECONDARY }
    private val chunk = Regex("\\d+|\\D+")

    override fun compare(a: String, b: String): Int {
        val x = chunk.findAll(a).map { it.value }.toList()
        val y = chunk.findAll(b).map { it.value }.toList()
        for (i in 0 until minOf(x.size, y.size)) {
            val p = x[i]
            val q = y[i]
            val r = if (p[0].isDigit() && q[0].isDigit()) {
                val n = p.trimStart('0').length.compareTo(q.trimStart('0').length)
                if (n != 0) n else p.trimStart('0').compareTo(q.trimStart('0'))
            } else {
                collator.compare(p, q)
            }
            if (r != 0) return r
        }
        return x.size.compareTo(y.size)
    }
}
