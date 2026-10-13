#include "ShowDetailsView.h"

#include "EpisodeManagerView.h"
#include "Format.h"
#include "LibraryActions.h"
#include "Theme.h"

#include <QDragEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
const QSize kCover(210, 315);
}

ShowDetailsView::ShowDetailsView(ApplicationController& app, LibraryActions* actions, QWidget* parent)
    : QWidget(parent), m_app(app), m_actions(actions)
{
    setAcceptDrops(true);
    buildUi();

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);  // coalesce bursts of signals into one refresh
    connect(&m_refreshTimer, &QTimer::timeout, this, &ShowDetailsView::refreshPage);

    auto& lib = m_app.library();
    connect(&lib, &LibraryManager::showChanged, this, [this](Id id) {
        if (id == m_showId) scheduleRefresh();
    });
    connect(&lib, &LibraryManager::libraryChanged, this, &ShowDetailsView::scheduleRefresh);
    connect(&lib, &LibraryManager::filesVerified, this, &ShowDetailsView::scheduleRefresh);
    connect(&lib, &LibraryManager::progressChanged, this, &ShowDetailsView::onProgressChanged);
    connect(&m_app.playback(), &PlaybackController::nowPlaying, this, &ShowDetailsView::onNowPlaying);
    connect(&m_app.playback(), &PlaybackController::stateChanged, this, &ShowDetailsView::onStateChanged);
}

void ShowDetailsView::buildUi()
{
    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(32, 24, 32, 32);
    root->setSpacing(22);

    auto* back = new QPushButton(Theme::icon(Icon::Back, Colors::text, 18), QStringLiteral("TV Shows"), content);
    back->setProperty("variant", QStringLiteral("ghost"));
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, &ShowDetailsView::backRequested);
    auto* backRow = new QHBoxLayout;
    backRow->addWidget(back);
    backRow->addStretch();
    root->addLayout(backRow);

    // ---- header
    m_cover = new QLabel(content);
    m_cover->setFixedSize(kCover);
    m_title = new QLabel(content);
    m_title->setObjectName(QStringLiteral("Heading"));
    m_title->setWordWrap(true);
    m_stats = new QLabel(content);
    m_stats->setProperty("muted", true);
    m_description = new QLabel(content);
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_overall = new QProgressBar(content);
    m_overall->setRange(0, 1000);
    m_overall->setTextVisible(false);
    m_overall->setMaximumWidth(420);

    m_continue = new QPushButton(content);
    m_continue->setObjectName(QStringLiteral("ContinueButton"));
    m_continue->setProperty("variant", QStringLiteral("primary"));
    m_continue->setMinimumHeight(42);
    m_continue->setCursor(Qt::PointingHandCursor);
    QFont cf = m_continue->font();
    cf.setPointSizeF(11.5);
    m_continue->setFont(cf);
    m_continueHint = new QLabel(content);
    m_continueHint->setProperty("muted", true);

    m_addFiles = new QPushButton(Theme::icon(Icon::Add, Colors::text, 18), QStringLiteral("Add files"), content);
    m_addFolder = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Add folder"), content);
    m_newSeason = new QPushButton(QStringLiteral("New season"), content);
    m_edit = new QPushButton(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Edit"), content);
    m_more = new QToolButton(content);
    m_more->setIcon(Theme::icon(Icon::More, Colors::text, 20));
    m_more->setToolTip(QStringLiteral("More actions"));
    m_more->setProperty("iconButton", true);
    m_more->setCursor(Qt::PointingHandCursor);
    for (QPushButton* b : {m_addFiles, m_addFolder, m_newSeason, m_edit}) b->setCursor(Qt::PointingHandCursor);

    auto* playRow = new QHBoxLayout;
    playRow->setSpacing(12);
    playRow->addWidget(m_continue);
    playRow->addWidget(m_continueHint, 1);

    auto* editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    editRow->addWidget(m_addFiles);
    editRow->addWidget(m_addFolder);
    editRow->addWidget(m_newSeason);
    editRow->addWidget(m_edit);
    editRow->addWidget(m_more);
    editRow->addStretch();

    auto* info = new QVBoxLayout;
    info->setSpacing(10);
    info->addWidget(m_title);
    info->addWidget(m_stats);
    info->addWidget(m_overall);
    info->addWidget(m_description, 1);
    info->addLayout(playRow);
    info->addLayout(editRow);

    auto* header = new QHBoxLayout;
    header->setSpacing(28);
    header->addWidget(m_cover, 0, Qt::AlignTop);
    header->addLayout(info, 1);
    root->addLayout(header);

    // ---- episodes
    auto* episodesTitle = new QLabel(QStringLiteral("Episodes"), content);
    episodesTitle->setObjectName(QStringLiteral("SectionTitle"));
    root->addWidget(episodesTitle);
    m_episodes = new EpisodeManagerView(m_app, m_actions, content);
    m_episodes->setMinimumHeight(380);
    root->addWidget(m_episodes, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_scroll);

    // ---- wiring
    connect(m_continue, &QPushButton::clicked, this, &ShowDetailsView::onContinueClicked);
    connect(m_addFiles, &QPushButton::clicked, this, [this] { m_actions->importEpisodesDialog(m_showId, m_episodes->selectedSeasonId(), false); });
    connect(m_addFolder, &QPushButton::clicked, this, [this] { m_actions->importEpisodesDialog(m_showId, m_episodes->selectedSeasonId(), true); });
    connect(m_newSeason, &QPushButton::clicked, this, [this] {
        const Id id = m_app.library().addSeason(m_showId);
        if (id != kInvalidId) m_episodes->expandAll();
    });
    connect(m_edit, &QPushButton::clicked, this, [this] { m_actions->editShow(m_showId); });
    connect(m_more, &QToolButton::clicked, this, &ShowDetailsView::showMoreMenu);
    connect(m_episodes, &EpisodeManagerView::newSeasonRequested, m_newSeason, &QPushButton::click);
    connect(m_episodes, &EpisodeManagerView::addFilesRequested, this, [this](Id seasonId) { m_actions->importEpisodesDialog(m_showId, seasonId, false); });
    connect(m_episodes, &EpisodeManagerView::addFolderRequested, this, [this](Id seasonId) { m_actions->importEpisodesDialog(m_showId, seasonId, true); });
    connect(m_episodes, &EpisodeManagerView::filesDropped, this,
            [this](const QStringList& paths, Id seasonId) { m_actions->importEpisodes(m_showId, seasonId, paths); });
    connect(m_episodes, &EpisodeManagerView::playEpisodeRequested, this,
            [this](Id mediaId) { emit playRequested({ContextType::Show, m_showId}, mediaId, StartMode::Auto); });
    connect(m_episodes, &EpisodeManagerView::playSeasonRequested, this,
            [this](Id mediaId) { emit playRequested({ContextType::Show, m_showId}, mediaId, StartMode::Auto); });
}

