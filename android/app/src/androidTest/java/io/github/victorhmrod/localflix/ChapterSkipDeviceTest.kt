package io.github.victorhmrod.localflix

import android.net.Uri
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.core.BackendListener
import io.github.victorhmrod.localflix.core.ChapterInfo
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.EndFileInfo
import io.github.victorhmrod.localflix.core.EndReason
import io.github.victorhmrod.localflix.core.FileProbe
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.PlaybackEvent
import io.github.victorhmrod.localflix.core.PlaybackState
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.SegmentKind
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.core.detectSkipSegments
import io.github.victorhmrod.localflix.data.LocalFlixDatabase
import io.github.victorhmrod.localflix.playback.Media3Backend
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections
import java.util.UUID

private const val TAG = "LocalFlixTest"

private class ChapterRecorder : BackendListener {
    lateinit var backend: Media3Backend
    val loaded = Collections.synchronizedList(ArrayList<Long>())
    val ended = Collections.synchronizedList(ArrayList<EndFileInfo>())

    /** What `backend.chapters` held at the moment the listener was told the file is open. */
    val chaptersAtLoad = Collections.synchronizedList(ArrayList<List<ChapterInfo>>())
    val chapterReports = Collections.synchronizedList(ArrayList<List<ChapterInfo>>())

    @Volatile var durationMs = 0L

    override fun onFileLoaded(token: Long) {
        chaptersAtLoad += backend.chapters
        loaded += token
    }

    override fun onChaptersChanged(chapters: List<ChapterInfo>) {
        chapterReports += chapters
    }

    override fun onDurationChanged(durationMs: Long) {
        this.durationMs = durationMs
    }

    override fun onEndFile(info: EndFileInfo) {
        ended += info
    }
}

/**
 * What the real engine (Media3/ExoPlayer) tells us about chapters and about seeking to the very end, on clips made
 * by android/scripts/make_chaptered_clips.py.
 */
@RunWith(AndroidJUnit4::class)
class Media3ChaptersTest {
    private fun <T> withBackend(block: (Media3Backend, ChapterRecorder) -> T): T {
        val rec = ChapterRecorder()
        val backend = onMainResult { Media3Backend(targetContext).also { rec.backend = it; it.listener = rec } }
        try {
            return block(backend, rec)
        } finally {
            onMain { backend.release() }
        }
    }

    private fun uriOf(name: String) = Uri.fromFile(copyTestVideo(name)).toString()

