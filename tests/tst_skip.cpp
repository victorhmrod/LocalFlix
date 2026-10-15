// Skipping the opening and the ending: chapter classification and segment
// detection (pure functions), then the PlaybackController rules with a
// scripted backend: when the button is offered, when a skip happens by
// itself, and that an ending which runs to the end of the file finishes the
// item only through the engine's end-of-file.
#include "TestSupport.h"

#include "SkipSegments.h"

using namespace lf;
using namespace lf::test;

namespace {

ChapterInfo ch(const QString& title, double seconds)
{
    return ChapterInfo{title, seconds};
}

// A typical episode: cold open, opening 0:30-2:00, story, ending 9:00-10:00 (with the next-episode preview).
QVector<ChapterInfo> typicalChapters()
{
    return {ch(QStringLiteral("Prologue"), 0),  ch(QStringLiteral("Opening"), 30), ch(QStringLiteral("Episode"), 120),
            ch(QStringLiteral("Ending"), 540),  ch(QStringLiteral("Preview"), 585)};
}

bool startWith(Env& env, Id showId, const Episode& e, const QVector<ChapterInfo>& chapters, double durationSeconds = 600,
               StartMode mode = StartMode::FromStart)
{
    env.backend->setChapters(chapters);
    if (!env.app->playback().play(showContext(showId), e.file.id, mode)) return false;
    startAndLoad(env.backend, durationSeconds);
    return env.app->playback().state() == PlaybackState::Playing;
}

}  // namespace

