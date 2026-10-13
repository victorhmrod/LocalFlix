package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Port of the progress-manager and resume-target parts of tests/tst_progress.cpp. */
class ProgressAndResumeTest {
    // ------------------------------------------------------------ saving / throttling

    @Test
    fun positionIsSavedOnPauseAndAtShutdown() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Persist", 2)
        val pm = h.progress

        pm.begin(eps[0].file.id, true, 600_000)
        pm.update(65_000, 600_000)
        pm.flush(FlushReason.PAUSE)
        assertEquals(65_000L, h.library.progress(eps[0].file.id).positionMs) // immediate on pause

        pm.update(70_500, 600_000)
        pm.end(FlushReason.SHUTDOWN) // application closing
        val p = h.library.progress(eps[0].file.id)
        assertEquals(70_500L, p.positionMs)
        assertEquals(600_000L, p.durationMs)
        assertTrue(p.lastWatchedAt > 0)
        assertFalse(p.completed)
        assertEquals(70_500L, p.resumePositionMs())
        assertEquals(0L, h.library.progress(eps[1].file.id).positionMs) // independent per episode
    }

    @Test
    fun writesAreThrottled() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Throttle", 1)
        val pm = h.progress
        pm.begin(eps[0].file.id, true, 600_000)
        val base = pm.writeCount

        pm.update(10_000, 600_000)
        pm.flush(FlushReason.PERIODIC)
        assertEquals(base + 1, pm.writeCount)

        pm.update(10_400, 600_000) // moved less than a second: not worth a write
        pm.flush(FlushReason.PERIODIC)
        assertEquals(base + 1, pm.writeCount)

        pm.update(11_200, 600_000)
        pm.flush(FlushReason.PERIODIC)
        assertEquals(base + 2, pm.writeCount)

        pm.flush(FlushReason.PAUSE) // nothing changed: no write even on pause
        assertEquals(base + 2, pm.writeCount)

        pm.update(11_250, 600_000)
        pm.flush(FlushReason.PAUSE) // pause always persists a real change
        assertEquals(base + 3, pm.writeCount)
        assertEquals(11_250L, h.library.progress(eps[0].file.id).positionMs)
        pm.end(FlushReason.STOP)
    }

    @Test
    fun periodicTimerSavesWithoutExplicitFlush() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Timer", 1)
        h.progress.begin(eps[0].file.id, true, 600_000)
        h.progress.update(30_000, 600_000)
        h.scheduler.advance(5000)
        assertEquals(30_000L, h.library.progress(eps[0].file.id).positionMs)
        h.progress.update(45_000, 600_000)
        h.scheduler.advance(5000)
        assertEquals(45_000L, h.library.progress(eps[0].file.id).positionMs)
        val writes = h.progress.writeCount
        h.scheduler.advance(30_000)
        assertEquals(writes, h.progress.writeCount)
        h.progress.end(FlushReason.STOP)
        assertEquals(0, h.scheduler.pendingCount) // the timer stops with the session
    }

    @Test
    fun openingAndLeavingImmediatelyCreatesNoHistory() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Accident", 2)
        h.progress.begin(eps[0].file.id, true, 600_000)
        h.progress.update(800, 600_000) // less than 2 s in
        h.progress.end(FlushReason.SWITCH)
        assertEquals(0L, h.library.progress(eps[0].file.id).lastWatchedAt)
    }

    @Test
    fun replayingACompletedEpisodeKeepsItCompleted() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Replay", 2)
        h.library.markCompleted(eps[0].file.id, 600_000)
        assertEquals(1, h.library.progress(eps[0].file.id).playCount)

        h.progress.begin(eps[0].file.id, false, 600_000)
        h.progress.update(250_000, 600_000)
        h.progress.flush(FlushReason.PAUSE)
        h.progress.end(FlushReason.STOP)

        val p = h.library.progress(eps[0].file.id)
        assertTrue(p.completed) // still watched
        assertEquals(0L, p.positionMs) // position not clobbered
        assertEquals(0L, p.resumePositionMs())
        assertTrue(p.lastWatchedAt > 0) // but it counts as recently watched

        h.progress.begin(eps[0].file.id, false, 600_000)
        h.progress.complete(600_000)
        assertEquals(2, h.library.progress(eps[0].file.id).playCount)
    }

    @Test
    fun durationIsRecordedOnTheFileOnlyOnce() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Dur", 1)
        assertEquals(0L, h.library.mediaFile(eps[0].file.id)!!.durationMs)
        h.progress.begin(eps[0].file.id, true, 0)
        val w0 = h.progress.writeCount
        h.progress.update(5000, 421_000)
        assertEquals(421_000L, h.library.mediaFile(eps[0].file.id)!!.durationMs)
        assertEquals(w0 + 1, h.progress.writeCount)
        h.progress.update(6000, 421_000) // no further duration writes
        h.progress.update(7000, 421_000)
        assertEquals(w0 + 1, h.progress.writeCount)
        h.progress.end(FlushReason.STOP)
    }

    @Test
    fun anEntryRemovedWhilePlayingIsNotWrittenAgain() {
        val h = PlaybackHarness()
        val (_, eps) = h.library.makeShow("Vanished", 2)
        h.progress.begin(eps[0].file.id, true, 600_000)
        h.progress.update(60_000, 600_000)
        h.library.removeEpisodes(listOf(eps[0].id))
        h.progress.end(FlushReason.STOP) // must not throw nor resurrect a progress row
        assertFalse(h.library.progresses.containsKey(eps[0].file.id))
    }

    // ------------------------------------------------------------ series resume target

    @Test
    fun resumeTargetFollowsTheViewingHistory() {
        val h = PlaybackHarness()
        val (show, eps) = h.library.makeShow("Resume", 4)
        val lib = h.library
        fun target() = computeResumeTarget(lib.episodesOf(show.id))

        // nothing watched: start at the first episode
        var r = target()
        assertEquals(ResumeReason.NOT_STARTED, r.reason)
        assertEquals(eps[0].file.id, r.mediaFileId)
        assertEquals(0L, r.positionMs)

        // left partway through episode 1
        lib.saveProgress(eps[0].file.id, 30_000, 600_000)
        r = target()
        assertEquals(ResumeReason.IN_PROGRESS, r.reason)
        assertEquals(eps[0].file.id, r.mediaFileId)
        assertEquals(30_000L, r.positionMs)

        // finished episode 1: next unfinished is episode 2
        lib.markCompleted(eps[0].file.id, 600_000)
        r = target()
        assertEquals(ResumeReason.NEXT_UNFINISHED, r.reason)
        assertEquals(eps[1].file.id, r.mediaFileId)
        assertEquals(0L, r.positionMs)

        // jumped ahead and left episode 3 partway: that is where "Continue" goes
        lib.saveProgress(eps[2].file.id, 20_000, 600_000)
        r = target()
        assertEquals(eps[2].file.id, r.mediaFileId)
        assertEquals(20_000L, r.positionMs)

        // finished 3 and 4: nothing after them, wrap to the unfinished episode 2
        lib.markCompleted(eps[2].file.id, 600_000)
        lib.markCompleted(eps[3].file.id, 600_000)
        r = target()
        assertEquals(ResumeReason.NEXT_UNFINISHED, r.reason)
        assertEquals(eps[1].file.id, r.mediaFileId)

        // everything completed
        lib.markCompleted(eps[1].file.id, 600_000)
        r = target()
        assertTrue(r.allCompleted())
        assertEquals(eps[0].file.id, r.mediaFileId) // replay from the top
        assertEquals(0L, r.positionMs)
    }

    @Test
    fun resumeTargetUsesTheConfiguredOrderNotFileOrder() {
        val h = PlaybackHarness()
        val (show, eps) = h.library.makeShow("Custom order", 4)
        h.library.markCompleted(eps[0].file.id, 600_000)
        assertEquals(eps[1].file.id, computeResumeTarget(h.library.episodesOf(show.id)).mediaFileId)

        // the user decides episode 4 should come right after episode 1
        h.library.moveEpisode(eps[3].id, 1)
        assertEquals(eps[3].file.id, computeResumeTarget(h.library.episodesOf(show.id)).mediaFileId)
    }

    @Test
    fun manuallyWatchedEpisodesAreSkippedByResume() {
        val h = PlaybackHarness()
        val (show, eps) = h.library.makeShow("Marked", 5)
        // "I already saw these": completed without ever being played here (no viewing history)
        for (i in 0..2) h.library.progresses[eps[i].file.id] = Progress(eps[i].file.id, completed = true)
        val r = computeResumeTarget(h.library.episodesOf(show.id))
        assertEquals(eps[3].file.id, r.mediaFileId)
        assertEquals(ResumeReason.NOT_STARTED, r.reason)
    }

    @Test
    fun emptyShowHasNoResumeTarget() {
        assertFalse(computeResumeTarget(emptyList()).valid())
    }

    @Test
    fun progressFractionAndResumeRules() {
        assertEquals(0.0, Progress(1, 0, 0).fraction(), 0.0)
        assertEquals(0.5, Progress(1, 300, 600).fraction(), 1e-9)
        assertEquals(1.0, Progress(1, 0, 600, completed = true).fraction(), 0.0)
        assertEquals(1.0, Progress(1, 900, 600).fraction(), 0.0) // never above 1
        assertEquals(0L, Progress(1, 4_999, 600_000).resumePositionMs())
        assertEquals(5_000L, Progress(1, 5_000, 600_000).resumePositionMs())
        assertEquals(0L, Progress(1, 100_000, 600_000, completed = true).resumePositionMs())
        assertTrue(Progress(1, 5_000, 600_000).started())
        assertFalse(Progress(1, 100_000, 600_000, completed = true).started())
    }
}
