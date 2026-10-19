#include "PlayerView.h"

#include "CenterCard.h"
#include "Format.h"
#include "MpvBackend.h"
#include "MpvWidget.h"
#include "SeekSlider.h"
#include "Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>

namespace lf::ui {

namespace {
constexpr int kControlBarHeight = 150;
constexpr int kPipControlBarHeight = 96;  // play/pause + seek bar only, in the small picture-in-picture window
constexpr int kTopBarHeight = 92;
constexpr int kAutoHideMs = 3000;

QString skipButtonStyle(bool compact)
{
    return QStringLiteral(
               "QPushButton { background: rgba(15,17,21,0.82); color: white; border: 1px solid rgba(255,255,255,0.7); border-radius: 6px;"
               " padding: %1; font-size: %2; font-weight: 600; }"
               "QPushButton:hover { background: white; color: black; border-color: white; }")
        .arg(compact ? QStringLiteral("5px 12px") : QStringLiteral("10px 22px"), compact ? QStringLiteral("10pt") : QStringLiteral("13pt"));
}
}  // namespace

int PlayerView::controlBarHeight() const
{
    return m_pip ? kPipControlBarHeight : kControlBarHeight;
}

int PlayerView::topInset() const
{
    return m_pip ? 0 : kTopBarHeight;  // no title bar over the video in picture in picture
}

PlayerView::PlayerView(ApplicationController& app, MpvBackend* backend, QWidget* parent)
    : QWidget(parent), m_app(app), m_playback(app.playback()), m_backend(backend)
{
    setMouseTracking(true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    m_video = new MpvWidget(backend, this);
    buildUi();
    buildShortcuts();

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &PlayerView::hideControlsIfIdle);
    m_clickTimer.setSingleShot(true);
    m_clickTimer.setInterval(QApplication::doubleClickInterval() + 30);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] { m_playback.togglePause(); });
    m_osdTimer.setSingleShot(true);
    connect(&m_osdTimer, &QTimer::timeout, m_osd, &QWidget::hide);
    m_countdownTimer.setInterval(100);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (!m_card->isPresented() || m_countdownTotalMs <= 0) {
            m_countdownTimer.stop();
            return;
        }
        const int remaining = std::max<qint64>(0, m_countdownRemainingAtStart - m_countdownClock.elapsed());
        m_card->setProgress(int(1000.0 * remaining / m_countdownTotalMs));
        const int secs = int(std::ceil(remaining / 1000.0));
        m_card->setBody(QStringLiteral("%1\nStarting in %2 s").arg(m_pendingNext.displayTitle()).arg(secs));
        if (remaining <= 0) m_countdownTimer.stop();
    });

    connect(&m_playback, &PlaybackController::stateChanged, this, &PlayerView::onStateChanged);
    connect(&m_playback, &PlaybackController::nowPlaying, this, &PlayerView::onNowPlaying);
    connect(&m_playback, &PlaybackController::queueUpdated, this, &PlayerView::onQueueUpdated);
    connect(&m_playback, &PlaybackController::positionChanged, this, &PlayerView::onPosition);
    connect(&m_playback, &PlaybackController::tracksChanged, this, &PlayerView::onTracks);
    connect(&m_playback, &PlaybackController::autoAdvancePending, this, &PlayerView::onAutoAdvancePending);
    connect(&m_playback, &PlaybackController::autoAdvanceCancelled, this, &PlayerView::onAutoAdvanceCancelled);
    connect(&m_playback, &PlaybackController::skipSegmentChanged, this, &PlayerView::onSkipSegmentChanged);
    connect(&m_playback, &PlaybackController::segmentSkipped, this, &PlayerView::onSegmentSkipped);
    connect(&m_playback, &PlaybackController::queueFinished, this, &PlayerView::onQueueFinished);
    connect(&m_playback, &PlaybackController::playbackEndedWithoutAdvance, this, &PlayerView::onEndedWithoutAdvance);
    connect(&m_playback, &PlaybackController::playbackStopped, this, &PlayerView::onStopped);
    connect(&m_playback, &PlaybackController::playbackFailed, this, &PlayerView::onFailed);

    connect(m_backend, &IPlaybackBackend::volumeChanged, this, [this](int v) {
        QSignalBlocker block(m_volume);
        m_volume->setValue(v);
        if (!m_backend->isMuted()) m_muteButton->setIcon(Theme::icon(v == 0 ? Icon::VolumeOff : Icon::VolumeUp, Qt::white, 26));
    });
    connect(m_backend, &IPlaybackBackend::mutedChanged, this, [this](bool muted) {
        m_muteButton->setIcon(Theme::icon(muted ? Icon::VolumeOff : Icon::VolumeUp, Qt::white, 26));
    });
    connect(m_backend, &IPlaybackBackend::speedChanged, this, [this](double s) {
        m_speedButton->setText(QStringLiteral("%1×").arg(s, 0, 'g', 3));
        for (QAction* a : m_speedGroup->actions()) a->setChecked(std::abs(a->data().toDouble() - s) < 0.001);
    });

    connect(m_video, &MpvWidget::mouseMoved, this, &PlayerView::showControls);
    connect(m_video, &MpvWidget::clicked, this, [this] { m_clickTimer.start(); });
    connect(m_video, &MpvWidget::doubleClicked, this, [this] {
        m_clickTimer.stop();  // a double click is a fullscreen toggle, not two pauses
        emit fullscreenToggleRequested();
    });
    connect(m_video, &MpvWidget::wheelScrolled, this, [this](int dy) { adjustVolume(dy > 0 ? 5 : -5); });

    // Any mouse movement anywhere on the page wakes the controls.
    const auto children = findChildren<QWidget*>();
    for (QWidget* w : children) {
        w->setMouseTracking(true);
        w->installEventFilter(this);
    }
    installEventFilter(this);

    onStateChanged(PlaybackState::Idle);
}

