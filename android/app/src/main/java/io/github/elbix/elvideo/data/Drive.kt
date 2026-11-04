package io.github.elbix.elvideo.data

import android.app.Activity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import com.google.android.gms.auth.GoogleAuthUtil
import com.google.android.gms.auth.api.identity.AuthorizationRequest
import com.google.android.gms.auth.api.identity.AuthorizationResult
import com.google.android.gms.auth.api.identity.Identity
import com.google.android.gms.common.api.Scope
import com.google.android.gms.tasks.Tasks
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.Locale
import java.util.concurrent.TimeUnit

// Google Drive, read only. Sign-in is Google Identity Services authorization
// (as in ELViewer): no client secret in the app, Google recognises it by package
// name and signing key (Android OAuth clients in the shared Cloud project).
//
// Library folders from Drive are "drive:<folderId>" and their videos
// "drive://<fileId>"; the player streams them from the Drive API with the token.
object Drive {
    const val TREE_PREFIX = "drive:"
    const val SCHEME = "drive"
    const val ROOT = "root"              // My Drive
    const val SHARED_WITH_ME = "shared"  // not a real folder: the "공유 문서함" list
    private const val FOLDER_MIME = "application/vnd.google-apps.folder"
    private const val SHORTCUT_MIME = "application/vnd.google-apps.shortcut"
    private const val API = "https://www.googleapis.com/drive/v3"
    private val scopes = listOf(Scope("https://www.googleapis.com/auth/drive.readonly"))

    fun isDriveTree(treeUri: String) = treeUri.startsWith(TREE_PREFIX)
    fun isDriveVideo(uri: String) = uri.startsWith("$SCHEME://")
    fun treeUri(folderId: String) = TREE_PREFIX + folderId
    fun folderIdOf(treeUri: String) = treeUri.removePrefix(TREE_PREFIX)
    fun videoUri(fileId: String) = "$SCHEME://$fileId"
    fun fileIdOf(uri: String) = uri.removePrefix("$SCHEME://")
    fun mediaUrl(fileId: String) = "$API/files/$fileId?alt=media&supportsAllDrives=true"

    // ---- sign-in -------------------------------------------------------------

    @Volatile private var token: String? = null
    @Volatile private var tokenAt = 0L

    private fun request() = AuthorizationRequest.builder().setRequestedScopes(scopes).build()

    // What signing in needs: a token now, or a consent screen to show first.
    sealed interface Auth {
        data class Granted(val token: String) : Auth
        data class NeedsConsent(val intent: PendingIntent) : Auth
        data class Failed(val message: String) : Auth
    }

    suspend fun authorize(activity: Activity): Auth = withContext(Dispatchers.IO) {
        runCatching { Tasks.await(Identity.getAuthorizationClient(activity).authorize(request()), 30, TimeUnit.SECONDS) }
            .fold({ toAuth(it) }, { Auth.Failed(it.message ?: "로그인할 수 없습니다") })
    }

    fun resultFromIntent(context: Context, data: Intent?): Auth =
        runCatching { toAuth(Identity.getAuthorizationClient(context).getAuthorizationResultFromIntent(data)) }
            .getOrElse { Auth.Failed(it.message ?: "로그인을 취소했습니다") }

    private fun toAuth(r: AuthorizationResult): Auth {
        val pending = r.pendingIntent
        if (r.hasResolution() && pending != null) return Auth.NeedsConsent(pending)
        val t = r.accessToken ?: return Auth.Failed("토큰을 받지 못했습니다")
        if (!r.grantedScopes.containsAll(scopes.map { it.scopeUri })) return Auth.Failed("Drive 읽기 권한을 허용해야 합니다")
        token = t
        tokenAt = System.currentTimeMillis()
        return Auth.Granted(t)
    }

    // A token without showing anything, for listing and playing; null when the
    // user has to sign in (again) from the app. Blocks: call off the main thread.
    fun tokenBlocking(context: Context, refresh: Boolean = false): String? {
        val cached = token
        if (!refresh && cached != null && System.currentTimeMillis() - tokenAt < 45 * 60_000) return cached
        if (refresh && cached != null) runCatching { GoogleAuthUtil.clearToken(context, cached) }
        token = null
        val r = runCatching { Tasks.await(Identity.getAuthorizationClient(context).authorize(request()), 30, TimeUnit.SECONDS) }.getOrNull()
            ?: return null
        return (toAuth(r) as? Auth.Granted)?.token
    }

    fun signOut(context: Context) {
        token?.let { t -> runCatching { GoogleAuthUtil.clearToken(context, t) } }
        token = null
    }

    // ---- API ------------------------------------------------------------------

    class NotSignedIn : IOException("Google Drive에 로그인해야 합니다")

    private fun get(context: Context, url: String): JSONObject {
        for (attempt in 0..1) {
            val t = tokenBlocking(context, refresh = attempt > 0) ?: throw NotSignedIn()
            val c = URL(url).openConnection() as HttpURLConnection
            c.setRequestProperty("Authorization", "Bearer $t")
            c.connectTimeout = 15_000
            c.readTimeout = 30_000
            try {
                val code = c.responseCode
                if (code == 401 && attempt == 0) continue
                if (code !in 200..299) throw IOException("Drive $code: " + (c.errorStream?.bufferedReader()?.readText()?.take(200) ?: ""))
                return JSONObject(c.inputStream.bufferedReader().readText())
            } finally {
                c.disconnect()
            }
        }
        throw NotSignedIn()
    }

