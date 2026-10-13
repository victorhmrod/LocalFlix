package io.github.victorhmrod.localflix.data

import android.util.Log
import io.github.victorhmrod.localflix.core.CardKind
import io.github.victorhmrod.localflix.core.Clock
import io.github.victorhmrod.localflix.core.Collection
import io.github.victorhmrod.localflix.core.CollectionEntry
import io.github.victorhmrod.localflix.core.CollectionItemType
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.ContinueItem
import io.github.victorhmrod.localflix.core.DuplicateInfo
import io.github.victorhmrod.localflix.core.Episode
import io.github.victorhmrod.localflix.core.INVALID_ID
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.ImportItem
import io.github.victorhmrod.localflix.core.LibraryCard
import io.github.victorhmrod.localflix.core.MIN_RESUME_MS
import io.github.victorhmrod.localflix.core.MediaFile
import io.github.victorhmrod.localflix.core.MediaKind
import io.github.victorhmrod.localflix.core.Movie
import io.github.victorhmrod.localflix.core.PlaybackStore
import io.github.victorhmrod.localflix.core.Progress
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.QueueEntry
import io.github.victorhmrod.localflix.core.RelinkProposal
import io.github.victorhmrod.localflix.core.ResumeTarget
import io.github.victorhmrod.localflix.core.ScannedFile
import io.github.victorhmrod.localflix.core.Season
import io.github.victorhmrod.localflix.core.SeasonLayout
import io.github.victorhmrod.localflix.core.SettingsStore
import io.github.victorhmrod.localflix.core.Show
import io.github.victorhmrod.localflix.core.ShowSummary
import io.github.victorhmrod.localflix.core.computeResumeTarget
import io.github.victorhmrod.localflix.core.episodeLabel
import java.io.File
import java.util.concurrent.CopyOnWriteArrayList

class ImportReport(
    val added: Int = 0,
    val addedIds: List<Id> = emptyList(),
    val duplicates: List<DuplicateInfo> = emptyList(),
    val rejected: List<String> = emptyList(), // "name — reason"
    val createdSeasonIds: List<Id> = emptyList(),
    val ok: Boolean = true,
    val error: String = "",
)

class RelinkResult(val ok: Boolean, val error: String = "")

/** Mirrors the signals of the desktop LibraryManager. Called on the thread that made the change. */
interface LibraryListener {
    /** Structural change of the lists (shows/movies/collections added, removed, renamed). */
    fun onLibraryChanged() {}

    /** Episodes/seasons of one show changed (content, order, watched state). */
    fun onShowChanged(showId: Id) {}

    fun onProgressChanged(mediaFileId: Id) {}

    fun onFilesVerified(mediaFileIds: List<Id>) {}

    fun onError(message: String) {}
}

/**
 * Application-level facade over the database: validation, change notifications, cover files and file recovery.
 * All methods are blocking; callers on the UI thread use the small ones (playback bookkeeping) and everything
 * else goes through a background dispatcher. Failures are reported through [LibraryListener.onError] and never thrown.
 */
