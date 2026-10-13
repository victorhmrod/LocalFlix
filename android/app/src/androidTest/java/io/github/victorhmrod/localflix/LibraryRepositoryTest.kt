package io.github.victorhmrod.localflix

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.github.victorhmrod.localflix.core.CollectionItemType
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.MediaKind
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.ResumeReason
import io.github.victorhmrod.localflix.core.SeasonLayout
import io.github.victorhmrod.localflix.core.TRACK_AUTO
import io.github.victorhmrod.localflix.core.TRACK_OFF
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The SQLite-backed library on a real device: port of the library, database and progress suites of the desktop
 * app (tst_library, tst_database, tst_progress). Each test gets a fresh in-memory database.
 */
@RunWith(AndroidJUnit4::class)
class LibraryRepositoryTest {
    private lateinit var env: RepoEnv
    private val repo get() = env.repo

    @Before
    fun setUp() {
        env = RepoEnv()
    }

    @After
    fun tearDown() {
        env.close()
    }

    // ------------------------------------------------------------ shows and import

    @Test
    fun aNewShowStartsWithOneSeasonAndNeedsATitle() {
        val id = repo.createShow("  Dark Matter  ")
        assertEquals("Dark Matter", repo.show(id)!!.title)
        assertEquals(listOf("Season 1"), repo.seasons(id).map { it.title })
        assertEquals(0L, repo.createShow("   "))
        assertEquals(1, env.errors.size)
    }

    @Test
    fun importKeepsThePlannedOrderAndIgnoresDuplicates() {
        val showId = repo.createShow("Order")
        val files = listOf("Order/ep10.mp4", "Order/ep2.mp4", "Order/ep1.mp4").map { scannedFile(it) }
        val report = repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(files))
        assertEquals(3, report.added)
        assertEquals(listOf("ep1.mp4", "ep2.mp4", "ep10.mp4"), repo.episodes(showId).map { it.file.displayName })

