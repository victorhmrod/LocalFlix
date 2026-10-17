package io.github.victorhmrod.localflix

import android.content.Context
import androidx.test.platform.app.InstrumentationRegistry
import io.github.victorhmrod.localflix.core.Clock
import io.github.victorhmrod.localflix.core.Episode
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.ScannedFile
import io.github.victorhmrod.localflix.data.LibraryListener
import io.github.victorhmrod.localflix.data.LibraryRepository
import io.github.victorhmrod.localflix.data.LocalFlixDatabase
import java.io.File
import java.io.FileOutputStream
import java.util.UUID

val targetContext: Context get() = InstrumentationRegistry.getInstrumentation().targetContext
val testContext: Context get() = InstrumentationRegistry.getInstrumentation().context

/** Every read is a second later than the one before, so "most recently watched" is never ambiguous. */
class TickingClock(var now: Long = 1_700_000_000_000L) : Clock {
    override fun nowMs(): Long = now.also { now += 1000 }
}

fun scannedFile(
    relative: String,
    root: String = "Library",
    size: Long = 1000,
    uri: String = "content://test/$root/$relative",
    docKey: String = "test/$root/$relative",
): ScannedFile {
    val parts = relative.split('/')
    return ScannedFile(
        uri = uri, docKey = docKey, displayName = parts.last(), relativePath = relative,
        folderChain = parts.dropLast(1).reversed() + root, sizeBytes = size, mtimeMs = 0, treeUri = "content://test/tree/$root",
    )
}

/** A library on a throw-away database, with the errors it reports collected. */
class RepoEnv(inMemory: Boolean = true, val dbName: String? = if (inMemory) null else "test-${UUID.randomUUID()}.db") {
    val clock = TickingClock()
    val covers = File(targetContext.cacheDir, "covers-${UUID.randomUUID()}").also { it.mkdirs() }
    var db = LocalFlixDatabase.open(targetContext, dbName)
    var repo = LibraryRepository(db, clock, covers)
    val errors = ArrayList<String>()
    private val listener = object : LibraryListener {
        override fun onError(message: String) {
            errors += message
        }
    }

    init {
        repo.addListener(listener)
    }

    /** Close and open the same database file again (an app restart). */
    fun restart() {
        check(dbName != null) { "an in-memory database cannot be reopened" }
        db.close()
        db = LocalFlixDatabase.open(targetContext, dbName)
        repo = LibraryRepository(db, clock, covers)
        repo.addListener(listener)
    }

    /** A show with [count] imported episodes ep01.mp4, ep02.mp4, ... in its first season. */
    fun makeShow(title: String, count: Int): Pair<Id, List<Episode>> {
        val showId = repo.createShow(title)
        val files = (1..count).map { scannedFile("$title/ep%02d.mp4".format(it)) }
        val report = repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(files))
        check(report.ok && report.added == count) { "import failed: ${report.error}" }
        return showId to repo.episodes(showId)
    }

    fun close() {
        db.close()
        dbName?.let { targetContext.deleteDatabase(it) }
        covers.deleteRecursively()
    }
}

/** Copies a bundled test clip into the app's cache and returns the file. */
fun copyTestVideo(name: String, into: File = File(targetContext.cacheDir, "videos")): File {
    into.mkdirs()
    val out = File(into, name)
    if (!out.exists() || out.length() == 0L) {
        testContext.assets.open(name).use { input -> FileOutputStream(out).use { input.copyTo(it) } }
    }
    return out
}

/** Like [copyTestVideo], but under another file name (an episode needs a file of its own). */
fun copyTestVideoAs(name: String, asName: String, into: File): File {
    into.mkdirs()
    val out = File(into, asName)
    if (!out.exists() || out.length() == 0L) {
        testContext.assets.open(name).use { input -> FileOutputStream(out).use { input.copyTo(it) } }
    }
    return out
}

fun testVideoAvailable(name: String): Boolean = try {
    testContext.assets.open(name).close()
    true
} catch (_: java.io.IOException) {
    false
}

/** Polls (from the test thread) until [condition] holds, failing with [what] after [timeoutMs]. */
fun waitUntil(what: String, timeoutMs: Long = 15_000, condition: () -> Boolean) {
    val end = System.currentTimeMillis() + timeoutMs
    while (System.currentTimeMillis() < end) {
        if (condition()) return
        Thread.sleep(50)
    }
    throw AssertionError("Timed out after $timeoutMs ms waiting for: $what")
}

fun onMain(block: () -> Unit) = InstrumentationRegistry.getInstrumentation().runOnMainSync(block)

fun <T> onMainResult(block: () -> T): T {
    var result: Any? = null
    InstrumentationRegistry.getInstrumentation().runOnMainSync { result = block() }
    @Suppress("UNCHECKED_CAST")
    return result as T
}
