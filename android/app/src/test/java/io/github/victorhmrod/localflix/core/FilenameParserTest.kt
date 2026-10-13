package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Test

/** Port of tests/tst_parser.cpp: the same cases must give the same answers as the desktop app. */
class FilenameParserTest {
    private data class Row(val label: String, val file: String, val season: Int, val episode: Int, val title: String)

    @Test
    fun parsesEpisodeMarkers() {
        val rows = listOf(
            Row("sxxeyy", "Breaking.Bad.S02E05.Breakage.720p.mkv", 2, 5, "Breakage"),
            Row("sxxeyy dash", "Show - S01E02 - The Pilot.mp4", 1, 2, "The Pilot"),
            Row("lowercase", "show.s3e12.mkv", 3, 12, "Episode 12"),
            Row("1x02", "Seinfeld 4x03 The Pitch.avi", 4, 3, "The Pitch"),
            Row("episode word", "downloaded_episode_04.mp4", -1, 4, "Episode 4"),
            Row("episode word 2", "episode_01.avi", -1, 1, "Episode 1"),
            Row("Ep", "Naruto Ep107.mkv", -1, 107, "Episode 107"),
            Row("E only", "Show E09.mkv", -1, 9, "Episode 9"),
            Row("anime dash number", "[SubGroup] My Anime - 05 [1080p].mkv", -1, 5, "Episode 5"),
            Row("anime dash number v2", "My Anime - 12v2.mkv", -1, 12, "Episode 12"),
            Row("trailing number", "video_27.mkv", -1, 27, "Video 27"),
            Row("no number", "anime_final.mkv", -1, 0, "Anime Final"),
            Row("resolution is not an episode", "movie_1080p.mkv", -1, 0, "Movie"),
            Row("year is not an episode", "Documentary 2019.mkv", -1, 0, "Documentary 2019"),
            Row("1920x1080 is not 19x20", "clip 1920x1080.mkv", -1, 0, "Clip 1920x1080"),
            Row("portuguese", "Serie Episodio 12.mkv", -1, 12, "Episode 12"),
            Row("sxxeyy no space title", "S01E01Pilot.mkv", 1, 1, "Pilot"),
        )
        for (r in rows) {
            val p = parseFileName(r.file)
            assertEquals("${r.label}: season", r.season, p.season)
            assertEquals("${r.label}: episode", r.episode, p.episode)
            assertEquals("${r.label}: title", r.title, p.title)
        }
    }

    @Test
    fun seasonFromFolders() {
        val cases = listOf(
            "Season 2" to 2, "season 03" to 3, "Season.4" to 4, "S5" to 5, "Temporada 6" to 6, "2nd Season" to 2,
            "Specials" to 0, "Extras" to 0, "Downloads" to -1, "Breaking Bad (2008)" to -1, "" to -1,
        )
        for ((folder, season) in cases) assertEquals("folder '$folder'", season, seasonFromFolderName(folder))
    }

    @Test
    fun naturalOrderingSortsNumbersNumerically() {
        val names = listOf("ep10.mkv", "ep2.mkv", "Ep1.mkv", "ep20.mkv")
        assertEquals(listOf("Ep1.mkv", "ep2.mkv", "ep10.mkv", "ep20.mkv"), names.sortedWith { a, b -> naturalCompare(a, b) })
        assertEquals(0, naturalCompare("A", "a"))
        assertEquals(0, naturalCompare("ep007", "ep7"))
    }

    @Test
    fun prettifyTitleCleansReleaseNoise() {
        assertEquals("The Matrix 1999", prettifyTitle("The.Matrix.1999.1080p.BluRay.x264"))
        assertEquals("Some Movie", prettifyTitle("some_movie"))
        assertEquals("Film Name", prettifyTitle("[Group] Film Name (2020) [720p]"))
        assertEquals("Already Fine", prettifyTitle("Already Fine"))
    }

    @Test
    fun baseNameAndExtension() {
        assertEquals("a.b.c", baseName("a.b.c.mkv"))
        assertEquals("noext", baseName("noext"))
        assertEquals("mkv", extensionOf("a.b.C.MKV".lowercase()))
        assertEquals("", extensionOf("noext"))
    }

    @Test
    fun clockAndLabels() {
        assertEquals("1:23", formatClock(83_000))
        assertEquals("1:02:05", formatClock(3_725_000))
        assertEquals("0:00", formatClock(-5))
        assertEquals("S1E3 · Pilot", episodeLabel(1, 3, "Pilot"))
        assertEquals("E3", episodeLabel(0, 3, ""))
        assertEquals("Pilot", episodeLabel(2, 0, "Pilot"))
    }
}
