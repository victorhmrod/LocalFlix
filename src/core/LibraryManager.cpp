#include "LibraryManager.h"

#include "Clock.h"
#include "Format.h"
#include "ResumeLogic.h"
#include "SqlUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QUuid>

#include <algorithm>

Q_LOGGING_CATEGORY(lcLibrary, "localflix.library")

namespace lf {

namespace {

QStringList pathComponents(const QString& path)
{
    return QDir::cleanPath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool sameComponent(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

}  // namespace

LibraryManager::LibraryManager(DatabaseManager& db, QString coversDir, QObject* parent)
    : QObject(parent), m_db(db), m_repo(db.database()), m_coversDir(std::move(coversDir))
{
    m_verifyPool.setMaxThreadCount(1);
    m_verifyPool.setObjectName(QStringLiteral("localflix-file-verify"));
    qRegisterMetaType<QVector<lf::Id>>("QVector<lf::Id>");
    QDir().mkpath(m_coversDir);
}

LibraryManager::~LibraryManager()
{
    m_shuttingDown = true;
    m_verifyPool.clear();
    m_verifyPool.waitForDone();
}

void LibraryManager::reportError(const QString& message)
{
    qCWarning(lcLibrary) << message;
    emit errorOccurred(message);
}

QString LibraryManager::normalizePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString LibraryManager::coverPath(const QString& fileName) const
{
    if (fileName.isEmpty()) return {};
    const QString full = QDir(m_coversDir).filePath(fileName);
    return QFileInfo::exists(full) ? full : QString();
}

QString LibraryManager::storeImage(const QString& sourcePath, QString* error)
{
    static const QSet<QString> allowed = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                          QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                                          QStringLiteral("svg")};
    const QFileInfo fi(sourcePath);
    const QString ext = fi.suffix().toLower();
    if (!fi.exists() || !fi.isFile()) {
        if (error) *error = QStringLiteral("Image not found: %1").arg(sourcePath);
        return {};
    }
    if (!allowed.contains(ext)) {
        if (error) *error = QStringLiteral("Unsupported image type '.%1'").arg(ext);
        return {};
    }
    QDir().mkpath(m_coversDir);
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('.') + ext;
    if (!QFile::copy(fi.absoluteFilePath(), QDir(m_coversDir).filePath(name))) {
        if (error) *error = QStringLiteral("Could not copy the image into the library folder");
        return {};
    }
    return name;
}

void LibraryManager::removeStoredImage(const QString& fileName)
{
    // Only ever our own copy inside the covers directory; never a user file.
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))) return;
    QFile::remove(QDir(m_coversDir).filePath(fileName));
}

Id LibraryManager::showIdOfMedia(Id mediaFileId)
{
    return guarded<Id>([&] {
        const auto e = m_repo.episodeByMedia(mediaFileId);
        return e ? e->showId : kInvalidId;
    });
}

// ------------------------------------------------------------------ shows & seasons

QVector<ShowSummary> LibraryManager::showSummaries()
{
    return guarded<QVector<ShowSummary>>([&] { return m_repo.showSummaries(); });
}

std::optional<Show> LibraryManager::show(Id id)
{
    return guarded<std::optional<Show>>([&] { return m_repo.show(id); });
}

Id LibraryManager::createShow(const QString& rawTitle)
{
    const QString title = rawTitle.trimmed();
    if (title.isEmpty()) {
        reportError(QStringLiteral("A show needs a title."));
        return kInvalidId;
    }
    const Id id = guarded<Id>([&] {
        Transaction tx(m_db.database());
        const Id showId = m_repo.createShow(title);
        m_repo.createSeason(showId, QStringLiteral("Season 1"), 1);
        tx.commit();
        return showId;
    });
    if (id != kInvalidId) emit libraryChanged();
    return id;
}

bool LibraryManager::renameShow(Id id, const QString& rawTitle)
{
    const QString title = rawTitle.trimmed();
    if (title.isEmpty()) {
        reportError(QStringLiteral("A show needs a title."));
        return false;
    }
    const bool ok = guarded<bool>([&] {
        auto s = m_repo.show(id);
        if (!s) return false;
        s->title = title;
        m_repo.updateShow(*s);
        return true;
    });
    if (ok) {
        emit libraryChanged();
        emit showChanged(id);
    }
    return ok;
}

