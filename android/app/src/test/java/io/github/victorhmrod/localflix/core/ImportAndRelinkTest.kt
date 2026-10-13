package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ImportAndRelinkTest {
    private fun scanned(
        relative: String,
        root: String = "Library",
        docKey: String = "ext/primary:$root/$relative",
        size: Long = 1000,
    ): ScannedFile {
        val parts = relative.split('/')
        val chain = parts.dropLast(1).reversed() + root
        return ScannedFile(
            uri = "content://ext/tree/x/document/${docKey.substringAfter('/')}",
            docKey = docKey,
            displayName = parts.last(),
            relativePath = relative,
            folderChain = chain,
            sizeBytes = size,
            mtimeMs = 0,
            treeUri = "content://ext/tree/x",
        )
    }

    // ------------------------------------------------------------ planning an import

    @Test
    fun episodesAreOrderedByNumberWithinTheirSeasons() {
        val files = listOf(
            scanned("Show/Season 2/Show S02E02.mkv"),
            scanned("Show/Season 1/Show S01E10.mkv"),
            scanned("Show/Season 1/Show S01E02.mkv"),
            scanned("Show/Season 2/Show S02E01.mkv"),
            scanned("Show/Season 1/Show S01E01.mkv"),
        )
        val plan = ImportPlanner.planEpisodes(files)
        assertEquals(
            listOf("S01E01", "S01E02", "S01E10", "S02E01", "S02E02"),
            plan.map { Regex("S\\d\\dE\\d\\d").find(it.file.displayName)!!.value },
        )
        assertEquals(listOf(1, 1, 1, 2, 2), plan.map { it.seasonNumber })
        assertEquals(listOf(0, 1, 2, 3, 4), plan.map { it.groupIndex })
    }

    @Test
    fun seasonIsTakenFromTheNearestSeasonFolderWhenNamesDoNotSayIt() {
        val files = listOf(
            scanned("Anime/Season 3/Disc 1/ep_01.mkv"),
            scanned("Anime/Season 3/Extras/making_of.mkv"),
            scanned("Anime/Specials/ova.mkv"),
            scanned("Anime/loose_02.mkv"),
        )
        val bySeason = ImportPlanner.planEpisodes(files).associate { it.file.displayName to it.seasonNumber }
        assertEquals(3, bySeason["ep_01.mkv"])
        assertEquals(0, bySeason["making_of.mkv"]) // extras are specials even inside a season folder (as on the desktop)
        assertEquals(0, bySeason["ova.mkv"])
        assertEquals(-1, bySeason["loose_02.mkv"])
    }

    @Test
    fun seasonDetectionCanBeSwitchedOff() {
        val files = listOf(scanned("Show/Season 2/Show S02E01.mkv"))
        val plan = ImportPlanner.planEpisodes(files, PlanOptions(detectSeasonFromNames = false, detectSeasonFromFolders = false))
        assertEquals(-1, plan.single().seasonNumber)
    }

    @Test
    fun withoutEpisodeNumbersFilesFollowNaturalNameOrder() {
        val files = listOf(scanned("Show/clip10.mp4"), scanned("Show/clip2.mp4"), scanned("Show/clip1.mp4"))
        val names = ImportPlanner.planEpisodes(files).map { it.file.displayName }
        assertEquals(listOf("clip1.mp4", "clip2.mp4", "clip10.mp4"), names)
    }

    @Test
    fun moviesGetCleanTitlesInNaturalOrder() {
        val files = listOf(scanned("The.Matrix.1999.1080p.BluRay.x264.mkv"), scanned("some_movie.mp4"))
        val plan = ImportPlanner.planMovies(files)
        assertEquals(listOf("Some Movie", "The Matrix 1999"), plan.map { it.title })
    }

    @Test
    fun videoDetectionUsesMimeTypeOrExtension() {
        assertTrue(ImportPlanner.isVideo("a.mkv", "application/octet-stream"))
        assertTrue(ImportPlanner.isVideo("a.bin", "video/x-matroska"))
        assertTrue(ImportPlanner.isVideo("A.MP4", null))
        assertFalse(ImportPlanner.isVideo("notes.txt", "text/plain"))
        assertFalse(ImportPlanner.isVideo("cover.jpg", "image/jpeg"))
    }

    // ------------------------------------------------------------ reconnecting moved files

    private fun missing(id: Long, relative: String, root: String = "Library", size: Long = 1000) = MissingRef(
        mediaFileId = id,
        docKey = "ext/primary:$root/$relative",
        relativePath = relative,
        displayName = relative.substringAfterLast('/'),
        sizeBytes = size,
    )

    @Test
    fun aMovedFolderTreeIsReconnectedByNameAndTrailingPath() {
        val miss = listOf(missing(1, "Show/S1/ep1.mkv"), missing(2, "Show/S1/ep2.mkv"), missing(3, "Show/S2/ep1.mkv"))
        // the user moved "Library/Show" to "Backup/2024/Show"
        val found = listOf("Show/S1/ep1.mkv", "Show/S1/ep2.mkv", "Show/S2/ep1.mkv").map {
            scanned(it, docKey = "ext/primary:Backup/2024/$it")
        }
        val proposals = RelinkMatcher.propose(miss, found, emptySet())
        assertEquals(3, proposals.size)
        // ep1.mkv exists in S1 and S2: each entry must get the one with the matching folder, not the other
        assertEquals("ext/primary:Backup/2024/Show/S1/ep1.mkv", proposals.first { it.mediaFileId == 1L }.newFile.docKey)
        assertEquals("ext/primary:Backup/2024/Show/S2/ep1.mkv", proposals.first { it.mediaFileId == 3L }.newFile.docKey)
        assertEquals("ext/primary:Backup/2024/Show/S1/ep2.mkv", proposals.first { it.mediaFileId == 2L }.newFile.docKey)
    }

    @Test
    fun ambiguousCandidatesAreLeftForTheUser() {
        val miss = listOf(missing(1, "old/ep1.mkv", size = 5))
        val found = listOf(
            scanned("a/ep1.mkv", docKey = "ext/primary:new/a/ep1.mkv", size = 7),
            scanned("b/ep1.mkv", docKey = "ext/primary:new/b/ep1.mkv", size = 9),
        )
        assertTrue(RelinkMatcher.propose(miss, found, emptySet()).isEmpty())
    }

    @Test
    fun fileSizeBreaksATieBetweenEquallyNamedCandidates() {
        val miss = listOf(missing(1, "old/ep1.mkv", size = 5))
        val found = listOf(
            scanned("a/ep1.mkv", docKey = "ext/primary:new/a/ep1.mkv", size = 7),
            scanned("b/ep1.mkv", docKey = "ext/primary:new/b/ep1.mkv", size = 5),
        )
        val p = RelinkMatcher.propose(miss, found, emptySet()).single()
        assertEquals("ext/primary:new/b/ep1.mkv", p.newFile.docKey)
    }

    @Test
    fun filesThatAreAlreadyInTheLibraryAreNotUsedAsReplacements() {
        val miss = listOf(missing(1, "old/ep1.mkv"))
        val found = listOf(scanned("x/ep1.mkv", docKey = "ext/primary:new/x/ep1.mkv"))
        assertTrue(RelinkMatcher.propose(miss, found, setOf("ext/primary:new/x/ep1.mkv")).isEmpty())
    }

    @Test
    fun theSameDocumentComingBackIsRecognised() {
        // after reinstalling, granting the same folder again yields the very same document
        val miss = listOf(missing(1, "Show/ep1.mkv"))
        val found = listOf(scanned("Show/ep1.mkv"))
        val p = RelinkMatcher.propose(miss, found, setOf(miss[0].docKey)).single()
        assertEquals(1L, p.mediaFileId)
        assertEquals(miss[0].docKey, p.newFile.docKey)
    }

    @Test
    fun differentFileNamesNeverMatch() {
        val miss = listOf(missing(1, "Show/ep1.mkv"))
        val found = listOf(scanned("Show/ep1-renamed.mkv"))
        assertTrue(RelinkMatcher.propose(miss, found, emptySet()).isEmpty())
    }

    @Test
    fun oneFileCannotReplaceTwoEntries() {
        val miss = listOf(missing(1, "a/ep1.mkv"), missing(2, "b/ep1.mkv"))
        val found = listOf(scanned("a/ep1.mkv", docKey = "ext/primary:n/a/ep1.mkv"))
        val proposals = RelinkMatcher.propose(miss, found, emptySet())
        assertEquals(1, proposals.size)
        assertEquals(1L, proposals[0].mediaFileId) // the entry whose folder matches wins
    }

    @Test
    fun componentsPreferDocumentIdPathsAndFallBackToRelativePath() {
        assertEquals(listOf("primary", "Movies", "ep.mkv"), RelinkMatcher.components("ext/primary:Movies/ep.mkv", "ep.mkv", "ep.mkv"))
        assertEquals(listOf("Show", "ep.mkv"), RelinkMatcher.components("dl/msf:1234", "Show/ep.mkv", "ep.mkv"))
        assertEquals(listOf("ep.mkv"), RelinkMatcher.components("", "", "ep.mkv"))
    }
}
