package io.github.victorhmrod.localflix.core

import java.util.Locale

enum class SegmentKind { INTRO, ENDING }

/** What a chapter title says about the chapter. */
enum class ChapterRole {
    OTHER,
    INTRO, // "Opening", "OP1", "Intro", "Abertura"
    ENDING, // "Ending", "ED", "Credits", "Créditos", "Encerramento"
    PREVIEW, // "Preview", "Next episode": only meaningful straight after an ending
}

/** One chapter of the file being played, as embedded in the container. */
data class ChapterInfo(val title: String, val startMs: Long)

/**
 * A stretch of the playing file that can be skipped: the opening or the ending (credits) found through the file's
 * chapters.
 */
data class SkipSegment(
    val kind: SegmentKind = SegmentKind.INTRO,
    val startMs: Long = 0,
    val endMs: Long = 0, // exclusive; endMs <= startMs means "no segment"
    val toEnd: Boolean = false, // runs to the end of the file: skipping it ends the playback
    val title: String = "", // title of the first chapter of the segment
) {
    fun valid() = endMs > startMs

    fun contains(ms: Long) = ms >= startMs && ms < endMs

    val label: String get() = if (kind == SegmentKind.INTRO) "Skip Intro" else "Skip Ending"
    val noun: String get() = if (kind == SegmentKind.INTRO) "intro" else "ending"
}

/** Segments shorter than this are not worth a button (nor an automatic jump). */
const val MIN_SKIP_SEGMENT_MS = 2000L

private val introWords = setOf("opening", "intro", "abertura", "ncop")
private val endingWords = setOf("ending", "credits", "creditos", "créditos", "outro", "encerramento", "nced")
private val previewWords = setOf("preview", "previa", "prévia", "trailer", "next", "proximo", "próximo")

// Words that may accompany a keyword without turning the chapter into content: "Opening Theme", "End Credits",
// "Ending 2", "Next Episode".
private val fillerWords = setOf(
    "the", "theme", "song", "sequence", "part", "end", "closing", "final", "of", "de", "do", "da",
    "tema", "musica", "música", "episode", "episodio", "episódio", "ep",
)

private const val SEPARATORS = ":([{)]}|–—"

private fun isHeadTerminator(s: String, i: Int): Boolean {
    val c = s[i]
    if (c in SEPARATORS) return true
    // a spaced hyphen separates a name from a description ("OP - Song"); a plain one joins words ("Post-Credits")
    return c == '-' && i > 0 && i + 1 < s.length && s[i - 1].isWhitespace() && s[i + 1].isWhitespace()
}

/** The part of the title before the first separator, e.g. "Opening" of "Opening: Song name". */
private fun titleHead(title: String): String {
    var start = 0
    while (start < title.length && (title[start].isWhitespace() || title[start] in "([{")) start++ // "[OP] Song"
    var end = start
    while (end < title.length && !isHeadTerminator(title, end)) end++
    return title.substring(start, end)
}

private fun words(head: String): List<String> {
    val out = ArrayList<String>()
    val current = StringBuilder()
    fun flush() {
        if (current.isNotEmpty()) out += current.toString()
        current.clear()
    }
    for (c in head.lowercase(Locale.ROOT)) {
        if (c.isLetterOrDigit() || c == '\'' || c == '’') current.append(c) else flush()
    }
    flush()
    return out
}

private fun isNumber(w: String) = w.isNotEmpty() && w.all { it.isDigit() }

/** "op1", "ed2": the abbreviation with its number glued to it ("op" itself is not a number, so it is not one of these). */
private fun isAbbreviation(w: String, abbr: String) = w.startsWith(abbr) && isNumber(w.substring(abbr.length))

private fun isAbbreviationWord(w: String, abbr: String) = w == abbr || isAbbreviation(w, abbr)

/**
 * Decides from the chapter title alone. Whole words only ("OP" is an opening, "Operation" is not), the part after
 * a separator is ignored ("Opening: Song name"), and any other word in the title ("Opening Night", "Post-Credits
 * Scene") makes it ordinary content. Port of classifyChapterTitle (desktop).
 */
fun classifyChapterTitle(title: String): ChapterRole {
    val ws = words(titleHead(title))
    if (ws.isEmpty()) return ChapterRole.OTHER

    var intro = false
    var ending = false
    var preview = false
    for (w in ws) {
        if (isNumber(w) || w in fillerWords) continue
        when {
            w in introWords || isAbbreviationWord(w, "op") -> intro = true
            w in endingWords || isAbbreviationWord(w, "ed") -> ending = true
            w in previewWords -> preview = true
            else -> return ChapterRole.OTHER // some other word: this is content ("Opening Night")
        }
    }
    // Mixed keywords ("Opening Ending") are not something we understand.
    if (listOf(intro, ending, preview).count { it } != 1) return ChapterRole.OTHER
    return if (intro) ChapterRole.INTRO else if (ending) ChapterRole.ENDING else ChapterRole.PREVIEW
}

/**
 * The chapters as the engine reports them, cleaned up: the same chapter often arrives once per track of the file,
 * so exact repeats are dropped, and the rest is put in playback order. Negative starts count as 0.
 */
fun normalizeChapters(raw: List<ChapterInfo>): List<ChapterInfo> =
    raw.map { if (it.startMs < 0) it.copy(startMs = 0) else it }.distinct().sortedBy { it.startMs }

/**
 * Opening and ending segments of a file. A segment starts at a chapter whose title says so and lasts until the
 * next chapter that does not belong to it (neighbouring chapters of the same role are merged; a "Preview" right
 * after an ending is part of the ending). It ends at the start of the next other chapter, so a post-credits scene
 * that has its own chapter is kept, or at the end of the file ([SkipSegment.toEnd]). `durationMs <= 0` (unknown)
 * drops a last segment. Port of detectSkipSegments (desktop).
 */
fun detectSkipSegments(input: List<ChapterInfo>, durationMs: Long): List<SkipSegment> {
    val chapters = input.sortedBy { it.startMs } // stable
    val roles = chapters.map { classifyChapterTitle(it.title) }
    val n = chapters.size

    val out = ArrayList<SkipSegment>()
    var i = 0
    while (i < n) {
        val role = roles[i]
        if (role != ChapterRole.INTRO && role != ChapterRole.ENDING) {
            i++
            continue
        }

        var next = i + 1
        while (next < n && (roles[next] == role || (role == ChapterRole.ENDING && roles[next] == ChapterRole.PREVIEW))) next++

        val toEnd = next >= n
        var endMs = if (toEnd) durationMs else maxOf(0L, chapters[next].startMs)
        var reachesEnd = toEnd
        if (durationMs > 0 && endMs >= durationMs) {
            endMs = durationMs
            reachesEnd = true
        }
        val seg = SkipSegment(
            kind = if (role == ChapterRole.INTRO) SegmentKind.INTRO else SegmentKind.ENDING,
            startMs = maxOf(0L, chapters[i].startMs),
            endMs = endMs,
            toEnd = reachesEnd,
            title = chapters[i].title,
        )
        i = next

        if (seg.endMs - seg.startMs >= MIN_SKIP_SEGMENT_MS) out += seg
    }
    return out
}
