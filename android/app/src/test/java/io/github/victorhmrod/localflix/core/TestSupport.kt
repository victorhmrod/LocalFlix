package io.github.victorhmrod.localflix.core

/** Time that only moves when the test says so; due tasks run in order. */
class ManualScheduler : Scheduler {
    private class Task(val due: Long, val seq: Long, val block: () -> Unit) : Cancelable {
        var cancelled = false

        override fun cancel() {
            cancelled = true
        }
    }

    private var now = 0L
    private var seq = 0L
    private val tasks = ArrayList<Task>()

    override fun uptimeMs() = now

    override fun postDelayed(delayMs: Long, task: () -> Unit): Cancelable = Task(now + delayMs, seq++, task).also { tasks += it }

    /** Moves the clock forward, running everything that falls due on the way (also tasks scheduled meanwhile). */
    fun advance(ms: Long) {
        val target = now + ms
        while (true) {
            val next = tasks.filter { !it.cancelled && it.due <= target }.minWithOrNull(compareBy<Task> { it.due }.thenBy { it.seq }) ?: break
            tasks.remove(next)
            now = maxOf(now, next.due)
            next.block()
        }
        now = target
        tasks.removeAll { it.cancelled }
    }

    /** Runs what is due right now (zero-delay tasks such as queue reloads or a 0 s countdown). */
    fun settle() = advance(0)

    val pendingCount get() = tasks.count { !it.cancelled }
}

/** A scripted engine: nothing happens on its own; the test decides when a load finishes and how a file ends. */
class FakeBackend : PlaybackBackend {
    data class Load(val uri: String, val startMs: Long, val token: Long)

    override var listener: BackendListener? = null
    val loads = ArrayList<Load>()
    var stopCalls = 0
    val seeks = ArrayList<Pair<Long, Boolean>>() // (ms, relative)
    val audioTrackRequests = ArrayList<Int>()
    val subtitleTrackRequests = ArrayList<Int>()
    var preferredAudio = ""
    var preferredSubtitle = ""

    private var nextToken = 0L
    private var position = 0L
    private var duration = 600_000L // ten minutes unless a test says otherwise
    private var paused = false
    private var vol = 100
    private var muted = false
    private var spd = 1.0
    var trackList: List<TrackInfo> = emptyList()

    override fun loadFile(uri: String, startMs: Long): Long {
        val l = Load(uri, startMs, ++nextToken)
        loads += l
        paused = false
        position = startMs
        return l.token
    }

    override fun stop() {
        stopCalls++
    }

    override fun setPaused(paused: Boolean) {
        this.paused = paused
        listener?.onPausedChanged(paused)
    }

    override fun seekToMs(positionMs: Long) {
        seeks += positionMs to false
        position = positionMs
    }

    override fun seekByMs(deltaMs: Long) {
        seeks += deltaMs to true
        position += deltaMs
    }

    override fun setVolume(percent: Int) {
        vol = percent
    }

    override fun setMuted(muted: Boolean) {
        this.muted = muted
    }

    override fun setSpeed(speed: Double) {
        spd = speed
    }

    override fun setAudioTrack(trackId: Int) {
        audioTrackRequests += trackId
    }

    override fun setSubtitleTrack(trackId: Int) {
        subtitleTrackRequests += trackId
    }

    override fun addExternalSubtitle(uri: String, displayName: String) = true

    override fun setPreferredLanguages(audio: String, subtitle: String) {
        preferredAudio = audio
        preferredSubtitle = subtitle
    }

    override val positionMs get() = position
    override val durationMs get() = duration
    override val isPaused get() = paused
    override val volume get() = vol
    override val isMuted get() = muted
    override val speed get() = spd
    override val tracks get() = trackList
    override var chapters: List<ChapterInfo> = emptyList()
        private set

    // ---- test controls -----------------------------------------------------
    val lastToken get() = nextToken
    val lastLoad get() = loads.last()

    fun setDuration(ms: Long) {
        duration = ms
    }

    /** What the engine reports for the next file(s); the listener hears about it like it does from the real one. */
    fun setChapters(list: List<ChapterInfo>) {
        chapters = list
        listener?.onChaptersChanged(list)
    }

    fun finishLoading(token: Long = 0) {
        listener?.onFileLoaded(if (token != 0L) token else nextToken)
    }

    fun tick(seconds: Double) {
        position = (seconds * 1000).toLong()
        listener?.onPositionChanged(position)
    }

    fun end(reason: EndReason, token: Long = 0, error: String = "") {
        listener?.onEndFile(EndFileInfo(if (token != 0L) token else nextToken, reason, error))
    }
}

/**
 * Just enough of a library to drive the queue, progress and playback logic: shows with episodes, movies,
 * collections, per-file progress. (The real SQLite-backed implementation is covered by the instrumented tests.)
 */
