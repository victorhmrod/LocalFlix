package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Port of tests/tst_skip.cpp: skipping the opening and the ending. Chapter classification and segment detection
 * (pure functions), then the PlaybackController rules against the scripted engine: when the button is offered,
 * when a skip happens by itself, and that an ending which runs to the end of the file finishes the item only
 * through the engine's end-of-file.
 */
class SkipSegmentsTest {
    private fun ch(title: String, seconds: Double) = ChapterInfo(title, (seconds * 1000).toLong())

    /** A typical episode: cold open, opening 0:30-2:00, story, ending 9:00-10:00 (with the next-episode preview). */
    private fun typicalChapters() = listOf(ch("Prologue", 0.0), ch("Opening", 30.0), ch("Episode", 120.0), ch("Ending", 540.0), ch("Preview", 585.0))

    private fun harness(countdown: Int = 0) = PlaybackHarness().also { it.settings.setAutoPlayCountdownSeconds(countdown) }

    private fun PlaybackHarness.startWith(
        show: Show,
        e: Episode,
        chapters: List<ChapterInfo>,
        durationMs: Long = 600_000,
        mode: StartMode = StartMode.FROM_START,
    ): Boolean {
        backend.setChapters(chapters)
        return startEpisode(show, e, mode, durationMs)
    }

    // ------------------------------------------------------------ chapter titles

    @Test
    fun chapterTitlesAreClassifiedByWholeWords() {
        val intro = ChapterRole.INTRO
        val ending = ChapterRole.ENDING
        val preview = ChapterRole.PREVIEW
        val other = ChapterRole.OTHER
        val table = listOf(
            "Opening" to intro,
            "OP" to intro,
            "op" to intro,
            "OP1" to intro,
            "Opening 2" to intro,
            "Opening Theme" to intro,
            "Opening: Some Song Name" to intro,
            "OP - Some Song Name" to intro,
            "[OP] Some Song Name" to intro,
            "Opening (TV Size)" to intro,
            "Intro" to intro,
            "Abertura" to intro,
            "Abertura do Episódio" to intro,
            "NCOP" to intro,

            "Ending" to ending,
            "ED" to ending,
            "ED2" to ending,
            "Credits" to ending,
            "End Credits" to ending,
            "Closing Credits" to ending,
            "Creditos" to ending,
            "Créditos" to ending,
            "CRÉDITOS" to ending,
            "Encerramento" to ending,
            "Ending Theme" to ending,
            "Outro" to ending,

            "Preview" to preview,
            "Next Episode Preview" to preview,
            "Prévia" to preview,
            "Trailer" to preview,

            "" to other,
            "Prologue" to other,
            "Episode" to other,
            "Part A" to other,
            "Chapter 1" to other,
            "The End" to other,
            "Operation" to other, // a substring is not a word
            "Edward" to other,
            "Editing" to other,
            "Ed's Place" to other,
            "Opening Night" to other,
            "Chapter 1: Opening Night" to other,
            "Post-Credits Scene" to other,
            "Opening Credits" to other, // ambiguous
            "Next Day" to other,
        )
        val wrong = table.filter { (title, role) -> classifyChapterTitle(title) != role }.map { (title, role) -> "\"$title\" should be $role but is ${classifyChapterTitle(title)}" }
        assertTrue(wrong.joinToString("\n"), wrong.isEmpty())
    }

    // ------------------------------------------------------------ segment detection

    @Test
    fun aTypicalEpisodeHasAnOpeningAndAnEndingThatIncludesThePreview() {
        val segs = detectSkipSegments(typicalChapters(), 600_000)
        assertEquals(2, segs.size)

        assertEquals(SegmentKind.INTRO, segs[0].kind)
        assertEquals(30_000L, segs[0].startMs)
        assertEquals(120_000L, segs[0].endMs)
        assertFalse(segs[0].toEnd)
        assertEquals("Opening", segs[0].title)

        assertEquals(SegmentKind.ENDING, segs[1].kind)
        assertEquals(540_000L, segs[1].startMs)
        assertEquals(600_000L, segs[1].endMs) // through the preview, up to the end of the file
        assertTrue(segs[1].toEnd)
    }