PlayerView::~PlayerView() = default;

// ---------------------------------------------------------------- construction

QToolButton* PlayerView::makeButton(Icon icon, const QString& tooltip, int iconSize)
{
    auto* b = new QToolButton(m_controls);
    b->setIcon(Theme::icon(icon, Qt::white, iconSize));
    b->setIconSize(QSize(iconSize, iconSize));
    b->setToolTip(tooltip);
    b->setProperty("iconButton", true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAccessibleName(tooltip);
    return b;
}

void PlayerView::buildUi()
{
    // ---- top bar
    m_topBar = new QFrame(this);
    m_topBar->setObjectName(QStringLiteral("TopBar"));
    m_backButton = new QToolButton(m_topBar);
    m_backButton->setIcon(Theme::icon(Icon::Back, Qt::white, 26));
    m_backButton->setIconSize(QSize(26, 26));
    m_backButton->setToolTip(QStringLiteral("Back to library (Esc)"));
    m_backButton->setObjectName(QStringLiteral("BackButton"));
    m_backButton->setProperty("iconButton", true);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setFocusPolicy(Qt::NoFocus);
    m_titleLabel = new QLabel(m_topBar);
    QFont tf = m_titleLabel->font();
    tf.setPointSizeF(15);
    tf.setBold(true);
    m_titleLabel->setFont(tf);
    m_subtitleLabel = new QLabel(m_topBar);
    m_subtitleLabel->setProperty("muted", true);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_titleLabel);
    titles->addWidget(m_subtitleLabel);
    auto* top = new QHBoxLayout(m_topBar);
    top->setContentsMargins(20, 14, 24, 14);
    top->setSpacing(12);
    top->addWidget(m_backButton);
    top->addLayout(titles, 1);
    connect(m_backButton, &QToolButton::clicked, this, &PlayerView::leaveToLibrary);

    // ---- control bar
    m_controls = new QFrame(this);
    m_controls->setObjectName(QStringLiteral("ControlBar"));
    m_seek = new SeekSlider(m_controls);
    m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), m_controls);
    m_timeLabel->setMinimumWidth(110);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_prevButton = makeButton(Icon::Previous, QStringLiteral("Previous episode (P)"));
    m_backSeekButton = makeButton(Icon::Rewind, QStringLiteral("Back 10 seconds (J)"));
    m_playButton = makeButton(Icon::Play, QStringLiteral("Play / pause (Space)"), 34);
    m_forwardSeekButton = makeButton(Icon::Forward, QStringLiteral("Forward 10 seconds (L)"));
    m_nextButton = makeButton(Icon::Next, QStringLiteral("Next episode (N)"));
    m_stopButton = makeButton(Icon::Stop, QStringLiteral("Stop"));
    m_prevButton->setObjectName(QStringLiteral("PrevButton"));
    m_nextButton->setObjectName(QStringLiteral("NextButton"));
    m_playButton->setObjectName(QStringLiteral("PlayPauseButton"));
    m_stopButton->setObjectName(QStringLiteral("StopButton"));
    m_backSeekButton->setObjectName(QStringLiteral("BackSeekButton"));
    m_forwardSeekButton->setObjectName(QStringLiteral("ForwardSeekButton"));
    m_seek->setObjectName(QStringLiteral("SeekSlider"));
    m_muteButton = makeButton(Icon::VolumeUp, QStringLiteral("Mute (M)"));
    m_volume = new QSlider(Qt::Horizontal, m_controls);
    m_volume->setRange(0, 130);
    m_volume->setValue(m_app.settings().defaultVolume());
    m_volume->setFixedWidth(110);
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setToolTip(QStringLiteral("Volume (Up / Down, mouse wheel)"));

    m_speedButton = new QToolButton(m_controls);
    m_speedButton->setText(QStringLiteral("1×"));
    m_speedButton->setToolTip(QStringLiteral("Playback speed ( [ and ] )"));
    m_speedButton->setProperty("iconButton", true);
    m_speedButton->setPopupMode(QToolButton::InstantPopup);
    m_speedButton->setCursor(Qt::PointingHandCursor);
    m_speedButton->setFocusPolicy(Qt::NoFocus);
    m_speedButton->setMinimumWidth(52);
    m_speedMenu = new QMenu(m_speedButton);
    m_speedGroup = new QActionGroup(m_speedMenu);
    for (double s : {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0}) {
        QAction* a = m_speedMenu->addAction(QStringLiteral("%1×").arg(s, 0, 'g', 3));
        a->setCheckable(true);
        a->setData(s);
        a->setChecked(s == 1.0);
        m_speedGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, s] {
            m_playback.setSpeed(s);
            showOsd(QStringLiteral("Speed %1×").arg(s, 0, 'g', 3));
        });
    }
    m_speedButton->setMenu(m_speedMenu);

    m_marksButton = makeButton(Icon::Flag, QStringLiteral("Mark the opening / ending for this season"), 24);
    m_marksButton->setObjectName(QStringLiteral("MarksButton"));
    m_marksButton->setPopupMode(QToolButton::InstantPopup);
    m_marksMenu = new QMenu(m_marksButton);
    m_marksButton->setMenu(m_marksMenu);
    connect(m_marksMenu, &QMenu::aboutToShow, this, &PlayerView::rebuildMarksMenu);

    m_audioButton = makeButton(Icon::Audio, QStringLiteral("Audio track (A)"), 24);
    m_audioButton->setPopupMode(QToolButton::InstantPopup);
    m_audioMenu = new QMenu(m_audioButton);
    m_audioButton->setMenu(m_audioMenu);
    m_subtitleButton = makeButton(Icon::Subtitles, QStringLiteral("Subtitles (S)"), 24);
    m_subtitleButton->setPopupMode(QToolButton::InstantPopup);
    m_subtitleMenu = new QMenu(m_subtitleButton);
    m_subtitleButton->setMenu(m_subtitleMenu);
    m_fullscreenButton = makeButton(Icon::Fullscreen, QStringLiteral("Fullscreen (F)"), 24);
    m_fullscreenButton->setObjectName(QStringLiteral("FullscreenButton"));
    m_pipButton = makeButton(Icon::PictureInPicture, QStringLiteral("Picture in picture (Ctrl+P)"), 24);
    m_pipButton->setObjectName(QStringLiteral("PictureInPictureButton"));
    m_muteButton->setObjectName(QStringLiteral("MuteButton"));
    m_volume->setObjectName(QStringLiteral("VolumeSlider"));

    auto* seekRow = new QHBoxLayout;
    seekRow->setSpacing(14);
    seekRow->addWidget(m_seek, 1);
    seekRow->addWidget(m_timeLabel);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    buttons->addWidget(m_prevButton);
    buttons->addWidget(m_backSeekButton);
    buttons->addWidget(m_playButton);
    buttons->addWidget(m_forwardSeekButton);
    buttons->addWidget(m_nextButton);
    buttons->addSpacing(6);
    buttons->addWidget(m_stopButton);
    buttons->addSpacing(18);
    buttons->addWidget(m_muteButton);
    buttons->addWidget(m_volume);
    buttons->addStretch(1);
    buttons->addWidget(m_speedButton);
    buttons->addWidget(m_marksButton);
    buttons->addWidget(m_audioButton);
    buttons->addWidget(m_subtitleButton);
    buttons->addWidget(m_pipButton);
    buttons->addWidget(m_fullscreenButton);

    // What picture in picture hides: everything but play/pause, the seek bar and the way back.
    m_fullOnlyControls = {m_timeLabel,    m_prevButton,  m_backSeekButton, m_forwardSeekButton, m_nextButton, m_stopButton,
                          m_muteButton,   m_volume,      m_speedButton,    m_marksButton,       m_audioButton, m_subtitleButton,
                          m_fullscreenButton};

    m_barLayout = new QVBoxLayout(m_controls);
    m_barLayout->setContentsMargins(24, 44, 24, 14);
    m_barLayout->setSpacing(6);
    m_barLayout->addLayout(seekRow);
    m_barLayout->addLayout(buttons);

    // ---- transient overlays
    m_osd = new QLabel(this);
    m_osd->setStyleSheet(QStringLiteral("background: rgba(0,0,0,0.65); color: white; border-radius: 10px; padding: 8px 16px; font-size: 13pt;"));
    m_osd->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osd->hide();
    m_hoverTime = new QLabel(this);
    m_hoverTime->setStyleSheet(QStringLiteral("background: rgba(0,0,0,0.8); color: white; border-radius: 6px; padding: 3px 8px;"));
    m_hoverTime->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hoverTime->hide();
    m_card = new CenterCard(this);
    m_card->setObjectName(QStringLiteral("CenterCard"));
    m_skipButton = new QPushButton(this);
    m_skipButton->setObjectName(QStringLiteral("SkipSegmentButton"));
    m_skipButton->setCursor(Qt::PointingHandCursor);
    m_skipButton->setFocusPolicy(Qt::NoFocus);
    m_skipButton->setStyleSheet(skipButtonStyle(false));
    m_skipButton->hide();

    // ---- wiring
    connect(m_prevButton, &QToolButton::clicked, &m_playback, &PlaybackController::previous);
    connect(m_nextButton, &QToolButton::clicked, &m_playback, &PlaybackController::next);
    connect(m_backSeekButton, &QToolButton::clicked, this, [this] {
        m_playback.seekByMs(-10000);
        showOsd(QStringLiteral("−10 s"));
    });
    connect(m_forwardSeekButton, &QToolButton::clicked, this, [this] {
        m_playback.seekByMs(10000);
        showOsd(QStringLiteral("+10 s"));
    });
    connect(m_playButton, &QToolButton::clicked, this, [this] {
        if (m_playback.state() == PlaybackState::Idle && m_entry.valid())
            m_playback.playEntry(m_entry.mediaFileId, StartMode::Resume);
        else
            m_playback.togglePause();
    });
    connect(m_stopButton, &QToolButton::clicked, &m_playback, &PlaybackController::stop);
    connect(m_muteButton, &QToolButton::clicked, this, &PlayerView::toggleMute);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_playback.setVolume(v);
        if (m_backend->isMuted() && v > 0) m_playback.setMuted(false);
    });
    connect(m_fullscreenButton, &QToolButton::clicked, this, &PlayerView::fullscreenToggleRequested);
    connect(m_pipButton, &QToolButton::clicked, this, &PlayerView::pictureInPictureToggleRequested);
    connect(m_skipButton, &QPushButton::clicked, this, [this] { m_playback.skipActiveSegment(); });

    connect(m_seek, &SeekSlider::scrubbed, this, [this](double f) {
        m_scrubbing = true;
        updateTimeLabels(qint64(f * m_durationMs), m_durationMs);
    });
    connect(m_seek, &SeekSlider::seekRequested, this, [this](double f) {
        m_scrubbing = false;
        seekFraction(f);
    });
    connect(m_seek, &SeekSlider::hoverFraction, this, [this](double f, const QPoint& global) {
        if (m_durationMs <= 0) return;
        m_hoverTime->setText(formatClock(qint64(f * m_durationMs)));
        m_hoverTime->adjustSize();
        const QPoint local = mapFromGlobal(global);
        m_hoverTime->move(std::clamp(local.x() - m_hoverTime->width() / 2, 4, width() - m_hoverTime->width() - 4),
                          m_controls->y() + m_seek->y() - m_hoverTime->height() - 6);
        m_hoverTime->raise();
        m_hoverTime->show();
    });
    connect(m_seek, &SeekSlider::hoverEnded, m_hoverTime, &QWidget::hide);

    connect(m_audioMenu, &QMenu::aboutToHide, this, [this] { m_hideTimer.start(kAutoHideMs); });
    connect(m_subtitleMenu, &QMenu::aboutToHide, this, [this] { m_hideTimer.start(kAutoHideMs); });
    connect(m_speedMenu, &QMenu::aboutToHide, this, [this] { m_hideTimer.start(kAutoHideMs); });
    connect(m_marksMenu, &QMenu::aboutToHide, this, [this] { m_hideTimer.start(kAutoHideMs); });
}