class TstSkip : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<lf::SkipSegment>();
    }

    // ------------------------------------------------------------ chapter titles

    void chapterTitlesAreClassifiedByWholeWords_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("role");
        const int intro = int(ChapterRole::Intro);
        const int ending = int(ChapterRole::Ending);
        const int preview = int(ChapterRole::Preview);
        const int other = int(ChapterRole::Other);

        QTest::newRow("Opening") << "Opening" << intro;
        QTest::newRow("OP") << "OP" << intro;
        QTest::newRow("op lower case") << "op" << intro;
        QTest::newRow("OP1") << "OP1" << intro;
        QTest::newRow("Opening 2") << "Opening 2" << intro;
        QTest::newRow("Opening Theme") << "Opening Theme" << intro;
        QTest::newRow("Opening: song name") << "Opening: Some Song Name" << intro;
        QTest::newRow("OP - song name") << "OP - Some Song Name" << intro;
        QTest::newRow("[OP] song name") << "[OP] Some Song Name" << intro;
        QTest::newRow("Opening (TV size)") << "Opening (TV Size)" << intro;
        QTest::newRow("Intro") << "Intro" << intro;
        QTest::newRow("Abertura") << "Abertura" << intro;
        QTest::newRow("Abertura do episodio") << "Abertura do Episódio" << intro;
        QTest::newRow("NCOP") << "NCOP" << intro;

        QTest::newRow("Ending") << "Ending" << ending;
        QTest::newRow("ED") << "ED" << ending;
        QTest::newRow("ED2") << "ED2" << ending;
        QTest::newRow("Credits") << "Credits" << ending;
        QTest::newRow("End Credits") << "End Credits" << ending;
        QTest::newRow("Closing Credits") << "Closing Credits" << ending;
        QTest::newRow("Creditos") << "Creditos" << ending;
        QTest::newRow("Créditos") << "Créditos" << ending;
        QTest::newRow("CRÉDITOS") << "CRÉDITOS" << ending;
        QTest::newRow("Encerramento") << "Encerramento" << ending;
        QTest::newRow("Ending Theme") << "Ending Theme" << ending;
        QTest::newRow("Outro") << "Outro" << ending;

        QTest::newRow("Preview") << "Preview" << preview;
        QTest::newRow("Next Episode Preview") << "Next Episode Preview" << preview;
        QTest::newRow("Prévia") << "Prévia" << preview;
        QTest::newRow("Trailer") << "Trailer" << preview;

        QTest::newRow("empty") << "" << other;
        QTest::newRow("Prologue") << "Prologue" << other;
        QTest::newRow("Episode") << "Episode" << other;
        QTest::newRow("Part A") << "Part A" << other;
        QTest::newRow("Chapter 1") << "Chapter 1" << other;
        QTest::newRow("The End") << "The End" << other;
        QTest::newRow("substring Operation") << "Operation" << other;
        QTest::newRow("substring Edward") << "Edward" << other;
        QTest::newRow("substring Editing") << "Editing" << other;
        QTest::newRow("Ed's Place") << "Ed's Place" << other;
        QTest::newRow("Opening Night") << "Opening Night" << other;
        QTest::newRow("Chapter 1: Opening Night") << "Chapter 1: Opening Night" << other;
        QTest::newRow("Post-Credits Scene") << "Post-Credits Scene" << other;
        QTest::newRow("Opening Credits is ambiguous") << "Opening Credits" << other;
        QTest::newRow("Next Day") << "Next Day" << other;
    }

    void chapterTitlesAreClassifiedByWholeWords()
    {
        QFETCH(QString, title);
        QFETCH(int, role);
        QCOMPARE(int(classifyChapterTitle(title)), role);
    }

    // ------------------------------------------------------------ segment detection

    void aTypicalEpisodeHasAnOpeningAndAnEndingThatIncludesThePreview()
    {
        const auto segs = detectSkipSegments(typicalChapters(), 600000);
        QCOMPARE(segs.size(), 2);

        QCOMPARE(int(segs[0].kind), int(SegmentKind::Intro));
        QCOMPARE(segs[0].startMs, qint64(30000));
        QCOMPARE(segs[0].endMs, qint64(120000));
        QVERIFY(!segs[0].toEnd);
        QCOMPARE(segs[0].title, QStringLiteral("Opening"));

        QCOMPARE(int(segs[1].kind), int(SegmentKind::Ending));
        QCOMPARE(segs[1].startMs, qint64(540000));
        QCOMPARE(segs[1].endMs, qint64(600000));  // through the preview, up to the end of the file
        QVERIFY(segs[1].toEnd);
    }

    void aChapterAfterTheEndingKeepsTheSceneOutOfTheSegment()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Episode"), 0), ch(QStringLiteral("Ending"), 500), ch(QStringLiteral("Post-Credits Scene"), 570)};
        const auto segs = detectSkipSegments(chapters, 600000);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs[0].startMs, qint64(500000));
        QCOMPARE(segs[0].endMs, qint64(570000));
        QVERIFY(!segs[0].toEnd);
    }

    void neighbouringChaptersOfTheSameRoleAreMerged()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("OP"), 0), ch(QStringLiteral("OP Part 2"), 45), ch(QStringLiteral("Episode"), 90)};
        const auto segs = detectSkipSegments(chapters, 600000);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs[0].startMs, qint64(0));
        QCOMPARE(segs[0].endMs, qint64(90000));
    }

    void unorderedChaptersAreSortedFirst()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Episode"), 120), ch(QStringLiteral("Opening"), 30), ch(QStringLiteral("Prologue"), 0)};
        const auto segs = detectSkipSegments(chapters, 600000);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs[0].startMs, qint64(30000));
        QCOMPARE(segs[0].endMs, qint64(120000));
    }

    void segmentsTooShortToBeWorthAButtonAreDropped()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Episode"), 0), ch(QStringLiteral("Opening"), 100), ch(QStringLiteral("Story"), 101)};
        QVERIFY(detectSkipSegments(chapters, 600000).isEmpty());
    }

    void aStrayPreviewIsNotASegment()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Episode"), 0), ch(QStringLiteral("Preview"), 570)};
        QVERIFY(detectSkipSegments(chapters, 600000).isEmpty());
    }

    void withoutADurationTheLastSegmentCannotBeMeasured()
    {
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Opening"), 30), ch(QStringLiteral("Episode"), 120), ch(QStringLiteral("Ending"), 540)};
        const auto segs = detectSkipSegments(chapters, 0);
        QCOMPARE(segs.size(), 1);  // the opening is bounded by the next chapter; the ending is not
        QCOMPARE(int(segs[0].kind), int(SegmentKind::Intro));
    }

    void noChaptersNoSegments()
    {
        QVERIFY(detectSkipSegments({}, 600000).isEmpty());
        QVERIFY(detectSkipSegments({ch(QStringLiteral("Chapter 1"), 0), ch(QStringLiteral("Chapter 2"), 300)}, 600000).isEmpty());
    }

    // ------------------------------------------------------------ the button (nothing automatic)

    void theSkipSegmentFollowsThePlayheadAndNothingIsSkippedByDefault()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Button"), 2, &showId);
        auto& pc = env.app->playback();
        QSignalSpy changed(&pc, &PlaybackController::skipSegmentChanged);
        QSignalSpy skipped(&pc, &PlaybackController::segmentSkipped);

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        QCOMPARE(pc.skipSegments().size(), 2);
        QVERIFY(!pc.activeSegment().valid());

        env.backend->tick(10);
        QVERIFY(!pc.activeSegment().valid());
        QCOMPARE(changed.count(), 0);

        env.backend->tick(31);
        QVERIFY(pc.activeSegment().valid());
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Intro));
        QCOMPARE(changed.count(), 1);
        env.backend->tick(60);  // still inside: no new signal
        QCOMPARE(changed.count(), 1);

        env.backend->tick(121);
        QVERIFY(!pc.activeSegment().valid());
        QCOMPARE(changed.count(), 2);
        QVERIFY(!changed.last().at(0).value<SkipSegment>().valid());

        env.backend->tick(545);
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Ending));

        QVERIFY(env.backend->seeks.isEmpty());  // settings are off: it is only offered
        QCOMPARE(skipped.count(), 0);
    }

    void pressingSkipJumpsToTheEndOfTheOpening()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Press"), 1, &showId);
        auto& pc = env.app->playback();
        QSignalSpy skipped(&pc, &PlaybackController::segmentSkipped);

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        pc.skipActiveSegment();  // nothing to skip yet
        QVERIFY(env.backend->seeks.isEmpty());

        env.backend->tick(40);
        pc.skipActiveSegment();
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 120.0);
        QVERIFY(!env.backend->seeks.last().second);  // absolute
        QCOMPARE(skipped.count(), 1);
        QVERIFY(!skipped.first().at(1).toBool());  // by the user
        QCOMPARE(int(skipped.first().at(0).value<SkipSegment>().kind), int(SegmentKind::Intro));
        QVERIFY(!pc.activeSegment().valid());  // the button goes away at once
        QCOMPARE(pc.positionMs(), qint64(120000));
    }

    void theButtonStaysAvailableWhilePaused()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("PausedButton"), 1, &showId);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(40);
        pc.setPaused(true);
        QVERIFY(pc.activeSegment().valid());
        pc.skipActiveSegment();
        QCOMPARE(env.backend->seeks.last().first, 120.0);
        QVERIFY(env.backend->isPaused());  // an opening is skipped where the user left it: paused
        QCOMPARE(pc.state(), PlaybackState::Paused);
    }

    void skippingAnEndingWhilePausedResumesFirstOrTheEngineWouldNeverReportTheEnd()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("PausedEnding"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(560);
        pc.setPaused(true);
        QVERIFY(env.backend->isPaused());

        pc.skipActiveSegment();
        QVERIFY(!env.backend->isPaused());  // resumed before the seek: a paused engine does not reach end-of-file
        QCOMPARE(env.backend->seeks.last().first, 600.0);
        QCOMPARE(pc.state(), PlaybackState::Playing);
        QCOMPARE(completed.count(), 0);  // still the engine's call

        env.backend->end(EndReason::Eof);
        QCOMPARE(completed.count(), 1);
        QTRY_COMPARE(env.backend->loads.size(), 2);
    }

    // ------------------------------------------------------------ automatic skipping

    void autoSkipOpeningSeeksExactlyOnce()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("AutoIntro"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        auto& pc = env.app->playback();
        QSignalSpy skipped(&pc, &PlaybackController::segmentSkipped);

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(29);
        QVERIFY(env.backend->seeks.isEmpty());
        env.backend->tick(30.5);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 120.0);
        QCOMPARE(skipped.count(), 1);
        QVERIFY(skipped.first().at(1).toBool());  // automatic

        // the engine keeps reporting positions from before the seek landed
        env.backend->tick(30.8);
        env.backend->tick(31.2);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(skipped.count(), 1);

        env.backend->tick(120.2);  // arrived
        QCOMPARE(env.backend->seeks.size(), 1);
        QVERIFY(!pc.activeSegment().valid());
    }

    void seekingBackIntoASkippedOpeningLeavesItAlone()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("SeekBack"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(30.5);
        QCOMPARE(env.backend->seeks.size(), 1);

        pc.seekToMs(50000);  // the user wants to see the opening after all
        QCOMPARE(env.backend->seeks.size(), 2);
        QCOMPARE(env.backend->seeks.last().first, 50.0);
        env.backend->tick(50.4);
        env.backend->tick(51.0);
        QCOMPARE(env.backend->seeks.size(), 2);  // nothing skipped it again
        QVERIFY(pc.activeSegment().valid());     // but the button is there if they change their mind

        pc.seekByMs(-10000);
        env.backend->tick(41);
        QCOMPARE(env.backend->seeks.size(), 3);  // only the user's own seek
        QVERIFY(env.backend->seeks.last().second);
    }

    void seekingIntoAnOpeningOnPurposeIsNotAutoSkipped()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Purpose"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        env.app->settings().setAutoSkipEnding(true);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));

        pc.seekToMs(60000);  // jumps straight into the opening, before playback ever reached it
        env.backend->tick(60.5);
        QCOMPARE(env.backend->seeks.size(), 1);
        QVERIFY(pc.activeSegment().valid());

        pc.seekToMs(550000);  // and into the ending
        env.backend->tick(550.5);
        QCOMPARE(env.backend->seeks.size(), 2);
        QCOMPARE(env.backend->seeks.last().first, 550.0);
    }

    void autoSkipWaitsForPlaybackNotPause()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("NotPaused"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));

        env.backend->tick(29);
        pc.setPaused(true);
        env.backend->tick(40);  // e.g. a frame step or a scrub while paused
        QVERIFY(env.backend->seeks.isEmpty());
        QVERIFY(pc.activeSegment().valid());

        pc.setPaused(false);
        env.backend->tick(40.3);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 120.0);
    }

    void eachSettingOnlyAffectsItsOwnKindOfSegment()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Separate"), 1, &showId);
        auto& pc = env.app->playback();

        env.app->settings().setAutoSkipEnding(true);  // opening stays manual
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(40);
        QVERIFY(env.backend->seeks.isEmpty());
        QVERIFY(pc.activeSegment().valid());

        env.app->settings().setAutoSkipEnding(false);
        env.app->settings().setAutoSkipIntro(true);  // now the other way round
        env.backend->tick(41);                       // still inside the opening, never skipped yet: it goes now
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 120.0);
        env.backend->tick(121);
        env.backend->tick(545);  // the ending stays manual
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Ending));
    }

    void anOpeningAtTheVeryStartIsSkippedAsSoonAsThePlaybackStarts()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("ColdStart"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("OP"), 0), ch(QStringLiteral("Episode"), 90)};
        QVERIFY(startWith(env, showId, eps[0], chapters));
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 90.0);
    }

    void resumingInsideAnOpeningSkipsTheRestOfIt()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("ResumeInside"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        env.app->library().saveProgress(eps[0].file.id, 45000, 600000);
        QVERIFY(startWith(env, showId, eps[0], typicalChapters(), 600, StartMode::Resume));
        QCOMPARE(env.backend->lastLoad().startSeconds, 45.0);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 120.0);
    }

    void aNewEpisodeGetsItsOwnSkips()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("EveryEpisode"), 2, &showId);
        env.app->settings().setAutoSkipIntro(true);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(31);
        QCOMPARE(env.backend->seeks.size(), 1);

        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QVERIFY(pc.skipSegments().isEmpty());  // the old file's segments are gone until the new file says otherwise
        QVERIFY(!pc.activeSegment().valid());
        startAndLoad(env.backend);
        QCOMPARE(pc.skipSegments().size(), 2);
        env.backend->tick(31);
        QCOMPARE(env.backend->seeks.size(), 2);  // skipped again in the second episode
    }

    void filesWithoutChaptersOfferNothing()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Plain"), 1, &showId);
        env.app->settings().setAutoSkipIntro(true);
        env.app->settings().setAutoSkipEnding(true);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        for (double t : {5.0, 40.0, 300.0, 590.0}) env.backend->tick(t);
        QVERIFY(pc.skipSegments().isEmpty());
        QVERIFY(!pc.activeSegment().valid());
        QVERIFY(env.backend->seeks.isEmpty());
    }

    void chaptersThatAppearAfterTheLoadAreUsed()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Late"), 1, &showId);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        QVERIFY(pc.skipSegments().isEmpty());
        env.backend->tick(40);
        env.backend->setChapters(typicalChapters());  // the engine finished reading them while playing
        QCOMPARE(pc.skipSegments().size(), 2);
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Intro));
    }

    // ------------------------------------------------------------ endings and the end of the file

    void skippingAnEndingThatRunsToTheEndFinishesTheEpisodeOnlyThroughEndOfFile()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("EndingSkip"), 2, &showId);
        env.app->settings().setAutoSkipEnding(true);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy skipped(&pc, &PlaybackController::segmentSkipped);

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(541);

        QCOMPARE(skipped.count(), 1);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 600.0);  // the very end of the file
        // the skip itself completes nothing and starts nothing...
        QCOMPARE(completed.count(), 0);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.backend->loads.size(), 1);
        QCOMPARE(pc.state(), PlaybackState::Playing);

        // ...that is the engine's job: it reports the natural end, and the usual path takes over
        env.backend->end(EndReason::Eof);
        QCOMPARE(completed.count(), 1);
        QVERIFY(env.app->library().progress(eps[0].file.id).completed);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        QCOMPARE(pc.currentEntry().mediaFileId, eps[1].file.id);
    }

    void aManualEndingSkipAlsoGoesThroughEndOfFile()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("ManualEnding"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(560);
        pc.skipActiveSegment();
        QCOMPARE(env.backend->seeks.last().first, 600.0);
        QCOMPARE(completed.count(), 0);
        env.backend->end(EndReason::Eof);
        QCOMPARE(completed.count(), 1);
    }

    void skippingTheEndingOfTheLastEpisodeFinishesTheQueue()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("LastEnding"), 2, &showId);
        env.app->settings().setAutoSkipEnding(true);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);

        QVERIFY(startWith(env, showId, eps[1], typicalChapters()));
        env.backend->tick(541);
        QCOMPARE(finished.count(), 0);  // still waiting for the engine
        env.backend->end(EndReason::Eof);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(pending.count(), 0);
        QCOMPARE(pc.state(), PlaybackState::Idle);
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    void skippingTheEndingWithAutoPlayOffDoesNotAdvance()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("NoAutoplay"), 2, &showId);
        env.app->settings().setAutoSkipEnding(true);
        env.app->settings().setAutoPlayNext(false);
        auto& pc = env.app->playback();
        QSignalSpy ended(&pc, &PlaybackController::playbackEndedWithoutAdvance);

        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(541);
        env.backend->end(EndReason::Eof);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.first().at(1).value<QueueEntry>().mediaFileId, eps[1].file.id);  // offered, not started
        QTest::qWait(100);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    void anEndingFollowedByAPostCreditsSceneJumpsOverItAndKeepsPlaying()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("PostCredits"), 2, &showId);
        env.app->settings().setAutoSkipEnding(true);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        const QVector<ChapterInfo> chapters = {ch(QStringLiteral("Episode"), 0), ch(QStringLiteral("Ending"), 500), ch(QStringLiteral("Post-Credits Scene"), 570)};

        QVERIFY(startWith(env, showId, eps[0], chapters));
        env.backend->tick(501);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 570.0);  // to the scene, not to the end
        env.backend->tick(570.2);
        QCOMPARE(pc.state(), PlaybackState::Playing);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    void aSkippedEndingThenAStopNeverCompletesAnything()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("StopAfterSkip"), 2, &showId);
        env.app->settings().setAutoSkipEnding(true);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(541);  // skip to the end requested...
        pc.stop();               // ...but the user stops before the engine reports anything
        env.backend->end(EndReason::Stop);
        env.backend->end(EndReason::Eof);  // stale
        QTest::qWait(100);
        QCOMPARE(completed.count(), 0);
        QVERIFY(!env.app->library().progress(eps[0].file.id).completed);
        QCOMPARE(env.backend->loads.size(), 1);
    }

    void nothingIsOfferedOrSkippedDuringTheNextEpisodeCountdown()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Countdown"), 2, &showId);
        env.app->settings().setAutoSkipIntro(true);
        env.app->settings().setAutoSkipEnding(true);
        env.app->settings().setAutoPlayCountdownSeconds(5);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->end(EndReason::Eof);
        QCOMPARE(pc.state(), PlaybackState::AdvancePending);
        QVERIFY(!pc.activeSegment().valid());
        const int seeks = int(env.backend->seeks.size());
        env.backend->tick(35);  // a late position event of the finished file
        pc.skipActiveSegment();
        QCOMPARE(int(env.backend->seeks.size()), seeks);
        QCOMPARE(pc.state(), PlaybackState::AdvancePending);
    }
};

QTEST_GUILESS_MAIN(TstSkip)
#include "tst_skip.moc"