bool LibraryManager::setShowDescription(Id id, const QString& description)
{
    const bool ok = guarded<bool>([&] {
        auto s = m_repo.show(id);
        if (!s) return false;
        s->description = description.trimmed();
        m_repo.updateShow(*s);
        return true;
    });
    if (ok) emit showChanged(id);
    return ok;
}

bool LibraryManager::setShowCover(Id id, const QString& imagePath)
{
    QString err;
    const QString stored = storeImage(imagePath, &err);
    if (stored.isEmpty()) {
        reportError(err);
        return false;
    }
    QString previous;
    const bool ok = guarded<bool>([&] {
        auto s = m_repo.show(id);
        if (!s) return false;
        previous = s->coverFile;
        s->coverFile = stored;
        m_repo.updateShow(*s);
        return true;
    });
    if (ok) {
        removeStoredImage(previous);
        emit libraryChanged();
        emit showChanged(id);
    } else {
        removeStoredImage(stored);
    }
    return ok;
}

bool LibraryManager::clearShowCover(Id id)
{
    QString previous;
    const bool ok = guarded<bool>([&] {
        auto s = m_repo.show(id);
        if (!s) return false;
        previous = s->coverFile;
        s->coverFile.clear();
        m_repo.updateShow(*s);
        return true;
    });
    if (ok) {
        removeStoredImage(previous);
        emit libraryChanged();
        emit showChanged(id);
    }
    return ok;
}

bool LibraryManager::deleteShow(Id id)
{
    QString cover;
    bool ok = true;
    try {
        cover = m_repo.deleteShow(id);
    } catch (const DbError& e) {
        reportError(e.message());
        ok = false;
    }
    if (ok) {
        removeStoredImage(cover);
        emit libraryChanged();
    }
    return ok;
}

QVector<Season> LibraryManager::seasons(Id showId)
{
    return guarded<QVector<Season>>([&] { return m_repo.seasons(showId); });
}

std::optional<Season> LibraryManager::season(Id seasonId)
{
    return guarded<std::optional<Season>>([&] { return m_repo.season(seasonId); });
}

Id LibraryManager::addSeason(Id showId, const QString& rawTitle, int number)
{
    const Id id = guarded<Id>([&] {
        int n = number;
        if (n < 0) {
            n = 0;
            for (const Season& s : m_repo.seasons(showId)) n = std::max(n, s.number);
            ++n;
        }
        QString title = rawTitle.trimmed();
        if (title.isEmpty()) title = n == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(n);
        return m_repo.createSeason(showId, title, n);
    });
    if (id != kInvalidId) emit showChanged(showId);
    return id;
}

bool LibraryManager::updateSeason(Id seasonId, const QString& title, int number)
{
    const QString t = title.trimmed();
    if (t.isEmpty()) {
        reportError(QStringLiteral("A season needs a title."));
        return false;
    }
    Id showId = kInvalidId;
    const bool ok = guarded<bool>([&] {
        const auto s = m_repo.season(seasonId);
        if (!s) return false;
        showId = s->showId;
        m_repo.updateSeason(seasonId, t, number);
        return true;
    });
    if (ok) emit showChanged(showId);
    return ok;
}

bool LibraryManager::deleteSeason(Id seasonId)
{
    Id showId = kInvalidId;
    const bool ok = guarded<bool>([&] {
        const auto s = m_repo.season(seasonId);
        if (!s) return false;
        showId = s->showId;
        m_repo.deleteSeason(seasonId);
        return true;
    });
    if (ok) {
        emit showChanged(showId);
        emit libraryChanged();
    }
    return ok;
}

// ------------------------------------------------------------------ episodes

QVector<Episode> LibraryManager::episodes(Id showId)
{
    return guarded<QVector<Episode>>([&] { return m_repo.episodes(showId); });
}

std::optional<Episode> LibraryManager::episode(Id id)
{
    return guarded<std::optional<Episode>>([&] { return m_repo.episode(id); });
}

