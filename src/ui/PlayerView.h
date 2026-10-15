#pragma once

#include "ApplicationController.h"
#include "IPlaybackBackend.h"
#include "Theme.h"
#include "Types.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <functional>

class QAction;
class QActionGroup;
class QFrame;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QToolButton;
class QSlider;

namespace lf {
class MpvBackend;
class MpvWidget;
}  // namespace lf

namespace lf::ui {

class SeekSlider;
class CenterCard;

// The player page: the libmpv video surface with Qt overlays stacked on top
// (title bar, control bar, state cards such as "Next episode in 3...").
class PlayerView : public QWidget {
    Q_OBJECT
public:
    PlayerView(ApplicationController& app, MpvBackend* backend, QWidget* parent = nullptr);
    ~PlayerView() override;

    // Runs `then` as soon as libmpv has a render context (immediately if it already has).
    void whenVideoReady(std::function<void()> then);
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return m_fullscreen; }
    // Shows the failure card for the media that could not be played.
    void showCardForState();

signals:
    void backRequested();            // user wants to leave the player (playback stopped)
    void fullscreenToggleRequested();
    void titleChanged(const QString& title);
    void locateFileRequested(lf::Id mediaFileId);
    void resumeChoiceMade();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onStateChanged(lf::PlaybackState state);
    void onNowPlaying(lf::QueueEntry entry, bool hasPrevious, bool hasNext);
    void onQueueUpdated(lf::QueueEntry current, bool hasPrevious, bool hasNext);
    void onPosition(qint64 positionMs, qint64 durationMs);
    void onTracks(QVector<lf::TrackInfo> tracks);
    void onAutoAdvancePending(lf::QueueEntry next, int remainingMs);
    void onAutoAdvanceCancelled();
    void onSkipSegmentChanged(lf::SkipSegment segment);
    void onSegmentSkipped(lf::SkipSegment segment, bool automatic);
    void onQueueFinished(lf::QueueEntry last);
    void onEndedWithoutAdvance(lf::QueueEntry finished, lf::QueueEntry next);
    void onStopped(lf::QueueEntry entry);
    void onFailed(lf::QueueEntry entry, lf::PlaybackFailure failure);

private:
    void buildUi();
    void buildShortcuts();
    void layoutOverlays();
    void placeSkipButton();
    QToolButton* makeButton(Icon icon, const QString& tooltip, int iconSize = 26);
    void updateTimeLabels(qint64 positionMs, qint64 durationMs);
    void updateNavigation(bool hasPrevious, bool hasNext);
    void updatePlayPauseIcon();
    void rebuildTrackMenus();
    void showOsd(const QString& text);
    void showControls();
    void hideControlsIfIdle();
    void leaveToLibrary();
    void restartCurrent();
    void stepSpeed(int direction);
    void cycleTrack(TrackType type);
    void adjustVolume(int delta);
    void toggleMute();
    void loadExternalSubtitle();
    void seekFraction(double fraction);
    bool controlsAreHot() const;

    ApplicationController& m_app;
    PlaybackController& m_playback;
    MpvBackend* m_backend;
    MpvWidget* m_video;

    QFrame* m_topBar;
    QToolButton* m_backButton;
    QLabel* m_titleLabel;
    QLabel* m_subtitleLabel;

    QFrame* m_controls;
    SeekSlider* m_seek;
    QLabel* m_timeLabel;
    QToolButton* m_prevButton;
    QToolButton* m_backSeekButton;
    QToolButton* m_playButton;
    QToolButton* m_forwardSeekButton;
    QToolButton* m_nextButton;
    QToolButton* m_stopButton;
    QToolButton* m_muteButton;
    QSlider* m_volume;
    QToolButton* m_speedButton;
    QToolButton* m_audioButton;
    QToolButton* m_subtitleButton;
    QToolButton* m_fullscreenButton;
    QMenu* m_speedMenu;
    QMenu* m_audioMenu;
    QMenu* m_subtitleMenu;
    QActionGroup* m_speedGroup;

    QPushButton* m_skipButton;  // "Skip Intro" / "Skip Ending", shown while the playhead is in one
    QLabel* m_osd;
    QTimer m_osdTimer;
    QLabel* m_hoverTime;
    CenterCard* m_card;

    QTimer m_hideTimer;
    QTimer m_clickTimer;
    QTimer m_countdownTimer;
    QElapsedTimer m_countdownClock;
    int m_countdownTotalMs = 0;
    int m_countdownRemainingAtStart = 0;
    QueueEntry m_pendingNext;

    QVector<TrackInfo> m_tracks;
    bool m_fullscreen = false;
    bool m_scrubbing = false;
    bool m_hasPrevious = false;
    bool m_hasNext = false;
    QueueEntry m_entry;
    QueueEntry m_failedEntry;
    qint64 m_durationMs = 0;
    int m_volumeBeforeMute = 100;
};

}  // namespace lf::ui
