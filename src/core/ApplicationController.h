#pragma once

#include "DatabaseManager.h"
#include "EpisodeQueueManager.h"
#include "IPlaybackBackend.h"
#include "LibraryManager.h"
#include "MediaImportService.h"
#include "PlaybackController.h"
#include "PlaybackProgressManager.h"
#include "SettingsManager.h"

#include <QObject>

#include <memory>

namespace lf {

// Owns the whole service graph and its lifecycle: database, library, settings,
// queue, progress tracking, importer and the playback controller. The UI and
// tests receive references; nothing else holds global state.
class ApplicationController : public QObject {
    Q_OBJECT
public:
    // `dataDir` holds library.db and the covers folder. The backend is owned.
    ApplicationController(QString dataDir, std::unique_ptr<IPlaybackBackend> backend, QObject* parent = nullptr);
    ~ApplicationController() override;

    // %APPDATA%/LocalFlix, or $LOCALFLIX_DATA_DIR when set.
    static QString defaultDataDir();

    bool initialize(QString* error = nullptr);
    // Saves progress, stops playback and background workers, closes the
    // database. Idempotent; the destructor calls it too.
    void shutdown();

    QString dataDir() const { return m_dataDir; }
    DatabaseManager& database() { return *m_db; }
    LibraryManager& library() { return *m_library; }
    SettingsManager& settings() { return *m_settings; }
    EpisodeQueueManager& queue() { return *m_queue; }
    PlaybackProgressManager& progress() { return *m_progress; }
    PlaybackController& playback() { return *m_playback; }
    MediaImportService& importer() { return *m_importer; }
    IPlaybackBackend& backend() { return *m_backend; }

private:
    void applySettingToBackend(const QString& key);

    QString m_dataDir;
    // Declaration order is destruction order in reverse: backend outlives everything that talks to it.
    std::unique_ptr<IPlaybackBackend> m_backend;
    std::unique_ptr<DatabaseManager> m_db;
    std::unique_ptr<LibraryManager> m_library;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<EpisodeQueueManager> m_queue;
    std::unique_ptr<PlaybackProgressManager> m_progress;
    std::unique_ptr<MediaImportService> m_importer;
    std::unique_ptr<PlaybackController> m_playback;
    bool m_initialized = false;
    bool m_shutDown = false;
};

}  // namespace lf
