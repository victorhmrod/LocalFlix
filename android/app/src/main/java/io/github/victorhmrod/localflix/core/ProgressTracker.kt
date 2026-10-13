package io.github.victorhmrod.localflix.core

import kotlin.math.abs

/**
 * Decides when the playing position is written to the database: every ~5 s while playing, immediately on pause,
 * when switching items, on stop and on shutdown (including when the app leaves the screen). Port of
 * PlaybackProgressManager, including its throttling rules.
 */
class ProgressTracker(
    private val store: PlaybackStore,
    private val scheduler: Scheduler,
    private val intervalMs: Long = 5000,
) {
    private var media: Id = INVALID_ID
    private var persist = true // false while replaying a completed item: its position must not be overwritten
    private var position = 0L
    private var duration = 0L
    private var lastSaved = -1L
    private var hadHistory = false
    private var durationRecorded = false
    private var knownFileDuration = 0L
    private var timer: Cancelable? = null

    /** Number of database writes done so far (used by tests to check throttling). */
    var writeCount = 0
        private set

    val activeMedia: Id get() = media

    fun begin(mediaFileId: Id, persistPosition: Boolean, durationMs: Long) {
        if (media != INVALID_ID) end(FlushReason.SWITCH)
        val saved = store.progress(mediaFileId)
        media = mediaFileId
        persist = persistPosition
        duration = if (durationMs > 0) durationMs else saved.durationMs
        position = if (persistPosition) saved.resumePositionMs() else 0
        lastSaved = if (persistPosition) saved.positionMs else -1
        hadHistory = saved.lastWatchedAt > 0
        durationRecorded = false
        knownFileDuration = store.mediaFile(mediaFileId)?.durationMs ?: 0
        recordDurationOnce()
        if (!persistPosition) {
            // Replay of a finished item: remember that it was watched again, nothing more.
            store.touchLastWatched(mediaFileId)
            writeCount++
        }
        startTimer()
    }

    fun update(positionMs: Long, durationMs: Long) {
        if (media == INVALID_ID) return
        position = maxOf(0, positionMs)
        if (durationMs > 0) {
            duration = durationMs
            recordDurationOnce()
        }
    }

    /** The duration of a file does not change while it plays: store it once instead of with every progress save. */
    private fun recordDurationOnce() {
        if (durationRecorded || duration <= 0 || media == INVALID_ID) return
        durationRecorded = true
        if (abs(duration - knownFileDuration) > 500) {
            store.setMediaDuration(media, duration)
            writeCount++
        }
    }

    private fun writePosition() {
        // The entry may have been removed from the library while it was playing.
        if (store.mediaFile(media) == null) {
            persist = false
            return
        }
        store.saveProgress(media, position, duration)
        lastSaved = position
        hadHistory = true
        writeCount++
    }

    fun flush(reason: FlushReason) {
        if (media == INVALID_ID || !persist) return
        if (!hadHistory && position < MIN_HISTORY_MS) return
        val delta = if (lastSaved < 0) position + 1 else abs(position - lastSaved)
        if (reason == FlushReason.PERIODIC) {
            if (delta < MIN_PERIODIC_DELTA_MS) return
        } else if (delta == 0L) {
            return
        }
        writePosition()
    }

    fun end(reason: FlushReason) {
        if (media == INVALID_ID) return
        flush(reason)
        stopTimer()
        media = INVALID_ID
        persist = true
        lastSaved = -1
    }

    /** The item played to its end: mark it completed (position resets to 0). */
    fun complete(durationMs: Long) {
        if (media == INVALID_ID) return
        val finished = media
        val d = if (durationMs > 0) durationMs else duration
        stopTimer()
        media = INVALID_ID
        persist = true
        lastSaved = -1
        if (store.mediaFile(finished) == null) return // removed from the library while playing
        store.markCompleted(finished, d)
        writeCount++
    }

    private fun startTimer() {
        stopTimer()
        timer = scheduler.postDelayed(intervalMs) {
            timer = null
            if (media != INVALID_ID) {
                flush(FlushReason.PERIODIC)
                startTimer()
            }
        }
    }

    private fun stopTimer() {
        timer?.cancel()
        timer = null
    }

    private companion object {
        /** Positions closer than this to the last saved value are not worth a write during normal playback. */
        const val MIN_PERIODIC_DELTA_MS = 1000L

        /** Opening an item and leaving within this time does not create history. */
        const val MIN_HISTORY_MS = 2000L
    }
}
