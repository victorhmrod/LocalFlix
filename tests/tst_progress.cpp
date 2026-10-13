// Persistent progress, completion, resume targets and Continue Watching.
#include "Clock.h"
#include "SqlUtil.h"
#include "TestSupport.h"

using namespace lf;
using namespace lf::test;

class TstProgress : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { Clock::reset(); }

    // ------------------------------------------------------------ saving / restoring

    void positionIsSavedAndRestoredAfterRestart()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Persist"), 2, &showId);
        auto& pm = env.app->progress();

        pm.begin(eps[0].file.id, true, 600000);
        pm.update(65000, 600000);
        pm.flush(FlushReason::Pause);
        QCOMPARE(env.app->library().progress(eps[0].file.id).positionMs, qint64(65000));  // immediate on pause

        pm.update(70500, 600000);
        pm.end(FlushReason::Shutdown);  // application closing

        env.restart();
        const Progress p = env.app->library().progress(eps[0].file.id);
        QCOMPARE(p.positionMs, qint64(70500));
        QCOMPARE(p.durationMs, qint64(600000));
        QVERIFY(p.lastWatchedAt > 0);
        QVERIFY(!p.completed);
        QCOMPARE(p.resumePositionMs(), qint64(70500));
        QCOMPARE(env.app->library().progress(eps[1].file.id).positionMs, qint64(0));  // independent per episode
    }

    void everyEpisodeHasIndependentProgress()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Independent"), 3, &showId);
        auto& lib = env.app->library();
        lib.saveProgress(eps[0].file.id, 11000, 100000);
        lib.saveProgress(eps[1].file.id, 22000, 200000);
        lib.markCompleted(eps[2].file.id, 300000);
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(11000));
        QCOMPARE(lib.progress(eps[1].file.id).positionMs, qint64(22000));
        QCOMPARE(lib.progress(eps[1].file.id).durationMs, qint64(200000));
        QVERIFY(!lib.progress(eps[0].file.id).completed);
        QVERIFY(lib.progress(eps[2].file.id).completed);
        QCOMPARE(lib.progress(eps[2].file.id).durationMs, qint64(300000));
    }

    void writesAreThrottled()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Throttle"), 1, &showId);
        auto& pm = env.app->progress();
        pm.begin(eps[0].file.id, true, 600000);
        const int base = pm.writeCount();

        pm.update(10000, 600000);
        pm.flush(FlushReason::Periodic);
        QCOMPARE(pm.writeCount(), base + 1);

        pm.update(10400, 600000);  // moved less than a second: not worth a write
        pm.flush(FlushReason::Periodic);
        QCOMPARE(pm.writeCount(), base + 1);

        pm.update(11200, 600000);
        pm.flush(FlushReason::Periodic);
        QCOMPARE(pm.writeCount(), base + 2);

        pm.flush(FlushReason::Pause);  // nothing changed: no write even on pause
        QCOMPARE(pm.writeCount(), base + 2);

        pm.update(11250, 600000);
        pm.flush(FlushReason::Pause);  // pause always persists a real change
        QCOMPARE(pm.writeCount(), base + 3);
        QCOMPARE(env.app->library().progress(eps[0].file.id).positionMs, qint64(11250));
        pm.end(FlushReason::Stop);
    }

    void periodicTimerSavesWithoutExplicitFlush()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Timer"), 1, &showId);
        PlaybackProgressManager fast(&env.app->library(), nullptr, 40);  // 40 ms instead of 5 s
        fast.begin(eps[0].file.id, true, 600000);
        fast.update(30000, 600000);
        QTRY_COMPARE_WITH_TIMEOUT(env.app->library().progress(eps[0].file.id).positionMs, qint64(30000), 3000);
        fast.update(45000, 600000);
        QTRY_COMPARE_WITH_TIMEOUT(env.app->library().progress(eps[0].file.id).positionMs, qint64(45000), 3000);
        // idle timer ticks do not keep writing the same value
        const int w = fast.writeCount();
        QTest::qWait(250);
        QCOMPARE(fast.writeCount(), w);
        fast.end(FlushReason::Stop);
    }

    void openingAndLeavingImmediatelyCreatesNoHistory()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Accident"), 2, &showId);
        auto& pm = env.app->progress();
        pm.begin(eps[0].file.id, true, 600000);
        pm.update(800, 600000);  // less than 2 s in
        pm.end(FlushReason::Switch);
        QCOMPARE(env.app->library().progress(eps[0].file.id).lastWatchedAt, qint64(0));
        QVERIFY(env.app->library().continueWatching().isEmpty());
    }

    void replayingACompletedEpisodeKeepsItCompleted()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Replay"), 2, &showId);
        auto& lib = env.app->library();
        lib.markCompleted(eps[0].file.id, 600000);
        QCOMPARE(lib.progress(eps[0].file.id).playCount, 1);

        auto& pm = env.app->progress();
        pm.begin(eps[0].file.id, /*persistPosition=*/false, 600000);
        pm.update(250000, 600000);
        pm.flush(FlushReason::Pause);
        pm.end(FlushReason::Stop);

        const Progress p = lib.progress(eps[0].file.id);
        QVERIFY(p.completed);               // still watched
        QCOMPARE(p.positionMs, qint64(0));  // position not clobbered
        QCOMPARE(p.resumePositionMs(), qint64(0));
        QVERIFY(p.lastWatchedAt > 0);       // but it counts as recently watched

        pm.begin(eps[0].file.id, false, 600000);
        pm.complete(600000);
        QCOMPARE(lib.progress(eps[0].file.id).playCount, 2);
    }

    void markWatchedUnwatchedAndReset()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Manual"), 3, &showId);
        auto& lib = env.app->library();
        lib.saveProgress(eps[0].file.id, 80000, 600000);

        QVERIFY(lib.setWatched({eps[0].file.id, eps[1].file.id}, true));
        QVERIFY(lib.progress(eps[0].file.id).completed);
        QVERIFY(lib.progress(eps[1].file.id).completed);
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(0));
        QVERIFY(!lib.progress(eps[2].file.id).completed);

        QVERIFY(lib.setWatched({eps[1].file.id}, false));
        QVERIFY(!lib.progress(eps[1].file.id).completed);

        lib.saveProgress(eps[2].file.id, 123000, 600000);
        QVERIFY(lib.resetProgress({eps[0].file.id, eps[2].file.id}));
        for (int i : {0, 2}) {
            const Progress p = lib.progress(eps[i].file.id);
            QVERIFY(!p.completed);
            QCOMPARE(p.positionMs, qint64(0));
            QCOMPARE(p.lastWatchedAt, qint64(0));
        }

        lib.markCompleted(eps[0].file.id, 1000);
        lib.saveProgress(eps[1].file.id, 9000, 1000);
        QVERIFY(lib.resetShowProgress(showId));
        for (const Episode& e : lib.episodes(showId)) {
            QVERIFY(!e.progress.completed);
            QCOMPARE(e.progress.positionMs, qint64(0));
        }
    }

    void durationIsRecordedOnTheFileOnlyOnce()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Dur"), 1, &showId);
        auto& pm = env.app->progress();
        QCOMPARE(env.app->library().mediaFile(eps[0].file.id)->durationMs, qint64(0));
        pm.begin(eps[0].file.id, true, 0);
        const int w0 = pm.writeCount();
        pm.update(5000, 421000);
        QCOMPARE(env.app->library().mediaFile(eps[0].file.id)->durationMs, qint64(421000));
        QCOMPARE(pm.writeCount(), w0 + 1);
        pm.update(6000, 421000);  // no further duration writes
        pm.update(7000, 421000);
        QCOMPARE(pm.writeCount(), w0 + 1);
        pm.end(FlushReason::Stop);
    }

    void audioAndSubtitleChoiceIsRemembered()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Tracks"), 1, &showId);
        auto& lib = env.app->library();
        QCOMPARE(lib.progress(eps[0].file.id).audioTrack, kTrackAuto);
        lib.saveTrackSelection(eps[0].file.id, 2, kTrackOff);
        env.restart();
        const Progress p = env.app->library().progress(eps[0].file.id);
        QCOMPARE(p.audioTrack, 2);
        QCOMPARE(p.subtitleTrack, kTrackOff);
    }

    // ------------------------------------------------------------ series resume target

    void resumeTargetFollowsTheViewingHistory()
    {
        qint64 t = 1000;
        Clock::setOverride([&] { return t += 1000; });
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Resume"), 4, &showId);
        auto& lib = env.app->library();

        // nothing watched: start at the first episode
        ResumeTarget r = lib.resumeTarget(showId);
        QCOMPARE(int(r.reason), int(ResumeReason::NotStarted));
        QCOMPARE(r.mediaFileId, eps[0].file.id);
        QCOMPARE(r.positionMs, qint64(0));

        // left partway through episode 1
        lib.saveProgress(eps[0].file.id, 30000, 600000);
        r = lib.resumeTarget(showId);
        QCOMPARE(int(r.reason), int(ResumeReason::InProgress));
        QCOMPARE(r.mediaFileId, eps[0].file.id);
        QCOMPARE(r.positionMs, qint64(30000));

        // finished episode 1: next unfinished is episode 2
        lib.markCompleted(eps[0].file.id, 600000);
        r = lib.resumeTarget(showId);
        QCOMPARE(int(r.reason), int(ResumeReason::NextUnfinished));
        QCOMPARE(r.mediaFileId, eps[1].file.id);
        QCOMPARE(r.positionMs, qint64(0));

        // jumped ahead and left episode 3 partway: that is where "Continue" goes
        lib.saveProgress(eps[2].file.id, 20000, 600000);
        r = lib.resumeTarget(showId);
        QCOMPARE(r.mediaFileId, eps[2].file.id);
        QCOMPARE(r.positionMs, qint64(20000));

        // finished 3 and 4: nothing after them, wrap to the unfinished episode 2
        lib.markCompleted(eps[2].file.id, 600000);
        lib.markCompleted(eps[3].file.id, 600000);
        r = lib.resumeTarget(showId);
        QCOMPARE(int(r.reason), int(ResumeReason::NextUnfinished));
        QCOMPARE(r.mediaFileId, eps[1].file.id);

        // everything completed
        lib.markCompleted(eps[1].file.id, 600000);
        r = lib.resumeTarget(showId);
        QVERIFY(r.allCompleted());
        QCOMPARE(r.mediaFileId, eps[0].file.id);  // replay from the top
        QCOMPARE(r.positionMs, qint64(0));
    }

    void resumeTargetUsesTheConfiguredOrderNotFileOrder()
    {
        qint64 t = 1000;
        Clock::setOverride([&] { return t += 1000; });
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Custom order"), 4, &showId);
        auto& lib = env.app->library();
        const Id season = eps[0].seasonId;

        lib.markCompleted(eps[0].file.id, 600000);
        QCOMPARE(lib.resumeTarget(showId).mediaFileId, eps[1].file.id);

        // the user decides episode 4 should come right after episode 1
        QVERIFY(lib.applyLayout(showId, {{season, {eps[0].id, eps[3].id, eps[1].id, eps[2].id}}}));
        QCOMPARE(lib.resumeTarget(showId).mediaFileId, eps[3].file.id);
    }

    void manuallyWatchedEpisodesAreSkippedByResume()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Marked"), 5, &showId);
        auto& lib = env.app->library();
        lib.setWatched({eps[0].file.id, eps[1].file.id, eps[2].file.id}, true);  // "I already saw these"
        const ResumeTarget r = lib.resumeTarget(showId);
        QCOMPARE(r.mediaFileId, eps[3].file.id);
        QCOMPARE(int(r.reason), int(ResumeReason::NotStarted));
    }

    // ------------------------------------------------------------ continue watching

    void continueWatchingListsShowsAndMoviesMostRecentFirst()
    {
        qint64 t = 10000;
        Clock::setOverride([&] { return t += 1000; });
        Env env;
        auto& lib = env.app->library();
        Id showA = 0, showB = 0, showDone = 0;
        const auto a = env.makeShow(QStringLiteral("Show A"), 3, &showA);
        const auto b = env.makeShow(QStringLiteral("Show B"), 3, &showB);
        const auto done = env.makeShow(QStringLiteral("Show Done"), 2, &showDone);
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("cw/film.mkv")), env.makeFile(QStringLiteral("cw/short.mkv")),
                                              env.makeFile(QStringLiteral("cw/seen.mkv"))});
        QVector<Movie> movies = lib.movies();
        QCOMPARE(movies.size(), 3);
        auto movieByFile = [&](const QString& name) {
            for (const Movie& m : lib.movies())
                if (m.file.path.endsWith(name)) return m;
            return Movie{};
        };

        lib.saveProgress(a[1].file.id, 40000, 600000);                        // oldest
        lib.markCompleted(done[0].file.id, 600000);                           // show fully watched ->
        lib.markCompleted(done[1].file.id, 600000);                           //   must not be listed
        lib.markCompleted(b[0].file.id, 600000);                              // B: ep1 done, ep2 next
        lib.saveProgress(movieByFile(QStringLiteral("film.mkv")).file.id, 120000, 5400000);
        lib.saveProgress(movieByFile(QStringLiteral("short.mkv")).file.id, 2000, 5400000);   // < 5 s: not started
        lib.markCompleted(movieByFile(QStringLiteral("seen.mkv")).file.id, 5400000);         // finished: not listed

        const auto items = lib.continueWatching();
        QCOMPARE(items.size(), 3);
        // newest first: film (movie), Show B (next episode), Show A (in progress)
        QCOMPARE(items[0].kind, MediaKind::Movie);
        QCOMPARE(items[0].title, QStringLiteral("Film"));
        QCOMPARE(items[0].positionMs, qint64(120000));
        QVERIFY(qAbs(items[0].fraction() - 120000.0 / 5400000.0) < 1e-9);

        QCOMPARE(items[1].title, QStringLiteral("Show B"));
        QCOMPARE(items[1].mediaFileId, b[1].file.id);  // the *next* episode, since ep1 finished
        QCOMPARE(items[1].positionMs, qint64(0));
        QVERIFY(items[1].subtitle.contains(b[1].title));

        QCOMPARE(items[2].title, QStringLiteral("Show A"));
        QCOMPARE(items[2].mediaFileId, a[1].file.id);
        QCOMPARE(items[2].positionMs, qint64(40000));
        QVERIFY(items[2].lastWatchedAt > 0);
    }

    void continueWatchingSurvivesRestart()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Back again"), 3, &showId);
        env.app->library().markCompleted(eps[0].file.id, 600000);
        env.app->library().saveProgress(eps[1].file.id, 321000, 600000);
        env.restart();
        const auto items = env.app->library().continueWatching();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].mediaFileId, eps[1].file.id);
        QCOMPARE(items[0].positionMs, qint64(321000));
    }

    void recentlyAddedMixesShowsAndMovies()
    {
        qint64 t = 100000;
        Clock::setOverride([&] { return t += 1000; });
        Env env;
        auto& lib = env.app->library();
        Id showId = 0;
        env.makeShow(QStringLiteral("Older show"), 1, &showId);
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("ra/new_movie.mkv"))});
        const auto cards = lib.recentlyAdded(10);
        QCOMPARE(cards.size(), 2);
        QCOMPARE(cards[0].kind, CardKind::Movie);  // added last
        QCOMPARE(cards[1].kind, CardKind::Show);
    }
};

QTEST_GUILESS_MAIN(TstProgress)
#include "tst_progress.moc"