bool LibraryManager::updateEpisode(Id id, const QString& title, int episodeNumber)
{
    const QString t = title.trimmed();
    if (t.isEmpty()) {
        reportError(QStringLiteral("An episode needs a title."));
        return false;
    }
    Id showId = kInvalidId;
    const bool ok = guarded<bool>([&] {
        const auto e = m_repo.episode(id);
        if (!e) return false;
        showId = e->showId;
        m_repo.updateEpisode(id, t, std::max(0, episodeNumber));
        return true;
    });
    if (ok) emit showChanged(showId);
    return ok;
}

bool LibraryManager::removeEpisodes(const QVector<Id>& episodeIds)
{
    QSet<Id> shows;
    const bool ok = guarded<bool>([&] {
        for (Id id : episodeIds)
            if (const auto e = m_repo.episode(id)) shows.insert(e->showId);
        m_repo.deleteEpisodes(episodeIds);
        return true;
    });
    if (ok) {
        for (Id s : shows) emit showChanged(s);
        emit libraryChanged();
    }
    return ok;
}

bool LibraryManager::applyLayout(Id showId, const QVector<SeasonLayout>& layout)
{
    const bool ok = guarded<bool>([&] {
        m_repo.applyLayout(showId, layout);
        return true;
    });
    if (ok) emit showChanged(showId);
    return ok;
}

bool LibraryManager::moveEpisode(Id episodeId, Id toSeasonId, int toIndex)
{
    Id showId = kInvalidId;
    QVector<SeasonLayout> layout;
    const bool built = guarded<bool>([&] {
        const auto e = m_repo.episode(episodeId);
        if (!e) return false;
        showId = e->showId;
        const QVector<Season> seasons = m_repo.seasons(showId);
        const QVector<Episode> eps = m_repo.episodes(showId);
        bool targetExists = false;
        for (const Season& s : seasons) {
            SeasonLayout sl;
            sl.seasonId = s.id;
            if (s.id == toSeasonId) targetExists = true;
            for (const Episode& x : eps)
                if (x.seasonId == s.id && x.id != episodeId) sl.episodeIds.push_back(x.id);
            layout.push_back(sl);
        }
        if (!targetExists) return false;
        for (SeasonLayout& sl : layout) {
            if (sl.seasonId == toSeasonId) {
                const int idx = std::clamp(toIndex, 0, int(sl.episodeIds.size()));
                sl.episodeIds.insert(idx, episodeId);
            }
        }
        return true;
    });
    return built && applyLayout(showId, layout);
}

bool LibraryManager::moveSeason(Id seasonId, int toIndex)
{
    Id showId = kInvalidId;
    QVector<SeasonLayout> layout;
    const bool built = guarded<bool>([&] {
        const auto season = m_repo.season(seasonId);
        if (!season) return false;
        showId = season->showId;
        const QVector<Season> seasons = m_repo.seasons(showId);
        const QVector<Episode> eps = m_repo.episodes(showId);
        QVector<Id> order;
        for (const Season& s : seasons)
            if (s.id != seasonId) order.push_back(s.id);
        order.insert(std::clamp(toIndex, 0, int(order.size())), seasonId);
        for (Id sid : order) {
            SeasonLayout sl;
            sl.seasonId = sid;
            for (const Episode& x : eps)
                if (x.seasonId == sid) sl.episodeIds.push_back(x.id);
            layout.push_back(sl);
        }
        return true;
    });
    return built && applyLayout(showId, layout);
}

// ------------------------------------------------------------------ movies

QVector<Movie> LibraryManager::movies()
{
    return guarded<QVector<Movie>>([&] { return m_repo.movies(); });
}

std::optional<Movie> LibraryManager::movie(Id id)
{
    return guarded<std::optional<Movie>>([&] { return m_repo.movie(id); });
}

bool LibraryManager::updateMovie(Id id, const QString& title, const QString& description)
{
    const QString t = title.trimmed();
    if (t.isEmpty()) {
        reportError(QStringLiteral("A movie needs a title."));
        return false;
    }
    const bool ok = guarded<bool>([&] {
        auto m = m_repo.movie(id);
        if (!m) return false;
        m->title = t;
        m->description = description.trimmed();
        m_repo.updateMovie(*m);
        return true;
    });
    if (ok) emit libraryChanged();
    return ok;
}