    private fun checkChaptersAtLoad(name: String) {
        assumeTrue("missing clip $name (run android/scripts/make_chaptered_clips.py)", testVideoAvailable(name))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf(name), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            waitUntil("duration known") { rec.durationMs > 0 }

            // known by the time the listener hears the file is open, and listed once (not once per track)
            val atLoad = rec.chaptersAtLoad.single()
            Log.i(TAG, "CHAPTERS $name: $atLoad duration=${rec.durationMs}ms")
            assertEquals(listOf("Opening", "Episode", "Ending"), atLoad.map { it.title })
            assertEquals(listOf(0L, 5_000L, 10_000L), atLoad.map { it.startMs })
            assertEquals(atLoad, onMainResult { backend.chapters })

            val segments = detectSkipSegments(atLoad, rec.durationMs)
            assertEquals(listOf(SegmentKind.INTRO, SegmentKind.ENDING), segments.map { it.kind })
            assertEquals(0L to 5_000L, segments[0].startMs to segments[0].endMs)
            assertFalse(segments[0].toEnd)
            assertEquals(10_000L, segments[1].startMs)
            assertTrue("the ending runs to the end of the file", segments[1].toEnd)
            assertEquals(rec.durationMs, segments[1].endMs)
        }
    }

    @Test
    fun matroskaChaptersAreKnownWhenTheFileIsReportedAsLoaded() = checkChaptersAtLoad("chaptered_episode.mkv")

    @Test
    fun mp4ChaptersAreKnownWhenTheFileIsReportedAsLoaded() = checkChaptersAtLoad("chaptered_episode.mp4")

    @Test
    fun aFileWithoutChaptersReportsNone() {
        assumeTrue(testVideoAvailable("video_27.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("video_27.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            assertTrue(rec.chaptersAtLoad.single().isEmpty())
            assertTrue(onMainResult { backend.chapters }.isEmpty())
        }
    }

    @Test
    fun chaptersAreForgottenWhenThePlaybackStopsAndForANewFile() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        assumeTrue(testVideoAvailable("video_27.mkv"))
        withBackend { backend, rec ->
            val t1 = onMainResult { backend.loadFile(uriOf("chaptered_episode.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(t1) }
            assertEquals(3, onMainResult { backend.chapters }.size)
            onMain { backend.stop() }
            assertTrue(onMainResult { backend.chapters }.isEmpty())

            val t2 = onMainResult { backend.loadFile(uriOf("chaptered_episode.mkv"), 0) }
            assertTrue("a new load starts without the previous chapters", onMainResult { backend.chapters }.isEmpty())
            waitUntil("file loaded again") { rec.loaded.contains(t2) }
            val t3 = onMainResult { backend.loadFile(uriOf("video_27.mkv"), 0) }
            waitUntil("plain file loaded") { rec.loaded.contains(t3) }
            assertTrue("the next file does not inherit them", rec.chaptersAtLoad.last().isEmpty())
        }
    }

    @Test
    fun seekingToTheDurationWhilePlayingReportsTheNaturalEnd() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("chaptered_episode.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            waitUntil("duration known") { rec.durationMs > 0 }
            val duration = onMainResult { backend.durationMs }
            onMain { backend.seekToMs(duration) }
            waitUntil("end of file reported", 10_000) { rec.ended.isNotEmpty() }
            assertEquals(EndReason.EOF, rec.ended[0].reason)
            assertEquals(token, rec.ended[0].token)
        }
    }

    /**
     * The raw behaviour of a paused player seeked to the end is only recorded (see the log line): on desktop libmpv
     * never reports the end then, which is why the controller resumes first. What must hold on every engine is
     * that it does finish once resumed.
     */
    @Test
    fun seekingToTheDurationWhilePausedFinishesOnceResumed() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("chaptered_episode.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            waitUntil("duration known") { rec.durationMs > 0 }
            val duration = onMainResult { backend.durationMs }
            onMain { backend.setPaused(true) }
            onMain { backend.seekToMs(duration) }
            val deadline = System.currentTimeMillis() + 5_000
            while (System.currentTimeMillis() < deadline && rec.ended.isEmpty()) Thread.sleep(100)
            val endedWhilePaused = rec.ended.isNotEmpty()
            Log.i(TAG, "PAUSED SEEK-TO-END: end of file reported while still paused = $endedWhilePaused")
            if (!endedWhilePaused) {
                onMain { backend.setPaused(false) }
                waitUntil("end of file reported after resuming", 10_000) { rec.ended.isNotEmpty() }
            }
            assertEquals(EndReason.EOF, rec.ended[0].reason)
            assertEquals(token, rec.ended[0].token)
        }
    }
}

/** Skipping on the real engine with the whole stack: settings, controller, queue, database. */
@RunWith(AndroidJUnit4::class)
class SkipFlowTest {
    private val dbName = "skip-${UUID.randomUUID()}.db"
    private val containers = ArrayList<AppContainer>()

    private class Seen(val event: PlaybackEvent, val at: Long = SystemClock.uptimeMillis())

    private fun open(): AppContainer = onMainResult {
        AppContainer(
            targetContext,
            LocalFlixDatabase.open(targetContext, dbName),
            File(targetContext.cacheDir, "skip-covers"),
            fileProbe = { FileProbe { File(Uri.parse(it).path!!).exists() } }, // plain files stand in for granted documents
            startupChecks = false,
        )
    }.also { containers += it }

