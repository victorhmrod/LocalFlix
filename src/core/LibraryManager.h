#pragma once

#include "DatabaseManager.h"
#include "DbError.h"
#include "MediaRepository.h"
#include "Types.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>

namespace lf {

struct ImportReport {
    int added = 0;
    QVector<Id> addedIds;
    QVector<DuplicateInfo> duplicates;
    QStringList rejected;  // "path — reason"
    QVector<Id> createdSeasonIds;
    bool ok = true;
    QString error;
};

struct RelinkResult {
    bool ok = false;
    QString error;
};

struct RelinkProposal {
    Id mediaFileId = kInvalidId;
    QString oldPath;
    QString newPath;
};

enum class CardKind { Show, Movie };

// A poster tile on the home screen / library grids.
struct LibraryCard {
    CardKind kind = CardKind::Show;
    Id id = kInvalidId;
    QString title;
    QString subtitle;
    QString imageFile;
    qint64 timestamp = 0;
    double progress = 0.0;  // 0..1 watched share
    bool allWatched = false;
    bool missing = false;
};

// Application-level facade over the repository: validation, signals for the
// UI, cover-image files, file recovery and queue construction. All public
// methods are for the GUI thread and never throw.
class LibraryManager : public QObject {
    Q_OBJECT
public:
    LibraryManager(DatabaseManager& db, QString coversDir, QObject* parent = nullptr);
    ~LibraryManager() override;

    MediaRepository& repository() { return m_repo; }
    DatabaseManager& database() { return m_db; }
    QString coversDir() const { return m_coversDir; }
    // Absolute path of a stored cover/poster, or empty if none / the file vanished.
    QString coverPath(const QString& fileName) const;
    // Copies an image into the covers directory; returns the stored file name.
    QString storeImage(const QString& sourcePath, QString* error = nullptr);

    static QString normalizePath(const QString& path);

    // ---- shows & seasons --------------------------------------------------
    QVector<ShowSummary> showSummaries();
    std::optional<Show> show(Id id);
    Id createShow(const QString& title);
    bool renameShow(Id id, const QString& title);
    bool setShowDescription(Id id, const QString& description);
    bool setShowCover(Id id, const QString& imagePath);
    bool clearShowCover(Id id);
    bool deleteShow(Id id);

    QVector<Season> seasons(Id showId);
    std::optional<Season> season(Id seasonId);
    Id addSeason(Id showId, const QString& title = {}, int number = -1);
    bool updateSeason(Id seasonId, const QString& title, int number);
    bool deleteSeason(Id seasonId);

    // ---- episodes ---------------------------------------------------------
    QVector<Episode> episodes(Id showId);
    std::optional<Episode> episode(Id id);
    bool updateEpisode(Id id, const QString& title, int episodeNumber);
    bool removeEpisodes(const QVector<Id>& episodeIds);
    bool applyLayout(Id showId, const QVector<SeasonLayout>& layout);
    bool moveEpisode(Id episodeId, Id toSeasonId, int toIndex);
    bool moveSeason(Id seasonId, int toIndex);

    // ---- movies -----------------------------------------------------------
    QVector<Movie> movies();
    std::optional<Movie> movie(Id id);
    bool updateMovie(Id id, const QString& title, const QString& description);
    bool setMoviePoster(Id id, const QString& imagePath);
    bool clearMoviePoster(Id id);
    bool deleteMovie(Id id);

    // ---- import (planning happens in MediaImportService) -----------------
    ImportReport importEpisodes(Id showId, Id seasonId, const QVector<ImportItem>& plan, const QStringList& rejected = {});
    ImportReport importMovies(const QVector<ImportItem>& plan, const QStringList& rejected = {});

    // ---- collections ------------------------------------------------------
    QVector<Collection> collections();
    Id createCollection(const QString& name);
    bool updateCollection(Id id, const QString& name, const QString& description);
    bool deleteCollection(Id id);
    QVector<CollectionEntry> collectionEntries(Id collectionId);
    bool addToCollection(Id collectionId, CollectionItemType type, Id itemId);
    bool removeFromCollection(Id entryId, Id collectionId);
    bool setCollectionOrder(Id collectionId, const QVector<Id>& entryIdsInOrder);

