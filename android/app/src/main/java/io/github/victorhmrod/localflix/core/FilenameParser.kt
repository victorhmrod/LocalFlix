package io.github.victorhmrod.localflix.core

/** What a file name tells us about an episode. Mirrors the desktop parser (src/core/FilenameParser.cpp). */
data class ParsedName(
    val title: String = "", // cleaned display title
    val season: Int = -1, // -1 = not found in the file name
    val episode: Int = 0, // 0 = not found
    val episodeMarkerFound: Boolean = false, // true for SxxEyy / 1x02 / "Episode 4" style markers
)

private val IC = setOf(RegexOption.IGNORE_CASE)

private val reSxxEyy = Regex("""(?<![A-Za-z0-9])S(\d{1,2})[ ._-]*E(\d{1,4})(?:[ ._-]*E\d{1,4})*(?!\d)""", IC)
private val reNxNN = Regex("""(?<![A-Za-z0-9])(\d{1,2})x(\d{2,3})(?![A-Za-z0-9])""", IC)
private val reEpisodeWord =
    Regex("""(?<![A-Za-z])(?:episode|episodio|episódio|ep|cap|capitulo|capítulo|folge)[ ._-]*(\d{1,4})(?!\d)""", IC)
private val reBareE = Regex("""(?<![A-Za-z0-9])E(\d{2,3})(?![A-Za-z0-9])""", IC)
private val reDashNumber = Regex("""[ _.]-[ _.]*(\d{1,4})(?:v\d)?(?![\dA-Za-z])""", IC)
private val reBrackets = Regex("""\[[^\]]*]|\([^)]*\)|\{[^}]*}""")
private val reNoiseTokens = Regex(
    """(?<![A-Za-z0-9])(?:2160p|1080p|1080i|720p|576p|480p|4k|uhd|x264|x265|h\.?264|h\.?265|hevc|avc|xvid|divx|""" +
        """bluray|blu-ray|bdrip|brrip|dvdrip|webrip|web-dl|webdl|hdtv|hdrip|remux|10bit|8bit|aac|ac3|dts|flac|""" +
        """dual[ ._-]?audio|multi|proper|repack|extended|uncut)(?![A-Za-z0-9])""",
    IC,
)
private val reDotSeparator = Regex("""(?<!\d)\.|\.(?!\d)""")
private val reEdge = Regex("""^[\s\-–—:.]+|[\s\-–—:.]+$""")
private val reWhitespace = Regex("""\s+""")
private val reResolutionPair = Regex("""(?<!\d)\d{3,4}[xX]\d{3,4}(?!\d)""")
private val reStandaloneDigits = Regex("""(?<!\d)(\d{1,4})(?!\d)""")

private fun simplified(s: String) = s.trim().replace(reWhitespace, " ")

private fun stripNoise(input: String): String {
    var s = input.replace(reBrackets, "")
    s = s.replace(reNoiseTokens, "")
    s = s.replace('_', ' ')
    // Dots between words are separators, but keep ones that are part of "1.5".
    s = s.replace(reDotSeparator, " ")
    s = simplified(s)
    // Trim leading/trailing separators such as " - ".
    return s.replace(reEdge, "")
}

/** "anime_final" reads better as "Anime Final"; names that already contain capitals are left alone. */
private fun capitalizeIfPlain(s: String): String {
    if (s.isEmpty() || s.any { it.isUpperCase() }) return s
    val out = StringBuilder(s.length)
    var startOfWord = true
    for (c in s) {
        out.append(if (startOfWord) c.uppercaseChar() else c)
        startOfWord = c.isWhitespace()
    }
    return out.toString()
}

private fun plausibleEpisodeNumber(n: Int) = n in 1..9999

/** "my_movie.2019.1080p.x264" -> "My Movie 2019". */
fun prettifyTitle(baseName: String): String = capitalizeIfPlain(stripNoise(baseName))

/** File name without directory and without its last extension. */
fun baseName(fileName: String): String {
    val dot = fileName.lastIndexOf('.')
    return if (dot > 0) fileName.substring(0, dot) else fileName
}

/** File extension in lower case, without the dot ("" when there is none). */
fun extensionOf(fileName: String): String {
    val dot = fileName.lastIndexOf('.')
    return if (dot > 0 && dot < fileName.length - 1) fileName.substring(dot + 1).lowercase() else ""
}

