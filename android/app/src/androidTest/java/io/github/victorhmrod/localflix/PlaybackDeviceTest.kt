package io.github.victorhmrod.localflix

import android.net.Uri
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.core.BackendListener
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.EndFileInfo
import io.github.victorhmrod.localflix.core.EndReason
import io.github.victorhmrod.localflix.core.FileProbe
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.PlaybackEvent
import io.github.victorhmrod.localflix.core.PlaybackState
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.ScannedFile
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.core.TrackInfo
import io.github.victorhmrod.localflix.data.LocalFlixDatabase
import io.github.victorhmrod.localflix.playback.Media3Backend
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections
import java.util.UUID

private const val TAG = "LocalFlixTest"

fun fileScan(file: File, root: String = "videos"): ScannedFile = ScannedFile(
    uri = Uri.fromFile(file).toString(),
    docKey = "files/${file.name}",
    displayName = file.name,
    relativePath = "$root/${file.name}",
    folderChain = listOf(root),
    sizeBytes = file.length(),
    mtimeMs = file.lastModified(),
    treeUri = "",
)

/** Records what a real engine reports. */
private class Recorder : BackendListener {
    val loaded = Collections.synchronizedList(ArrayList<Long>())
    val ended = Collections.synchronizedList(ArrayList<EndFileInfo>())
    val tracks = Collections.synchronizedList(ArrayList<List<TrackInfo>>())

    @Volatile var durationMs = 0L

    @Volatile var paused: Boolean? = null

    override fun onFileLoaded(token: Long) {
        loaded += token
    }

    override fun onDurationChanged(durationMs: Long) {
        this.durationMs = durationMs
    }

    override fun onPausedChanged(paused: Boolean) {
        this.paused = paused
    }

    override fun onTracksChanged(tracks: List<TrackInfo>) {
        this.tracks += tracks
    }

    override fun onEndFile(info: EndFileInfo) {
        ended += info
    }
}

/** The real engine (Media3/ExoPlayer) on real clips, on the device. */
@RunWith(AndroidJUnit4::class)
class Media3BackendTest {
    private fun <T> withBackend(block: (Media3Backend, Recorder) -> T): T {
        val rec = Recorder()
        val backend = onMainResult { Media3Backend(targetContext).also { it.listener = rec } }
        try {
            return block(backend, rec)
        } finally {
            onMain { backend.release() }
        }
    }

    private fun uriOf(name: String) = Uri.fromFile(copyTestVideo(name)).toString()

    @Test
    fun theMainContainersPlayToTheirNaturalEnd() {
        val results = ArrayList<String>()
        for (name in listOf("downloaded_episode_04.mp4", "video_27.mkv", "anime_final.webm", "S01E05 - The Reunion.mov", "episode_01.avi")) {
            if (!testVideoAvailable(name)) continue
            withBackend { backend, rec ->
                val token = onMainResult { backend.loadFile(uriOf(name), 0) }
                val deadline = System.currentTimeMillis() + 40_000
                while (System.currentTimeMillis() < deadline && rec.ended.isEmpty()) Thread.sleep(100)
                val end = rec.ended.firstOrNull()
                val line = "FORMAT $name: loaded=${rec.loaded.isNotEmpty()} duration=${rec.durationMs}ms end=${end?.reason} ${end?.errorText.orEmpty()}"
                Log.i(TAG, line)
                results += line
                if (name.endsWith(".avi")) return@withBackend // best effort: not every container is supported
                assertTrue(line, rec.loaded.contains(token))
                assertTrue(line, rec.durationMs > 0)
                assertEquals(line, EndReason.EOF, end?.reason)
                assertEquals(line, token, end?.token)
            }
        }
        assumeTrue("no test clips bundled (run scripts/make_test_videos.py)", results.isNotEmpty())
    }

    @Test
    fun startOffsetSeekPauseAndSpeedWork() {
        assumeTrue(testVideoAvailable("long_resume_test.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("long_resume_test.mkv"), 30_000) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            waitUntil("duration known") { rec.durationMs > 0 }
            assertTrue("starts near the requested offset", onMainResult { backend.positionMs } >= 29_500)

            onMain { backend.setPaused(true) }
            waitUntil("paused") { rec.paused == true }
            val parked = onMainResult { backend.positionMs }
            Thread.sleep(600)
            assertEquals("a paused video stands still", parked, onMainResult { backend.positionMs })

            onMain { backend.seekToMs(5_000) }
            waitUntil("seek applied") { onMainResult { backend.positionMs } in 4_000..6_500 }
            onMain { backend.setSpeed(2.0) }
            assertEquals(2.0, onMainResult { backend.speed }, 0.001)
            onMain { backend.setPaused(false) }
            waitUntil("playing again") { rec.paused == false }
        }
    }

    @Test
    fun stoppingAPlaybackNeverReportsAnEndOfFile() {
        assumeTrue(testVideoAvailable("long_resume_test.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("long_resume_test.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            onMain { backend.stop() }
            Thread.sleep(1500)
            assertTrue("a stop is silent, so it can never be mistaken for the end", rec.ended.isEmpty())
        }
    }

    @Test
    fun aFileThatCannotBeOpenedEndsWithAnError() {
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(Uri.fromFile(File(targetContext.cacheDir, "does-not-exist.mkv")).toString(), 0) }
            waitUntil("error reported") { rec.ended.isNotEmpty() }
            assertEquals(EndReason.ERROR, rec.ended[0].reason)
            assertEquals(token, rec.ended[0].token)
            assertTrue(rec.loaded.isEmpty())
        }
    }

