package io.github.victorhmrod.localflix.app

import android.net.Uri
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.MediaFile
import io.github.victorhmrod.localflix.core.MissingRef
import io.github.victorhmrod.localflix.core.PlanOptions
import io.github.victorhmrod.localflix.core.RelinkMatcher
import io.github.victorhmrod.localflix.data.ImportReport
import io.github.victorhmrod.localflix.data.LibraryRepository
import io.github.victorhmrod.localflix.files.DocumentAccess
import io.github.victorhmrod.localflix.files.ScanResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.withContext

/** What the user picked: a whole folder, or individual files. */
sealed interface PickedSource {
    data class Folder(val uri: Uri) : PickedSource

    data class Files(val uris: List<Uri>) : PickedSource
}

class ReconnectReport(
    val scanned: Int,
    val reconnected: Int,
    val stillMissing: Int,
    val problems: List<String>,
)

/** Scans what the user granted and feeds the library. Everything here runs off the main thread. */
class ImportService(private val library: LibraryRepository, private val docs: DocumentAccess) {
    private suspend fun scan(source: PickedSource, onProgress: (Int) -> Unit): ScanResult = withContext(Dispatchers.IO) {
        val job = currentCoroutineContext()[Job]
        val cancelled = { job?.isActive == false }
        when (source) {
            is PickedSource.Folder -> {
                docs.takePersistableRead(source.uri)
                docs.scanTree(source.uri, onProgress, cancelled)
            }
            is PickedSource.Files -> {
                source.uris.forEach { docs.takePersistableRead(it) }
                docs.scanDocuments(source.uris, cancelled)
            }
        }
    }

    suspend fun importEpisodes(
        showId: Id,
        fallbackSeasonId: Id,
        source: PickedSource,
        options: PlanOptions = PlanOptions(),
        onProgress: (Int) -> Unit = {},
    ): ImportReport {
        val result = scan(source, onProgress)
        if (result.cancelled) return ImportReport(ok = false, error = "Import cancelled.")
        return withContext(Dispatchers.IO) {
            library.importEpisodes(showId, fallbackSeasonId, ImportPlanner.planEpisodes(result.files, options), result.rejected)
        }
    }

    suspend fun importMovies(source: PickedSource, onProgress: (Int) -> Unit = {}): ImportReport {
        val result = scan(source, onProgress)
        if (result.cancelled) return ImportReport(ok = false, error = "Import cancelled.")
        return withContext(Dispatchers.IO) { library.importMovies(ImportPlanner.planMovies(result.files), result.rejected) }
    }

    /**
     * The user granted a folder to find moved or no-longer-accessible files in. Entries that are missing are matched
     * to the folder's files by name and path; nothing is guessed, ambiguous ones stay missing.
     */
    suspend fun reconnectFolder(treeUri: Uri, onProgress: (Int) -> Unit = {}): ReconnectReport {
        val result = scan(PickedSource.Folder(treeUri), onProgress)
        return withContext(Dispatchers.IO) {
            val all = library.allMediaFiles()
            val missing = all.filter { it.missing }.map { it.toRef() }
            val proposals = RelinkMatcher.propose(missing, result.files, all.map { it.docKey }.toSet())
            val done = library.applyRelinks(proposals)
            ReconnectReport(result.files.size, done, library.missingFiles().size, result.rejected)
        }
    }

    /** Re-checks that files can still be opened; entries that vanished are flagged, ones that came back are cleared. */
    suspend fun verify(files: List<MediaFile>): Pair<Int, Int> = withContext(Dispatchers.IO) {
        library.applyVerification(files.map { it to !docs.exists(it.uri) })
    }

    suspend fun verifyAll() = verify(withContext(Dispatchers.IO) { library.allMediaFiles() })

    suspend fun verifyShow(showId: Id) = verify(withContext(Dispatchers.IO) { library.mediaFilesOfShow(showId) })

    private fun MediaFile.toRef() = MissingRef(id, docKey, relativePath, displayName, sizeBytes)
}
