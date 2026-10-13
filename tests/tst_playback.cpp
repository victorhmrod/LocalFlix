// PlaybackController with a scripted backend: natural end vs. manual stop vs.
// error, stale events, countdown/advance rules, resume modes, missing files.
#include "TestSupport.h"

using namespace lf;
using namespace lf::test;

namespace {

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

// Starts an episode and completes the backend's load handshake.
bool startEpisode(Env& env, Id showId, const Episode& e, StartMode mode = StartMode::Auto, double durationSeconds = 600)
{
    const bool started = env.app->playback().play(showContext(showId), e.file.id, mode);
    if (!started) return false;
    startAndLoad(env.backend, durationSeconds);
    return env.app->playback().state() == PlaybackState::Playing;
}

}  // namespace

class TstPlayback : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ natural end

    void naturalEndMarksCompletedAndStartsNextEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Auto"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[0]));
        QCOMPARE(env.backend->loads.size(), 1);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[0].file.path));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);

        env.backend->tick(120);
        env.backend->end(EndReason::Eof);

        QCOMPARE(completed.count(), 1);
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.app->library().progress(eps[0].file.id).durationMs, qint64(600000));
        QCOMPARE(pending.count(), 1);
        QCOMPARE(pending.first().at(0).value<QueueEntry>().mediaFileId, eps[1].file.id);

        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));
        QCOMPARE(pc.currentEntry().mediaFileId, eps[1].file.id);
    }

    void countdownDelaysTheNextEpisodeAndReportsItsLength()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Countdown"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(1);
        auto& pc = env.app->playback();
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);

        QCOMPARE(pc.state(), PlaybackState::AdvancePending);
        QCOMPARE(pending.count(), 1);
        QCOMPARE(pending.first().at(1).toInt(), 1000);
        QTest::qWait(300);
        QCOMPARE(env.backend->loads.size(), 1);  // still counting down
        QTRY_COMPARE_WITH_TIMEOUT(env.backend->loads.size(), 2, 4000);
    }

    void lastEpisodeEndsCleanlyWithoutAdvancing()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Finale"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[2]));
        env.backend->end(EndReason::Eof);

        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<QueueEntry>().mediaFileId, eps[2].file.id);
        QCOMPARE(pending.count(), 0);
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QVERIFY(env.app->library().progress(eps[2].file.id).completed);
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);  // nothing else was started
    }

    void walkingThroughTheWholeSeriesPlaysEveryEpisodeOnce()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Marathon"), 4, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);

        QVERIFY(startEpisode(env, showId, eps[0]));
        for (int i = 1; i < eps.size(); ++i) {
            env.backend->end(EndReason::Eof);
            QTRY_COMPARE(env.backend->loads.size(), i + 1);
            QVERIFY(samePath(env.backend->lastLoad().path, eps[i].file.path));
            startAndLoad(env.backend);
        }
        env.backend->end(EndReason::Eof);
        QCOMPARE(finished.count(), 1);
        for (const Episode& e : env.app->library().episodes(showId)) QVERIFY(e.progress.completed);
        QCOMPARE(env.backend->loads.size(), 4);
    }

    void autoPlayOffStopsAfterTheEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Manual advance"), 2, &showId);
        env.app->settings().setAutoPlayNext(false);
        auto& pc = env.app->playback();
        QSignalSpy ended(&pc, &PlaybackController::playbackEndedWithoutAdvance);
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.first().at(1).value<QueueEntry>().mediaFileId, eps[1].file.id);  // offered, not started
        QCOMPARE(pending.count(), 0);
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);
    }

    void autoAdvanceCanSkipAlreadyCompletedEpisodes()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Skippy"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        env.app->library().markCompleted(eps[1].file.id, 1000);
        auto& pc = env.app->playback();

        // setting off (default): ep2 plays again
        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));

        // setting on: ep2 is skipped when advancing automatically...
        env.app->settings().setSkipCompleted(true);
        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 4);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[2].file.path));

        // ...but the manual Next button still follows the configured queue strictly
        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        pc.next();
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));
    }

    // ------------------------------------------------------------ things that must NOT advance

    void manualStopDoesNotStartTheNextEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Stop"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy stopped(&pc, &PlaybackController::playbackStopped);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->tick(90);
        pc.stop();
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QCOMPARE(env.backend->stopCalls, 1);
        QCOMPARE(stopped.count(), 1);

        // the engine reports the stop it was asked for; and, belt and braces, even a
        // (nonsensical) late EOF for the stopped playback must change nothing
        env.backend->end(EndReason::Stop);
        env.backend->end(EndReason::Eof);
        QTest::qWait(100);

        QCOMPARE(env.backend->loads.size(), 1);
        QCOMPARE(completed.count(), 0);
        const Progress p = env.app->library().progress(eps[0].file.id);
        QVERIFY(!p.completed);
        QCOMPARE(p.positionMs, qint64(90000));  // saved on stop
    }

    void stopReportedByTheEngineIsNeverACompletion()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("EngineStop"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        QSignalSpy completed(&env.app->playback(), &PlaybackController::itemCompleted);
        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->tick(10);
        env.backend->end(EndReason::Stop);  // e.g. the engine was stopped from outside
        QTest::qWait(100);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(env.backend->loads.size(), 1);
        QCOMPARE(env.app->playback().state(), PlaybackState::Idle);
    }

    void playbackErrorDoesNotAdvance()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Broken"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->tick(30);
        env.backend->end(EndReason::Error, 0, QStringLiteral("unsupported or unrecognised file format"));

        QCOMPARE(failed.count(), 1);
        const auto failure = failed.first().at(1).value<PlaybackFailure>();
        QCOMPARE(int(failure.kind), int(FailureKind::LoadError));
        QVERIFY(failure.message.contains(QStringLiteral("unsupported")));
        QCOMPARE(completed.count(), 0);
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
    }

    void loadErrorBeforePlaybackStartedDoesNotAdvance()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("NeverStarted"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);
        QVERIFY(pc.play(showContext(showId), eps[0].file.id));
        QCOMPARE(pc.state(), PlaybackState::Loading);
        env.backend->end(EndReason::Error, 0, QStringLiteral("could not be opened"));  // fileLoaded never came
        QCOMPARE(failed.count(), 1);
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    void eofBeforeTheFileEverLoadedIsNotACompletion()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Phantom"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);
        QVERIFY(pc.play(showContext(showId), eps[0].file.id));
        env.backend->end(EndReason::Eof);  // loading state: nothing ever played
        QCOMPARE(completed.count(), 0);
        QCOMPARE(failed.count(), 1);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
    }

    void seekingNearTheEndDoesNotCompleteTheEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Seeker"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);

        QVERIFY(startEpisode(env, showId, eps[0], StartMode::Auto, 600));
        pc.seekToMs(599000);  // one second from the end
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 599.0);
        env.backend->tick(599.2);
        QTest::qWait(50);
        QCOMPARE(completed.count(), 0);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.backend->loads.size(), 1);

        // closing the application right now keeps the position, still not completed
        pc.shutdown();
        const Progress p = env.app->library().progress(eps[0].file.id);
        QVERIFY(!p.completed);
        QVERIFY(p.positionMs >= 599000);
    }

    void staleEventsFromAReplacedFileAreIgnored()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Stale"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);

        QVERIFY(startEpisode(env, showId, eps[0]));
        const quint64 firstToken = env.backend->lastToken();
        pc.next();  // user presses Next: episode 2 is loaded, episode 1 is replaced
        QCOMPARE(env.backend->loads.size(), 2);
        const quint64 secondToken = env.backend->lastToken();
        QVERIFY(secondToken != firstToken);

        // events that were already queued for the first file arrive late
        env.backend->end(EndReason::Stop, firstToken);   // what mpv sends for a replaced file
        env.backend->end(EndReason::Eof, firstToken);    // even a race where it also hit its end
        env.backend->finishLoading(firstToken);
        QTest::qWait(100);

        QCOMPARE(completed.count(), 0);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.backend->loads.size(), 2);                       // no extra load either
        QCOMPARE(pc.state(), PlaybackState::Loading);                 // still waiting for episode 2
        env.backend->finishLoading(secondToken);
        QCOMPARE(pc.state(), PlaybackState::Playing);
        QCOMPARE(pc.currentEntry().mediaFileId, eps[1].file.id);
    }

    void aSingleEndOfFileProducesExactlyOneTransition()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Once"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[0]));
        const quint64 token = env.backend->lastToken();
        env.backend->end(EndReason::Eof, token);
        env.backend->end(EndReason::Eof, token);  // duplicate delivery
        env.backend->end(EndReason::Eof, token);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QTest::qWait(100);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(pending.count(), 1);
        QCOMPARE(env.backend->loads.size(), 2);
        QCOMPARE(env.app->library().progress(eps[0].file.id).playCount, 1);
    }

    // ------------------------------------------------------------ countdown control

    void cancellingTheCountdownStopsPlaybackGracefully()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Cancel"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(1);
        auto& pc = env.app->playback();
        QSignalSpy cancelled(&pc, &PlaybackController::autoAdvanceCancelled);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QCOMPARE(pc.state(), PlaybackState::AdvancePending);
        pc.cancelAutoAdvance();
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(pc.state(), PlaybackState::Idle);

        QTest::qWait(1400);  // longer than the countdown would have been
        QCOMPARE(env.backend->loads.size(), 1);
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);  // the finished one stays finished
        pc.cancelAutoAdvance();                                          // harmless when nothing is pending
        QCOMPARE(cancelled.count(), 1);
    }

    void playNowSkipsTheCountdown()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Now"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(30);
        auto& pc = env.app->playback();
        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QCOMPARE(pc.state(), PlaybackState::AdvancePending);
        pc.advanceNow();
        QCOMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));
        // the old countdown must not fire a second start later
        QTest::qWait(150);
        QCOMPARE(env.backend->loads.size(), 2);
    }

    void stopAndNextDuringCountdownAreHandled()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Interrupt"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(1);
        auto& pc = env.app->playback();

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        pc.stop();  // Stop during the countdown cancels it
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(1300);
        QCOMPARE(env.backend->loads.size(), 1);

        QVERIFY(startEpisode(env, showId, eps[0], StartMode::FromStart));
        env.backend->end(EndReason::Eof);
        pc.next();  // Next during the countdown plays right away, exactly once
        QCOMPARE(env.backend->loads.size(), 3);
        QTest::qWait(1300);
        QCOMPARE(env.backend->loads.size(), 3);
    }

    void reorderingDuringCountdownRetargetsTheNextEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Retarget"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(2);
        auto& pc = env.app->playback();
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QCOMPARE(pending.count(), 1);
        QCOMPARE(pending.last().at(0).value<QueueEntry>().mediaFileId, eps[1].file.id);

        // user drags episode 3 in front of episode 2 while the overlay counts down
        QVERIFY(env.app->library().moveEpisode(eps[2].id, eps[0].seasonId, 1));
        QTRY_COMPARE(pending.count(), 2);
        QCOMPARE(pending.last().at(0).value<QueueEntry>().mediaFileId, eps[2].file.id);
        QVERIFY(pending.last().at(1).toInt() <= 2000);

        QTRY_COMPARE_WITH_TIMEOUT(env.backend->loads.size(), 2, 5000);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[2].file.path));  // the new order won
    }

    void removingTheNextEpisodeDuringCountdownRetargetsOrFinishes()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Remove next"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(1);
        auto& pc = env.app->playback();
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QVERIFY(env.app->library().removeEpisodes({eps[1].id}));  // the only remaining episode disappears
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(1200);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    // ------------------------------------------------------------ navigation

    void nextAndPreviousFollowTheConfiguredOrder()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Nav"), 3, &showId);
        auto& lib = env.app->library();
        auto& pc = env.app->playback();
        // configured order: ep3, ep1, ep2
        QVERIFY(lib.applyLayout(showId, {{eps[0].seasonId, {eps[2].id, eps[0].id, eps[1].id}}}));

        QVERIFY(pc.play(showContext(showId), eps[2].file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        QVERIFY(!pc.hasPrevious());
        QVERIFY(pc.hasNext());

        pc.next();
        QVERIFY(samePath(env.backend->lastLoad().path, eps[0].file.path));
        startAndLoad(env.backend);
        pc.next();
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));
        startAndLoad(env.backend);
        QVERIFY(!pc.hasNext());
        const int loadsAtEnd = env.backend->loads.size();
        pc.next();  // nothing after the last one
        QCOMPARE(env.backend->loads.size(), loadsAtEnd);

        pc.previous();
        QVERIFY(samePath(env.backend->lastLoad().path, eps[0].file.path));
    }

    void reorderingWhilePlayingUpdatesWhatNextMeans()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Live reorder"), 3, &showId);
        auto& pc = env.app->playback();
        QSignalSpy updated(&pc, &PlaybackController::queueUpdated);

        QVERIFY(startEpisode(env, showId, eps[0], StartMode::FromStart));
        QVERIFY(pc.hasNext());
        QVERIFY(env.app->library().moveEpisode(eps[0].id, eps[0].seasonId, 2));  // current episode dragged to the end
        QTRY_VERIFY(updated.count() >= 1);
        QVERIFY(!pc.hasNext());
        QVERIFY(pc.hasPrevious());
        pc.previous();
        QVERIFY(samePath(env.backend->lastLoad().path, eps[2].file.path));  // previous in the *new* order
    }

    void removingThePlayingEpisodeStillAdvancesToTheNextOne()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Vanish"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        QVERIFY(startEpisode(env, showId, eps[1], StartMode::FromStart));
        QSignalSpy errors(&env.app->library(), &LibraryManager::errorOccurred);
        QVERIFY(env.app->library().removeEpisodes({eps[1].id}));  // removed from library while playing
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[2].file.path));
        QCOMPARE(errors.count(), 0);  // no database error for the vanished entry
    }

    // ------------------------------------------------------------ starting positions

    void resumeStartsAtTheSavedPosition()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Resume"), 2, &showId);
        env.app->library().saveProgress(eps[0].file.id, 100000, 600000);
        auto& pc = env.app->playback();

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::Auto));
        QCOMPARE(env.backend->lastLoad().startSeconds, 100.0);

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::Resume));
        QCOMPARE(env.backend->lastLoad().startSeconds, 100.0);
    }

    void resumeSettingOffAsksInsteadOfJumping()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Ask"), 1, &showId);
        env.app->library().saveProgress(eps[0].file.id, 100000, 600000);
        env.app->settings().setResumeAutomatically(false);
        auto& pc = env.app->playback();
        QSignalSpy ask(&pc, &PlaybackController::resumeDecisionNeeded);

        QVERIFY(!pc.play(showContext(showId), eps[0].file.id, StartMode::Auto));
        QCOMPARE(env.backend->loads.size(), 0);  // nothing started yet
        QCOMPARE(ask.count(), 1);
        QCOMPARE(ask.first().at(1).toLongLong(), qint64(100000));

        // an explicit answer from the user then starts it
        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);
    }

    void tinyPositionsAreNotWorthResuming()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Tiny"), 1, &showId);
        env.app->library().saveProgress(eps[0].file.id, 2500, 600000);  // 2.5 s in
        QVERIFY(env.app->playback().play(showContext(showId), eps[0].file.id, StartMode::Auto));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);
    }

    void completedEpisodesReplayFromTheStartAndStayCompleted()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Rewatch"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        env.app->library().markCompleted(eps[0].file.id, 600000);
        auto& pc = env.app->playback();

        QVERIFY(pc.play(showContext(showId), eps[0].file.id, StartMode::Resume));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);
        startAndLoad(env.backend);
        env.backend->tick(200);
        pc.setPaused(true);  // would normally persist the position
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.app->library().progress(eps[0].file.id).positionMs, qint64(0));

        pc.stop();
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);
        // and "Continue" still points past it
        QCOMPARE(env.app->library().resumeTarget(showId).mediaFileId, eps[1].file.id);
    }

    void watchAgainAfterEverythingCompletedStartsAtTheTop()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Done"), 2, &showId);
        for (const Episode& e : eps) env.app->library().markCompleted(e.file.id, 600000);
        const ResumeTarget target = env.app->library().resumeTarget(showId);
        QVERIFY(target.allCompleted());
        QVERIFY(env.app->playback().play(showContext(showId), target.mediaFileId, StartMode::Auto));
        QCOMPARE(env.backend->lastLoad().startSeconds, 0.0);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[0].file.path));
    }

    // ------------------------------------------------------------ saving

    void progressIsSavedOnPauseOnSwitchAndOnShutdown()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Saves"), 2, &showId);
        auto& pc = env.app->playback();
        auto& lib = env.app->library();

        QVERIFY(startEpisode(env, showId, eps[0], StartMode::FromStart));
        env.backend->tick(33);
        pc.setPaused(true);  // immediate save on pause
        QCOMPARE(pc.state(), PlaybackState::Paused);
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(33000));

        pc.setPaused(false);
        env.backend->tick(41);
        pc.next();  // save when switching episodes
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(41000));
        startAndLoad(env.backend);
        env.backend->tick(12);
        pc.shutdown();  // and when the application closes
        QCOMPARE(lib.progress(eps[1].file.id).positionMs, qint64(12000));
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(41000));  // earlier one untouched
    }

    void closingTheApplicationMidEpisodeResumesThere()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Acceptance"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);

        QVERIFY(startEpisode(env, showId, eps[0], StartMode::FromStart));
        env.backend->tick(250);
        env.app->playback().seekToMs(251500);
        env.app->shutdown();  // "close the application"

        env.restart();  // "reopen it"
        const auto items = env.app->library().continueWatching();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].mediaFileId, eps[0].file.id);
        QVERIFY(qAbs(items[0].positionMs - 251500) < 1000);

        QVERIFY(env.app->playback().play(showContext(showId), items[0].mediaFileId, StartMode::Auto));
        QVERIFY(qAbs(env.backend->lastLoad().startSeconds - 251.5) < 1.0);
        startAndLoad(env.backend);
        env.backend->end(EndReason::Eof);  // let it finish naturally
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));  // next episode starts

        env.app->shutdown();
        env.restart();  // second restart: completed + progress still correct
        const auto after = env.app->library().episodes(showId);
        QVERIFY(after[0].progress.completed);
        QVERIFY(!after[1].progress.completed);
        QCOMPARE(env.app->library().resumeTarget(showId).mediaFileId, eps[1].file.id);
    }

    void trackChoiceIsRestoredWhenAFileStartsAgain()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("TrackMemory"), 1, &showId);
        env.app->library().saveTrackSelection(eps[0].file.id, 2, 3);
        QVERIFY(startEpisode(env, showId, eps[0]));
        QCOMPARE(env.backend->audioTrackRequests.size(), 1);
        QCOMPARE(env.backend->audioTrackRequests.first(), 2);
        QCOMPARE(env.backend->subtitleTrackRequests.first(), 3);
    }

    // ------------------------------------------------------------ files

    void missingFileProducesRecoveryInformationAndNeverReachesTheBackend()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Gone"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);

        QVERIFY(QFile::remove(eps[1].file.path));  // disconnected drive / deleted / renamed
        QVERIFY(!pc.play(showContext(showId), eps[1].file.id));
        QCOMPARE(env.backend->loads.size(), 0);
        QCOMPARE(failed.count(), 1);
        const auto entry = failed.first().at(0).value<QueueEntry>();
        const auto failure = failed.first().at(1).value<PlaybackFailure>();
        QCOMPARE(entry.mediaFileId, eps[1].file.id);
        QCOMPARE(int(failure.kind), int(FailureKind::MissingFile));
        QVERIFY(failure.message.contains(QDir::toNativeSeparators(eps[1].file.path)));
        QVERIFY(env.app->library().episode(eps[1].id)->file.missing);
        QCOMPARE(pc.state(), PlaybackState::Idle);

        // recovery: the user recreates the file somewhere else and relinks it
        const QString elsewhere = env.makeFile(QStringLiteral("recovered/ep02-new-name.mp4"));
        QVERIFY(env.app->library().relinkFile(eps[1].file.id, elsewhere).ok);
        QVERIFY(pc.playEntry(eps[1].file.id, StartMode::Resume));
        QVERIFY(samePath(env.backend->lastLoad().path, elsewhere));
        QVERIFY(!env.app->library().episode(eps[1].id)->file.missing);
    }

    void advancingIntoAMissingFileDoesNotLoopOrCrash()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Hole"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);
        QVERIFY(QFile::remove(eps[1].file.path));

        QVERIFY(startEpisode(env, showId, eps[0]));
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(env.backend->loads.size(), 1);  // episode 2 was never handed to the engine
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(100);
        QCOMPARE(failed.count(), 1);  // and nothing retries by itself

        // the user chooses "skip to next" from the error panel
        pc.next();
        QCOMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[2].file.path));
    }

    void startingSomethingNotInTheQueueIsReportedNotCrashed()
    {
        Env env;
        Id showId = 0;
        env.makeShow(QStringLiteral("Q"), 1, &showId);
        auto& pc = env.app->playback();
        QSignalSpy failed(&pc, &PlaybackController::playbackFailed);
        env.app->queue().setContext(showContext(showId));
        QVERIFY(!pc.playEntry(424242));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(int(failed.first().at(1).value<PlaybackFailure>().kind), int(FailureKind::NotInQueue));
    }

    // ------------------------------------------------------------ movies & collections

    void aMovieEndsWithoutAdvancingAndKeepsItsOwnProgress()
    {
        Env env;
        auto& lib = env.app->library();
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("mv/solo.mkv")), env.makeFile(QStringLiteral("mv/other.mkv"))});
        env.app->settings().setAutoPlayCountdownSeconds(0);
        const Movie solo = [&] {
            for (const Movie& m : lib.movies())
                if (m.file.path.endsWith(QStringLiteral("solo.mkv"))) return m;
            return Movie{};
        }();
        auto& pc = env.app->playback();
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);

        QVERIFY(pc.play({ContextType::Movie, solo.id}, solo.file.id, StartMode::Auto));
        startAndLoad(env.backend, 5400);
        env.backend->tick(1234);
        pc.stop();
        QCOMPARE(lib.movie(solo.id)->progress.positionMs, qint64(1234000));

        QVERIFY(pc.play({ContextType::Movie, solo.id}, solo.file.id, StartMode::Auto));
        QCOMPARE(env.backend->lastLoad().startSeconds, 1234.0);
        startAndLoad(env.backend, 5400);
        env.backend->end(EndReason::Eof);
        QCOMPARE(finished.count(), 1);
        QVERIFY(lib.movie(solo.id)->progress.completed);
        QCOMPARE(env.backend->loads.size(), 2);  // never wandered on to the other movie
    }

    void collectionPlaysShowsAndMoviesInSequence()
    {
        Env env;
        auto& lib = env.app->library();
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Col show"), 2, &showId);
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("cm/film.mkv"))});
        const Movie movie = lib.movies().first();
        const Id col = lib.createCollection(QStringLiteral("Night"));
        lib.addToCollection(col, CollectionItemType::Movie, movie.id);
        lib.addToCollection(col, CollectionItemType::Show, showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();

        QVERIFY(pc.play({ContextType::Collection, col}, movie.file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[0].file.path));
        startAndLoad(env.backend);
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 3);
        QVERIFY(samePath(env.backend->lastLoad().path, eps[1].file.path));
    }

    // ------------------------------------------------------------ player options

    void volumeSpeedAndTrackCommandsReachTheBackend()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Options"), 1, &showId);
        auto& pc = env.app->playback();
        QVERIFY(startEpisode(env, showId, eps[0]));
        pc.setVolume(60);
        QCOMPARE(env.backend->volume(), 60);
        pc.setVolume(500);  // clamped to the engine's range
        QCOMPARE(env.backend->volume(), 130);
        pc.setMuted(true);
        QVERIFY(env.backend->isMuted());
        pc.setSpeed(1.5);
        QCOMPARE(env.backend->speed(), 1.5);
        pc.setSpeed(99);
        QCOMPARE(env.backend->speed(), 4.0);
        pc.seekByMs(10000);
        QCOMPARE(env.backend->seeks.last().first, 10.0);
        QVERIFY(env.backend->seeks.last().second);  // relative
        pc.selectSubtitleTrack(2);
        QCOMPARE(env.backend->subtitleTrackRequests.last(), 2);
        pc.selectAudioTrack(1);
        QCOMPARE(env.backend->audioTrackRequests.last(), 1);
        // the scripted backend reports no selected subtitle, so the other stream is remembered as "off"
        QCOMPARE(env.app->library().progress(eps[0].file.id).subtitleTrack, kTrackOff);
    }

    void newPlaybackSessionStartsAtTheConfiguredDefaultSpeed()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Speedy"), 1, &showId);
        env.app->settings().setPlaybackSpeed(1.25);
        QVERIFY(env.app->playback().play(showContext(showId), eps[0].file.id, StartMode::FromStart));
        QCOMPARE(env.backend->speed(), 1.25);
    }
};

QTEST_GUILESS_MAIN(TstPlayback)
#include "tst_playback.moc"
