package io.github.elbix.elvideo.data

import android.content.Context
import coil3.ImageLoader
import coil3.Uri
import coil3.decode.DataSource
import coil3.decode.ImageSource
import coil3.fetch.FetchResult
import coil3.fetch.Fetcher
import coil3.fetch.SourceFetchResult
import coil3.request.Options
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okio.Buffer

// Thumbnails of Drive videos (drive://<id>): Drive makes them, so nothing is
// downloaded from the video itself. The link Drive hands out is short-lived,
// so it is asked for each time and the picture kept in Coil's caches.
class DriveThumbnailFetcher(private val context: Context, private val fileId: String, private val options: Options) : Fetcher {
    override suspend fun fetch(): FetchResult = withContext(Dispatchers.IO) {
        val link = Drive.thumbnailLink(context, fileId) ?: error("no thumbnail")
        // Links end in a size ("=s220"); ask for one sharp enough for the big card.
        val bytes = Drive.bytes(context, link.replace(Regex("=s\\d+$"), "=s640"))
        SourceFetchResult(ImageSource(Buffer().write(bytes), options.fileSystem), "image/jpeg", DataSource.NETWORK)
    }

    class Factory(private val context: Context) : Fetcher.Factory<Uri> {
        override fun create(data: Uri, options: Options, imageLoader: ImageLoader): Fetcher? =
            if (data.scheme == Drive.SCHEME) DriveThumbnailFetcher(context.applicationContext, data.authority ?: "", options) else null
    }
}
