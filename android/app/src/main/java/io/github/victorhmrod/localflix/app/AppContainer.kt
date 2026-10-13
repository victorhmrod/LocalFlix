package io.github.victorhmrod.localflix.app

import android.app.Application
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import io.github.victorhmrod.localflix.core.AppSettings
import io.github.victorhmrod.localflix.core.EpisodeQueue
import io.github.victorhmrod.localflix.core.FileProbe
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.PlaybackController
import io.github.victorhmrod.localflix.core.ProgressTracker
import io.github.victorhmrod.localflix.core.SettingKey
import io.github.victorhmrod.localflix.core.SystemClock
import io.github.victorhmrod.localflix.data.LibraryListener
import io.github.victorhmrod.localflix.data.LibraryRepository
import io.github.victorhmrod.localflix.data.LocalFlixDatabase
import io.github.victorhmrod.localflix.files.CoverStore
import io.github.victorhmrod.localflix.files.DocumentAccess
import io.github.victorhmrod.localflix.playback.Media3Backend
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.File

class LocalFlixApp : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(this)
    }
}

/** Hand-wired object graph, created once per process. Everything that plays or persists lives here. */
class AppContainer(
    private val context: Context,
    database: LocalFlixDatabase = LocalFlixDatabase.open(context),
    coversDir: File = File(context.filesDir, "covers"),
    fileProbe: ((DocumentAccess) -> FileProbe)? = null, // tests substitute how "does this file still exist" is answered
    startupChecks: Boolean = true,
) {
    private val main = Handler(Looper.getMainLooper())
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    val scheduler = MainScheduler(main)

    val covers = coversDir.also { it.mkdirs() }
    val library = LibraryRepository(database, SystemClock, covers)
    val settings = AppSettings(library)
    val documents = DocumentAccess(context.contentResolver)
    val coverStore = CoverStore(context.contentResolver, covers)
    val importer = ImportService(library, documents)

    val backend = Media3Backend(context)
    val queue = EpisodeQueue(library, scheduler)
    val progress = ProgressTracker(library, scheduler)
    val playback = PlaybackController(backend, queue, progress, library, settings, scheduler, fileProbe?.invoke(documents) ?: FileProbe { documents.exists(it) })

    val playbackUi = io.github.victorhmrod.localflix.ui.PlaybackUi(playback)

    private val _revision = MutableStateFlow(0L)

    /** Bumped on every change to the library; screens re-read what they show when it moves. */
    val revision = _revision.asStateFlow()

    private val _errors = MutableSharedFlow<String>(extraBufferCapacity = 8)

    /** Database problems the UI should tell the user about. */
    val errors: SharedFlow<String> = _errors.asSharedFlow()

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    init {
        library.addListener(object : LibraryListener {
            override fun onLibraryChanged() {
                _revision.update { it + 1 }
                onMain { queue.onLibraryChanged() }
            }

            override fun onShowChanged(showId: Id) {
                _revision.update { it + 1 }
                onMain { queue.onShowChanged(showId) }
            }

            override fun onProgressChanged(mediaFileId: Id) {
                _revision.update { it + 1 }
                onMain { queue.onProgressChanged(mediaFileId) }
            }

            override fun onFilesVerified(mediaFileIds: List<Id>) {
                _revision.update { it + 1 }
            }

            override fun onError(message: String) {
                _errors.tryEmit(message)
            }
        })
        applyPlayerSettings()
        settings.addChangeListener { key ->
            when (key) {
                SettingKey.PREFERRED_AUDIO_LANGUAGE, SettingKey.PREFERRED_SUBTITLE_LANGUAGE -> applyPlayerSettings()
                SettingKey.PLAYBACK_SPEED -> if (!playback.isActive) backend.setSpeed(settings.playbackSpeed)
            }
        }
        // Check the files once at startup (off the main thread), then give back access grants nothing refers to.
        if (startupChecks) {
            scope.launch {
                importer.verifyAll()
                releaseUnusedGrants()
            }
        }
    }

    private fun applyPlayerSettings() {
        backend.setPreferredLanguages(settings.preferredAudioLanguage, settings.preferredSubtitleLanguage)
        backend.setSpeed(settings.playbackSpeed)
    }

    /** Persisted folder/file grants are capped per app: release the ones no library entry uses. */
    suspend fun releaseUnusedGrants() {
        kotlinx.coroutines.withContext(Dispatchers.IO) {
            val files = library.allMediaFiles()
            val used = HashSet<String>(files.size * 2)
            for (f in files) {
                used += f.uri
                if (f.treeUri.isNotEmpty()) used += f.treeUri
            }
            documents.releaseUnused { uri: Uri -> uri.toString() in used }
        }
    }
}
