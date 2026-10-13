#include "HomeView.h"

#include "CardFactory.h"
#include "CardRow.h"
#include "LibraryActions.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lf::ui {

HomeView::HomeView(ApplicationController& app, LibraryActions* actions, QWidget* parent)
    : QWidget(parent), m_app(app), m_actions(actions)
{
    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(32, 28, 32, 32);
    root->setSpacing(26);

    auto* title = new QLabel(QStringLiteral("Home"), content);
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    // ---- welcome card (empty library)
    auto* welcome = new QFrame(content);
    welcome->setObjectName(QStringLiteral("Card"));
    auto* wl = new QVBoxLayout(welcome);
    wl->setContentsMargins(36, 32, 36, 32);
    wl->setSpacing(10);
    auto* wt = new QLabel(QStringLiteral("Welcome to LocalFlix"), welcome);
    wt->setObjectName(QStringLiteral("Heading"));
    auto* wd = new QLabel(QStringLiteral("Your videos stay exactly where they are. Add them to a show or as movies, put the episodes in the order you want, "
                                         "and LocalFlix remembers where you stopped, then plays the next episode automatically."),
                          welcome);
    wd->setWordWrap(true);
    wd->setProperty("muted", true);
    auto* create = new QPushButton(Theme::icon(Icon::Add, Qt::white, 18), QStringLiteral("Create a show"), welcome);
    create->setProperty("variant", QStringLiteral("primary"));
    auto* folder = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Import a folder as a show"), welcome);
    auto* movies = new QPushButton(Theme::icon(Icon::Movie, Colors::text, 18), QStringLiteral("Add movies"), welcome);
    auto* wb = new QHBoxLayout;
    wb->setSpacing(10);
    wb->addWidget(create);
    wb->addWidget(folder);
    wb->addWidget(movies);
    wb->addStretch();
    wl->addWidget(wt);
    wl->addWidget(wd);
    wl->addSpacing(8);
    wl->addLayout(wb);
    m_welcome = welcome;
    root->addWidget(welcome);
    connect(create, &QPushButton::clicked, m_actions, [this] { m_actions->createShowInteractive(); });
    connect(folder, &QPushButton::clicked, m_actions, [this] { m_actions->importFolderAsShow(); });
    connect(movies, &QPushButton::clicked, m_actions, [this] { m_actions->importMoviesDialog(false); });

    // ---- sections
    m_sections = new QWidget(content);
    auto* sl = new QVBoxLayout(m_sections);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(26);
    m_continue = new CardRow(QStringLiteral("Continue Watching"), m_sections);
    m_recent = new CardRow(QStringLiteral("Recently Added"), m_sections);
    m_shows = new CardRow(QStringLiteral("TV Shows"), m_sections);
    m_movies = new CardRow(QStringLiteral("Movies"), m_sections);
    m_continue->setSeeAllVisible(true);
    m_shows->setSeeAllVisible(true);
    m_movies->setSeeAllVisible(true);
    m_continue->setEmptyText(QStringLiteral("Nothing in progress. Start an episode or a movie and it will appear here."));
    m_shows->setEmptyText(QStringLiteral("No shows yet."));
    m_movies->setEmptyText(QStringLiteral("No movies yet."));
    sl->addWidget(m_continue);
    sl->addWidget(m_recent);
    sl->addWidget(m_shows);
    sl->addWidget(m_movies);
    root->addWidget(m_sections);
    root->addStretch();

    connect(m_continue, &CardRow::seeAllClicked, this, [this] { emit navigateRequested(Page::Continue); });
    connect(m_shows, &CardRow::seeAllClicked, this, [this] { emit navigateRequested(Page::Shows); });
    connect(m_movies, &CardRow::seeAllClicked, this, [this] { emit navigateRequested(Page::Movies); });

    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_scroll);

    m_timer.setSingleShot(true);
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, &HomeView::refresh);
    auto& lib = m_app.library();
    connect(&lib, &LibraryManager::libraryChanged, this, &HomeView::markDirty);
    connect(&lib, &LibraryManager::progressChanged, this, [this] { markDirty(); });
    connect(&lib, &LibraryManager::filesVerified, this, &HomeView::markDirty);
}

void HomeView::markDirty()
{
    m_dirty = true;
    if (isVisible()) m_timer.start();
}

void HomeView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_dirty) refresh();
}

void HomeView::refresh()
{
    m_dirty = false;
    auto& lib = m_app.library();

    const QVector<ShowSummary> shows = lib.showSummaries();
    const QVector<Movie> movies = lib.movies();
    const bool empty = shows.isEmpty() && movies.isEmpty();
    m_welcome->setVisible(empty);
    m_sections->setVisible(!empty);
    if (empty) return;

    QList<QWidget*> cards;
    QVector<MediaFile> toCheck;
    for (const ContinueItem& item : lib.continueWatching(20)) {
        cards << CardFactory::continueCard(m_app, m_actions, item);
        if (const auto f = lib.mediaFile(item.mediaFileId)) toCheck.push_back(*f);
    }
    m_continue->setCards(cards);
    lib.verifyFilesAsync(toCheck);  // quietly flags moved/deleted files of what is on screen

    cards.clear();
    for (const LibraryCard& c : lib.recentlyAdded(14))
        if (QWidget* w = CardFactory::libraryCard(m_app, m_actions, c)) cards << w;
    m_recent->setCards(cards);

    cards.clear();
    for (const ShowSummary& s : shows) cards << CardFactory::showCard(m_app, m_actions, s);
    m_shows->setCards(cards);

    cards.clear();
    for (const Movie& m : movies) cards << CardFactory::movieCard(m_app, m_actions, m);
    m_movies->setCards(cards);
}

}  // namespace lf::ui
