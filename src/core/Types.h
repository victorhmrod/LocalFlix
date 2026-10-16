#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace lf {

using Id = qint64;
constexpr Id kInvalidId = 0;  // SQLite row ids start at 1

// Track selection stored per media file: kTrackAuto lets the player's language
// preferences decide, kTrackOff disables the stream, >0 is an mpv track id.
constexpr int kTrackAuto = -1;
constexpr int kTrackOff = 0;

// A resume position closer to the start than this is treated as "not started".
constexpr qint64 kMinResumeMs = 5000;

enum class MediaKind { Episode, Movie };

struct MediaFile {
    Id id = kInvalidId;
    QString path;
    qint64 sizeBytes = 0;
    qint64 mtimeMs = 0;
    qint64 durationMs = 0;
    bool missing = false;
    qint64 addedAt = 0;
};

struct Progress {
    Id mediaFileId = kInvalidId;
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    bool completed = false;
    int playCount = 0;
    qint64 lastWatchedAt = 0;  // 0 = never played
    int audioTrack = kTrackAuto;
    int subtitleTrack = kTrackAuto;

    // 0.0 - 1.0 for display; completed items always read as fully watched.
    double fraction() const
    {
        if (completed) return 1.0;
        if (durationMs <= 0) return 0.0;
        const double f = double(positionMs) / double(durationMs);
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }
    // The position playback should start from when this item is simply "played".
    qint64 resumePositionMs() const
    {
        if (completed || positionMs < kMinResumeMs) return 0;
        return positionMs;
    }
    bool started() const { return !completed && positionMs >= kMinResumeMs; }
};

// Where the opening and the ending are in the episodes of one season, for the
// files whose chapters do not say. The opening is an absolute stretch of the
// file; the ending is "the last N ms", so it fits episodes of different lengths.
struct SeasonSkipMarks {
    qint64 introStartMs = -1;
    qint64 introEndMs = -1;
    qint64 endingFromEndMs = -1;

    bool hasIntro() const { return introStartMs >= 0 && introEndMs > introStartMs; }
    bool hasEnding() const { return endingFromEndMs > 0; }
    bool any() const { return hasIntro() || hasEnding(); }
};

struct Show {
    Id id = kInvalidId;
    QString title;
    QString description;
    QString coverFile;  // file name inside the app's covers directory ("" = none)
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct Season {
    Id id = kInvalidId;
    Id showId = kInvalidId;
    int number = 0;  // 0 = unnumbered (specials, extras)
    QString title;
    int sortOrder = 0;
};

struct Episode {
    Id id = kInvalidId;
    Id seasonId = kInvalidId;
    Id showId = kInvalidId;
    int seasonSortOrder = 0;
    int seasonNumber = 0;
    QString title;
    int episodeNumber = 0;  // 0 = unknown
    int sortOrder = 0;
    qint64 createdAt = 0;
    MediaFile file;
    Progress progress;
};

struct Movie {
    Id id = kInvalidId;
    QString title;
    QString description;
    QString posterFile;
    qint64 createdAt = 0;
    MediaFile file;
    Progress progress;
};

struct Collection {
    Id id = kInvalidId;
    QString name;
    QString description;
    qint64 createdAt = 0;
    int entryCount = 0;
};

enum class CollectionItemType { Show, Movie };

struct CollectionEntry {
    Id id = kInvalidId;
    Id collectionId = kInvalidId;
    CollectionItemType type = CollectionItemType::Show;
    Id itemId = kInvalidId;
    int sortOrder = 0;
    QString title;  // resolved from the show / movie
};

// Aggregates shown on show cards and the show details header.
struct ShowSummary {
    Show show;
    int seasonCount = 0;
    int episodeCount = 0;
    int completedCount = 0;
    qint64 lastWatchedAt = 0;
};

// One playable thing in the queue: an episode or a movie.
struct QueueEntry {
    MediaKind kind = MediaKind::Episode;
    Id itemId = kInvalidId;  // episode id or movie id
    Id showId = kInvalidId;  // 0 for movies
    Id seasonId = kInvalidId;  // 0 for movies
    Id mediaFileId = kInvalidId;
    QString title;       // "S1E3 · The Title" or the movie title
    QString contextTitle;  // show title, or empty for movies
    QString path;
    bool missing = false;
    bool completed = false;
    qint64 positionMs = 0;
    qint64 durationMs = 0;

    bool valid() const { return mediaFileId != kInvalidId; }
    QString displayTitle() const
    {
        return contextTitle.isEmpty() ? title : contextTitle + QStringLiteral(" — ") + title;
    }
};

// Where an item came from, for queue construction.
enum class ContextType { None, Show, Movie, Collection };
struct QueueContext {
    ContextType type = ContextType::None;
    Id id = kInvalidId;
    bool operator==(const QueueContext& o) const { return type == o.type && id == o.id; }
};

// Series-level "what do I watch next" answer.
enum class ResumeReason {
    None,           // nothing to play (empty show)
    InProgress,     // an episode was left partway through
    NextUnfinished, // the last episode finished; continue with the next unfinished one
    NotStarted,     // nothing watched yet; start at the first unfinished episode
    AllCompleted    // everything is watched; target is the first episode (replay)
};

struct ResumeTarget {
    ResumeReason reason = ResumeReason::None;
    Id episodeId = kInvalidId;
    Id mediaFileId = kInvalidId;
    qint64 positionMs = 0;  // 0 when starting from the beginning

    bool valid() const { return reason != ResumeReason::None; }
    bool allCompleted() const { return reason == ResumeReason::AllCompleted; }
};

// Used by the Continue Watching section.
struct ContinueItem {
    MediaKind kind = MediaKind::Episode;
    Id showId = kInvalidId;   // 0 for movies
    Id movieId = kInvalidId;  // 0 for shows
    QString title;            // show or movie title
    QString subtitle;         // current episode title ("S1E3 · Name") or empty
    QString coverFile;
    Id mediaFileId = kInvalidId;  // the item that will start
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    qint64 lastWatchedAt = 0;
    bool missing = false;
    double fraction() const
    {
        return durationMs > 0 ? qBound(0.0, double(positionMs) / double(durationMs), 1.0) : 0.0;
    }
};

// Layout of a show used by drag-and-drop reordering: seasons in order, each
// with its episodes in order. Applying it is a single transaction.
struct SeasonLayout {
    Id seasonId = kInvalidId;
    QVector<Id> episodeIds;
};

}  // namespace lf

Q_DECLARE_METATYPE(lf::QueueEntry)
