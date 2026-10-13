#include "MainWindow.h"

#include "CollectionsView.h"
#include "Dialogs.h"
#include "HomeView.h"
#include "LibraryActions.h"
#include "LibraryView.h"
#include "MpvBackend.h"
#include "PlayerView.h"
#include "SettingsView.h"
#include "ShowDetailsView.h"
#include "Theme.h"
#include "Toast.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
struct NavEntry {
    Page page;
    Icon icon;
    const char* label;
};
constexpr NavEntry kNav[] = {
    {Page::Home, Icon::Home, "Home"},
    {Page::Continue, Icon::Continue, "Continue Watching"},
    {Page::Shows, Icon::Tv, "TV Shows"},
    {Page::Movies, Icon::Movie, "Movies"},
    {Page::Collections, Icon::Collections, "Collections"},
    {Page::Settings, Icon::Settings, "Settings"},
};
}  // namespace

MainWindow::MainWindow(ApplicationController& app, MpvBackend* backend, QWidget* parent)
    : QMainWindow(parent), m_app(app), m_backend(backend)
{
    setWindowTitle(QStringLiteral("LocalFlix"));
    setMinimumSize(980, 640);
    buildUi();

    const QByteArray geometry = m_app.settings().windowGeometry();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) resize(1360, 840);

    connect(&m_app.library(), &LibraryManager::errorOccurred, this, [this](const QString& message) { m_toast->showMessage(message, Toast::Kind::Error, 7000); });
    connect(&m_app.playback(), &PlaybackController::resumeDecisionNeeded, this, &MainWindow::onResumeDecisionNeeded);
    navigateTo(Page::Home);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- sidebar
    m_sidebar = new QFrame(central);
    m_sidebar->setObjectName(QStringLiteral("Sidebar"));
    m_sidebar->setFixedWidth(236);
    auto* sl = new QVBoxLayout(m_sidebar);
    sl->setContentsMargins(14, 22, 14, 16);
    sl->setSpacing(4);
    auto* brand = new QLabel(QStringLiteral("LocalFlix"), m_sidebar);
    brand->setObjectName(QStringLiteral("Brand"));
    auto* tag = new QLabel(QStringLiteral("Your videos, your order"), m_sidebar);
    tag->setProperty("muted", true);
    sl->addWidget(brand);
    sl->addWidget(tag);
    sl->addSpacing(22);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    for (int i = 0; i < 6; ++i) {
        auto* b = new QToolButton(m_sidebar);
        b->setText(QStringLiteral("  ") + QString::fromLatin1(kNav[i].label));
        b->setIcon(Theme::icon(kNav[i].icon, Colors::text, 22));
        b->setIconSize(QSize(22, 22));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setProperty("nav", true);
        b->setObjectName(QStringLiteral("nav_%1").arg(QString::fromLatin1(kNav[i].label).toLower().remove(QLatin1Char(' '))));
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(QStringLiteral("%1 (Ctrl+%2)").arg(QString::fromLatin1(kNav[i].label)).arg(i + 1));
        m_navGroup->addButton(b, i);
        m_navButtons[i] = b;
        sl->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, i] { navigateTo(kNav[i].page); });
        auto* sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, i] {
            if (!m_player->isVisible()) navigateTo(kNav[i].page);
        });
    }
    sl->addStretch();
    auto* version = new QLabel(QStringLiteral("v%1 · offline").arg(QCoreApplication::applicationVersion()), m_sidebar);
    version->setProperty("muted", true);
    sl->addWidget(version);

    // ---- pages
    m_stack = new QStackedWidget(central);
    m_toast = new Toast(central);
    m_actions = new LibraryActions(m_app, this, m_toast);

    m_home = new HomeView(m_app, m_actions, m_stack);
    m_continue = new LibraryView(m_app, m_actions, LibraryView::Mode::Continue, m_stack);
    m_shows = new LibraryView(m_app, m_actions, LibraryView::Mode::Shows, m_stack);
    m_movies = new LibraryView(m_app, m_actions, LibraryView::Mode::Movies, m_stack);
    m_collections = new CollectionsView(m_app, m_actions, m_stack);
    const QString engine = m_backend ? m_backend->mpvVersion() : QStringLiteral("none");
    m_settings = new SettingsView(m_app, QStringLiteral("libmpv (%1)").arg(engine), m_toast, m_stack);
    m_details = new ShowDetailsView(m_app, m_actions, m_stack);
    m_player = new PlayerView(m_app, m_backend, m_stack);

    for (QWidget* w : std::initializer_list<QWidget*>{m_home, m_continue, m_shows, m_movies, m_collections, m_settings, m_details, m_player})
        m_stack->addWidget(w);

    root->addWidget(m_sidebar);
    root->addWidget(m_stack, 1);
    setCentralWidget(central);

    // ---- navigation & playback wiring
    connect(m_home, &HomeView::navigateRequested, this, &MainWindow::navigateTo);
    connect(m_actions, &LibraryActions::openShowRequested, this, &MainWindow::openShow);
    connect(m_actions, &LibraryActions::playShowRequested, this, &MainWindow::playShow);
    connect(m_actions, &LibraryActions::playMovieRequested, this, &MainWindow::playMovie);
    connect(m_actions, &LibraryActions::playMediaRequested, this, &MainWindow::startPlayback);
    connect(m_actions, &LibraryActions::fileRelinked, this, [this](Id mediaFileId) {
        // Reconnected while the player is showing "File not found": try again right away.
        if (m_stack->currentWidget() == m_player && !m_app.playback().isActive()) m_app.playback().playEntry(mediaFileId, StartMode::Resume);
    });
    connect(m_collections, &CollectionsView::playRequested, this, &MainWindow::startPlayback);
    connect(m_details, &ShowDetailsView::playRequested, this, &MainWindow::startPlayback);
    connect(m_details, &ShowDetailsView::backRequested, this, [this] {
        QWidget* target = m_detailsReturnWidget ? m_detailsReturnWidget : static_cast<QWidget*>(m_shows);
        m_stack->setCurrentWidget(target);
        for (int i = 0; i < 6; ++i)
            if (pageWidget(kNav[i].page) == target) m_navButtons[i]->setChecked(true);
    });
    connect(m_player, &PlayerView::backRequested, this, &MainWindow::leavePlayer);
    connect(m_player, &PlayerView::fullscreenToggleRequested, this, [this] { setFullscreen(!isFullScreen()); });
    connect(m_player, &PlayerView::locateFileRequested, m_actions, &LibraryActions::locateMissing);
    connect(m_player, &PlayerView::titleChanged, this, [this](const QString& t) {
        setWindowTitle(t.isEmpty() ? QStringLiteral("LocalFlix") : QStringLiteral("%1 — LocalFlix").arg(t));
    });
}

