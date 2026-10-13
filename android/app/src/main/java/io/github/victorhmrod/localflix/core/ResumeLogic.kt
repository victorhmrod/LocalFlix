package io.github.victorhmrod.localflix.core

private fun targetFor(e: Episode, reason: ResumeReason, useSavedPosition: Boolean) = ResumeTarget(
    reason = reason,
    episodeId = e.id,
    mediaFileId = e.file.id,
    positionMs = if (useSavedPosition) e.progress.resumePositionMs() else 0,
)

private fun firstUnfinished(eps: List<Episode>, from: Int, to: Int): Int {
    for (i in from until to) if (!eps[i].progress.completed) return i
    return -1
}

/**
 * "What do I watch next" for a show whose episodes are given in the user's playback order: the episode watched
 * most recently decides. Partway through -> continue it; finished -> the next unfinished one after it, wrapping
 * to earlier unfinished ones; nothing watched -> the first unfinished; everything watched -> replay from the top.
 */
fun computeResumeTarget(ordered: List<Episode>): ResumeTarget {
    if (ordered.isEmpty()) return ResumeTarget()

    var last = -1
    var lastWatched = 0L
    ordered.forEachIndexed { i, e ->
        if (e.progress.lastWatchedAt > lastWatched) {
            lastWatched = e.progress.lastWatchedAt
            last = i
        }
    }

    if (last < 0) {
        val i = firstUnfinished(ordered, 0, ordered.size)
        return if (i >= 0) targetFor(ordered[i], ResumeReason.NOT_STARTED, true) else targetFor(ordered.first(), ResumeReason.ALL_COMPLETED, false)
    }

    if (!ordered[last].progress.completed) return targetFor(ordered[last], ResumeReason.IN_PROGRESS, true)

    var next = firstUnfinished(ordered, last + 1, ordered.size)
    if (next < 0) next = firstUnfinished(ordered, 0, last)
    return if (next >= 0) targetFor(ordered[next], ResumeReason.NEXT_UNFINISHED, true) else targetFor(ordered.first(), ResumeReason.ALL_COMPLETED, false)
}
