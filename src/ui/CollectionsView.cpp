#include "CollectionsView.h"

#include "Format.h"
#include "LibraryActions.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace lf::ui {

namespace {
enum Role { RoleId = Qt::UserRole + 1, RoleType, RoleItemId };
}

CollectionsView::CollectionsView(ApplicationController& app, LibraryActions* actions, QWidget* parent)
    : QWidget(parent), m_app(app), m_actions(actions)
{
    auto* title = new QLabel(QStringLiteral("Collections"), this);
    title->setObjectName(QStringLiteral("PageTitle"));
    auto* sub = new QLabel(QStringLiteral("Group shows and movies your own way and play them in your own order."), this);
    sub->setProperty("muted", true);

    // ---- left: the collections
    auto* newBtn = new QPushButton(Theme::icon(Icon::Add, Qt::white, 18), QStringLiteral("New collection"), this);
    newBtn->setProperty("variant", QStringLiteral("primary"));
    m_collections = new QListWidget(this);
    m_collections->setMinimumWidth(240);
    m_collections->setMaximumWidth(340);
    auto* left = new QVBoxLayout;
    left->setSpacing(10);
    left->addWidget(newBtn);
    left->addWidget(m_collections, 1);

    // ---- right: the selected collection
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("Heading"));
    m_summary = new QLabel(this);
    m_summary->setProperty("muted", true);
    m_summary->setWordWrap(true);
    m_play = new QPushButton(Theme::icon(Icon::Play, Qt::white, 20), QStringLiteral("Play collection"), this);
    m_play->setProperty("variant", QStringLiteral("primary"));
    m_play->setMinimumHeight(38);
    m_addShow = new QPushButton(Theme::icon(Icon::Tv, Colors::text, 18), QStringLiteral("Add show…"), this);
    m_addMovie = new QPushButton(Theme::icon(Icon::Movie, Colors::text, 18), QStringLiteral("Add movie…"), this);
    m_rename = new QPushButton(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Rename…"), this);
    m_delete = new QPushButton(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Delete collection"), this);
    m_delete->setProperty("variant", QStringLiteral("danger"));
    auto* actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(8);
    actionsRow->addWidget(m_play);
    actionsRow->addWidget(m_addShow);
    actionsRow->addWidget(m_addMovie);
    actionsRow->addWidget(m_rename);
    actionsRow->addStretch();
    actionsRow->addWidget(m_delete);

    m_entries = new QListWidget(this);
    m_entries->setSelectionMode(QAbstractItemView::SingleSelection);
    m_entries->setDragDropMode(QAbstractItemView::InternalMove);
    m_entries->setDefaultDropAction(Qt::MoveAction);
    m_entries->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entries->setIconSize(QSize(20, 20));
    m_entries->setSpacing(2);
    auto* hint = new QLabel(QStringLiteral("Drag entries to change the order. Double-click to open."), this);
    hint->setProperty("muted", true);

    auto* detail = new QVBoxLayout;
    detail->setSpacing(10);
    detail->addWidget(m_name);
    detail->addWidget(m_summary);
    detail->addLayout(actionsRow);
    detail->addWidget(hint);
    detail->addWidget(m_entries, 1);
    m_detail = new QWidget(this);
    m_detail->setLayout(detail);

    auto* emptyLayout = new QVBoxLayout;
    auto* emptyTitle = new QLabel(QStringLiteral("No collection selected"), this);
    emptyTitle->setObjectName(QStringLiteral("SectionTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto* emptyText = new QLabel(QStringLiteral("Create a collection, then add shows and movies from here or from any card's menu (⋯ → Add to collection)."), this);
    emptyText->setProperty("muted", true);
    emptyText->setWordWrap(true);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();
    m_emptyState = new QWidget(this);
    m_emptyState->setLayout(emptyLayout);

    auto* right = new QVBoxLayout;
    right->addWidget(m_detail, 1);
    right->addWidget(m_emptyState, 1);

    auto* body = new QHBoxLayout;
    body->setSpacing(24);
    body->addLayout(left);
    body->addLayout(right, 1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 24);
    root->setSpacing(6);
    root->addWidget(title);
    root->addWidget(sub);
    root->addSpacing(10);
    root->addLayout(body, 1);

    connect(newBtn, &QPushButton::clicked, this, &CollectionsView::createCollection);
    connect(m_collections, &QListWidget::currentItemChanged, this, [this] { if (!m_loading) reloadEntries(); });
    connect(m_play, &QPushButton::clicked, this, &CollectionsView::playCollection);
    connect(m_addShow, &QPushButton::clicked, this, &CollectionsView::addShow);
    connect(m_addMovie, &QPushButton::clicked, this, &CollectionsView::addMovie);
    connect(m_rename, &QPushButton::clicked, this, &CollectionsView::renameCollection);
    connect(m_delete, &QPushButton::clicked, this, &CollectionsView::deleteCollection);
    connect(m_entries, &QListWidget::itemActivated, this, &CollectionsView::activateEntry);
    connect(m_entries, &QListWidget::customContextMenuRequested, this, &CollectionsView::entryMenu);
    connect(m_entries->model(), &QAbstractItemModel::rowsMoved, this, [this] { if (!m_loading) persistOrder(); });
    auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), m_entries);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &CollectionsView::removeSelectedEntry);

    m_timer.setSingleShot(true);
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &CollectionsView::refresh);
    connect(&m_app.library(), &LibraryManager::libraryChanged, this, &CollectionsView::markDirty);
    connect(&m_app.library(), &LibraryManager::progressChanged, this, [this] { markDirty(); });
}