QWidget* MainWindow::pageWidget(Page page) const
{
    switch (page) {
    case Page::Home: return m_home;
    case Page::Continue: return m_continue;
    case Page::Shows: return m_shows;
    case Page::Movies: return m_movies;
    case Page::Collections: return m_collections;
    case Page::Settings: return m_settings;
    }
    return m_home;
}

void MainWindow::navigateTo(Page page)
{
    if (m_stack->currentWidget() == m_player) return;  // leave the player explicitly first
    m_stack->setCurrentWidget(pageWidget(page));
    for (int i = 0; i < 6; ++i)
        if (kNav[i].page == page) m_navButtons[i]->setChecked(true);
}

void MainWindow::openShow(Id showId)
{
    if (m_stack->currentWidget() == m_player) return;
    if (m_stack->currentWidget() != m_details) m_detailsReturnWidget = m_stack->currentWidget();
    m_details->setShow(showId);
    m_stack->setCurrentWidget(m_details);
    m_navButtons[2]->setChecked(true);  // a show lives under "TV Shows"
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (!m_darkTitleApplied) {
        m_darkTitleApplied = true;
        Theme::useDarkTitleBar(this);
    }
}

// ---------------------------------------------------------------- playback glue

void MainWindow::playShow(Id showId)
{
    const ResumeTarget t = m_app.library().resumeTarget(showId);
    if (!t.valid()) {
        m_toast->showMessage(QStringLiteral("This show has no episodes yet."), Toast::Kind::Info);
        return;
    }
    startPlayback({ContextType::Show, showId}, t.mediaFileId, t.allCompleted() ? StartMode::FromStart : StartMode::Resume);
}