void PlayerView::buildShortcuts()
{
    auto add = [this](const QKeySequence& seq, auto&& fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };
    auto seek = [this](qint64 ms) {
        m_playback.seekByMs(ms);
        showControls();
        showOsd(QStringLiteral("%1%2 s").arg(ms < 0 ? QStringLiteral("−") : QStringLiteral("+")).arg(std::abs(ms) / 1000));
    };

    add(Qt::Key_Space, [this] { m_playback.togglePause(); showControls(); });
    add(Qt::Key_K, [this] { m_playback.togglePause(); showControls(); });
    add(Qt::Key_Left, [seek] { seek(-5000); });
    add(Qt::Key_Right, [seek] { seek(5000); });
    add(Qt::SHIFT | Qt::Key_Left, [seek] { seek(-30000); });
    add(Qt::SHIFT | Qt::Key_Right, [seek] { seek(30000); });
    add(Qt::Key_J, [seek] { seek(-10000); });
    add(Qt::Key_L, [seek] { seek(10000); });
    add(Qt::Key_Up, [this] { adjustVolume(5); });
    add(Qt::Key_Down, [this] { adjustVolume(-5); });
    add(Qt::Key_M, [this] { toggleMute(); });
    add(Qt::Key_F, [this] { emit fullscreenToggleRequested(); });
    add(Qt::Key_F11, [this] { emit fullscreenToggleRequested(); });
    add(Qt::ALT | Qt::Key_Return, [this] { emit fullscreenToggleRequested(); });
    add(Qt::Key_Escape, [this] {
        if (m_fullscreen)
            emit fullscreenToggleRequested();
        else if (m_pip)
            emit pictureInPictureToggleRequested();  // Esc first leaves the small window, then the player
        else
            leaveToLibrary();
    });
    add(Qt::CTRL | Qt::Key_P, [this] { emit pictureInPictureToggleRequested(); });
    add(Qt::Key_Return, [this] { m_playback.skipActiveSegment(); });  // the Skip button; does nothing outside an opening/ending
    add(Qt::Key_Enter, [this] { m_playback.skipActiveSegment(); });
    add(Qt::Key_N, [this] { m_playback.next(); });
    add(Qt::Key_PageDown, [this] { m_playback.next(); });
    add(Qt::Key_P, [this] { m_playback.previous(); });
    add(Qt::Key_PageUp, [this] { m_playback.previous(); });
    add(Qt::Key_BracketLeft, [this] { stepSpeed(-1); });
    add(Qt::Key_BracketRight, [this] { stepSpeed(+1); });
    add(Qt::Key_Home, [this] { restartCurrent(); });
    add(Qt::Key_S, [this] { cycleTrack(TrackType::Subtitle); });
    add(Qt::Key_A, [this] { cycleTrack(TrackType::Audio); });
    for (int d = 0; d <= 9; ++d)
        add(QKeySequence(Qt::Key_0 + d), [this, d] { seekFraction(d / 10.0); });
}

