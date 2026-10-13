package io.github.victorhmrod.localflix.core

import java.util.concurrent.CopyOnWriteArrayList

enum class StartMode {
    AUTO, // follow the "resume playback automatically" setting
    RESUME, // continue from the saved position
    FROM_START, // always begin at 0:00
}

enum class PlaybackState {
    IDLE, // nothing loaded
    LOADING, // load requested, waiting for the backend
    PLAYING,
    PAUSED,
    ADVANCE_PENDING, // an item finished; the next one starts after the countdown
}

enum class FailureKind {
    MISSING_FILE, // the file cannot be opened (moved, deleted, access revoked, drive removed)
    LOAD_ERROR, // the file exists but cannot be opened/decoded
    NOT_IN_QUEUE, // the requested item is not part of the current queue
}

/** [detail] is the engine's error text, or the file's label for a missing file. */
data class PlaybackFailure(val kind: FailureKind, val detail: String = "")

sealed interface PlaybackEvent {
    data class StateChanged(val state: PlaybackState) : PlaybackEvent

    /** The backend has the file open and playback starts. */
    data class NowPlaying(val entry: QueueEntry, val hasPrevious: Boolean, val hasNext: Boolean) : PlaybackEvent

    data class PositionChanged(val positionMs: Long, val durationMs: Long) : PlaybackEvent

    data class TracksChanged(val tracks: List<TrackInfo>) : PlaybackEvent

    /** The item played to its natural end and was marked completed. */
    data class ItemCompleted(val entry: QueueEntry) : PlaybackEvent

    /** A countdown to the next item started (or its target changed). */
    data class AutoAdvancePending(val next: QueueEntry, val remainingMs: Long) : PlaybackEvent

    data object AutoAdvanceCancelled : PlaybackEvent

    /** The queue was edited (reorder, rename, removal): refreshed current entry and whether Previous/Next exist. */
    data class QueueUpdated(val current: QueueEntry, val hasPrevious: Boolean, val hasNext: Boolean) : PlaybackEvent

    /** The last item of the queue finished (nothing left to play). */
    data class QueueFinished(val last: QueueEntry) : PlaybackEvent

    /** Playback ended normally but auto-play is off; [next] may be invalid. */
    data class EndedWithoutAdvance(val finished: QueueEntry, val next: QueueEntry) : PlaybackEvent

    data class Stopped(val entry: QueueEntry) : PlaybackEvent

    data class Failed(val entry: QueueEntry, val failure: PlaybackFailure) : PlaybackEvent

    /** "Resume playback automatically" is off and the item has a saved position: the UI must ask. */
    data class ResumeDecisionNeeded(val entry: QueueEntry, val positionMs: Long) : PlaybackEvent
}

fun interface PlaybackListener {
    fun onEvent(event: PlaybackEvent)
}

/**
 * Orchestrates one playback session: loads queue entries into the backend, tracks progress, and decides what
 * happens when playback ends. The central rule: only [EndReason.EOF] on the *current* load completes an item and
 * may start the next one; manual stops, replaced loads, errors and stale events never advance.
 * Port of PlaybackController (desktop), used from the main thread only.
 */
