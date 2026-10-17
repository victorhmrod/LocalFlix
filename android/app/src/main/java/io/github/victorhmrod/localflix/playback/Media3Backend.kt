package io.github.victorhmrod.localflix.playback

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.Tracks
import androidx.media3.common.TrackSelectionOverride
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.extractor.metadata.Chapter
import io.github.victorhmrod.localflix.core.BackendListener
import io.github.victorhmrod.localflix.core.ChapterInfo
import io.github.victorhmrod.localflix.core.EndFileInfo
import io.github.victorhmrod.localflix.core.EndReason
import io.github.victorhmrod.localflix.core.PlaybackBackend
import io.github.victorhmrod.localflix.core.TrackInfo
import io.github.victorhmrod.localflix.core.TrackType
import io.github.victorhmrod.localflix.core.normalizeChapters

/**
 * The real engine: Media3/ExoPlayer decoding straight from the content URI the user granted. Every playback gets a
 * token; only the token of the current load is ever reported, so events of a replaced file cannot leak out.
 * Main thread only.
 */
class Media3Backend(context: Context) : PlaybackBackend {
    val player: ExoPlayer = ExoPlayer.Builder(
        context.applicationContext,
        DefaultRenderersFactory(context.applicationContext).setEnableDecoderFallback(true),
    )
        .setAudioAttributes(AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), true)
        .setHandleAudioBecomingNoisy(true)
        .build()

    override var listener: BackendListener? = null

    private val main = Handler(Looper.getMainLooper())
    private var nextToken = 0L
    private var activeToken = 0L // 0 = nothing we report events for
    private var loaded = false // fileLoaded already reported for activeToken
    private var currentUri: String = ""
    private var lastDuration = 0L
    private var chapterList: List<ChapterInfo> = emptyList()

    private val ticker = object : Runnable {
        override fun run() {
            if (activeToken != 0L && loaded) listener?.onPositionChanged(player.currentPosition.coerceAtLeast(0))
            main.postDelayed(this, POSITION_INTERVAL_MS)
        }
    }

    private val playerListener = object : Player.Listener {
        override fun onPlaybackStateChanged(playbackState: Int) {
            if (activeToken == 0L) return
            when (playbackState) {
                Player.STATE_READY -> {
                    reportDuration()
                    if (!loaded) {
                        loaded = true
                        refreshChapters(player.currentTracks) // known before the listener hears the file is open
                        listener?.onFileLoaded(activeToken)
                        listener?.onTracksChanged(buildTracks(player.currentTracks))
                    }
                }
                Player.STATE_ENDED -> if (loaded) finish(EndReason.EOF)
                else -> {}
            }
        }

        override fun onTimelineChanged(timeline: androidx.media3.common.Timeline, reason: Int) {
            if (activeToken != 0L && loaded) reportDuration()
        }

        override fun onPlayWhenReadyChanged(playWhenReady: Boolean, reason: Int) {
            if (activeToken != 0L && loaded) listener?.onPausedChanged(!playWhenReady)
        }

        override fun onTracksChanged(tracks: Tracks) {
            if (activeToken == 0L || !loaded) return
            listener?.onTracksChanged(buildTracks(tracks))
            if (refreshChapters(tracks)) listener?.onChaptersChanged(chapterList)
        }

        override fun onPlayerError(error: PlaybackException) {
            if (activeToken == 0L) return
            finish(EndReason.ERROR, "${error.errorCodeName}: ${error.message ?: "playback failed"}")
        }
    }

    init {
        player.addListener(playerListener)
        main.post(ticker)
    }

    private fun finish(reason: EndReason, text: String = "") {
        val t = activeToken
        activeToken = 0
        loaded = false
        chapterList = emptyList()
        listener?.onEndFile(EndFileInfo(t, reason, text))
    }

    private fun reportDuration() {
        val d = player.duration
        if (d != C.TIME_UNSET && d > 0 && d != lastDuration) {
            lastDuration = d
            listener?.onDurationChanged(d)
        }
    }

    override fun loadFile(uri: String, startMs: Long): Long {
        activeToken = ++nextToken
        loaded = false
        lastDuration = 0
        chapterList = emptyList()
        currentUri = uri
        player.setMediaItem(MediaItem.fromUri(Uri.parse(uri)), startMs.coerceAtLeast(0))
        player.prepare()
        player.playWhenReady = true
        return activeToken
    }

    override fun stop() {
        activeToken = 0 // whatever the engine still reports for the old load is dropped
        loaded = false
        chapterList = emptyList()
        player.stop()
        player.clearMediaItems()
    }

    override fun setPaused(paused: Boolean) {
        player.playWhenReady = !paused
    }

    override fun seekToMs(positionMs: Long) = player.seekTo(positionMs.coerceAtLeast(0))

    override fun seekByMs(deltaMs: Long) {
        val d = player.duration
        var target = player.currentPosition + deltaMs
        target = if (d != C.TIME_UNSET && d > 0) target.coerceIn(0, d) else target.coerceAtLeast(0)
        player.seekTo(target)
    }

    override fun setVolume(percent: Int) {
        player.volume = (percent.coerceIn(0, 100) / 100f)
    }

    override fun setMuted(muted: Boolean) {
        player.volume = if (muted) 0f else 1f
    }

    override fun setSpeed(speed: Double) = player.setPlaybackSpeed(speed.toFloat())

    // ------------------------------------------------------------------ tracks

    /** Track ids are 1-based and stable for a file: group index * 100 + track index + 1. */
    private fun idOf(group: Int, track: Int) = group * 100 + track + 1

    private fun buildTracks(tracks: Tracks): List<TrackInfo> {
        val out = ArrayList<TrackInfo>()
        tracks.groups.forEachIndexed { gi, g ->
            val type = when (g.type) {
                C.TRACK_TYPE_AUDIO -> TrackType.AUDIO
                C.TRACK_TYPE_TEXT -> TrackType.SUBTITLE
                C.TRACK_TYPE_VIDEO -> TrackType.VIDEO
                else -> return@forEachIndexed
            }
            for (ti in 0 until g.length) {
                val f = g.getTrackFormat(ti)
                out += TrackInfo(
                    id = idOf(gi, ti),
                    type = type,
                    language = f.language.orEmpty(),
                    title = f.label.orEmpty(),
                    codec = f.sampleMimeType?.substringAfter('/')?.uppercase().orEmpty(),
                    selected = g.isTrackSelected(ti),
                    external = type == TrackType.SUBTITLE && f.label == EXTERNAL_LABEL,
                    isDefault = (f.selectionFlags and C.SELECTION_FLAG_DEFAULT) != 0,
                )
            }
        }
        return out
    }

    private fun select(type: Int, trackId: Int) {
        val params = player.trackSelectionParameters.buildUpon()
        if (trackId <= 0) {
            params.setTrackTypeDisabled(type, true)
        } else {
            val group = (trackId - 1) / 100
            val track = (trackId - 1) % 100
            val g = player.currentTracks.groups.getOrNull(group)
            if (g == null || g.type != type || track >= g.length) return
            params.setTrackTypeDisabled(type, false)
            params.setOverrideForType(TrackSelectionOverride(g.mediaTrackGroup, track))
        }
        player.trackSelectionParameters = params.build()
    }

    override fun setAudioTrack(trackId: Int) = select(C.TRACK_TYPE_AUDIO, trackId)

    override fun setSubtitleTrack(trackId: Int) = select(C.TRACK_TYPE_TEXT, trackId)

    override fun addExternalSubtitle(uri: String, displayName: String): Boolean {
        if (activeToken == 0L || !loaded || currentUri.isEmpty()) return false
        val mime = when (displayName.substringAfterLast('.', "").lowercase()) {
            "srt" -> MimeTypes.APPLICATION_SUBRIP
            "vtt" -> MimeTypes.TEXT_VTT
            "ass", "ssa" -> MimeTypes.TEXT_SSA
            "ttml", "xml", "dfxp" -> MimeTypes.APPLICATION_TTML
            else -> return false
        }
        val sub = MediaItem.SubtitleConfiguration.Builder(Uri.parse(uri))
            .setMimeType(mime)
            .setLabel(EXTERNAL_LABEL)
            .setSelectionFlags(C.SELECTION_FLAG_DEFAULT)
            .build()
        val position = player.currentPosition
        val wasPlaying = player.playWhenReady
        // Same playback (same token): the engine just re-opens the item with the extra subtitle at the same spot.
        player.setMediaItem(MediaItem.Builder().setUri(Uri.parse(currentUri)).setSubtitleConfigurations(listOf(sub)).build(), position)
        player.trackSelectionParameters = player.trackSelectionParameters.buildUpon().setTrackTypeDisabled(C.TRACK_TYPE_TEXT, false).build()
        player.prepare()
        player.playWhenReady = wasPlaying
        return true
    }

    override fun setPreferredLanguages(audio: String, subtitle: String) {
        player.trackSelectionParameters = player.trackSelectionParameters.buildUpon()
            .setPreferredAudioLanguage(audio.ifEmpty { null })
            .setPreferredTextLanguage(subtitle.ifEmpty { null })
            .build()
    }

    // ------------------------------------------------------------------ state

    override val positionMs: Long get() = player.currentPosition.coerceAtLeast(0)
    override val durationMs: Long get() = player.duration.let { if (it == C.TIME_UNSET) 0 else it }
    override val isPaused: Boolean get() = !player.playWhenReady
    override val volume: Int get() = (player.volume * 100).toInt()
    override val isMuted: Boolean get() = player.volume == 0f
    override val speed: Double get() = player.playbackParameters.speed.toDouble()
    override val tracks: List<TrackInfo> get() = buildTracks(player.currentTracks)
    override val chapters: List<ChapterInfo> get() = chapterList

    /**
     * Chapters are carried by the container's tracks: Media3 hangs a Chapter entry (Matroska/WebM chapters, MP4
     * chpl) on the format of every track, so the same chapter shows up once per track. A track list without any
     * chapters never erases the known ones: re-opening the same item (adding a subtitle) briefly has none.
     * Returns whether the known chapters changed.
     */
    @OptIn(UnstableApi::class)
    private fun refreshChapters(tracks: Tracks): Boolean {
        val found = ArrayList<ChapterInfo>()
        for (group in tracks.groups) {
            for (ti in 0 until group.length) {
                val metadata = group.getTrackFormat(ti).metadata ?: continue
                for (c in metadata.getEntriesOfType(Chapter::class.java)) found += ChapterInfo(c.title?.value.orEmpty(), c.startTimeMs)
            }
        }
        if (found.isEmpty()) return false
        val normalized = normalizeChapters(found)
        if (normalized == chapterList) return false
        chapterList = normalized
        return true
    }

    fun release() {
        main.removeCallbacks(ticker)
        player.removeListener(playerListener)
        player.release()
    }

    private companion object {
        const val POSITION_INTERVAL_MS = 500L
        const val EXTERNAL_LABEL = "External"
    }
}
