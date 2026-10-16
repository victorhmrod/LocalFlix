#pragma once

#include "IPlaybackBackend.h"

#include <QHash>

#include <atomic>

struct mpv_handle;
struct mpv_event;
struct mpv_node;

namespace lf {

// IPlaybackBackend on top of libmpv. Events are drained on the GUI thread: the
// libmpv wakeup callback (which runs on an arbitrary mpv thread) only posts a
// queued call; no Qt or libmpv state is touched from it.
class MpvBackend : public IPlaybackBackend {
    Q_OBJECT
public:
    struct Options {
        bool video = true;       // vo=libmpv (a MpvWidget must attach a render context)
        bool audio = true;       // false -> ao=null (tests, headless runs)
        QString hwdec = QStringLiteral("auto-safe");
    };

    explicit MpvBackend(const Options& options, QObject* parent = nullptr);
    ~MpvBackend() override;

    // True when libmpv-2.dll can be loaded. Call before constructing a backend
    // to show a friendly message instead of a loader error.
    static bool libraryAvailable(QString* error = nullptr);

    bool isValid() const { return m_handle != nullptr; }
    QString initError() const { return m_initError; }
    mpv_handle* handle() const { return m_handle; }
    QString mpvVersion() const;
    // Display aspect ratio (width / height) of the video being played, 0 when unknown.
    double videoAspect() const;

    quint64 loadFile(const QString& path, double startSeconds) override;
    void stop() override;
    void setPaused(bool paused) override;
    void seekAbsolute(double seconds) override;
    void seekRelative(double seconds) override;
    void setVolume(int percent) override;
    void setMuted(bool muted) override;
    void setSpeed(double speed) override;
    void setAudioTrack(int trackId) override;
    void setSubtitleTrack(int trackId) override;
    bool addExternalSubtitle(const QString& path) override;
    void setPreferredLanguages(const QString& audio, const QString& subtitle) override;

    double positionSeconds() const override { return m_position; }
    double durationSeconds() const override { return m_duration; }
    bool isPaused() const override { return m_paused; }
    int volume() const override { return m_volume; }
    bool isMuted() const override { return m_muted; }
    double speed() const override { return m_speed; }
    QVector<TrackInfo> tracks() const override { return m_tracks; }
    QVector<ChapterInfo> chapters() const override { return m_chapters; }

signals:
    // Emitted at the start of destruction, before the mpv core is torn down, so
    // a render context attached by a widget can be released first.
    void aboutToDestroy();

private slots:
    void processEvents();

private:
    static void wakeupCallback(void* ctx);
    void handleEvent(const mpv_event* event);
    void handlePropertyChange(const mpv_event* event);
    void refreshTracks();
    void refreshChapters();
    bool setOptionString(const char* name, const QString& value);
    bool setPropertyString(const char* name, const QString& value);
    int command(const QByteArrayList& args);
    quint64 tokenForEntry(qint64 entryId) const;

    mpv_handle* m_handle = nullptr;
    QString m_initError;
    std::atomic<bool> m_wakeupPending{false};

    quint64 m_nextToken = 1;
    quint64 m_activeToken = 0;
    QHash<qint64, quint64> m_entryTokens;  // playlist entry id -> load token
    quint64 m_lastLoadToken = 0;

    double m_position = 0.0;
    double m_lastEmittedPosition = -1.0;
    double m_duration = 0.0;
    bool m_paused = false;
    int m_volume = 100;
    bool m_muted = false;
    double m_speed = 1.0;
    QVector<TrackInfo> m_tracks;
    QVector<ChapterInfo> m_chapters;
    Options m_options;
};

}  // namespace lf