    fun bytes(context: Context, url: String): ByteArray {
        val t = tokenBlocking(context) ?: throw NotSignedIn()
        val c = URL(url).openConnection() as HttpURLConnection
        c.setRequestProperty("Authorization", "Bearer $t")
        c.connectTimeout = 15_000
        c.readTimeout = 30_000
        try {
            if (c.responseCode !in 200..299) throw IOException("Drive ${c.responseCode}")
            return c.inputStream.readBytes()
        } finally {
            c.disconnect()
        }
    }

    data class Item(
        val id: String,
        val name: String,
        val mime: String,
        val size: Long,
        val durationSec: Double,  // Drive's own reading of the video, -1 unknown
    ) {
        val isFolder get() = mime == FOLDER_MIME
    }

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    // Every child of a folder, shortcuts resolved to what they point at.
    fun children(context: Context, folderId: String): List<Item> {
        val q = when (folderId) {
            SHARED_WITH_ME -> "sharedWithMe and trashed = false"
            else -> "'$folderId' in parents and trashed = false"
        }
        val fields = "nextPageToken,files(id,name,mimeType,size,videoMediaMetadata(durationMillis)," +
            "shortcutDetails(targetId,targetMimeType))"
        val out = ArrayList<Item>()
        var page: String? = null
        do {
            val url = "$API/files?q=${enc(q)}&fields=${enc(fields)}&pageSize=1000" +
                "&supportsAllDrives=true&includeItemsFromAllDrives=true" + (page?.let { "&pageToken=${enc(it)}" } ?: "")
            val j = get(context, url)
            val files = j.optJSONArray("files")
            for (i in 0 until (files?.length() ?: 0)) {
                val f = files!!.getJSONObject(i)
                var id = f.getString("id")
                var mime = f.optString("mimeType")
                if (mime == SHORTCUT_MIME) {
                    val s = f.optJSONObject("shortcutDetails") ?: continue
                    id = s.optString("targetId")
                    mime = s.optString("targetMimeType")
                }
                val ms = f.optJSONObject("videoMediaMetadata")?.optString("durationMillis")?.toLongOrNull()
                out += Item(id, f.optString("name"), mime, f.optString("size").toLongOrNull() ?: 0, ms?.let { it / 1000.0 } ?: -1.0)
            }
            page = j.optString("nextPageToken").ifEmpty { null }
        } while (page != null)
        return out
    }

    // Shared drives the account can see, for the folder browser.
    fun sharedDrives(context: Context): List<Item> = runCatching {
        val j = get(context, "$API/drives?pageSize=100&fields=${enc("drives(id,name)")}")
        val arr = j.optJSONArray("drives")
        (0 until (arr?.length() ?: 0)).map { i ->
            val d = arr!!.getJSONObject(i)
            Item(d.getString("id"), d.optString("name"), FOLDER_MIME, 0, -1.0)
        }
    }.getOrDefault(emptyList())

    fun thumbnailLink(context: Context, fileId: String): String? =
        get(context, "$API/files/$fileId?supportsAllDrives=true&fields=thumbnailLink").optString("thumbnailLink").ifEmpty { null }

    private fun extension(name: String) = name.substringAfterLast('.', "").lowercase(Locale.ROOT)

    // The same listing as a local folder: subfolders (with every video below them,
    // for the watched counts) and videos with same-named subtitles.
    suspend fun list(context: Context, folderId: String, progress: WatchProgress): Listing = withContext(Dispatchers.IO) {
        val kids = children(context, folderId)
        val subs = kids.filter { extension(it.name) in setOf("srt", "vtt") }
            .associateBy { it.name.substringBeforeLast('.').lowercase(Locale.ROOT) }
        val videos = kids.filter { !it.isFolder && Folders.isVideo(it.name, it.mime) }
            .sortedWith(compareBy(NaturalOrder) { it.name })
            .map {
                val base = it.name.substringBeforeLast('.')
                if (it.durationSec > 0) progress.setDuration(videoUri(it.id), it.durationSec)
                val sub = subs[base.lowercase(Locale.ROOT)]
                Video(videoUri(it.id), it.id, it.name, base, it.size,
                    subtitleUri = sub?.let { s -> videoUri(s.id) },
                    subtitleMime = sub?.let { s -> Folders.subtitleMime(s.name) })
            }
        // Subfolders are read side by side: one request each per level.
        val folders = coroutineScope {
            kids.filter { it.isFolder }.sortedWith(compareBy(NaturalOrder) { it.name })
                .map { f -> async { SubFolder(f.id, f.name, videosBelow(context, f.id, 3)) } }
                .awaitAll()
        }
        Listing(folders, videos)
    }

    private suspend fun videosBelow(context: Context, folderId: String, depth: Int): List<String> = coroutineScope {
        if (depth == 0) return@coroutineScope emptyList()
        val kids = runCatching { children(context, folderId) }.getOrDefault(emptyList())
        val here = kids.filter { !it.isFolder && Folders.isVideo(it.name, it.mime) }.map { videoUri(it.id) }
        val below = kids.filter { it.isFolder }.map { async { videosBelow(context, it.id, depth - 1) } }.awaitAll().flatten()
        here + below
    }
}