// ---------------------------------------------------------------- layout & events

void PlayerView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutOverlays();
}

void PlayerView::layoutOverlays()
{
    m_video->setGeometry(rect());
    m_topBar->setGeometry(0, 0, width(), kTopBarHeight);
    m_controls->setGeometry(0, height() - controlBarHeight(), width(), controlBarHeight());
    m_card->setFixedWidth(std::clamp(width() - 24, 200, 500));  // narrower than 500 in a small window
    m_card->adjustSize();
    m_card->move((width() - m_card->width()) / 2, std::max(m_pip ? 4 : 20, (height() - m_card->height()) / 2 - 20));
    m_osd->adjustSize();
    m_osd->move(width() - m_osd->width() - 28, topInset() + 8);
    placeSkipButton();
    m_topBar->raise();
    m_controls->raise();
    m_skipButton->raise();
    m_osd->raise();
    m_card->raise();
}

// Bottom right, just above the seek bar while the controls are up and near the
// bottom edge when they are hidden, so it never jumps far when they fade.
void PlayerView::placeSkipButton()
{
    m_skipButton->adjustSize();
    const int bottom = m_controls->isVisible() ? m_controls->y() + (m_pip ? 14 : 30) : height() - (m_pip ? 14 : 40);
    m_skipButton->move(std::max(8, width() - m_skipButton->width() - (m_pip ? 10 : 28)), std::max(topInset(), bottom - m_skipButton->height()));
}

void PlayerView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    layoutOverlays();
    showControls();
    setFocus();
    updateKeepAwake();
}

void PlayerView::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    m_hideTimer.stop();
    m_clickTimer.stop();
    m_countdownTimer.stop();
    m_video->unsetCursor();
    updateKeepAwake();
}

bool PlayerView::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove && isVisible()) showControls();
    return QWidget::eventFilter(watched, event);
}

void PlayerView::wheelEvent(QWheelEvent* e)
{
    adjustVolume(e->angleDelta().y() > 0 ? 5 : -5);
    e->accept();
}

bool PlayerView::controlsAreHot() const
{
    if (m_controls->underMouse() || m_topBar->underMouse() || m_card->underMouse()) return true;
    return m_audioMenu->isVisible() || m_subtitleMenu->isVisible() || m_speedMenu->isVisible() || m_marksMenu->isVisible();
}

