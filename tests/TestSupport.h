#pragma once

#include "ApplicationController.h"
#include "IPlaybackBackend.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

// QVERIFY returns void; helpers that return a value use this instead.
#define LF_REQUIRE(cond, ret)                                                               do {                                                                                        if (!QTest::qVerify(static_cast<bool>(cond), #cond, "", __FILE__, __LINE__)) return ret;     } while (false)

namespace lf::test {

// Scripted playback engine. Nothing happens on its own: the test decides when
// a load "finishes", when time advances and how a file ends, using chosen
// tokens (so stale events from replaced files can be simulated).
class FakeBackend : public IPlaybackBackend {
    Q_OBJECT
public:
    struct Load {
        QString path;
        double startSeconds = 0;
        quint64 token = 0;
    };

    quint64 loadFile(const QString& path, double startSeconds) override
    {
        Load l{path, startSeconds, ++m_nextToken};
        loads.push_back(l);
        m_paused = false;
        m_position = startSeconds;
        return l.token;
    }
    void stop() override { ++stopCalls; }
    void setPaused(bool paused) override
    {
        m_paused = paused;
        emit pausedChanged(paused);
    }
    void seekAbsolute(double s) override { seeks.push_back({s, false}); m_position = s; }
    void seekRelative(double s) override { seeks.push_back({s, true}); m_position += s; }
    void setVolume(int v) override { m_volume = v; }
    void setMuted(bool m) override { m_muted = m; }
    void setSpeed(double s) override { m_speed = s; }
    void setAudioTrack(int id) override { audioTrackRequests.push_back(id); }
    void setSubtitleTrack(int id) override { subtitleTrackRequests.push_back(id); }
    bool addExternalSubtitle(const QString&) override { return true; }
    void setPreferredLanguages(const QString& a, const QString& s) override { preferredAudio = a; preferredSubtitle = s; }

    double positionSeconds() const override { return m_position; }
    double durationSeconds() const override { return m_duration; }
    bool isPaused() const override { return m_paused; }
    int volume() const override { return m_volume; }
    bool isMuted() const override { return m_muted; }
    double speed() const override { return m_speed; }
    QVector<TrackInfo> tracks() const override { return m_tracks; }

    // ---- test controls -----------------------------------------------------
    quint64 lastToken() const { return m_nextToken; }
    const Load& lastLoad() const { return loads.last(); }
    void setDuration(double seconds) { m_duration = seconds; }
    void finishLoading(quint64 token = 0)
    {
        emit fileLoaded(token ? token : m_nextToken);
    }
    void tick(double seconds)
    {
        m_position = seconds;
        emit positionChanged(seconds);
    }
    void end(EndReason reason, quint64 token = 0, const QString& error = {})
    {
        EndFileInfo info;
        info.token = token ? token : m_nextToken;
        info.reason = reason;
        info.errorText = error;
        emit endFile(info);
    }

    QVector<Load> loads;
    int stopCalls = 0;
    QVector<QPair<double, bool>> seeks;  // (seconds, relative)
    QVector<int> audioTrackRequests;
    QVector<int> subtitleTrackRequests;
    QString preferredAudio;
    QString preferredSubtitle;

private:
    quint64 m_nextToken = 0;
    double m_position = 0;
    double m_duration = 600;  // ten minutes unless a test says otherwise
    bool m_paused = false;
    int m_volume = 100;
    bool m_muted = false;
    double m_speed = 1.0;
    QVector<TrackInfo> m_tracks;
};

// A throw-away data directory with an initialised service graph and helpers to
// create stand-in "video" files (any non-empty file is enough: the library only
// stats files, and FakeBackend never decodes).
class Env {
public:
    explicit Env(const QString& dirName = QString())
    {
        QVERIFY2(m_dir.isValid(), "temp dir");
        m_root = dirName.isEmpty() ? m_dir.path() : QDir(m_dir.path()).filePath(dirName);
        QDir().mkpath(m_root);
        auto fake = std::make_unique<FakeBackend>();
        backend = fake.get();
        app = std::make_unique<ApplicationController>(QDir(m_root).filePath(QStringLiteral("data")), std::move(fake));
        QString error;
        ok = app->initialize(&error);
        QVERIFY2(ok, qPrintable(error));
    }

    QString mediaDir() const
    {
        const QString d = QDir(m_root).filePath(QStringLiteral("media"));
        QDir().mkpath(d);
        return d;
    }

    // Creates `relativeName` (inside the media dir) with some content.
    QString makeFile(const QString& relativeName, int bytes = 2048) const
    {
        const QString full = QDir(mediaDir()).filePath(relativeName);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QByteArray(bytes, 'x'));
            f.close();
        }
        return LibraryManager::normalizePath(full);
    }

    QStringList makeFiles(const QStringList& names) const
    {
        QStringList out;
        for (const QString& n : names) out << makeFile(n);
        return out;
    }

    // Creates a show with `count` imported episodes ep01.mp4 ... and returns the episode ids in order.
    QVector<Episode> makeShow(const QString& title, int count, Id* showIdOut = nullptr)
    {
        const Id showId = app->library().createShow(title);
        LF_REQUIRE(showId != kInvalidId, {});
        if (showIdOut) *showIdOut = showId;
        QStringList names;
        for (int i = 1; i <= count; ++i) names << QStringLiteral("%1/ep%2.mp4").arg(title).arg(i, 2, 10, QLatin1Char('0'));
        const QStringList paths = makeFiles(names);
        const ImportReport r = app->importer().importEpisodesSync(showId, kInvalidId, paths);
        LF_REQUIRE(r.ok && r.added == count, {});
        return app->library().episodes(showId);
    }

    // Close and reopen everything on the same database file (simulates an app restart).
    void restart()
    {
        auto fake = std::make_unique<FakeBackend>();
        app->shutdown();
        app.reset();
        backend = fake.get();
        app = std::make_unique<ApplicationController>(QDir(m_root).filePath(QStringLiteral("data")), std::move(fake));
        QString error;
        ok = app->initialize(&error);
        QVERIFY2(ok, qPrintable(error));
    }

    QString dbPath() const { return QDir(m_root).filePath(QStringLiteral("data/library.db")); }
    QString root() const { return m_root; }

    QTemporaryDir m_dir;
    QString m_root;
    bool ok = false;
    FakeBackend* backend = nullptr;
    std::unique_ptr<ApplicationController> app;
};

// Drives the controller like the real backend would for one successful start.
inline void startAndLoad(FakeBackend* b, double durationSeconds = 600)
{
    b->setDuration(durationSeconds);
    b->finishLoading();
}

inline QueueContext showContext(Id showId)
{
    return {ContextType::Show, showId};
}

}  // namespace lf::test
