package io.github.victorhmrod.localflix.core

/** Key/value persistence behind [AppSettings] (the `settings` table in the app database). */
interface SettingsStore {
    fun allSettings(): Map<String, String>

    fun setSetting(key: String, value: String)
}

object SettingKey {
    const val AUTO_PLAY_NEXT = "playback.autoPlayNext"
    const val AUTO_PLAY_COUNTDOWN = "playback.autoPlayCountdownSeconds"
    const val RESUME_AUTOMATICALLY = "playback.resumeAutomatically"
    const val SKIP_COMPLETED = "playback.skipCompleted"
    const val PREFERRED_AUDIO_LANGUAGE = "playback.preferredAudioLanguage"
    const val PREFERRED_SUBTITLE_LANGUAGE = "playback.preferredSubtitleLanguage"
    const val DEFAULT_VOLUME = "playback.defaultVolume"
    const val PLAYBACK_SPEED = "playback.speed"
    const val PLAYER_ORIENTATION = "ui.playerOrientation"
}

/** How the player screen treats the device orientation. */
enum class PlayerOrientation(val stored: Int) {
    LANDSCAPE(0), // lock to landscape while playing
    FOLLOW_DEVICE(1), // rotate with the sensor
    ;

    companion object {
        fun fromStored(v: Int) = entries.firstOrNull { it.stored == v } ?: LANDSCAPE
    }
}

/** Typed access to the key/value settings with defaults. Values are cached; writes go straight to the store. */
class AppSettings(private val store: SettingsStore) : PlaybackSettings {
    private val values = HashMap<String, String>()
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<(String) -> Unit>()

    init {
        reload()
    }

    fun reload() {
        values.clear()
        values.putAll(store.allSettings())
    }

    fun addChangeListener(l: (String) -> Unit) {
        listeners += l
    }

    fun removeChangeListener(l: (String) -> Unit) {
        listeners -= l
    }

    private fun defaultFor(key: String): String = when (key) {
        SettingKey.AUTO_PLAY_NEXT -> "1"
        SettingKey.AUTO_PLAY_COUNTDOWN -> "3"
        SettingKey.RESUME_AUTOMATICALLY -> "1"
        SettingKey.SKIP_COMPLETED -> "0"
        SettingKey.DEFAULT_VOLUME -> "100"
        SettingKey.PLAYBACK_SPEED -> "1"
        SettingKey.PLAYER_ORIENTATION -> "0"
        else -> ""
    }

    private fun get(key: String): String = values[key] ?: defaultFor(key)

    private fun set(key: String, value: String) {
        if (get(key) == value) return // unchanged (or still at its default)
        store.setSetting(key, value)
        values[key] = value
        listeners.forEach { it(key) }
    }

    private fun flag(key: String) = get(key).toIntOrNull()?.let { it != 0 } ?: (defaultFor(key) != "0")

    override val autoPlayNext get() = flag(SettingKey.AUTO_PLAY_NEXT)
    override val autoPlayCountdownSeconds get() = (get(SettingKey.AUTO_PLAY_COUNTDOWN).toIntOrNull() ?: 3).coerceIn(0, 30)
    override val resumeAutomatically get() = flag(SettingKey.RESUME_AUTOMATICALLY)
    override val skipCompleted get() = flag(SettingKey.SKIP_COMPLETED)
    val preferredAudioLanguage get() = get(SettingKey.PREFERRED_AUDIO_LANGUAGE)
    val preferredSubtitleLanguage get() = get(SettingKey.PREFERRED_SUBTITLE_LANGUAGE)
    val defaultVolume get() = (get(SettingKey.DEFAULT_VOLUME).toIntOrNull() ?: 100).coerceIn(0, 130)
    override val playbackSpeed get() = (get(SettingKey.PLAYBACK_SPEED).toDoubleOrNull() ?: 1.0).coerceIn(0.25, 4.0)
    val playerOrientation get() = PlayerOrientation.fromStored(get(SettingKey.PLAYER_ORIENTATION).toIntOrNull() ?: 0)

    fun setAutoPlayNext(v: Boolean) = set(SettingKey.AUTO_PLAY_NEXT, if (v) "1" else "0")

    fun setAutoPlayCountdownSeconds(v: Int) = set(SettingKey.AUTO_PLAY_COUNTDOWN, v.coerceIn(0, 30).toString())

    fun setResumeAutomatically(v: Boolean) = set(SettingKey.RESUME_AUTOMATICALLY, if (v) "1" else "0")

    fun setSkipCompleted(v: Boolean) = set(SettingKey.SKIP_COMPLETED, if (v) "1" else "0")

    fun setPreferredAudioLanguage(v: String) = set(SettingKey.PREFERRED_AUDIO_LANGUAGE, v.trim())

    fun setPreferredSubtitleLanguage(v: String) = set(SettingKey.PREFERRED_SUBTITLE_LANGUAGE, v.trim())

    fun setDefaultVolume(v: Int) = set(SettingKey.DEFAULT_VOLUME, v.coerceIn(0, 130).toString())

    fun setPlaybackSpeed(v: Double) = set(SettingKey.PLAYBACK_SPEED, v.coerceIn(0.25, 4.0).toString())

    fun setPlayerOrientation(v: PlayerOrientation) = set(SettingKey.PLAYER_ORIENTATION, v.stored.toString())
}
