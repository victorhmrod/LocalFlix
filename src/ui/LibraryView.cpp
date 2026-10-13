#include "LibraryView.h"

#include "CardFactory.h"
#include "FlowLayout.h"
#include "LibraryActions.h"
#include "Theme.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lf::ui {

LibraryView::LibraryView(ApplicationController& app, LibraryActions* actions, Mode mode, QWidget* parent)
    : QWidget(parent), m_app(app), m_actions(actions), m_mode(mode)
{
    setAcceptDrops(mode != Mode::Continue);

    const QString title = mode == Mode::Shows ? QStringLiteral("TV Shows") : (mode == Mode::Movies ? QStringLiteral("Movies") : QStringLiteral("Continue Watching"));
    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("PageTitle"));
    m_count = new QLabel(this);
    m_count->setProperty("muted", true);

    m_toolbar = new QHBoxLayout;
    m_toolbar->setSpacing(10);
    if (mode == Mode::Shows) {
        auto* create = new QPushButton(Theme::icon(Icon::Add, Qt::white, 18), QStringLiteral("New show"), this);
        create->setObjectName(QStringLiteral("NewShowButton"));
        create->setProperty("variant", QStringLiteral("primary"));
        auto* folder = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Import folder as show"), this);
        connect(create, &QPushButton::clicked, m_actions, [this] { m_actions->createShowInteractive(); });
        connect(folder, &QPushButton::clicked, m_actions, [this] { m_actions->importFolderAsShow(); });
        m_toolbar->addWidget(create);
        m_toolbar->addWidget(folder);
    } else if (mode == Mode::Movies) {
        auto* add = new QPushButton(Theme::icon(Icon::Add, Qt::white, 18), QStringLiteral("Add movies"), this);
        add->setObjectName(QStringLiteral("AddMoviesButton"));
        add->setProperty("variant", QStringLiteral("primary"));
        auto* folder = new QPushButton(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Add folder"), this);
        connect(add, &QPushButton::clicked, m_actions, [this] { m_actions->importMoviesDialog(false); });
        connect(folder, &QPushButton::clicked, m_actions, [this] { m_actions->importMoviesDialog(true); });
        m_toolbar->addWidget(add);
        m_toolbar->addWidget(folder);
    }
    m_toolbar->addStretch();
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedWidth(220);
    m_filter->setVisible(mode != Mode::Continue);
    m_toolbar->addWidget(m_filter);

    m_gridHost = new QWidget;
    m_grid = new FlowLayout(m_gridHost, 0, 18, 22);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_gridHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    buildEmptyState();

    auto* head = new QHBoxLayout;
    head->addWidget(m_title);
    head->addSpacing(12);
    head->addWidget(m_count, 0, Qt::AlignBottom);
    head->addStretch();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 16);
    root->setSpacing(14);
    root->addLayout(head);
    root->addLayout(m_toolbar);
    root->addWidget(m_scroll, 1);
    root->addWidget(m_empty, 1);

    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &LibraryView::refresh);
    connect(m_filter, &QLineEdit::textChanged, this, &LibraryView::applyFilter);

    auto& lib = m_app.library();
    connect(&lib, &LibraryManager::libraryChanged, this, &LibraryView::markDirty);
    connect(&lib, &LibraryManager::filesVerified, this, &LibraryView::markDirty);
    if (mode != Mode::Shows) connect(&lib, &LibraryManager::progressChanged, this, [this] { markDirty(); });
}