bool LibraryManager::setMoviePoster(Id id, const QString& imagePath)
{
    QString err;
    const QString stored = storeImage(imagePath, &err);
    if (stored.isEmpty()) {
        reportError(err);
        return false;
    }
    QString previous;
    const bool ok = guarded<bool>([&] {
        auto m = m_repo.movie(id);
        if (!m) return false;
        previous = m->posterFile;
        m->posterFile = stored;
        m_repo.updateMovie(*m);
        return true;
    });
    if (ok) {
        removeStoredImage(previous);
        emit libraryChanged();
    } else {
        removeStoredImage(stored);
    }
    return ok;
}

bool LibraryManager::clearMoviePoster(Id id)
{
    QString previous;
    const bool ok = guarded<bool>([&] {
        auto m = m_repo.movie(id);
        if (!m) return false;
        previous = m->posterFile;
        m->posterFile.clear();
        m_repo.updateMovie(*m);
        return true;
    });
    if (ok) {
        removeStoredImage(previous);
        emit libraryChanged();
    }
    return ok;
}

bool LibraryManager::deleteMovie(Id id)
{
    QString poster;
    bool ok = true;
    try {
        poster = m_repo.deleteMovie(id);
    } catch (const DbError& e) {
        reportError(e.message());
        ok = false;
    }
    if (ok) {
        removeStoredImage(poster);
        emit libraryChanged();
    }
    return ok;
}

// ------------------------------------------------------------------ import

ImportReport LibraryManager::importEpisodes(Id showId, Id seasonId, const QVector<ImportItem>& plan, const QStringList& rejected)
{
    ImportReport report;
    report.rejected = rejected;
    try {
        const ImportOutcome o = m_repo.importEpisodes(showId, seasonId, plan);
        report.addedIds = o.addedItemIds;
        report.added = int(o.addedItemIds.size());
        report.duplicates = o.duplicates;
        report.createdSeasonIds = o.createdSeasonIds;
    } catch (const DbError& e) {
        report.ok = false;
        report.error = e.message();
        reportError(e.message());
        return report;
    }
    if (report.added > 0) {
        emit showChanged(showId);
        emit libraryChanged();
    }
    return report;
}

ImportReport LibraryManager::importMovies(const QVector<ImportItem>& plan, const QStringList& rejected)
{
    ImportReport report;
    report.rejected = rejected;
    try {
        const ImportOutcome o = m_repo.importMovies(plan);
        report.addedIds = o.addedItemIds;
        report.added = int(o.addedItemIds.size());
        report.duplicates = o.duplicates;
    } catch (const DbError& e) {
        report.ok = false;
        report.error = e.message();
        reportError(e.message());
        return report;
    }
    if (report.added > 0) emit libraryChanged();
    return report;
}

// ------------------------------------------------------------------ collections

QVector<Collection> LibraryManager::collections()
{
    return guarded<QVector<Collection>>([&] { return m_repo.collections(); });
}

Id LibraryManager::createCollection(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        reportError(QStringLiteral("A collection needs a name."));
        return kInvalidId;
    }
    const Id id = guarded<Id>([&] { return m_repo.createCollection(n); });
    if (id != kInvalidId) emit libraryChanged();
    return id;
}

bool LibraryManager::updateCollection(Id id, const QString& name, const QString& description)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        reportError(QStringLiteral("A collection needs a name."));
        return false;
    }
    const bool ok = guarded<bool>([&] {
        auto c = m_repo.collection(id);
        if (!c) return false;
        c->name = n;
        c->description = description.trimmed();
        m_repo.updateCollection(*c);
        return true;
    });
    if (ok) emit libraryChanged();
    return ok;
}

bool LibraryManager::deleteCollection(Id id)
{
    const bool ok = guarded<bool>([&] {
        m_repo.deleteCollection(id);
        return true;
    });
    if (ok) emit libraryChanged();
    return ok;
}

QVector<CollectionEntry> LibraryManager::collectionEntries(Id collectionId)
{
    return guarded<QVector<CollectionEntry>>([&] { return m_repo.collectionEntries(collectionId); });
}

bool LibraryManager::addToCollection(Id collectionId, CollectionItemType type, Id itemId)
{
    const bool ok = guarded<bool>([&] { return m_repo.addCollectionEntry(collectionId, type, itemId); });
    if (ok) emit libraryChanged();
    return ok;
}

