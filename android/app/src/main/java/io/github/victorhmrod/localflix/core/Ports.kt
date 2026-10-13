package io.github.victorhmrod.localflix.core

/** Wall-clock time, replaceable in tests. */
fun interface Clock {
    fun nowMs(): Long
}

object SystemClock : Clock {
    override fun nowMs(): Long = System.currentTimeMillis()
}

fun interface Cancelable {
    fun cancel()
}

/** Single-threaded timers (the Android main looper in the app, a manual clock in tests). */
interface Scheduler {
    /** Monotonic milliseconds, used to measure how much of a countdown is left. */
    fun uptimeMs(): Long

    fun postDelayed(delayMs: Long, task: () -> Unit): Cancelable
}

/** Whether the file behind a content URI can still be opened (moved, deleted, permission revoked...). */
fun interface FileProbe {
    fun exists(uri: String): Boolean
}

/** Why progress is being written; decides which throttling rules apply. */
enum class FlushReason { PERIODIC, PAUSE, SWITCH, STOP, SHUTDOWN }

/**
 * Everything the playback side needs from the library. All calls are small indexed reads/writes that the
 * playback code makes from the main thread, exactly like the desktop controller does.
 */
interface PlaybackStore {
    fun buildQueue(context: QueueContext): List<QueueEntry>

    fun progress(mediaFileId: Id): Progress

    fun mediaFile(id: Id): MediaFile?

    fun saveProgress(mediaFileId: Id, positionMs: Long, durationMs: Long)

    fun touchLastWatched(mediaFileId: Id)

    fun saveTrackSelection(mediaFileId: Id, audioTrack: Int, subtitleTrack: Int)

    fun markCompleted(mediaFileId: Id, durationMs: Long)

    /** Records the file's duration once it is known (not on every progress save). */
    fun setMediaDuration(mediaFileId: Id, durationMs: Long)

    fun setMissing(mediaFileId: Id, missing: Boolean): Boolean
}

/** The slice of the settings the playback logic reads. */
interface PlaybackSettings {
    val autoPlayNext: Boolean
    val autoPlayCountdownSeconds: Int
    val resumeAutomatically: Boolean
    val skipCompleted: Boolean
    val playbackSpeed: Double
}