void CollectionsView::markDirty()
{
    m_dirty = true;
    if (isVisible()) m_timer.start();
}

void CollectionsView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_dirty) refresh();
}

Id CollectionsView::currentCollectionId() const
{
    const QListWidgetItem* it = m_collections->currentItem();
    return it ? it->data(RoleId).toLongLong() : kInvalidId;
}

void CollectionsView::refresh()
{
    m_dirty = false;
    reloadCollections();
}

void CollectionsView::reloadCollections()
{
    const Id keep = currentCollectionId();
    m_loading = true;
    m_collections->clear();
    int select = 0;
    int row = 0;
    for (const Collection& c : m_app.library().collections()) {
        auto* item = new QListWidgetItem(Theme::icon(Icon::Collections, Colors::textMuted, 20),
                                         QStringLiteral("%1   ·   %2").arg(c.name).arg(c.entryCount));
        item->setData(RoleId, c.id);
        item->setToolTip(c.name);
        m_collections->addItem(item);
        if (c.id == keep) select = row;
        ++row;
    }
    if (m_collections->count() > 0) m_collections->setCurrentRow(select);
    m_loading = false;
    reloadEntries();
}

void CollectionsView::reloadEntries()
{
    const Id id = currentCollectionId();
    const bool has = id != kInvalidId;
    m_detail->setVisible(has);
    m_emptyState->setVisible(!has);
    if (!has) return;

    auto& lib = m_app.library();
    const auto collection = lib.collections();
    QString name;
    for (const Collection& c : collection)
        if (c.id == id) name = c.name;
    m_name->setText(name);

    m_loading = true;
    m_entries->clear();
    const QVector<CollectionEntry> entries = lib.collectionEntries(id);
    const auto shows = lib.showSummaries();
    int totalEpisodes = 0;
    for (const CollectionEntry& e : entries) {
        QString detail;
        QIcon icon;
        if (e.type == CollectionItemType::Show) {
            icon = Theme::icon(Icon::Tv, Colors::accentHover, 20);
            for (const ShowSummary& s : shows)
                if (s.show.id == e.itemId) {
                    detail = QStringLiteral("Show · %1 episodes · %2 watched").arg(s.episodeCount).arg(s.completedCount);
                    totalEpisodes += s.episodeCount;
                }
        } else {
            icon = Theme::icon(Icon::Movie, Colors::warning, 20);
            if (const auto m = lib.movie(e.itemId)) {
                detail = QStringLiteral("Movie · %1").arg(m->progress.completed ? QStringLiteral("Watched") : (m->progress.started() ? QStringLiteral("In progress") : QStringLiteral("Not watched")));
                totalEpisodes += 1;
            }
        }
        auto* item = new QListWidgetItem(icon, QStringLiteral("%1\n%2").arg(e.title, detail));
        item->setData(RoleId, e.id);
        item->setData(RoleType, e.type == CollectionItemType::Show ? 0 : 1);
        item->setData(RoleItemId, e.itemId);
        item->setSizeHint(QSize(0, 52));
        m_entries->addItem(item);
    }
    m_loading = false;
    m_summary->setText(entries.isEmpty() ? QStringLiteral("Empty. Add shows and movies to build your sequence.")
                                         : QStringLiteral("%1 entr%2 · %3 playable items").arg(entries.size()).arg(entries.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies")).arg(totalEpisodes));
    m_play->setEnabled(totalEpisodes > 0);
}

void CollectionsView::createCollection()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New collection"), QStringLiteral("Collection name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const Id id = m_app.library().createCollection(name);
    if (id == kInvalidId) return;
    reloadCollections();
    for (int i = 0; i < m_collections->count(); ++i)
        if (m_collections->item(i)->data(RoleId).toLongLong() == id) m_collections->setCurrentRow(i);
}

void CollectionsView::renameCollection()
{
    const Id id = currentCollectionId();
    if (id == kInvalidId) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename collection"), QStringLiteral("Name:"), QLineEdit::Normal, m_name->text(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    m_app.library().updateCollection(id, name, QString());
}

void CollectionsView::deleteCollection()
{
    const Id id = currentCollectionId();
    if (id == kInvalidId) return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Delete collection"));
    box.setText(QStringLiteral("Delete the collection “%1”?").arg(m_name->text()));
    box.setInformativeText(QStringLiteral("Only the grouping is removed. The shows, movies and files in it are not touched."));
    QPushButton* ok = box.addButton(QStringLiteral("Delete collection"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == ok) m_app.library().deleteCollection(id);
}

void CollectionsView::addShow()
{
    const Id id = currentCollectionId();
    if (id == kInvalidId) return;
    const auto shows = m_app.library().showSummaries();
    if (shows.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Add show"), QStringLiteral("There are no shows in the library yet."));
        return;
    }
    QStringList names;
    for (const ShowSummary& s : shows) names << s.show.title;
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Add show"), QStringLiteral("Show:"), names, 0, false, &ok);
    if (!ok) return;
    for (const ShowSummary& s : shows)
        if (s.show.title == pick) {
            m_app.library().addToCollection(id, CollectionItemType::Show, s.show.id);
            break;
        }
}

void CollectionsView::addMovie()
{
    const Id id = currentCollectionId();
    if (id == kInvalidId) return;
    const auto movies = m_app.library().movies();
    if (movies.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Add movie"), QStringLiteral("There are no movies in the library yet."));
        return;
    }
    QStringList names;
    for (const Movie& m : movies) names << m.title;
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Add movie"), QStringLiteral("Movie:"), names, 0, false, &ok);
    if (!ok) return;
    for (const Movie& m : movies)
        if (m.title == pick) {
            m_app.library().addToCollection(id, CollectionItemType::Movie, m.id);
            break;
        }
}

void CollectionsView::removeSelectedEntry()
{
    const QListWidgetItem* it = m_entries->currentItem();
    const Id collectionId = currentCollectionId();
    if (!it || collectionId == kInvalidId) return;
    m_app.library().removeFromCollection(it->data(RoleId).toLongLong(), collectionId);
}

void CollectionsView::persistOrder()
{
    const Id collectionId = currentCollectionId();
    if (collectionId == kInvalidId) return;
    QVector<Id> order;
    for (int i = 0; i < m_entries->count(); ++i) order.push_back(m_entries->item(i)->data(RoleId).toLongLong());
    m_app.library().setCollectionOrder(collectionId, order);
}

void CollectionsView::playCollection()
{
    const Id id = currentCollectionId();
    if (id == kInvalidId) return;
    const QueueContext ctx{ContextType::Collection, id};
    const QVector<QueueEntry> queue = m_app.library().buildQueue(ctx);
    if (queue.isEmpty()) return;
    // first unfinished item, or the very first if everything has been watched
    Id start = queue.first().mediaFileId;
    bool allDone = true;
    for (const QueueEntry& e : queue) {
        if (!e.completed) {
            start = e.mediaFileId;
            allDone = false;
            break;
        }
    }
    emit playRequested(ctx, start, allDone ? StartMode::FromStart : StartMode::Resume);
}

void CollectionsView::activateEntry(QListWidgetItem* item)
{
    if (!item) return;
    const Id itemId = item->data(RoleItemId).toLongLong();
    if (item->data(RoleType).toInt() == 0)
        emit m_actions->openShowRequested(itemId);
    else
        emit m_actions->playMovieRequested(itemId);
}

void CollectionsView::entryMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_entries->itemAt(pos);
    if (!item) return;
    m_entries->setCurrentItem(item);
    QMenu menu(this);
    menu.addAction(item->data(RoleType).toInt() == 0 ? QStringLiteral("Open show") : QStringLiteral("Play movie"), this, [this, item] { activateEntry(item); });
    menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove from collection"), this, [this] { removeSelectedEntry(); });
    menu.exec(m_entries->viewport()->mapToGlobal(pos));
}

}  // namespace lf::ui