void MainWindow::playMovie(Id movieId)
{
    const auto movie = m_app.library().movie(movieId);
    if (!movie) return;
    startPlayback({ContextType::Movie, movieId}, movie->file.id, StartMode::Auto);
}

void MainWindow::startPlayback(const QueueContext& context, Id mediaFileId, StartMode mode)
{
    // "Resume automatically" is off: ask here, on the library page, before the player opens.
    if (mode == StartMode::Auto && !m_app.settings().resumeAutomatically()) {
        const QueueEntry entry = m_app.library().entryForMedia(mediaFileId);
        if (entry.valid() && !entry.completed && entry.positionMs > 0) {
            const ResumeChoice choice = askResumeChoice(this, entry.displayTitle(), entry.positionMs);
            if (choice == ResumeChoice::Cancel) return;
            mode = choice == ResumeChoice::Resume ? StartMode::Resume : StartMode::FromStart;
        }
    }
    enterPlayer();
    m_player->whenVideoReady([this, context, mediaFileId, mode] { m_app.playback().play(context, mediaFileId, mode); });
}

void MainWindow::onResumeDecisionNeeded(QueueEntry entry, qint64 positionMs)
{
    // Safety net for paths that reach the controller with StartMode::Auto.
    const ResumeChoice choice = askResumeChoice(this, entry.displayTitle(), positionMs);
    if (choice == ResumeChoice::Cancel) {
        if (!m_app.playback().isActive()) leavePlayer();
        return;
    }
    m_app.playback().playEntry(entry.mediaFileId, choice == ResumeChoice::Resume ? StartMode::Resume : StartMode::FromStart);
}

void MainWindow::enterPlayer()
{
    if (m_stack->currentWidget() == m_player) return;
    m_returnWidget = m_stack->currentWidget();
    m_sidebar->hide();
    m_toast->setAnchoredToTop(true);
    m_stack->setCurrentWidget(m_player);
    switch (m_app.settings().fullscreenBehavior()) {
    case FullscreenBehavior::OnPlayback: setFullscreen(true); break;
    case FullscreenBehavior::Remember: setFullscreen(m_app.settings().lastFullscreen()); break;
    case FullscreenBehavior::Manual: break;
    }
    m_player->setFullscreen(isFullScreen());
}

void MainWindow::leavePlayer()
{
    if (m_stack->currentWidget() != m_player) return;
    if (isFullScreen()) setFullscreen(false);  // library screens are not meant for fullscreen
    m_sidebar->show();
    m_toast->setAnchoredToTop(false);
    setWindowTitle(QStringLiteral("LocalFlix"));
    QWidget* target = m_returnWidget ? m_returnWidget : static_cast<QWidget*>(m_home);
    m_stack->setCurrentWidget(target);
    for (int i = 0; i < 6; ++i)
        if (pageWidget(kNav[i].page) == target) m_navButtons[i]->setChecked(true);
}

void MainWindow::setFullscreen(bool fullscreen)
{
    if (fullscreen == isFullScreen()) {
        m_player->setFullscreen(fullscreen);
        return;
    }
    if (fullscreen) {
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
    m_player->setFullscreen(fullscreen);
    // "Remember" mode stores what the user chose while watching (not the automatic exit).
    if (m_stack->currentWidget() == m_player && m_app.settings().fullscreenBehavior() == FullscreenBehavior::Remember)
        m_app.settings().setLastFullscreen(fullscreen);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save progress and stop the engine before anything is torn down.
    m_app.playback().shutdown();
    if (!isFullScreen()) m_app.settings().setWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}

}  // namespace lf::ui
