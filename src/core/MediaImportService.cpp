#include "MediaImportService.h"

#include "FilenameParser.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>

#include <algorithm>

Q_LOGGING_CATEGORY(lcImport, "localflix.import")

namespace lf {

QStringList MediaImportService::videoExtensions()
{
    return {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("webm"),
            QStringLiteral("m4v"), QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("ts"),  QStringLiteral("m2ts"),
            QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ogv"), QStringLiteral("3gp"), QStringLiteral("divx"),
            QStringLiteral("vob"), QStringLiteral("mts")};
}

bool MediaImportService::isVideoFile(const QString& path)
{
    static const QSet<QString> exts = [] {
        const QStringList l = videoExtensions();
        return QSet<QString>(l.begin(), l.end());
    }();
    return exts.contains(QFileInfo(path).suffix().toLower());
}

MediaImportService::MediaImportService(LibraryManager* library, QObject* parent) : QObject(parent), m_library(library)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("localflix-import"));
    qRegisterMetaType<lf::ImportReport>("lf::ImportReport");
}

MediaImportService::~MediaImportService()
{
    m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone();
}

bool MediaImportService::waitForIdle(int timeoutMs)
{
    return m_pool.waitForDone(timeoutMs);
}

ScanResult MediaImportService::scan(const QStringList& paths, bool recursive, const std::atomic<bool>* cancel,
                                    const std::function<void(int)>& progress)
{
    ScanResult result;
    QSet<QString> seen;
    int sinceReport = 0;

    auto addFile = [&](const QFileInfo& fi, const QString& rootDir) {
        const QString norm = LibraryManager::normalizePath(fi.absoluteFilePath());
        const QString key = norm.toLower();
        if (seen.contains(key)) return;
        if (fi.size() <= 0) {
            result.rejected.push_back(QStringLiteral("%1 — file is empty").arg(QDir::toNativeSeparators(norm)));
            return;
        }
        if (!fi.isReadable()) {
            result.rejected.push_back(QStringLiteral("%1 — file is not readable").arg(QDir::toNativeSeparators(norm)));
            return;
        }
        seen.insert(key);
        ScannedFile f;
        f.path = norm;
        f.sizeBytes = fi.size();
        f.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        f.parentFolderName = fi.absoluteDir().dirName();
        if (rootDir.isEmpty()) {
            f.folderChain = {f.parentFolderName};
        } else {
            const QDir root(rootDir);
            const QStringList rel = root.relativeFilePath(fi.absolutePath()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (qsizetype i = rel.size() - 1; i >= 0; --i)
                if (rel[i] != QLatin1String(".")) f.folderChain.push_back(rel[i]);
            if (!root.dirName().isEmpty()) f.folderChain.push_back(root.dirName());
        }
        result.files.push_back(f);
        if (progress && ++sinceReport >= 25) {
            sinceReport = 0;
            progress(int(result.files.size()));
        }
    };

    for (const QString& raw : paths) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            break;
        }
        const QFileInfo fi(raw);
        if (!fi.exists()) {
            result.rejected.push_back(QStringLiteral("%1 — not found").arg(QDir::toNativeSeparators(raw)));
            continue;
        }
        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                            recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                if (cancel && cancel->load()) {
                    result.cancelled = true;
                    break;
                }
                it.next();
                const QFileInfo f = it.fileInfo();
                if (f.fileName().startsWith(QLatin1String("._"))) continue;
                if (isVideoFile(f.fileName())) addFile(f, fi.absoluteFilePath());
            }
            if (result.cancelled) break;
        } else {
            addFile(fi, QString());
        }
    }
    if (progress) progress(int(result.files.size()));
    return result;
}

