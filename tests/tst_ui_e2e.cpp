// End-to-end test of the acceptance scenario, driven through the real main window:
// real libmpv playback, real media files, real widgets (clicks via QTest).
//
// It needs a desktop session (a window appears for a few seconds) and the
// generated clips from scripts/make_test_videos.py. It renders with Qt's
// software OpenGL by default so it behaves the same on every machine; set
// LOCALFLIX_E2E_HARDWARE_GL=1 to use the GPU. Screenshots of each step (rendered
// by Qt itself) are written to <build>/e2e-shots for review.
#include "ApplicationController.h"
#include "CenterCard.h"
#include "EpisodeManagerView.h"
#include "MainWindow.h"
#include "MpvWidget.h"
#include "MpvBackend.h"
#include "PlayerView.h"
#include "SeekSlider.h"
#include "Format.h"
#include "Theme.h"
#include "TestSupport.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QToolButton>
#include <QTreeWidget>

#include <functional>
#include <memory>

using namespace lf;
using namespace lf::ui;
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
        QTest::qWait(20);
    }
    return true;
}

QString shotDir()
{
    const QString dir = QStringLiteral(LOCALFLIX_SHOT_DIR);
    QDir().mkpath(dir);
    return dir;
}

// One "run" of the application: controller + real libmpv backend + main window.
struct Session {
    std::unique_ptr<ApplicationController> app;
    MpvBackend* engine = nullptr;
    std::unique_ptr<MainWindow> window;

    bool start(const QString& dataDir)
    {
        if (window) closeLikeAUser();
        MpvBackend::Options opts;
        opts.video = true;
        opts.audio = false;  // no sound from the test machine's speakers
        auto backend = std::make_unique<MpvBackend>(opts);
        if (!backend->isValid()) return false;
        engine = backend.get();
        app = std::make_unique<ApplicationController>(dataDir, std::move(backend));
        QString err;
        if (!app->initialize(&err)) {
            qWarning() << err;
            return false;
        }
        window = std::make_unique<MainWindow>(*app, engine);
        window->resize(1280, 800);
        window->show();
        QTest::qWait(300);
        return true;
    }

    // Closing the window is what a user does: progress must be saved by that alone.
    void closeLikeAUser()
    {
        window->close();
        window.reset();      // video widget (render context) first,
        app->shutdown();     // then the engine
        app.reset();
        engine = nullptr;
    }

    template <class W>
    W* find(const char* name) const
    {
        return window->findChild<W*>(QString::fromLatin1(name));
    }

    void shot(const QString& name) const
    {
        QTest::qWait(250);
        window->grab().save(QDir(shotDir()).filePath(name + QStringLiteral(".png")));
    }
};

void clickButton(QWidget* w)
{
    QVERIFY(w);
    QVERIFY(w->isEnabled());
    QTest::mouseClick(w, Qt::LeftButton);
}

// Answers the next QInputDialog (opened by the click that follows) with `text`.
void answerNextInputDialog(const QString& text)
{
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [text, poll] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* d = qobject_cast<QInputDialog*>(w); d && d->isVisible()) {
                d->setTextValue(text);
                d->accept();
                return;
            }
        }
        QTimer::singleShot(50, *poll);
    };
    QTimer::singleShot(50, *poll);
}


// Keeps accepting any visible dialog (e.g. the import report) until the returned flag is cleared.
std::shared_ptr<bool> autoDismissDialogs()
{
    auto active = std::make_shared<bool>(true);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [active, poll] {
        if (!*active) return;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* d = qobject_cast<QDialog*>(w); d && d->isVisible()) d->accept();
        QTimer::singleShot(100, *poll);
    };
    QTimer::singleShot(100, *poll);
    return active;
}


