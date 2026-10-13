package io.github.victorhmrod.localflix.data

import androidx.room.ColumnInfo
import androidx.room.Dao
import androidx.room.Embedded
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query

private const val MEDIA_COLS =
    "m.id AS m_id, m.uri AS m_uri, m.doc_key AS m_doc_key, m.display_name AS m_display_name, " +
        "m.relative_path AS m_relative_path, m.tree_uri AS m_tree_uri, m.size_bytes AS m_size_bytes, " +
        "m.mtime_ms AS m_mtime_ms, m.duration_ms AS m_duration_ms, m.is_missing AS m_is_missing, m.added_at AS m_added_at"

private const val PROGRESS_COLS =
    "COALESCE(p.media_file_id, m.id) AS p_media_file_id, COALESCE(p.position_ms, 0) AS p_position_ms, " +
        "COALESCE(p.duration_ms, 0) AS p_duration_ms, COALESCE(p.completed, 0) AS p_completed, " +
        "COALESCE(p.play_count, 0) AS p_play_count, COALESCE(p.last_watched_at, 0) AS p_last_watched_at, " +
        "COALESCE(p.audio_track, -1) AS p_audio_track, COALESCE(p.subtitle_track, -1) AS p_subtitle_track, " +
        "COALESCE(p.updated_at, 0) AS p_updated_at"

private const val EPISODE_SELECT =
    "SELECT e.id AS e_id, e.season_id AS e_season_id, s.show_id AS e_show_id, s.sort_order AS e_season_sort, " +
        "s.season_number AS e_season_number, e.title AS e_title, e.episode_number AS e_episode_number, " +
        "e.sort_order AS e_sort, e.created_at AS e_created, " + MEDIA_COLS + ", " + PROGRESS_COLS + " " +
        "FROM episodes e JOIN seasons s ON s.id = e.season_id JOIN media_files m ON m.id = e.media_file_id " +
        "LEFT JOIN playback_progress p ON p.media_file_id = m.id "

private const val MOVIE_SELECT =
    "SELECT mv.id AS v_id, mv.title AS v_title, mv.description AS v_description, mv.poster_file AS v_poster_file, " +
        "mv.created_at AS v_created, " + MEDIA_COLS + ", " + PROGRESS_COLS + " " +
        "FROM movies mv JOIN media_files m ON m.id = mv.media_file_id " +
        "LEFT JOIN playback_progress p ON p.media_file_id = m.id "

data class EpisodeRow(
    @ColumnInfo(name = "e_id") val id: Long,
    @ColumnInfo(name = "e_season_id") val seasonId: Long,
    @ColumnInfo(name = "e_show_id") val showId: Long,
    @ColumnInfo(name = "e_season_sort") val seasonSortOrder: Int,
    @ColumnInfo(name = "e_season_number") val seasonNumber: Int,
    @ColumnInfo(name = "e_title") val title: String,
    @ColumnInfo(name = "e_episode_number") val episodeNumber: Int,
    @ColumnInfo(name = "e_sort") val sortOrder: Int,
    @ColumnInfo(name = "e_created") val createdAt: Long,
    @Embedded(prefix = "m_") val file: MediaFileEntity,
    @Embedded(prefix = "p_") val progress: ProgressEntity,
)

data class MovieRow(
    @ColumnInfo(name = "v_id") val id: Long,
    @ColumnInfo(name = "v_title") val title: String,
    @ColumnInfo(name = "v_description") val description: String,
    @ColumnInfo(name = "v_poster_file") val posterFile: String,
    @ColumnInfo(name = "v_created") val createdAt: Long,
    @Embedded(prefix = "m_") val file: MediaFileEntity,
    @Embedded(prefix = "p_") val progress: ProgressEntity,
)

data class ShowSummaryRow(
    @Embedded val show: ShowEntity,
    @ColumnInfo(name = "season_count") val seasonCount: Int,
    @ColumnInfo(name = "episode_count") val episodeCount: Int,
    @ColumnInfo(name = "completed_count") val completedCount: Int,
    @ColumnInfo(name = "last_watched") val lastWatched: Long,
)

data class CollectionRow(
    @Embedded val collection: CollectionEntity,
    @ColumnInfo(name = "entry_count") val entryCount: Int,
)

data class CollectionEntryRow(
    @Embedded val entry: CollectionEntryEntity,
    @ColumnInfo(name = "item_title") val title: String?,
)