bool LibraryManager::removeFromCollection(Id entryId, Id collectionId)
{
    Q_UNUSED(collectionId)
    const bool ok = guarded<bool>([&] {
        m_repo.removeCollectionEntry(entryId);
        return true;
    });
    if (ok) emit libraryChanged();
    return ok;
}

bool LibraryManager::setCollectionOrder(Id collectionId, const QVector<Id>& entryIdsInOrder)
{
    const bool ok = guarded<bool>([&] {
        m_repo.setCollectionEntryOrder(collectionId, entryIdsInOrder);
        return true;
    });
    if (ok) emit libraryChanged();
    return ok;
}

// ------------------------------------------------------------------ progress

Progress LibraryManager::progress(Id mediaFileId)
{
    return guarded<Progress>([&] { return m_repo.progress(mediaFileId); });
}

void LibraryManager::saveProgress(Id mediaFileId, qint64 positionMs, qint64 durationMs)
{
    const bool ok = guarded<bool>([&] {
        m_repo.saveProgress(mediaFileId, positionMs, durationMs);
        return true;
    });
    if (ok) emit progressChanged(mediaFileId);
}

void LibraryManager::touchLastWatched(Id mediaFileId)
{
    const bool ok = guarded<bool>([&] {
        m_repo.touchLastWatched(mediaFileId);
        return true;
    });
    if (ok) emit progressChanged(mediaFileId);
}

void LibraryManager::saveTrackSelection(Id mediaFileId, int audioTrack, int subtitleTrack)
{
    guarded<bool>([&] {
        m_repo.saveTrackSelection(mediaFileId, audioTrack, subtitleTrack);
        return true;
    });
}

void LibraryManager::setMediaDuration(Id mediaFileId, qint64 durationMs)
{
    guarded<bool>([&] {
        m_repo.setMediaDuration(mediaFileId, durationMs);
        return true;
    });
}

void LibraryManager::markCompleted(Id mediaFileId, qint64 durationMs)
{
    const bool ok = guarded<bool>([&] {
        m_repo.markCompleted(mediaFileId, durationMs);
        return true;
    });
    if (ok) {
        emit progressChanged(mediaFileId);
        if (const Id s = showIdOfMedia(mediaFileId); s != kInvalidId) emit showChanged(s);
    }
}

bool LibraryManager::setWatched(const QVector<Id>& mediaFileIds, bool watched)
{
    const bool ok = guarded<bool>([&] {
        m_repo.setWatched(mediaFileIds, watched);
        return true;
    });
    if (ok) {
        QSet<Id> shows;
        for (Id id : mediaFileIds) {
            emit progressChanged(id);
            if (const Id s = showIdOfMedia(id); s != kInvalidId) shows.insert(s);
        }
        for (Id s : shows) emit showChanged(s);
        emit libraryChanged();
    }
    return ok;
}

bool LibraryManager::resetProgress(const QVector<Id>& mediaFileIds)
{
    const bool ok = guarded<bool>([&] {
        m_repo.resetProgress(mediaFileIds);
        return true;
    });
    if (ok) {
        QSet<Id> shows;
        for (Id id : mediaFileIds) {
            emit progressChanged(id);
            if (const Id s = showIdOfMedia(id); s != kInvalidId) shows.insert(s);
        }
        for (Id s : shows) emit showChanged(s);
        emit libraryChanged();
    }
    return ok;
}

bool LibraryManager::resetShowProgress(Id showId)
{
    QVector<Id> ids;
    guarded<bool>([&] {
        for (const Episode& e : m_repo.episodes(showId)) ids.push_back(e.file.id);
        return true;
    });
    return resetProgress(ids);
}

// ------------------------------------------------------------------ continue watching

ResumeTarget LibraryManager::resumeTarget(Id showId)
{
    return guarded<ResumeTarget>([&] { return computeResumeTarget(m_repo.episodes(showId)); });
}

