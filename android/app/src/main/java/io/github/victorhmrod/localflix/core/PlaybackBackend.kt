package io.github.victorhmrod.localflix.core

/**
 * Why the engine stopped playing a file. Only [EOF] means "the media played to its end"; everything else must
 * never trigger automatic advancement.
 */
enum class EndReason {
    EOF, // natural end of the file
    STOP, // stopped by a command (manual stop, or replaced by another load)
    QUIT, // the player is shutting down
    ERROR, // the file could not be loaded or playback failed
    REDIRECT, // not used by LocalFlix
}

data class EndFileInfo(
    val token: Long, // the value returned by loadFile() for this playback
    val reason: EndReason,
    val errorText: String = "",
)

enum class TrackType { VIDEO, AUDIO, SUBTITLE }

data class TrackInfo(
    val id: Int, // > 0; stable for a given file
    val type: TrackType,
    val language: String = "", // ISO code as reported by the container, may be empty
    val title: String = "",
    val codec: String = "",
    val selected: Boolean = false,
    val external: Boolean = false,
    val isDefault: Boolean = false,
) {
    fun label(languageName: (String) -> String = { it }): String {
        val lang = if (language.isNotEmpty() && language != "und") languageName(language) else ""
        val parts = mutableListOf<String>()
        if (lang.isNotEmpty()) parts += lang
        if (title.isNotEmpty() && !title.equals(lang, ignoreCase = true)) parts += title
        if (parts.isEmpty()) parts += "Track $id"
        var out = parts.joinToString(" — ")
        if (codec.isNotEmpty()) out += " ($codec)"
        if (external) out += " [external]"
        return out
    }
}

/** Events a backend reports. Always delivered on the main thread. */
interface BackendListener {
    fun onFileLoaded(token: Long) {}

    fun onDurationChanged(durationMs: Long) {}

    fun onPositionChanged(positionMs: Long) {}

    fun onPausedChanged(paused: Boolean) {}

    fun onTracksChanged(tracks: List<TrackInfo>) {}

    /** The file's chapters became known or changed (normally they are already there when [onFileLoaded] fires). */
    fun onChaptersChanged(chapters: List<ChapterInfo>) {}

    fun onEndFile(info: EndFileInfo) {}
}

/**
 * Abstraction over the media engine. The real implementation wraps Media3/ExoPlayer; tests inject a scripted
 * fake. All methods and callbacks are used on the main thread only.
 */
interface PlaybackBackend {
    var listener: BackendListener?

    /**
     * Starts [uri] at [startMs]. Returns a token that identifies this playback in [BackendListener.onFileLoaded] and
     * [BackendListener.onEndFile], so events of a replaced file can be told apart from the current one.
     */
    fun loadFile(uri: String, startMs: Long): Long

    fun stop()

    fun setPaused(paused: Boolean)

    fun seekToMs(positionMs: Long)

    fun seekByMs(deltaMs: Long)

    fun setVolume(percent: Int)

    fun setMuted(muted: Boolean)

    fun setSpeed(speed: Double)

    fun setAudioTrack(trackId: Int) // 0 = off

    fun setSubtitleTrack(trackId: Int) // 0 = off

    fun addExternalSubtitle(uri: String, displayName: String): Boolean

    fun setPreferredLanguages(audio: String, subtitle: String)

    val positionMs: Long
    val durationMs: Long
    val isPaused: Boolean
    val volume: Int
    val isMuted: Boolean
    val speed: Double
    val tracks: List<TrackInfo>

    /** Chapters of the current file in playback order (empty when it has none, or nothing is loaded). */
    val chapters: List<ChapterInfo>
}