void PlayerView::showControls()
{
    if (!m_controls->isVisible() || (!m_pip && !m_topBar->isVisible())) {
        m_controls->show();
        if (!m_pip) m_topBar->show();
        layoutOverlays();
    }
    m_video->unsetCursor();
    m_hideTimer.start(kAutoHideMs);
    placeSkipButton();
}

void PlayerView::hideControlsIfIdle()
{
    const bool playing = m_playback.state() == PlaybackState::Playing;
    if (!playing || m_scrubbing || controlsAreHot() || m_card->isPresented()) {
        m_hideTimer.start(kAutoHideMs);
        return;
    }
    m_controls->hide();
    m_topBar->hide();
    m_hoverTime->hide();
    m_video->setCursor(Qt::BlankCursor);  // also hide the pointer while it is idle over the video
    placeSkipButton();
}

void PlayerView::showOsd(const QString& text)
{
    m_osd->setText(text);
    m_osd->adjustSize();
    m_osd->move(width() - m_osd->width() - 28, topInset() + 8);
    m_osd->raise();
    m_osd->show();
    m_osdTimer.start(1200);
}

void PlayerView::setPictureInPicture(bool on)
{
    if (m_pip == on) return;
    m_pip = on;
    for (QWidget* w : m_fullOnlyControls) w->setVisible(!on);
    m_barLayout->setContentsMargins(on ? QMargins(12, 14, 12, 8) : QMargins(24, 44, 24, 14));
    m_pipButton->setIcon(Theme::icon(on ? Icon::FullscreenExit : Icon::PictureInPicture, Qt::white, 24));
    m_pipButton->setToolTip(on ? QStringLiteral("Back to the full window (Ctrl+P, Esc)") : QStringLiteral("Picture in picture (Ctrl+P)"));
    m_skipButton->setStyleSheet(skipButtonStyle(on));
    if (on) m_topBar->hide();
    m_osd->hide();
    layoutOverlays();
    showControls();
}

void PlayerView::setFullscreen(bool fullscreen)
{
    m_fullscreen = fullscreen;
    m_fullscreenButton->setIcon(Theme::icon(fullscreen ? Icon::FullscreenExit : Icon::Fullscreen, Qt::white, 24));
    m_fullscreenButton->setToolTip(fullscreen ? QStringLiteral("Exit fullscreen (F, Esc)") : QStringLiteral("Fullscreen (F)"));
    showControls();
}

void PlayerView::whenVideoReady(std::function<void()> then)
{
    if (m_video->isRenderReady()) {
        then();
        return;
    }
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto fired = std::make_shared<bool>(false);
    *conn = connect(m_video, &MpvWidget::renderReady, this, [this, then, conn, fired] {
        if (*fired) return;
        *fired = true;
        disconnect(*conn);
        then();
    });
    // If the GPU context never comes up, say so instead of waiting forever.
    QTimer::singleShot(6000, this, [this, fired] {
        if (*fired || m_video->isRenderReady()) return;
        *fired = true;
        m_card->present(QStringLiteral("Video output unavailable"),
                        QStringLiteral("LocalFlix could not create an OpenGL context for video rendering. Update your graphics driver and try again."),
                        {{QStringLiteral("Back to library"), true, [this] { leaveToLibrary(); }}});
        layoutOverlays();
    });
}

// ---------------------------------------------------------------- controller signals

void PlayerView::onStateChanged(PlaybackState state)
{
    updatePlayPauseIcon();
    const bool active = state == PlaybackState::Playing || state == PlaybackState::Paused || state == PlaybackState::Loading;
    m_stopButton->setEnabled(active);
    m_seek->setEnabled(state == PlaybackState::Playing || state == PlaybackState::Paused);
    m_backSeekButton->setEnabled(state == PlaybackState::Playing || state == PlaybackState::Paused);
    m_forwardSeekButton->setEnabled(state == PlaybackState::Playing || state == PlaybackState::Paused);
    if (state == PlaybackState::Playing) {
        m_card->dismiss();
        m_hideTimer.start(kAutoHideMs);
    } else {
        if (state == PlaybackState::Loading) m_card->dismiss();
        showControls();
    }
    if (state == PlaybackState::Idle) {
        m_tracks.clear();
        rebuildTrackMenus();
    }
    updateKeepAwake();
}

// Only a video that is actually playing, on the visible player page, holds the display on: paused, finished,
// stopped or back in the library, the PC goes to standby as usual. Loading counts, so going from one episode to
// the next does not release it for the moment in between.
void PlayerView::updateKeepAwake()
{
    const PlaybackState state = m_playback.state();
    m_keepAwake.setActive(isVisible() && (state == PlaybackState::Playing || state == PlaybackState::Loading));
}

void PlayerView::updatePlayPauseIcon()
{
    const bool playing = m_playback.state() == PlaybackState::Playing;
    m_playButton->setIcon(Theme::icon(playing ? Icon::Pause : Icon::Play, Qt::white, 34));
    m_playButton->setToolTip(playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)"));
}

void PlayerView::updateNavigation(bool hasPrevious, bool hasNext)
{
    m_hasPrevious = hasPrevious;
    m_hasNext = hasNext;
    m_prevButton->setEnabled(hasPrevious);
    m_nextButton->setEnabled(hasNext);
}

void PlayerView::onNowPlaying(QueueEntry entry, bool hasPrevious, bool hasNext)
{
    m_entry = entry;
    m_card->dismiss();
    m_titleLabel->setText(entry.contextTitle.isEmpty() ? entry.title : entry.contextTitle);
    m_subtitleLabel->setText(entry.contextTitle.isEmpty() ? QString() : entry.title);
    m_subtitleLabel->setVisible(!m_subtitleLabel->text().isEmpty());
    emit titleChanged(entry.displayTitle());
    updateNavigation(hasPrevious, hasNext);
    m_durationMs = m_playback.durationMs();
    updateTimeLabels(m_playback.positionMs(), m_durationMs);
    showControls();
}