class InMemoryLibrary(private val clock: Clock) : PlaybackStore, SettingsStore {
    private var nextId = 1L
    val files = LinkedHashMap<Id, MediaFile>()
    val progresses = HashMap<Id, Progress>()
    val shows = LinkedHashMap<Id, Show>()
    val seasons = ArrayList<Season>()
    val episodes = ArrayList<Episode>() // file/progress are joined when read
    val movies = ArrayList<Movie>()
    val collections = LinkedHashMap<Id, MutableList<CollectionEntry>>()
    private val settingValues = HashMap<String, String>()
    var settingWrites = 0
    val unavailable = HashSet<String>() // uris that cannot be opened any more

    var onProgressChanged: (Id) -> Unit = {}
    var onShowChanged: (Id) -> Unit = {}
    var onLibraryChanged: () -> Unit = {}

    private fun newId() = nextId++

    fun uriOf(path: String) = "content://test/$path"

    private fun newFile(path: String, durationMs: Long = 0): MediaFile {
        val f = MediaFile(
            id = newId(),
            uri = uriOf(path),
            docKey = "test/$path",
            displayName = path.substringAfterLast('/'),
            relativePath = path,
            sizeBytes = 2048,
            durationMs = durationMs,
            addedAt = clock.nowMs(),
        )
        files[f.id] = f
        return f
    }

    /** A show with one season and [count] episodes ep01.mp4, ep02.mp4, ... */
    fun makeShow(title: String, count: Int): Pair<Show, List<Episode>> {
        val show = Show(newId(), title, createdAt = clock.nowMs(), updatedAt = clock.nowMs())
        shows[show.id] = show
        val season = Season(newId(), show.id, 1, "Season 1", seasons.count { it.showId == show.id })
        seasons += season
        val list = (1..count).map { i ->
            val f = newFile("$title/ep%02d.mp4".format(i))
            Episode(newId(), season.id, show.id, 0, 1, "Episode $i", i, i - 1, clock.nowMs(), f).also { episodes += it }
        }
        return show to list
    }

    fun makeMovie(title: String, fileName: String = "$title.mkv"): Movie {
        val f = newFile("movies/$fileName")
        return Movie(newId(), title, createdAt = clock.nowMs(), file = f).also { movies += it }
    }

    fun makeCollection(name: String, vararg entries: Pair<CollectionItemType, Id>): Id {
        val id = newId()
        collections[id] = entries.mapIndexed { i, (type, itemId) ->
            CollectionEntry(newId(), id, type, itemId, i)
        }.toMutableList()
        return id
    }

    fun episodesOf(showId: Id): List<Episode> {
        val order = seasons.filter { it.showId == showId }.sortedBy { it.sortOrder }
        return order.flatMap { s -> episodes.filter { it.seasonId == s.id }.sortedBy { it.sortOrder } }
            .map { it.copy(seasonNumber = seasons.first { s -> s.id == it.seasonId }.number, file = files.getValue(it.file.id), progress = progress(it.file.id)) }
    }

    fun moveEpisode(episodeId: Id, toIndex: Int) {
        val e = episodes.first { it.id == episodeId }
        val siblings = episodes.filter { it.seasonId == e.seasonId && it.id != episodeId }.sortedBy { it.sortOrder }.toMutableList()
        siblings.add(toIndex.coerceIn(0, siblings.size), e)
        siblings.forEachIndexed { i, x -> episodes[episodes.indexOfFirst { it.id == x.id }] = x.copy(sortOrder = i) }
        onShowChanged(e.showId)
    }

    fun removeEpisodes(ids: List<Id>) {
        val shows = episodes.filter { it.id in ids }.map { it.showId }.toSet()
        for (e in episodes.filter { it.id in ids }) {
            episodes.remove(e)
            files.remove(e.file.id)
            progresses.remove(e.file.id)
        }
        shows.forEach(onShowChanged)
        onLibraryChanged()
    }

    fun relinkFile(mediaFileId: Id, newPath: String) {
        files[mediaFileId] = files.getValue(mediaFileId).copy(uri = uriOf(newPath), docKey = "test/$newPath", relativePath = newPath, missing = false)
        unavailable.remove(files.getValue(mediaFileId).uri)
    }

    // ---- PlaybackStore ----------------------------------------------------

    private fun entry(e: Episode, showTitle: String): QueueEntry {
        val f = files.getValue(e.file.id)
        val p = progress(f.id)
        return QueueEntry(
            kind = MediaKind.EPISODE, itemId = e.id, showId = e.showId, mediaFileId = f.id,
            title = episodeLabel(e), contextTitle = showTitle, uri = f.uri, fileLabel = f.label,
            missing = f.missing, completed = p.completed, positionMs = p.resumePositionMs(),
            durationMs = if (p.durationMs > 0) p.durationMs else f.durationMs,
        )
    }

