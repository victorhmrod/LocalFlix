package io.github.victorhmrod.localflix.data

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.ForeignKey
import androidx.room.Index
import androidx.room.PrimaryKey

/**
 * Every video the library knows about, regardless of what references it. Only "Locate missing file" rewrites
 * [uri]/[docKey], so progress (keyed by id) survives a move. Library rows are removed only when no episode or
 * movie references them any more; the file itself is never touched.
 */
@Entity(tableName = "media_files", indices = [Index(value = ["doc_key"], unique = true)])
data class MediaFileEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val uri: String,
    @ColumnInfo(name = "doc_key") val docKey: String,
    @ColumnInfo(name = "display_name") val displayName: String,
    @ColumnInfo(name = "relative_path") val relativePath: String,
    @ColumnInfo(name = "tree_uri") val treeUri: String,
    @ColumnInfo(name = "size_bytes") val sizeBytes: Long,
    @ColumnInfo(name = "mtime_ms") val mtimeMs: Long,
    @ColumnInfo(name = "duration_ms") val durationMs: Long = 0,
    @ColumnInfo(name = "is_missing") val isMissing: Boolean = false,
    @ColumnInfo(name = "added_at") val addedAt: Long,
)

@Entity(tableName = "shows")
data class ShowEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val title: String,
    val description: String = "",
    @ColumnInfo(name = "cover_file") val coverFile: String = "",
    @ColumnInfo(name = "created_at") val createdAt: Long,
    @ColumnInfo(name = "updated_at") val updatedAt: Long,
)

@Entity(
    tableName = "seasons",
    foreignKeys = [ForeignKey(entity = ShowEntity::class, parentColumns = ["id"], childColumns = ["show_id"], onDelete = ForeignKey.CASCADE)],
    indices = [Index(value = ["show_id", "sort_order"])],
)
data class SeasonEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "show_id") val showId: Long,
    @ColumnInfo(name = "season_number") val seasonNumber: Int = 0,
    val title: String,
    @ColumnInfo(name = "sort_order") val sortOrder: Int,
)

/** [sortOrder] is the user's explicit playback order within a season. */
@Entity(
    tableName = "episodes",
    foreignKeys = [
        ForeignKey(entity = SeasonEntity::class, parentColumns = ["id"], childColumns = ["season_id"], onDelete = ForeignKey.CASCADE),
        ForeignKey(entity = MediaFileEntity::class, parentColumns = ["id"], childColumns = ["media_file_id"], onDelete = ForeignKey.RESTRICT),
    ],
    indices = [Index(value = ["season_id", "sort_order"]), Index(value = ["media_file_id"], unique = true)],
)
data class EpisodeEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "season_id") val seasonId: Long,
    @ColumnInfo(name = "media_file_id") val mediaFileId: Long,
    val title: String,
    @ColumnInfo(name = "episode_number") val episodeNumber: Int = 0,
    @ColumnInfo(name = "sort_order") val sortOrder: Int,
    @ColumnInfo(name = "created_at") val createdAt: Long,
)

@Entity(
    tableName = "movies",
    foreignKeys = [ForeignKey(entity = MediaFileEntity::class, parentColumns = ["id"], childColumns = ["media_file_id"], onDelete = ForeignKey.RESTRICT)],
    indices = [Index(value = ["media_file_id"], unique = true)],
)
data class MovieEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "media_file_id") val mediaFileId: Long,
    val title: String,
    val description: String = "",
    @ColumnInfo(name = "poster_file") val posterFile: String = "",
    @ColumnInfo(name = "created_at") val createdAt: Long,
)

@Entity(tableName = "collections")
data class CollectionEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val name: String,
    val description: String = "",
    @ColumnInfo(name = "created_at") val createdAt: Long,
)

/** [itemId] is polymorphic (show or movie), so cleanup when one is deleted is done by the repository. */
@Entity(
    tableName = "collection_entries",
    foreignKeys = [ForeignKey(entity = CollectionEntity::class, parentColumns = ["id"], childColumns = ["collection_id"], onDelete = ForeignKey.CASCADE)],
    indices = [Index(value = ["collection_id", "item_type", "item_id"], unique = true), Index(value = ["collection_id", "sort_order"])],
)
data class CollectionEntryEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "collection_id") val collectionId: Long,
    @ColumnInfo(name = "item_type") val itemType: String, // "show" | "movie"
    @ColumnInfo(name = "item_id") val itemId: Long,
    @ColumnInfo(name = "sort_order") val sortOrder: Int,
)

/** One row per media file; keyed on the file so reordering or moving an episode can never disturb it. */
@Entity(
    tableName = "playback_progress",
    foreignKeys = [ForeignKey(entity = MediaFileEntity::class, parentColumns = ["id"], childColumns = ["media_file_id"], onDelete = ForeignKey.CASCADE)],
    indices = [Index(value = ["last_watched_at"])],
)
data class ProgressEntity(
    @PrimaryKey @ColumnInfo(name = "media_file_id") val mediaFileId: Long,
    @ColumnInfo(name = "position_ms") val positionMs: Long = 0,
    @ColumnInfo(name = "duration_ms") val durationMs: Long = 0,
    val completed: Boolean = false,
    @ColumnInfo(name = "play_count") val playCount: Int = 0,
    @ColumnInfo(name = "last_watched_at") val lastWatchedAt: Long = 0,
    @ColumnInfo(name = "audio_track") val audioTrack: Int = -1,
    @ColumnInfo(name = "subtitle_track") val subtitleTrack: Int = -1,
    @ColumnInfo(name = "updated_at") val updatedAt: Long = 0,
)

@Entity(tableName = "settings")
data class SettingEntity(
    @PrimaryKey val key: String,
    val value: String,
)