data class ShowHistoryRow(
    @ColumnInfo(name = "show_id") val showId: Long,
    @ColumnInfo(name = "last_watched") val lastWatched: Long,
)

/** All SQL of the app. Plain blocking calls: the playback bookkeeping makes small indexed ones from the main thread. */
@Dao
interface LibraryDao {
    // ------------------------------------------------------------------ media files

    @Query("SELECT * FROM media_files WHERE id = :id")
    fun mediaFile(id: Long): MediaFileEntity?

    @Query("SELECT * FROM media_files WHERE doc_key = :docKey")
    fun mediaFileByDocKey(docKey: String): MediaFileEntity?

    @Query("SELECT * FROM media_files ORDER BY id")
    fun allMediaFiles(): List<MediaFileEntity>

    @Query("SELECT * FROM media_files WHERE is_missing = 1 ORDER BY relative_path, display_name")
    fun missingMediaFiles(): List<MediaFileEntity>

    @Query(
        "SELECT m.* FROM media_files m JOIN episodes e ON e.media_file_id = m.id " +
            "JOIN seasons se ON se.id = e.season_id WHERE se.show_id = :showId ORDER BY m.id",
    )
    fun mediaFilesOfShow(showId: Long): List<MediaFileEntity>

    @Insert
    fun insertMediaFile(file: MediaFileEntity): Long

    @Query(
        "UPDATE media_files SET uri = :uri, doc_key = :docKey, display_name = :displayName, relative_path = :relativePath, " +
            "tree_uri = :treeUri, size_bytes = :sizeBytes, mtime_ms = :mtimeMs, is_missing = 0 WHERE id = :id",
    )
    fun updateMediaLocation(
        id: Long,
        uri: String,
        docKey: String,
        displayName: String,
        relativePath: String,
        treeUri: String,
        sizeBytes: Long,
        mtimeMs: Long,
    ): Int

    @Query("UPDATE media_files SET is_missing = :missing WHERE id IN (:ids) AND is_missing <> :missing")
    fun setMissing(ids: List<Long>, missing: Boolean): Int

    @Query("UPDATE media_files SET duration_ms = :durationMs WHERE id = :id")
    fun setMediaDuration(id: Long, durationMs: Long)

    /** Files that no episode or movie references any more (their progress goes with them, via ON DELETE CASCADE). */
    @Query("DELETE FROM media_files WHERE id NOT IN (SELECT media_file_id FROM episodes) AND id NOT IN (SELECT media_file_id FROM movies)")
    fun deleteOrphanMedia(): Int

    // ------------------------------------------------------------------ shows

    @Query(
        "SELECT sh.*, " +
            "(SELECT COUNT(*) FROM seasons WHERE show_id = sh.id) AS season_count, " +
            "(SELECT COUNT(*) FROM episodes e JOIN seasons se ON se.id = e.season_id WHERE se.show_id = sh.id) AS episode_count, " +
            "(SELECT COUNT(*) FROM episodes e JOIN seasons se ON se.id = e.season_id " +
            "   JOIN playback_progress p ON p.media_file_id = e.media_file_id WHERE se.show_id = sh.id AND p.completed = 1) AS completed_count, " +
            "(SELECT COALESCE(MAX(p.last_watched_at), 0) FROM episodes e JOIN seasons se ON se.id = e.season_id " +
            "   JOIN playback_progress p ON p.media_file_id = e.media_file_id WHERE se.show_id = sh.id) AS last_watched " +
            "FROM shows sh ORDER BY sh.title COLLATE NOCASE, sh.id",
    )
    fun showSummaries(): List<ShowSummaryRow>

    @Query("SELECT * FROM shows WHERE id = :id")
    fun show(id: Long): ShowEntity?

    @Insert
    fun insertShow(show: ShowEntity): Long

    @Query("UPDATE shows SET title = :title, description = :description, cover_file = :coverFile, updated_at = :now WHERE id = :id")
    fun updateShow(id: Long, title: String, description: String, coverFile: String, now: Long): Int

    @Query("UPDATE shows SET updated_at = :now WHERE id = :id")
    fun touchShow(id: Long, now: Long)

    @Query("SELECT cover_file FROM shows WHERE id = :id")
    fun coverOfShow(id: Long): String?

    @Query("DELETE FROM shows WHERE id = :id")
    fun deleteShow(id: Long): Int

    // ------------------------------------------------------------------ seasons

    @Query("SELECT * FROM seasons WHERE show_id = :showId ORDER BY sort_order, id")
    fun seasons(showId: Long): List<SeasonEntity>