void PlayerView::onQueueUpdated(QueueEntry current, bool hasPrevious, bool hasNext)
{
    if (current.valid() && current.mediaFileId == m_entry.mediaFileId) {
        m_entry = current;
        m_titleLabel->setText(current.contextTitle.isEmpty() ? current.title : current.contextTitle);
        m_subtitleLabel->setText(current.contextTitle.isEmpty() ? QString() : current.title);
        emit titleChanged(current.displayTitle());
    }
    updateNavigation(hasPrevious, hasNext);
}

void PlayerView::updateTimeLabels(qint64 positionMs, qint64 durationMs)
{
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatClock(positionMs), durationMs > 0 ? formatClock(durationMs) : QStringLiteral("--:--")));
}

void PlayerView::onPosition(qint64 positionMs, qint64 durationMs)
{
    m_durationMs = durationMs;
    if (m_scrubbing) return;
    updateTimeLabels(positionMs, durationMs);
    if (durationMs > 0) {
        QSignalBlocker block(m_seek);
        m_seek->setValue(int(std::clamp(double(positionMs) / double(durationMs), 0.0, 1.0) * SeekSlider::kMax));
    }
}

void PlayerView::seekFraction(double fraction)
{
    if (m_durationMs <= 0) return;
    m_playback.seekToMs(qint64(std::clamp(fraction, 0.0, 1.0) * double(m_durationMs)));
    showControls();
}

void PlayerView::onTracks(QVector<TrackInfo> tracks)
{
    m_tracks = tracks;
    rebuildTrackMenus();
}

void PlayerView::rebuildTrackMenus()
{
    m_audioMenu->clear();
    m_subtitleMenu->clear();

    auto* audioGroup = new QActionGroup(m_audioMenu);
    int audioCount = 0;
    bool anyAudioSelected = false;
    for (const TrackInfo& t : m_tracks) {
        if (t.type != TrackType::Audio) continue;
        ++audioCount;
        anyAudioSelected = anyAudioSelected || t.selected;
        QAction* a = m_audioMenu->addAction(t.label());
        a->setCheckable(true);
        a->setChecked(t.selected);
        audioGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] {
            m_playback.selectAudioTrack(t.id);
            showOsd(QStringLiteral("Audio: %1").arg(t.label()));
        });
    }
    if (audioCount == 0) {
        QAction* none = m_audioMenu->addAction(QStringLiteral("No audio tracks"));
        none->setEnabled(false);
    } else {
        m_audioMenu->addSeparator();
        QAction* off = m_audioMenu->addAction(QStringLiteral("Disable audio"));
        off->setCheckable(true);
        off->setChecked(!anyAudioSelected);
        audioGroup->addAction(off);
        connect(off, &QAction::triggered, this, [this] {
            m_playback.selectAudioTrack(kTrackOff);
            showOsd(QStringLiteral("Audio off"));
        });
    }

    auto* subGroup = new QActionGroup(m_subtitleMenu);
    bool anySubSelected = false;
    QList<QAction*> subActions;
    for (const TrackInfo& t : m_tracks) {
        if (t.type != TrackType::Subtitle) continue;
        anySubSelected = anySubSelected || t.selected;
        QAction* a = new QAction(t.label(), m_subtitleMenu);
        a->setCheckable(true);
        a->setChecked(t.selected);
        subGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] {
            m_playback.selectSubtitleTrack(t.id);
            showOsd(QStringLiteral("Subtitles: %1").arg(t.label()));
        });
        subActions << a;
    }
    QAction* off = m_subtitleMenu->addAction(QStringLiteral("Off"));
    off->setCheckable(true);
    off->setChecked(!anySubSelected);
    subGroup->addAction(off);
    connect(off, &QAction::triggered, this, [this] {
        m_playback.selectSubtitleTrack(kTrackOff);
        showOsd(QStringLiteral("Subtitles off"));
    });
    for (QAction* a : subActions) m_subtitleMenu->addAction(a);
    m_subtitleMenu->addSeparator();
    QAction* load = m_subtitleMenu->addAction(Theme::icon(Icon::Add, Colors::text, 16), QStringLiteral("Load external subtitle…"));
    connect(load, &QAction::triggered, this, &PlayerView::loadExternalSubtitle);

    m_audioButton->setEnabled(m_playback.isActive());
    m_subtitleButton->setEnabled(m_playback.isActive());
}

