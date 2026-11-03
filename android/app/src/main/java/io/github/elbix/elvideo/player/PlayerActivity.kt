package io.github.elbix.elvideo.player

import android.app.PictureInPictureParams
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.util.Rational
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import io.github.elbix.elvideo.ui.ElvTheme
import kotlinx.coroutines.launch

class PlayerActivity : ComponentActivity() {
    companion object {
        const val EXTRA_TREE = "tree"
        const val EXTRA_PARENT = "parent"
        const val EXTRA_FOLDER_NAME = "folderName"
    }

    private val vm: PlayerViewModel by viewModels()
    private var fold by mutableStateOf<FoldingFeature?>(null)
    private var inPip by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(SystemBarStyle.dark(android.graphics.Color.TRANSPARENT), SystemBarStyle.dark(android.graphics.Color.TRANSPARENT))
        load(intent)

        // The hinge: half-open flat (tabletop) or upright (book) changes the layout.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                WindowInfoTracker.getOrCreate(this@PlayerActivity).windowLayoutInfo(this@PlayerActivity).collect { info ->
                    fold = info.displayFeatures.filterIsInstance<FoldingFeature>().firstOrNull()
                }
            }
        }

        setContent {
            ElvTheme(dark = true) {
                PlayerScreen(
                    vm = vm,
                    fold = fold,
                    inPip = inPip,
                    onBack = { finish() },
                    onPip = { enterPip() },
                    onRotate = { toggleOrientation() },
                    onImmersive = { setFullscreen(it) },
                    onBrightness = { setBrightness(it) },
                    currentBrightness = { window.attributes.screenBrightness },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        load(intent)
    }

    private fun load(intent: Intent) {
        val uri = intent.data?.toString() ?: return
        vm.load(uri, intent.getStringExtra(EXTRA_TREE), intent.getStringExtra(EXTRA_PARENT), intent.getStringExtra(EXTRA_FOLDER_NAME))
    }

    private fun pipParams(): PictureInPictureParams {
        val size = vm.player.videoSize
        val ratio = if (size.width > 0 && size.height > 0) {
            // PiP accepts ratios between 1:2.39 and 2.39:1.
            Rational(size.width, size.height).let {
                when {
                    it.toFloat() > 2.39f -> Rational(239, 100)
                    it.toFloat() < 1 / 2.39f -> Rational(100, 239)
                    else -> it
                }
            }
        } else Rational(16, 9)
        val b = PictureInPictureParams.Builder().setAspectRatio(ratio)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) b.setAutoEnterEnabled(vm.isPlaying).setSeamlessResizeEnabled(true)
        return b.build()
    }

    private fun enterPip() {
        runCatching { enterPictureInPictureMode(pipParams()) }
    }

    // Going home while a lecture plays keeps it in a small window.
    override fun onUserLeaveHint() {
        super.onUserLeaveHint()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S && vm.isPlaying) enterPip()
    }

    override fun onResume() {
        super.onResume()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) runCatching { setPictureInPictureParams(pipParams()) }
    }

    override fun onPause() {
        super.onPause()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) runCatching { setPictureInPictureParams(pipParams()) }
    }

    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean, newConfig: Configuration) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        inPip = isInPictureInPictureMode
    }

    override fun onStop() {
        super.onStop()
        // Closing the small window stops the lecture; it was saved as it played.
        if (!isInPictureInPictureMode) vm.player.pause()
        vm.record()
    }

    private fun toggleOrientation() {
        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT else ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    }

    private fun setFullscreen(on: Boolean) {
        val c = WindowCompat.getInsetsController(window, window.decorView)
        c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        if (on) c.hide(WindowInsetsCompat.Type.systemBars()) else c.show(WindowInsetsCompat.Type.systemBars())
    }

    private fun setBrightness(v: Float) {
        window.attributes = window.attributes.apply { screenBrightness = v }
    }
}