    private fun finish() {
        for (c in containers) onMain { c.playback.shutdown(); c.backend.release() }
        targetContext.deleteDatabase(dbName)
    }

    private fun show(c: AppContainer, title: String, clip: String, count: Int): Pair<Long, List<io.github.victorhmrod.localflix.core.Episode>> {
        val files = (1..count).map { copyTestVideoAs(clip, "ep$it.${clip.substringAfterLast('.')}", File(targetContext.cacheDir, "skipflow/$title")) }
        val showId = c.library.createShow(title)
        val report = c.library.importEpisodes(showId, 0, ImportPlanner.planEpisodes(files.map { fileScan(it) }))
        assertEquals(count, report.added)
        c.settings.setAutoPlayCountdownSeconds(0)
        return showId to c.library.episodes(showId)
    }

    private fun record(c: AppContainer): MutableList<Seen> {
        val seen = Collections.synchronizedList(ArrayList<Seen>())
        onMain { c.playback.addListener { seen += Seen(it) } }
        return seen
    }

    private inline fun <reified T : PlaybackEvent> List<Seen>.countOf(noinline test: (T) -> Boolean = { true }) = toList().count { it.event is T && test(it.event) }

    @Test
    fun openingAndEndingAreSkippedByThemselvesAndTheNextEpisodeFollows() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        try {
            val c = open()
            val (showId, eps) = show(c, "AutoSkip", "chaptered_episode.mkv", 2)
            c.settings.setAutoSkipIntro(true)
            c.settings.setAutoSkipEnding(true)
            val seen = record(c)

            assertTrue(onMainResult { c.playback.play(QueueContext(ContextType.SHOW, showId), eps[0].file.id, StartMode.FROM_START) })
            waitUntil("the opening is skipped by itself", 15_000) {
                seen.countOf<PlaybackEvent.SegmentSkipped> { it.automatic && it.segment.kind == SegmentKind.INTRO } == 1
            }
            waitUntil("the playhead is past the opening", 8_000) { onMainResult { c.backend.positionMs } >= 4_500 }

            waitUntil("the ending is skipped by itself", 25_000) {
                seen.countOf<PlaybackEvent.SegmentSkipped> { it.automatic && it.segment.kind == SegmentKind.ENDING } == 1
            }
            // the skip itself completed nothing: that comes from the engine's end of file
            val skipEnding = seen.toList().indexOfFirst { it.event is PlaybackEvent.SegmentSkipped && it.event.segment.kind == SegmentKind.ENDING }
            assertTrue(seen.toList().take(skipEnding + 1).none { it.event is PlaybackEvent.ItemCompleted })

            waitUntil("the episode completes through end of file", 15_000) { c.library.progress(eps[0].file.id).completed }
            waitUntil("the next episode starts", 15_000) {
                onMainResult { c.playback.currentEntry.mediaFileId } == eps[1].file.id && onMainResult { c.playback.state } == PlaybackState.PLAYING
            }
            assertEquals(1, seen.countOf<PlaybackEvent.ItemCompleted>())
            waitUntil("the second episode's opening is skipped too", 15_000) {
                seen.countOf<PlaybackEvent.SegmentSkipped> { it.segment.kind == SegmentKind.INTRO } == 2
            }
            onMain { c.playback.stop() }
        } finally {
            finish()
        }
    }

    @Test
    fun theSkipButtonStateFollowsThePlayheadAndNothingHappensByItself() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        try {
            val c = open()
            val (showId, eps) = show(c, "Button", "chaptered_episode.mkv", 1)
            val seen = record(c)

            assertTrue(onMainResult { c.playback.play(QueueContext(ContextType.SHOW, showId), eps[0].file.id, StartMode.FROM_START) })
            waitUntil("the opening is offered", 15_000) { onMainResult { c.playbackUi.activeSegment }.kind == SegmentKind.INTRO && onMainResult { c.playbackUi.activeSegment }.valid() }
            assertEquals(0, seen.countOf<PlaybackEvent.SegmentSkipped>())
            onMain { c.playback.skipActiveSegment() }
            waitUntil("the playhead moved past the opening", 8_000) { onMainResult { c.backend.positionMs } >= 4_500 }
            waitUntil("the button went away", 5_000) { !onMainResult { c.playbackUi.activeSegment }.valid() }
            waitUntil("the ending is offered", 15_000) { onMainResult { c.playbackUi.activeSegment }.kind == SegmentKind.ENDING && onMainResult { c.playbackUi.activeSegment }.valid() }
            assertEquals(1, seen.countOf<PlaybackEvent.SegmentSkipped>())
            assertEquals(0, seen.countOf<PlaybackEvent.ItemCompleted>())
            onMain { c.playback.stop() }
        } finally {
            finish()
        }
    }

    @Test
    fun skippingAnEndingWhilePausedStillFinishesTheEpisode() {
        assumeTrue(testVideoAvailable("chaptered_episode.mkv"))
        try {
            val c = open()
            val (showId, eps) = show(c, "PausedEnding", "chaptered_episode.mkv", 1)
            val seen = record(c)

            assertTrue(onMainResult { c.playback.play(QueueContext(ContextType.SHOW, showId), eps[0].file.id, StartMode.FROM_START) })
            waitUntil("playing", 15_000) { onMainResult { c.playback.state } == PlaybackState.PLAYING }
            onMain { c.playback.seekToMs(11_000) }
            waitUntil("the ending is offered", 8_000) { onMainResult { c.playback.activeSegment }.kind == SegmentKind.ENDING && onMainResult { c.playback.activeSegment }.valid() }
            onMain { c.playback.setPaused(true) }
            waitUntil("paused", 5_000) { onMainResult { c.playback.state } == PlaybackState.PAUSED }
            assertEquals(0, seen.countOf<PlaybackEvent.ItemCompleted>())

            onMain { c.playback.skipActiveSegment() }
            waitUntil("the engine reports the end and the episode completes", 15_000) { seen.countOf<PlaybackEvent.ItemCompleted>() == 1 }
            assertTrue(c.library.progress(eps[0].file.id).completed)
        } finally {
            finish()
        }
    }

    @Test
    fun anEndingFollowedByAPostCreditsSceneKeepsPlayingToTheirOwnEnd() {
        assumeTrue(testVideoAvailable("chaptered_postcredits.mkv"))
        try {
            val c = open()
            val (showId, eps) = show(c, "PostCredits", "chaptered_postcredits.mkv", 1)
            c.settings.setAutoSkipEnding(true)
            val seen = record(c)

            assertTrue(onMainResult { c.playback.play(QueueContext(ContextType.SHOW, showId), eps[0].file.id, StartMode.FROM_START) })
            waitUntil("the ending is skipped by itself", 25_000) { seen.countOf<PlaybackEvent.SegmentSkipped>() == 1 }
            val skippedAt = seen.toList().first { it.event is PlaybackEvent.SegmentSkipped }.at
            waitUntil("the playhead landed on the scene", 8_000) { onMainResult { c.backend.positionMs } >= 16_500 }
            assertEquals(PlaybackState.PLAYING, onMainResult { c.playback.state })
            assertEquals(0, seen.countOf<PlaybackEvent.ItemCompleted>())

            waitUntil("the scene plays out and the episode completes", 15_000) { seen.countOf<PlaybackEvent.ItemCompleted>() == 1 }
            val completedAt = seen.toList().first { it.event is PlaybackEvent.ItemCompleted }.at
            assertTrue("the scene (about 3 s) was played, not jumped over: ${completedAt - skippedAt} ms", completedAt - skippedAt >= 1_500)
        } finally {
            finish()
        }
    }
}
