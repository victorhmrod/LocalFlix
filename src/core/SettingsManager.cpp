#include "SettingsManager.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSettings, "localflix.settings")

namespace lf {

namespace {

QString defaultFor(const QString& key)
{
    static const QHash<QString, QString> defaults = {
        {SettingKey::AutoPlayNext, QStringLiteral("1")},
        {SettingKey::AutoPlayCountdown, QStringLiteral("3")},
        {SettingKey::ResumeAutomatically, QStringLiteral("1")},
        {SettingKey::SkipCompleted, QStringLiteral("0")},
        {SettingKey::AutoSkipIntro, QStringLiteral("0")},
        {SettingKey::AutoSkipEnding, QStringLiteral("0")},
        {SettingKey::PreferredAudioLanguage, QString()},
        {SettingKey::PreferredSubtitleLanguage, QString()},
        {SettingKey::DefaultVolume, QStringLiteral("100")},
        {SettingKey::PlaybackSpeed, QStringLiteral("1")},
        {SettingKey::FullscreenBehavior, QStringLiteral("0")},
        {SettingKey::LastFullscreen, QStringLiteral("0")},
        {SettingKey::DefaultImportDir, QString()},
        {SettingKey::WindowGeometry, QString()},
    };
    return defaults.value(key);
}

QString toStored(const QVariant& v)
{
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    return v.toString();
}

}  // namespace

SettingsManager::SettingsManager(MediaRepository* repo, QObject* parent) : QObject(parent), m_repo(repo)
{
    reload();
}

void SettingsManager::reload()
{
    m_values.clear();
    try {
        const auto all = m_repo->allSettings();
        for (auto it = all.begin(); it != all.end(); ++it) m_values.insert(it.key(), it.value());
    } catch (const DbError& e) {
        qCWarning(lcSettings) << "Could not load settings:" << e.message();
    }
}

QVariant SettingsManager::get(const QString& key) const
{
    return m_values.contains(key) ? m_values.value(key) : defaultFor(key);
}

void SettingsManager::set(const QString& key, const QVariant& value)
{
    const QString stored = toStored(value);
    const QString current = m_values.contains(key) ? m_values.value(key) : defaultFor(key);
    if (current == stored) return;  // unchanged (or still at its default)
    try {
        m_repo->setSetting(key, stored);
    } catch (const DbError& e) {
        qCWarning(lcSettings) << "Could not save setting" << key << e.message();
        return;
    }
    m_values.insert(key, stored);
    emit changed(key);
}

bool SettingsManager::autoPlayNext() const { return get(SettingKey::AutoPlayNext).toInt() != 0; }
int SettingsManager::autoPlayCountdownSeconds() const { return qBound(0, get(SettingKey::AutoPlayCountdown).toInt(), 30); }
bool SettingsManager::resumeAutomatically() const { return get(SettingKey::ResumeAutomatically).toInt() != 0; }
bool SettingsManager::skipCompleted() const { return get(SettingKey::SkipCompleted).toInt() != 0; }
bool SettingsManager::autoSkipIntro() const { return get(SettingKey::AutoSkipIntro).toInt() != 0; }
bool SettingsManager::autoSkipEnding() const { return get(SettingKey::AutoSkipEnding).toInt() != 0; }
QString SettingsManager::preferredAudioLanguage() const { return get(SettingKey::PreferredAudioLanguage).toString(); }
QString SettingsManager::preferredSubtitleLanguage() const { return get(SettingKey::PreferredSubtitleLanguage).toString(); }
int SettingsManager::defaultVolume() const { return qBound(0, get(SettingKey::DefaultVolume).toInt(), 130); }
double SettingsManager::playbackSpeed() const { return qBound(0.25, get(SettingKey::PlaybackSpeed).toDouble(), 4.0); }
FullscreenBehavior SettingsManager::fullscreenBehavior() const
{
    const int v = get(SettingKey::FullscreenBehavior).toInt();
    return v == 1 ? FullscreenBehavior::OnPlayback : (v == 2 ? FullscreenBehavior::Remember : FullscreenBehavior::Manual);
}
bool SettingsManager::lastFullscreen() const { return get(SettingKey::LastFullscreen).toInt() != 0; }
QString SettingsManager::defaultImportDir() const { return get(SettingKey::DefaultImportDir).toString(); }
QByteArray SettingsManager::windowGeometry() const
{
    return QByteArray::fromBase64(get(SettingKey::WindowGeometry).toString().toLatin1());
}
void SettingsManager::setWindowGeometry(const QByteArray& v)
{
    set(SettingKey::WindowGeometry, QString::fromLatin1(v.toBase64()));
}

}  // namespace lf