QVector<ContinueItem> LibraryManager::continueWatching(int limit)
{
    return guarded<QVector<ContinueItem>>([&] {
        QVector<ContinueItem> out;

        for (const auto& [showId, lastWatched] : m_repo.showsWithHistory(limit)) {
            const auto show = m_repo.show(showId);
            if (!show) continue;
            const QVector<Episode> eps = m_repo.episodes(showId);
            const ResumeTarget target = computeResumeTarget(eps);
            if (!target.valid() || target.allCompleted()) continue;  // finished shows leave the row
            for (const Episode& e : eps) {
                if (e.id != target.episodeId) continue;
                ContinueItem item;
                item.kind = MediaKind::Episode;
                item.showId = showId;
                item.title = show->title;
                item.subtitle = episodeLabel(e);
                item.coverFile = show->coverFile;
                item.mediaFileId = e.file.id;
                item.positionMs = target.positionMs;
                item.durationMs = e.progress.durationMs > 0 ? e.progress.durationMs : e.file.durationMs;
                item.lastWatchedAt = lastWatched;
                item.missing = e.file.missing;
                out.push_back(item);
                break;
            }
        }

        for (const Movie& m : m_repo.startedMovies(limit)) {
            ContinueItem item;
            item.kind = MediaKind::Movie;
            item.movieId = m.id;
            item.title = m.title;
            item.coverFile = m.posterFile;
            item.mediaFileId = m.file.id;
            item.positionMs = m.progress.resumePositionMs();
            item.durationMs = m.progress.durationMs > 0 ? m.progress.durationMs : m.file.durationMs;
            item.lastWatchedAt = m.progress.lastWatchedAt;
            item.missing = m.file.missing;
            out.push_back(item);
        }

        std::stable_sort(out.begin(), out.end(),
                         [](const ContinueItem& a, const ContinueItem& b) { return a.lastWatchedAt > b.lastWatchedAt; });
        if (out.size() > limit) out.resize(limit);
        return out;
    });
}

QVector<LibraryCard> LibraryManager::recentlyAdded(int limit)
{
    return guarded<QVector<LibraryCard>>([&] {
        QVector<LibraryCard> out;
        for (const ShowSummary& s : m_repo.showSummaries()) {
            LibraryCard c;
            c.kind = CardKind::Show;
            c.id = s.show.id;
            c.title = s.show.title;
            c.subtitle = s.episodeCount == 1 ? QStringLiteral("1 episode") : QStringLiteral("%1 episodes").arg(s.episodeCount);
            c.imageFile = s.show.coverFile;
            c.timestamp = s.show.updatedAt;
            c.progress = s.episodeCount > 0 ? double(s.completedCount) / double(s.episodeCount) : 0.0;
            c.allWatched = s.episodeCount > 0 && s.completedCount >= s.episodeCount;
            out.push_back(c);
        }
        for (const Movie& m : m_repo.movies()) {
            LibraryCard c;
            c.kind = CardKind::Movie;
            c.id = m.id;
            c.title = m.title;
            c.subtitle = QStringLiteral("Movie");
            c.imageFile = m.posterFile;
            c.timestamp = m.createdAt;
            c.progress = m.progress.fraction();
            c.allWatched = m.progress.completed;
            c.missing = m.file.missing;
            out.push_back(c);
        }
        std::stable_sort(out.begin(), out.end(), [](const LibraryCard& a, const LibraryCard& b) { return a.timestamp > b.timestamp; });
        if (out.size() > limit) out.resize(limit);
        return out;
    });
}

// ------------------------------------------------------------------ queue

QueueEntry LibraryManager::toEntry(const Episode& e, const QString& showTitle) const
{
    QueueEntry q;
    q.kind = MediaKind::Episode;
    q.itemId = e.id;
    q.showId = e.showId;
    q.mediaFileId = e.file.id;
    q.title = episodeLabel(e);
    q.contextTitle = showTitle;
    q.path = e.file.path;
    q.missing = e.file.missing;
    q.completed = e.progress.completed;
    q.positionMs = e.progress.resumePositionMs();
    q.durationMs = e.progress.durationMs > 0 ? e.progress.durationMs : e.file.durationMs;
    return q;
}

QueueEntry LibraryManager::toEntry(const Movie& m) const
{
    QueueEntry q;
    q.kind = MediaKind::Movie;
    q.itemId = m.id;
    q.mediaFileId = m.file.id;
    q.title = m.title;
    q.path = m.file.path;
    q.missing = m.file.missing;
    q.completed = m.progress.completed;
    q.positionMs = m.progress.resumePositionMs();
    q.durationMs = m.progress.durationMs > 0 ? m.progress.durationMs : m.file.durationMs;
    return q;
}