    @Query("SELECT * FROM seasons WHERE id = :id")
    fun season(id: Long): SeasonEntity?

    @Insert
    fun insertSeason(season: SeasonEntity): Long

    @Query("UPDATE seasons SET title = :title, season_number = :number WHERE id = :id")
    fun updateSeason(id: Long, title: String, number: Int): Int

    @Query("DELETE FROM seasons WHERE id = :id")
    fun deleteSeason(id: Long): Int

    @Query("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM seasons WHERE show_id = :showId")
    fun nextSeasonOrder(showId: Long): Int

    @Query("SELECT id FROM seasons WHERE show_id = :showId AND season_number = :number ORDER BY sort_order LIMIT 1")
    fun seasonIdByNumber(showId: Long, number: Int): Long?

    @Query("SELECT MIN(sort_order) FROM seasons WHERE show_id = :showId AND season_number > :number")
    fun firstSeasonOrderAbove(showId: Long, number: Int): Int?

    @Query("UPDATE seasons SET sort_order = sort_order + 1 WHERE show_id = :showId AND sort_order >= :from")
    fun shiftSeasonOrders(showId: Long, from: Int)

    @Query("UPDATE seasons SET sort_order = :order WHERE id = :id")
    fun setSeasonOrder(id: Long, order: Int)

    // ------------------------------------------------------------------ episodes

    @Query(EPISODE_SELECT + "WHERE s.show_id = :showId ORDER BY s.sort_order, s.id, e.sort_order, e.id")
    fun episodes(showId: Long): List<EpisodeRow>

    @Query(EPISODE_SELECT + "WHERE e.id = :id")
    fun episode(id: Long): EpisodeRow?

    @Query(EPISODE_SELECT + "WHERE e.media_file_id = :mediaFileId")
    fun episodeByMedia(mediaFileId: Long): EpisodeRow?

    @Insert
    fun insertEpisode(episode: EpisodeEntity): Long

    @Query("UPDATE episodes SET title = :title, episode_number = :number WHERE id = :id")
    fun updateEpisode(id: Long, title: String, number: Int): Int

    @Query("DELETE FROM episodes WHERE id = :id")
    fun deleteEpisode(id: Long): Int

    @Query("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM episodes WHERE season_id = :seasonId")
    fun nextEpisodeOrder(seasonId: Long): Int

    @Query("UPDATE episodes SET season_id = :seasonId, sort_order = :order WHERE id = :id")
    fun placeEpisode(id: Long, seasonId: Long, order: Int)

    @Query("SELECT e.id FROM episodes e JOIN seasons se ON se.id = e.season_id WHERE se.show_id = :showId")
    fun episodeIdsOfShow(showId: Long): List<Long>

    // ------------------------------------------------------------------ movies

    @Query(MOVIE_SELECT + "ORDER BY mv.title COLLATE NOCASE, mv.id")
    fun movies(): List<MovieRow>

    @Query(MOVIE_SELECT + "WHERE mv.id = :id")
    fun movie(id: Long): MovieRow?

    @Query(MOVIE_SELECT + "WHERE mv.media_file_id = :mediaFileId")
    fun movieByMedia(mediaFileId: Long): MovieRow?

    @Insert
    fun insertMovie(movie: MovieEntity): Long

    @Query("UPDATE movies SET title = :title, description = :description, poster_file = :posterFile WHERE id = :id")
    fun updateMovie(id: Long, title: String, description: String, posterFile: String): Int

    @Query("SELECT poster_file FROM movies WHERE id = :id")
    fun posterOfMovie(id: Long): String?

    @Query("DELETE FROM movies WHERE id = :id")
    fun deleteMovie(id: Long): Int

    // ------------------------------------------------------------------ collections

    @Query(
        "SELECT c.*, (SELECT COUNT(*) FROM collection_entries WHERE collection_id = c.id) AS entry_count " +
            "FROM collections c ORDER BY c.name COLLATE NOCASE, c.id",
    )
    fun collections(): List<CollectionRow>

    @Insert
    fun insertCollection(collection: CollectionEntity): Long

    @Query("UPDATE collections SET name = :name, description = :description WHERE id = :id")
    fun updateCollection(id: Long, name: String, description: String): Int

    @Query("DELETE FROM collections WHERE id = :id")
    fun deleteCollection(id: Long): Int

