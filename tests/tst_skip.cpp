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

// Both automatic options default to on; the tests of the button and of manual skipping turn them off.
void manualOnly(Env& env)
{
    env.app->settings().setAutoSkipIntro(false);
    env.app->settings().setAutoSkipEnding(false);
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

    void theSkipSegmentFollowsThePlayheadAndNothingIsSkippedWhenTheOptionsAreOff()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Button"), 2, &showId);
        manualOnly(env);
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
        manualOnly(env);
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
        manualOnly(env);
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
        manualOnly(env);
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

        env.app->settings().setAutoSkipIntro(false);  // opening stays manual
        env.app->settings().setAutoSkipEnding(true);
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
        manualOnly(env);
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
        manualOnly(env);
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

    // ------------------------------------------------------------ defaults

    void bothAutomaticOptionsAreOnByDefaultAndTheUsersChoiceSticks()
    {
        Env env;
        QVERIFY(env.app->settings().autoSkipIntro());
        QVERIFY(env.app->settings().autoSkipEnding());
        env.app->settings().setAutoSkipEnding(false);
        env.restart();
        QVERIFY(env.app->settings().autoSkipIntro());
        QVERIFY(!env.app->settings().autoSkipEnding());
    }

    // ------------------------------------------------------------ season marks: segments

    void anEndingMarkIsTheLastStretchOfEveryFileWhateverItsLength()
    {
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        const auto a = skipSegmentsFor({}, marks, 600000);
        QCOMPARE(a.size(), 1);
        QCOMPARE(int(a[0].kind), int(SegmentKind::Ending));
        QCOMPARE(a[0].startMs, qint64(570000));
        QCOMPARE(a[0].endMs, qint64(600000));
        QVERIFY(a[0].toEnd);
        const auto b = skipSegmentsFor({}, marks, 1325909);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].startMs, qint64(1295909));
        QVERIFY(b[0].toEnd);
    }

    void anOpeningMarkIsAnAbsoluteStretchOfTheFile()
    {
        SeasonSkipMarks marks;
        marks.introStartMs = 45000;
        marks.introEndMs = 110000;
        const auto segs = skipSegmentsFor({}, marks, 600000);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(int(segs[0].kind), int(SegmentKind::Intro));
        QCOMPARE(segs[0].startMs, qint64(45000));
        QCOMPARE(segs[0].endMs, qint64(110000));
        QVERIFY(!segs[0].toEnd);
        QCOMPARE(skipSegmentsFor({}, marks, 0).size(), 1);  // an opening does not need the duration
    }

    void marksThatDoNotFitAFileAreIgnored()
    {
        SeasonSkipMarks ending;
        ending.endingFromEndMs = 30000;
        QVERIFY(skipSegmentsFor({}, ending, 20000).isEmpty());  // the "ending" would be the whole file
        QVERIFY(skipSegmentsFor({}, ending, 30000).isEmpty());
        QVERIFY(skipSegmentsFor({}, ending, 0).isEmpty());      // unknown duration: cannot place it
        SeasonSkipMarks tiny;
        tiny.endingFromEndMs = 1000;
        QVERIFY(skipSegmentsFor({}, tiny, 600000).isEmpty());

        SeasonSkipMarks intro;
        intro.introStartMs = 45000;
        intro.introEndMs = 110000;
        QVERIFY(skipSegmentsFor({}, intro, 100000).isEmpty());  // the file ends before the opening does
        SeasonSkipMarks brief;
        brief.introStartMs = 5000;
        brief.introEndMs = 6000;
        QVERIFY(skipSegmentsFor({}, brief, 600000).isEmpty());
    }

    void chaptersWinOverTheMarksKindByKind()
    {
        SeasonSkipMarks marks;
        marks.introStartMs = 5000;
        marks.introEndMs = 50000;
        marks.endingFromEndMs = 20000;
        // chapters for both: the marks are not used at all
        const auto both = skipSegmentsFor(typicalChapters(), marks, 600000);
        QCOMPARE(both.size(), 2);
        QCOMPARE(both[0].startMs, qint64(30000));
        QCOMPARE(both[1].startMs, qint64(540000));

        // chapters for the ending only: the opening comes from the mark
        const QVector<ChapterInfo> endingOnly = {ch(QStringLiteral("Episode"), 0), ch(QStringLiteral("Credits"), 560)};
        const auto mixed = skipSegmentsFor(endingOnly, marks, 600000);
        QCOMPARE(mixed.size(), 2);
        QCOMPARE(int(mixed[0].kind), int(SegmentKind::Intro));
        QCOMPARE(mixed[0].startMs, qint64(5000));
        QCOMPARE(int(mixed[1].kind), int(SegmentKind::Ending));
        QCOMPARE(mixed[1].startMs, qint64(560000));  // the chapter's, not 580000 from the mark
    }

    void aSegmentKeepsItsKeyWhenTheDurationIsRefined()
    {
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        const auto a = skipSegmentsFor({}, marks, 600000);
        const auto b = skipSegmentsFor({}, marks, 600400);
        QVERIFY(a[0].startMs != b[0].startMs);
        QCOMPARE(a[0].key(), b[0].key());
        QVERIFY(a[0].key() != SkipSegment{}.key());  // not the key of an opening at 0
    }

    // ------------------------------------------------------------ season marks: storage

    void marksArePerSeasonSurviveARestartAndGoWithTheSeason()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Stored"), 2, &showId);
        auto& lib = env.app->library();
        const Id season2 = lib.addSeason(showId, QStringLiteral("Season 2"));
        QVERIFY(season2 != kInvalidId);

        QVERIFY(!lib.seasonSkipMarks(eps[0].seasonId).any());
        QSignalSpy changed(&lib, &LibraryManager::skipMarksChanged);
        SeasonSkipMarks marks;
        marks.introStartMs = 45000;
        marks.introEndMs = 110000;
        marks.endingFromEndMs = 32000;
        QVERIFY(lib.setSeasonSkipMarks(eps[0].seasonId, marks));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toLongLong(), eps[0].seasonId);
        SeasonSkipMarks other;
        other.endingFromEndMs = 5000;
        QVERIFY(lib.setSeasonSkipMarks(season2, other));

        env.restart();
        auto& lib2 = env.app->library();
        const SeasonSkipMarks back = lib2.seasonSkipMarks(eps[0].seasonId);
        QCOMPARE(back.introStartMs, qint64(45000));
        QCOMPARE(back.introEndMs, qint64(110000));
        QCOMPARE(back.endingFromEndMs, qint64(32000));
        QCOMPARE(lib2.seasonSkipMarks(season2).endingFromEndMs, qint64(5000));  // each season has its own
        QVERIFY(!lib2.setSeasonSkipMarks(kInvalidId, marks));

        QVERIFY(lib2.deleteSeason(season2));
        QVERIFY(!lib2.seasonSkipMarks(season2).any());  // gone with the season

        QVERIFY(lib2.setSeasonSkipMarks(eps[0].seasonId, SeasonSkipMarks{}));  // nothing set = removed
        QVERIFY(!lib2.seasonSkipMarks(eps[0].seasonId).any());
    }

    void aHalfSetOpeningIsNotStored()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Half"), 1, &showId);
        SeasonSkipMarks marks;
        marks.introStartMs = 45000;  // no end
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        const SeasonSkipMarks back = env.app->library().seasonSkipMarks(eps[0].seasonId);
        QVERIFY(!back.hasIntro());
        QCOMPARE(back.introStartMs, qint64(-1));
        QCOMPARE(back.endingFromEndMs, qint64(30000));
    }

    // ------------------------------------------------------------ season marks: playback

    void aSeasonEndingMarkSkipsEveryEpisodeWithoutChaptersWhateverItsLength()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Marked"), 3, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        auto& pc = env.app->playback();
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);

        QVERIFY(startWith(env, showId, eps[0], {}));
        QCOMPARE(pc.skipSegments().size(), 1);
        env.backend->tick(560);  // before the ending
        QVERIFY(env.backend->seeks.isEmpty());
        env.backend->tick(571);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 600.0);
        QCOMPARE(completed.count(), 0);  // the engine's end-of-file does that
        env.backend->end(EndReason::Eof);
        QCOMPARE(completed.count(), 1);

        // the next episode of the season is longer: the same mark, "the last 30 s", fits it
        QTRY_COMPARE(env.backend->loads.size(), 2);
        startAndLoad(env.backend, 1300);
        env.backend->tick(1260);
        QCOMPARE(env.backend->seeks.size(), 1);
        env.backend->tick(1275);
        QCOMPARE(env.backend->seeks.size(), 2);
        QCOMPARE(env.backend->seeks.last().first, 1300.0);
    }

    void aSeasonOpeningMarkSkipsEveryEpisodeWithoutChapters()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("MarkedIntro"), 1, &showId);
        SeasonSkipMarks marks;
        marks.introStartMs = 45000;
        marks.introEndMs = 110000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        QVERIFY(startWith(env, showId, eps[0], {}));
        env.backend->tick(44);
        QVERIFY(env.backend->seeks.isEmpty());
        env.backend->tick(46);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 110.0);
    }

    void theMarksOfOneSeasonDoNotTouchAnotherSeason()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("TwoSeasons"), 2, &showId);
        const Id season2 = env.app->library().addSeason(showId, QStringLiteral("Season 2"));
        QVERIFY(env.app->library().moveEpisode(eps[1].id, season2, 0));
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));

        QVERIFY(startWith(env, showId, eps[1], {}));  // an episode of the other season
        QVERIFY(env.app->playback().skipSegments().isEmpty());
        env.backend->tick(590);
        QVERIFY(env.backend->seeks.isEmpty());
    }

    void chapterSegmentsWinOverTheSeasonMarkOnTheFilesThatHaveThem()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("ChaptersWin"), 1, &showId);
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 10000;  // would start at 590 s
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        QVERIFY(startWith(env, showId, eps[0], typicalChapters()));
        env.backend->tick(541);  // the file's own "Ending" chapter starts at 540
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 600.0);
    }

    void marksSetWhileWatchingApplyAtOnceToTheEpisodeOnScreen()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Live"), 1, &showId);
        manualOnly(env);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        QVERIFY(pc.skipSegments().isEmpty());
        env.backend->tick(580);
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));  // e.g. from somewhere else
        QCOMPARE(pc.skipSegments().size(), 1);
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Ending));
    }

    void markingTheEndingWhileWatchingDoesNotSkipItOnTheSpot()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("MarkHere"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);  // automatic skipping is on, as by default
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        QVERIFY(pc.canMarkSkips());
        env.backend->tick(580);

        QVERIFY(pc.markEndingStartHere());
        QCOMPARE(pc.skipMarks().endingFromEndMs, qint64(20000));
        QCOMPARE(env.app->library().seasonSkipMarks(eps[0].seasonId).endingFromEndMs, qint64(20000));
        // the playhead is inside the ending it just marked: it must stay put, but the button is offered
        QVERIFY(env.backend->seeks.isEmpty());
        QVERIFY(pc.activeSegment().valid());
        QCOMPARE(int(pc.activeSegment().kind), int(SegmentKind::Ending));
        env.backend->tick(582);
        QVERIFY(env.backend->seeks.isEmpty());

        // the next episode uses it by itself
        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        startAndLoad(env.backend);
        env.backend->tick(585);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 600.0);
    }

    void markingTheOpeningTakesAStartThenAnEndAfterIt()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("MarkIntro"), 2, &showId);
        env.app->settings().setAutoPlayCountdownSeconds(0);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));

        env.backend->tick(45);
        QVERIFY(!pc.markIntroEndHere());  // no start yet
        QVERIFY(pc.markIntroStartHere());
        QCOMPARE(pc.pendingIntroStartMs(), qint64(45000));
        env.backend->tick(46);
        QVERIFY(!pc.markIntroEndHere());  // 1 s is not an opening
        QVERIFY(!pc.skipMarks().hasIntro());
        env.backend->tick(110);
        QVERIFY(pc.markIntroEndHere());
        QCOMPARE(pc.skipMarks().introStartMs, qint64(45000));
        QCOMPARE(pc.skipMarks().introEndMs, qint64(110000));
        QCOMPARE(pc.pendingIntroStartMs(), qint64(-1));
        QVERIFY(env.backend->seeks.isEmpty());

        env.backend->end(EndReason::Eof);
        QTRY_COMPARE(env.backend->loads.size(), 2);
        startAndLoad(env.backend);
        env.backend->tick(46);
        QCOMPARE(env.backend->seeks.size(), 1);
        QCOMPARE(env.backend->seeks.last().first, 110.0);
    }

    void marksCanBeClearedOneKindAtATime()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Clear"), 1, &showId);
        manualOnly(env);
        auto& pc = env.app->playback();
        SeasonSkipMarks marks;
        marks.introStartMs = 45000;
        marks.introEndMs = 110000;
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        QVERIFY(startWith(env, showId, eps[0], {}));
        QCOMPARE(pc.skipSegments().size(), 2);

        QVERIFY(pc.clearEndingMark());
        QCOMPARE(pc.skipSegments().size(), 1);
        QCOMPARE(int(pc.skipSegments()[0].kind), int(SegmentKind::Intro));
        QVERIFY(pc.clearIntroMark());
        QVERIFY(pc.skipSegments().isEmpty());
        QVERIFY(!env.app->library().seasonSkipMarks(eps[0].seasonId).any());
    }

    void aMarkUsesThePositionItWasAskedAtNotWhereThePlayheadHasMovedTo()
    {
        // the marks menu stays open while the video plays on: it marks where it was opened
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("MenuOpen"), 1, &showId);
        manualOnly(env);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        env.backend->tick(580);
        env.backend->tick(590);  // ten seconds pass while the menu is open
        QVERIFY(pc.markEndingStartHere(580000));
        QCOMPARE(pc.skipMarks().endingFromEndMs, qint64(20000));

        QVERIFY(pc.markIntroStartHere(45000));
        QCOMPARE(pc.pendingIntroStartMs(), qint64(45000));
        QVERIFY(pc.markIntroEndHere(110000));
        QCOMPARE(pc.skipMarks().introEndMs, qint64(110000));
    }

    void anEndingMarkTooCloseToTheEndIsRefused()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("TooClose"), 1, &showId);
        manualOnly(env);
        auto& pc = env.app->playback();
        QVERIFY(startWith(env, showId, eps[0], {}));
        env.backend->tick(599);
        QVERIFY(!pc.markEndingStartHere());
        QVERIFY(!env.app->library().seasonSkipMarks(eps[0].seasonId).any());
    }

    void moviesHaveNoSeasonSoTheyCannotBeMarked()
    {
        Env env;
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("mv/solo.mkv"))});
        const Movie movie = env.app->library().movies().first();
        auto& pc = env.app->playback();
        QVERIFY(pc.play({ContextType::Movie, movie.id}, movie.file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        env.backend->tick(580);
        QVERIFY(!pc.canMarkSkips());
        QVERIFY(!pc.markEndingStartHere());
        QVERIFY(!pc.markIntroStartHere());
    }

    void aMarkedEndingIsNotSkippedTwiceWhenTheDurationIsRefinedAfterwards()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Refined"), 1, &showId);
        SeasonSkipMarks marks;
        marks.endingFromEndMs = 30000;
        QVERIFY(env.app->library().setSeasonSkipMarks(eps[0].seasonId, marks));
        QVERIFY(startWith(env, showId, eps[0], {}));
        env.backend->tick(575);
        QCOMPARE(env.backend->seeks.size(), 1);

        emit env.backend->durationChanged(600.4);  // the engine settles on a slightly different duration
        env.backend->tick(575.5);                  // a stale position, inside the (moved) ending again
        QCOMPARE(env.backend->seeks.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstSkip)
#include "tst_skip.moc"