void LibraryView::buildEmptyState()
{
    m_empty = new QFrame(this);
    m_empty->setObjectName(QStringLiteral("Card"));
    m_emptyTitle = new QLabel(m_empty);
    m_emptyTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyText = new QLabel(m_empty);
    m_emptyText->setProperty("muted", true);
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);

    auto* l = new QVBoxLayout(m_empty);
    l->setContentsMargins(40, 40, 40, 40);
    l->setSpacing(12);
    l->addStretch();
    l->addWidget(m_emptyTitle);
    l->addWidget(m_emptyText);
    l->addStretch();

    switch (m_mode) {
    case Mode::Shows:
        m_emptyTitle->setText(QStringLiteral("No shows yet"));
        m_emptyText->setText(QStringLiteral("Create a show, then add its video files or a whole folder. Files stay where they are; LocalFlix only remembers where they are and your order.\nYou can also drop a folder here to turn it into a show."));
        break;
    case Mode::Movies:
        m_emptyTitle->setText(QStringLiteral("No movies yet"));
        m_emptyText->setText(QStringLiteral("Add movie files or a folder of movies, or drop video files here."));
        break;
    case Mode::Continue:
        m_emptyTitle->setText(QStringLiteral("Nothing to continue yet"));
        m_emptyText->setText(QStringLiteral("Start watching something and it will show up here with a Resume button."));
        break;
    }
    m_empty->hide();
}

void LibraryView::markDirty()
{
    m_dirty = true;
    if (isVisible()) m_timer.start();
}

void LibraryView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_dirty) refresh();
}

void LibraryView::refresh()
{
    m_dirty = false;
    for (const auto& c : std::as_const(m_cards)) {
        c.first->hide();
        m_grid->removeWidget(c.first);
        c.first->deleteLater();
    }
    m_cards.clear();

    auto& lib = m_app.library();
    switch (m_mode) {
    case Mode::Shows:
        for (const ShowSummary& s : lib.showSummaries()) {
            QWidget* w = CardFactory::showCard(m_app, m_actions, s, m_gridHost);
            m_cards.push_back({w, s.show.title});
            m_grid->addWidget(w);
        }
        m_count->setText(QStringLiteral("%1 show%2").arg(m_cards.size()).arg(m_cards.size() == 1 ? QString() : QStringLiteral("s")));
        break;
    case Mode::Movies:
        for (const Movie& m : lib.movies()) {
            QWidget* w = CardFactory::movieCard(m_app, m_actions, m, m_gridHost);
            m_cards.push_back({w, m.title});
            m_grid->addWidget(w);
        }
        m_count->setText(QStringLiteral("%1 movie%2").arg(m_cards.size()).arg(m_cards.size() == 1 ? QString() : QStringLiteral("s")));
        break;
    case Mode::Continue: {
        const QVector<ContinueItem> items = lib.continueWatching(60);
        QVector<MediaFile> toCheck;
        for (const ContinueItem& it : items) {
            QWidget* w = CardFactory::continueCard(m_app, m_actions, it, m_gridHost);
            m_cards.push_back({w, it.title});
            m_grid->addWidget(w);
            if (const auto f = lib.mediaFile(it.mediaFileId)) toCheck.push_back(*f);
        }
        lib.verifyFilesAsync(toCheck);  // flag moved/deleted files without blocking
        m_count->setText(QStringLiteral("%1 item%2").arg(m_cards.size()).arg(m_cards.size() == 1 ? QString() : QStringLiteral("s")));
        break;
    }
    }
    applyFilter();
}

void LibraryView::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    int visible = 0;
    for (const auto& c : std::as_const(m_cards)) {
        const bool match = needle.isEmpty() || c.second.contains(needle, Qt::CaseInsensitive);
        c.first->setVisible(match);
        if (match) ++visible;
    }
    const bool nothing = m_cards.isEmpty();
    m_scroll->setVisible(!nothing);
    m_empty->setVisible(nothing);
    if (!nothing && visible == 0) m_count->setText(QStringLiteral("No matches"));
    m_gridHost->updateGeometry();
}

void LibraryView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void LibraryView::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void LibraryView::dropEvent(QDropEvent* e)
{
    const QStringList paths = m_actions->localPathsFromUrls(e->mimeData()->urls());
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    if (m_mode == Mode::Movies) {
        m_actions->importMovies(paths);
    } else if (m_mode == Mode::Shows) {
        m_actions->importDroppedAsShows(paths);
    }
}

}  // namespace lf::ui
