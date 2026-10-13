package io.github.victorhmrod.localflix.ui

import android.net.Uri
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.navigation.NavHostController
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.app.PickedSource
import io.github.victorhmrod.localflix.app.ReconnectReport
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.PlanOptions
import io.github.victorhmrod.localflix.core.PlaybackFailure
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.QueueEntry
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.data.ImportReport
import io.github.victorhmrod.localflix.data.LibraryRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** The system pickers, wrapped so any screen can ask for a file/folder/picture and get the answer in a callback. */
class Pickers(
    val videos: ((List<Uri>) -> Unit) -> Unit,
    val video: ((Uri?) -> Unit) -> Unit,
    val folder: ((Uri?) -> Unit) -> Unit,
    val image: ((Uri?) -> Unit) -> Unit,
    val subtitle: ((Uri?) -> Unit) -> Unit,
)

sealed interface GlobalDialog {
    data class Message(val title: String, val text: String) : GlobalDialog

    /** After an import: what was added and what was skipped, and why. */
    data class ImportSummary(val title: String, val headline: String, val details: List<String>) : GlobalDialog

    data class ResumeChoice(val entry: QueueEntry, val positionMs: Long) : GlobalDialog

    data class PlaybackFailed(val entry: QueueEntry, val failure: PlaybackFailure) : GlobalDialog

    /** After locating one file: the others that are missing may be in the same folder. */
    data class ReconnectOffer(val remaining: Int) : GlobalDialog

    data class ReconnectResult(val report: ReconnectReport) : GlobalDialog
}