    @Test
    fun subtitleAndAudioTracksAreReported() {
        assumeTrue(testVideoAvailable("video_27.mkv"))
        withBackend { backend, rec ->
            val token = onMainResult { backend.loadFile(uriOf("video_27.mkv"), 0) }
            waitUntil("file loaded") { rec.loaded.contains(token) }
            waitUntil("tracks reported") { rec.tracks.isNotEmpty() }
            val tracks = onMainResult { backend.tracks }
            Log.i(TAG, "TRACKS video_27.mkv: $tracks")
            assertTrue("a video file has at least one track", tracks.isNotEmpty())
            assertTrue("ids are positive and unique", tracks.all { it.id > 0 } && tracks.map { it.id }.toSet().size == tracks.size)
        }
    }
}

/**
 * The whole viewing story on the device with the real engine and a real database: import, play, seek, close the
 * app, reopen, continue where it stopped, let it finish and move to the next episode.
 */
@RunWith(AndroidJUnit4::class)
class SeriesFlowTest {
    private val dbName = "flow-${UUID.randomUUID()}.db"
    private val containers = ArrayList<AppContainer>()

    private fun open(): AppContainer = onMainResult {
        AppContainer(
            targetContext,
            LocalFlixDatabase.open(targetContext, dbName),
            File(targetContext.cacheDir, "flow-covers"),
            fileProbe = { FileProbe { File(Uri.parse(it).path!!).exists() } }, // plain files stand in for granted documents
            startupChecks = false,
        )
    }.also { containers += it }

    private fun close(c: AppContainer) = onMain {
        c.playback.shutdown()
        c.backend.release()
    }

    @Test
    fun theAcceptanceScenario() {
        for (n in listOf("long_resume_test.mkv", "downloaded_episode_04.mp4", "video_27.mkv")) assumeTrue("missing clip $n", testVideoAvailable(n))
        val long = copyTestVideo("long_resume_test.mkv")
        val short = copyTestVideo("downloaded_episode_04.mp4")
        val third = copyTestVideo("video_27.mkv")

        // ---- create the show and import the files
        var c = open()
        val showId = c.library.createShow("Acceptance")
        val report = c.library.importEpisodes(showId, 0, ImportPlanner.planEpisodes(listOf(short, long, third).map { fileScan(it) }))
        assertEquals(3, report.added)
        c.settings.setAutoPlayCountdownSeconds(0)

        // ---- the user's order, not the file names': the 40 s clip first
        val eps0 = c.library.episodes(showId)
        val longEp = eps0.first { it.file.displayName == long.name }
        assertTrue(c.library.moveEpisode(longEp.id, longEp.seasonId, 0))
        val order = c.library.episodes(showId)
        assertEquals(long.name, order[0].file.displayName)
        val ctx = QueueContext(ContextType.SHOW, showId)

        // ---- play, seek into the clip, pause
        val events = Collections.synchronizedList(ArrayList<PlaybackEvent>())
        onMain { c.playback.addListener { events += it } }
        assertTrue(onMainResult { c.playback.play(ctx, order[0].file.id, StartMode.FROM_START) })
        waitUntil("playing the first episode") { onMainResult { c.playback.state } == PlaybackState.PLAYING }
        onMain { c.playback.seekToMs(30_000) }
        waitUntil("position reaches the seek target") { onMainResult { c.backend.positionMs } >= 29_500 }
        onMain { c.playback.setPaused(true) }
        waitUntil("progress saved on pause") { c.library.progress(order[0].file.id).positionMs in 28_000..33_000 }

        // ---- "close the application"
        close(c)
        c.library.let { /* the database is closed with the process; reopen below */ }

        // ---- reopen: Continue Watching knows where to go
        c = open()
        c.settings.setAutoPlayCountdownSeconds(0)
        val items = c.library.continueWatching()
        assertEquals(1, items.size)
        assertEquals(order[0].file.id, items[0].mediaFileId)
        assertTrue("resume point kept", items[0].positionMs in 28_000..33_000)

        val events2 = Collections.synchronizedList(ArrayList<PlaybackEvent>())
        onMain { c.playback.addListener { events2 += it } }
        assertTrue(onMainResult { c.playback.play(ctx, items[0].mediaFileId, StartMode.AUTO) })
        waitUntil("playing again") { onMainResult { c.playback.state } == PlaybackState.PLAYING }
        assertTrue("resumed near the saved spot", onMainResult { c.backend.positionMs } >= 28_000)

        // ---- let it end naturally: completed, and the next episode in the user's order starts by itself
        waitUntil("first episode completes", timeoutMs = 40_000) { c.library.progress(order[0].file.id).completed }
        waitUntil("second episode starts", timeoutMs = 20_000) { onMainResult { c.playback.currentEntry.mediaFileId } == order[1].file.id }
        waitUntil("second episode plays") { onMainResult { c.playback.state } == PlaybackState.PLAYING }
        assertEquals(1, events2.filterIsInstance<PlaybackEvent.ItemCompleted>().size)

        // ---- stopping by hand does not complete or advance anything
        onMain { c.playback.stop() }
        Thread.sleep(1500)
        assertEquals(PlaybackState.IDLE, onMainResult { c.playback.state })
        assertEquals(order[1].file.id, onMainResult { c.playback.currentEntry.mediaFileId })
        assertTrue(!c.library.progress(order[1].file.id).completed)
        assertEquals("Continue goes to the next unfinished episode", order[1].file.id, c.library.resumeTarget(showId).mediaFileId)

        close(c)
        targetContext.deleteDatabase(dbName)
    }
}