// The opening and the ending are found through the file's chapters. For files without them the user marks them
// once, here, and the marks apply to every episode of the season (chapters, where a file has them, win).
void PlayerView::rebuildMarksMenu()
{
    m_marksMenu->clear();
    auto note = [this](const QString& text) {
        QAction* a = m_marksMenu->addAction(text);
        a->setEnabled(false);
        return a;
    };

    if (!m_playback.canMarkSkips()) {
        note(m_entry.valid() && m_entry.seasonId == kInvalidId ? QStringLiteral("Marks are for episodes of a show, not movies")
                                                                : QStringLiteral("Marks are available while an episode plays"));
        return;
    }

    const qint64 here = m_playback.positionMs();
    const qint64 pending = m_playback.pendingIntroStartMs();
    const SeasonSkipMarks marks = m_playback.skipMarks();

    note(QStringLiteral("Applies to every episode of this season"));
    m_marksMenu->addSeparator();

    // The video keeps playing while the menu is open, so every mark is for the position the menu was opened at.
    QAction* introStart = m_marksMenu->addAction(QStringLiteral("Opening starts here (%1)").arg(formatClock(here)));
    connect(introStart, &QAction::triggered, this, [this, here] {
        if (m_playback.markIntroStartHere(here))
            showOsd(QStringLiteral("Opening start set at %1. Now mark where it ends.").arg(formatClock(here)));
    });
    QAction* introEnd = m_marksMenu->addAction(pending >= 0 ? QStringLiteral("Opening ends here (%1), it started at %2").arg(formatClock(here), formatClock(pending))
                                                            : QStringLiteral("Opening ends here (mark its start first)"));
    introEnd->setEnabled(pending >= 0);
    connect(introEnd, &QAction::triggered, this, [this, here, pending] {
        showOsd(m_playback.markIntroEndHere(here)
                    ? QStringLiteral("Opening marked %1 – %2 for this season").arg(formatClock(pending), formatClock(here))
                    : QStringLiteral("The opening must end at least 2 s after its start"));
    });
    QAction* ending = m_marksMenu->addAction(QStringLiteral("Ending starts here (%1)").arg(formatClock(here)));
    connect(ending, &QAction::triggered, this, [this, here] {
        const qint64 fromEnd = m_playback.durationMs() - here;
        showOsd(m_playback.markEndingStartHere(here)
                    ? QStringLiteral("Ending marked: the last %1 of every episode of this season").arg(formatClock(fromEnd))
                    : QStringLiteral("The ending must start at least 2 s before the end"));
    });

    m_marksMenu->addSeparator();
    note(marks.hasIntro() ? QStringLiteral("Opening: %1 – %2").arg(formatClock(marks.introStartMs), formatClock(marks.introEndMs))
                          : QStringLiteral("Opening: not marked"));
    note(marks.hasEnding() ? QStringLiteral("Ending: the last %1").arg(formatClock(marks.endingFromEndMs)) : QStringLiteral("Ending: not marked"));
    QAction* clearIntro = m_marksMenu->addAction(QStringLiteral("Clear the opening mark"));
    clearIntro->setEnabled(marks.hasIntro() || pending >= 0);
    connect(clearIntro, &QAction::triggered, this, [this] {
        if (m_playback.clearIntroMark()) showOsd(QStringLiteral("Opening mark cleared"));
    });
    QAction* clearEnding = m_marksMenu->addAction(QStringLiteral("Clear the ending mark"));
    clearEnding->setEnabled(marks.hasEnding());
    connect(clearEnding, &QAction::triggered, this, [this] {
        if (m_playback.clearEndingMark()) showOsd(QStringLiteral("Ending mark cleared"));
    });
    m_marksMenu->addSeparator();
    note(QStringLiteral("Files with their own chapters keep using them"));
}

void PlayerView::loadExternalSubtitle()
{
    if (!m_playback.isActive()) return;
    const QString dir = m_entry.path.isEmpty() ? QString() : QFileInfo(m_entry.path).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Load subtitle file"), dir,
                                                      QStringLiteral("Subtitles (*.srt *.ass *.ssa *.sub *.vtt *.smi *.sup *.idx);;All files (*)"));
    if (file.isEmpty()) return;
    if (m_playback.addExternalSubtitle(file))
        showOsd(QStringLiteral("Subtitle loaded: %1").arg(QFileInfo(file).fileName()));
    else
        showOsd(QStringLiteral("Could not load that subtitle file"));
}

void PlayerView::cycleTrack(TrackType type)
{
    QVector<TrackInfo> list;
    for (const TrackInfo& t : m_tracks)
        if (t.type == type) list.push_back(t);
    if (list.isEmpty()) {
        showOsd(type == TrackType::Audio ? QStringLiteral("No audio tracks") : QStringLiteral("No subtitles"));
        return;
    }
    int current = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].selected) current = i;
    // Subtitles cycle through "off"; audio just rotates.
    int next = current + 1;
    if (type == TrackType::Subtitle) {
        if (next >= list.size()) {
            m_playback.selectSubtitleTrack(kTrackOff);
            showOsd(QStringLiteral("Subtitles off"));
            return;
        }
        m_playback.selectSubtitleTrack(list[next].id);
        showOsd(QStringLiteral("Subtitles: %1").arg(list[next].label()));
    } else {
        next %= int(list.size());
        m_playback.selectAudioTrack(list[next].id);
        showOsd(QStringLiteral("Audio: %1").arg(list[next].label()));
    }
}

void PlayerView::adjustVolume(int delta)
{
    const int v = std::clamp(m_backend->volume() + delta, 0, 130);
    if (m_backend->isMuted()) m_playback.setMuted(false);
    m_playback.setVolume(v);
    {
        QSignalBlocker block(m_volume);
        m_volume->setValue(v);
    }
    showOsd(QStringLiteral("Volume %1%").arg(v));
    showControls();
}

void PlayerView::toggleMute()
{
    const bool mute = !m_backend->isMuted();
    m_playback.setMuted(mute);
    showOsd(mute ? QStringLiteral("Muted") : QStringLiteral("Volume %1%").arg(m_backend->volume()));
}

void PlayerView::stepSpeed(int direction)
{
    const double current = m_backend->speed();
    double next = current + 0.25 * direction;
    next = std::clamp(next, 0.25, 4.0);
    m_playback.setSpeed(next);
    showOsd(QStringLiteral("Speed %1×").arg(next, 0, 'g', 3));
}

void PlayerView::restartCurrent()
{
    if (m_playback.state() == PlaybackState::Playing || m_playback.state() == PlaybackState::Paused) {
        m_playback.seekToMs(0);
        showOsd(QStringLiteral("Restart"));
    }
}

// ---------------------------------------------------------------- cards

