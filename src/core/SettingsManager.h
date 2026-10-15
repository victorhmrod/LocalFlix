#pragma once

#include "MediaRepository.h"

#include <QHash>
#include <QObject>
#include <QVariant>

namespace lf {

enum class FullscreenBehavior {
    Manual = 0,     // never change fullscreen automatically
    OnPlayback = 1, // enter fullscreen whenever playback starts
    Remember = 2    // restore the fullscreen state used last time
};

namespace SettingKey {
inline const QString AutoPlayNext = QStringLiteral("playback.autoPlayNext");
inline const QString AutoPlayCountdown = QStringLiteral("playback.autoPlayCountdownSeconds");
inline const QString ResumeAutomatically = QStringLiteral("playback.resumeAutomatically");
inline const QString SkipCompleted = QStringLiteral("playback.skipCompleted");
inline const QString AutoSkipIntro = QStringLiteral("playback.autoSkipIntro");
inline const QString AutoSkipEnding = QStringLiteral("playback.autoSkipEnding");
inline const QString PreferredAudioLanguage = QStringLiteral("playback.preferredAudioLanguage");
inline const QString PreferredSubtitleLanguage = QStringLiteral("playback.preferredSubtitleLanguage");
inline const QString DefaultVolume = QStringLiteral("playback.defaultVolume");
inline const QString PlaybackSpeed = QStringLiteral("playback.speed");
inline const QString FullscreenBehavior = QStringLiteral("ui.fullscreenBehavior");
inline const QString LastFullscreen = QStringLiteral("ui.lastFullscreen");
inline const QString DefaultImportDir = QStringLiteral("library.defaultImportDir");
inline const QString WindowGeometry = QStringLiteral("ui.windowGeometry");
}  // namespace SettingKey

// Typed access to the key/value `settings` table with defaults and change
// notification. Values are cached; writes go straight to SQLite.
class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(MediaRepository* repo, QObject* parent = nullptr);

    bool autoPlayNext() const;
    int autoPlayCountdownSeconds() const;
    bool resumeAutomatically() const;
    bool skipCompleted() const;
    bool autoSkipIntro() const;
    bool autoSkipEnding() const;
    QString preferredAudioLanguage() const;
    QString preferredSubtitleLanguage() const;
    int defaultVolume() const;
    double playbackSpeed() const;
    FullscreenBehavior fullscreenBehavior() const;
    bool lastFullscreen() const;
    QString defaultImportDir() const;
    QByteArray windowGeometry() const;

    void setAutoPlayNext(bool v) { set(SettingKey::AutoPlayNext, v); }
    void setAutoPlayCountdownSeconds(int v) { set(SettingKey::AutoPlayCountdown, qBound(0, v, 30)); }
    void setResumeAutomatically(bool v) { set(SettingKey::ResumeAutomatically, v); }
    void setSkipCompleted(bool v) { set(SettingKey::SkipCompleted, v); }
    void setAutoSkipIntro(bool v) { set(SettingKey::AutoSkipIntro, v); }
    void setAutoSkipEnding(bool v) { set(SettingKey::AutoSkipEnding, v); }
    void setPreferredAudioLanguage(const QString& v) { set(SettingKey::PreferredAudioLanguage, v.trimmed()); }
    void setPreferredSubtitleLanguage(const QString& v) { set(SettingKey::PreferredSubtitleLanguage, v.trimmed()); }
    void setDefaultVolume(int v) { set(SettingKey::DefaultVolume, qBound(0, v, 130)); }
    void setPlaybackSpeed(double v) { set(SettingKey::PlaybackSpeed, qBound(0.25, v, 4.0)); }
    void setFullscreenBehavior(FullscreenBehavior v) { set(SettingKey::FullscreenBehavior, int(v)); }
    void setLastFullscreen(bool v) { set(SettingKey::LastFullscreen, v); }
    void setDefaultImportDir(const QString& v) { set(SettingKey::DefaultImportDir, v); }
    void setWindowGeometry(const QByteArray& v);

    // Re-reads the table (used after tests change the database directly).
    void reload();

signals:
    void changed(const QString& key);

private:
    QVariant get(const QString& key) const;
    void set(const QString& key, const QVariant& value);

    MediaRepository* m_repo;
    QHash<QString, QString> m_values;
};

}  // namespace lf
