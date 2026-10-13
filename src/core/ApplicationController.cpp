#include "ApplicationController.h"

#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcApp, "localflix.app")

namespace lf {

ApplicationController::ApplicationController(QString dataDir, std::unique_ptr<IPlaybackBackend> backend, QObject* parent)
    : QObject(parent), m_dataDir(std::move(dataDir)), m_backend(std::move(backend))
{
}

ApplicationController::~ApplicationController()
{
    shutdown();
}

QString ApplicationController::defaultDataDir()
{
    const QByteArray override = qgetenv("LOCALFLIX_DATA_DIR");
    if (!override.isEmpty()) return QDir::cleanPath(QString::fromLocal8Bit(override));
    // %APPDATA%\LocalFlix (roaming). QStandardPaths adds the organisation
    // and application names; we set only the application name.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool ApplicationController::initialize(QString* error)
{
    if (m_initialized) return true;
    if (!QDir().mkpath(m_dataDir)) {
        if (error) *error = QStringLiteral("Cannot create the data folder %1").arg(m_dataDir);
        return false;
    }

    m_db = std::make_unique<DatabaseManager>(QDir(m_dataDir).filePath(QStringLiteral("library.db")));
    if (!m_db->open(error)) return false;

    m_library = std::make_unique<LibraryManager>(*m_db, QDir(m_dataDir).filePath(QStringLiteral("covers")));
    m_settings = std::make_unique<SettingsManager>(&m_library->repository());
    m_queue = std::make_unique<EpisodeQueueManager>(m_library.get());
    m_progress = std::make_unique<PlaybackProgressManager>(m_library.get());
    m_importer = std::make_unique<MediaImportService>(m_library.get());
    m_playback = std::make_unique<PlaybackController>(m_backend.get(), m_queue.get(), m_progress.get(), m_library.get(), m_settings.get());

    connect(m_settings.get(), &SettingsManager::changed, this, &ApplicationController::applySettingToBackend);
    m_backend->setVolume(m_settings->defaultVolume());
    m_backend->setSpeed(m_settings->playbackSpeed());
    m_backend->setPreferredLanguages(m_settings->preferredAudioLanguage(), m_settings->preferredSubtitleLanguage());

    m_initialized = true;
    return true;
}

void ApplicationController::applySettingToBackend(const QString& key)
{
    if (key == SettingKey::PreferredAudioLanguage || key == SettingKey::PreferredSubtitleLanguage)
        m_backend->setPreferredLanguages(m_settings->preferredAudioLanguage(), m_settings->preferredSubtitleLanguage());
    else if (key == SettingKey::DefaultVolume)
        m_backend->setVolume(m_settings->defaultVolume());
    else if (key == SettingKey::PlaybackSpeed && !m_playback->isActive())
        m_backend->setSpeed(m_settings->playbackSpeed());
}

void ApplicationController::shutdown()
{
    if (m_shutDown || !m_initialized) return;
    m_shutDown = true;
    // Order matters: persist playback state first, then stop workers, then close the database.
    m_playback->shutdown();
    m_importer->cancel();
    m_importer->waitForIdle(3000);
    m_library->waitForBackgroundWork(3000);
    m_playback.reset();
    m_importer.reset();
    m_progress.reset();
    m_queue.reset();
    m_settings.reset();
    m_library.reset();
    m_db->close();
    m_db.reset();
    qCInfo(lcApp) << "shutdown complete";
}

}  // namespace lf