    @Test
    fun aChapterAfterTheEndingKeepsTheSceneOutOfTheSegment() {
        val segs = detectSkipSegments(listOf(ch("Episode", 0.0), ch("Ending", 500.0), ch("Post-Credits Scene", 570.0)), 600_000)
        assertEquals(1, segs.size)
        assertEquals(500_000L, segs[0].startMs)
        assertEquals(570_000L, segs[0].endMs)
        assertFalse(segs[0].toEnd)
    }

    @Test
    fun neighbouringChaptersOfTheSameRoleAreMerged() {
        val segs = detectSkipSegments(listOf(ch("OP", 0.0), ch("OP Part 2", 45.0), ch("Episode", 90.0)), 600_000)
        assertEquals(1, segs.size)
        assertEquals(0L, segs[0].startMs)
        assertEquals(90_000L, segs[0].endMs)
    }

    @Test
    fun unorderedChaptersAreSortedFirst() {
        val segs = detectSkipSegments(listOf(ch("Episode", 120.0), ch("Opening", 30.0), ch("Prologue", 0.0)), 600_000)
        assertEquals(1, segs.size)
        assertEquals(30_000L, segs[0].startMs)
        assertEquals(120_000L, segs[0].endMs)
    }

    @Test
    fun segmentsTooShortToBeWorthAButtonAreDropped() {
        assertTrue(detectSkipSegments(listOf(ch("Episode", 0.0), ch("Opening", 100.0), ch("Story", 101.0)), 600_000).isEmpty())
    }

    @Test
    fun aStrayPreviewIsNotASegment() {
        assertTrue(detectSkipSegments(listOf(ch("Episode", 0.0), ch("Preview", 570.0)), 600_000).isEmpty())
    }

    @Test
    fun withoutADurationTheLastSegmentCannotBeMeasured() {
        val segs = detectSkipSegments(listOf(ch("Opening", 30.0), ch("Episode", 120.0), ch("Ending", 540.0)), 0)
        assertEquals(1, segs.size) // the opening is bounded by the next chapter; the ending is not
        assertEquals(SegmentKind.INTRO, segs[0].kind)
    }

    @Test
    fun anEndingThatStartsPastTheDurationIsDropped() {
        assertTrue(detectSkipSegments(listOf(ch("Episode", 0.0), ch("Ending", 700.0)), 600_000).isEmpty())
    }

    @Test
    fun noChaptersNoSegments() {
        assertTrue(detectSkipSegments(emptyList(), 600_000).isEmpty())
        assertTrue(detectSkipSegments(listOf(ch("Chapter 1", 0.0), ch("Chapter 2", 300.0)), 600_000).isEmpty())
    }

    @Test
    fun chaptersReportedOncePerTrackAreMergedIntoOneList() {
        val video = typicalChapters()
        val raw = video + video.reversed() + listOf(ChapterInfo("Odd one", -500))
        val clean = normalizeChapters(raw)
        assertEquals(video.size + 1, clean.size)
        assertEquals(listOf(0L, 0L, 30_000L, 120_000L, 540_000L, 585_000L), clean.map { it.startMs })
        assertTrue(clean.zipWithNext().all { (a, b) -> a.startMs <= b.startMs })
        assertEquals(clean, normalizeChapters(clean))
    }

    // ------------------------------------------------------------ the button (nothing automatic)

    @Test
    fun theSkipSegmentFollowsThePlayheadAndNothingIsSkippedByDefault() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Button", 2)
        val pc = h.controller

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        assertEquals(2, pc.skipSegments.size)
        assertFalse(pc.activeSegment.valid())

        h.backend.tick(10.0)
        assertFalse(pc.activeSegment.valid())
        assertEquals(0, h.events<PlaybackEvent.SkipSegmentChanged>().size)

        h.backend.tick(31.0)
        assertTrue(pc.activeSegment.valid())
        assertEquals(SegmentKind.INTRO, pc.activeSegment.kind)
        assertEquals(1, h.events<PlaybackEvent.SkipSegmentChanged>().size)
        h.backend.tick(60.0) // still inside: no new event
        assertEquals(1, h.events<PlaybackEvent.SkipSegmentChanged>().size)