class LibraryRepository(
    private val db: LocalFlixDatabase,
    private val clock: Clock,
    private val coversDir: File,
) : PlaybackStore, SettingsStore {
    private val dao = db.dao()
    private val listeners = CopyOnWriteArrayList<LibraryListener>()

    fun addListener(l: LibraryListener) {
        listeners += l
    }

    fun removeListener(l: LibraryListener) {
        listeners -= l
    }

    private fun reportError(message: String, t: Throwable? = null) {
        Log.e(TAG, message, t)
        listeners.forEach { it.onError(message) }
    }

    private inline fun <R> guarded(fallback: R, block: () -> R): R = try {
        block()
    } catch (e: RuntimeException) { // SQLiteException and IllegalState from validation are both runtime exceptions
        reportError(e.message ?: e.javaClass.simpleName, e)
        fallback
    }

    private fun libraryChanged() = listeners.forEach { it.onLibraryChanged() }

    private fun showChanged(id: Id) = listeners.forEach { it.onShowChanged(id) }

    private fun progressChanged(id: Id) = listeners.forEach { it.onProgressChanged(id) }

    private fun now() = clock.nowMs()

    // ------------------------------------------------------------------ mapping

    private fun MediaFileEntity.toCore() = MediaFile(
        id, uri, docKey, displayName, relativePath, treeUri, sizeBytes, mtimeMs, durationMs, isMissing, addedAt,
    )

    private fun ProgressEntity.toCore() = Progress(
        mediaFileId, positionMs, durationMs, completed, playCount, lastWatchedAt, audioTrack, subtitleTrack,
    )

    private fun ShowEntity.toCore() = Show(id, title, description, coverFile, createdAt, updatedAt)

    private fun SeasonEntity.toCore() = Season(id, showId, seasonNumber, title, sortOrder)

    private fun EpisodeRow.toCore() = Episode(
        id, seasonId, showId, seasonSortOrder, seasonNumber, title, episodeNumber, sortOrder, createdAt, file.toCore(), progress.toCore(),
    )

    private fun MovieRow.toCore() = Movie(id, title, description, posterFile, createdAt, file.toCore(), progress.toCore())

    // ------------------------------------------------------------------ covers

    /** Absolute file of a stored cover/poster, or null if there is none / the file vanished. */
    fun coverFile(fileName: String): File? = fileName.takeIf { it.isNotEmpty() }?.let { File(coversDir, it) }?.takeIf { it.isFile }

    private fun removeStoredImage(fileName: String) {
        if (fileName.isNotEmpty()) File(coversDir, fileName).delete()
    }

    // ------------------------------------------------------------------ shows & seasons

    fun showSummaries(): List<ShowSummary> = guarded(emptyList()) {
        dao.showSummaries().map { ShowSummary(it.show.toCore(), it.seasonCount, it.episodeCount, it.completedCount, it.lastWatched) }
    }

    fun show(id: Id): Show? = guarded(null) { dao.show(id)?.toCore() }

    fun createShow(rawTitle: String): Id {
        val title = rawTitle.trim()
        if (title.isEmpty()) {
            reportError("A show needs a title.")
            return INVALID_ID
        }
        val id = guarded(INVALID_ID) {
            db.runInTransaction<Id> {
                val showId = dao.insertShow(ShowEntity(title = title, createdAt = now(), updatedAt = now()))
                dao.insertSeason(SeasonEntity(showId = showId, seasonNumber = 1, title = "Season 1", sortOrder = 0))
                showId
            }
        }
        if (id != INVALID_ID) libraryChanged()
        return id
    }

    fun renameShow(id: Id, rawTitle: String): Boolean {
        val title = rawTitle.trim()
        if (title.isEmpty()) {
            reportError("A show needs a title.")
            return false
        }
        val ok = guarded(false) {
            val s = dao.show(id) ?: return@guarded false
            dao.updateShow(id, title, s.description, s.coverFile, now()) == 1
        }
        if (ok) {
            libraryChanged()
            showChanged(id)
        }
        return ok
    }

    fun setShowDescription(id: Id, description: String): Boolean {
        val ok = guarded(false) {
            val s = dao.show(id) ?: return@guarded false
            dao.updateShow(id, s.title, description.trim(), s.coverFile, now()) == 1
        }
        if (ok) showChanged(id)
        return ok
    }

    /** [storedFileName] is a file the caller already copied into the covers directory. */
    fun setShowCover(id: Id, storedFileName: String): Boolean {
        var previous = ""
        val ok = guarded(false) {
            val s = dao.show(id) ?: return@guarded false
            previous = s.coverFile
            dao.updateShow(id, s.title, s.description, storedFileName, now()) == 1
        }
        if (ok) {
            if (previous != storedFileName) removeStoredImage(previous)
            libraryChanged()
            showChanged(id)
        } else {
            removeStoredImage(storedFileName)
        }
        return ok
    }

    fun clearShowCover(id: Id): Boolean = setShowCoverInternal(id, "")

    private fun setShowCoverInternal(id: Id, value: String): Boolean {
        var previous = ""
        val ok = guarded(false) {
            val s = dao.show(id) ?: return@guarded false
            previous = s.coverFile
            dao.updateShow(id, s.title, s.description, value, now()) == 1
        }
        if (ok) {
            removeStoredImage(previous)
            libraryChanged()
            showChanged(id)
        }
        return ok
    }

    fun deleteShow(id: Id): Boolean {
        var cover = ""
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                cover = dao.coverOfShow(id).orEmpty()
                dao.deleteEntriesOfItem(ITEM_SHOW, id)
                dao.deleteShow(id) // cascades to seasons/episodes
                dao.deleteOrphanMedia()
                true
            }
        }
        if (ok) {
            removeStoredImage(cover)
            libraryChanged()
        }
        return ok
    }

    fun seasons(showId: Id): List<Season> = guarded(emptyList()) { dao.seasons(showId).map { it.toCore() } }

    fun season(seasonId: Id): Season? = guarded(null) { dao.season(seasonId)?.toCore() }

    fun addSeason(showId: Id, rawTitle: String = "", number: Int = -1): Id {
        val id = guarded(INVALID_ID) {
            var n = number
            if (n < 0) n = (dao.seasons(showId).maxOfOrNull { it.seasonNumber } ?: 0) + 1
            var title = rawTitle.trim()
            if (title.isEmpty()) title = if (n == 0) "Specials" else "Season $n"
            dao.insertSeason(SeasonEntity(showId = showId, seasonNumber = n, title = title, sortOrder = dao.nextSeasonOrder(showId)))
        }
        if (id != INVALID_ID) showChanged(showId)
        return id
    }

    fun updateSeason(seasonId: Id, title: String, number: Int): Boolean {
        val t = title.trim()
        if (t.isEmpty()) {
            reportError("A season needs a title.")
            return false
        }
        var showId = INVALID_ID
        val ok = guarded(false) {
            val s = dao.season(seasonId) ?: return@guarded false
            showId = s.showId
            dao.updateSeason(seasonId, t, number) == 1
        }
        if (ok) showChanged(showId)
        return ok
    }

    fun deleteSeason(seasonId: Id): Boolean {
        var showId = INVALID_ID
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                val s = dao.season(seasonId) ?: return@runInTransaction false
                showId = s.showId
                dao.deleteSeason(seasonId) // cascades to episodes
                dao.deleteOrphanMedia()
                true
            }
        }
        if (ok) {
            showChanged(showId)
            libraryChanged()
        }
        return ok
    }

    // ------------------------------------------------------------------ episodes

    fun episodes(showId: Id): List<Episode> = guarded(emptyList()) { dao.episodes(showId).map { it.toCore() } }

    fun episode(id: Id): Episode? = guarded(null) { dao.episode(id)?.toCore() }

    fun episodeByMedia(mediaFileId: Id): Episode? = guarded(null) { dao.episodeByMedia(mediaFileId)?.toCore() }

    fun updateEpisode(id: Id, title: String, episodeNumber: Int): Boolean {
        val t = title.trim()
        if (t.isEmpty()) {
            reportError("An episode needs a title.")
            return false
        }
        var showId = INVALID_ID
        val ok = guarded(false) {
            val e = dao.episode(id) ?: return@guarded false
            showId = e.showId
            dao.updateEpisode(id, t, maxOf(0, episodeNumber)) == 1
        }
        if (ok) showChanged(showId)
        return ok
    }

    fun removeEpisodes(episodeIds: List<Id>): Boolean {
        val shows = HashSet<Id>()
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                for (id in episodeIds) dao.episode(id)?.let { shows += it.showId }
                for (id in episodeIds) dao.deleteEpisode(id)
                dao.deleteOrphanMedia()
                true
            }
        }
        if (ok) {
            shows.forEach(::showChanged)
            libraryChanged()
        }
        return ok
    }

    /**
     * Replaces season order, episode order and episode->season assignment in one transaction. [layout] must contain
     * every season and episode of the show exactly once.
     */
    fun applyLayout(showId: Id, layout: List<SeasonLayout>): Boolean {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                val knownSeasons = dao.seasons(showId).map { it.id }.toSet()
                val knownEpisodes = dao.episodeIdsOfShow(showId).toSet()
                val seenSeasons = HashSet<Id>()
                val seenEpisodes = HashSet<Id>()
                for (sl in layout) {
                    check(sl.seasonId in knownSeasons && seenSeasons.add(sl.seasonId)) {
                        "applyLayout: season ${sl.seasonId} is not part of show $showId or repeated"
                    }
                    for (eid in sl.episodeIds) {
                        check(eid in knownEpisodes && seenEpisodes.add(eid)) {
                            "applyLayout: episode $eid is not part of show $showId or repeated"
                        }
                    }
                }
                check(seenSeasons == knownSeasons && seenEpisodes == knownEpisodes) {
                    "applyLayout: layout does not cover every season and episode of show $showId"
                }
                layout.forEachIndexed { seasonOrder, sl ->
                    dao.setSeasonOrder(sl.seasonId, seasonOrder)
                    sl.episodeIds.forEachIndexed { episodeOrder, eid -> dao.placeEpisode(eid, sl.seasonId, episodeOrder) }
                }
                true
            }
        }
        if (ok) showChanged(showId)
        return ok
    }

    fun moveEpisode(episodeId: Id, toSeasonId: Id, toIndex: Int): Boolean {
        var showId = INVALID_ID
        var layout: List<SeasonLayout> = emptyList()
        val built = guarded(false) {
            val e = dao.episode(episodeId) ?: return@guarded false
            showId = e.showId
            val seasons = dao.seasons(showId)
            if (seasons.none { it.id == toSeasonId }) return@guarded false
            val eps = dao.episodes(showId)
            layout = seasons.map { s ->
                val ids = eps.filter { it.seasonId == s.id && it.id != episodeId }.map { it.id }.toMutableList()
                if (s.id == toSeasonId) ids.add(toIndex.coerceIn(0, ids.size), episodeId)
                SeasonLayout(s.id, ids)
            }
            true
        }
        return built && applyLayout(showId, layout)
    }

    fun moveSeason(seasonId: Id, toIndex: Int): Boolean {
        var showId = INVALID_ID
        var layout: List<SeasonLayout> = emptyList()
        val built = guarded(false) {
            val season = dao.season(seasonId) ?: return@guarded false
            showId = season.showId
            val eps = dao.episodes(showId)
            val order = dao.seasons(showId).map { it.id }.filter { it != seasonId }.toMutableList()
            order.add(toIndex.coerceIn(0, order.size), seasonId)
            layout = order.map { sid -> SeasonLayout(sid, eps.filter { it.seasonId == sid }.map { it.id }) }
            true
        }
        return built && applyLayout(showId, layout)
    }

    // ------------------------------------------------------------------ movies

    fun movies(): List<Movie> = guarded(emptyList()) { dao.movies().map { it.toCore() } }

    fun movie(id: Id): Movie? = guarded(null) { dao.movie(id)?.toCore() }

    fun movieByMedia(mediaFileId: Id): Movie? = guarded(null) { dao.movieByMedia(mediaFileId)?.toCore() }

    fun updateMovie(id: Id, title: String, description: String): Boolean {
        val t = title.trim()
        if (t.isEmpty()) {
            reportError("A movie needs a title.")
            return false
        }
        val ok = guarded(false) {
            val m = dao.movie(id) ?: return@guarded false
            dao.updateMovie(id, t, description.trim(), m.posterFile) == 1
        }
        if (ok) libraryChanged()
        return ok
    }

    fun setMoviePoster(id: Id, storedFileName: String): Boolean {
        var previous = ""
        val ok = guarded(false) {
            val m = dao.movie(id) ?: return@guarded false
            previous = m.posterFile
            dao.updateMovie(id, m.title, m.description, storedFileName) == 1
        }
        if (ok) {
            if (previous != storedFileName) removeStoredImage(previous)
            libraryChanged()
        } else {
            removeStoredImage(storedFileName)
        }
        return ok
    }

    fun clearMoviePoster(id: Id): Boolean {
        var previous = ""
        val ok = guarded(false) {
            val m = dao.movie(id) ?: return@guarded false
            previous = m.posterFile
            dao.updateMovie(id, m.title, m.description, "") == 1
        }
        if (ok) {
            removeStoredImage(previous)
            libraryChanged()
        }
        return ok
    }

    fun deleteMovie(id: Id): Boolean {
        var poster = ""
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                poster = dao.posterOfMovie(id).orEmpty()
                dao.deleteEntriesOfItem(ITEM_MOVIE, id)
                dao.deleteMovie(id)
                dao.deleteOrphanMedia()
                true
            }
        }
        if (ok) {
            removeStoredImage(poster)
            libraryChanged()
        }
        return ok
    }

    // ------------------------------------------------------------------ import

    private fun describeMedia(mediaFileId: Id): String {
        dao.episodeByMedia(mediaFileId)?.let { e ->
            val showTitle = dao.show(e.showId)?.title.orEmpty()
            return "$showTitle — ${episodeLabel(e.seasonNumber, e.episodeNumber, e.title)}"
        }
        dao.movieByMedia(mediaFileId)?.let { return "Movie: ${it.title}" }
        return "the library"
    }

    private fun insertFile(f: ScannedFile): Id = dao.insertMediaFile(
        MediaFileEntity(
            uri = f.uri, docKey = f.docKey, displayName = f.displayName, relativePath = f.relativePath, treeUri = f.treeUri,
            sizeBytes = f.sizeBytes, mtimeMs = f.mtimeMs, addedAt = now(),
        ),
    )

    private fun resolveSeasonForImport(showId: Id, seasonNumber: Int, fallbackSeasonId: Id, created: MutableList<Id>): Id {
        if (seasonNumber < 0) {
            if (fallbackSeasonId != INVALID_ID) return fallbackSeasonId
            dao.seasons(showId).firstOrNull()?.let { return it.id }
            return dao.insertSeason(SeasonEntity(showId = showId, seasonNumber = 1, title = "Season 1", sortOrder = dao.nextSeasonOrder(showId)))
                .also { created += it }
        }
        dao.seasonIdByNumber(showId, seasonNumber)?.let { return it }

        val title = if (seasonNumber == 0) "Specials" else "Season $seasonNumber"
        // Keep numbered seasons ascending: slot the new one before the first numbered season with a higher number.
        var position = -1
        if (seasonNumber > 0) position = dao.firstSeasonOrderAbove(showId, seasonNumber) ?: -1
        if (position < 0) {
            position = dao.nextSeasonOrder(showId)
        } else {
            dao.shiftSeasonOrders(showId, position)
        }
        return dao.insertSeason(SeasonEntity(showId = showId, seasonNumber = seasonNumber, title = title, sortOrder = position))
            .also { created += it }
    }

    fun importEpisodes(showId: Id, fallbackSeasonId: Id, plan: List<ImportItem>, rejected: List<String> = emptyList()): ImportReport {
        return try {
            val added = ArrayList<Id>()
            val duplicates = ArrayList<DuplicateInfo>()
            val created = ArrayList<Id>()
            db.runInTransaction {
                for (item in plan) {
                    val existing = dao.mediaFileByDocKey(item.file.docKey)
                    if (existing != null) {
                        duplicates += DuplicateInfo(item.file.sortKey, describeMedia(existing.id))
                        continue
                    }
                    val seasonId = resolveSeasonForImport(showId, item.seasonNumber, fallbackSeasonId, created)
                    val mediaId = insertFile(item.file)
                    val order = dao.nextEpisodeOrder(seasonId)
                    added += dao.insertEpisode(
                        EpisodeEntity(
                            seasonId = seasonId, mediaFileId = mediaId, title = item.title, episodeNumber = item.episodeNumber,
                            sortOrder = order, createdAt = now(),
                        ),
                    )
                }
                if (added.isNotEmpty()) dao.touchShow(showId, now())
            }
            if (added.isNotEmpty()) {
                showChanged(showId)
                libraryChanged()
            }
            ImportReport(added.size, added, duplicates, rejected, created)
        } catch (e: RuntimeException) {
            reportError(e.message ?: "import failed", e)
            ImportReport(ok = false, error = e.message ?: "import failed", rejected = rejected)
        }
    }

    fun importMovies(plan: List<ImportItem>, rejected: List<String> = emptyList()): ImportReport {
        return try {
            val added = ArrayList<Id>()
            val duplicates = ArrayList<DuplicateInfo>()
            db.runInTransaction {
                for (item in plan) {
                    val existing = dao.mediaFileByDocKey(item.file.docKey)
                    if (existing != null) {
                        duplicates += DuplicateInfo(item.file.sortKey, describeMedia(existing.id))
                        continue
                    }
                    val mediaId = insertFile(item.file)
                    added += dao.insertMovie(MovieEntity(mediaFileId = mediaId, title = item.title, createdAt = now()))
                }
            }
            if (added.isNotEmpty()) libraryChanged()
            ImportReport(added.size, added, duplicates, rejected)
        } catch (e: RuntimeException) {
            reportError(e.message ?: "import failed", e)
            ImportReport(ok = false, error = e.message ?: "import failed", rejected = rejected)
        }
    }

    // ------------------------------------------------------------------ collections

    fun collections(): List<Collection> = guarded(emptyList()) {
        dao.collections().map { Collection(it.collection.id, it.collection.name, it.collection.description, it.collection.createdAt, it.entryCount) }
    }

    fun collection(id: Id): Collection? = collections().firstOrNull { it.id == id }

    fun createCollection(name: String): Id {
        val n = name.trim()
        if (n.isEmpty()) {
            reportError("A collection needs a name.")
            return INVALID_ID
        }
        val id = guarded(INVALID_ID) { dao.insertCollection(CollectionEntity(name = n, createdAt = now())) }
        if (id != INVALID_ID) libraryChanged()
        return id
    }

    fun updateCollection(id: Id, name: String, description: String): Boolean {
        val n = name.trim()
        if (n.isEmpty()) {
            reportError("A collection needs a name.")
            return false
        }
        val ok = guarded(false) { dao.updateCollection(id, n, description.trim()) == 1 }
        if (ok) libraryChanged()
        return ok
    }

    fun deleteCollection(id: Id): Boolean {
        val ok = guarded(false) { dao.deleteCollection(id) >= 0 }
        if (ok) libraryChanged()
        return ok
    }

    fun collectionEntries(collectionId: Id): List<CollectionEntry> = guarded(emptyList()) {
        dao.collectionEntries(collectionId).map {
            CollectionEntry(
                it.entry.id, it.entry.collectionId,
                if (it.entry.itemType == ITEM_SHOW) CollectionItemType.SHOW else CollectionItemType.MOVIE,
                it.entry.itemId, it.entry.sortOrder, it.title.orEmpty(),
            )
        }
    }

    fun addToCollection(collectionId: Id, type: CollectionItemType, itemId: Id): Boolean {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                dao.insertCollectionEntry(
                    CollectionEntryEntity(
                        collectionId = collectionId, itemType = if (type == CollectionItemType.SHOW) ITEM_SHOW else ITEM_MOVIE,
                        itemId = itemId, sortOrder = dao.nextCollectionOrder(collectionId),
                    ),
                ) > 0
            }
        }
        if (ok) libraryChanged()
        return ok
    }

    fun removeFromCollection(entryId: Id, collectionId: Id): Boolean {
        val ok = guarded(false) { dao.deleteCollectionEntry(entryId, collectionId) >= 0 }
        if (ok) libraryChanged()
        return ok
    }

    fun setCollectionOrder(collectionId: Id, entryIdsInOrder: List<Id>): Boolean {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                entryIdsInOrder.forEachIndexed { order, id -> dao.setCollectionEntryOrder(id, collectionId, order) }
                true
            }
        }
        if (ok) libraryChanged()
        return ok
    }

    // ------------------------------------------------------------------ progress

    override fun progress(mediaFileId: Id): Progress =
        guarded(Progress(mediaFileId)) { dao.progress(mediaFileId)?.toCore() ?: Progress(mediaFileId) }

    private inline fun writeProgress(mediaFileId: Id, crossinline change: () -> Unit) {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                dao.ensureProgressRow(mediaFileId)
                change()
                true
            }
        }
        if (ok) progressChanged(mediaFileId)
    }

    override fun saveProgress(mediaFileId: Id, positionMs: Long, durationMs: Long) =
        writeProgress(mediaFileId) { dao.updatePosition(mediaFileId, positionMs, durationMs, now()) }

    override fun touchLastWatched(mediaFileId: Id) = writeProgress(mediaFileId) { dao.updateLastWatched(mediaFileId, now()) }

    override fun saveTrackSelection(mediaFileId: Id, audioTrack: Int, subtitleTrack: Int) {
        guarded(Unit) {
            db.runInTransaction {
                dao.ensureProgressRow(mediaFileId)
                dao.updateTracks(mediaFileId, audioTrack, subtitleTrack, now())
            }
        }
    }

    override fun markCompleted(mediaFileId: Id, durationMs: Long) {
        writeProgress(mediaFileId) { dao.updateCompleted(mediaFileId, durationMs, now()) }
        episodeByMedia(mediaFileId)?.let { showChanged(it.showId) }
    }

    override fun setMediaDuration(mediaFileId: Id, durationMs: Long) {
        if (durationMs <= 0) return
        guarded(Unit) { dao.setMediaDuration(mediaFileId, durationMs) }
    }

    fun setWatched(mediaFileIds: List<Id>, watched: Boolean): Boolean {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                for (id in mediaFileIds) {
                    dao.ensureProgressRow(id)
                    dao.updateWatched(id, watched, now())
                }
                true
            }
        }
        if (ok) notifyProgressBatch(mediaFileIds)
        return ok
    }

    fun resetProgress(mediaFileIds: List<Id>): Boolean {
        val ok = guarded(false) {
            db.runInTransaction<Boolean> {
                for (id in mediaFileIds) dao.resetProgress(id, now())
                true
            }
        }
        if (ok) notifyProgressBatch(mediaFileIds)
        return ok
    }

    fun resetShowProgress(showId: Id): Boolean = resetProgress(episodes(showId).map { it.file.id })

    private fun notifyProgressBatch(mediaFileIds: List<Id>) {
        val shows = HashSet<Id>()
        for (id in mediaFileIds) {
            progressChanged(id)
            episodeByMedia(id)?.let { shows += it.showId }
        }
        shows.forEach(::showChanged)
        libraryChanged()
    }

    // ------------------------------------------------------------------ continue watching

    fun resumeTarget(showId: Id): ResumeTarget = guarded(ResumeTarget()) { computeResumeTarget(episodes(showId)) }

    fun continueWatching(limit: Int = 20): List<ContinueItem> = guarded(emptyList()) {
        val out = ArrayList<ContinueItem>()
        for (h in dao.showsWithHistory(limit)) {
            val show = dao.show(h.showId) ?: continue
            val eps = episodes(h.showId)
            val target = computeResumeTarget(eps)
            if (!target.valid() || target.allCompleted()) continue // finished shows leave the row
            val e = eps.firstOrNull { it.id == target.episodeId } ?: continue
            out += ContinueItem(
                kind = MediaKind.EPISODE, showId = h.showId, title = show.title, subtitle = episodeLabel(e),
                coverFile = show.coverFile, mediaFileId = e.file.id, positionMs = target.positionMs,
                durationMs = if (e.progress.durationMs > 0) e.progress.durationMs else e.file.durationMs,
                lastWatchedAt = h.lastWatched, missing = e.file.missing,
            )
        }
        for (m in dao.startedMovies(MIN_RESUME_MS, limit)) {
            val movie = m.toCore()
            out += ContinueItem(
                kind = MediaKind.MOVIE, movieId = movie.id, title = movie.title, coverFile = movie.posterFile,
                mediaFileId = movie.file.id, positionMs = movie.progress.resumePositionMs(),
                durationMs = if (movie.progress.durationMs > 0) movie.progress.durationMs else movie.file.durationMs,
                lastWatchedAt = movie.progress.lastWatchedAt, missing = movie.file.missing,
            )
        }
        out.sortedByDescending { it.lastWatchedAt }.take(limit)
    }

    fun recentlyAdded(limit: Int = 20): List<LibraryCard> = guarded(emptyList()) {
        val out = ArrayList<LibraryCard>()
        for (s in showSummaries()) {
            out += LibraryCard(
                kind = CardKind.SHOW, id = s.show.id, title = s.show.title,
                subtitle = if (s.episodeCount == 1) "1 episode" else "${s.episodeCount} episodes",
                imageFile = s.show.coverFile, timestamp = s.show.updatedAt,
                progress = if (s.episodeCount > 0) s.completedCount.toDouble() / s.episodeCount else 0.0,
                allWatched = s.episodeCount > 0 && s.completedCount >= s.episodeCount,
            )
        }
        for (m in movies()) {
            out += LibraryCard(
                kind = CardKind.MOVIE, id = m.id, title = m.title, subtitle = "Movie", imageFile = m.posterFile,
                timestamp = m.createdAt, progress = m.progress.fraction(), allWatched = m.progress.completed, missing = m.file.missing,
            )
        }
        out.sortedByDescending { it.timestamp }.take(limit)
    }

    // ------------------------------------------------------------------ queue

    private fun toEntry(e: Episode, showTitle: String) = QueueEntry(
        kind = MediaKind.EPISODE, itemId = e.id, showId = e.showId, mediaFileId = e.file.id, title = episodeLabel(e),
        contextTitle = showTitle, uri = e.file.uri, fileLabel = e.file.label, missing = e.file.missing,
        completed = e.progress.completed, positionMs = e.progress.resumePositionMs(),
        durationMs = if (e.progress.durationMs > 0) e.progress.durationMs else e.file.durationMs,
    )

    private fun toEntry(m: Movie) = QueueEntry(
        kind = MediaKind.MOVIE, itemId = m.id, mediaFileId = m.file.id, title = m.title, uri = m.file.uri,
        fileLabel = m.file.label, missing = m.file.missing, completed = m.progress.completed,
        positionMs = m.progress.resumePositionMs(),
        durationMs = if (m.progress.durationMs > 0) m.progress.durationMs else m.file.durationMs,
    )

    override fun buildQueue(context: QueueContext): List<QueueEntry> = guarded(emptyList()) {
        val out = ArrayList<QueueEntry>()
        fun appendShow(showId: Id) {
            val show = dao.show(showId) ?: return
            for (e in episodes(showId)) out += toEntry(e, show.title)
        }
        when (context.type) {
            ContextType.SHOW -> appendShow(context.id)
            ContextType.MOVIE -> movie(context.id)?.let { out += toEntry(it) }
            ContextType.COLLECTION -> for (ce in collectionEntries(context.id)) {
                if (ce.type == CollectionItemType.SHOW) appendShow(ce.itemId) else movie(ce.itemId)?.let { out += toEntry(it) }
            }
            ContextType.NONE -> {}
        }
        out
    }

    // ------------------------------------------------------------------ file recovery

    override fun mediaFile(id: Id): MediaFile? = guarded(null) { dao.mediaFile(id)?.toCore() }

    fun allMediaFiles(): List<MediaFile> = guarded(emptyList()) { dao.allMediaFiles().map { it.toCore() } }

    fun missingFiles(): List<MediaFile> = guarded(emptyList()) { dao.missingMediaFiles().map { it.toCore() } }

    fun mediaFilesOfShow(showId: Id): List<MediaFile> = guarded(emptyList()) { dao.mediaFilesOfShow(showId).map { it.toCore() } }

    /** Where an entry lives in the library, for messages: "Show — S1E3 · Title" or "Movie: Title". */
    fun describe(mediaFileId: Id): String = guarded("the library") { describeMedia(mediaFileId) }

    /** Reconnects an entry to a new document. Metadata, order and progress are kept. */
    fun relinkFile(mediaFileId: Id, newFile: ScannedFile): RelinkResult {
        val result = try {
            db.runInTransaction<RelinkResult> {
                dao.mediaFile(mediaFileId) ?: return@runInTransaction RelinkResult(false, "This entry no longer exists in the library.")
                val other = dao.mediaFileByDocKey(newFile.docKey)
                if (other != null && other.id != mediaFileId) {
                    return@runInTransaction RelinkResult(false, "That file is already in the library as \"${describeMedia(other.id)}\".")
                }
                dao.updateMediaLocation(
                    mediaFileId, newFile.uri, newFile.docKey, newFile.displayName, newFile.relativePath, newFile.treeUri,
                    newFile.sizeBytes, newFile.mtimeMs,
                )
                RelinkResult(true)
            }
        } catch (e: RuntimeException) {
            reportError(e.message ?: "relink failed", e)
            RelinkResult(false, e.message ?: "relink failed")
        }
        if (result.ok) {
            episodeByMedia(mediaFileId)?.let { showChanged(it.showId) }
            listeners.forEach { it.onFilesVerified(listOf(mediaFileId)) }
            libraryChanged()
        }
        return result
    }

    fun applyRelinks(proposals: List<RelinkProposal>): Int = proposals.count { relinkFile(it.mediaFileId, it.newFile).ok }

    override fun setMissing(mediaFileId: Id, missing: Boolean): Boolean {
        val ok = guarded(false) {
            dao.setMissing(listOf(mediaFileId), missing)
            true
        }
        if (ok) {
            episodeByMedia(mediaFileId)?.let { showChanged(it.showId) }
            listeners.forEach { it.onFilesVerified(listOf(mediaFileId)) }
        }
        return ok
    }

    /**
     * Applies the outcome of probing files off the main thread: [results] pairs the snapshot that was probed with
     * whether it turned out to be missing. Entries relinked or removed in the meantime are skipped.
     * Returns the number of entries probed and how many entries are missing now.
     */
    fun applyVerification(results: List<Pair<MediaFile, Boolean>>): Pair<Int, Int> = guarded(0 to 0) {
        val nowMissing = ArrayList<Id>()
        val nowPresent = ArrayList<Id>()
        val shows = HashSet<Id>()
        db.runInTransaction {
            for ((snapshot, missing) in results) {
                val current = dao.mediaFile(snapshot.id) ?: continue
                if (current.uri != snapshot.uri) continue // relinked while we were checking
                if (current.isMissing == missing) continue
                (if (missing) nowMissing else nowPresent) += snapshot.id
                dao.episodeByMedia(snapshot.id)?.let { shows += it.showId }
            }
            if (nowMissing.isNotEmpty()) dao.setMissing(nowMissing, true)
            if (nowPresent.isNotEmpty()) dao.setMissing(nowPresent, false)
        }
        val changed = nowMissing + nowPresent
        if (changed.isNotEmpty()) {
            shows.forEach(::showChanged)
            listeners.forEach { it.onFilesVerified(changed) }
            libraryChanged()
        }
        results.size to dao.missingMediaFiles().size
    }

    // ------------------------------------------------------------------ settings

    override fun allSettings(): Map<String, String> = guarded(emptyMap()) { dao.allSettings().associate { it.key to it.value } }

    override fun setSetting(key: String, value: String) {
        guarded(Unit) { dao.putSetting(SettingEntity(key, value)) }
    }

    private companion object {
        const val TAG = "LocalFlix.Library"
        const val ITEM_SHOW = "show"
        const val ITEM_MOVIE = "movie"
    }
}
