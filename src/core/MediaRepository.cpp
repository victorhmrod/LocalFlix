#include "MediaRepository.h"

#include "Clock.h"
#include "Format.h"
#include "SqlUtil.h"

#include <QSet>

namespace lf {

namespace {

// Column groups shared by several queries.
constexpr const char* kMediaCols =
    "m.id, m.path, m.size_bytes, m.mtime_ms, m.duration_ms, m.is_missing, m.added_at";
constexpr const char* kProgressCols =
    "COALESCE(p.position_ms,0), COALESCE(p.duration_ms,0), COALESCE(p.completed,0), "
    "COALESCE(p.play_count,0), COALESCE(p.last_watched_at,0), COALESCE(p.audio_track,-1), "
    "COALESCE(p.subtitle_track,-1)";

MediaFile readMedia(const Stmt& s, int o)
{
    MediaFile f;
    f.id = s.i64(o);
    f.path = s.str(o + 1);
    f.sizeBytes = s.i64(o + 2);
    f.mtimeMs = s.i64(o + 3);
    f.durationMs = s.i64(o + 4);
    f.missing = s.flag(o + 5);
    f.addedAt = s.i64(o + 6);
    return f;
}

Progress readProgress(const Stmt& s, int o, Id mediaFileId)
{
    Progress p;
    p.mediaFileId = mediaFileId;
    p.positionMs = s.i64(o);
    p.durationMs = s.i64(o + 1);
    p.completed = s.flag(o + 2);
    p.playCount = s.i32(o + 3);
    p.lastWatchedAt = s.i64(o + 4);
    p.audioTrack = s.i32(o + 5);
    p.subtitleTrack = s.i32(o + 6);
    return p;
}

QString episodeSelect()
{
    return QStringLiteral(
               "SELECT e.id, e.season_id, s.show_id, s.sort_order, s.season_number, e.title, "
               "e.episode_number, e.sort_order, e.created_at, ")
        + QLatin1String(kMediaCols) + QStringLiteral(", ") + QLatin1String(kProgressCols)
        + QStringLiteral(
               " FROM episodes e JOIN seasons s ON s.id = e.season_id "
               "JOIN media_files m ON m.id = e.media_file_id "
               "LEFT JOIN playback_progress p ON p.media_file_id = m.id ");
}

Episode readEpisode(const Stmt& s)
{
    Episode e;
    e.id = s.i64(0);
    e.seasonId = s.i64(1);
    e.showId = s.i64(2);
    e.seasonSortOrder = s.i32(3);
    e.seasonNumber = s.i32(4);
    e.title = s.str(5);
    e.episodeNumber = s.i32(6);
    e.sortOrder = s.i32(7);
    e.createdAt = s.i64(8);
    e.file = readMedia(s, 9);
    e.progress = readProgress(s, 16, e.file.id);
    return e;
}

QString movieSelect()
{
    return QStringLiteral("SELECT mv.id, mv.title, mv.description, mv.poster_file, mv.created_at, ")
        + QLatin1String(kMediaCols) + QStringLiteral(", ") + QLatin1String(kProgressCols)
        + QStringLiteral(
               " FROM movies mv JOIN media_files m ON m.id = mv.media_file_id "
               "LEFT JOIN playback_progress p ON p.media_file_id = m.id ");
}

Movie readMovie(const Stmt& s)
{
    Movie mv;
    mv.id = s.i64(0);
    mv.title = s.str(1);
    mv.description = s.str(2);
    mv.posterFile = s.str(3);
    mv.createdAt = s.i64(4);
    mv.file = readMedia(s, 5);
    mv.progress = readProgress(s, 12, mv.file.id);
    return mv;
}

Show readShow(const Stmt& s)
{
    Show sh;
    sh.id = s.i64(0);
    sh.title = s.str(1);
    sh.description = s.str(2);
    sh.coverFile = s.str(3);
    sh.createdAt = s.i64(4);
    sh.updatedAt = s.i64(5);
    return sh;
}

Season readSeason(const Stmt& s)
{
    Season se;
    se.id = s.i64(0);
    se.showId = s.i64(1);
    se.number = s.i32(2);
    se.title = s.str(3);
    se.sortOrder = s.i32(4);
    return se;
}

}  // namespace

// ------------------------------------------------------------------ media files

std::optional<MediaFile> MediaRepository::mediaFile(Id id) const
{
    Stmt s(m_db, QStringLiteral("SELECT %1 FROM media_files m WHERE m.id = ?").arg(QLatin1String(kMediaCols)));
    s.bind(id).run();
    if (!s.next()) return std::nullopt;
    return readMedia(s, 0);
}

std::optional<MediaFile> MediaRepository::mediaFileByPath(const QString& path) const
{
    Stmt s(m_db, QStringLiteral("SELECT %1 FROM media_files m WHERE m.path = ?").arg(QLatin1String(kMediaCols)));
    s.bind(path).run();
    if (!s.next()) return std::nullopt;
    return readMedia(s, 0);
}

QVector<MediaFile> MediaRepository::allMediaFiles() const
{
    Stmt s(m_db, QStringLiteral("SELECT %1 FROM media_files m ORDER BY m.id").arg(QLatin1String(kMediaCols)));
    s.run();
    QVector<MediaFile> out;
    while (s.next()) out.push_back(readMedia(s, 0));
    return out;
}

QVector<MediaFile> MediaRepository::mediaFilesOfShow(Id showId) const
{
    Stmt s(m_db, QStringLiteral("SELECT %1 FROM media_files m JOIN episodes e ON e.media_file_id = m.id "
                                "JOIN seasons se ON se.id = e.season_id WHERE se.show_id = ? ORDER BY m.id")
                     .arg(QLatin1String(kMediaCols)));
    s.bind(showId).run();
    QVector<MediaFile> out;
    while (s.next()) out.push_back(readMedia(s, 0));
    return out;
}

QVector<MediaFile> MediaRepository::missingMediaFiles() const
{
    Stmt s(m_db, QStringLiteral("SELECT %1 FROM media_files m WHERE m.is_missing = 1 ORDER BY m.path").arg(QLatin1String(kMediaCols)));
    s.run();
    QVector<MediaFile> out;
    while (s.next()) out.push_back(readMedia(s, 0));
    return out;
}

Id MediaRepository::insertMediaFile(const QString& path, qint64 size, qint64 mtimeMs)
{
    Stmt s(m_db, QStringLiteral("INSERT INTO media_files(path, size_bytes, mtime_ms, added_at) VALUES(?,?,?,?)"));
    s.bind(path).bind(size).bind(mtimeMs).bind(Clock::nowMs()).run();
    return s.lastInsertId();
}

void MediaRepository::relinkMediaFile(Id id, const QString& newPath, qint64 sizeBytes, qint64 mtimeMs)
{
    const int n = execSql(m_db,
                          QStringLiteral("UPDATE media_files SET path = ?, size_bytes = ?, mtime_ms = ?, is_missing = 0 WHERE id = ?"),
                          {newPath, sizeBytes, mtimeMs, id});
    if (n != 1) throw DbError(QStringLiteral("relinkMediaFile: no media file with id %1").arg(id));
}

void MediaRepository::setMissing(const QVector<Id>& ids, bool missing)
{
    if (ids.isEmpty()) return;
    Transaction tx(m_db);
    for (Id id : ids)
        execSql(m_db, QStringLiteral("UPDATE media_files SET is_missing = ? WHERE id = ? AND is_missing <> ?"),
                {missing ? 1 : 0, id, missing ? 1 : 0});
    tx.commit();
}

void MediaRepository::setMediaDuration(Id id, qint64 durationMs)
{
    if (durationMs <= 0) return;
    execSql(m_db, QStringLiteral("UPDATE media_files SET duration_ms = ? WHERE id = ?"), {durationMs, id});
}

void MediaRepository::cleanupOrphans()
{
    // Files that no episode or movie references any more (and their progress,
    // via ON DELETE CASCADE). Only library rows are removed, never files on disk.
    execSql(m_db, QStringLiteral("DELETE FROM media_files WHERE id NOT IN (SELECT media_file_id FROM episodes) "
                                 "AND id NOT IN (SELECT media_file_id FROM movies)"));
}

int MediaRepository::nextSortOrder(const QString& table, const QString& scopeColumn, Id scopeId) const
{
    Stmt s(m_db, QStringLiteral("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM %1 WHERE %2 = ?").arg(table, scopeColumn));
    s.bind(scopeId).run();
    return s.next() ? s.i32(0) : 0;
}

QString MediaRepository::describeMedia(Id mediaFileId) const
{
    {
        Stmt s(m_db, QStringLiteral("SELECT sh.title, se.season_number, e.episode_number, e.title FROM episodes e "
                                    "JOIN seasons se ON se.id = e.season_id JOIN shows sh ON sh.id = se.show_id "
                                    "WHERE e.media_file_id = ?"));
        s.bind(mediaFileId).run();
        if (s.next())
            return s.str(0) + QStringLiteral(" — ") + episodeLabel(s.i32(1), s.i32(2), s.str(3));
    }
    {
        Stmt s(m_db, QStringLiteral("SELECT title FROM movies WHERE media_file_id = ?"));
        s.bind(mediaFileId).run();
        if (s.next()) return QStringLiteral("Movie: ") + s.str(0);
    }
    return QStringLiteral("the library");
}

// ------------------------------------------------------------------ shows

QVector<ShowSummary> MediaRepository::showSummaries() const
{
    Stmt s(m_db, QStringLiteral(
                     "SELECT sh.id, sh.title, sh.description, sh.cover_file, sh.created_at, sh.updated_at, "
                     "(SELECT COUNT(*) FROM seasons WHERE show_id = sh.id), "
                     "(SELECT COUNT(*) FROM episodes e JOIN seasons se ON se.id = e.season_id WHERE se.show_id = sh.id), "
                     "(SELECT COUNT(*) FROM episodes e JOIN seasons se ON se.id = e.season_id "
                     "   JOIN playback_progress p ON p.media_file_id = e.media_file_id "
                     "   WHERE se.show_id = sh.id AND p.completed = 1), "
                     "(SELECT COALESCE(MAX(p.last_watched_at), 0) FROM episodes e JOIN seasons se ON se.id = e.season_id "
                     "   JOIN playback_progress p ON p.media_file_id = e.media_file_id WHERE se.show_id = sh.id) "
                     "FROM shows sh ORDER BY sh.title COLLATE NOCASE, sh.id"));
    s.run();
    QVector<ShowSummary> out;
    while (s.next()) {
        ShowSummary sum;
        sum.show = readShow(s);
        sum.seasonCount = s.i32(6);
        sum.episodeCount = s.i32(7);
        sum.completedCount = s.i32(8);
        sum.lastWatchedAt = s.i64(9);
        out.push_back(sum);
    }
    return out;
}

std::optional<Show> MediaRepository::show(Id id) const
{
    Stmt s(m_db, QStringLiteral("SELECT id, title, description, cover_file, created_at, updated_at FROM shows WHERE id = ?"));
    s.bind(id).run();
    if (!s.next()) return std::nullopt;
    return readShow(s);
}

Id MediaRepository::createShow(const QString& title)
{
    const qint64 now = Clock::nowMs();
    Stmt s(m_db, QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES(?,?,?)"));
    s.bind(title).bind(now).bind(now).run();
    return s.lastInsertId();
}

void MediaRepository::updateShow(const Show& show)
{
    const int n = execSql(m_db,
                          QStringLiteral("UPDATE shows SET title = ?, description = ?, cover_file = ?, updated_at = ? WHERE id = ?"),
                          {show.title, show.description, show.coverFile, Clock::nowMs(), show.id});
    if (n != 1) throw DbError(QStringLiteral("updateShow: no show with id %1").arg(show.id));
}

QString MediaRepository::deleteShow(Id id)
{
    Transaction tx(m_db);
    QString cover;
    {
        Stmt s(m_db, QStringLiteral("SELECT cover_file FROM shows WHERE id = ?"));
        s.bind(id).run();
        if (s.next()) cover = s.str(0);
    }
    execSql(m_db, QStringLiteral("DELETE FROM shows WHERE id = ?"), {id});  // cascades to seasons/episodes
    cleanupOrphans();
    tx.commit();
    return cover;
}

// ------------------------------------------------------------------ seasons

QVector<Season> MediaRepository::seasons(Id showId) const
{
    Stmt s(m_db, QStringLiteral("SELECT id, show_id, season_number, title, sort_order FROM seasons WHERE show_id = ? "
                                "ORDER BY sort_order, id"));
    s.bind(showId).run();
    QVector<Season> out;
    while (s.next()) out.push_back(readSeason(s));
    return out;
}

std::optional<Season> MediaRepository::season(Id id) const
{
    Stmt s(m_db, QStringLiteral("SELECT id, show_id, season_number, title, sort_order FROM seasons WHERE id = ?"));
    s.bind(id).run();
    if (!s.next()) return std::nullopt;
    return readSeason(s);
}

Id MediaRepository::createSeason(Id showId, const QString& title, int number)
{
    const int order = nextSortOrder(QStringLiteral("seasons"), QStringLiteral("show_id"), showId);
    Stmt s(m_db, QStringLiteral("INSERT INTO seasons(show_id, season_number, title, sort_order) VALUES(?,?,?,?)"));
    s.bind(showId).bind(number).bind(title).bind(order).run();
    return s.lastInsertId();
}

void MediaRepository::updateSeason(Id id, const QString& title, int number)
{
    const int n = execSql(m_db, QStringLiteral("UPDATE seasons SET title = ?, season_number = ? WHERE id = ?"), {title, number, id});
    if (n != 1) throw DbError(QStringLiteral("updateSeason: no season with id %1").arg(id));
}

void MediaRepository::deleteSeason(Id id)
{
    Transaction tx(m_db);
    execSql(m_db, QStringLiteral("DELETE FROM seasons WHERE id = ?"), {id});  // cascades to episodes
    cleanupOrphans();
    tx.commit();
}

// ------------------------------------------------------------------ episodes

QVector<Episode> MediaRepository::episodes(Id showId) const
{
    Stmt s(m_db, episodeSelect() + QStringLiteral("WHERE s.show_id = ? ORDER BY s.sort_order, s.id, e.sort_order, e.id"));
    s.bind(showId).run();
    QVector<Episode> out;
    while (s.next()) out.push_back(readEpisode(s));
    return out;
}

std::optional<Episode> MediaRepository::episode(Id id) const
{
    Stmt s(m_db, episodeSelect() + QStringLiteral("WHERE e.id = ?"));
    s.bind(id).run();
    if (!s.next()) return std::nullopt;
    return readEpisode(s);
}

std::optional<Episode> MediaRepository::episodeByMedia(Id mediaFileId) const
{
    Stmt s(m_db, episodeSelect() + QStringLiteral("WHERE e.media_file_id = ?"));
    s.bind(mediaFileId).run();
    if (!s.next()) return std::nullopt;
    return readEpisode(s);
}

void MediaRepository::updateEpisode(Id id, const QString& title, int episodeNumber)
{
    const int n = execSql(m_db, QStringLiteral("UPDATE episodes SET title = ?, episode_number = ? WHERE id = ?"),
                          {title, episodeNumber, id});
    if (n != 1) throw DbError(QStringLiteral("updateEpisode: no episode with id %1").arg(id));
}

void MediaRepository::deleteEpisodes(const QVector<Id>& ids)
{
    if (ids.isEmpty()) return;
    Transaction tx(m_db);
    for (Id id : ids) execSql(m_db, QStringLiteral("DELETE FROM episodes WHERE id = ?"), {id});
    cleanupOrphans();
    tx.commit();
}

void MediaRepository::applyLayout(Id showId, const QVector<SeasonLayout>& layout)
{
    Transaction tx(m_db);

    QSet<Id> knownSeasons;
    QSet<Id> knownEpisodes;
    {
        Stmt s(m_db, QStringLiteral("SELECT id FROM seasons WHERE show_id = ?"));
        s.bind(showId).run();
        while (s.next()) knownSeasons.insert(s.i64(0));
    }
    {
        Stmt s(m_db, QStringLiteral("SELECT e.id FROM episodes e JOIN seasons se ON se.id = e.season_id WHERE se.show_id = ?"));
        s.bind(showId).run();
        while (s.next()) knownEpisodes.insert(s.i64(0));
    }

    QSet<Id> seenSeasons;
    QSet<Id> seenEpisodes;
    for (const SeasonLayout& sl : layout) {
        if (!knownSeasons.contains(sl.seasonId) || seenSeasons.contains(sl.seasonId))
            throw DbError(QStringLiteral("applyLayout: season %1 is not part of show %2 or repeated").arg(sl.seasonId).arg(showId));
        seenSeasons.insert(sl.seasonId);
        for (Id eid : sl.episodeIds) {
            if (!knownEpisodes.contains(eid) || seenEpisodes.contains(eid))
                throw DbError(QStringLiteral("applyLayout: episode %1 is not part of show %2 or repeated").arg(eid).arg(showId));
            seenEpisodes.insert(eid);
        }
    }
    if (seenSeasons != knownSeasons || seenEpisodes != knownEpisodes)
        throw DbError(QStringLiteral("applyLayout: layout does not cover every season and episode of show %1").arg(showId));

    int seasonOrder = 0;
    for (const SeasonLayout& sl : layout) {
        execSql(m_db, QStringLiteral("UPDATE seasons SET sort_order = ? WHERE id = ?"), {seasonOrder++, sl.seasonId});
        int episodeOrder = 0;
        for (Id eid : sl.episodeIds)
            execSql(m_db, QStringLiteral("UPDATE episodes SET season_id = ?, sort_order = ? WHERE id = ?"),
                    {sl.seasonId, episodeOrder++, eid});
    }
    tx.commit();
}

Id MediaRepository::resolveSeasonForImport(Id showId, int seasonNumber, Id fallbackSeasonId, ImportOutcome& outcome)
{
    if (seasonNumber < 0) {
        if (fallbackSeasonId != kInvalidId) return fallbackSeasonId;
        const QVector<Season> existing = seasons(showId);
        if (!existing.isEmpty()) return existing.first().id;
        const Id created = createSeason(showId, QStringLiteral("Season 1"), 1);
        outcome.createdSeasonIds.push_back(created);
        return created;
    }

    {
        Stmt s(m_db, QStringLiteral("SELECT id FROM seasons WHERE show_id = ? AND season_number = ? ORDER BY sort_order LIMIT 1"));
        s.bind(showId).bind(seasonNumber).run();
        if (s.next()) return s.i64(0);
    }

    const QString title = seasonNumber == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(seasonNumber);
    // Keep numbered seasons ascending: slot the new one before the first
    // numbered season with a higher number, otherwise append.
    int position = -1;
    if (seasonNumber > 0) {
        Stmt s(m_db, QStringLiteral("SELECT MIN(sort_order) FROM seasons WHERE show_id = ? AND season_number > ?"));
        s.bind(showId).bind(seasonNumber).run();
        if (s.next() && !s.value(0).isNull()) position = s.i32(0);
    }
    if (position < 0) {
        position = nextSortOrder(QStringLiteral("seasons"), QStringLiteral("show_id"), showId);
    } else {
        execSql(m_db, QStringLiteral("UPDATE seasons SET sort_order = sort_order + 1 WHERE show_id = ? AND sort_order >= ?"),
                {showId, position});
    }
    Stmt ins(m_db, QStringLiteral("INSERT INTO seasons(show_id, season_number, title, sort_order) VALUES(?,?,?,?)"));
    ins.bind(showId).bind(seasonNumber).bind(title).bind(position).run();
    const Id created = ins.lastInsertId();
    outcome.createdSeasonIds.push_back(created);
    return created;
}

ImportOutcome MediaRepository::importEpisodes(Id showId, Id fallbackSeasonId, const QVector<ImportItem>& items)
{
    ImportOutcome outcome;
    Transaction tx(m_db);
    for (const ImportItem& item : items) {
        if (const auto existing = mediaFileByPath(item.path)) {
            outcome.duplicates.push_back({item.path, describeMedia(existing->id)});
            continue;
        }
        const Id seasonId = resolveSeasonForImport(showId, item.seasonNumber, fallbackSeasonId, outcome);
        const Id mediaId = insertMediaFile(item.path, item.sizeBytes, item.mtimeMs);
        const int order = nextSortOrder(QStringLiteral("episodes"), QStringLiteral("season_id"), seasonId);
        Stmt s(m_db, QStringLiteral("INSERT INTO episodes(season_id, media_file_id, title, episode_number, sort_order, created_at) "
                                    "VALUES(?,?,?,?,?,?)"));
        s.bind(seasonId).bind(mediaId).bind(item.title).bind(item.episodeNumber).bind(order).bind(Clock::nowMs()).run();
        outcome.addedItemIds.push_back(s.lastInsertId());
    }
    if (!outcome.addedItemIds.isEmpty()) {
        execSql(m_db, QStringLiteral("UPDATE shows SET updated_at = ? WHERE id = ?"), {Clock::nowMs(), showId});
    }
    tx.commit();
    return outcome;
}

// ------------------------------------------------------------------ movies

QVector<Movie> MediaRepository::movies() const
{
    Stmt s(m_db, movieSelect() + QStringLiteral("ORDER BY mv.title COLLATE NOCASE, mv.id"));
    s.run();
    QVector<Movie> out;
    while (s.next()) out.push_back(readMovie(s));
    return out;
}

std::optional<Movie> MediaRepository::movie(Id id) const
{
    Stmt s(m_db, movieSelect() + QStringLiteral("WHERE mv.id = ?"));
    s.bind(id).run();
    if (!s.next()) return std::nullopt;
    return readMovie(s);
}

std::optional<Movie> MediaRepository::movieByMedia(Id mediaFileId) const
{
    Stmt s(m_db, movieSelect() + QStringLiteral("WHERE mv.media_file_id = ?"));
    s.bind(mediaFileId).run();
    if (!s.next()) return std::nullopt;
    return readMovie(s);
}

void MediaRepository::updateMovie(const Movie& movie)
{
    const int n = execSql(m_db,
                          QStringLiteral("UPDATE movies SET title = ?, description = ?, poster_file = ? WHERE id = ?"),
                          {movie.title, movie.description, movie.posterFile, movie.id});
    if (n != 1) throw DbError(QStringLiteral("updateMovie: no movie with id %1").arg(movie.id));
}

QString MediaRepository::deleteMovie(Id id)
{
    Transaction tx(m_db);
    QString poster;
    {
        Stmt s(m_db, QStringLiteral("SELECT poster_file FROM movies WHERE id = ?"));
        s.bind(id).run();
        if (s.next()) poster = s.str(0);
    }
    execSql(m_db, QStringLiteral("DELETE FROM movies WHERE id = ?"), {id});
    cleanupOrphans();
    tx.commit();
    return poster;
}

ImportOutcome MediaRepository::importMovies(const QVector<ImportItem>& items)
{
    ImportOutcome outcome;
    Transaction tx(m_db);
    for (const ImportItem& item : items) {
        if (const auto existing = mediaFileByPath(item.path)) {
            outcome.duplicates.push_back({item.path, describeMedia(existing->id)});
            continue;
        }
        const Id mediaId = insertMediaFile(item.path, item.sizeBytes, item.mtimeMs);
        Stmt s(m_db, QStringLiteral("INSERT INTO movies(media_file_id, title, created_at) VALUES(?,?,?)"));
        s.bind(mediaId).bind(item.title).bind(Clock::nowMs()).run();
        outcome.addedItemIds.push_back(s.lastInsertId());
    }
    tx.commit();
    return outcome;
}

// ------------------------------------------------------------------ collections

QVector<Collection> MediaRepository::collections() const
{
    Stmt s(m_db, QStringLiteral("SELECT c.id, c.name, c.description, c.created_at, "
                                "(SELECT COUNT(*) FROM collection_entries WHERE collection_id = c.id) "
                                "FROM collections c ORDER BY c.name COLLATE NOCASE, c.id"));
    s.run();
    QVector<Collection> out;
    while (s.next()) {
        Collection c;
        c.id = s.i64(0);
        c.name = s.str(1);
        c.description = s.str(2);
        c.createdAt = s.i64(3);
        c.entryCount = s.i32(4);
        out.push_back(c);
    }
    return out;
}

std::optional<Collection> MediaRepository::collection(Id id) const
{
    const QVector<Collection> all = collections();
    for (const Collection& c : all)
        if (c.id == id) return c;
    return std::nullopt;
}

Id MediaRepository::createCollection(const QString& name)
{
    Stmt s(m_db, QStringLiteral("INSERT INTO collections(name, created_at) VALUES(?,?)"));
    s.bind(name).bind(Clock::nowMs()).run();
    return s.lastInsertId();
}

void MediaRepository::updateCollection(const Collection& c)
{
    const int n = execSql(m_db, QStringLiteral("UPDATE collections SET name = ?, description = ? WHERE id = ?"),
                          {c.name, c.description, c.id});
    if (n != 1) throw DbError(QStringLiteral("updateCollection: no collection with id %1").arg(c.id));
}

void MediaRepository::deleteCollection(Id id)
{
    execSql(m_db, QStringLiteral("DELETE FROM collections WHERE id = ?"), {id});  // entries cascade
}

QVector<CollectionEntry> MediaRepository::collectionEntries(Id collectionId) const
{
    Stmt s(m_db, QStringLiteral(
                     "SELECT ce.id, ce.collection_id, ce.item_type, ce.item_id, ce.sort_order, "
                     "CASE ce.item_type WHEN 'show' THEN (SELECT title FROM shows WHERE id = ce.item_id) "
                     "ELSE (SELECT title FROM movies WHERE id = ce.item_id) END "
                     "FROM collection_entries ce WHERE ce.collection_id = ? ORDER BY ce.sort_order, ce.id"));
    s.bind(collectionId).run();
    QVector<CollectionEntry> out;
    while (s.next()) {
        CollectionEntry e;
        e.id = s.i64(0);
        e.collectionId = s.i64(1);
        e.type = s.str(2) == QStringLiteral("show") ? CollectionItemType::Show : CollectionItemType::Movie;
        e.itemId = s.i64(3);
        e.sortOrder = s.i32(4);
        e.title = s.str(5);
        out.push_back(e);
    }
    return out;
}

bool MediaRepository::addCollectionEntry(Id collectionId, CollectionItemType type, Id itemId)
{
    const int order = nextSortOrder(QStringLiteral("collection_entries"), QStringLiteral("collection_id"), collectionId);
    const int n = execSql(m_db,
                          QStringLiteral("INSERT OR IGNORE INTO collection_entries(collection_id, item_type, item_id, sort_order) "
                                         "VALUES(?,?,?,?)"),
                          {collectionId, type == CollectionItemType::Show ? QStringLiteral("show") : QStringLiteral("movie"),
                           itemId, order});
    return n == 1;
}

void MediaRepository::removeCollectionEntry(Id entryId)
{
    execSql(m_db, QStringLiteral("DELETE FROM collection_entries WHERE id = ?"), {entryId});
}

void MediaRepository::setCollectionEntryOrder(Id collectionId, const QVector<Id>& entryIdsInOrder)
{
    Transaction tx(m_db);
    int order = 0;
    for (Id id : entryIdsInOrder)
        execSql(m_db, QStringLiteral("UPDATE collection_entries SET sort_order = ? WHERE id = ? AND collection_id = ?"),
                {order++, id, collectionId});
    tx.commit();
}

// ------------------------------------------------------------------ progress

Progress MediaRepository::progress(Id mediaFileId) const
{
    Stmt s(m_db, QStringLiteral("SELECT position_ms, duration_ms, completed, play_count, last_watched_at, audio_track, subtitle_track "
                                "FROM playback_progress WHERE media_file_id = ?"));
    s.bind(mediaFileId).run();
    Progress p;
    p.mediaFileId = mediaFileId;
    if (s.next()) p = readProgress(s, 0, mediaFileId);
    return p;
}

void MediaRepository::saveProgress(Id mediaFileId, qint64 positionMs, qint64 durationMs)
{
    const qint64 now = Clock::nowMs();
    execSql(m_db,
            QStringLiteral("INSERT INTO playback_progress(media_file_id, position_ms, duration_ms, last_watched_at, updated_at) "
                           "VALUES(?,?,?,?,?) ON CONFLICT(media_file_id) DO UPDATE SET "
                           "position_ms = excluded.position_ms, "
                           "duration_ms = CASE WHEN excluded.duration_ms > 0 THEN excluded.duration_ms ELSE duration_ms END, "
                           "last_watched_at = excluded.last_watched_at, updated_at = excluded.updated_at"),
            {mediaFileId, positionMs, durationMs, now, now});
}

void MediaRepository::touchLastWatched(Id mediaFileId)
{
    const qint64 now = Clock::nowMs();
    execSql(m_db,
            QStringLiteral("INSERT INTO playback_progress(media_file_id, last_watched_at, updated_at) VALUES(?,?,?) "
                           "ON CONFLICT(media_file_id) DO UPDATE SET last_watched_at = excluded.last_watched_at, "
                           "updated_at = excluded.updated_at"),
            {mediaFileId, now, now});
}

void MediaRepository::saveTrackSelection(Id mediaFileId, int audioTrack, int subtitleTrack)
{
    execSql(m_db,
            QStringLiteral("INSERT INTO playback_progress(media_file_id, audio_track, subtitle_track, updated_at) VALUES(?,?,?,?) "
                           "ON CONFLICT(media_file_id) DO UPDATE SET audio_track = excluded.audio_track, "
                           "subtitle_track = excluded.subtitle_track, updated_at = excluded.updated_at"),
            {mediaFileId, audioTrack, subtitleTrack, Clock::nowMs()});
}

void MediaRepository::markCompleted(Id mediaFileId, qint64 durationMs)
{
    const qint64 now = Clock::nowMs();
    execSql(m_db,
            QStringLiteral("INSERT INTO playback_progress(media_file_id, position_ms, duration_ms, completed, play_count, last_watched_at, updated_at) "
                           "VALUES(?,0,?,1,1,?,?) ON CONFLICT(media_file_id) DO UPDATE SET "
                           "position_ms = 0, completed = 1, play_count = play_count + 1, "
                           "duration_ms = CASE WHEN excluded.duration_ms > 0 THEN excluded.duration_ms ELSE duration_ms END, "
                           "last_watched_at = excluded.last_watched_at, updated_at = excluded.updated_at"),
            {mediaFileId, durationMs, now, now});
}

void MediaRepository::setWatched(const QVector<Id>& mediaFileIds, bool watched)
{
    if (mediaFileIds.isEmpty()) return;
    Transaction tx(m_db);
    for (Id id : mediaFileIds) {
        execSql(m_db,
                QStringLiteral("INSERT INTO playback_progress(media_file_id, completed, updated_at) VALUES(?,?,?) "
                               "ON CONFLICT(media_file_id) DO UPDATE SET completed = excluded.completed, position_ms = 0, "
                               "updated_at = excluded.updated_at"),
                {id, watched ? 1 : 0, Clock::nowMs()});
    }
    tx.commit();
}

void MediaRepository::resetProgress(const QVector<Id>& mediaFileIds)
{
    if (mediaFileIds.isEmpty()) return;
    Transaction tx(m_db);
    for (Id id : mediaFileIds)
        execSql(m_db,
                QStringLiteral("UPDATE playback_progress SET position_ms = 0, completed = 0, last_watched_at = 0, updated_at = ? "
                               "WHERE media_file_id = ?"),
                {Clock::nowMs(), id});
    tx.commit();
}

QVector<std::pair<Id, qint64>> MediaRepository::showsWithHistory(int limit) const
{
    Stmt s(m_db, QStringLiteral("SELECT se.show_id, MAX(p.last_watched_at) AS lw FROM episodes e "
                                "JOIN seasons se ON se.id = e.season_id "
                                "JOIN playback_progress p ON p.media_file_id = e.media_file_id "
                                "WHERE p.last_watched_at > 0 GROUP BY se.show_id ORDER BY lw DESC LIMIT ?"));
    s.bind(limit).run();
    QVector<std::pair<Id, qint64>> out;
    while (s.next()) out.push_back({s.i64(0), s.i64(1)});
    return out;
}

QVector<Movie> MediaRepository::startedMovies(int limit) const
{
    Stmt s(m_db, movieSelect() + QStringLiteral("WHERE p.last_watched_at > 0 AND p.completed = 0 AND p.position_ms >= ? "
                                                "ORDER BY p.last_watched_at DESC LIMIT ?"));
    s.bind(kMinResumeMs).bind(limit).run();
    QVector<Movie> out;
    while (s.next()) out.push_back(readMovie(s));
    return out;
}

// ------------------------------------------------------------------ season skip marks

SeasonSkipMarks MediaRepository::seasonSkipMarks(Id seasonId) const
{
    Stmt s(m_db, QStringLiteral("SELECT intro_start_ms, intro_end_ms, ending_from_end_ms FROM season_skip_marks WHERE season_id = ?"));
    s.bind(seasonId).run();
    SeasonSkipMarks marks;
    if (s.next()) {
        marks.introStartMs = s.i64(0);
        marks.introEndMs = s.i64(1);
        marks.endingFromEndMs = s.i64(2);
    }
    return marks;
}

void MediaRepository::setSeasonSkipMarks(Id seasonId, const SeasonSkipMarks& marks)
{
    if (!marks.any()) {
        execSql(m_db, QStringLiteral("DELETE FROM season_skip_marks WHERE season_id = ?"), {seasonId});
        return;
    }
    // a half-set opening is not stored
    const qint64 introStart = marks.hasIntro() ? marks.introStartMs : -1;
    const qint64 introEnd = marks.hasIntro() ? marks.introEndMs : -1;
    const qint64 ending = marks.hasEnding() ? marks.endingFromEndMs : -1;
    execSql(m_db,
            QStringLiteral("INSERT INTO season_skip_marks(season_id, intro_start_ms, intro_end_ms, ending_from_end_ms) VALUES(?,?,?,?) "
                           "ON CONFLICT(season_id) DO UPDATE SET intro_start_ms = excluded.intro_start_ms, "
                           "intro_end_ms = excluded.intro_end_ms, ending_from_end_ms = excluded.ending_from_end_ms"),
            {seasonId, introStart, introEnd, ending});
}

// ------------------------------------------------------------------ settings

QMap<QString, QString> MediaRepository::allSettings() const
{
    Stmt s(m_db, QStringLiteral("SELECT key, value FROM settings"));
    s.run();
    QMap<QString, QString> out;
    while (s.next()) out.insert(s.str(0), s.str(1));
    return out;
}

void MediaRepository::setSetting(const QString& key, const QString& value)
{
    execSql(m_db, QStringLiteral("INSERT INTO settings(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
            {key, value});
}

}  // namespace lf
