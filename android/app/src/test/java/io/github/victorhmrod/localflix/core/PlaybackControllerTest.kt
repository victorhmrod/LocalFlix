package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Port of tests/tst_playback.cpp: the controller against a scripted engine. Natural end vs. manual stop vs.
 * error, stale events, countdown/advance rules, resume modes, missing files.
 */
class PlaybackControllerTest {
    private fun harness(countdown: Int = 0) = PlaybackHarness().also { it.settings.setAutoPlayCountdownSeconds(countdown) }

    // ------------------------------------------------------------ natural end

    @Test
    fun naturalEndMarksCompletedAndStartsNextEpisode() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Auto", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        assertEquals(1, h.backend.loads.size)
        assertEquals(eps[0].file.uri, h.backend.lastLoad.uri)
        assertEquals(0L, h.backend.lastLoad.startMs)

        h.backend.tick(120.0)
        h.backend.end(EndReason.EOF)

        assertEquals(1, h.events<PlaybackEvent.ItemCompleted>().size)
        assertTrue(h.library.progress(eps[0].file.id).completed)
        assertEquals(600_000L, h.library.progress(eps[0].file.id).durationMs)
        val pending = h.events<PlaybackEvent.AutoAdvancePending>()
        assertEquals(1, pending.size)
        assertEquals(eps[1].file.id, pending[0].next.mediaFileId)

        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)
        assertEquals(eps[1].file.id, h.controller.currentEntry.mediaFileId)
    }

    @Test
    fun countdownDelaysTheNextEpisodeAndReportsItsLength() {
        val h = harness(countdown = 1)
        val (show, eps) = h.library.makeShow("Countdown", 2)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)

        assertEquals(PlaybackState.ADVANCE_PENDING, h.controller.state)
        val pending = h.events<PlaybackEvent.AutoAdvancePending>()
        assertEquals(1, pending.size)
        assertEquals(1000L, pending[0].remainingMs)
        h.scheduler.advance(300)
        assertEquals(1, h.backend.loads.size) // still counting down
        h.scheduler.advance(800)
        assertEquals(2, h.backend.loads.size)
    }

    @Test
    fun lastEpisodeEndsCleanlyWithoutAdvancing() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Finale", 3)

        assertTrue(h.startEpisode(show, eps[2]))
        h.backend.end(EndReason.EOF)

        val finished = h.events<PlaybackEvent.QueueFinished>()
        assertEquals(1, finished.size)
        assertEquals(eps[2].file.id, finished[0].last.mediaFileId)
        assertEquals(0, h.events<PlaybackEvent.AutoAdvancePending>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
        assertTrue(h.library.progress(eps[2].file.id).completed)
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size) // nothing else was started
    }

    @Test
    fun walkingThroughTheWholeSeriesPlaysEveryEpisodeOnce() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Marathon", 4)

        assertTrue(h.startEpisode(show, eps[0]))
        for (i in 1 until eps.size) {
            h.backend.end(EndReason.EOF)
            h.scheduler.settle()
            assertEquals(i + 1, h.backend.loads.size)
            assertEquals(eps[i].file.uri, h.backend.lastLoad.uri)
            h.loadFinished()
        }
        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.QueueFinished>().size)
        for (e in h.library.episodesOf(show.id)) assertTrue(e.progress.completed)
        assertEquals(4, h.backend.loads.size)
    }

    @Test
    fun autoPlayOffStopsAfterTheEpisode() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Manual advance", 2)
        h.settings.setAutoPlayNext(false)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)

        val ended = h.events<PlaybackEvent.EndedWithoutAdvance>()
        assertEquals(1, ended.size)
        assertEquals(eps[1].file.id, ended[0].next.mediaFileId) // offered, not started
        assertEquals(0, h.events<PlaybackEvent.AutoAdvancePending>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size)
        assertTrue(h.library.progress(eps[0].file.id).completed)
    }

    @Test
    fun autoAdvanceCanSkipAlreadyCompletedEpisodes() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Skippy", 3)
        h.library.markCompleted(eps[1].file.id, 1000)

        // setting off (default): ep2 plays again
        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)

        // setting on: ep2 is skipped when advancing automatically...
        h.settings.setSkipCompleted(true)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.FROM_START))
        h.loadFinished()
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(4, h.backend.loads.size)
        assertEquals(eps[2].file.uri, h.backend.lastLoad.uri)

        // ...but the manual Next button still follows the configured queue strictly
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.FROM_START))
        h.loadFinished()
        h.controller.next()
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)
    }

    // ------------------------------------------------------------ things that must NOT advance

    @Test
    fun manualStopDoesNotStartTheNextEpisode() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Stop", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.tick(90.0)
        h.controller.stop()
        assertEquals(PlaybackState.IDLE, h.controller.state)
        assertEquals(1, h.backend.stopCalls)
        assertEquals(1, h.events<PlaybackEvent.Stopped>().size)

        // the engine reports the stop it was asked for; and, belt and braces, even a (nonsensical) late EOF
        h.backend.end(EndReason.STOP)
        h.backend.end(EndReason.EOF)
        h.scheduler.advance(100)

        assertEquals(1, h.backend.loads.size)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        val p = h.library.progress(eps[0].file.id)
        assertFalse(p.completed)
        assertEquals(90_000L, p.positionMs) // saved on stop
    }

    @Test
    fun stopReportedByTheEngineIsNeverACompletion() {
        val h = harness()
        val (show, eps) = h.library.makeShow("EngineStop", 2)
        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.tick(10.0)
        h.backend.end(EndReason.STOP) // e.g. the engine was stopped from outside
        h.scheduler.advance(100)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertEquals(1, h.backend.loads.size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
    }

    @Test
    fun playbackErrorDoesNotAdvance() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Broken", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.tick(30.0)
        h.backend.end(EndReason.ERROR, error = "unsupported or unrecognised file format")

        val failed = h.events<PlaybackEvent.Failed>()
        assertEquals(1, failed.size)
        assertEquals(FailureKind.LOAD_ERROR, failed[0].failure.kind)
        assertTrue(failed[0].failure.detail.contains("unsupported"))
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
    }

    @Test
    fun loadErrorBeforePlaybackStartedDoesNotAdvance() {
        val h = harness()
        val (show, eps) = h.library.makeShow("NeverStarted", 2)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id))
        assertEquals(PlaybackState.LOADING, h.controller.state)
        h.backend.end(EndReason.ERROR, error = "could not be opened") // onFileLoaded never came
        assertEquals(1, h.events<PlaybackEvent.Failed>().size)
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size)
    }

    @Test
    fun eofBeforeTheFileEverLoadedIsNotACompletion() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Phantom", 2)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id))
        h.backend.end(EndReason.EOF) // loading state: nothing ever played
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertEquals(1, h.events<PlaybackEvent.Failed>().size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
    }

    @Test
    fun seekingNearTheEndDoesNotCompleteTheEpisode() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Seeker", 2)

        assertTrue(h.startEpisode(show, eps[0], StartMode.AUTO, 600_000))
        h.controller.seekToMs(599_000) // one second from the end
        assertEquals(1, h.backend.seeks.size)
        assertEquals(599_000L to false, h.backend.seeks.last())
        h.backend.tick(599.2)
        h.scheduler.advance(50)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
        assertEquals(1, h.backend.loads.size)

        // closing the application right now keeps the position, still not completed
        h.controller.shutdown()
        val p = h.library.progress(eps[0].file.id)
        assertFalse(p.completed)
        assertTrue(p.positionMs >= 599_000)
    }

    @Test
    fun staleEventsFromAReplacedFileAreIgnored() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Stale", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        val firstToken = h.backend.lastToken
        h.controller.next() // user presses Next: episode 2 is loaded, episode 1 is replaced
        assertEquals(2, h.backend.loads.size)
        val secondToken = h.backend.lastToken
        assertTrue(secondToken != firstToken)

        // events that were already queued for the first file arrive late
        h.backend.end(EndReason.STOP, firstToken) // what the engine sends for a replaced file
        h.backend.end(EndReason.EOF, firstToken) // even a race where it also hit its end
        h.backend.finishLoading(firstToken)
        h.scheduler.advance(100)

        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
        assertEquals(2, h.backend.loads.size) // no extra load either
        assertEquals(PlaybackState.LOADING, h.controller.state) // still waiting for episode 2
        h.backend.finishLoading(secondToken)
        assertEquals(PlaybackState.PLAYING, h.controller.state)
        assertEquals(eps[1].file.id, h.controller.currentEntry.mediaFileId)
    }

    @Test
    fun aSingleEndOfFileProducesExactlyOneTransition() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Once", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        val token = h.backend.lastToken
        h.backend.end(EndReason.EOF, token)
        h.backend.end(EndReason.EOF, token) // duplicate delivery
        h.backend.end(EndReason.EOF, token)
        h.scheduler.settle()
        h.scheduler.advance(100)
        assertEquals(1, h.events<PlaybackEvent.ItemCompleted>().size)
        assertEquals(1, h.events<PlaybackEvent.AutoAdvancePending>().size)
        assertEquals(2, h.backend.loads.size)
        assertEquals(1, h.library.progress(eps[0].file.id).playCount)
    }

    // ------------------------------------------------------------ countdown control

    @Test
    fun cancellingTheCountdownStopsPlaybackGracefully() {
        val h = harness(countdown = 1)
        val (show, eps) = h.library.makeShow("Cancel", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        assertEquals(PlaybackState.ADVANCE_PENDING, h.controller.state)
        h.controller.cancelAutoAdvance()
        assertEquals(1, h.events<PlaybackEvent.AutoAdvanceCancelled>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)

        h.scheduler.advance(1400) // longer than the countdown would have been
        assertEquals(1, h.backend.loads.size)
        assertTrue(h.library.progress(eps[0].file.id).completed) // the finished one stays finished
        h.controller.cancelAutoAdvance() // harmless when nothing is pending
        assertEquals(1, h.events<PlaybackEvent.AutoAdvanceCancelled>().size)
    }

    @Test
    fun playNowSkipsTheCountdown() {
        val h = harness(countdown = 30)
        val (show, eps) = h.library.makeShow("Now", 2)
        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        assertEquals(PlaybackState.ADVANCE_PENDING, h.controller.state)
        h.controller.advanceNow()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)
        // the old countdown must not fire a second start later
        h.scheduler.advance(60_000)
        assertEquals(2, h.backend.loads.size)
    }

    @Test
    fun stopAndNextDuringCountdownAreHandled() {
        val h = harness(countdown = 1)
        val (show, eps) = h.library.makeShow("Interrupt", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        h.controller.stop() // Stop during the countdown cancels it
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(1300)
        assertEquals(1, h.backend.loads.size)

        assertTrue(h.startEpisode(show, eps[0], StartMode.FROM_START))
        h.backend.end(EndReason.EOF)
        h.controller.next() // Next during the countdown plays right away, exactly once
        assertEquals(3, h.backend.loads.size)
        h.scheduler.advance(1300)
        assertEquals(3, h.backend.loads.size)
    }

    @Test
    fun reorderingDuringCountdownRetargetsTheNextEpisode() {
        val h = harness(countdown = 2)
        val (show, eps) = h.library.makeShow("Retarget", 3)

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        val pending = h.events<PlaybackEvent.AutoAdvancePending>()
        assertEquals(1, pending.size)
        assertEquals(eps[1].file.id, pending.last().next.mediaFileId)

        // user drags episode 3 in front of episode 2 while the overlay counts down
        h.library.moveEpisode(eps[2].id, 1)
        h.scheduler.settle()
        val after = h.events<PlaybackEvent.AutoAdvancePending>()
        assertEquals(2, after.size)
        assertEquals(eps[2].file.id, after.last().next.mediaFileId)
        assertTrue(after.last().remainingMs <= 2000)

        h.scheduler.advance(2100)
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[2].file.uri, h.backend.lastLoad.uri) // the new order won
    }

    @Test
    fun removingTheNextEpisodeDuringCountdownRetargetsOrFinishes() {
        val h = harness(countdown = 1)
        val (show, eps) = h.library.makeShow("Remove next", 2)
        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        h.library.removeEpisodes(listOf(eps[1].id)) // the only remaining episode disappears
        h.scheduler.settle()
        assertEquals(1, h.events<PlaybackEvent.QueueFinished>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(1200)
        assertEquals(1, h.backend.loads.size)
    }

    // ------------------------------------------------------------ navigation

    @Test
    fun nextAndPreviousFollowTheConfiguredOrder() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Nav", 3)
        // configured order: ep3, ep1, ep2
        h.library.moveEpisode(eps[2].id, 0)

        assertTrue(h.controller.play(h.showContext(show), eps[2].file.id, StartMode.FROM_START))
        h.loadFinished()
        assertFalse(h.controller.hasPrevious)
        assertTrue(h.controller.hasNext)

        h.controller.next()
        assertEquals(eps[0].file.uri, h.backend.lastLoad.uri)
        h.loadFinished()
        h.controller.next()
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)
        h.loadFinished()
        assertFalse(h.controller.hasNext)
        val loadsAtEnd = h.backend.loads.size
        h.controller.next() // nothing after the last one
        assertEquals(loadsAtEnd, h.backend.loads.size)

        h.controller.previous()
        assertEquals(eps[0].file.uri, h.backend.lastLoad.uri)
    }

    @Test
    fun reorderingWhilePlayingUpdatesWhatNextMeans() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Live reorder", 3)

        assertTrue(h.startEpisode(show, eps[0], StartMode.FROM_START))
        assertTrue(h.controller.hasNext)
        h.library.moveEpisode(eps[0].id, 2) // current episode dragged to the end
        h.scheduler.settle()
        assertTrue(h.events<PlaybackEvent.QueueUpdated>().isNotEmpty())
        assertFalse(h.controller.hasNext)
        assertTrue(h.controller.hasPrevious)
        h.controller.previous()
        assertEquals(eps[2].file.uri, h.backend.lastLoad.uri) // previous in the *new* order
    }

    @Test
    fun removingThePlayingEpisodeStillAdvancesToTheNextOne() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Vanish", 3)
        assertTrue(h.startEpisode(show, eps[1], StartMode.FROM_START))
        h.library.removeEpisodes(listOf(eps[1].id)) // removed from library while playing
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[2].file.uri, h.backend.lastLoad.uri)
    }

    // ------------------------------------------------------------ starting positions

    @Test
    fun resumeStartsAtTheSavedPosition() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Resume", 2)
        h.library.saveProgress(eps[0].file.id, 100_000, 600_000)

        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.AUTO))
        assertEquals(100_000L, h.backend.lastLoad.startMs)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.FROM_START))
        assertEquals(0L, h.backend.lastLoad.startMs)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.RESUME))
        assertEquals(100_000L, h.backend.lastLoad.startMs)
    }

    @Test
    fun resumeSettingOffAsksInsteadOfJumping() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Ask", 1)
        h.library.saveProgress(eps[0].file.id, 100_000, 600_000)
        h.settings.setResumeAutomatically(false)

        assertFalse(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.AUTO))
        assertEquals(0, h.backend.loads.size) // nothing started yet
        val ask = h.events<PlaybackEvent.ResumeDecisionNeeded>()
        assertEquals(1, ask.size)
        assertEquals(100_000L, ask[0].positionMs)

        // an explicit answer from the user then starts it
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.FROM_START))
        assertEquals(0L, h.backend.lastLoad.startMs)
    }

    @Test
    fun tinyPositionsAreNotWorthResuming() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Tiny", 1)
        h.library.saveProgress(eps[0].file.id, 2_500, 600_000) // 2.5 s in
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.AUTO))
        assertEquals(0L, h.backend.lastLoad.startMs)
    }

    @Test
    fun completedEpisodesReplayFromTheStartAndStayCompleted() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Rewatch", 2)
        h.library.markCompleted(eps[0].file.id, 600_000)

        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.RESUME))
        assertEquals(0L, h.backend.lastLoad.startMs)
        h.loadFinished()
        h.backend.tick(200.0)
        h.controller.setPaused(true) // would normally persist the position
        assertTrue(h.library.progress(eps[0].file.id).completed)
        assertEquals(0L, h.library.progress(eps[0].file.id).positionMs)

        h.controller.stop()
        assertTrue(h.library.progress(eps[0].file.id).completed)
        // and "Continue" still points past it
        assertEquals(eps[1].file.id, computeResumeTarget(h.library.episodesOf(show.id)).mediaFileId)
    }

    @Test
    fun watchAgainAfterEverythingCompletedStartsAtTheTop() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Done", 2)
        for (e in eps) h.library.markCompleted(e.file.id, 600_000)
        val target = computeResumeTarget(h.library.episodesOf(show.id))
        assertTrue(target.allCompleted())
        assertTrue(h.controller.play(h.showContext(show), target.mediaFileId, StartMode.AUTO))
        assertEquals(0L, h.backend.lastLoad.startMs)
        assertEquals(eps[0].file.uri, h.backend.lastLoad.uri)
    }

    // ------------------------------------------------------------ saving

    @Test
    fun progressIsSavedOnPauseOnSwitchAndOnShutdown() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Saves", 2)

        assertTrue(h.startEpisode(show, eps[0], StartMode.FROM_START))
        h.backend.tick(33.0)
        h.controller.setPaused(true) // immediate save on pause
        assertEquals(PlaybackState.PAUSED, h.controller.state)
        assertEquals(33_000L, h.library.progress(eps[0].file.id).positionMs)

        h.controller.setPaused(false)
        h.backend.tick(41.0)
        h.controller.next() // save when switching episodes
        assertEquals(41_000L, h.library.progress(eps[0].file.id).positionMs)
        h.loadFinished()
        h.backend.tick(12.0)
        h.controller.shutdown() // and when the application closes
        assertEquals(12_000L, h.library.progress(eps[1].file.id).positionMs)
        assertEquals(41_000L, h.library.progress(eps[0].file.id).positionMs) // earlier one untouched
    }

    @Test
    fun periodicTimerSavesWhilePlayingEveryFewSeconds() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Ticker", 1)
        assertTrue(h.startEpisode(show, eps[0], StartMode.FROM_START))
        h.backend.tick(30.0)
        assertEquals(0L, h.library.progress(eps[0].file.id).positionMs) // not yet: the timer has not fired
        h.scheduler.advance(5000)
        assertEquals(30_000L, h.library.progress(eps[0].file.id).positionMs)
        h.backend.tick(45.0)
        h.scheduler.advance(5000)
        assertEquals(45_000L, h.library.progress(eps[0].file.id).positionMs)
        val writes = h.progress.writeCount
        h.scheduler.advance(20_000) // idle ticks do not keep writing the same value
        assertEquals(writes, h.progress.writeCount)
    }

    @Test
    fun trackChoiceIsRestoredWhenAFileStartsAgain() {
        val h = harness()
        val (show, eps) = h.library.makeShow("TrackMemory", 1)
        h.library.saveTrackSelection(eps[0].file.id, 2, 3)
        assertTrue(h.startEpisode(show, eps[0]))
        assertEquals(listOf(2), h.backend.audioTrackRequests)
        assertEquals(listOf(3), h.backend.subtitleTrackRequests)
    }

    // ------------------------------------------------------------ files

    @Test
    fun missingFileProducesRecoveryInformationAndNeverReachesTheBackend() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Gone", 3)
        h.library.unavailable += eps[1].file.uri // disconnected drive / deleted / access revoked

        assertFalse(h.controller.play(h.showContext(show), eps[1].file.id))
        assertEquals(0, h.backend.loads.size)
        val failed = h.events<PlaybackEvent.Failed>()
        assertEquals(1, failed.size)
        assertEquals(eps[1].file.id, failed[0].entry.mediaFileId)
        assertEquals(FailureKind.MISSING_FILE, failed[0].failure.kind)
        assertTrue(failed[0].failure.detail.contains(eps[1].file.label))
        assertTrue(h.library.files.getValue(eps[1].file.id).missing)
        assertEquals(PlaybackState.IDLE, h.controller.state)

        // recovery: the user locates the file somewhere else
        h.library.relinkFile(eps[1].file.id, "recovered/ep02-new-name.mp4")
        h.queue.reload()
        assertTrue(h.controller.playEntry(eps[1].file.id, StartMode.RESUME))
        assertEquals(h.library.uriOf("recovered/ep02-new-name.mp4"), h.backend.lastLoad.uri)
        assertFalse(h.library.files.getValue(eps[1].file.id).missing)
    }

    @Test
    fun advancingIntoAMissingFileDoesNotLoopOrCrash() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Hole", 3)
        h.library.unavailable += eps[1].file.uri

        assertTrue(h.startEpisode(show, eps[0]))
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(1, h.events<PlaybackEvent.Failed>().size)
        assertEquals(1, h.backend.loads.size) // episode 2 was never handed to the engine
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(100)
        assertEquals(1, h.events<PlaybackEvent.Failed>().size) // and nothing retries by itself

        // the user chooses "skip to next" from the error panel
        h.controller.next()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[2].file.uri, h.backend.lastLoad.uri)
    }

    @Test
    fun startingSomethingNotInTheQueueIsReportedNotCrashed() {
        val h = harness()
        val (show, _) = h.library.makeShow("Q", 1)
        h.queue.setContext(h.showContext(show))
        assertFalse(h.controller.playEntry(424242))
        val failed = h.events<PlaybackEvent.Failed>()
        assertEquals(1, failed.size)
        assertEquals(FailureKind.NOT_IN_QUEUE, failed[0].failure.kind)
    }

    // ------------------------------------------------------------ movies & collections

    @Test
    fun aMovieEndsWithoutAdvancingAndKeepsItsOwnProgress() {
        val h = harness()
        val solo = h.library.makeMovie("Solo")
        h.library.makeMovie("Other")
        val ctx = QueueContext(ContextType.MOVIE, solo.id)

        assertTrue(h.controller.play(ctx, solo.file.id, StartMode.AUTO))
        h.loadFinished(5_400_000)
        h.backend.tick(1234.0)
        h.controller.stop()
        assertEquals(1_234_000L, h.library.progress(solo.file.id).positionMs)

        assertTrue(h.controller.play(ctx, solo.file.id, StartMode.AUTO))
        assertEquals(1_234_000L, h.backend.lastLoad.startMs)
        h.loadFinished(5_400_000)
        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.QueueFinished>().size)
        assertTrue(h.library.progress(solo.file.id).completed)
        assertEquals(2, h.backend.loads.size) // never wandered on to the other movie
    }

    @Test
    fun collectionPlaysShowsAndMoviesInSequence() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Col show", 2)
        val movie = h.library.makeMovie("Film")
        val col = h.library.makeCollection("Night", CollectionItemType.MOVIE to movie.id, CollectionItemType.SHOW to show.id)
        val ctx = QueueContext(ContextType.COLLECTION, col)

        assertTrue(h.controller.play(ctx, movie.file.id, StartMode.FROM_START))
        h.loadFinished()
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[0].file.uri, h.backend.lastLoad.uri)
        h.loadFinished()
        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(3, h.backend.loads.size)
        assertEquals(eps[1].file.uri, h.backend.lastLoad.uri)
    }

    // ------------------------------------------------------------ player options

    @Test
    fun volumeSpeedAndTrackCommandsReachTheBackend() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Options", 1)
        assertTrue(h.startEpisode(show, eps[0]))
        h.controller.setVolume(60)
        assertEquals(60, h.backend.volume)
        h.controller.setVolume(500) // clamped to the engine's range
        assertEquals(130, h.backend.volume)
        h.controller.setMuted(true)
        assertTrue(h.backend.isMuted)
        h.controller.setSpeed(1.5)
        assertEquals(1.5, h.backend.speed, 0.0)
        h.controller.setSpeed(99.0)
        assertEquals(4.0, h.backend.speed, 0.0)
        h.controller.seekByMs(10_000)
        assertEquals(10_000L to true, h.backend.seeks.last())
        h.controller.selectSubtitleTrack(2)
        assertEquals(2, h.backend.subtitleTrackRequests.last())
        h.controller.selectAudioTrack(1)
        assertEquals(1, h.backend.audioTrackRequests.last())
        // the scripted backend reports no selected subtitle, so the other stream is remembered as "off"
        assertEquals(TRACK_OFF, h.library.progress(eps[0].file.id).subtitleTrack)
    }

    @Test
    fun newPlaybackSessionStartsAtTheConfiguredDefaultSpeed() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Speedy", 1)
        h.settings.setPlaybackSpeed(1.25)
        assertTrue(h.controller.play(h.showContext(show), eps[0].file.id, StartMode.FROM_START))
        assertEquals(1.25, h.backend.speed, 0.0)
    }
}