class PlaybackController(
    private val backend: PlaybackBackend,
    private val queue: EpisodeQueue,
    private val progress: ProgressTracker,
    private val store: PlaybackStore,
    private val settings: PlaybackSettings,
    private val scheduler: Scheduler,
    private val files: FileProbe,
) : BackendListener, EpisodeQueue.Listener {
    private val listeners = CopyOnWriteArrayList<PlaybackListener>()

    var state: PlaybackState = PlaybackState.IDLE
        private set
    var currentEntry: QueueEntry = QueueEntry()
        private set
    var positionMs = 0L
        private set
    var durationMs = 0L
        private set

    private var token = 0L // token of the load we currently care about (0 = none)
    private var trackingPosition = true
    private var advanceTimer: Cancelable? = null
    private var advanceStartedAt = 0L
    private var advanceDelayMs = 0L
    private var pendingNext = QueueEntry()
    private var shutDown = false

    init {
        backend.listener = this
        queue.addListener(this)
    }

    fun addListener(l: PlaybackListener) {
        listeners += l
    }

    fun removeListener(l: PlaybackListener) {
        listeners -= l
    }

    private fun emit(e: PlaybackEvent) = listeners.forEach { it.onEvent(e) }

    private fun setState(s: PlaybackState) {
        if (state == s) return
        state = s
        emit(PlaybackEvent.StateChanged(s))
    }

    val isActive: Boolean get() = state == PlaybackState.PLAYING || state == PlaybackState.PAUSED || state == PlaybackState.LOADING
    val hasNext: Boolean get() = queue.next(false).valid()
    val hasPrevious: Boolean get() = queue.previous().valid()

    private fun nextForAdvance(): QueueEntry = queue.next(settings.skipCompleted)

    // ------------------------------------------------------------------ starting

    /** Starts [mediaFileId] in the given queue context (show, movie, collection). */
    fun play(context: QueueContext, mediaFileId: Id, mode: StartMode = StartMode.AUTO): Boolean {
        if (shutDown) return false
        if (queue.context != context) queue.setContext(context)
        // A fresh viewing session starts at the configured default speed.
        backend.setSpeed(settings.playbackSpeed)
        return playEntry(mediaFileId, mode)
    }

    /** Starts an entry of the current queue. */
    fun playEntry(mediaFileId: Id, mode: StartMode = StartMode.RESUME): Boolean {
        if (shutDown) return false

        val entry = queue.entryForMedia(mediaFileId)
        if (!entry.valid()) {
            emit(PlaybackEvent.Failed(QueueEntry(), PlaybackFailure(FailureKind.NOT_IN_QUEUE)))
            return false
        }

        // "Resume automatically" off: let the UI ask before touching anything.
        if (mode == StartMode.AUTO && !entry.completed && entry.positionMs > 0 && !settings.resumeAutomatically) {
            emit(PlaybackEvent.ResumeDecisionNeeded(entry, entry.positionMs))
            return false
        }

        // Leave whatever is active: stop the countdown, save the old position and invalidate its token so its
        // trailing end-of-file (reason Stop) is ignored.
        if (state == PlaybackState.ADVANCE_PENDING) {
            cancelAdvanceTimer()
            emit(PlaybackEvent.AutoAdvanceCancelled)
        }
        finishActiveItem(FlushReason.SWITCH)

        var startMs = 0L
        if (!entry.completed && mode != StartMode.FROM_START) startMs = entry.positionMs

        if (!files.exists(entry.uri)) {
            backend.stop()
            currentEntry = entry
            queue.setCurrentMedia(entry.mediaFileId)
            store.setMissing(entry.mediaFileId, true)
            setState(PlaybackState.IDLE)
            emit(PlaybackEvent.Failed(entry, PlaybackFailure(FailureKind.MISSING_FILE, entry.fileLabel)))
            return false
        }

        beginLoad(entry, startMs)
        return true
    }

    private fun beginLoad(entry: QueueEntry, startMs: Long) {
        currentEntry = entry
        positionMs = startMs
        durationMs = entry.durationMs
        // Replaying a completed item must not overwrite its completed state.
        trackingPosition = !entry.completed
        queue.setCurrentMedia(entry.mediaFileId)
        setState(PlaybackState.LOADING)
        token = backend.loadFile(entry.uri, startMs)
    }

    private fun finishActiveItem(reason: FlushReason) {
        progress.end(reason)
        token = 0
    }

    // ------------------------------------------------------------------ backend events

    override fun onFileLoaded(token: Long) {
        if (token == 0L || token != this.token || state != PlaybackState.LOADING) return

        val dur = backend.durationMs
        if (dur > 0) durationMs = dur
        setState(if (backend.isPaused) PlaybackState.PAUSED else PlaybackState.PLAYING)
        progress.begin(currentEntry.mediaFileId, trackingPosition, durationMs)

        // Restore the audio/subtitle choice made for this file earlier.
        val saved = store.progress(currentEntry.mediaFileId)
        if (saved.audioTrack != TRACK_AUTO) backend.setAudioTrack(saved.audioTrack)
        if (saved.subtitleTrack != TRACK_AUTO) backend.setSubtitleTrack(saved.subtitleTrack)

        emit(PlaybackEvent.NowPlaying(currentEntry, hasPrevious, hasNext))
        emit(PlaybackEvent.PositionChanged(positionMs, durationMs))
    }

    override fun onEndFile(info: EndFileInfo) {
        // Anything that is not the load we are tracking is a leftover of a file we already replaced or stopped;
        // it must never complete or advance anything.
        if (info.token == 0L || info.token != token) return
        token = 0
        val neverStarted = state == PlaybackState.LOADING

        when (info.reason) {
            EndReason.EOF -> {
                if (neverStarted) {
                    // EOF without a loaded file is a broken file, not a finished one.
                    progress.end(FlushReason.STOP)
                    setState(PlaybackState.IDLE)
                    emit(PlaybackEvent.Failed(currentEntry, PlaybackFailure(FailureKind.LOAD_ERROR, "ended before playback could start")))
                    return
                }
                handleNaturalEnd()
            }
            EndReason.ERROR -> {
                progress.end(FlushReason.STOP)
                setState(PlaybackState.IDLE)
                val exists = files.exists(currentEntry.uri)
                if (!exists) store.setMissing(currentEntry.mediaFileId, true)
                val detail = info.errorText.ifEmpty { "unknown error" }
                emit(
                    PlaybackEvent.Failed(
                        currentEntry,
                        if (exists) PlaybackFailure(FailureKind.LOAD_ERROR, detail) else PlaybackFailure(FailureKind.MISSING_FILE, currentEntry.fileLabel),
                    ),
                )
            }
            EndReason.STOP, EndReason.QUIT, EndReason.REDIRECT -> {
                progress.end(FlushReason.STOP)
                setState(PlaybackState.IDLE)
                emit(PlaybackEvent.Stopped(currentEntry))
            }
        }
    }

    private fun handleNaturalEnd() {
        val finished = currentEntry
        progress.complete(durationMs)
        positionMs = 0
        emit(PlaybackEvent.ItemCompleted(finished))

        val next = nextForAdvance()
        if (!next.valid()) {
            setState(PlaybackState.IDLE)
            emit(PlaybackEvent.QueueFinished(finished))
            return
        }
        if (!settings.autoPlayNext) {
            setState(PlaybackState.IDLE)
            emit(PlaybackEvent.EndedWithoutAdvance(finished, next))
            return
        }
        startPendingAdvance()
    }

    private fun startPendingAdvance() {
        pendingNext = nextForAdvance()
        advanceDelayMs = settings.autoPlayCountdownSeconds * 1000L
        setState(PlaybackState.ADVANCE_PENDING)
        advanceStartedAt = scheduler.uptimeMs()
        advanceTimer = scheduler.postDelayed(advanceDelayMs) { onAdvanceTimeout() }
        emit(PlaybackEvent.AutoAdvancePending(pendingNext, advanceDelayMs))
    }

    private fun cancelAdvanceTimer() {
        advanceTimer?.cancel()
        advanceTimer = null
    }

    private fun onAdvanceTimeout() {
        advanceTimer = null
        if (state != PlaybackState.ADVANCE_PENDING) return
        // Recompute: the user may have reordered or removed episodes meanwhile.
        val next = nextForAdvance()
        if (!next.valid()) {
            setState(PlaybackState.IDLE)
            emit(PlaybackEvent.QueueFinished(currentEntry))
            return
        }
        playEntry(next.mediaFileId, StartMode.RESUME)
    }

    override fun onPositionChanged(positionMs: Long) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        this.positionMs = positionMs
        progress.update(positionMs, durationMs)
        emit(PlaybackEvent.PositionChanged(positionMs, durationMs))
    }

    override fun onDurationChanged(durationMs: Long) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        if (durationMs <= 0) return
        this.durationMs = durationMs
        progress.update(positionMs, durationMs)
        emit(PlaybackEvent.PositionChanged(positionMs, durationMs))
    }

    override fun onPausedChanged(paused: Boolean) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        setState(if (paused) PlaybackState.PAUSED else PlaybackState.PLAYING)
        if (paused) progress.flush(FlushReason.PAUSE)
    }

    override fun onTracksChanged(tracks: List<TrackInfo>) = emit(PlaybackEvent.TracksChanged(tracks))

    // ------------------------------------------------------------------ queue changes

    override fun onQueueChanged() {
        if (state == PlaybackState.ADVANCE_PENDING) {
            val n = nextForAdvance()
            if (!n.valid()) {
                cancelAdvanceTimer()
                setState(PlaybackState.IDLE)
                emit(PlaybackEvent.QueueFinished(currentEntry))
                return
            }
            if (n.mediaFileId != pendingNext.mediaFileId) {
                pendingNext = n
                val remaining = maxOf(0L, advanceDelayMs - (scheduler.uptimeMs() - advanceStartedAt))
                emit(PlaybackEvent.AutoAdvancePending(n, remaining))
            }
            return
        }
        if (state == PlaybackState.IDLE && !currentEntry.valid()) return
        val refreshed = queue.current()
        if (refreshed.valid()) currentEntry = refreshed
        emit(PlaybackEvent.QueueUpdated(currentEntry, hasPrevious, hasNext))
    }

    // ------------------------------------------------------------------ user commands

    fun stop() {
        if (state == PlaybackState.IDLE) return
        if (state == PlaybackState.ADVANCE_PENDING) {
            cancelAdvanceTimer()
            setState(PlaybackState.IDLE)
            emit(PlaybackEvent.AutoAdvanceCancelled)
            return
        }
        // Save first, then invalidate the token, so the Stop end-of-file the backend is about to send can
        // neither complete nor advance anything.
        finishActiveItem(FlushReason.STOP)
        backend.stop()
        setState(PlaybackState.IDLE)
        emit(PlaybackEvent.Stopped(currentEntry))
    }

    fun togglePause() {
        if (state == PlaybackState.PLAYING) {
            setPaused(true)
        } else if (state == PlaybackState.PAUSED) {
            setPaused(false)
        }
    }

    fun setPaused(paused: Boolean) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        backend.setPaused(paused)
    }

    fun next() {
        val n = queue.next(false)
        if (n.valid()) playEntry(n.mediaFileId, StartMode.RESUME)
    }

    fun previous() {
        val p = queue.previous()
        if (p.valid()) playEntry(p.mediaFileId, StartMode.RESUME)
    }

    /** Skips the countdown and starts the pending next item now. */
    fun advanceNow() {
        if (state != PlaybackState.ADVANCE_PENDING) return
        cancelAdvanceTimer()
        onAdvanceTimeout()
    }

    /** Cancels the pending automatic advance; playback stays stopped. */
    fun cancelAutoAdvance() {
        if (state != PlaybackState.ADVANCE_PENDING) return
        cancelAdvanceTimer()
        setState(PlaybackState.IDLE)
        emit(PlaybackEvent.AutoAdvanceCancelled)
    }

    fun seekToMs(positionMs: Long) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        var target = maxOf(0L, positionMs)
        if (durationMs > 0) target = minOf(target, durationMs)
        this.positionMs = target
        progress.update(target, durationMs) // so an immediate quit saves the new spot
        backend.seekToMs(target)
        emit(PlaybackEvent.PositionChanged(target, durationMs))
    }

    fun seekByMs(deltaMs: Long) {
        if (state != PlaybackState.PLAYING && state != PlaybackState.PAUSED) return
        var target = maxOf(0L, positionMs + deltaMs)
        if (durationMs > 0) target = minOf(target, durationMs)
        this.positionMs = target
        progress.update(target, durationMs)
        backend.seekByMs(deltaMs)
        emit(PlaybackEvent.PositionChanged(target, durationMs))
    }

    /** Flushes progress and stops the backend; safe to call more than once. */
    fun shutdown() {
        if (shutDown) return
        shutDown = true
        cancelAdvanceTimer()
        val wasActive = isActive
        finishActiveItem(FlushReason.SHUTDOWN)
        if (wasActive) backend.stop()
        state = PlaybackState.IDLE
    }

    /** The app went to the background: keep the position safe without ending the session. */
    fun flushProgress(reason: FlushReason = FlushReason.PAUSE) = progress.flush(reason)

    // ------------------------------------------------------------------ audio / video options

    fun setVolume(percent: Int) = backend.setVolume(percent.coerceIn(0, 130))

    fun setMuted(muted: Boolean) = backend.setMuted(muted)

    fun setSpeed(speed: Double) = backend.setSpeed(speed.coerceIn(0.25, 4.0))

    private fun selectedTrackId(tracks: List<TrackInfo>, type: TrackType): Int =
        tracks.firstOrNull { it.type == type && it.selected }?.id ?: TRACK_OFF

    fun selectAudioTrack(trackId: Int) {
        backend.setAudioTrack(trackId)
        if (currentEntry.valid() && isActive) {
            store.saveTrackSelection(currentEntry.mediaFileId, trackId, selectedTrackId(backend.tracks, TrackType.SUBTITLE))
        }
    }

    fun selectSubtitleTrack(trackId: Int) {
        backend.setSubtitleTrack(trackId)
        if (currentEntry.valid() && isActive) {
            store.saveTrackSelection(currentEntry.mediaFileId, selectedTrackId(backend.tracks, TrackType.AUDIO), trackId)
        }
    }

    fun addExternalSubtitle(uri: String, displayName: String): Boolean = backend.addExternalSubtitle(uri, displayName)
}