        h.backend.tick(121.0)
        assertFalse(pc.activeSegment.valid())
        assertEquals(2, h.events<PlaybackEvent.SkipSegmentChanged>().size)
        assertFalse(h.events<PlaybackEvent.SkipSegmentChanged>().last().segment.valid())

        h.backend.tick(545.0)
        assertEquals(SegmentKind.ENDING, pc.activeSegment.kind)

        assertTrue(h.backend.seeks.isEmpty()) // settings are off: it is only offered
        assertEquals(0, h.events<PlaybackEvent.SegmentSkipped>().size)
    }

    @Test
    fun pressingSkipJumpsToTheEndOfTheOpening() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Press", 1)
        val pc = h.controller

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        pc.skipActiveSegment() // nothing to skip yet
        assertTrue(h.backend.seeks.isEmpty())

        h.backend.tick(40.0)
        pc.skipActiveSegment()
        assertEquals(1, h.backend.seeks.size)
        assertEquals(120_000L to false, h.backend.seeks.last()) // absolute
        val skipped = h.events<PlaybackEvent.SegmentSkipped>()
        assertEquals(1, skipped.size)
        assertFalse(skipped[0].automatic) // by the user
        assertEquals(SegmentKind.INTRO, skipped[0].segment.kind)
        assertFalse(pc.activeSegment.valid()) // the button goes away at once
        assertEquals(120_000L, pc.positionMs)
    }

    @Test
    fun theButtonStaysAvailableWhilePaused() {
        val h = harness()
        val (show, eps) = h.library.makeShow("PausedButton", 1)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(40.0)
        pc.setPaused(true)
        assertTrue(pc.activeSegment.valid())
        pc.skipActiveSegment()
        assertEquals(120_000L to false, h.backend.seeks.last())
        assertTrue(h.backend.isPaused) // an opening is skipped where the user left it: paused
        assertEquals(PlaybackState.PAUSED, pc.state)
    }

    @Test
    fun skippingAnEndingWhilePausedResumesFirstOrTheEngineMightNeverReportTheEnd() {
        val h = harness()
        val (show, eps) = h.library.makeShow("PausedEnding", 2)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(560.0)
        pc.setPaused(true)
        assertTrue(h.backend.isPaused)

        pc.skipActiveSegment()
        assertFalse(h.backend.isPaused) // resumed before the seek
        assertEquals(600_000L to false, h.backend.seeks.last())
        assertEquals(PlaybackState.PLAYING, pc.state)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size) // still the engine's call

        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.ItemCompleted>().size)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
    }

    // ------------------------------------------------------------ automatic skipping

    @Test
    fun autoSkipOpeningSeeksExactlyOnce() {
        val h = harness()
        val (show, eps) = h.library.makeShow("AutoIntro", 1)
        h.settings.setAutoSkipIntro(true)
        val pc = h.controller

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(29.0)
        assertTrue(h.backend.seeks.isEmpty())
        h.backend.tick(30.5)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(120_000L to false, h.backend.seeks.last())
        val skipped = h.events<PlaybackEvent.SegmentSkipped>()
        assertEquals(1, skipped.size)
        assertTrue(skipped[0].automatic)

        // the engine keeps reporting positions from before the seek landed
        h.backend.tick(30.8)
        h.backend.tick(31.2)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(1, h.events<PlaybackEvent.SegmentSkipped>().size)

        h.backend.tick(120.2) // arrived
        assertEquals(1, h.backend.seeks.size)
        assertFalse(pc.activeSegment.valid())
    }

    @Test
    fun seekingBackIntoASkippedOpeningLeavesItAlone() {
        val h = harness()
        val (show, eps) = h.library.makeShow("SeekBack", 1)
        h.settings.setAutoSkipIntro(true)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(30.5)
        assertEquals(1, h.backend.seeks.size)

        pc.seekToMs(50_000) // the user wants to see the opening after all
        assertEquals(2, h.backend.seeks.size)
        assertEquals(50_000L to false, h.backend.seeks.last())
        h.backend.tick(50.4)
        h.backend.tick(51.0)
        assertEquals(2, h.backend.seeks.size) // nothing skipped it again
        assertTrue(pc.activeSegment.valid()) // but the button is there if they change their mind

        pc.seekByMs(-10_000)
        h.backend.tick(41.0)
        assertEquals(3, h.backend.seeks.size) // only the user's own seek
        assertTrue(h.backend.seeks.last().second)
    }

    @Test
    fun seekingIntoAnOpeningOnPurposeIsNotAutoSkipped() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Purpose", 1)
        h.settings.setAutoSkipIntro(true)
        h.settings.setAutoSkipEnding(true)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))

        pc.seekToMs(60_000) // jumps straight into the opening, before playback ever reached it
        h.backend.tick(60.5)
        assertEquals(1, h.backend.seeks.size)
        assertTrue(pc.activeSegment.valid())

        pc.seekToMs(550_000) // and into the ending
        h.backend.tick(550.5)
        assertEquals(2, h.backend.seeks.size)
        assertEquals(550_000L to false, h.backend.seeks.last())
    }

    @Test
    fun autoSkipWaitsForPlaybackNotPause() {
        val h = harness()
        val (show, eps) = h.library.makeShow("NotPaused", 1)
        h.settings.setAutoSkipIntro(true)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))

        h.backend.tick(29.0)
        pc.setPaused(true)
        h.backend.tick(40.0) // e.g. a scrub while paused
        assertTrue(h.backend.seeks.isEmpty())
        assertTrue(pc.activeSegment.valid())

        pc.setPaused(false)
        h.backend.tick(40.3)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(120_000L to false, h.backend.seeks.last())
    }

    @Test
    fun eachSettingOnlyAffectsItsOwnKindOfSegment() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Separate", 1)
        val pc = h.controller

        h.settings.setAutoSkipEnding(true) // opening stays manual
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(40.0)
        assertTrue(h.backend.seeks.isEmpty())
        assertTrue(pc.activeSegment.valid())

        h.settings.setAutoSkipEnding(false)
        h.settings.setAutoSkipIntro(true) // now the other way round
        h.backend.tick(41.0) // still inside the opening, never skipped yet: it goes now
        assertEquals(1, h.backend.seeks.size)
        assertEquals(120_000L to false, h.backend.seeks.last())
        h.backend.tick(121.0)
        h.backend.tick(545.0) // the ending stays manual
        assertEquals(1, h.backend.seeks.size)
        assertEquals(SegmentKind.ENDING, pc.activeSegment.kind)
    }

    @Test
    fun bothSettingsAreOffByDefault() {
        val h = harness()
        assertFalse(h.settings.autoSkipIntro)
        assertFalse(h.settings.autoSkipEnding)
        val (show, eps) = h.library.makeShow("Defaults", 1)
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        for (t in listOf(35.0, 100.0, 545.0, 590.0)) h.backend.tick(t)
        assertTrue(h.backend.seeks.isEmpty())
    }

    @Test
    fun anOpeningAtTheVeryStartIsSkippedAsSoonAsThePlaybackStarts() {
        val h = harness()
        val (show, eps) = h.library.makeShow("ColdStart", 1)
        h.settings.setAutoSkipIntro(true)
        assertTrue(h.startWith(show, eps[0], listOf(ch("OP", 0.0), ch("Episode", 90.0))))
        assertEquals(1, h.backend.seeks.size)
        assertEquals(90_000L to false, h.backend.seeks.last())
    }

    @Test
    fun theSkipIsAnnouncedAfterTheUiKnowsWhatIsPlaying() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Order", 1)
        h.settings.setAutoSkipIntro(true)
        assertTrue(h.startWith(show, eps[0], listOf(ch("OP", 0.0), ch("Episode", 90.0))))
        val nowPlaying = h.events.indexOfFirst { it is PlaybackEvent.NowPlaying }
        val skipped = h.events.indexOfFirst { it is PlaybackEvent.SegmentSkipped }
        assertTrue(nowPlaying >= 0 && skipped > nowPlaying)
    }

    @Test
    fun resumingInsideAnOpeningSkipsTheRestOfIt() {
        val h = harness()
        val (show, eps) = h.library.makeShow("ResumeInside", 1)
        h.settings.setAutoSkipIntro(true)
        h.library.saveProgress(eps[0].file.id, 45_000, 600_000)
        assertTrue(h.startWith(show, eps[0], typicalChapters(), mode = StartMode.RESUME))
        assertEquals(45_000L, h.backend.lastLoad.startMs)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(120_000L to false, h.backend.seeks.last())
    }

    @Test
    fun aNewEpisodeGetsItsOwnSkips() {
        val h = harness()
        val (show, eps) = h.library.makeShow("EveryEpisode", 2)
        h.settings.setAutoSkipIntro(true)
        val pc = h.controller

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(31.0)
        assertEquals(1, h.backend.seeks.size)

        h.backend.end(EndReason.EOF)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertTrue(pc.skipSegments.isEmpty()) // the old file's segments are gone until the new file says otherwise
        assertFalse(pc.activeSegment.valid())
        h.loadFinished()
        assertEquals(2, pc.skipSegments.size)
        h.backend.tick(31.0)
        assertEquals(2, h.backend.seeks.size) // skipped again in the second episode
    }

    @Test
    fun filesWithoutChaptersOfferNothing() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Plain", 1)
        h.settings.setAutoSkipIntro(true)
        h.settings.setAutoSkipEnding(true)
        assertTrue(h.startWith(show, eps[0], emptyList()))
        for (t in listOf(5.0, 40.0, 300.0, 590.0)) h.backend.tick(t)
        assertTrue(h.controller.skipSegments.isEmpty())
        assertFalse(h.controller.activeSegment.valid())
        assertTrue(h.backend.seeks.isEmpty())
    }

    @Test
    fun chaptersThatAppearAfterTheLoadAreUsed() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Late", 1)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], emptyList()))
        assertTrue(pc.skipSegments.isEmpty())
        h.backend.tick(40.0)
        h.backend.setChapters(typicalChapters()) // the engine finished reading them while playing
        assertEquals(2, pc.skipSegments.size)
        assertEquals(SegmentKind.INTRO, pc.activeSegment.kind)
    }

    @Test
    fun chaptersReportedAgainDoNotRearmASkippedSegment() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Replay", 1)
        h.settings.setAutoSkipIntro(true)
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(30.5)
        assertEquals(1, h.backend.seeks.size)

        h.backend.setChapters(typicalChapters()) // e.g. the engine re-opened the item to add a subtitle
        h.backend.tick(31.0) // a position from before the seek landed
        assertEquals(1, h.backend.seeks.size)
    }

    @Test
    fun chaptersOfAFileThatIsNoLongerPlayingAreIgnored() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Stale", 1)
        h.backend.setChapters(typicalChapters()) // nothing is loaded yet
        assertTrue(h.controller.skipSegments.isEmpty())
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.controller.stop()
        h.backend.setChapters(typicalChapters()) // a late report after the stop
        assertTrue(h.controller.skipSegments.isEmpty())
        assertFalse(h.controller.activeSegment.valid())
    }

    @Test
    fun theEndOfALastSegmentFollowsTheDuration() {
        val h = harness()
        val (show, eps) = h.library.makeShow("Duration", 1)
        assertTrue(h.startWith(show, eps[0], typicalChapters(), durationMs = 600_000))
        assertEquals(600_000L, h.controller.skipSegments.last().endMs)
        h.backend.listener?.onDurationChanged(660_000)
        assertEquals(660_000L, h.controller.skipSegments.last().endMs)
    }

    // ------------------------------------------------------------ endings and the end of the file

    @Test
    fun skippingAnEndingThatRunsToTheEndFinishesTheEpisodeOnlyThroughEndOfFile() {
        val h = harness()
        val (show, eps) = h.library.makeShow("EndingSkip", 2)
        h.settings.setAutoSkipEnding(true)
        val pc = h.controller

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(541.0)

        assertEquals(1, h.events<PlaybackEvent.SegmentSkipped>().size)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(600_000L to false, h.backend.seeks.last()) // the very end of the file
        // the skip itself completes nothing and starts nothing...
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
        assertEquals(1, h.backend.loads.size)
        assertEquals(PlaybackState.PLAYING, pc.state)

        // ...that is the engine's job: it reports the natural end, and the usual path takes over
        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.ItemCompleted>().size)
        assertTrue(h.library.progress(eps[0].file.id).completed)
        h.scheduler.settle()
        assertEquals(2, h.backend.loads.size)
        assertEquals(eps[1].file.id, pc.currentEntry.mediaFileId)
    }

    @Test
    fun aManualEndingSkipAlsoGoesThroughEndOfFile() {
        val h = harness()
        val (show, eps) = h.library.makeShow("ManualEnding", 2)
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(560.0)
        h.controller.skipActiveSegment()
        assertEquals(600_000L to false, h.backend.seeks.last())
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.ItemCompleted>().size)
    }

    @Test
    fun skippingTheEndingOfTheLastEpisodeFinishesTheQueue() {
        val h = harness()
        val (show, eps) = h.library.makeShow("LastEnding", 2)
        h.settings.setAutoSkipEnding(true)

        assertTrue(h.startWith(show, eps[1], typicalChapters()))
        h.backend.tick(541.0)
        assertEquals(0, h.events<PlaybackEvent.QueueFinished>().size) // still waiting for the engine
        h.backend.end(EndReason.EOF)
        assertEquals(1, h.events<PlaybackEvent.QueueFinished>().size)
        assertEquals(0, h.events<PlaybackEvent.AutoAdvancePending>().size)
        assertEquals(PlaybackState.IDLE, h.controller.state)
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size)
    }

    @Test
    fun skippingTheEndingWithAutoPlayOffDoesNotAdvance() {
        val h = harness()
        val (show, eps) = h.library.makeShow("NoAutoplay", 2)
        h.settings.setAutoSkipEnding(true)
        h.settings.setAutoPlayNext(false)

        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(541.0)
        h.backend.end(EndReason.EOF)
        val ended = h.events<PlaybackEvent.EndedWithoutAdvance>()
        assertEquals(1, ended.size)
        assertEquals(eps[1].file.id, ended[0].next.mediaFileId) // offered, not started
        h.scheduler.advance(100)
        assertEquals(1, h.backend.loads.size)
    }

    @Test
    fun anEndingFollowedByAPostCreditsSceneJumpsOverItAndKeepsPlaying() {
        val h = harness()
        val (show, eps) = h.library.makeShow("PostCredits", 2)
        h.settings.setAutoSkipEnding(true)
        val chapters = listOf(ch("Episode", 0.0), ch("Ending", 500.0), ch("Post-Credits Scene", 570.0))

        assertTrue(h.startWith(show, eps[0], chapters))
        h.backend.tick(501.0)
        assertEquals(1, h.backend.seeks.size)
        assertEquals(570_000L to false, h.backend.seeks.last()) // to the scene, not to the end
        h.backend.tick(570.2)
        assertEquals(PlaybackState.PLAYING, h.controller.state)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertEquals(1, h.backend.loads.size)
    }

    @Test
    fun aSkippedEndingThenAStopNeverCompletesAnything() {
        val h = harness()
        val (show, eps) = h.library.makeShow("StopAfterSkip", 2)
        h.settings.setAutoSkipEnding(true)
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.tick(541.0) // skip to the end requested...
        h.controller.stop() // ...but the user stops before the engine reports anything
        h.backend.end(EndReason.STOP)
        h.backend.end(EndReason.EOF) // stale
        h.scheduler.advance(100)
        assertEquals(0, h.events<PlaybackEvent.ItemCompleted>().size)
        assertFalse(h.library.progress(eps[0].file.id).completed)
        assertEquals(1, h.backend.loads.size)
    }

    @Test
    fun nothingIsOfferedOrSkippedDuringTheNextEpisodeCountdown() {
        val h = harness(countdown = 5)
        val (show, eps) = h.library.makeShow("Countdown", 2)
        h.settings.setAutoSkipIntro(true)
        h.settings.setAutoSkipEnding(true)
        val pc = h.controller
        assertTrue(h.startWith(show, eps[0], typicalChapters()))
        h.backend.end(EndReason.EOF)
        assertEquals(PlaybackState.ADVANCE_PENDING, pc.state)
        assertFalse(pc.activeSegment.valid())
        val seeks = h.backend.seeks.size
        h.backend.tick(35.0) // a late position event of the finished file
        pc.skipActiveSegment()
        assertEquals(seeks, h.backend.seeks.size)
        assertEquals(PlaybackState.ADVANCE_PENDING, pc.state)
    }
}
