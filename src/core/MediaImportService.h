#pragma once

#include "LibraryManager.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <functional>

namespace lf {

struct ScannedFile {
    QString path;  // normalised absolute path
    qint64 sizeBytes = 0;
    qint64 mtimeMs = 0;
    QString parentFolderName;
    // Folder names from the file's own directory up to the scanned root,
    // nearest first ("deep", "Season 2", "Series"): used to find season hints.
    QStringList folderChain;
};

struct ScanResult {
    QVector<ScannedFile> files;
    QStringList rejected;  // "path — reason"
    bool cancelled = false;
};

struct PlanOptions {
    bool detectSeasonFromNames = true;    // S02E05 in the file name
    bool detectSeasonFromFolders = true;  // "Season 2" parent folder
};

// Turns files and folders into library entries. Directory walking runs on a
// single owned worker thread so large imports never block the UI; planning
// and the database transaction happen on the GUI thread. Files are only read
// (stat), never moved or copied.
class MediaImportService : public QObject {
    Q_OBJECT
public:
    explicit MediaImportService(LibraryManager* library, QObject* parent = nullptr);
    ~MediaImportService() override;

    static QStringList videoExtensions();
    static bool isVideoFile(const QString& path);

    // Synchronous building blocks (also used by tests).
    static ScanResult scan(const QStringList& paths, bool recursive, const std::atomic<bool>* cancel = nullptr,
                           const std::function<void(int)>& progress = {});
    static QVector<ImportItem> planEpisodes(const ScanResult& scan, const PlanOptions& options = {});
    static QVector<ImportItem> planMovies(const ScanResult& scan);

    // Asynchronous entry points; the result arrives via importFinished().
    quint64 importEpisodesAsync(Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options = {});
    quint64 importMoviesAsync(const QStringList& paths);
    // Convenience for tests and scripts: same pipeline, blocking.
    ImportReport importEpisodesSync(Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options = {});
    ImportReport importMoviesSync(const QStringList& paths);

    bool isBusy() const { return m_busy > 0; }
    void cancel() { m_cancel = true; }
    bool waitForIdle(int timeoutMs);

signals:
    void scanProgress(quint64 requestId, int filesFound);
    void importFinished(quint64 requestId, lf::ImportReport report);

private:
    enum class Kind { Episodes, Movies };
    quint64 start(Kind kind, Id showId, Id seasonId, const QStringList& paths, const PlanOptions& options);
    ImportReport finish(Kind kind, Id showId, Id seasonId, const ScanResult& scanResult, const PlanOptions& options);

    LibraryManager* m_library;
    QThreadPool m_pool;
    std::atomic<bool> m_cancel{false};
    std::atomic<int> m_busy{0};
    quint64 m_nextRequest = 1;
};

}  // namespace lf

Q_DECLARE_METATYPE(lf::ImportReport)