void ShowDetailsView::setShow(Id showId)
{
    m_showId = showId;
    m_episodes->setShow(showId);
    m_app.library().verifyShowAsync(showId);  // flag files that went missing since last time (off the UI thread)
    refreshPage();
    m_scroll->verticalScrollBar()->setValue(0);
}

void ShowDetailsView::scheduleRefresh()
{
    if (isVisible() || m_showId != kInvalidId) m_refreshTimer.start();
}

void ShowDetailsView::refreshPage()
{
    if (m_showId == kInvalidId) return;
    if (!m_app.library().show(m_showId)) {  // deleted elsewhere
        emit backRequested();
        return;
    }
    updateHeader();
    m_episodes->reload();
}

void ShowDetailsView::updateHeader()
{
    auto& lib = m_app.library();
    const auto show = lib.show(m_showId);
    if (!show) return;
    const QVector<Episode> episodes = lib.episodes(m_showId);
    const int seasons = int(lib.seasons(m_showId).size());
    int watched = 0;
    qint64 totalMs = 0;
    int missing = 0;
    for (const Episode& e : episodes) {
        if (e.progress.completed) ++watched;
        totalMs += e.file.durationMs;
        if (e.file.missing) ++missing;
    }

    m_title->setText(show->title);
    QString stats = QStringLiteral("%1 season%2 · %3 episode%4 · %5 watched")
                        .arg(seasons)
                        .arg(seasons == 1 ? QString() : QStringLiteral("s"))
                        .arg(episodes.size())
                        .arg(episodes.size() == 1 ? QString() : QStringLiteral("s"))
                        .arg(watched);
    if (totalMs > 0) stats += QStringLiteral(" · %1 total").arg(formatClock(totalMs));
    if (missing > 0) stats += QStringLiteral(" · ⚠ %1 file%2 missing").arg(missing).arg(missing == 1 ? QString() : QStringLiteral("s"));
    m_stats->setText(stats);
    m_overall->setValue(episodes.isEmpty() ? 0 : int(1000.0 * watched / episodes.size()));
    m_overall->setToolTip(QStringLiteral("%1 of %2 episodes watched").arg(watched).arg(episodes.size()));

    if (show->description.isEmpty()) {
        m_description->setText(QStringLiteral("No description yet. Use “Edit” to add one."));
        m_description->setProperty("muted", true);
    } else {
        m_description->setText(show->description);
        m_description->setProperty("muted", false);
    }
    m_description->style()->unpolish(m_description);
    m_description->style()->polish(m_description);

    m_cover->setPixmap(Theme::cover(lib.coverPath(show->coverFile), show->title, kCover, devicePixelRatioF()));

    // Series-level resume target: continue the last episode, or the next unfinished one.
    m_target = lib.resumeTarget(m_showId);
    m_continue->setEnabled(m_target.valid());
    m_continueHint->clear();
    if (!m_target.valid()) {
        m_continue->setIcon(QIcon());
        m_continue->setText(QStringLiteral("No episodes yet"));
        m_continueHint->setText(QStringLiteral("Add files or a folder, or drop video files onto this page."));
        return;
    }
    const Episode* targetEpisode = nullptr;
    for (const Episode& e : episodes)
        if (e.id == m_target.episodeId) targetEpisode = &e;
    const QString label = targetEpisode ? episodeLabel(*targetEpisode) : QString();
    m_continue->setIcon(Theme::icon(m_target.allCompleted() ? Icon::Replay : Icon::Play, Qt::white, 22));
    switch (m_target.reason) {
    case ResumeReason::InProgress:
        m_continue->setText(QStringLiteral("  Continue watching"));
        m_continueHint->setText(m_target.positionMs > 0 ? QStringLiteral("%1 · from %2").arg(label, formatClock(m_target.positionMs)) : label);
        break;
    case ResumeReason::NextUnfinished:
        m_continue->setText(QStringLiteral("  Play next episode"));
        m_continueHint->setText(label);
        break;
    case ResumeReason::NotStarted:
        m_continue->setText(QStringLiteral("  Start watching"));
        m_continueHint->setText(label);
        break;
    case ResumeReason::AllCompleted:
        m_continue->setText(QStringLiteral("  Watch again"));
        m_continueHint->setText(QStringLiteral("You have watched everything. Starts from %1.").arg(label));
        break;
    case ResumeReason::None:
        break;
    }
}