    // ---- progress ---------------------------------------------------------
    Progress progress(Id mediaFileId);
    void saveProgress(Id mediaFileId, qint64 positionMs, qint64 durationMs);
    void touchLastWatched(Id mediaFileId);
    void saveTrackSelection(Id mediaFileId, int audioTrack, int subtitleTrack);
    void markCompleted(Id mediaFileId, qint64 durationMs);
    // Records the file's duration once it is known (not on every progress save).
    void setMediaDuration(Id mediaFileId, qint64 durationMs);
    bool setWatched(const QVector<Id>& mediaFileIds, bool watched);
    bool resetProgress(const QVector<Id>& mediaFileIds);
    bool resetShowProgress(Id showId);

    // ---- audio/subtitle choice of a show ----------------------------------
    ShowTrackPrefs showTrackPrefs(Id showId);
    // Remembers the choice for one kind (audio or subtitle) of a show; the other kind is not touched.
    void saveShowTrackChoice(Id showId, TrackType type, const TrackChoice& choice);

    // ---- opening/ending marks of a season ---------------------------------
    SeasonSkipMarks seasonSkipMarks(Id seasonId);
    bool setSeasonSkipMarks(Id seasonId, const SeasonSkipMarks& marks);

    // ---- continue watching ------------------------------------------------
    ResumeTarget resumeTarget(Id showId);
    QVector<ContinueItem> continueWatching(int limit = 20);
    QVector<LibraryCard> recentlyAdded(int limit = 20);

    // ---- queue ------------------------------------------------------------
    QVector<QueueEntry> buildQueue(const QueueContext& context);
    QueueEntry entryForMedia(Id mediaFileId);

    // ---- file recovery ----------------------------------------------------
    std::optional<MediaFile> mediaFile(Id id);
    QVector<MediaFile> missingFiles();
    QVector<MediaFile> allMediaFiles();
    // Reconnects an entry to a new path. Metadata, order and progress are kept.
    RelinkResult relinkFile(Id mediaFileId, const QString& newPath);
    // After a file was relinked from oldPath to newPath, finds other missing
    // files that moved the same way: if the file name is unchanged the common
    // trailing path is stripped to get the old and new roots (so a whole moved
    // folder tree is covered); otherwise only same-folder siblings are tried.
    QVector<RelinkProposal> proposeRelinks(const QString& oldPath, const QString& newPath);
    int applyRelinks(const QVector<RelinkProposal>& proposals);
    bool setMissing(Id mediaFileId, bool missing);
    // Checks existence of files on a worker thread, then updates the missing
    // flags and emits filesVerified().
    void verifyFilesAsync(const QVector<MediaFile>& files);
    void verifyShowAsync(Id showId);
    // Blocks until running verification finished or `timeoutMs` elapsed.
    bool waitForBackgroundWork(int timeoutMs);

signals:
    // Structural change of the lists (shows/movies/collections added, removed, renamed).
    void libraryChanged();
    // Episodes/seasons of one show changed (content, order, watched state).
    void showChanged(lf::Id showId);
    void progressChanged(lf::Id mediaFileId);
    // The opening/ending marks of a season were set or cleared.
    void skipMarksChanged(lf::Id seasonId);
    void filesVerified(QVector<lf::Id> changedMediaFileIds);
    // Every background check ends with this, even when nothing changed.
    void verificationFinished(int checked, int missingTotal);
    void errorOccurred(const QString& message);

private:
    template <class R, class F>
    R guarded(F&& f, R fallback = R())
    {
        try {
            return f();
        } catch (const DbError& e) {
            reportError(e.message());
            return fallback;
        }
    }
    void reportError(const QString& message);
    void removeStoredImage(const QString& fileName);
    Id showIdOfMedia(Id mediaFileId);
    QueueEntry toEntry(const Episode& e, const QString& showTitle) const;
    QueueEntry toEntry(const Movie& m) const;

    DatabaseManager& m_db;
    MediaRepository m_repo;
    QString m_coversDir;
    QThreadPool m_verifyPool;
    std::atomic<bool> m_shuttingDown{false};
};

}  // namespace lf

Q_DECLARE_METATYPE(QVector<lf::Id>)