void PlayerView::onAutoAdvancePending(QueueEntry next, int remainingMs)
{
    m_pendingNext = next;
    m_countdownTotalMs = std::max(remainingMs, m_app.settings().autoPlayCountdownSeconds() * 1000);
    m_countdownRemainingAtStart = remainingMs;
    m_countdownClock.start();
    m_card->present(QStringLiteral("Next episode"),
                    QStringLiteral("%1\nStarting in %2 s").arg(next.displayTitle()).arg(int(std::ceil(remainingMs / 1000.0))),
                    {{QStringLiteral("Play now"), true, [this] { m_playback.advanceNow(); }},
                     {QStringLiteral("Cancel"), false, [this] { m_playback.cancelAutoAdvance(); }}},
                    /*withProgress=*/true);
    layoutOverlays();
    m_countdownTimer.start();
    showControls();
}

void PlayerView::onAutoAdvanceCancelled()
{
    m_countdownTimer.stop();
    QList<CenterCard::Button> buttons;
    if (m_playback.hasNext())
        buttons.push_back({QStringLiteral("Play next"), true, [this] { m_playback.next(); }});
    buttons.push_back({QStringLiteral("Replay"), false, [this] { m_playback.playEntry(m_entry.mediaFileId, StartMode::FromStart); }});
    buttons.push_back({QStringLiteral("Back to library"), false, [this] { leaveToLibrary(); }});
    m_card->present(QStringLiteral("Autoplay cancelled"), m_entry.displayTitle(), buttons);
    layoutOverlays();
    showControls();
}

void PlayerView::onSkipSegmentChanged(SkipSegment segment)
{
    if (!segment.valid()) {
        m_skipButton->hide();
        return;
    }
    m_skipButton->setText(segment.label());
    m_skipButton->setToolTip(QStringLiteral("%1 (Enter)").arg(segment.label()));
    m_skipButton->setAccessibleName(segment.label());
    placeSkipButton();
    m_skipButton->raise();
    m_skipButton->show();
}

void PlayerView::onSegmentSkipped(SkipSegment segment, bool automatic)
{
    if (automatic) showOsd(QStringLiteral("Skipped %1").arg(segment.noun()));
}

void PlayerView::onQueueFinished(QueueEntry last)
{
    m_countdownTimer.stop();
    const QString what = last.contextTitle.isEmpty() ? last.title : last.contextTitle;
    m_card->present(QStringLiteral("That's everything"),
                    QStringLiteral("You finished the last item of “%1”.").arg(what),
                    {{QStringLiteral("Watch again from the start"), false, [this] {
                          const auto& q = m_app.queue().entries();
                          if (!q.isEmpty()) m_playback.playEntry(q.first().mediaFileId, StartMode::FromStart);
                      }},
                     {QStringLiteral("Back to library"), true, [this] { leaveToLibrary(); }}});
    layoutOverlays();
    showControls();
}

void PlayerView::onEndedWithoutAdvance(QueueEntry finished, QueueEntry next)
{
    m_countdownTimer.stop();
    QList<CenterCard::Button> buttons;
    if (next.valid()) buttons.push_back({QStringLiteral("Play next"), true, [this] { m_playback.next(); }});
    buttons.push_back({QStringLiteral("Replay"), false, [this, finished] { m_playback.playEntry(finished.mediaFileId, StartMode::FromStart); }});
    buttons.push_back({QStringLiteral("Back to library"), false, [this] { leaveToLibrary(); }});
    m_card->present(QStringLiteral("Finished"),
                    next.valid() ? QStringLiteral("%1\nUp next: %2").arg(finished.displayTitle(), next.displayTitle()) : finished.displayTitle(),
                    buttons);
    layoutOverlays();
    showControls();
}

void PlayerView::onStopped(QueueEntry entry)
{
    if (!isVisible()) return;
    m_countdownTimer.stop();
    m_card->present(QStringLiteral("Playback stopped"), entry.displayTitle(),
                    {{QStringLiteral("Resume"), true, [this, entry] { m_playback.playEntry(entry.mediaFileId, StartMode::Resume); }},
                     {QStringLiteral("Back to library"), false, [this] { leaveToLibrary(); }}});
    layoutOverlays();
    showControls();
}

void PlayerView::onFailed(QueueEntry entry, PlaybackFailure failure)
{
    m_countdownTimer.stop();
    m_failedEntry = entry;
    if (entry.valid()) {
        m_entry = entry;
        m_titleLabel->setText(entry.contextTitle.isEmpty() ? entry.title : entry.contextTitle);
        m_subtitleLabel->setText(entry.contextTitle.isEmpty() ? QString() : entry.title);
        emit titleChanged(entry.displayTitle());
    }
    updateNavigation(m_playback.hasPrevious(), m_playback.hasNext());

    QList<CenterCard::Button> buttons;
    QString title = QStringLiteral("Cannot play this file");
    if (failure.kind == FailureKind::MissingFile) {
        title = QStringLiteral("File not found");
        buttons.push_back({QStringLiteral("Locate file…"), true, [this] {
                               if (m_failedEntry.valid()) emit locateFileRequested(m_failedEntry.mediaFileId);
                           }});
    }
    if (failure.kind != FailureKind::NotInQueue) {
        if (m_playback.hasNext()) buttons.push_back({QStringLiteral("Skip to next"), false, [this] { m_playback.next(); }});
        buttons.push_back({QStringLiteral("Retry"), false, [this] {
                               if (m_failedEntry.valid()) m_playback.playEntry(m_failedEntry.mediaFileId, StartMode::Resume);
                           }});
    }
    buttons.push_back({QStringLiteral("Back to library"), failure.kind == FailureKind::NotInQueue, [this] { leaveToLibrary(); }});
    m_card->present(title, failure.message, buttons);
    layoutOverlays();
    showControls();
}

void PlayerView::showCardForState()
{
    layoutOverlays();
}

void PlayerView::leaveToLibrary()
{
    m_countdownTimer.stop();
    m_clickTimer.stop();
    m_playback.stop();       // saves progress; no-op when already idle
    m_playback.cancelAutoAdvance();
    m_card->dismiss();
    emit backRequested();
}

}  // namespace lf::ui
