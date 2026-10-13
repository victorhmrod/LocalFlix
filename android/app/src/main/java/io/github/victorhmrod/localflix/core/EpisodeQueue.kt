package io.github.victorhmrod.localflix.core

import java.util.concurrent.CopyOnWriteArrayList

/**
 * The ordered list of things that will play after one another (a show, a movie or a collection), always in the
 * user's configured order. Edits to the library (reorder, rename, remove) are folded in on the next read, so
 * "what is next" is never stale. Port of EpisodeQueueManager.
 */
class EpisodeQueue(private val store: PlaybackStore, private val scheduler: Scheduler) {
    interface Listener {
        fun onQueueChanged() {}

        fun onCurrentChanged(current: QueueEntry) {}
    }

    private val listeners = CopyOnWriteArrayList<Listener>()
    private var entries: List<QueueEntry> = emptyList()
    private var _context = QueueContext()
    private var currentMedia: Id = INVALID_ID
    private var currentIndex = -1
    private var anchor = 0 // position "next" counts from when the current item left the queue
    private var dirty = false
    private var pendingReload: Cancelable? = null

    val context: QueueContext get() = _context

    fun addListener(l: Listener) {
        listeners += l
    }

    fun removeListener(l: Listener) {
        listeners -= l
    }

    fun setContext(context: QueueContext) {
        _context = context
        currentMedia = INVALID_ID
        currentIndex = -1
        anchor = 0
        doReload()
    }

    fun reload() = doReload()

    private fun doReload() {
        dirty = false
        pendingReload?.cancel()
        pendingReload = null
        val oldIndex = currentIndex
        entries = if (_context.type == ContextType.NONE) emptyList() else store.buildQueue(_context)

        currentIndex = -1
        if (currentMedia != INVALID_ID) currentIndex = entries.indexOfFirst { it.mediaFileId == currentMedia }
        anchor = when {
            currentIndex >= 0 -> currentIndex + 1
            // The playing item left the queue (removed): whatever now sits at its old position becomes "next".
            currentMedia != INVALID_ID -> minOf(if (oldIndex >= 0) oldIndex else maxOf(0, anchor - 1), entries.size)
            else -> 0
        }
        listeners.forEach { it.onQueueChanged() }
    }

    private fun scheduleReload() {
        if (_context.type == ContextType.NONE) return
        dirty = true
        if (pendingReload == null) pendingReload = scheduler.postDelayed(0) { flushIfDirty() }
    }

    private fun flushIfDirty() {
        if (dirty) doReload()
    }

    /** Library structure changed (shows/movies/collections added, removed, renamed). */
    fun onLibraryChanged() = scheduleReload()

    /** Episodes/seasons of one show changed (content, order, watched state). */
    fun onShowChanged(showId: Id) {
        val relevant = _context.type == ContextType.COLLECTION || (_context.type == ContextType.SHOW && _context.id == showId)
        if (relevant) scheduleReload()
    }

    /** Keeps the cached completed/position flags current without rebuilding the queue. */
    fun onProgressChanged(mediaFileId: Id) {
        val i = entries.indexOfFirst { it.mediaFileId == mediaFileId }
        if (i < 0) return
        val p = store.progress(mediaFileId)
        val e = entries[i]
        entries = entries.toMutableList().also {
            it[i] = e.copy(
                completed = p.completed,
                positionMs = p.resumePositionMs(),
                durationMs = if (p.durationMs > 0) p.durationMs else e.durationMs,
            )
        }
    }

    fun entries(): List<QueueEntry> {
        flushIfDirty()
        return entries
    }

    fun indexOfMedia(mediaFileId: Id): Int {
        flushIfDirty()
        return entries.indexOfFirst { it.mediaFileId == mediaFileId }
    }

    fun entryForMedia(mediaFileId: Id): QueueEntry = indexOfMedia(mediaFileId).let { if (it >= 0) entries[it] else QueueEntry() }

    fun setCurrentMedia(mediaFileId: Id) {
        flushIfDirty()
        currentMedia = mediaFileId
        currentIndex = if (mediaFileId == INVALID_ID) -1 else indexOfMedia(mediaFileId)
        anchor = if (currentIndex >= 0) currentIndex + 1 else 0
        val c = current()
        listeners.forEach { it.onCurrentChanged(c) }
    }

    fun current(): QueueEntry {
        flushIfDirty()
        return if (currentIndex in entries.indices) entries[currentIndex] else QueueEntry()
    }

    fun next(skipCompleted: Boolean): QueueEntry {
        flushIfDirty()
        val start = if (currentIndex >= 0) currentIndex + 1 else anchor
        for (i in maxOf(0, start) until entries.size) {
            if (skipCompleted && entries[i].completed) continue
            return entries[i]
        }
        return QueueEntry()
    }

    fun previous(): QueueEntry {
        flushIfDirty()
        val start = if (currentIndex >= 0) currentIndex - 1 else anchor - 1
        return if (start >= 0 && start < entries.size) entries[start] else QueueEntry()
    }
}
