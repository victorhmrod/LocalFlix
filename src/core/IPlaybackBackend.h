#pragma once

#include "Types.h"

#include <QLocale>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

namespace lf {

// Why the backend stopped playing a file. Only Eof means "the media played to
// its end"; everything else must never trigger automatic advancement.
enum class EndReason {
    Eof,       // natural end of the file
    Stop,      // stopped by a command (manual stop, or replaced by another load)
    Quit,      // the player is shutting down
    Error,     // the file could not be loaded or playback failed
    Redirect   // playlist redirect (not used by LocalFlix)
};

struct EndFileInfo {
    quint64 token = 0;  // the value returned by loadFile() for this playback
    EndReason reason = EndReason::Stop;
    int errorCode = 0;
    QString errorText;
};

struct TrackInfo {
    int id = 0;
    TrackType type = TrackType::Audio;
    QString language;  // ISO code as reported by the container, may be empty
    QString title;
    QString codec;
    bool selected = false;
    bool external = false;
    bool isDefault = false;

    QString label() const
    {
        QString lang;
        if (!language.isEmpty()) {
            const QLocale::Language l = QLocale::codeToLanguage(language);
            lang = (l != QLocale::AnyLanguage && l != QLocale::C) ? QLocale::languageToString(l) : language;
        }
        QStringList parts;
        if (!lang.isEmpty()) parts << lang;
        if (!title.isEmpty() && title.compare(lang, Qt::CaseInsensitive) != 0) parts << title;
        if (parts.isEmpty()) parts << QStringLiteral("Track %1").arg(id);
        QString out = parts.join(QStringLiteral(" — "));
        if (!codec.isEmpty()) out += QStringLiteral(" (%1)").arg(codec);
        if (external) out += QStringLiteral(" [external]");
        return out;
    }
};

// One chapter of the file being played, as embedded in the container.
struct ChapterInfo {
    QString title;
    double startSeconds = 0.0;
};

// Abstraction over the media engine. The real implementation wraps libmpv;
// tests inject a scripted fake. All methods and signals are used on the GUI
// thread only.
class IPlaybackBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IPlaybackBackend() override = default;

    // Starts `path` at `startSeconds`. Returns a token that identifies this
    // playback in fileLoaded()/endFile(), so events of a replaced file can be
    // told apart from the current one.
    virtual quint64 loadFile(const QString& path, double startSeconds) = 0;
    virtual void stop() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void seekAbsolute(double seconds) = 0;
    virtual void seekRelative(double seconds) = 0;
    virtual void setVolume(int percent) = 0;
    virtual void setMuted(bool muted) = 0;
    virtual void setSpeed(double speed) = 0;
    virtual void setAudioTrack(int trackId) = 0;     // 0 = off
    virtual void setSubtitleTrack(int trackId) = 0;  // 0 = off
    virtual bool addExternalSubtitle(const QString& path) = 0;
    virtual void setPreferredLanguages(const QString& audio, const QString& subtitle) = 0;

    virtual double positionSeconds() const = 0;
    virtual double durationSeconds() const = 0;
    virtual bool isPaused() const = 0;
    virtual int volume() const = 0;
    virtual bool isMuted() const = 0;
    virtual double speed() const = 0;
    virtual QVector<TrackInfo> tracks() const = 0;
    // Chapters of the current file, in playback order (empty when it has none).
    virtual QVector<ChapterInfo> chapters() const = 0;

signals:
    void fileLoaded(quint64 token);
    void durationChanged(double seconds);
    void positionChanged(double seconds);
    void pausedChanged(bool paused);
    void volumeChanged(int percent);
    void mutedChanged(bool muted);
    void speedChanged(double speed);
    void tracksChanged(QVector<lf::TrackInfo> tracks);
    void chaptersChanged(QVector<lf::ChapterInfo> chapters);
    void endFile(lf::EndFileInfo info);
};

}  // namespace lf

Q_DECLARE_METATYPE(lf::EndFileInfo)
Q_DECLARE_METATYPE(lf::TrackInfo)
Q_DECLARE_METATYPE(QVector<lf::TrackInfo>)
Q_DECLARE_METATYPE(lf::ChapterInfo)
Q_DECLARE_METATYPE(QVector<lf::ChapterInfo>)
