// Integration tests against the real libmpv engine and real media files
// (headless: no video or audio output). The files come from
// scripts/make_test_videos.py; without them these tests are skipped, never faked.
#include "ApplicationController.h"
#include "MpvBackend.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <functional>

using namespace lf;
using namespace lf::test;

namespace {

QString videoPath(const QString& name)
{
    return QDir(QStringLiteral(LOCALFLIX_TESTDATA_DIR)).filePath(name);
}

bool waitFor(const std::function<bool()>& cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) return false;
        QTest::qWait(10);
    }
    return true;
}

MpvBackend::Options headless()
{
    MpvBackend::Options o;
    o.video = false;
    o.audio = false;
    return o;
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

}  // namespace

class TstMpvBackend : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<lf::EndFileInfo>();
        qRegisterMetaType<QVector<lf::TrackInfo>>();
        QString err;
        QVERIFY2(MpvBackend::libraryAvailable(&err), qPrintable(err));
        if (!QFileInfo::exists(videoPath(QStringLiteral("video_27.mkv"))))
            QSKIP("Test videos missing: run `python scripts/make_test_videos.py` first.");
    }

    void engineStartsAndReportsItsVersion()
    {
        MpvBackend b(headless());
        QVERIFY2(b.isValid(), qPrintable(b.initError()));
        QVERIFY(b.mpvVersion().contains(QStringLiteral("mpv")));
    }

    void naturalEndOfFileIsReportedAsEofWithTheLoadToken()
    {
        MpvBackend b(headless());
        QVERIFY(b.isValid());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        QSignalSpy positions(&b, &IPlaybackBackend::positionChanged);

        const quint64 token = b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(token > 0);
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 30000));
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(loaded.first().at(0).value<quint64>(), token);
        QCOMPARE(ended.count(), 1);
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Eof));
        QVERIFY(b.durationSeconds() > 5.5 && b.durationSeconds() < 6.5);
        QVERIFY(positions.count() >= 3);  // position was reported while playing
        double last = -1;
        for (const auto& p : positions) {
            QVERIFY(p.at(0).toDouble() >= last - 0.01);  // no backwards jumps in a plain playthrough
            last = p.at(0).toDouble();
        }
        // and nothing else arrives afterwards
        QTest::qWait(500);
        QCOMPARE(ended.count(), 1);
    }

    void everySupportedContainerPlaysToItsEnd_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("mkv/h264") << "video_27.mkv";
        QTest::newRow("mp4/h264") << "downloaded_episode_04.mp4";
        QTest::newRow("webm/vp8") << "anime_final.webm";
        QTest::newRow("avi/mpeg4") << "episode_01.avi";
        QTest::newRow("mov/h264") << "S01E05 - The Reunion.mov";
    }

    void everySupportedContainerPlaysToItsEnd()
    {
        QFETCH(QString, file);
        MpvBackend b(headless());
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        b.setSpeed(4.0);  // keep the suite quick
        const quint64 token = b.loadFile(videoPath(file), 0);
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 30000));
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Eof));
    }

    void startOffsetIsHonouredAndEndStillComes()
    {
        MpvBackend b(headless());
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        QSignalSpy positions(&b, &IPlaybackBackend::positionChanged);
        b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 3.0);
        QVERIFY(waitFor([&] { return positions.count() >= 1; }, 15000));
        const double first = positions.first().at(0).toDouble();
        QVERIFY2(first >= 2.8 && first < 3.8, qPrintable(QString::number(first)));
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 15000));
        QCOMPARE(int(ended.first().at(0).value<EndFileInfo>().reason), int(EndReason::Eof));
    }

    void manualStopIsReportedAsStopNotEof()
    {
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        const quint64 token = b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        b.stop();
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 12000));
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Stop));
        QTest::qWait(1500);
        QCOMPARE(ended.count(), 1);  // no EOF follows a stop
    }

    void replacingAFileEndsTheOldOneWithStopAndLoadsTheNew()
    {
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        b.setSpeed(4.0);
        const quint64 first = b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        const quint64 second = b.loadFile(videoPath(QStringLiteral("anime_final.webm")), 0);
        QVERIFY(second != first);
        QVERIFY(waitFor([&] { return ended.count() >= 2; }, 30000));

        const auto stopOfFirst = ended.at(0).at(0).value<EndFileInfo>();
        QCOMPARE(stopOfFirst.token, first);  // the old file's end carries the OLD token
        QCOMPARE(int(stopOfFirst.reason), int(EndReason::Stop));
        const auto eofOfSecond = ended.at(1).at(0).value<EndFileInfo>();
        QCOMPARE(eofOfSecond.token, second);
        QCOMPARE(int(eofOfSecond.reason), int(EndReason::Eof));
        QCOMPARE(loaded.at(1).at(0).value<quint64>(), second);
    }

    void missingFileIsAnErrorWithTheToken()
    {
        MpvBackend b(headless());
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        const quint64 token = b.loadFile(videoPath(QStringLiteral("this-file-does-not-exist.mkv")), 0);
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 15000));
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Error));
        QVERIFY(!info.errorText.isEmpty());
    }

    void corruptFileIsAnErrorNotACrash()
    {
        QTemporaryDir dir;
        const QString garbage = QDir(dir.path()).filePath(QStringLiteral("garbage.mkv"));
        QFile f(garbage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray junk(64 * 1024, 0);
        for (int i = 0; i < junk.size(); ++i) junk[i] = char((i * 31 + 7) & 0xFF);
        f.write(junk);
        f.close();

        MpvBackend b(headless());
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        const quint64 token = b.loadFile(garbage, 0);
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 20000));
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Error));

        // the engine survives and plays a good file afterwards
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
    }

    void pauseSeekVolumeMuteAndSpeed()
    {
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy paused(&b, &IPlaybackBackend::pausedChanged);
        QSignalSpy volume(&b, &IPlaybackBackend::volumeChanged);
        QSignalSpy muted(&b, &IPlaybackBackend::mutedChanged);
        QSignalSpy speed(&b, &IPlaybackBackend::speedChanged);
        QSignalSpy positions(&b, &IPlaybackBackend::positionChanged);
        b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));

        b.setPaused(true);
        QVERIFY(waitFor([&] { return b.isPaused(); }, 12000));
        QVERIFY(waitFor([&] { return !paused.isEmpty() && paused.last().at(0).toBool(); }, 12000));

        positions.clear();
        b.seekAbsolute(4.0);
        QVERIFY(waitFor([&] { return !positions.isEmpty() && qAbs(positions.last().at(0).toDouble() - 4.0) < 0.6; }, 12000));

        b.setVolume(40);
        QVERIFY(waitFor([&] { return b.volume() == 40; }, 12000));
        b.setMuted(true);
        QVERIFY(waitFor([&] { return b.isMuted(); }, 12000));
        b.setMuted(false);
        b.setSpeed(2.0);
        QVERIFY(waitFor([&] { return qAbs(b.speed() - 2.0) < 0.01; }, 12000));
        QVERIFY(!volume.isEmpty() && !muted.isEmpty() && !speed.isEmpty());

        b.seekRelative(-2.0);
        QVERIFY(waitFor([&] { return qAbs(b.positionSeconds() - 2.0) < 0.7; }, 12000));
        b.setPaused(false);
        QVERIFY(waitFor([&] { return !b.isPaused(); }, 12000));
    }

    void audioAndSubtitleTracksAreListedAndSelectable()
    {
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        // downloaded_episode_04.srt sits next to the .mp4 and is picked up automatically (sub-auto)
        b.loadFile(videoPath(QStringLiteral("downloaded_episode_04.mp4")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        QVERIFY(waitFor([&] { return b.tracks().size() >= 2; }, 12000));

        int audioId = 0;
        int subId = 0;
        for (const TrackInfo& t : b.tracks()) {
            if (t.type == TrackType::Audio) audioId = t.id;
            if (t.type == TrackType::Subtitle) subId = t.id;
        }
        QVERIFY2(audioId > 0, "no audio track listed");
        QVERIFY2(subId > 0, "sidecar subtitle not detected");

        b.setSubtitleTrack(0);
        QVERIFY(waitFor([&] {
            for (const TrackInfo& t : b.tracks())
                if (t.type == TrackType::Subtitle && t.selected) return false;
            return true;
        }, 5000));
        b.setSubtitleTrack(subId);
        QVERIFY(waitFor([&] {
            for (const TrackInfo& t : b.tracks())
                if (t.type == TrackType::Subtitle && t.id == subId && t.selected) return true;
            return false;
        }, 5000));

        // loading an additional external subtitle file
        QTemporaryDir dir;
        const QString extra = QDir(dir.path()).filePath(QStringLiteral("extra.srt"));
        QFile f(extra);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("1\n00:00:00,000 --> 00:00:02,000\nHello from an extra subtitle\n");
        f.close();
        const int before = int(b.tracks().size());
        QVERIFY(b.addExternalSubtitle(extra));
        QVERIFY(waitFor([&] { return b.tracks().size() == before + 1; }, 12000));
        QVERIFY(!b.addExternalSubtitle(QDir(dir.path()).filePath(QStringLiteral("nope.srt"))));
    }

    // ---- whole pipeline with the real engine (the §16 acceptance flow, minus the UI) ----

    void realEngineAdvancesToTheNextEpisodeAndFinishesTheSeries()
    {
        QTemporaryDir dir;
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::make_unique<MpvBackend>(headless()));
        QString err;
        QVERIFY2(app.initialize(&err), qPrintable(err));
        app.settings().setAutoPlayCountdownSeconds(0);
        app.settings().setPlaybackSpeed(4.0);  // keep the suite quick; applied at the start of each session

        auto& lib = app.library();
        const Id showId = lib.createShow(QStringLiteral("Real playback"));
        // referenced in place, never copied or moved
        const QStringList files = {videoPath(QStringLiteral("video_27.mkv")), videoPath(QStringLiteral("downloaded_episode_04.mp4")),
                                   videoPath(QStringLiteral("episode_01.avi"))};
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, files).added, 3);
        auto eps = lib.episodes(showId);
        // order is the user's choice: avi, mkv, mp4
        QVector<Id> order;
        for (const QString& want : {QStringLiteral("episode_01.avi"), QStringLiteral("video_27.mkv"), QStringLiteral("downloaded_episode_04.mp4")})
            for (const Episode& e : eps)
                if (e.file.path.endsWith(want)) order << e.id;
        QVERIFY(lib.applyLayout(showId, {{eps[0].seasonId, order}}));
        eps = lib.episodes(showId);

        QSignalSpy completed(&app.playback(), &PlaybackController::itemCompleted);
        QSignalSpy finished(&app.playback(), &PlaybackController::queueFinished);
        QSignalSpy now(&app.playback(), &PlaybackController::nowPlaying);
        QVERIFY(app.playback().play(showContext(showId), eps[0].file.id, StartMode::FromStart));

        QVERIFY2(waitFor([&] { return finished.count() >= 1; }, 60000), "series did not finish by itself");
        QCOMPARE(completed.count(), 3);
        QCOMPARE(now.count(), 3);
        QStringList played;
        for (const auto& n : now) played << QFileInfo(n.at(0).value<QueueEntry>().path).fileName();
        QCOMPARE(played, QStringList({QStringLiteral("episode_01.avi"), QStringLiteral("video_27.mkv"), QStringLiteral("downloaded_episode_04.mp4")}));
        for (const Episode& e : lib.episodes(showId)) {
            QVERIFY(e.progress.completed);
            QVERIFY(e.progress.durationMs > 5000);
        }
        QCOMPARE(app.playback().state(), PlaybackState::Idle);
        for (const QString& f : files) QVERIFY(QFileInfo::exists(f));  // the media files are untouched
    }

    void realEngineResumesWhereTheApplicationWasClosed()
    {
        QTemporaryDir dir;
        const QString dataDir = QDir(dir.path()).filePath(QStringLiteral("data"));
        Id showId = 0;
        Id firstMedia = 0;
        {
            auto backend = std::make_unique<MpvBackend>(headless());
            MpvBackend* engine = backend.get();
            ApplicationController app(dataDir, std::move(backend));
            QVERIFY(app.initialize());
            showId = app.library().createShow(QStringLiteral("Resume real"));
            const QStringList files = {videoPath(QStringLiteral("long_resume_test.mkv")), videoPath(QStringLiteral("downloaded_episode_04.mp4"))};
            QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, files).added, 2);
            // pick the long clip by name; the import order follows the file names, not our argument order
            for (const Episode& e : app.library().episodes(showId))
                if (e.file.path.endsWith(QStringLiteral("long_resume_test.mkv"))) firstMedia = e.file.id;
            QVERIFY(firstMedia != 0);

            QSignalSpy now(&app.playback(), &PlaybackController::nowPlaying);
            QVERIFY(app.playback().play(showContext(showId), firstMedia, StartMode::FromStart));
            QVERIFY(waitFor([&] { return now.count() >= 1; }, 15000));
            app.playback().seekToMs(12000);  // seek to an arbitrary position
            QVERIFY(waitFor([&] { return engine->positionSeconds() >= 11.5; }, 12000));
            app.shutdown();  // close the application
        }
        {
            auto backend = std::make_unique<MpvBackend>(headless());
            MpvBackend* engine = backend.get();
            ApplicationController app(dataDir, std::move(backend));  // reopen
            QVERIFY(app.initialize());
            const auto items = app.library().continueWatching();
            QCOMPARE(items.size(), 1);
            QCOMPARE(items[0].mediaFileId, firstMedia);
            QVERIFY2(items[0].positionMs >= 11500 && items[0].positionMs < 15000, qPrintable(QString::number(items[0].positionMs)));

            QSignalSpy positions(engine, &IPlaybackBackend::positionChanged);
            QVERIFY(app.playback().play(showContext(showId), items[0].mediaFileId, StartMode::Auto));
            QVERIFY(waitFor([&] { return positions.count() >= 1; }, 15000));
            const double first = positions.first().at(0).toDouble();
            QVERIFY2(first >= 11.4 && first < 14.5, qPrintable(QString::number(first)));  // resumed, not restarted from 0
        }
    }

    void realEngineMissingFileOffersRecoveryThenPlaysAfterRelink()
    {
        QTemporaryDir dir;
        // a private copy of one clip that we are free to move around
        const QString original = QDir(dir.path()).filePath(QStringLiteral("clips/copy.mkv"));
        QDir().mkpath(QFileInfo(original).absolutePath());
        QVERIFY(QFile::copy(videoPath(QStringLiteral("long_resume_test.mkv")), original));

        auto backend = std::make_unique<MpvBackend>(headless());
        MpvBackend* engine = backend.get();
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::move(backend));
        QVERIFY(app.initialize());
        const Id showId = app.library().createShow(QStringLiteral("Moved"));
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, {LibraryManager::normalizePath(original)}).added, 1);
        const Episode ep = app.library().episodes(showId).first();
        app.library().saveProgress(ep.file.id, 12000, 40000);  // had watched 12 s of it

        const QString moved = QDir(dir.path()).filePath(QStringLiteral("elsewhere/renamed.mkv"));
        QDir().mkpath(QFileInfo(moved).absolutePath());
        QVERIFY(QFile::rename(original, moved));

        QSignalSpy failed(&app.playback(), &PlaybackController::playbackFailed);
        QVERIFY(!app.playback().play(showContext(showId), ep.file.id, StartMode::Auto));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(int(failed.first().at(1).value<PlaybackFailure>().kind), int(FailureKind::MissingFile));

        QVERIFY(app.library().relinkFile(ep.file.id, moved).ok);
        QSignalSpy positions(engine, &IPlaybackBackend::positionChanged);
        QVERIFY(app.playback().playEntry(ep.file.id, StartMode::Resume));
        QVERIFY(waitFor([&] { return positions.count() >= 1; }, 15000));
        QVERIFY2(positions.first().at(0).toDouble() >= 11.4, qPrintable(QString::number(positions.first().at(0).toDouble())));  // progress survived the move
        QVERIFY(samePath(app.library().episode(ep.id)->file.path, LibraryManager::normalizePath(moved)));
    }
};

QTEST_GUILESS_MAIN(TstMpvBackend)
#include "tst_mpv_backend.moc"