/**
 * Extracts season/episode numbers and a display title from a file name (without directory). Understands
 * S01E02, 1x02, "Episode 4", "Ep04", "Show - 05 [1080p]" and, as a last resort, a trailing number.
 */
fun parseFileName(fileName: String): ParsedName {
    val base = baseName(fileName).ifEmpty { fileName }

    // The remainder after a marker is usually the episode title; the part before it is usually the show name.
    fun finish(season: Int, episode: Int, m: MatchResult): ParsedName {
        val cleaned = capitalizeIfPlain(stripNoise(base.substring(m.range.last + 1)))
        return ParsedName(
            title = cleaned.ifEmpty { "Episode $episode" },
            season = season,
            episode = episode,
            episodeMarkerFound = true,
        )
    }

    reSxxEyy.find(base)?.let { return finish(it.groupValues[1].toInt(), it.groupValues[2].toInt(), it) }
    reNxNN.find(base)?.let { return finish(it.groupValues[1].toInt(), it.groupValues[2].toInt(), it) }
    reEpisodeWord.find(base)?.let { return finish(-1, it.groupValues[1].toInt(), it) }
    reBareE.find(base)?.let { return finish(-1, it.groupValues[1].toInt(), it) }
    reDashNumber.find(base)?.let { return finish(-1, it.groupValues[1].toInt(), it) }

    // Last resort: the final standalone number that is not a year/resolution.
    val title = capitalizeIfPlain(stripNoise(base)).ifEmpty { base }
    // Resolutions ("1080p", "1920x1080") and codec tags are not episode numbers.
    val cleaned = base.replace(reBrackets, "").replace(reResolutionPair, "").replace(reNoiseTokens, "")
    var found = 0
    for (m in reStandaloneDigits.findAll(cleaned)) {
        val digits = m.groupValues[1]
        val n = digits.toInt()
        val yearLike = digits.length == 4 && n in 1900..2100
        if (!yearLike && plausibleEpisodeNumber(n)) found = n
    }
    return ParsedName(title = title, season = -1, episode = found)
}

private val reSeasonWords = Regex("""^(?:season|temporada|saison|staffel|series)[ ._-]*(\d{1,2})$""", IC)
private val reSeasonShort = Regex("""^S(\d{1,2})$""", IC)
private val reSeasonOrdinal = Regex("""^(\d{1,2})(?:st|nd|rd|th|ª|º|a|°)?[ ._-]*(?:season|temporada)$""", IC)
private val reSpecials = Regex("""^(?:specials?|extras?|ovas?|especiais|bonus)$""", IC)

/** Season hint from a folder name: "Season 2", "Temporada 03", "S2", "Specials" (= 0). -1 when there is none. */
fun seasonFromFolderName(folderName: String): Int {
    val name = folderName.trim()
    reSeasonWords.find(name)?.let { return it.groupValues[1].toInt() }
    reSeasonShort.find(name)?.let { return it.groupValues[1].toInt() }
    reSeasonOrdinal.find(name)?.let { return it.groupValues[1].toInt() }
    if (reSpecials.containsMatchIn(name)) return 0
    return -1
}

/** Natural, case-insensitive ordering: "ep2" < "ep10". */
fun naturalCompare(a: String, b: String): Int {
    val na = a.length
    val nb = b.length
    var i = 0
    var j = 0
    while (i < na && j < nb) {
        val ca = a[i]
        val cb = b[j]
        if (ca.isDigit() && cb.isDigit()) {
            var si = i
            var sj = j
            while (si < na && a[si] == '0') si++ // skip leading zeros
            while (sj < nb && b[sj] == '0') sj++
            var ei = si
            var ej = sj
            while (ei < na && a[ei].isDigit()) ei++
            while (ej < nb && b[ej].isDigit()) ej++
            val lenA = ei - si
            val lenB = ej - sj
            if (lenA != lenB) return if (lenA < lenB) -1 else 1
            for (k in 0 until lenA) {
                val da = a[si + k]
                val db = b[sj + k]
                if (da != db) return if (da < db) -1 else 1
            }
            i = ei
            j = ej
            continue
        }
        val fa = ca.lowercaseChar()
        val fb = cb.lowercaseChar()
        if (fa != fb) return if (fa < fb) -1 else 1
        i++
        j++
    }
    if (i < na) return 1
    if (j < nb) return -1
    return 0
}