// Delivers a mouse-move straight to `w`. (QTest::mouseMove drives the real OS cursor, which makes a test
// depend on where the physical pointer happens to be and what is on top of the window.)
void moveMouse(QWidget* w, const QPoint& local)
{
    QMouseEvent ev(QEvent::MouseMove, QPointF(local), QPointF(w->mapToGlobal(local)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

QTreeWidgetItem* episodeItemByTitle(QTreeWidget* tree, const QString& title)
{
    for (int s = 0; s < tree->topLevelItemCount(); ++s)
        for (int c = 0; c < tree->topLevelItem(s)->childCount(); ++c)
            if (tree->topLevelItem(s)->child(c)->text(2) == title) return tree->topLevelItem(s)->child(c);
    return nullptr;
}

QStringList episodeTitlesInTree(QTreeWidget* tree)
{
    QStringList out;
    for (int s = 0; s < tree->topLevelItemCount(); ++s)
        for (int c = 0; c < tree->topLevelItem(s)->childCount(); ++c) out << tree->topLevelItem(s)->child(c)->text(2);
    return out;
}

void selectItem(QTreeWidget* tree, QTreeWidgetItem* item)
{
    tree->clearSelection();
    tree->setCurrentItem(item);
    item->setSelected(true);
}

}  // namespace

class TstUiE2e : public QObject {
    Q_OBJECT

private:
    // Work inside the (git-ignored) testdata folder rather than the user's %TEMP%, so screenshots and
    // logs never contain a personal profile path.
    QTemporaryDir m_dir{QDir::cleanPath(QStringLiteral(LOCALFLIX_TESTDATA_DIR "/../e2e-XXXXXX"))};
    QString m_dataDir;
    QString m_relinkCopy;  // private copy of a clip we are allowed to move
    Id m_showId = kInvalidId;
    Session s;

    QStringList mediaFiles() const
    {
        // five spec-style clips + the long one; the "episode_01.avi" slot is a private copy
        return {videoPath(QStringLiteral("video_27.mkv")), videoPath(QStringLiteral("downloaded_episode_04.mp4")),
                videoPath(QStringLiteral("anime_final.webm")), m_relinkCopy, videoPath(QStringLiteral("S01E05 - The Reunion.mov")),
                videoPath(QStringLiteral("long_resume_test.mkv"))};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_dataDir = QDir(m_dir.path()).filePath(QStringLiteral("data"));
        if (!QFileInfo::exists(videoPath(QStringLiteral("long_resume_test.mkv"))))
            QSKIP("Test videos missing: run `python scripts/make_test_videos.py` first.");
        m_relinkCopy = QDir(m_dir.path()).filePath(QStringLiteral("private/episode_01.avi"));
        QDir().mkpath(QFileInfo(m_relinkCopy).absolutePath());
        QVERIFY(QFile::copy(videoPath(QStringLiteral("episode_01.avi")), m_relinkCopy));
        m_relinkCopy = LibraryManager::normalizePath(m_relinkCopy);
    }

    // 1-3: launch, create a show, import six local files into a season
    void step1_createShowThroughTheUi()
    {
        QVERIFY(s.start(m_dataDir));
        s.shot(QStringLiteral("01_home_empty"));
        clickButton(s.find<QToolButton>("nav_tvshows"));
        auto* create = s.find<QPushButton>("NewShowButton");
        QVERIFY(create);
        answerNextInputDialog(QStringLiteral("Acceptance Show"));
        clickButton(create);
        QVERIFY(waitFor([&] { return !s.app->library().showSummaries().isEmpty(); }, 12000));
        m_showId = s.app->library().showSummaries().first().show.id;
        QCOMPARE(s.app->library().show(m_showId)->title, QStringLiteral("Acceptance Show"));
        // the show page opens by itself so files can be added
        QVERIFY(waitFor([&] { auto* t = s.find<QTreeWidget>("EpisodeTree"); return t && t->isVisible(); }, 12000));
        s.shot(QStringLiteral("02_new_show_page"));
    }

    void step2_importSixLocalFilesIntoASeason()
    {
        QSignalSpy finished(&s.app->importer(), &MediaImportService::importFinished);
        const Id season = s.app->library().seasons(m_showId).first().id;
        s.app->importer().importEpisodesAsync(m_showId, season, mediaFiles(), PlanOptions{false, false});
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.first().at(1).value<ImportReport>().added, 6);
        auto* tree = s.find<QTreeWidget>("EpisodeTree");
        QVERIFY(waitFor([&] { return tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 6; }, 12000));
        // the user's files were referenced in place, never copied or moved
        for (const QString& f : mediaFiles()) QVERIFY(QFileInfo::exists(f));
        s.shot(QStringLiteral("03_six_episodes_imported"));
    }

    // 5: change the playback order with the on-screen buttons
    void step3_reorderWithTheMoveButtons()
    {
        auto* tree = s.find<QTreeWidget>("EpisodeTree");
        QStringList before = episodeTitlesInTree(tree);
        // put the long clip first, then check the exact resulting order in the database
        QTreeWidgetItem* item = nullptr;
        for (const QString& t : before)
            if (t.contains(QStringLiteral("Long"), Qt::CaseInsensitive)) item = episodeItemByTitle(tree, t);
        QVERIFY(item);
        const QString longTitle = item->text(2);
        selectItem(tree, item);
        for (int i = 0; i < 10 && episodeTitlesInTree(tree).first() != longTitle; ++i) {
            const int indexBefore = int(episodeTitlesInTree(tree).indexOf(longTitle));
            clickButton(s.find<QToolButton>("MoveUpButton"));
            QVERIFY(waitFor([&] { return episodeTitlesInTree(tree).indexOf(longTitle) == indexBefore - 1; }, 12000));
            item = episodeItemByTitle(tree, longTitle);
            selectItem(tree, item);
        }
        QCOMPARE(episodeTitlesInTree(tree).first(), longTitle);

        // and the database holds exactly what the tree shows
        QStringList dbOrder;
        for (const Episode& e : s.app->library().episodes(m_showId)) dbOrder << e.title;
        QCOMPARE(dbOrder, episodeTitlesInTree(tree));
        QVERIFY(dbOrder != before);
        s.shot(QStringLiteral("04_reordered"));
    }

    // 6-8: start the first episode, seek to an arbitrary position, close the application
    void step4_startSeekAndClose()
    {
        auto* cont = s.find<QPushButton>("ContinueButton");
        QVERIFY(cont->isEnabled());
        QVERIFY2(cont->text().contains(QStringLiteral("Start watching")), qPrintable(cont->text()));
        QSignalSpy now(&s.app->playback(), &PlaybackController::nowPlaying);
        clickButton(cont);
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 20000));
        auto* player = s.window->findChild<PlayerView*>();
        QVERIFY(player && player->isVisible());
        QVERIFY(waitFor([&] { return s.app->playback().positionMs() >= 1000; }, 15000));
        QCOMPARE(now.first().at(0).value<QueueEntry>().path.contains(QStringLiteral("long_resume_test")), true);
        s.shot(QStringLiteral("05_playing_first_episode"));

        // click on the seek bar at 30%
        auto* seek = s.find<SeekSlider>("SeekSlider");
        QVERIFY(seek && seek->isVisible());
        const QPoint target(int(seek->width() * 0.30), seek->height() / 2);
        QTest::mouseClick(seek, Qt::LeftButton, Qt::NoModifier, target);
        QVERIFY(waitFor([&] { return s.engine->positionSeconds() >= 11.0 && s.engine->positionSeconds() < 15.0; }, 12000));
        s.shot(QStringLiteral("06_after_seek"));

        // pause button: position is saved immediately
        clickButton(s.find<QToolButton>("PlayPauseButton"));
        QVERIFY(waitFor([&] { return s.app->playback().state() == PlaybackState::Paused; }, 12000));
        const Id media = s.app->playback().currentEntry().mediaFileId;
        QVERIFY2(s.app->library().progress(media).positionMs >= 11000, "pause did not persist the position");

        clickButton(s.find<QToolButton>("PlayPauseButton"));  // resume playing
        QVERIFY(waitFor([&] { return s.app->playback().state() == PlaybackState::Playing; }, 12000));
        QTest::qWait(1200);
        s.closeLikeAUser();  // closing the window alone must preserve progress
    }

    // 9-10: reopen; Continue Watching offers the right file at the saved timestamp
    void step5_reopenAndResumeFromContinueWatching()
    {
        QVERIFY(s.start(m_dataDir));
        const auto items = s.app->library().continueWatching();
        QCOMPARE(items.size(), 1);
        QVERIFY2(items[0].positionMs >= 11000 && items[0].positionMs < 20000, qPrintable(QString::number(items[0].positionMs)));
        const qint64 saved = items[0].positionMs;

        // the Home screen shows the card with title, episode, percentage and a Resume button
        auto* resume = s.find<QPushButton>("ResumeButton");
        QVERIFY(waitFor([&] { return s.find<QPushButton>("ResumeButton") != nullptr; }, 12000));
        resume = s.find<QPushButton>("ResumeButton");
        QVERIFY(resume->isVisible());
        const auto labels = s.window->findChildren<QLabel*>();
        bool sawPercent = false;
        bool sawTitle = false;
        for (QLabel* l : labels) {
            if (l->isVisible() && l->text().contains(QStringLiteral("% watched"))) sawPercent = true;
            if (l->isVisible() && l->text() == QStringLiteral("Acceptance Show")) sawTitle = true;
        }
        QVERIFY2(sawPercent, "Continue Watching card has no percentage");
        QVERIFY2(sawTitle, "Continue Watching card has no show title");
        s.shot(QStringLiteral("07_home_continue_watching"));

        QSignalSpy positions(s.engine, &IPlaybackBackend::positionChanged);
        clickButton(resume);
        QVERIFY(waitFor([&] { return positions.count() >= 1; }, 20000));
        const double first = positions.first().at(0).toDouble();
        QVERIFY2(qAbs(first - saved / 1000.0) < 1.5, qPrintable(QStringLiteral("resumed at %1, saved %2").arg(first).arg(saved / 1000.0)));
        QVERIFY(first > 10.0);  // not from zero
        s.shot(QStringLiteral("08_resumed"));
    }

    // 11-12: let it end naturally (seeking near the end first); the next episode starts by itself
    void step6_endNaturallyAndAdvance()
    {
        auto& pc = s.app->playback();
        s.app->settings().setAutoPlayCountdownSeconds(3);
        const Episode first = s.app->library().episodes(m_showId).first();
        QSignalSpy pending(&pc, &PlaybackController::autoAdvancePending);
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);

        auto* seek = s.find<SeekSlider>("SeekSlider");
        QTest::mouseClick(seek, Qt::LeftButton, Qt::NoModifier, QPoint(int(seek->width() * 0.94), seek->height() / 2));
        QVERIFY(waitFor([&] { return s.engine->positionSeconds() > 36.0; }, 12000));
        // seeking close to the end must not complete the episode...
        QVERIFY(!s.app->library().progress(first.file.id).completed);
        // ...only actually reaching the end does
        QVERIFY(waitFor([&] { return pending.count() >= 1; }, 15000));
        QVERIFY(s.app->library().progress(first.file.id).completed);

        auto* card = s.find<CenterCard>("CenterCard");
        QVERIFY(card && card->isVisible());
        const QString expectedNext = episodeLabel(s.app->library().episodes(m_showId).at(1));
        QCOMPARE(pending.first().at(0).value<QueueEntry>().title, expectedNext);
        s.shot(QStringLiteral("09_next_episode_countdown"));

        // three seconds later the next episode in the configured order plays
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 12000));
        QCOMPARE(now.first().at(0).value<QueueEntry>().title, expectedNext);
        QVERIFY(waitFor([&] { return s.app->playback().positionMs() >= 800; }, 12000));
        QVERIFY(!card->isVisible());
        s.shot(QStringLiteral("10_second_episode_playing"));
        // watch a few seconds (an episode left within its first 2 s deliberately leaves no history)
        QVERIFY(waitFor([&] { return s.app->playback().positionMs() >= 3000; }, 12000));
        s.closeLikeAUser();
    }

    // 13-14: reopen again; completed state and progress are still right
    void step7_stateSurvivesSecondRestart()
    {
        QVERIFY(s.start(m_dataDir));
        const auto eps = s.app->library().episodes(m_showId);
        QVERIFY(eps[0].progress.completed);
        QVERIFY(!eps[1].progress.completed);
        QVERIFY(eps[1].progress.positionMs > 0 || eps[1].progress.lastWatchedAt > 0);

        // open the show page and look at the tree
        clickButton(s.find<QToolButton>("nav_tvshows"));
        s.app->library().libraryChanged();  // same as arriving on the page
        QTest::qWait(300);
        s.window->openShow(m_showId);
        auto* tree = s.find<QTreeWidget>("EpisodeTree");
        QVERIFY(waitFor([&] { return tree && tree->isVisible() && tree->topLevelItemCount() == 1; }, 12000));
        QTreeWidgetItem* firstItem = tree->topLevelItem(0)->child(0);
        QCOMPARE(firstItem->data(0, Qt::UserRole + 5).toBool(), true);  // RoleWatched
        auto* cont = s.find<QPushButton>("ContinueButton");
        QVERIFY2(cont->text().contains(QStringLiteral("Continue")) || cont->text().contains(QStringLiteral("next")), qPrintable(cont->text()));
        s.shot(QStringLiteral("11_show_page_after_two_restarts"));
    }

    // 15-16: reorder again; the new sequence is the one that plays
    void step8_reorderAndSequenceIsRespected()
    {
        QVERIFY2(s.window, "the previous step left no running session");
        auto* tree = s.find<QTreeWidget>("EpisodeTree");
        const QString lastTitle = episodeTitlesInTree(tree).last();
        selectItem(tree, episodeItemByTitle(tree, lastTitle));
        for (int i = 0; i < 12 && episodeTitlesInTree(tree).first() != lastTitle; ++i) {
            const int indexBefore = int(episodeTitlesInTree(tree).indexOf(lastTitle));
            clickButton(s.find<QToolButton>("MoveUpButton"));
            QVERIFY(waitFor([&] { return episodeTitlesInTree(tree).indexOf(lastTitle) == indexBefore - 1; }, 12000));
            selectItem(tree, episodeItemByTitle(tree, lastTitle));
        }
        QCOMPARE(episodeTitlesInTree(tree).first(), lastTitle);

        QStringList dbOrder;
        QStringList dbLabels;
        for (const Episode& e : s.app->library().episodes(m_showId)) {
            dbOrder << e.title;
            dbLabels << episodeLabel(e);
        }
        QCOMPARE(dbOrder, episodeTitlesInTree(tree));

        // the playback queue (what Next / auto-advance use) follows the same order
        s.app->queue().setContext({ContextType::Show, m_showId});
        QStringList queued;
        for (const QueueEntry& q : s.app->queue().entries()) queued << q.title;
        QCOMPARE(queued, dbLabels);

        // start from the new first item and use the on-screen Next button: it goes to the 2nd in the new order
        QSignalSpy now(&s.app->playback(), &PlaybackController::nowPlaying);
        s.window->startPlayback({ContextType::Show, m_showId}, s.app->library().episodes(m_showId).first().file.id, StartMode::FromStart);
        QVERIFY(waitFor([&] { return now.count() >= 1; }, 20000));
        QVERIFY(waitFor([&] { return s.app->playback().state() == PlaybackState::Playing; }, 12000));
        auto* next = s.find<QToolButton>("NextButton");
        QVERIFY(next->isEnabled());
        clickButton(next);
        QVERIFY(waitFor([&] { return s.app->playback().currentEntry().title == dbLabels.at(1); }, 15000));
        s.shot(QStringLiteral("12_next_button_follows_new_order"));
    }


    // Drag-and-drop reorder: the tree turns a drop into a layout; here the drop is delivered through
    // the tree's own signal (an OS-level drag cannot be synthesised in-process), the rest is real.
    void step8b_dropHandlerReordersAndMovesAcrossSeasons()
    {
        QVERIFY(s.start(m_dataDir));
        s.window->openShow(m_showId);
        auto* tree = s.find<EpisodeTree>("EpisodeTree");
        QVERIFY(waitFor([&] { return tree && tree->isVisible() && tree->topLevelItemCount() >= 1; }, 12000));
        auto& lib = s.app->library();

        // a second season to drop into
        const Id season2 = lib.addSeason(m_showId, QStringLiteral("Bonus"));
        QVERIFY(waitFor([&] { return tree->topLevelItemCount() == 2; }, 12000));
        QVector<Episode> eps = lib.episodes(m_showId);
        const Id first = eps.first().id;
        const Id last = eps.last().id;
        const Id lastMedia = eps.last().file.id;
        lib.saveProgress(lastMedia, 7777, 40000);

        // drop the first episode at the end of season 1, and the last one into the new season
        emit tree->episodesDropped({first}, eps.first().seasonId, int(eps.size()) - 1);
        QVERIFY(waitFor([&] { return lib.episodes(m_showId).last().id == first; }, 12000));
        emit tree->episodesDropped({last}, season2, 0);
        QVERIFY(waitFor([&] { return lib.episode(last)->seasonId == season2; }, 12000));
        QCOMPARE(lib.episodes(m_showId).last().id, last);              // season 2 plays after season 1
        QCOMPARE(lib.progress(lastMedia).positionMs, qint64(7777));     // progress untouched by moves
        QCOMPARE(tree->topLevelItem(1)->childCount(), 1);               // and the tree shows it
        // put things back so later steps see one season
        emit tree->episodesDropped({last}, eps.first().seasonId, int(eps.size()) - 1);
        QVERIFY(waitFor([&] { return lib.episode(last)->seasonId != season2; }, 12000));
        QVERIFY(lib.deleteSeason(season2));
        QVERIFY(waitFor([&] { return tree->topLevelItemCount() == 1; }, 12000));
        s.closeLikeAUser();
    }


    // 17-19: move a video file, see it flagged, reconnect it, progress is kept
    void step9_moveFileAndLocateIt()
    {
        QVERIFY(s.start(m_dataDir));
        auto& lib = s.app->library();
        Episode victim;
        for (const Episode& e : lib.episodes(m_showId))
            if (e.file.path.compare(m_relinkCopy, Qt::CaseInsensitive) == 0) victim = e;
        QVERIFY(victim.id != kInvalidId);
        s.app->playback().stop();
        lib.setWatched({victim.file.id}, false);
        lib.saveProgress(victim.file.id, 5500, 6000);  // 5.5 s into a 6 s clip: inside the file and resumable (>= 5 s)
        const QString moved = LibraryManager::normalizePath(QDir(m_dir.path()).filePath(QStringLiteral("moved away/renamed_clip.avi")));
        QDir().mkpath(QFileInfo(moved).absolutePath());
        QVERIFY(QFile::rename(m_relinkCopy, moved));

        QSignalSpy verified(&lib, &LibraryManager::filesVerified);
        lib.verifyShowAsync(m_showId);
        QVERIFY(verified.wait(10000));
        QVERIFY(lib.episode(victim.id)->file.missing);

        // playing it shows a clear "File not found" card with recovery options
        s.window->startPlayback({ContextType::Show, m_showId}, victim.file.id, StartMode::Resume);
        auto* card = s.find<CenterCard>("CenterCard");
        QVERIFY(waitFor([&] { return card && card->isVisible(); }, 12000));
        QStringList buttons;
        for (QPushButton* b : card->findChildren<QPushButton*>()) buttons << b->text();
        QVERIFY2(buttons.contains(QStringLiteral("Locate file…")), qPrintable(buttons.join(QLatin1Char('|'))));
        QVERIFY(buttons.contains(QStringLiteral("Retry")));
        QVERIFY(buttons.contains(QStringLiteral("Back to library")));
        s.shot(QStringLiteral("13_file_not_found"));

        // "Locate file…" (the file dialog itself is manual-only): reconnect and press Retry
        QVERIFY(lib.relinkFile(victim.file.id, moved).ok);
        QPushButton* retry = nullptr;
        for (QPushButton* b : card->findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Retry")) retry = b;
        QVERIFY(retry);
        QSignalSpy positions(s.engine, &IPlaybackBackend::positionChanged);
        QTest::mouseClick(retry, Qt::LeftButton);
        QVERIFY(waitFor([&] { return positions.count() >= 1; }, 15000));
        QVERIFY2(positions.first().at(0).toDouble() >= 5.0, "progress was lost by moving the file");  // resumed at ~5.5 s
        QCOMPARE(lib.episode(victim.id)->file.path, moved);
        QCOMPARE(lib.episode(victim.id)->title, victim.title);
        s.shot(QStringLiteral("14_relinked_and_resumed"));
        s.closeLikeAUser();
    }

    // Every screen with real data: nothing blank, nothing crashing; screenshots for review.
    void step10_visualTourOfEveryScreen()
    {
        QVERIFY(s.start(m_dataDir));
        auto& lib = s.app->library();
        // a duplicate import (anime_final.webm is already an episode) must be reported, not added;
        // the UI shows a report dialog which a user would dismiss with OK
        auto dismiss = autoDismissDialogs();
        QSignalSpy finished(&s.app->importer(), &MediaImportService::importFinished);
        s.app->importer().importMoviesAsync({videoPath(QStringLiteral("anime_final.webm"))});
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(1).value<ImportReport>().added, 0);
        QCOMPARE(finished.first().at(1).value<ImportReport>().duplicates.size(), 1);
        *dismiss = false;

        // a movie (private copy, spec-style release name), a collection holding the show and the movie
        const QString movieFile = QDir(m_dir.path()).filePath(QStringLiteral("movies/Big.Buck.Test.2008.1080p.mkv"));
        QDir().mkpath(QFileInfo(movieFile).absolutePath());
        QVERIFY(QFile::copy(videoPath(QStringLiteral("video_27.mkv")), movieFile));
        s.app->importer().importMoviesAsync({LibraryManager::normalizePath(movieFile)});
        QVERIFY(waitFor([&] { return !lib.movies().isEmpty(); }, 12000));
        QCOMPARE(lib.movies().first().title, QStringLiteral("Big Buck Test 2008"));
        const Id col = lib.createCollection(QStringLiteral("Weekend marathon"));
        lib.addToCollection(col, CollectionItemType::Show, m_showId);
        lib.addToCollection(col, CollectionItemType::Movie, lib.movies().first().id);

        const char* pages[] = {"nav_home", "nav_continuewatching", "nav_tvshows", "nav_movies", "nav_collections", "nav_settings"};
        int n = 15;
        for (const char* nav : pages) {
            clickButton(s.find<QToolButton>(nav));
            s.shot(QStringLiteral("%1_%2").arg(n++).arg(QString::fromLatin1(nav)));
        }
        s.closeLikeAUser();
    }
    // Fullscreen, auto-hiding controls, keyboard shortcuts and mouse controls in the player.
    void step11_fullscreenAutoHideAndShortcuts()
    {
        QVERIFY(s.start(m_dataDir));
        auto& lib = s.app->library();
        const Episode ep = lib.episodes(m_showId).first();
        QSignalSpy now(&s.app->playback(), &PlaybackController::nowPlaying);
        s.window->startPlayback({ContextType::Show, m_showId}, ep.file.id, StartMode::FromStart);
        QVERIFY(waitFor([&] { return now.count() >= 1 && s.app->playback().positionMs() >= 500; }, 20000));
        auto* player = s.window->findChild<PlayerView*>();
        QVERIFY(player && player->isVisible());
        auto* controls = player->findChild<QFrame*>("ControlBar");
        auto* topBar = player->findChild<QFrame*>("TopBar");
        QVERIFY(controls && topBar);

        // fullscreen from the button, and out again with Escape
        QVERIFY(!s.window->isFullScreen());
        clickButton(s.find<QToolButton>("FullscreenButton"));
        QVERIFY(waitFor([&] { return s.window->isFullScreen(); }, 12000));
        QVERIFY(player->isFullscreen());

        // controls hide by themselves after a few idle seconds while playing, return on mouse movement
        QVERIFY(controls->isVisible());
        QVERIFY2(waitFor([&] { return !controls->isVisible() && !topBar->isVisible(); }, 8000), "controls did not auto-hide");
        auto* video = player->findChild<MpvWidget*>();
        moveMouse(video, QPoint(300, 300));
        moveMouse(video, QPoint(320, 310));
        QVERIFY2(waitFor([&] { return controls->isVisible(); }, 3000), "controls did not return on mouse movement");
        s.shot(QStringLiteral("21_fullscreen_controls"));

        // keyboard: space pauses/plays, M mutes, Up raises volume, Right seeks, N goes to the next item
        QTest::keyClick(player, Qt::Key_Space);
        QVERIFY(waitFor([&] { return s.app->playback().state() == PlaybackState::Paused; }, 12000));
        QTest::keyClick(player, Qt::Key_Space);
        QVERIFY(waitFor([&] { return s.app->playback().state() == PlaybackState::Playing; }, 12000));
        QTest::keyClick(player, Qt::Key_M);
        QVERIFY(waitFor([&] { return s.engine->isMuted(); }, 12000));
        QTest::keyClick(player, Qt::Key_M);
        QVERIFY(waitFor([&] { return !s.engine->isMuted(); }, 12000));
        const int volume = s.engine->volume();
        QTest::keyClick(player, Qt::Key_Down);
        QVERIFY(waitFor([&] { return s.engine->volume() == volume - 5; }, 12000));
        const double before = s.engine->positionSeconds();
        QTest::keyClick(player, Qt::Key_Right);  // +5 s
        QVERIFY(waitFor([&] { return s.engine->positionSeconds() >= before + 4.0; }, 12000));
        QTest::keyClick(player, Qt::Key_BracketRight);  // speed up
        QVERIFY(waitFor([&] { return s.engine->speed() > 1.2; }, 12000));
        QTest::keyClick(player, Qt::Key_BracketLeft);
        QVERIFY(waitFor([&] { return s.engine->speed() < 1.1; }, 12000));
        const Id currentBefore = s.app->playback().currentEntry().mediaFileId;
        QTest::keyClick(player, Qt::Key_N);
        QVERIFY(waitFor([&] { return s.app->playback().currentEntry().mediaFileId != currentBefore; }, 12000));

        // double click on the video leaves fullscreen
        QTest::mouseDClick(player->findChild<MpvWidget*>(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QVERIFY2(waitFor([&] { return !s.window->isFullScreen(); }, 5000), "double click did not toggle fullscreen");

        // Escape with the player in windowed mode goes back to the library (and stops playback)
        QTest::keyClick(player, Qt::Key_Escape);
        QVERIFY(waitFor([&] { return !player->isVisible(); }, 12000));
        QCOMPARE(s.app->playback().state(), PlaybackState::Idle);
        s.closeLikeAUser();
    }

    // Skip Intro / Skip Ending: the options on the Settings page, the button the chapters make appear,
    // a click on it, and Enter on an ending that runs to the end of the file.
    void step12_skipOpeningAndEndingFromTheChapters()
    {
        QVERIFY(s.start(m_dataDir));
        auto& lib = s.app->library();
        auto& pc = s.app->playback();
        const QString dir = QDir(m_dir.path()).filePath(QStringLiteral("chaptered"));
        QDir().mkpath(dir);
        const QString clip = writeChapteredClip(videoPath(QStringLiteral("long_resume_test.mkv")), dir);
        QVERIFY(!clip.isEmpty());
        QCOMPARE(s.app->importer().importMoviesSync({LibraryManager::normalizePath(clip)}).added, 1);
        Movie movie;
        for (const Movie& m : lib.movies())
            if (m.file.path.endsWith(QStringLiteral("chaptered_episode.mkv"), Qt::CaseInsensitive)) movie = m;
        QVERIFY(movie.id != kInvalidId);

        // the two automatic options are on by default and write through when toggled
        clickButton(s.find<QToolButton>("nav_settings"));
        auto* autoIntro = s.find<QCheckBox>("AutoSkipIntro");
        auto* autoEnding = s.find<QCheckBox>("AutoSkipEnding");
        QVERIFY(autoIntro && autoEnding);
        QVERIFY(autoIntro->isChecked() && autoEnding->isChecked());
        QVERIFY(s.app->settings().autoSkipIntro() && s.app->settings().autoSkipEnding());
        s.shot(QStringLiteral("22_settings_skip_options"));
        autoIntro->click();
        autoEnding->click();
        QVERIFY(!s.app->settings().autoSkipIntro() && !s.app->settings().autoSkipEnding());

        // with both off, the opening is only offered: a "Skip Intro" button, and the video keeps playing
        QSignalSpy now(&pc, &PlaybackController::nowPlaying);
        s.window->startPlayback({ContextType::Movie, movie.id}, movie.file.id, StartMode::FromStart);
        auto* player = s.window->findChild<PlayerView*>();
        QVERIFY(player);
        auto* skip = player->findChild<QPushButton*>("SkipSegmentButton");
        QVERIFY(skip);
        QVERIFY2(waitFor([&] { return now.count() >= 1 && skip->isVisible(); }, 20000), "no Skip Intro button inside the opening");
        QCOMPARE(skip->text(), QStringLiteral("Skip Intro"));
        QVERIFY(s.engine->positionSeconds() < 8.0);
        s.shot(QStringLiteral("23_skip_intro_button"));

        QTest::mouseClick(skip, Qt::LeftButton);
        QVERIFY2(waitFor([&] { return s.engine->positionSeconds() >= 8.0; }, 8000), "clicking Skip Intro did not jump past the opening");
        QVERIFY(waitFor([&] { return !skip->isVisible(); }, 5000));

        // into the ending (14 s - end): the button now reads "Skip Ending"; Enter ends the movie by the
        // engine's own end-of-file, 25 s before the clip would have ended
        pc.seekToMs(15000);
        QVERIFY2(waitFor([&] { return skip->isVisible(); }, 8000), "no Skip Ending button inside the ending");
        QCOMPARE(skip->text(), QStringLiteral("Skip Ending"));
        s.shot(QStringLiteral("24_skip_ending_button"));
        QSignalSpy completed(&pc, &PlaybackController::itemCompleted);
        QSignalSpy finished(&pc, &PlaybackController::queueFinished);
        QTest::keyClick(player, Qt::Key_Return);
        QVERIFY2(waitFor([&] { return finished.count() >= 1; }, 10000), "Enter did not finish the movie");
        QCOMPARE(completed.count(), 1);
        QVERIFY(lib.movie(movie.id)->progress.completed);
        QVERIFY(!skip->isVisible());
        s.closeLikeAUser();
    }

};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("LOCALFLIX_E2E_HARDWARE_GL")) QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    lf::ui::Theme::apply(app);
    TstUiE2e test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ui_e2e.moc"
