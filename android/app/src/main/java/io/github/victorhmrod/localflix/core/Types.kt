package io.github.victorhmrod.localflix.core

/** Row ids start at 1, so 0 means "none". */
typealias Id = Long

const val INVALID_ID: Id = 0L

/** Track selection stored per media file: AUTO lets the player's language preferences decide, OFF disables the stream. */
const val TRACK_AUTO = -1
const val TRACK_OFF = 0

/** A resume position closer to the start than this is treated as "not started". */
const val MIN_RESUME_MS = 5000L

enum class MediaKind { EPISODE, MOVIE }

/**
 * A video the library knows about. On Android files are content URIs granted through the system file picker,
 * never paths: [uri] is what the player opens, [docKey] identifies the document regardless of how it was
 * granted (single file or inside a folder), and [relativePath] is its place inside the folder the user picked.
 */
data class MediaFile(
    val id: Id = INVALID_ID,
    val uri: String = "",
    val docKey: String = "",
    val displayName: String = "",
    val relativePath: String = "",
    val treeUri: String = "",
    val sizeBytes: Long = 0,
    val mtimeMs: Long = 0,
    val durationMs: Long = 0,
    val missing: Boolean = false,
    val addedAt: Long = 0,
) {
    /** What the user recognises the file by: folder-relative path when known, otherwise the file name. */
    val label: String get() = relativePath.ifEmpty { displayName }
}

data class Progress(
    val mediaFileId: Id = INVALID_ID,
    val positionMs: Long = 0,
    val durationMs: Long = 0,
    val completed: Boolean = false,
    val playCount: Int = 0,
    val lastWatchedAt: Long = 0, // 0 = never played
    val audioTrack: Int = TRACK_AUTO,
    val subtitleTrack: Int = TRACK_AUTO,
) {
    /** 0.0 - 1.0 for display; completed items always read as fully watched. */
    fun fraction(): Double {
        if (completed) return 1.0
        if (durationMs <= 0) return 0.0
        return (positionMs.toDouble() / durationMs.toDouble()).coerceIn(0.0, 1.0)
    }

    /** The position playback should start from when this item is simply "played". */
    fun resumePositionMs(): Long = if (completed || positionMs < MIN_RESUME_MS) 0 else positionMs

    fun started(): Boolean = !completed && positionMs >= MIN_RESUME_MS
}

data class Show(
    val id: Id = INVALID_ID,
    val title: String = "",
    val description: String = "",
    val coverFile: String = "", // file name inside the app's covers directory ("" = none)
    val createdAt: Long = 0,
    val updatedAt: Long = 0,
)

data class Season(
    val id: Id = INVALID_ID,
    val showId: Id = INVALID_ID,
    val number: Int = 0, // 0 = unnumbered (specials, extras)
    val title: String = "",
    val sortOrder: Int = 0,
)

data class Episode(
    val id: Id = INVALID_ID,
    val seasonId: Id = INVALID_ID,
    val showId: Id = INVALID_ID,
    val seasonSortOrder: Int = 0,
    val seasonNumber: Int = 0,
    val title: String = "",
    val episodeNumber: Int = 0, // 0 = unknown
    val sortOrder: Int = 0,
    val createdAt: Long = 0,
    val file: MediaFile = MediaFile(),
    val progress: Progress = Progress(),
)

data class Movie(
    val id: Id = INVALID_ID,
    val title: String = "",
    val description: String = "",
    val posterFile: String = "",
    val createdAt: Long = 0,
    val file: MediaFile = MediaFile(),
    val progress: Progress = Progress(),
)

data class Collection(
    val id: Id = INVALID_ID,
    val name: String = "",
    val description: String = "",
    val createdAt: Long = 0,
    val entryCount: Int = 0,
)

enum class CollectionItemType { SHOW, MOVIE }

data class CollectionEntry(
    val id: Id = INVALID_ID,
    val collectionId: Id = INVALID_ID,
    val type: CollectionItemType = CollectionItemType.SHOW,
    val itemId: Id = INVALID_ID,
    val sortOrder: Int = 0,
    val title: String = "", // resolved from the show / movie
)

/** Aggregates shown on show cards and the show details header. */
data class ShowSummary(
    val show: Show,
    val seasonCount: Int = 0,
    val episodeCount: Int = 0,
    val completedCount: Int = 0,
    val lastWatchedAt: Long = 0,
)

/** One playable thing in the queue: an episode or a movie. */
data class QueueEntry(
    val kind: MediaKind = MediaKind.EPISODE,
    val itemId: Id = INVALID_ID, // episode id or movie id
    val showId: Id = INVALID_ID, // 0 for movies
    val mediaFileId: Id = INVALID_ID,
    val title: String = "", // "S1E3 · The Title" or the movie title
    val contextTitle: String = "", // show title, or empty for movies
    val uri: String = "",
    val fileLabel: String = "", // for messages: where the file lives (relative path or name)
    val missing: Boolean = false,
    val completed: Boolean = false,
    val positionMs: Long = 0,
    val durationMs: Long = 0,
) {
    fun valid(): Boolean = mediaFileId != INVALID_ID

    fun displayTitle(): String = if (contextTitle.isEmpty()) title else "$contextTitle — $title"
}

/** Where an item came from, for queue construction. */
enum class ContextType { NONE, SHOW, MOVIE, COLLECTION }

data class QueueContext(val type: ContextType = ContextType.NONE, val id: Id = INVALID_ID)

/** Series-level "what do I watch next" answer. */
enum class ResumeReason {
    NONE, // nothing to play (empty show)
    IN_PROGRESS, // an episode was left partway through
    NEXT_UNFINISHED, // the last episode finished; continue with the next unfinished one
    NOT_STARTED, // nothing watched yet; start at the first unfinished episode
    ALL_COMPLETED, // everything is watched; target is the first episode (replay)
}

data class ResumeTarget(
    val reason: ResumeReason = ResumeReason.NONE,
    val episodeId: Id = INVALID_ID,
    val mediaFileId: Id = INVALID_ID,
    val positionMs: Long = 0, // 0 when starting from the beginning
) {
    fun valid(): Boolean = reason != ResumeReason.NONE

    fun allCompleted(): Boolean = reason == ResumeReason.ALL_COMPLETED
}

/** Used by the Continue Watching section. */
data class ContinueItem(
    val kind: MediaKind = MediaKind.EPISODE,
    val showId: Id = INVALID_ID, // 0 for movies
    val movieId: Id = INVALID_ID, // 0 for shows
    val title: String = "", // show or movie title
    val subtitle: String = "", // current episode title ("S1E3 · Name") or empty
    val coverFile: String = "",
    val mediaFileId: Id = INVALID_ID, // the item that will start
    val positionMs: Long = 0,
    val durationMs: Long = 0,
    val lastWatchedAt: Long = 0,
    val missing: Boolean = false,
) {
    fun fraction(): Double = if (durationMs > 0) (positionMs.toDouble() / durationMs.toDouble()).coerceIn(0.0, 1.0) else 0.0
}

/** Layout of a show used by reordering: seasons in order, each with its episodes in order. Applied in one transaction. */
data class SeasonLayout(val seasonId: Id, val episodeIds: List<Id>)

enum class CardKind { SHOW, MOVIE }

/** A poster tile on the home screen / library grids. */
data class LibraryCard(
    val kind: CardKind,
    val id: Id,
    val title: String,
    val subtitle: String = "",
    val imageFile: String = "",
    val timestamp: Long = 0,
    val progress: Double = 0.0, // 0..1 watched share
    val allWatched: Boolean = false,
    val missing: Boolean = false,
)