QVector<ImportItem> MediaImportService::planEpisodes(const ScanResult& scanResult, const PlanOptions& options)
{
    QMap<int, QVector<ImportItem>> groups;  // season number (-1 = none) -> items
    for (const ScannedFile& f : scanResult.files) {
        const ParsedName parsed = parseFileName(QFileInfo(f.path).fileName());
        ImportItem item;
        item.path = f.path;
        item.sizeBytes = f.sizeBytes;
        item.mtimeMs = f.mtimeMs;
        item.title = parsed.title;
        item.episodeNumber = parsed.episode;
        item.seasonNumber = -1;
        if (options.detectSeasonFromNames && parsed.season >= 0) {
            item.seasonNumber = parsed.season;
        } else if (options.detectSeasonFromFolders) {
            // The nearest folder that names a season wins ("Season 2/Extras/ep.mkv" -> 2).
            for (const QString& folder : f.folderChain) {
                const int s = seasonFromFolderName(folder);
                if (s >= 0) {
                    item.seasonNumber = s;
                    break;
                }
            }
        }
        groups[item.seasonNumber].push_back(item);
    }

    QVector<ImportItem> plan;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QVector<ImportItem>& items = it.value();
        const bool allNumbered = std::all_of(items.begin(), items.end(), [](const ImportItem& i) { return i.episodeNumber > 0; });
        std::stable_sort(items.begin(), items.end(), [allNumbered](const ImportItem& a, const ImportItem& b) {
            if (allNumbered && a.episodeNumber != b.episodeNumber) return a.episodeNumber < b.episodeNumber;
            return naturalCompare(a.path, b.path) < 0;
        });
        for (const ImportItem& i : items) plan.push_back(i);
    }
    for (int i = 0; i < plan.size(); ++i) plan[i].groupIndex = i;
    return plan;
}

QVector<ImportItem> MediaImportService::planMovies(const ScanResult& scanResult)
{
    QVector<ImportItem> plan;
    for (const ScannedFile& f : scanResult.files) {
        ImportItem item;
        item.path = f.path;
        item.sizeBytes = f.sizeBytes;
        item.mtimeMs = f.mtimeMs;
        const QString base = QFileInfo(f.path).completeBaseName();
        item.title = prettifyTitle(base);
        if (item.title.isEmpty()) item.title = base;
        plan.push_back(item);
    }
    std::stable_sort(plan.begin(), plan.end(), [](const ImportItem& a, const ImportItem& b) { return naturalCompare(a.path, b.path) < 0; });
    for (int i = 0; i < plan.size(); ++i) plan[i].groupIndex = i;
    return plan;
}

ImportReport MediaImportService::finish(Kind kind, Id showId, Id seasonId, const ScanResult& scanResult, const PlanOptions& options)
{
    if (scanResult.cancelled) {
        ImportReport r;
        r.ok = false;
        r.error = QStringLiteral("Import cancelled.");
        return r;
    }
    if (kind == Kind::Episodes)
        return m_library->importEpisodes(showId, seasonId, planEpisodes(scanResult, options), scanResult.rejected);
    return m_library->importMovies(planMovies(scanResult), scanResult.rejected);
}

quint64 MediaImportService::start(Kind kind, Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options)
{
    const quint64 requestId = m_nextRequest++;
    m_cancel = false;
    ++m_busy;
    m_pool.start([this, kind, showId, seasonId, paths, options, requestId] {
        const ScanResult result = scan(paths, /*recursive=*/true, &m_cancel, [this, requestId](int n) {
            QMetaObject::invokeMethod(this, [this, requestId, n] { emit scanProgress(requestId, n); }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(
            this,
            [this, kind, showId, seasonId, options, requestId, result] {
                const ImportReport report = finish(kind, showId, seasonId, result, options);
                --m_busy;
                emit importFinished(requestId, report);
            },
            Qt::QueuedConnection);
    });
    return requestId;
}

quint64 MediaImportService::importEpisodesAsync(Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options)
{
    return start(Kind::Episodes, showId, seasonId, paths, options);
}

quint64 MediaImportService::importMoviesAsync(const QStringList& paths)
{
    return start(Kind::Movies, kInvalidId, kInvalidId, paths, {});
}

ImportReport MediaImportService::importEpisodesSync(Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options)
{
    return finish(Kind::Episodes, showId, seasonId, scan(paths, true), options);
}

ImportReport MediaImportService::importMoviesSync(const QStringList& paths)
{
    return finish(Kind::Movies, kInvalidId, kInvalidId, scan(paths, true), {});
}

}  // namespace lf