    @Query(
        "SELECT ce.*, CASE ce.item_type WHEN 'show' THEN (SELECT title FROM shows WHERE id = ce.item_id) " +
            "ELSE (SELECT title FROM movies WHERE id = ce.item_id) END AS item_title " +
            "FROM collection_entries ce WHERE ce.collection_id = :collectionId ORDER BY ce.sort_order, ce.id",
    )
    fun collectionEntries(collectionId: Long): List<CollectionEntryRow>

    @Insert(onConflict = OnConflictStrategy.IGNORE)
    fun insertCollectionEntry(entry: CollectionEntryEntity): Long

    @Query("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM collection_entries WHERE collection_id = :collectionId")
    fun nextCollectionOrder(collectionId: Long): Int

    @Query("DELETE FROM collection_entries WHERE id = :entryId AND collection_id = :collectionId")
    fun deleteCollectionEntry(entryId: Long, collectionId: Long): Int

    @Query("UPDATE collection_entries SET sort_order = :order WHERE id = :id AND collection_id = :collectionId")
    fun setCollectionEntryOrder(id: Long, collectionId: Long, order: Int)

    @Query("DELETE FROM collection_entries WHERE item_type = :type AND item_id = :itemId")
    fun deleteEntriesOfItem(type: String, itemId: Long)

    // ------------------------------------------------------------------ progress

    @Query("SELECT * FROM playback_progress WHERE media_file_id = :mediaFileId")
    fun progress(mediaFileId: Long): ProgressEntity?

    /** Creates the row if it does not exist yet; the UPDATE statements below then only have to modify it. */
    @Query("INSERT OR IGNORE INTO playback_progress(media_file_id) VALUES(:mediaFileId)")
    fun ensureProgressRow(mediaFileId: Long)

    @Query(
        "UPDATE playback_progress SET position_ms = :positionMs, " +
            "duration_ms = CASE WHEN :durationMs > 0 THEN :durationMs ELSE duration_ms END, " +
            "last_watched_at = :now, updated_at = :now WHERE media_file_id = :mediaFileId",
    )
    fun updatePosition(mediaFileId: Long, positionMs: Long, durationMs: Long, now: Long)

    @Query("UPDATE playback_progress SET last_watched_at = :now, updated_at = :now WHERE media_file_id = :mediaFileId")
    fun updateLastWatched(mediaFileId: Long, now: Long)

    @Query("UPDATE playback_progress SET audio_track = :audio, subtitle_track = :subtitle, updated_at = :now WHERE media_file_id = :mediaFileId")
    fun updateTracks(mediaFileId: Long, audio: Int, subtitle: Int, now: Long)

    @Query(
        "UPDATE playback_progress SET position_ms = 0, completed = 1, play_count = play_count + 1, " +
            "duration_ms = CASE WHEN :durationMs > 0 THEN :durationMs ELSE duration_ms END, " +
            "last_watched_at = :now, updated_at = :now WHERE media_file_id = :mediaFileId",
    )
    fun updateCompleted(mediaFileId: Long, durationMs: Long, now: Long)

    @Query("UPDATE playback_progress SET completed = :watched, position_ms = 0, updated_at = :now WHERE media_file_id = :mediaFileId")
    fun updateWatched(mediaFileId: Long, watched: Boolean, now: Long)

    @Query("UPDATE playback_progress SET position_ms = 0, completed = 0, last_watched_at = 0, updated_at = :now WHERE media_file_id = :mediaFileId")
    fun resetProgress(mediaFileId: Long, now: Long)

    @Query(
        "SELECT se.show_id AS show_id, MAX(p.last_watched_at) AS last_watched FROM episodes e " +
            "JOIN seasons se ON se.id = e.season_id JOIN playback_progress p ON p.media_file_id = e.media_file_id " +
            "WHERE p.last_watched_at > 0 GROUP BY se.show_id ORDER BY last_watched DESC LIMIT :limit",
    )
    fun showsWithHistory(limit: Int): List<ShowHistoryRow>

    @Query(
        MOVIE_SELECT + "WHERE p.last_watched_at > 0 AND p.completed = 0 AND p.position_ms >= :minResumeMs " +
            "ORDER BY p.last_watched_at DESC LIMIT :limit",
    )
    fun startedMovies(minResumeMs: Long, limit: Int): List<MovieRow>

    // ------------------------------------------------------------------ settings

    @Query("SELECT * FROM settings")
    fun allSettings(): List<SettingEntity>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    fun putSetting(setting: SettingEntity)
}
