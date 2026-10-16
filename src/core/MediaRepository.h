#pragma once

#include "DbError.h"
#include "Types.h"

#include <QHash>
#include <QMap>
#include <QSqlDatabase>

#include <optional>

namespace lf {

// One file offered for import, already analysed by the filename parser.
struct ImportItem {
    QString path;       // absolute, normalised
    qint64 sizeBytes = 0;
    qint64 mtimeMs = 0;
    QString title;      // display title
    int episodeNumber = 0;
    int seasonNumber = -1;  // -1 = no season detected, use the fallback season
    int groupIndex = 0;     // keeps the planner's ordering stable
};

struct DuplicateInfo {
    QString path;
    QString existingAs;  // human-readable: 'My Show — S1E3 · Pilot' or 'Movie: Title'
};

struct ImportOutcome {
    QVector<Id> addedItemIds;  // episode ids or movie ids, in insertion order
    QVector<DuplicateInfo> duplicates;
    QVector<Id> createdSeasonIds;
};

// All SQL lives here. An instance wraps one connection and must be used only
// on the thread that owns that connection; create one per worker via
// DatabaseManager::openSession(). Methods throw DbError.
class MediaRepository {
public:
    explicit MediaRepository(QSqlDatabase db) : m_db(std::move(db)) {}

    // --- media files -------------------------------------------------------
    std::optional<MediaFile> mediaFile(Id id) const;
    std::optional<MediaFile> mediaFileByPath(const QString& path) const;
    QVector<MediaFile> allMediaFiles() const;
    QVector<MediaFile> mediaFilesOfShow(Id showId) const;
    QVector<MediaFile> missingMediaFiles() const;
    // Rewrites the path of an existing entry; progress and metadata stay attached.
    void relinkMediaFile(Id id, const QString& newPath, qint64 sizeBytes, qint64 mtimeMs);
    void setMissing(const QVector<Id>& ids, bool missing);
    void setMediaDuration(Id id, qint64 durationMs);

    // --- shows -------------------------------------------------------------
    QVector<ShowSummary> showSummaries() const;
    std::optional<Show> show(Id id) const;
    Id createShow(const QString& title);
    void updateShow(const Show& show);
    // Returns the cover file name so the caller can remove its own copy.
    QString deleteShow(Id id);

    // --- seasons -----------------------------------------------------------
    QVector<Season> seasons(Id showId) const;
    std::optional<Season> season(Id id) const;
    Id createSeason(Id showId, const QString& title, int number);
    void updateSeason(Id id, const QString& title, int number);
    void deleteSeason(Id id);

    // --- episodes ----------------------------------------------------------
    // All episodes of a show in the user's playback order (season order, then
    // episode order), with file and progress attached.
    QVector<Episode> episodes(Id showId) const;
    std::optional<Episode> episode(Id id) const;
    std::optional<Episode> episodeByMedia(Id mediaFileId) const;
    void updateEpisode(Id id, const QString& title, int episodeNumber);
    void deleteEpisodes(const QVector<Id>& ids);
    // Replaces season order, episode order and episode->season assignment in
    // one transaction. `layout` must contain every season and episode of the
    // show exactly once.
    void applyLayout(Id showId, const QVector<SeasonLayout>& layout);

    ImportOutcome importEpisodes(Id showId, Id fallbackSeasonId, const QVector<ImportItem>& items);

    // --- movies ------------------------------------------------------------
    QVector<Movie> movies() const;
    std::optional<Movie> movie(Id id) const;
    std::optional<Movie> movieByMedia(Id mediaFileId) const;
    void updateMovie(const Movie& movie);
    QString deleteMovie(Id id);  // returns poster file name
    ImportOutcome importMovies(const QVector<ImportItem>& items);

    // --- collections -------------------------------------------------------
    QVector<Collection> collections() const;
    std::optional<Collection> collection(Id id) const;
    Id createCollection(const QString& name);
    void updateCollection(const Collection& c);
    void deleteCollection(Id id);
    QVector<CollectionEntry> collectionEntries(Id collectionId) const;
    bool addCollectionEntry(Id collectionId, CollectionItemType type, Id itemId);
    void removeCollectionEntry(Id entryId);
    void setCollectionEntryOrder(Id collectionId, const QVector<Id>& entryIdsInOrder);

    // --- progress ----------------------------------------------------------
    Progress progress(Id mediaFileId) const;
    void saveProgress(Id mediaFileId, qint64 positionMs, qint64 durationMs);
    void touchLastWatched(Id mediaFileId);
    void saveTrackSelection(Id mediaFileId, int audioTrack, int subtitleTrack);
    void markCompleted(Id mediaFileId, qint64 durationMs);
    void setWatched(const QVector<Id>& mediaFileIds, bool watched);
    void resetProgress(const QVector<Id>& mediaFileIds);

    // Show ids that have playback history, most recently watched first.
    QVector<std::pair<Id, qint64>> showsWithHistory(int limit) const;
    // Unfinished movies that were started, most recently watched first.
    QVector<Movie> startedMovies(int limit) const;

    // --- audio/subtitle choice of a show -----------------------------------
    ShowTrackPrefs showTrackPrefs(Id showId) const;
    // Replaces the choice of one kind (audio or subtitle) and leaves the other alone.
    void setShowTrackChoice(Id showId, TrackType type, const TrackChoice& choice);

    // --- opening/ending marks of a season ----------------------------------
    SeasonSkipMarks seasonSkipMarks(Id seasonId) const;
    // Replaces the marks; marks with nothing set remove the row.
    void setSeasonSkipMarks(Id seasonId, const SeasonSkipMarks& marks);

    // --- settings ----------------------------------------------------------
    QMap<QString, QString> allSettings() const;
    void setSetting(const QString& key, const QString& value);

private:
    void cleanupOrphans();
    int nextSortOrder(const QString& table, const QString& scopeColumn, Id scopeId) const;
    Id resolveSeasonForImport(Id showId, int seasonNumber, Id fallbackSeasonId, ImportOutcome& outcome);
    QString describeMedia(Id mediaFileId) const;
    Id insertMediaFile(const QString& path, qint64 size, qint64 mtimeMs);

    QSqlDatabase m_db;
};

}  // namespace lf