    private fun entry(m: Movie): QueueEntry {
        val f = files.getValue(m.file.id)
        val p = progress(f.id)
        return QueueEntry(
            kind = MediaKind.MOVIE, itemId = m.id, mediaFileId = f.id, title = m.title, uri = f.uri, fileLabel = f.label,
            missing = f.missing, completed = p.completed, positionMs = p.resumePositionMs(),
            durationMs = if (p.durationMs > 0) p.durationMs else f.durationMs,
        )
    }

    override fun buildQueue(context: QueueContext): List<QueueEntry> {
        fun showEntries(showId: Id) = shows[showId]?.let { s -> episodesOf(showId).map { entry(it, s.title) } }.orEmpty()
        return when (context.type) {
            ContextType.SHOW -> showEntries(context.id)
            ContextType.MOVIE -> movies.filter { it.id == context.id }.map { entry(it) }
            ContextType.COLLECTION -> collections[context.id].orEmpty().sortedBy { it.sortOrder }.flatMap { ce ->
                if (ce.type == CollectionItemType.SHOW) showEntries(ce.itemId) else movies.filter { it.id == ce.itemId }.map { entry(it) }
            }
            ContextType.NONE -> emptyList()
        }
    }

    override fun progress(mediaFileId: Id): Progress = progresses[mediaFileId] ?: Progress(mediaFileId)

    override fun mediaFile(id: Id): MediaFile? = files[id]

    private fun showOfMedia(mediaFileId: Id) = episodes.firstOrNull { it.file.id == mediaFileId }?.showId

    private fun update(mediaFileId: Id, change: (Progress) -> Progress) {
        progresses[mediaFileId] = change(progress(mediaFileId))
        onProgressChanged(mediaFileId)
    }

    override fun saveProgress(mediaFileId: Id, positionMs: Long, durationMs: Long) = update(mediaFileId) {
        it.copy(positionMs = positionMs, durationMs = if (durationMs > 0) durationMs else it.durationMs, lastWatchedAt = clock.nowMs())
    }

    override fun touchLastWatched(mediaFileId: Id) = update(mediaFileId) { it.copy(lastWatchedAt = clock.nowMs()) }

    override fun saveTrackSelection(mediaFileId: Id, audioTrack: Int, subtitleTrack: Int) =
        update(mediaFileId) { it.copy(audioTrack = audioTrack, subtitleTrack = subtitleTrack) }

    override fun markCompleted(mediaFileId: Id, durationMs: Long) {
        update(mediaFileId) {
            it.copy(
                positionMs = 0, completed = true, playCount = it.playCount + 1,
                durationMs = if (durationMs > 0) durationMs else it.durationMs, lastWatchedAt = clock.nowMs(),
            )
        }
        showOfMedia(mediaFileId)?.let(onShowChanged)
    }

    override fun setMediaDuration(mediaFileId: Id, durationMs: Long) {
        files[mediaFileId]?.let { files[mediaFileId] = it.copy(durationMs = durationMs) }
    }

    override fun setMissing(mediaFileId: Id, missing: Boolean): Boolean {
        val f = files[mediaFileId] ?: return false
        files[mediaFileId] = f.copy(missing = missing)
        showOfMedia(mediaFileId)?.let(onShowChanged)
        return true
    }

    // ---- SettingsStore ----------------------------------------------------

    override fun allSettings(): Map<String, String> = HashMap(settingValues)

    override fun setSetting(key: String, value: String) {
        settingValues[key] = value
        settingWrites++
    }
}

/** The whole playback stack wired up the way the app wires it, on fake time and a fake engine. */
class PlaybackHarness {
    private var wall = 1_000L
    val clock = Clock { wall.also { wall += 1000 } } // every read is a second later, so "most recent" is unambiguous
    val scheduler = ManualScheduler()
    val library = InMemoryLibrary(clock)
    val settings = AppSettings(library)
    val backend = FakeBackend()
    val queue = EpisodeQueue(library, scheduler)
    val progress = ProgressTracker(library, scheduler)
    val files = FileProbe { it !in library.unavailable }
    val controller = PlaybackController(backend, queue, progress, library, settings, scheduler, files)
    val events = ArrayList<PlaybackEvent>()

    init {
        library.onProgressChanged = { queue.onProgressChanged(it) }
        library.onShowChanged = { queue.onShowChanged(it) }
        library.onLibraryChanged = { queue.onLibraryChanged() }
        controller.addListener { events += it }
    }

    inline fun <reified T : PlaybackEvent> events(): List<T> = events.filterIsInstance<T>()

    fun showContext(show: Show) = QueueContext(ContextType.SHOW, show.id)

    /** Starts an episode and completes the engine's load handshake. */
    fun startEpisode(show: Show, e: Episode, mode: StartMode = StartMode.AUTO, durationMs: Long = 600_000): Boolean {
        val started = controller.play(showContext(show), e.file.id, mode)
        if (!started) return false
        backend.setDuration(durationMs)
        backend.finishLoading()
        return controller.state == PlaybackState.PLAYING
    }

    fun loadFinished(durationMs: Long = 600_000) {
        backend.setDuration(durationMs)
        backend.finishLoading()
    }
}
