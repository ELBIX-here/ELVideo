package io.github.elbix.elvideo.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONObject

// A folder the user added: a document tree the app holds a lasting permission for.
data class LibraryFolder(val treeUri: String, val name: String)

// The app's preferences, as flows the screens collect.
class Settings(context: Context) {
    private val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    private val _appearance = MutableStateFlow(prefs.getString("appearance", "system")!!)
    val appearance: StateFlow<String> = _appearance.asStateFlow()

    private val _speed = MutableStateFlow(prefs.getFloat("speed", 1f))
    val speed: StateFlow<Float> = _speed.asStateFlow()

    private val _autoNext = MutableStateFlow(prefs.getBoolean("autoNext", true))
    val autoNext: StateFlow<Boolean> = _autoNext.asStateFlow()

    private val _resume = MutableStateFlow(prefs.getBoolean("resume", true))
    val resume: StateFlow<Boolean> = _resume.asStateFlow()

    private val _seekStep = MutableStateFlow(prefs.getInt("seekStep", 10))
    val seekStep: StateFlow<Int> = _seekStep.asStateFlow()

    // Signed in to Google Drive from this app (the token itself is Play services').
    private val _driveConnected = MutableStateFlow(prefs.getBoolean("driveConnected", false))
    val driveConnected: StateFlow<Boolean> = _driveConnected.asStateFlow()

    private val _folders = MutableStateFlow(readFolders())
    val folders: StateFlow<List<LibraryFolder>> = _folders.asStateFlow()

    fun setAppearance(v: String) { _appearance.value = v; prefs.edit().putString("appearance", v).apply() }
    fun setSpeed(v: Float) { _speed.value = v; prefs.edit().putFloat("speed", v).apply() }
    fun setAutoNext(v: Boolean) { _autoNext.value = v; prefs.edit().putBoolean("autoNext", v).apply() }
    fun setResume(v: Boolean) { _resume.value = v; prefs.edit().putBoolean("resume", v).apply() }
    fun setSeekStep(v: Int) { _seekStep.value = v; prefs.edit().putInt("seekStep", v).apply() }

    fun setDriveConnected(v: Boolean) { _driveConnected.value = v; prefs.edit().putBoolean("driveConnected", v).apply() }

    fun addFolder(folder: LibraryFolder) {
        if (_folders.value.any { it.treeUri == folder.treeUri }) return
        writeFolders(_folders.value + folder)
    }

    fun removeFolder(treeUri: String) = writeFolders(_folders.value.filterNot { it.treeUri == treeUri })

    private fun readFolders(): List<LibraryFolder> {
        val arr = runCatching { JSONArray(prefs.getString("folders", "[]")) }.getOrElse { JSONArray() }
        return (0 until arr.length()).mapNotNull { i ->
            arr.optJSONObject(i)?.let { LibraryFolder(it.optString("uri"), it.optString("name")) }
        }.filter { it.treeUri.isNotEmpty() }
    }

    private fun writeFolders(list: List<LibraryFolder>) {
        _folders.value = list
        val arr = JSONArray()
        list.forEach { arr.put(JSONObject().put("uri", it.treeUri).put("name", it.name)) }
        prefs.edit().putString("folders", arr.toString()).apply()
    }
}