QVector<QueueEntry> LibraryManager::buildQueue(const QueueContext& context)
{
    return guarded<QVector<QueueEntry>>([&] {
        QVector<QueueEntry> out;
        auto appendShow = [&](Id showId) {
            const auto show = m_repo.show(showId);
            if (!show) return;
            for (const Episode& e : m_repo.episodes(showId)) out.push_back(toEntry(e, show->title));
        };
        switch (context.type) {
        case ContextType::Show:
            appendShow(context.id);
            break;
        case ContextType::Movie:
            if (const auto m = m_repo.movie(context.id)) out.push_back(toEntry(*m));
            break;
        case ContextType::Collection:
            for (const CollectionEntry& ce : m_repo.collectionEntries(context.id)) {
                if (ce.type == CollectionItemType::Show) {
                    appendShow(ce.itemId);
                } else if (const auto m = m_repo.movie(ce.itemId)) {
                    out.push_back(toEntry(*m));
                }
            }
            break;
        case ContextType::None:
            break;
        }
        return out;
    });
}

QueueEntry LibraryManager::entryForMedia(Id mediaFileId)
{
    return guarded<QueueEntry>([&] {
        if (const auto e = m_repo.episodeByMedia(mediaFileId)) {
            const auto show = m_repo.show(e->showId);
            return toEntry(*e, show ? show->title : QString());
        }
        if (const auto m = m_repo.movieByMedia(mediaFileId)) return toEntry(*m);
        return QueueEntry{};
    });
}

// ------------------------------------------------------------------ file recovery

std::optional<MediaFile> LibraryManager::mediaFile(Id id)
{
    return guarded<std::optional<MediaFile>>([&] { return m_repo.mediaFile(id); });
}

QVector<MediaFile> LibraryManager::allMediaFiles()
{
    return guarded<QVector<MediaFile>>([&] { return m_repo.allMediaFiles(); });
}

QVector<MediaFile> LibraryManager::missingFiles()
{
    return guarded<QVector<MediaFile>>([&] { return m_repo.missingMediaFiles(); });
}

RelinkResult LibraryManager::relinkFile(Id mediaFileId, const QString& newPath)
{
    RelinkResult result;
    const QString norm = normalizePath(newPath);
    const QFileInfo fi(norm);
    if (!fi.exists() || !fi.isFile()) {
        result.error = QStringLiteral("The selected file does not exist.");
        return result;
    }
    if (!fi.isReadable()) {
        result.error = QStringLiteral("The selected file cannot be read.");
        return result;
    }
    try {
        const auto current = m_repo.mediaFile(mediaFileId);
        if (!current) {
            result.error = QStringLiteral("This entry no longer exists in the library.");
            return result;
        }
        if (const auto other = m_repo.mediaFileByPath(norm); other && other->id != mediaFileId) {
            const QueueEntry where = entryForMedia(other->id);
            result.error = QStringLiteral("That file is already in the library as “%1”.").arg(where.displayTitle());
            return result;
        }
        m_repo.relinkMediaFile(mediaFileId, norm, fi.size(), fi.lastModified().toMSecsSinceEpoch());
    } catch (const DbError& e) {
        result.error = e.message();
        reportError(e.message());
        return result;
    }
    result.ok = true;
    if (const Id s = showIdOfMedia(mediaFileId); s != kInvalidId) emit showChanged(s);
    emit filesVerified({mediaFileId});
    emit libraryChanged();
    return result;
}