        // the same documents again, plus one new: only the new one is added, the others are reported
        val again = repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(files + scannedFile("Order/ep11.mp4")))
        assertEquals(1, again.added)
        assertEquals(3, again.duplicates.size)
        assertTrue(again.duplicates.all { it.existingAs.startsWith("Order — ") })
        assertEquals(4, repo.episodes(showId).size)
    }

    @Test
    fun theSameDocumentGrantedAnotherWayIsStillADuplicate() {
        val showId = repo.createShow("Dup")
        repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(listOf(scannedFile("Dup/a.mp4"))))
        // as a single picked file the URI differs, the document identity does not
        val viaPicker = scannedFile("a.mp4", uri = "content://test/document/other-uri", docKey = "test/Library/Dup/a.mp4")
        val report = repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(listOf(viaPicker)))
        assertEquals(0, report.added)
        assertEquals(1, report.duplicates.size)
    }

    @Test
    fun seasonsAreCreatedFromNamesAndKeptAscending() {
        val showId = repo.createShow("Seasons")
        assertTrue(repo.deleteSeason(repo.seasons(showId).single().id)) // start without the default season
        fun import(vararg names: String) = repo.importEpisodes(showId, 0, ImportPlanner.planEpisodes(names.map { scannedFile("Seasons/$it") }))
        import("Seasons S03E01.mkv")
        import("Seasons S01E01.mkv")
        import("Seasons S02E01.mkv") // arrives last, but belongs between 1 and 3
        import("Extras/making of.mkv") // specials are appended
        assertEquals(listOf("Season 1", "Season 2", "Season 3", "Specials"), repo.seasons(showId).map { it.title })
        assertEquals(listOf(1, 2, 3, 0), repo.seasons(showId).map { it.number })
        assertEquals(listOf("Seasons S01E01.mkv", "Seasons S02E01.mkv", "Seasons S03E01.mkv", "making of.mkv"), repo.episodes(showId).map { it.file.displayName })
    }

    @Test
    fun importIntoAGivenSeasonUsesItWhenNothingElseIsDetected() {
        val showId = repo.createShow("Given")
        val second = repo.addSeason(showId)
        val report = repo.importEpisodes(showId, second, ImportPlanner.planEpisodes(listOf(scannedFile("Given/a.mp4"), scannedFile("Given/b.mp4"))))
        assertEquals(2, report.added)
        assertEquals(setOf(second), repo.episodes(showId).map { it.seasonId }.toSet())
    }

    // ------------------------------------------------------------ ordering

    @Test
    fun applyLayoutReordersAndMovesAcrossSeasons() {
        val (showId, eps) = env.makeShow("Reorder", 4)
        val s1 = eps[0].seasonId
        val s2 = repo.addSeason(showId)
        val ok = repo.applyLayout(showId, listOf(SeasonLayout(s2, listOf(eps[3].id)), SeasonLayout(s1, listOf(eps[2].id, eps[0].id, eps[1].id))))
        assertTrue(ok)
        assertEquals(listOf(s2, s1), repo.seasons(showId).map { it.id })
        assertEquals(listOf(eps[3].id, eps[2].id, eps[0].id, eps[1].id), repo.episodes(showId).map { it.id })
        assertEquals(s2, repo.episode(eps[3].id)!!.seasonId)
    }

    @Test
    fun anInvalidLayoutChangesNothing() {
        val (showId, eps) = env.makeShow("Strict", 3)
        val s1 = eps[0].seasonId
        val before = repo.episodes(showId).map { it.id }
        // missing an episode, repeating one, and naming a stranger
        assertFalse(repo.applyLayout(showId, listOf(SeasonLayout(s1, listOf(eps[1].id, eps[0].id)))))
        assertFalse(repo.applyLayout(showId, listOf(SeasonLayout(s1, listOf(eps[1].id, eps[1].id, eps[0].id, eps[2].id)))))
        assertFalse(repo.applyLayout(showId, listOf(SeasonLayout(s1, listOf(eps[2].id, eps[1].id, eps[0].id, 9999)))))
        assertFalse(repo.applyLayout(showId, listOf(SeasonLayout(9999, before))))
        assertEquals(before, repo.episodes(showId).map { it.id })
    }

    @Test
    fun moveEpisodeAndMoveSeason() {
        val (showId, eps) = env.makeShow("Move", 3)
        val s1 = eps[0].seasonId
        assertTrue(repo.moveEpisode(eps[2].id, s1, 0))
        assertEquals(listOf(eps[2].id, eps[0].id, eps[1].id), repo.episodes(showId).map { it.id })
        val s2 = repo.addSeason(showId)
        assertTrue(repo.moveEpisode(eps[0].id, s2, 0))
        assertEquals(s2, repo.episode(eps[0].id)!!.seasonId)
        assertTrue(repo.moveSeason(s2, 0))
        assertEquals(listOf(s2, s1), repo.seasons(showId).map { it.id })
        assertEquals(eps[0].id, repo.episodes(showId).first().id)
        assertFalse(repo.moveEpisode(eps[1].id, 9999, 0))
    }

    @Test
    fun orderSurvivesRenamingAndRenumbering() {
        val (showId, eps) = env.makeShow("Stable", 3)
        assertTrue(repo.moveEpisode(eps[2].id, eps[0].seasonId, 0))
        assertTrue(repo.updateEpisode(eps[2].id, "Opening", 99))
        assertEquals(eps[2].id, repo.episodes(showId).first().id)
        assertEquals(99, repo.episode(eps[2].id)!!.episodeNumber)
        assertFalse(repo.updateEpisode(eps[2].id, "  ", 1))
    }

    // ------------------------------------------------------------ deleting

    @Test
    fun removingEpisodesDropsTheirFileRowsAndProgressButNothingElse() {
        val (showId, eps) = env.makeShow("Remove", 3)
        repo.saveProgress(eps[0].file.id, 60_000, 600_000)
        assertTrue(repo.removeEpisodes(listOf(eps[0].id)))
        assertEquals(2, repo.episodes(showId).size)
        assertNull(repo.mediaFile(eps[0].file.id))
        assertEquals(0L, repo.progress(eps[0].file.id).positionMs)
        assertEquals(2, repo.allMediaFiles().size)
    }

    @Test
    fun deletingAShowCascadesAndCleansCollections() {
        val (showId, eps) = env.makeShow("Gone", 2)
        val col = repo.createCollection("Mine")
        assertTrue(repo.addToCollection(col, CollectionItemType.SHOW, showId))
        repo.saveProgress(eps[0].file.id, 30_000, 600_000)
        assertTrue(repo.deleteShow(showId))
        assertNull(repo.show(showId))
        assertTrue(repo.seasons(showId).isEmpty())
        assertTrue(repo.allMediaFiles().isEmpty())
        assertTrue(repo.collectionEntries(col).isEmpty())
        assertEquals(0, repo.collections().single().entryCount)
    }

    @Test
    fun deletingASeasonRemovesItsEpisodes() {
        val (showId, eps) = env.makeShow("Season gone", 2)
        val s2 = repo.addSeason(showId)
        repo.importEpisodes(showId, s2, ImportPlanner.planEpisodes(listOf(scannedFile("Season gone/x.mp4"))))
        assertEquals(3, repo.episodes(showId).size)
        assertTrue(repo.deleteSeason(s2))
        assertEquals(eps.map { it.id }, repo.episodes(showId).map { it.id })
        assertEquals(2, repo.allMediaFiles().size)
    }

    // ------------------------------------------------------------ movies and collections

    @Test
    fun moviesAreImportedWithCleanTitles() {
        val report = repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("The.Matrix.1999.1080p.BluRay.x264.mkv"), scannedFile("some_movie.mp4"))))
        assertEquals(2, report.added)
        assertEquals(listOf("Some Movie", "The Matrix 1999"), repo.movies().map { it.title })
        val again = repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("some_movie.mp4"))))
        assertEquals(0, again.added)
        assertTrue(again.duplicates.single().existingAs.startsWith("Movie: "))
    }

    @Test
    fun collectionsKeepTheirOrderAndRefuseDuplicates() {
        val (showId, _) = env.makeShow("Show", 1)
        repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("film.mkv"))))
        val movieId = repo.movies().single().id
        val col = repo.createCollection("Night")
        assertTrue(repo.addToCollection(col, CollectionItemType.MOVIE, movieId))
        assertTrue(repo.addToCollection(col, CollectionItemType.SHOW, showId))
        assertFalse("adding twice is refused", repo.addToCollection(col, CollectionItemType.SHOW, showId))
        var entries = repo.collectionEntries(col)
        assertEquals(listOf("Film", "Show"), entries.map { it.title })
        assertTrue(repo.setCollectionOrder(col, entries.reversed().map { it.id }))
        entries = repo.collectionEntries(col)
        assertEquals(listOf("Show", "Film"), entries.map { it.title })
        assertTrue(repo.removeFromCollection(entries[0].id, col))
        assertEquals(1, repo.collection(col)!!.entryCount)
        assertTrue(repo.deleteMovie(movieId))
        assertTrue("deleting the movie removes its entry", repo.collectionEntries(col).isEmpty())
    }

    @Test
    fun theQueueFollowsShowsMoviesAndCollections() {
        val (showId, eps) = env.makeShow("Queue", 2)
        repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("film.mkv"))))
        val movie = repo.movies().single()
        val col = repo.createCollection("Seq")
        repo.addToCollection(col, CollectionItemType.MOVIE, movie.id)
        repo.addToCollection(col, CollectionItemType.SHOW, showId)

        assertEquals(eps.map { it.file.id }, repo.buildQueue(QueueContext(ContextType.SHOW, showId)).map { it.mediaFileId })
        val movieQueue = repo.buildQueue(QueueContext(ContextType.MOVIE, movie.id))
        assertEquals(listOf(movie.file.id), movieQueue.map { it.mediaFileId })
        assertEquals(MediaKind.MOVIE, movieQueue.single().kind)
        val all = repo.buildQueue(QueueContext(ContextType.COLLECTION, col))
        assertEquals(listOf(movie.file.id) + eps.map { it.file.id }, all.map { it.mediaFileId })
        assertEquals("Queue — S1E1 · ep01.mp4".substringBefore(" — "), all[1].contextTitle)
    }

    // ------------------------------------------------------------ progress

    @Test
    fun everyEpisodeHasIndependentProgress() {
        val (_, eps) = env.makeShow("Independent", 3)
        repo.saveProgress(eps[0].file.id, 11_000, 100_000)
        repo.saveProgress(eps[1].file.id, 22_000, 200_000)
        repo.markCompleted(eps[2].file.id, 300_000)
        assertEquals(11_000L, repo.progress(eps[0].file.id).positionMs)
        assertEquals(22_000L, repo.progress(eps[1].file.id).positionMs)
        assertEquals(200_000L, repo.progress(eps[1].file.id).durationMs)
        assertFalse(repo.progress(eps[0].file.id).completed)
        assertTrue(repo.progress(eps[2].file.id).completed)
        assertEquals(1, repo.progress(eps[2].file.id).playCount)
        repo.markCompleted(eps[2].file.id, 0)
        assertEquals(2, repo.progress(eps[2].file.id).playCount)
        assertEquals("a zero duration never erases the known one", 300_000L, repo.progress(eps[2].file.id).durationMs)
    }

    @Test
    fun progressAndTracksSurviveARestart() {
        env.close()
        env = RepoEnv(inMemory = false)
        val (showId, eps) = env.makeShow("Persist", 2)
        env.repo.markCompleted(eps[0].file.id, 600_000)
        env.repo.saveProgress(eps[1].file.id, 321_000, 600_000)
        env.repo.saveTrackSelection(eps[1].file.id, 2, TRACK_OFF)
        assertEquals(TRACK_AUTO, env.repo.progress(eps[0].file.id).audioTrack)
        env.repo.setSetting("playback.autoPlayNext", "0")

        env.restart()
        val p = env.repo.progress(eps[1].file.id)
        assertEquals(321_000L, p.positionMs)
        assertEquals(2, p.audioTrack)
        assertEquals(TRACK_OFF, p.subtitleTrack)
        assertTrue(env.repo.progress(eps[0].file.id).completed)
        assertEquals("0", env.repo.allSettings()["playback.autoPlayNext"])
        val items = env.repo.continueWatching()
        assertEquals(1, items.size)
        assertEquals(eps[1].file.id, items[0].mediaFileId)
        assertEquals(321_000L, items[0].positionMs)
        assertEquals(showId, items[0].showId)
    }

    @Test
    fun watchedUnwatchedAndResetWork() {
        val (showId, eps) = env.makeShow("Manual", 3)
        repo.saveProgress(eps[0].file.id, 80_000, 600_000)
        assertTrue(repo.setWatched(listOf(eps[0].file.id, eps[1].file.id), true))
        assertTrue(repo.progress(eps[0].file.id).completed)
        assertEquals(0L, repo.progress(eps[0].file.id).positionMs)
        assertFalse(repo.progress(eps[2].file.id).completed)
        assertTrue(repo.setWatched(listOf(eps[1].file.id), false))
        assertFalse(repo.progress(eps[1].file.id).completed)

        repo.saveProgress(eps[2].file.id, 123_000, 600_000)
        assertTrue(repo.resetProgress(listOf(eps[0].file.id, eps[2].file.id)))
        for (i in listOf(0, 2)) {
            val p = repo.progress(eps[i].file.id)
            assertFalse(p.completed)
            assertEquals(0L, p.positionMs)
            assertEquals(0L, p.lastWatchedAt)
        }
        repo.markCompleted(eps[0].file.id, 1000)
        assertTrue(repo.resetShowProgress(showId))
        assertTrue(repo.episodes(showId).all { !it.progress.completed && it.progress.positionMs == 0L })
    }

    @Test
    fun resumeTargetFollowsTheHistoryAndTheConfiguredOrder() {
        val (showId, eps) = env.makeShow("Resume", 4)
        var r = repo.resumeTarget(showId)
        assertEquals(ResumeReason.NOT_STARTED, r.reason)
        assertEquals(eps[0].file.id, r.mediaFileId)

        repo.saveProgress(eps[0].file.id, 30_000, 600_000)
        r = repo.resumeTarget(showId)
        assertEquals(ResumeReason.IN_PROGRESS, r.reason)
        assertEquals(30_000L, r.positionMs)

        repo.markCompleted(eps[0].file.id, 600_000)
        assertEquals(eps[1].file.id, repo.resumeTarget(showId).mediaFileId)

        // the user decides episode 4 should come right after episode 1
        assertTrue(repo.moveEpisode(eps[3].id, eps[0].seasonId, 1))
        assertEquals(eps[3].file.id, repo.resumeTarget(showId).mediaFileId)

        for (e in eps) repo.markCompleted(e.file.id, 600_000)
        val done = repo.resumeTarget(showId)
        assertTrue(done.allCompleted())
        assertEquals(repo.episodes(showId).first().file.id, done.mediaFileId)
    }

    @Test
    fun manuallyWatchedEpisodesAreSkippedByResume() {
        val (showId, eps) = env.makeShow("Marked", 5)
        repo.setWatched(eps.take(3).map { it.file.id }, true)
        val r = repo.resumeTarget(showId)
        assertEquals(eps[3].file.id, r.mediaFileId)
        assertEquals(ResumeReason.NOT_STARTED, r.reason)
    }

    @Test
    fun continueWatchingListsShowsAndMoviesMostRecentFirst() {
        val (_, a) = env.makeShow("Show A", 3)
        val (_, b) = env.makeShow("Show B", 3)
        val (_, done) = env.makeShow("Show Done", 2)
        repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("cw/film.mkv"), scannedFile("cw/short.mkv"), scannedFile("cw/seen.mkv"))))
        fun movie(name: String) = repo.movies().first { it.file.displayName == name }

        repo.saveProgress(a[1].file.id, 40_000, 600_000) // oldest
        repo.markCompleted(done[0].file.id, 600_000) // show fully watched ->
        repo.markCompleted(done[1].file.id, 600_000) //   must not be listed
        repo.markCompleted(b[0].file.id, 600_000) // B: ep1 done, ep2 next
        repo.saveProgress(movie("film.mkv").file.id, 120_000, 5_400_000)
        repo.saveProgress(movie("short.mkv").file.id, 2_000, 5_400_000) // < 5 s: not started
        repo.markCompleted(movie("seen.mkv").file.id, 5_400_000) // finished: not listed

        val items = repo.continueWatching()
        assertEquals(3, items.size)
        assertEquals(MediaKind.MOVIE, items[0].kind) // newest first: film, Show B (next episode), Show A (in progress)
        assertEquals("Film", items[0].title)
        assertEquals(120_000L, items[0].positionMs)
        assertEquals(120_000.0 / 5_400_000.0, items[0].fraction(), 1e-9)
        assertEquals("Show B", items[1].title)
        assertEquals(b[1].file.id, items[1].mediaFileId)
        assertEquals(0L, items[1].positionMs)
        assertEquals("Show A", items[2].title)
        assertEquals(a[1].file.id, items[2].mediaFileId)
        assertEquals(40_000L, items[2].positionMs)
        assertTrue(items[2].lastWatchedAt > 0)
    }

    @Test
    fun openingAnItemAndLeavingCreatesNoHistory() {
        val (_, eps) = env.makeShow("Accident", 1)
        assertEquals(0L, repo.progress(eps[0].file.id).lastWatchedAt)
        assertTrue(repo.continueWatching().isEmpty())
    }

    @Test
    fun recentlyAddedMixesShowsAndMovies() {
        env.makeShow("Older show", 1)
        repo.importMovies(ImportPlanner.planMovies(listOf(scannedFile("new_movie.mkv"))))
        val cards = repo.recentlyAdded(10)
        assertEquals(2, cards.size)
        assertEquals(io.github.victorhmrod.localflix.core.CardKind.MOVIE, cards[0].kind) // added last
        assertEquals(io.github.victorhmrod.localflix.core.CardKind.SHOW, cards[1].kind)
    }

    @Test
    fun durationIsStoredOnTheFile() {
        val (_, eps) = env.makeShow("Dur", 1)
        assertEquals(0L, repo.mediaFile(eps[0].file.id)!!.durationMs)
        repo.setMediaDuration(eps[0].file.id, 421_000)
        repo.setMediaDuration(eps[0].file.id, 0) // ignored
        assertEquals(421_000L, repo.mediaFile(eps[0].file.id)!!.durationMs)
    }

    // ------------------------------------------------------------ file recovery

    @Test
    fun verificationFlagsMissingFilesAndClearsThemAgain() {
        val (showId, eps) = env.makeShow("Check", 3)
        val files = repo.mediaFilesOfShow(showId)
        assertEquals(3, files.size)

        val (checked, missing) = repo.applyVerification(listOf(files[0] to true, files[1] to false, files[2] to true))
        assertEquals(3, checked)
        assertEquals(2, missing)
        assertEquals(setOf(eps[0].file.id, eps[2].file.id), repo.missingFiles().map { it.id }.toSet())
        assertTrue(repo.episodes(showId)[0].file.missing)

        // they come back
        assertEquals(0, repo.applyVerification(listOf(files[0] to false, files[2] to false)).second)
        assertTrue(repo.missingFiles().isEmpty())
    }

    @Test
    fun verificationIgnoresEntriesRelinkedMeanwhile() {
        val (_, eps) = env.makeShow("Race", 1)
        val snapshot = repo.mediaFile(eps[0].file.id)!!
        assertTrue(repo.relinkFile(snapshot.id, scannedFile("Race/moved.mp4", docKey = "test/elsewhere/moved.mp4", uri = "content://test/elsewhere/moved.mp4")).ok)
        repo.applyVerification(listOf(snapshot to true)) // the probe was about the old location
        assertFalse(repo.mediaFile(snapshot.id)!!.missing)
    }

    @Test
    fun relinkKeepsIdentityProgressAndPlaceInTheOrder() {
        val (showId, eps) = env.makeShow("Relink", 3)
        repo.saveProgress(eps[1].file.id, 99_000, 600_000)
        repo.setMissing(eps[1].file.id, true)
        assertTrue(repo.moveEpisode(eps[1].id, eps[0].seasonId, 0))

        val result = repo.relinkFile(eps[1].file.id, scannedFile("Moved/renamed episode.mp4", docKey = "test/Moved/renamed episode.mp4", uri = "content://test/Moved/renamed episode.mp4", size = 4321))
        assertTrue(result.ok)
        val e = repo.episode(eps[1].id)!!
        assertEquals(eps[1].file.id, e.file.id)
        assertEquals("content://test/Moved/renamed episode.mp4", e.file.uri)
        assertEquals("renamed episode.mp4", e.file.displayName)
        assertEquals(4321L, e.file.sizeBytes)
        assertFalse(e.file.missing)
        assertEquals(99_000L, e.progress.positionMs)
        assertEquals(eps[1].id, repo.episodes(showId).first().id)
    }

    @Test
    fun relinkRefusesAFileThatIsAlreadyInTheLibrary() {
        val (_, eps) = env.makeShow("Clash", 2)
        val other = repo.mediaFile(eps[0].file.id)!!
        val result = repo.relinkFile(eps[1].file.id, scannedFile("Clash/ep01.mp4", uri = other.uri, docKey = other.docKey))
        assertFalse(result.ok)
        assertTrue(result.error, result.error.contains("already in the library"))
        assertEquals(eps[1].file.uri, repo.mediaFile(eps[1].file.id)!!.uri)
        assertFalse(repo.relinkFile(424242, scannedFile("x.mp4")).ok)
    }

    // ------------------------------------------------------------ settings

    @Test
    fun settingsAreStoredAndReplaced() {
        assertTrue(repo.allSettings().isEmpty())
        repo.setSetting("a", "1")
        repo.setSetting("a", "2")
        repo.setSetting("b", "x")
        assertEquals(mapOf("a" to "2", "b" to "x"), repo.allSettings())
        assertNotNull(repo)
    }
}