void ShowDetailsView::onContinueClicked()
{
    if (!m_target.valid()) return;
    // "Continue" is an explicit request, so it resumes without asking.
    emit playRequested({ContextType::Show, m_showId}, m_target.mediaFileId, m_target.allCompleted() ? StartMode::FromStart : StartMode::Resume);
}

void ShowDetailsView::showMoreMenu()
{
    QMenu menu(this);
    menu.addAction(Theme::icon(Icon::Collections, Colors::text, 18), QStringLiteral("Add to collection…"), this,
                   [this] { m_actions->addToCollection(CollectionItemType::Show, m_showId); });
    menu.addAction(Theme::icon(Icon::Refresh, Colors::text, 18), QStringLiteral("Check that files still exist"), this,
                   [this] { m_app.library().verifyShowAsync(m_showId); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Mark all as watched"), this, [this] { m_actions->setShowWatched(m_showId, true); });
    menu.addAction(QStringLiteral("Mark all as unwatched"), this, [this] { m_actions->setShowWatched(m_showId, false); });
    menu.addAction(Theme::icon(Icon::Replay, Colors::text, 18), QStringLiteral("Reset progress…"), this, [this] { m_actions->resetShowProgress(m_showId); });
    menu.addSeparator();
    menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove show from library…"), this, [this] { m_actions->deleteShow(m_showId); });
    menu.exec(m_more->mapToGlobal(QPoint(0, m_more->height())));
}

void ShowDetailsView::onProgressChanged(Id mediaFileId)
{
    if (!isVisible()) return;
    m_episodes->refreshProgress(mediaFileId);
    // header (continue target, watched count) follows, coalesced
    m_refreshTimer.start(400);
}

void ShowDetailsView::onNowPlaying(QueueEntry entry, bool, bool)
{
    m_episodes->setNowPlaying(entry.showId == m_showId ? entry.mediaFileId : kInvalidId);
}

void ShowDetailsView::onStateChanged(PlaybackState state)
{
    if (state == PlaybackState::Idle) m_episodes->setNowPlaying(kInvalidId);
}

void ShowDetailsView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ShowDetailsView::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ShowDetailsView::dropEvent(QDropEvent* e)
{
    const QStringList paths = m_actions->localPathsFromUrls(e->mimeData()->urls());
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    m_actions->importEpisodes(m_showId, m_episodes->selectedSeasonId(), paths);
}

}  // namespace lf::ui
