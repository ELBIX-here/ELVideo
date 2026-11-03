package io.github.elbix.elvideo

import android.app.Application
import android.content.Context
import coil3.ImageLoader
import coil3.PlatformContext
import coil3.SingletonImageLoader
import coil3.video.VideoFrameDecoder
import io.github.elbix.elvideo.data.Settings
import io.github.elbix.elvideo.data.WatchProgress

class ElvApp : Application(), SingletonImageLoader.Factory {
    lateinit var settings: Settings
        private set
    lateinit var progress: WatchProgress
        private set

    override fun onCreate() {
        super.onCreate()
        settings = Settings(this)
        progress = WatchProgress(this)
    }

    // Thumbnails are a frame from each video.
    override fun newImageLoader(context: PlatformContext): ImageLoader =
        ImageLoader.Builder(context)
            .components { add(VideoFrameDecoder.Factory()) }
            .build()
}

val Context.app: ElvApp get() = applicationContext as ElvApp