QVector<RelinkProposal> LibraryManager::proposeRelinks(const QString& oldPath, const QString& newPath)
{
    return guarded<QVector<RelinkProposal>>([&] {
        QVector<RelinkProposal> out;
        const QStringList oldC = pathComponents(oldPath);
        const QStringList newC = pathComponents(newPath);
        if (oldC.isEmpty() || newC.isEmpty()) return out;

        // Length of the common trailing path (e.g. "Anime/S1/ep1.mkv").
        int common = 0;
        while (common < oldC.size() && common < newC.size() &&
               sameComponent(oldC[oldC.size() - 1 - common], newC[newC.size() - 1 - common]))
            ++common;

        QStringList oldRoot;
        QStringList newRoot;
        if (common >= 1) {
            oldRoot = oldC.mid(0, oldC.size() - common);
            newRoot = newC.mid(0, newC.size() - common);
        } else {
            // File was renamed as well: only same-folder siblings can be matched.
            oldRoot = oldC.mid(0, oldC.size() - 1);
            newRoot = newC.mid(0, newC.size() - 1);
        }

        for (const MediaFile& f : m_repo.missingMediaFiles()) {
            const QStringList fc = pathComponents(f.path);
            if (fc.size() <= oldRoot.size()) continue;
            bool under = true;
            for (int i = 0; i < oldRoot.size(); ++i)
                if (!sameComponent(fc[i], oldRoot[i])) {
                    under = false;
                    break;
                }
            if (!under) continue;
            if (common == 0 && fc.size() != oldRoot.size() + 1) continue;  // siblings only
            const QStringList rest = fc.mid(oldRoot.size());
            const QString candidate = (newRoot + rest).join(QLatin1Char('/'));
            const QString normalized = normalizePath(candidate);
            if (!QFileInfo::exists(normalized)) continue;
            if (m_repo.mediaFileByPath(normalized)) continue;  // already known
            out.push_back({f.id, f.path, normalized});
        }
        return out;
    });
}

int LibraryManager::applyRelinks(const QVector<RelinkProposal>& proposals)
{
    int applied = 0;
    for (const RelinkProposal& p : proposals)
        if (relinkFile(p.mediaFileId, p.newPath).ok) ++applied;
    return applied;
}

bool LibraryManager::setMissing(Id mediaFileId, bool missing)
{
    const bool ok = guarded<bool>([&] {
        m_repo.setMissing({mediaFileId}, missing);
        return true;
    });
    if (ok) {
        if (const Id s = showIdOfMedia(mediaFileId); s != kInvalidId) emit showChanged(s);
        emit filesVerified({mediaFileId});
    }
    return ok;
}

void LibraryManager::verifyFilesAsync(const QVector<MediaFile>& files)
{
    if (files.isEmpty()) return;
    m_verifyPool.start([this, files] {
        QVector<std::pair<MediaFile, bool>> results;
        results.reserve(files.size());
        for (const MediaFile& f : files) {
            if (m_shuttingDown) return;
            const QFileInfo fi(f.path);
            results.push_back({f, !(fi.exists() && fi.isFile())});
        }
        if (m_shuttingDown) return;
        QMetaObject::invokeMethod(
            this,
            [this, results] {
                QVector<Id> nowMissing;
                QVector<Id> nowPresent;
                QVector<Id> changed;
                QSet<Id> shows;
                guarded<bool>([&] {
                    for (const auto& [snapshot, missing] : results) {
                        const auto current = m_repo.mediaFile(snapshot.id);
                        // Skip entries that were relinked or removed while we were checking.
                        if (!current || current->path.compare(snapshot.path, Qt::CaseInsensitive) != 0) continue;
                        if (current->missing == missing) continue;
                        (missing ? nowMissing : nowPresent).push_back(snapshot.id);
                        changed.push_back(snapshot.id);
                        if (const auto e = m_repo.episodeByMedia(snapshot.id)) shows.insert(e->showId);
                    }
                    m_repo.setMissing(nowMissing, true);
                    m_repo.setMissing(nowPresent, false);
                    return true;
                });
                if (!changed.isEmpty()) {
                    for (Id s : shows) emit showChanged(s);
                    emit filesVerified(changed);
                    emit libraryChanged();
                }
                emit verificationFinished(int(results.size()), int(missingFiles().size()));
            },
            Qt::QueuedConnection);
    });
}

void LibraryManager::verifyShowAsync(Id showId)
{
    const QVector<MediaFile> files = guarded<QVector<MediaFile>>([&] { return m_repo.mediaFilesOfShow(showId); });
    verifyFilesAsync(files);
}

bool LibraryManager::waitForBackgroundWork(int timeoutMs)
{
    return m_verifyPool.waitForDone(timeoutMs);
}

}  // namespace lf
