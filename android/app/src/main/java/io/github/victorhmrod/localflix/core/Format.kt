package io.github.victorhmrod.localflix.core

import java.util.Locale

/** "S1E3 · Title", "E3 · Title" or just the title. */
fun episodeLabel(seasonNumber: Int, episodeNumber: Int, title: String): String {
    val prefix = when {
        seasonNumber > 0 && episodeNumber > 0 -> "S${seasonNumber}E$episodeNumber"
        episodeNumber > 0 -> "E$episodeNumber"
        else -> ""
    }
    if (prefix.isEmpty()) return title
    if (title.isEmpty()) return prefix
    return "$prefix · $title"
}

fun episodeLabel(e: Episode): String = episodeLabel(e.seasonNumber, e.episodeNumber, e.title)

/** 83000 -> "1:23", 3_725_000 -> "1:02:05". */
fun formatClock(ms: Long): String {
    val total = (if (ms < 0) 0 else ms) / 1000
    val h = total / 3600
    val m = (total % 3600) / 60
    val s = total % 60
    return if (h > 0) String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s) else String.format(Locale.ROOT, "%d:%02d", m, s)
}

fun formatFileSize(bytes: Long): String {
    val kb = 1024.0
    val b = bytes.toDouble()
    return when {
        b >= kb * kb * kb -> String.format(Locale.ROOT, "%.1f GB", b / (kb * kb * kb))
        b >= kb * kb -> String.format(Locale.ROOT, "%.0f MB", b / (kb * kb))
        b >= kb -> String.format(Locale.ROOT, "%.0f KB", b / kb)
        else -> "$bytes B"
    }
}
