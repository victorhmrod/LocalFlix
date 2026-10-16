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

QString writeChapteredClip(const QString& dir)
{
    return lf::test::writeChapteredClip(videoPath(QStringLiteral("long_resume_test.mkv")), dir);
}

// A 6 s "episode" with two audio tracks, made as an mpv EDL file out of two generated clips: the audio tracks
// are `first` and `second` ("eng" or "por", titled "EN" / "BR") in that order. Written next to the others in `dir`
// and named like a video so it can be imported; mpv recognises it by its content.
QString writeTwoAudioEpisode(const QString& dir, const QString& name, const QString& first, const QString& second)
{
    const QString out = QDir(dir).filePath(name);
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write("# mpv EDL v0\n");
    int n = 0;
    for (const QString& lang : {first, second}) {
        const bool english = lang == QLatin1String("eng");
        const QByteArray clip = QDir::fromNativeSeparators(videoPath(english ? QStringLiteral("video_27.mkv") : QStringLiteral("anime_final.webm"))).toUtf8();
        if (n++ > 0) f.write("!new_stream\n");
        f.write("!track_meta,lang=" + lang.toUtf8() + ",title=" + (english ? "EN" : "BR") + "\n");
        f.write("%" + QByteArray::number(clip.size()) + "%" + clip + ",0,6\n");
    }
    return LibraryManager::normalizePath(out);
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

    // ---- chapters, and skipping an ending that runs to the end of the file ----

    void chaptersAreKnownWhenTheFileIsLoadedAndForgottenWhenItEnds()
    {
        QTemporaryDir dir;
        const QString clip = writeChapteredClip(dir.path());
        QVERIFY(!clip.isEmpty());

        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy changed(&b, &IPlaybackBackend::chaptersChanged);
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        int chaptersAtLoad = -1;  // what a fileLoaded listener sees: the controller relies on this
        connect(&b, &IPlaybackBackend::fileLoaded, &b, [&] { chaptersAtLoad = int(b.chapters().size()); });

        b.loadFile(clip, 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        QCOMPARE(chaptersAtLoad, 3);
        const QVector<ChapterInfo> chapters = b.chapters();
        QCOMPARE(chapters.size(), 3);
        QCOMPARE(chapters[0].title, QStringLiteral("Opening"));
        QCOMPARE(chapters[1].title, QStringLiteral("Episode"));
        QCOMPARE(chapters[2].title, QStringLiteral("Ending"));
        QVERIFY(qAbs(chapters[0].startSeconds - 0.0) < 0.05);
        QVERIFY(qAbs(chapters[1].startSeconds - 8.0) < 0.05);
        QVERIFY(qAbs(chapters[2].startSeconds - 14.0) < 0.05);
        QVERIFY(changed.count() >= 1);

        // the chapter titles say what they are
        const auto segments = detectSkipSegments(chapters, qint64(b.durationSeconds() * 1000.0));
        QCOMPARE(segments.size(), 2);
        QCOMPARE(int(segments[0].kind), int(SegmentKind::Intro));
        QCOMPARE(int(segments[1].kind), int(SegmentKind::Ending));
        QVERIFY(segments[1].toEnd);

        b.stop();
        QVERIFY(waitFor([&] { return ended.count() >= 1; }, 12000));
        QVERIFY(b.chapters().isEmpty());
        QVERIFY(changed.last().at(0).value<QVector<ChapterInfo>>().isEmpty());
    }

    void tracksAreKnownWhenTheFileIsLoaded()
    {
        // the controller puts back the audio/subtitle choices from the fileLoaded signal, so the list must be there by then
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        int audioAtLoad = -1;
        connect(&b, &IPlaybackBackend::fileLoaded, &b, [&] {
            audioAtLoad = 0;
            for (const TrackInfo& t : b.tracks())
                if (t.type == TrackType::Audio) ++audioAtLoad;
        });
        b.loadFile(videoPath(QStringLiteral("downloaded_episode_04.mp4")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        QVERIFY2(audioAtLoad >= 1, "no audio track listed yet when fileLoaded was emitted");
    }

    void aFileWithoutChaptersReportsNone()
    {
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        b.loadFile(videoPath(QStringLiteral("video_27.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        QVERIFY(b.chapters().isEmpty());
    }

    void seekingToTheDurationEndsTheFileWithEofNotWithAStop_data()
    {
        QTest::addColumn<bool>("pausedBefore");
        QTest::newRow("playing") << false;
        // A seek to the end while the engine is still *paused* does not end the file (it parks on the last frame),
        // which is why the controller resumes before skipping an ending; this is the sequence it performs.
        QTest::newRow("paused, then resumed") << true;
    }

    void seekingToTheDurationEndsTheFileWithEofNotWithAStop()
    {
        // This is what "skip ending" does for an ending that lasts until the end of the file. The clip is 40 s
        // long, so an end that arrives within seconds can only be the seek, and it must be a natural end.
        QFETCH(bool, pausedBefore);
        MpvBackend b(headless());
        QSignalSpy loaded(&b, &IPlaybackBackend::fileLoaded);
        QSignalSpy ended(&b, &IPlaybackBackend::endFile);
        const quint64 token = b.loadFile(videoPath(QStringLiteral("long_resume_test.mkv")), 0);
        QVERIFY(waitFor([&] { return loaded.count() >= 1; }, 15000));
        QVERIFY(b.durationSeconds() > 30);
        if (pausedBefore) {
            b.setPaused(true);
            QVERIFY(waitFor([&] { return b.isPaused(); }, 12000));
            b.setPaused(false);
        }

        QElapsedTimer clock;
        clock.start();
        // truncated to milliseconds, like the controller's duration
        b.seekAbsolute(double(qint64(b.durationSeconds() * 1000.0)) / 1000.0);
        QVERIFY2(waitFor([&] { return ended.count() >= 1; }, 8000), "the seek to the end did not end the file");
        QVERIFY(clock.elapsed() < 6000);
        const auto info = ended.first().at(0).value<EndFileInfo>();
        QCOMPARE(info.token, token);
        QCOMPARE(int(info.reason), int(EndReason::Eof));
        QTest::qWait(500);
        QCOMPARE(ended.count(), 1);
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

    void realEngineSkipsTheOpeningAndTheEndingByItself()
    {
        QTemporaryDir dir;
        const QString chaptered = writeChapteredClip(dir.path());
        QVERIFY(!chaptered.isEmpty());

        auto backend = std::make_unique<MpvBackend>(headless());
        MpvBackend* engine = backend.get();
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::move(backend));
        QVERIFY(app.initialize());
        app.settings().setAutoPlayCountdownSeconds(0);
        app.settings().setPlaybackSpeed(4.0);
        app.settings().setAutoSkipIntro(true);
        app.settings().setAutoSkipEnding(true);

        const Id showId = app.library().createShow(QStringLiteral("Skips"));
        // the chaptered clip sorts first; the second episode is a plain 6 s file with no chapters
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, {chaptered, videoPath(QStringLiteral("downloaded_episode_04.mp4"))}).added, 2);
        const auto eps = app.library().episodes(showId);
        QVERIFY(eps[0].file.path.endsWith(QStringLiteral("chaptered_episode.mkv")));

        auto& pc = app.playback();
        QStringList order;  // what happened, in order
        QElapsedTimer sinceEndingSkip;
        qint64 endingToCompletedMs = -1;
        connect(&pc, &PlaybackController::segmentSkipped, this, [&](lf::SkipSegment seg, bool automatic) {
            order << QStringLiteral("skip-%1%2").arg(seg.noun(), automatic ? QStringLiteral("-auto") : QString());
            if (seg.kind == SegmentKind::Ending) sinceEndingSkip.start();
        });
        connect(&pc, &PlaybackController::itemCompleted, this, [&](lf::QueueEntry) {
            order << QStringLiteral("completed");
            if (sinceEndingSkip.isValid() && endingToCompletedMs < 0) endingToCompletedMs = sinceEndingSkip.elapsed();
        });
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        // the opening (0-8 s) is skipped as soon as the file is open
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 15000));
        QCOMPARE(pc.skipSegments().size(), 2);
        QVERIFY2(waitFor([&] { return engine->positionSeconds() >= 7.5; }, 8000), qPrintable(QString::number(engine->positionSeconds())));
        QVERIFY(order.startsWith(QStringLiteral("skip-intro-auto")));

        // the ending (14 s to the end) is skipped when playback reaches it; the file then ends by itself, long
        // before its 26 s would have played (at 4x speed they would still take 6.5 s)
        QVERIFY2(waitFor([&] { return order.contains(QStringLiteral("skip-ending-auto")); }, 30000), qPrintable(order.join(QLatin1Char(','))));
        QVERIFY2(waitFor([&] { return endingToCompletedMs >= 0; }, 10000), "the episode did not end after its ending was skipped");
        QVERIFY2(endingToCompletedMs < 4000, qPrintable(QString::number(endingToCompletedMs)));
        QCOMPARE(order, QStringList({QStringLiteral("skip-intro-auto"), QStringLiteral("skip-ending-auto"), QStringLiteral("completed")}));
        QVERIFY(app.library().episode(eps[0].id)->progress.completed);

        // and the next episode starts through the normal end-of-file path, then plays out untouched
        QVERIFY2(waitFor([&] { return finished.count() >= 1; }, 30000), "the series did not finish");
        QCOMPARE(now.count(), 2);
        QCOMPARE(now.at(1).at(0).value<QueueEntry>().mediaFileId, eps[1].file.id);
        QCOMPARE(order.size(), 4);  // ep 2 has no chapters: nothing else was skipped
        for (const Episode& e : app.library().episodes(showId)) QVERIFY(e.progress.completed);
    }

    void realEngineSkipsTheEndingOfFilesWithoutChaptersFromTheSeasonMark()
    {
        QTemporaryDir dir;
        auto backend = std::make_unique<MpvBackend>(headless());
        MpvBackend* engine = backend.get();
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::move(backend));
        QVERIFY(app.initialize());
        app.settings().setAutoPlayCountdownSeconds(0);
        app.settings().setPlaybackSpeed(4.0);  // automatic skipping is on, as by default

        const Id showId = app.library().createShow(QStringLiteral("Season mark"));
        // neither file has chapters; by name the 6 s clip comes first, then the 40 s one
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, {videoPath(QStringLiteral("long_resume_test.mkv")), videoPath(QStringLiteral("downloaded_episode_04.mp4"))}).added, 2);
        const auto eps = app.library().episodes(showId);
        QVERIFY(eps[0].file.path.endsWith(QStringLiteral("downloaded_episode_04.mp4")));
        QVERIFY(eps[1].file.path.endsWith(QStringLiteral("long_resume_test.mkv")));
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;  // longer than the 6 s clip, so it does not apply to it; 10 s - 40 s of the long one
        QVERIFY(app.library().setSeasonSkipMarks(eps[0].seasonId, marks));

        auto& pc = app.playback();
        QStringList order;
        QElapsedTimer sinceSkip;
        qint64 skipToCompletedMs = -1;
        connect(&pc, &PlaybackController::segmentSkipped, this, [&](lf::SkipSegment seg, bool automatic) {
            order << QStringLiteral("skip-%1%2").arg(seg.noun(), automatic ? QStringLiteral("-auto") : QString());
            sinceSkip.start();
        });
        connect(&pc, &PlaybackController::itemCompleted, this, [&](lf::QueueEntry) {
            order << QStringLiteral("completed");
            if (sinceSkip.isValid() && skipToCompletedMs < 0) skipToCompletedMs = sinceSkip.elapsed();
        });
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        Q_UNUSED(engine)

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QVERIFY2(waitFor([&] { return finished.count() >= 1; }, 60000), qPrintable(order.join(QLatin1Char(','))));
        // the short clip played through (no skip), the long one was skipped from the mark and ended at once:
        // at 4x its last 30 s would have taken 7.5 s more
        QCOMPARE(order, QStringList({QStringLiteral("completed"), QStringLiteral("skip-ending-auto"), QStringLiteral("completed")}));
        QVERIFY2(skipToCompletedMs >= 0 && skipToCompletedMs < 4000, qPrintable(QString::number(skipToCompletedMs)));
        for (const Episode& e : app.library().episodes(showId)) QVERIFY(e.progress.completed);
    }

    void realEngineKeepsTheChosenAudioLanguageForTheNextEpisodes()
    {
        // three episodes with an English and a Portuguese audio track; the second lists them the other way round
        QTemporaryDir dir;
        const QStringList files = {writeTwoAudioEpisode(dir.path(), QStringLiteral("ep01.mkv"), QStringLiteral("eng"), QStringLiteral("por")),
                                   writeTwoAudioEpisode(dir.path(), QStringLiteral("ep02.mkv"), QStringLiteral("por"), QStringLiteral("eng")),
                                   writeTwoAudioEpisode(dir.path(), QStringLiteral("ep03.mkv"), QStringLiteral("eng"), QStringLiteral("por"))};
        auto backend = std::make_unique<MpvBackend>(headless());
        MpvBackend* engine = backend.get();
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::move(backend));
        QVERIFY(app.initialize());
        app.settings().setAutoPlayCountdownSeconds(0);
        app.settings().setPlaybackSpeed(4.0);

        const Id showId = app.library().createShow(QStringLiteral("Dubbed"));
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, files).added, 3);
        const auto eps = app.library().episodes(showId);
        auto audioLanguage = [&] {
            for (const TrackInfo& t : engine->tracks())
                if (t.type == TrackType::Audio && t.selected) return t.language;
            return QString();
        };
        auto settled = [&](const QString& language) {  // the engine takes a moment to report a switch; then it must stay
            if (!waitFor([&] { return audioLanguage() == language; }, 8000)) return false;
            QTest::qWait(600);
            return audioLanguage() == language;
        };

        auto& pc = app.playback();
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);
        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 15000));
        QVERIFY2(settled(QStringLiteral("eng")), "baseline: the first episode starts in English");
        int portuguese = 0;
        for (const TrackInfo& t : engine->tracks())
            if (t.type == TrackType::Audio && t.language == QLatin1String("por")) portuguese = t.id;
        QCOMPARE(portuguese, 2);
        pc.selectAudioTrack(portuguese);  // the user switches the series to Portuguese
        QVERIFY(settled(QStringLiteral("por")));

        // next episode, picked by the user: Portuguese is its first audio track
        pc.next();
        QVERIFY(waitFor([&] { return now.count() >= 2; }, 15000));
        QVERIFY2(settled(QStringLiteral("por")), qPrintable(QStringLiteral("episode 2 plays in '%1'").arg(audioLanguage())));

        // and when an episode ends by itself and the next one starts after the countdown
        QVERIFY2(waitFor([&] { return now.count() >= 3; }, 30000), "the third episode did not start by itself");
        QVERIFY2(settled(QStringLiteral("por")), qPrintable(QStringLiteral("episode 3 plays in '%1'").arg(audioLanguage())));
    }

    void realEngineKeepsTheSubtitlesOffForTheNextEpisodeOfTheShow()
    {
        // two copies of the generated clip with its sidecar subtitle, so the show has two episodes that start with one
        QTemporaryDir dir;
        const QStringList names = {QStringLiteral("ep01"), QStringLiteral("ep02")};
        QStringList files;
        for (const QString& n : names) {
            QVERIFY(QFile::copy(videoPath(QStringLiteral("downloaded_episode_04.mp4")), QDir(dir.path()).filePath(n + QStringLiteral(".mp4"))));
            QVERIFY(QFile::copy(videoPath(QStringLiteral("downloaded_episode_04.srt")), QDir(dir.path()).filePath(n + QStringLiteral(".srt"))));
            files << LibraryManager::normalizePath(QDir(dir.path()).filePath(n + QStringLiteral(".mp4")));
        }
        auto backend = std::make_unique<MpvBackend>(headless());
        MpvBackend* engine = backend.get();
        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::move(backend));
        QVERIFY(app.initialize());
        app.settings().setAutoPlayNext(false);  // the test moves on by itself

        const Id showId = app.library().createShow(QStringLiteral("Subtitles off"));
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, files).added, 2);
        const auto eps = app.library().episodes(showId);
        auto subtitle = [&](bool selected) {
            for (const TrackInfo& t : engine->tracks())
                if (t.type == TrackType::Subtitle && t.selected == selected) return true;
            return false;
        };

        auto& pc = app.playback();
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);
        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 15000));
        QVERIFY2(waitFor([&] { return subtitle(true); }, 8000), "baseline: the sidecar subtitle is on by default");

        pc.selectSubtitleTrack(kTrackOff);  // the user turns the subtitles off in the first episode
        QVERIFY(waitFor([&] { return !subtitle(true); }, 8000));

        pc.next();
        QVERIFY(waitFor([&] { return now.count() >= 2; }, 15000));
        QVERIFY(waitFor([&] { return subtitle(false); }, 8000));  // the second episode has its subtitle...
        QTest::qWait(700);                                         // (give the engine time to settle)
        QVERIFY2(!subtitle(true), "the second episode started with subtitles although they were turned off for the show");  // ...switched off
    }

    void realEngineSkippingTheEndingWhilePausedStillFinishesTheEpisode()
    {
        QTemporaryDir dir;
        const QString chaptered = writeChapteredClip(dir.path());
        QVERIFY(!chaptered.isEmpty());

        ApplicationController app(QDir(dir.path()).filePath(QStringLiteral("data")), std::make_unique<MpvBackend>(headless()));
        QVERIFY(app.initialize());
        app.settings().setAutoPlayCountdownSeconds(0);
        app.settings().setAutoSkipIntro(false);  // the button is what is under test, not the automatic skip
        app.settings().setAutoSkipEnding(false);
        const Id showId = app.library().createShow(QStringLiteral("Paused skip"));
        QCOMPARE(app.importer().importEpisodesSync(showId, kInvalidId, {chaptered}).added, 1);
        const Episode ep = app.library().episodes(showId).first();
        app.library().saveProgress(ep.file.id, 16000, 40000);  // 16 s in: inside the ending (14 s - end), resumable

        auto& pc = app.playback();
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        QVERIFY(pc.play(showContext(showId), ep.file.id, StartMode::Resume));
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 15000));
        pc.setPaused(true);
        QVERIFY(waitFor([&] { return pc.state() == PlaybackState::Paused; }, 12000));
        QVERIFY2(waitFor([&] { return pc.activeSegment().valid(); }, 8000), "the Skip Ending button is not offered while paused");
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Ending));

        pc.skipActiveSegment();
        // the clip has 24 s left: finishing within seconds means the skip got through despite the pause
        QVERIFY2(waitFor([&] { return finished.count() >= 1; }, 10000), "Skip Ending while paused did not finish the episode");
        QCOMPARE(completed.count(), 1);
        QVERIFY(app.library().episode(ep.id)->progress.completed);
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