/** Flows that cut across screens: playing, importing, locating files, global dialogs and messages. */
class AppActions(
    val container: AppContainer,
    val nav: NavHostController,
    val snackbar: SnackbarHostState,
    private val scope: CoroutineScope,
    val pickers: Pickers,
) {
    var dialog by mutableStateOf<GlobalDialog?>(null)

    /** Set while a long operation runs ("Scanning folder... 120 videos"). */
    var busy by mutableStateOf<String?>(null)
        private set
    private var busyJob: Job? = null

    /** Runs a library write/read on a background thread. */
    fun io(block: LibraryRepository.() -> Unit) {
        scope.launch(Dispatchers.IO) { container.library.block() }
    }

    /** Runs a library call in the background and hands its result to [onResult] on the main thread. */
    fun <T> ioResult(block: LibraryRepository.() -> T, onResult: (T) -> Unit) {
        scope.launch {
            val r = withContext(Dispatchers.IO) { container.library.block() }
            onResult(r)
        }
    }

    fun dismissDialog() {
        dialog = null
    }

    fun message(text: String) {
        scope.launch {
            snackbar.currentSnackbarData?.dismiss()
            snackbar.showSnackbar(text, duration = SnackbarDuration.Short)
        }
    }

    fun cancelBusy() {
        busyJob?.cancel()
    }

    private fun runBusy(initial: String, block: suspend () -> Unit) {
        if (busyJob?.isActive == true) {
            message("Another import is still running.")
            return
        }
        busy = initial
        busyJob = scope.launch {
            try {
                block()
            } finally {
                busy = null
            }
        }
    }

    // ------------------------------------------------------------------ playback

    /** Starts [mediaFileId] in [context] and opens the player. Resume prompts and failures come back as dialogs. */
    fun play(context: QueueContext, mediaFileId: Id, mode: StartMode = StartMode.AUTO) {
        container.playbackUi.reset()
        if (container.playback.play(context, mediaFileId, mode)) openPlayer()
    }

    fun playQueueEntry(mediaFileId: Id, mode: StartMode) {
        container.playbackUi.reset()
        if (container.playback.playEntry(mediaFileId, mode)) openPlayer()
    }

    fun openPlayer() {
        if (nav.currentDestination?.route != Routes.PLAYER) nav.navigate(Routes.PLAYER)
    }

    // ------------------------------------------------------------------ importing

    private fun summarize(title: String, noun: String, r: ImportReport) {
        if (!r.ok) {
            dialog = GlobalDialog.Message(title, r.error.ifEmpty { "The import failed." })
            return
        }
        val headline = when {
            r.added > 0 -> "Added ${r.added} $noun${if (r.added == 1) "" else "s"}."
            r.duplicates.isNotEmpty() || r.rejected.isNotEmpty() -> "Nothing new was added."
            else -> "No video files were found."
        }
        val details = r.duplicates.map { "${it.label} — already in the library as ${it.existingAs}" } + r.rejected
        if (details.isEmpty()) message(headline) else dialog = GlobalDialog.ImportSummary(title, headline, details)
    }

    fun importEpisodes(showId: Id, seasonId: Id, source: PickedSource, options: PlanOptions = PlanOptions()) {
        runBusy(if (source is PickedSource.Folder) "Scanning folder…" else "Reading files…") {
            val report = container.importer.importEpisodes(showId, seasonId, source, options) { n -> busy = "Scanning folder… $n videos found" }
            summarize("Add episodes", "episode", report)
            container.importer.verifyShow(showId)
        }
    }

    fun importMovies(source: PickedSource) {
        runBusy(if (source is PickedSource.Folder) "Scanning folder…" else "Reading files…") {
            val report = container.importer.importMovies(source) { n -> busy = "Scanning folder… $n videos found" }
            summarize("Add movies", "movie", report)
        }
    }

    // ------------------------------------------------------------------ pictures

    private fun chooseImage(save: LibraryRepository.(String) -> Unit) {
        pickers.image { uri ->
            if (uri == null) return@image
            scope.launch {
                val name = withContext(Dispatchers.IO) { container.coverStore.store(uri) { message(it) } } ?: return@launch
                withContext(Dispatchers.IO) { container.library.save(name) }
            }
        }
    }

    fun chooseShowCover(showId: Id) = chooseImage { setShowCover(showId, it) }

    fun chooseMoviePoster(movieId: Id) = chooseImage { setMoviePoster(movieId, it) }

    // ------------------------------------------------------------------ missing files

    /** The user picks the file that replaces a missing one; order and progress stay with the entry. */
    fun locateFile(mediaFileId: Id, onRelinked: (() -> Unit)? = null) {
        pickers.video { uri ->
            if (uri == null) return@video
            scope.launch {
                container.documents.takePersistableRead(uri)
                val (file, problem) = withContext(Dispatchers.IO) { container.documents.describeDocument(uri) }
                if (file == null) {
                    dialog = GlobalDialog.Message("Locate file", problem ?: "That file cannot be used.")
                    return@launch
                }
                val result = withContext(Dispatchers.IO) { container.library.relinkFile(mediaFileId, file) }
                if (!result.ok) {
                    dialog = GlobalDialog.Message("Locate file", result.error)
                    return@launch
                }
                message("File reconnected.")
                onRelinked?.invoke()
                val left = withContext(Dispatchers.IO) { container.library.missingFiles().size }
                if (left > 0) dialog = GlobalDialog.ReconnectOffer(left)
            }
        }
    }

    /** The user grants the folder where the moved files live; matching entries are reconnected in bulk. */
    fun reconnectFolder() {
        pickers.folder { tree ->
            if (tree == null) return@folder
            runBusy("Looking for missing files…") {
                val report = container.importer.reconnectFolder(tree) { n -> busy = "Looking for missing files… $n videos found" }
                dialog = GlobalDialog.ReconnectResult(report)
            }
        }
    }

    fun recheckFiles() {
        scope.launch {
            val (checked, missing) = container.importer.verifyAll()
            message(
                when {
                    checked == 0 -> "The library is empty."
                    missing == 0 -> "Checked $checked files. All can be opened."
                    else -> "Checked $checked files. $missing cannot be opened."
                },
            )
        }
    }
}
