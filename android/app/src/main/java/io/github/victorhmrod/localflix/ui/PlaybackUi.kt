package io.github.victorhmrod.localflix.ui

import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.victorhmrod.localflix.core.MIN_RESUME_MS
import io.github.victorhmrod.localflix.core.PlaybackController
import io.github.victorhmrod.localflix.core.PlaybackEvent
import io.github.victorhmrod.localflix.core.PlaybackFailure
import io.github.victorhmrod.localflix.core.PlaybackState
import io.github.victorhmrod.localflix.core.QueueEntry
import io.github.victorhmrod.localflix.core.SkipSegment
import io.github.victorhmrod.localflix.core.TrackInfo
import io.github.victorhmrod.localflix.core.formatClock

/** What the player screen shows, kept current from the controller's events. Compose state, main thread. */
class PlaybackUi(private val controller: PlaybackController) {
    var state by mutableStateOf(PlaybackState.IDLE)
        private set
    var entry by mutableStateOf(QueueEntry())
        private set
    var positionMs by mutableLongStateOf(0L)
        private set
    var durationMs by mutableLongStateOf(0L)
        private set
    var hasNext by mutableStateOf(false)
        private set
    var hasPrevious by mutableStateOf(false)
        private set
    var tracks by mutableStateOf<List<TrackInfo>>(emptyList())
        private set

    /** The opening/ending the playhead is in (invalid when none): the player offers a Skip button while there is one. */
    var activeSegment by mutableStateOf(SkipSegment())
        private set

    /** Countdown to the next item: who is next and the uptime (ms) when it starts. */
    var upNext by mutableStateOf<QueueEntry?>(null)
        private set
    var upNextAt by mutableLongStateOf(0L)
        private set

    /** Playback ended and auto-play is off (or nothing is left): what the user can still do. */
    var endedNext by mutableStateOf<QueueEntry?>(null)
        private set
    var finished by mutableStateOf(false)
        private set

    var failure by mutableStateOf<Pair<QueueEntry, PlaybackFailure>?>(null)
        private set

    /** A short transient message ("Episode 3 finished"), cleared by the UI. */
    var notice by mutableStateOf<String?>(null)

    private val listener = io.github.victorhmrod.localflix.core.PlaybackListener { e ->
        when (e) {
            is PlaybackEvent.StateChanged -> state = e.state
            is PlaybackEvent.NowPlaying -> {
                entry = e.entry
                hasNext = e.hasNext
                hasPrevious = e.hasPrevious
                upNext = null
                endedNext = null
                finished = false
                failure = null
                if (controller.positionMs >= MIN_RESUME_MS) notice = "Resuming at ${formatClock(controller.positionMs)}"
            }
            is PlaybackEvent.PositionChanged -> {
                positionMs = e.positionMs
                durationMs = e.durationMs
            }
            is PlaybackEvent.TracksChanged -> tracks = e.tracks
            is PlaybackEvent.SkipSegmentChanged -> activeSegment = e.segment
            is PlaybackEvent.SegmentSkipped -> if (e.automatic) notice = "Skipped ${e.segment.noun}"
            is PlaybackEvent.ItemCompleted -> {}
            is PlaybackEvent.AutoAdvancePending -> {
                upNext = e.next
                upNextAt = SystemClock.uptimeMillis() + e.remainingMs
            }
            PlaybackEvent.AutoAdvanceCancelled -> upNext = null
            is PlaybackEvent.QueueUpdated -> {
                entry = e.current
                hasNext = e.hasNext
                hasPrevious = e.hasPrevious
            }
            is PlaybackEvent.QueueFinished -> {
                upNext = null
                finished = true
            }
            is PlaybackEvent.EndedWithoutAdvance -> endedNext = e.next
            is PlaybackEvent.Stopped -> {
                upNext = null
                endedNext = null
            }
            is PlaybackEvent.Failed -> {
                upNext = null
                entry = e.entry.takeIf { it.valid() } ?: entry
                failure = e.entry to e.failure
            }
            is PlaybackEvent.ResumeDecisionNeeded -> {}
        }
    }

    init {
        controller.addListener(listener)
    }

    fun dismissFailure() {
        failure = null
    }

    fun reset() {
        upNext = null
        endedNext = null
        finished = false
        failure = null
        notice = null
    }
}
