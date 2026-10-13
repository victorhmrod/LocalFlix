#include "EpisodeManagerView.h"

#include "Format.h"
#include "LibraryActions.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lf::ui {

namespace {

// Eliding in the middle keeps both ends of a long path readable.
class PathDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(opt, index);
        opt->textElideMode = Qt::ElideMiddle;
    }
};

// Progress cell: thin bar plus "42%" / "Watched".
class ProgressDelegate : public QStyledItemDelegate {
public:
    ProgressDelegate(int roleFraction, int roleWatched, QObject* parent)
        : QStyledItemDelegate(parent), m_roleFraction(roleFraction), m_roleWatched(roleWatched)
    {
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget* w = option.widget;
        (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const QModelIndex rowData = index.siblingAtColumn(0);  // roles are stored on the row's first cell
        const double fraction = rowData.data(m_roleFraction).toDouble();
        const bool watched = rowData.data(m_roleWatched).toBool();
        if (fraction <= 0.0 && !watched) return;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(8, 0, -8, 0);
        const int textWidth = 62;
        const QRect bar(r.left(), r.center().y() - 3, r.width() - textWidth, 6);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x2a, 0x2f, 0x3a));
        p->drawRoundedRect(bar, 3, 3);
        p->setBrush(watched ? Colors::success : Colors::accent);
        p->drawRoundedRect(QRect(bar.left(), bar.top(), int(bar.width() * (watched ? 1.0 : qBound(0.0, fraction, 1.0))), bar.height()), 3, 3);
        p->setPen(watched ? Colors::success : Colors::textMuted);
        p->drawText(QRect(bar.right() + 8, r.top(), textWidth - 8, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                    watched ? QStringLiteral("Watched") : QStringLiteral("%1%").arg(int(fraction * 100.0 + 0.5)));
        p->restore();
    }

private:
    int m_roleFraction;
    int m_roleWatched;
};

}  // namespace

// ---------------------------------------------------------------- EpisodeTree (drag and drop)

void EpisodeTree::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        return;
    }
    QTreeWidget::dragEnterEvent(e);
}

void EpisodeTree::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        return;
    }
    QTreeWidget::dragMoveEvent(e);
}

void EpisodeTree::dropEvent(QDropEvent* e)
{
    QTreeWidgetItem* target = itemAt(e->position().toPoint());
    const auto kindOf = [](QTreeWidgetItem* it) { return it->data(0, Qt::UserRole + 1).toInt(); };  // RoleKind
    const auto idOf = [](QTreeWidgetItem* it) { return it->data(0, Qt::UserRole + 2).toLongLong(); };  // RoleId

    // Files dragged in from Explorer: import them (into the season under the cursor).
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        Id seasonId = kInvalidId;
        if (target) seasonId = kindOf(target) == 0 ? idOf(target) : idOf(target->parent());
        e->acceptProposedAction();
        if (!paths.isEmpty()) emit filesDropped(paths, seasonId);
        return;
    }
    if (e->source() != this) {
        e->ignore();
        return;
    }

    const DropIndicatorPosition pos = dropIndicatorPosition();
    const QList<QTreeWidgetItem*> selected = selectedItems();
    bool allEpisodes = !selected.isEmpty();
    bool allSeasons = !selected.isEmpty();
    for (QTreeWidgetItem* it : selected) {
        if (kindOf(it) == 1) allSeasons = false;
        else allEpisodes = false;
    }

    // Everything below is *intent*; the model is changed by the library, then the tree reloads.
    if (allEpisodes) {
        QTreeWidgetItem* seasonItem = nullptr;
        int insertAt = 0;  // position among the season's current children (before removing the dragged ones)
        if (!target) {
            seasonItem = topLevelItemCount() > 0 ? topLevelItem(topLevelItemCount() - 1) : nullptr;
            insertAt = seasonItem ? seasonItem->childCount() : 0;
        } else if (kindOf(target) == 1) {
            seasonItem = target->parent();
            insertAt = seasonItem->indexOfChild(target) + (pos == BelowItem ? 1 : 0);
        } else {
            seasonItem = target;
            const int idx = indexOfTopLevelItem(target);
            if (pos == OnItem) {
                insertAt = target->childCount();
            } else if (pos == BelowItem) {
                insertAt = target->isExpanded() ? 0 : target->childCount();
            } else if (idx > 0) {  // AboveItem: end of the previous season
                seasonItem = topLevelItem(idx - 1);
                insertAt = seasonItem->childCount();
            } else {
                insertAt = 0;
            }
        }
        if (seasonItem) {
            QSet<QTreeWidgetItem*> moving(selected.begin(), selected.end());
            int others = 0;
            for (int i = 0; i < insertAt && i < seasonItem->childCount(); ++i)
                if (!moving.contains(seasonItem->child(i))) ++others;

            // dragged episodes in on-screen order
            QList<Id> ids;
            for (int s = 0; s < topLevelItemCount(); ++s)
                for (int c = 0; c < topLevelItem(s)->childCount(); ++c)
                    if (moving.contains(topLevelItem(s)->child(c))) ids << idOf(topLevelItem(s)->child(c));
            emit episodesDropped(ids, idOf(seasonItem), others);
        }
    } else if (allSeasons && target) {
        QTreeWidgetItem* seasonTarget = kindOf(target) == 0 ? target : target->parent();
        QTreeWidgetItem* dragged = selected.first();
        if (seasonTarget && dragged != seasonTarget) {
            int others = 0;
            const int targetIdx = indexOfTopLevelItem(seasonTarget);
            const int insertAt = targetIdx + (pos == AboveItem ? 0 : 1);
            for (int i = 0; i < insertAt && i < topLevelItemCount(); ++i)
                if (topLevelItem(i) != dragged) ++others;
            emit seasonDropped(idOf(dragged), others);
        }
    }

    // We handled the drop ourselves: tell Qt nothing was moved so it does not
    // delete the dragged rows from the model afterwards.
    e->setDropAction(Qt::IgnoreAction);
    e->accept();
    stopAutoScroll();
    setState(QAbstractItemView::NoState);
    viewport()->update();
}

// ---------------------------------------------------------------- EpisodeManagerView

EpisodeManagerView::EpisodeManagerView(ApplicationController& app, LibraryActions* actions, QWidget* parent)
    : QWidget(parent), m_app(app), m_actions(actions)
{
    auto mkButton = [this](Icon icon, const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon, Colors::text, 18));
        b->setText(text);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setProperty("variant", QStringLiteral("ghost"));
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    m_up = mkButton(Icon::Back, QStringLiteral("Move up"), QStringLiteral("Move the selected episodes earlier in the sequence (Ctrl+Up)"));
    m_up->setIcon(QIcon());  // text only; the arrow icon would point left
    m_up->setText(QStringLiteral("▲ Move up"));
    m_down = mkButton(Icon::Back, QStringLiteral("▼ Move down"), QStringLiteral("Move the selected episodes later in the sequence (Ctrl+Down)"));
    m_down->setIcon(QIcon());
    m_moveTo = mkButton(Icon::Collections, QStringLiteral("Move to season"), QStringLiteral("Move the selected episodes to another season"));
    m_moveTo->setPopupMode(QToolButton::InstantPopup);
    m_watched = mkButton(Icon::Check, QStringLiteral("Mark watched"), QStringLiteral("Mark the selected episodes as watched"));
    m_unwatched = mkButton(Icon::Replay, QStringLiteral("Mark unwatched"), QStringLiteral("Mark the selected episodes as not watched"));
    m_showPaths = new QCheckBox(QStringLiteral("Show file paths"), this);
    m_showPaths->setChecked(true);
    m_hint = new QLabel(QStringLiteral("Drag episodes to set the playback order"), this);
    m_hint->setProperty("muted", true);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    bar->addWidget(m_up);
    bar->addWidget(m_down);
    bar->addWidget(m_moveTo);
    bar->addSpacing(10);
    bar->addWidget(m_watched);
    bar->addWidget(m_unwatched);
    bar->addStretch();
    bar->addWidget(m_showPaths);

    m_tree = new EpisodeTree(this);
    m_tree->setObjectName(QStringLiteral("EpisodeTree"));
    m_up->setObjectName(QStringLiteral("MoveUpButton"));
    m_down->setObjectName(QStringLiteral("MoveDownButton"));
    m_watched->setObjectName(QStringLiteral("MarkWatchedButton"));
    m_unwatched->setObjectName(QStringLiteral("MarkUnwatchedButton"));
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({QString(), QStringLiteral("#"), QStringLiteral("Title"), QStringLiteral("Progress"), QStringLiteral("Duration"), QStringLiteral("File")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIconSize(QSize(18, 18));
    m_tree->setIndentation(18);
    m_tree->setItemDelegateForColumn(ColProgress, new ProgressDelegate(RoleFraction, RoleWatched, m_tree));
    m_tree->setItemDelegateForColumn(ColFile, new PathDelegate(m_tree));
    QHeaderView* header = m_tree->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    header->setSectionResizeMode(ColNumber, QHeaderView::Fixed);
    header->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    header->setSectionResizeMode(ColProgress, QHeaderView::Fixed);
    header->setSectionResizeMode(ColDuration, QHeaderView::Fixed);
    header->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    header->resizeSection(ColStatus, 30);
    header->resizeSection(ColNumber, 54);
    header->resizeSection(ColProgress, 190);
    header->resizeSection(ColDuration, 84);
    m_tree->setMinimumHeight(260);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    root->addLayout(bar);
    root->addWidget(m_hint);
    root->addWidget(m_tree, 1);

    connect(m_showPaths, &QCheckBox::toggled, this, [this](bool on) { m_tree->setColumnHidden(ColFile, !on); });
    connect(m_tree, &QTreeWidget::itemChanged, this, &EpisodeManagerView::onItemChanged);
    connect(m_tree, &QTreeWidget::itemActivated, this, &EpisodeManagerView::onItemActivated);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &EpisodeManagerView::updateButtons);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &EpisodeManagerView::onContextMenu);
    connect(m_up, &QToolButton::clicked, this, [this] { moveSelection(-1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveSelection(+1); });
    connect(m_watched, &QToolButton::clicked, this, [this] { setWatchedForSelection(true); });
    connect(m_unwatched, &QToolButton::clicked, this, [this] { setWatchedForSelection(false); });
    connect(m_tree, &EpisodeTree::filesDropped, this, &EpisodeManagerView::filesDropped);
    connect(m_tree, &EpisodeTree::episodesDropped, this, [this](QList<Id> ids, Id seasonId, int index) {
        // Build the complete new layout from what is on screen and apply it atomically.
        QVector<SeasonLayout> layout = layoutFromTree();
        const QSet<Id> moving(ids.begin(), ids.end());
        for (SeasonLayout& sl : layout) {
            QVector<Id> kept;
            for (Id id : sl.episodeIds)
                if (!moving.contains(id)) kept.push_back(id);
            sl.episodeIds = kept;
        }
        for (SeasonLayout& sl : layout) {
            if (sl.seasonId != seasonId) continue;
            const int at = std::clamp(index, 0, int(sl.episodeIds.size()));
            for (int i = 0; i < ids.size(); ++i) sl.episodeIds.insert(at + i, ids[i]);
        }
        m_app.library().applyLayout(m_showId, layout);
        reload();
    });
    connect(m_tree, &EpisodeTree::seasonDropped, this, [this](Id seasonId, int index) {
        m_app.library().moveSeason(seasonId, index);
        reload();
    });

    auto key = [this](const QKeySequence& seq, auto fn) {
        auto* sc = new QShortcut(seq, m_tree);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };
    key(QKeySequence(Qt::Key_Delete), [this] { removeSelection(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { moveSelection(-1); });
    key(QKeySequence(Qt::CTRL | Qt::Key_Down), [this] { moveSelection(+1); });
    key(QKeySequence(Qt::Key_F2), [this] { renameCurrent(); });

    updateButtons();
}

void EpisodeManagerView::setShow(Id showId)
{
    if (showId != m_showId) m_loaded = false;
    m_showId = showId;
    reload();
}

QVector<SeasonLayout> EpisodeManagerView::layoutFromTree() const
{
    QVector<SeasonLayout> layout;
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* si = m_tree->topLevelItem(s);
        SeasonLayout sl;
        sl.seasonId = si->data(0, RoleId).toLongLong();
        for (int c = 0; c < si->childCount(); ++c) sl.episodeIds.push_back(si->child(c)->data(0, RoleId).toLongLong());
        layout.push_back(sl);
    }
    return layout;
}

void EpisodeManagerView::applyEpisodeItem(QTreeWidgetItem* item, const Episode& e)
{
    item->setData(0, RoleKind, EpisodeItem);
    item->setData(0, RoleId, e.id);
    item->setData(0, RoleMediaId, e.file.id);
    item->setData(0, RoleFraction, e.progress.fraction());
    item->setData(0, RoleWatched, e.progress.completed);
    item->setData(0, RoleMissing, e.file.missing);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);

    item->setText(ColNumber, e.episodeNumber > 0 ? QString::number(e.episodeNumber) : QString());
    item->setTextAlignment(ColNumber, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColTitle, e.title);
    item->setText(ColDuration, e.file.durationMs > 0 ? formatClock(e.file.durationMs) : QStringLiteral("—"));
    item->setForeground(ColDuration, Colors::textMuted);
    item->setText(ColFile, QDir::toNativeSeparators(e.file.path) + (e.file.missing ? QStringLiteral("   (missing)") : QString()));
    item->setToolTip(ColFile, QDir::toNativeSeparators(e.file.path));
    item->setForeground(ColFile, e.file.missing ? Colors::warning : Colors::textMuted);

    QIcon statusIcon;
    QString tip;
    if (e.file.id == m_nowPlayingMedia) {
        statusIcon = Theme::icon(Icon::Play, Colors::accentHover, 16);
        tip = QStringLiteral("Now playing");
    } else if (e.file.missing) {
        statusIcon = Theme::icon(Icon::Warning, Colors::warning, 16);
        tip = QStringLiteral("File not found. Right-click → Locate file…");
    } else if (e.progress.completed) {
        statusIcon = Theme::icon(Icon::Check, Colors::success, 16);
        tip = QStringLiteral("Watched");
    }
    item->setIcon(ColTitle, statusIcon);
    item->setToolTip(ColStatus, tip);
    item->setToolTip(ColTitle, e.file.missing ? QStringLiteral("File not found: ") + QDir::toNativeSeparators(e.file.path)
                                              : (tip.isEmpty() ? e.title : e.title + QStringLiteral("  —  ") + tip));
    QFont f = item->font(ColTitle);
    f.setStrikeOut(false);
    item->setFont(ColTitle, f);
    item->setForeground(ColTitle, e.progress.completed ? Colors::textMuted : Colors::text);
}

void EpisodeManagerView::applySeasonSummary(QTreeWidgetItem* si, int total, int watched, const Season& season)
{
    si->setData(0, RoleKind, SeasonItem);
    si->setData(0, RoleId, season.id);
    si->setData(0, RoleSeasonNumber, season.number);
    si->setData(0, RoleFraction, total > 0 ? double(watched) / total : 0.0);
    si->setData(0, RoleWatched, total > 0 && watched >= total);
    si->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    si->setText(ColNumber, season.number > 0 ? QString::number(season.number) : QString());
    si->setTextAlignment(ColNumber, Qt::AlignRight | Qt::AlignVCenter);
    si->setText(ColTitle, QStringLiteral("%1  ·  %2 episode%3").arg(season.title).arg(total).arg(total == 1 ? QString() : QStringLiteral("s")));
    QFont bold = si->font(ColTitle);
    bold.setBold(true);
    si->setFont(ColTitle, bold);
    si->setFont(ColNumber, bold);
    si->setToolTip(ColTitle, QStringLiteral("Season: drag to reorder seasons, or drop episodes onto it. Right-click for more."));
}

void EpisodeManagerView::reload()
{
    if (m_showId == kInvalidId) {
        m_tree->clear();
        return;
    }
    // remember UI state
    QSet<Id> collapsed;
    QSet<Id> selected;
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* si = m_tree->topLevelItem(s);
        if (!si->isExpanded()) collapsed.insert(si->data(0, RoleId).toLongLong());
        if (si->isSelected()) selected.insert(si->data(0, RoleId).toLongLong());
        for (int c = 0; c < si->childCount(); ++c)
            if (si->child(c)->isSelected()) selected.insert(si->child(c)->data(0, RoleId).toLongLong() + (Id(1) << 40));
    }
    const int scroll = m_tree->verticalScrollBar()->value();
    const bool wasLoaded = m_loaded;

    QSignalBlocker block(m_tree);
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();

    auto& lib = m_app.library();
    const QVector<Season> seasons = lib.seasons(m_showId);
    const QVector<Episode> episodes = lib.episodes(m_showId);
    for (const Season& season : seasons) {
        auto* si = new QTreeWidgetItem(m_tree);
        int total = 0;
        int watched = 0;
        for (const Episode& e : episodes) {
            if (e.seasonId != season.id) continue;
            auto* ci = new QTreeWidgetItem(si);
            applyEpisodeItem(ci, e);
            ++total;
            if (e.progress.completed) ++watched;
            if (selected.contains(e.id + (Id(1) << 40))) ci->setSelected(true);
        }
        applySeasonSummary(si, total, watched, season);
        si->setFirstColumnSpanned(false);
        si->setExpanded(!wasLoaded || !collapsed.contains(season.id));
        if (selected.contains(season.id)) si->setSelected(true);
    }
    m_loaded = true;
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(scroll);
    updateButtons();
}

void EpisodeManagerView::expandAll()
{
    m_tree->expandAll();
}

QTreeWidgetItem* EpisodeManagerView::findEpisodeByMedia(Id mediaFileId) const
{
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* si = m_tree->topLevelItem(s);
        for (int c = 0; c < si->childCount(); ++c)
            if (si->child(c)->data(0, RoleMediaId).toLongLong() == mediaFileId) return si->child(c);
    }
    return nullptr;
}

void EpisodeManagerView::refreshProgress(Id mediaFileId)
{
    QTreeWidgetItem* item = findEpisodeByMedia(mediaFileId);
    if (!item) return;
    const auto e = m_app.library().episode(item->data(0, RoleId).toLongLong());
    if (!e) return;
    QSignalBlocker block(m_tree);
    applyEpisodeItem(item, *e);
    // refresh the season summary row as well
    if (QTreeWidgetItem* si = item->parent()) {
        int total = si->childCount();
        int watched = 0;
        for (int c = 0; c < total; ++c)
            if (si->child(c)->data(0, RoleWatched).toBool()) ++watched;
        const auto season = m_app.library().season(si->data(0, RoleId).toLongLong());
        if (season) applySeasonSummary(si, total, watched, *season);
    }
    m_tree->viewport()->update();
}

void EpisodeManagerView::setNowPlaying(Id mediaFileId)
{
    const Id previous = m_nowPlayingMedia;
    m_nowPlayingMedia = mediaFileId;
    if (previous != kInvalidId) refreshProgress(previous);
    if (mediaFileId != kInvalidId) refreshProgress(mediaFileId);
}

Id EpisodeManagerView::selectedSeasonId() const
{
    const auto items = m_tree->selectedItems();
    for (QTreeWidgetItem* it : items) {
        if (it->data(0, RoleKind).toInt() == SeasonItem) return it->data(0, RoleId).toLongLong();
        if (it->parent()) return it->parent()->data(0, RoleId).toLongLong();
    }
    if (m_tree->topLevelItemCount() > 0) return m_tree->topLevelItem(m_tree->topLevelItemCount() - 1)->data(0, RoleId).toLongLong();
    return kInvalidId;
}

QList<QTreeWidgetItem*> EpisodeManagerView::selectedEpisodeItems() const
{
    QList<QTreeWidgetItem*> out;
    // tree order, not click order
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s)
        for (int c = 0; c < m_tree->topLevelItem(s)->childCount(); ++c)
            if (m_tree->topLevelItem(s)->child(c)->isSelected()) out << m_tree->topLevelItem(s)->child(c);
    return out;
}

QList<Id> EpisodeManagerView::selectedEpisodeIds() const
{
    QList<Id> ids;
    for (QTreeWidgetItem* it : selectedEpisodeItems()) ids << it->data(0, RoleId).toLongLong();
    return ids;
}

QList<Id> EpisodeManagerView::selectedMediaIds() const
{
    QList<Id> ids;
    for (QTreeWidgetItem* it : selectedEpisodeItems()) ids << it->data(0, RoleMediaId).toLongLong();
    return ids;
}

void EpisodeManagerView::updateButtons()
{
    const bool any = !selectedEpisodeItems().isEmpty();
    m_up->setEnabled(any);
    m_down->setEnabled(any);
    m_moveTo->setEnabled(any);
    m_watched->setEnabled(any);
    m_unwatched->setEnabled(any);

    auto* menu = new QMenu(m_moveTo);
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* si = m_tree->topLevelItem(s);
        const Id seasonId = si->data(0, RoleId).toLongLong();
        QString title = si->text(ColTitle);
        title = title.left(title.indexOf(QStringLiteral("  ·  ")));
        menu->addAction(title, this, [this, seasonId] { moveSelectionToSeason(seasonId); });
    }
    menu->addSeparator();
    menu->addAction(Theme::icon(Icon::Add, Colors::text, 16), QStringLiteral("New season…"), this, [this] { emit newSeasonRequested(); });
    if (QMenu* old = m_moveTo->menu()) old->deleteLater();
    m_moveTo->setMenu(menu);
}

// ---------------------------------------------------------------- editing

void EpisodeManagerView::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (item->data(0, RoleKind).toInt() != EpisodeItem) return;
    const Id id = item->data(0, RoleId).toLongLong();
    const auto current = m_app.library().episode(id);
    if (!current) return;
    QString title = current->title;
    int number = current->episodeNumber;
    if (column == ColTitle) {
        title = item->text(ColTitle).trimmed();
    } else if (column == ColNumber) {
        bool ok = false;
        const QString t = item->text(ColNumber).trimmed();
        number = t.isEmpty() ? 0 : t.toInt(&ok);
        if (!t.isEmpty() && (!ok || number < 0)) number = current->episodeNumber;
    } else {
        return;
    }
    if (!m_app.library().updateEpisode(id, title, number)) reload();  // invalid input: show the stored values again
    else reload();
}

void EpisodeManagerView::renameCurrent()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item) return;
    if (item->data(0, RoleKind).toInt() == SeasonItem)
        m_actions->editSeason(item->data(0, RoleId).toLongLong());
    else
        m_tree->editItem(item, ColTitle);
}

void EpisodeManagerView::onItemActivated(QTreeWidgetItem* item)
{
    if (!item) return;
    if (item->data(0, RoleKind).toInt() == EpisodeItem) {
        emit playEpisodeRequested(item->data(0, RoleMediaId).toLongLong());
    } else if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
    }
}

void EpisodeManagerView::moveSelection(int delta)
{
    const QList<QTreeWidgetItem*> items = selectedEpisodeItems();
    if (items.isEmpty()) return;
    QSet<Id> selected;
    for (QTreeWidgetItem* it : items) selected.insert(it->data(0, RoleId).toLongLong());

    QVector<SeasonLayout> layout = layoutFromTree();
    auto locate = [&](Id id, int& s, int& i) {
        for (s = 0; s < layout.size(); ++s)
            for (i = 0; i < layout[s].episodeIds.size(); ++i)
                if (layout[s].episodeIds[i] == id) return true;
        return false;
    };
    QList<Id> order = selectedEpisodeIds();
    if (delta > 0) std::reverse(order.begin(), order.end());
    bool changed = false;
    for (Id id : order) {
        int s = 0;
        int i = 0;
        if (!locate(id, s, i)) continue;
        QVector<Id>& list = layout[s].episodeIds;
        if (delta < 0) {
            if (i > 0 && !selected.contains(list[i - 1])) {
                std::swap(list[i], list[i - 1]);
                changed = true;
            } else if (i == 0 && s > 0) {  // leave the top of this season: end of the previous one
                list.removeAt(0);
                layout[s - 1].episodeIds.push_back(id);
                changed = true;
            }
        } else {
            if (i < list.size() - 1 && !selected.contains(list[i + 1])) {
                std::swap(list[i], list[i + 1]);
                changed = true;
            } else if (i == list.size() - 1 && s < layout.size() - 1) {
                list.removeLast();
                layout[s + 1].episodeIds.push_front(id);
                changed = true;
            }
        }
    }
    if (!changed) return;
    m_app.library().applyLayout(m_showId, layout);
    reload();  // synchronous: a quick second click must see the order this one produced
    // keep the moved episodes selected after the reload
    const QSet<Id>& keep = selected;
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s)
        for (int c = 0; c < m_tree->topLevelItem(s)->childCount(); ++c) {
            QTreeWidgetItem* it = m_tree->topLevelItem(s)->child(c);
            it->setSelected(keep.contains(it->data(0, RoleId).toLongLong()));
        }
}

void EpisodeManagerView::moveSelectionToSeason(Id seasonId)
{
    const QList<Id> ids = selectedEpisodeIds();
    if (ids.isEmpty()) return;
    QVector<SeasonLayout> layout = layoutFromTree();
    const QSet<Id> moving(ids.begin(), ids.end());
    for (SeasonLayout& sl : layout) {
        QVector<Id> kept;
        for (Id id : sl.episodeIds)
            if (!moving.contains(id)) kept.push_back(id);
        sl.episodeIds = kept;
    }
    for (SeasonLayout& sl : layout)
        if (sl.seasonId == seasonId)
            for (Id id : ids) sl.episodeIds.push_back(id);
    m_app.library().applyLayout(m_showId, layout);
    reload();
}

void EpisodeManagerView::setWatchedForSelection(bool watched)
{
    QVector<Id> media;
    for (Id id : selectedMediaIds()) media.push_back(id);
    if (!media.isEmpty()) m_app.library().setWatched(media, watched);
}

void EpisodeManagerView::resetSelection()
{
    QVector<Id> media;
    for (Id id : selectedMediaIds()) media.push_back(id);
    if (!media.isEmpty()) m_app.library().resetProgress(media);
}

void EpisodeManagerView::removeSelection()
{
    const QList<Id> ids = selectedEpisodeIds();
    if (ids.isEmpty()) return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Remove from library"));
    box.setText(QStringLiteral("Remove %1 episode%2 from this show?").arg(ids.size()).arg(ids.size() == 1 ? QString() : QStringLiteral("s")));
    box.setInformativeText(QStringLiteral("Their watch progress is removed from LocalFlix.\n\nYour video files are NOT deleted or moved."));
    QPushButton* remove = box.addButton(QStringLiteral("Remove from library"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() != remove) return;
    QVector<Id> vec;
    for (Id id : ids) vec.push_back(id);
    m_app.library().removeEpisodes(vec);
}

void EpisodeManagerView::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* clicked = m_tree->itemAt(pos);
    if (clicked && !clicked->isSelected()) {
        m_tree->clearSelection();
        clicked->setSelected(true);
        m_tree->setCurrentItem(clicked);
    }
    QMenu menu(this);
    const QList<QTreeWidgetItem*> episodes = selectedEpisodeItems();

    if (!clicked) {
        const Id season = selectedSeasonId();
        menu.addAction(Theme::icon(Icon::Add, Colors::text, 18), QStringLiteral("Add files…"), this, [this, season] { emit addFilesRequested(season); });
        menu.addAction(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Add folder…"), this, [this, season] { emit addFolderRequested(season); });
        menu.addAction(QStringLiteral("New season…"), this, [this] { emit newSeasonRequested(); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }

    if (clicked->data(0, RoleKind).toInt() == SeasonItem) {
        const Id seasonId = clicked->data(0, RoleId).toLongLong();
        const int idx = m_tree->indexOfTopLevelItem(clicked);
        if (clicked->childCount() > 0) {
            const Id first = clicked->child(0)->data(0, RoleMediaId).toLongLong();
            menu.addAction(Theme::icon(Icon::Play, Colors::text, 18), QStringLiteral("Play this season"), this, [this, first] { emit playSeasonRequested(first); });
            menu.addSeparator();
        }
        menu.addAction(Theme::icon(Icon::Add, Colors::text, 18), QStringLiteral("Add files to this season…"), this, [this, seasonId] { emit addFilesRequested(seasonId); });
        menu.addAction(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Add folder to this season…"), this, [this, seasonId] { emit addFolderRequested(seasonId); });
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Rename / renumber season…"), this, [this, seasonId] { m_actions->editSeason(seasonId); });
        QAction* up = menu.addAction(QStringLiteral("Move season up"), this, [this, seasonId, idx] { m_app.library().moveSeason(seasonId, idx - 1); });
        up->setEnabled(idx > 0);
        QAction* down = menu.addAction(QStringLiteral("Move season down"), this, [this, seasonId, idx] { m_app.library().moveSeason(seasonId, idx + 1); });
        down->setEnabled(idx < m_tree->topLevelItemCount() - 1);
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove season from library…"), this, [this, seasonId, clicked] {
            const int n = clicked->childCount();
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(QStringLiteral("Remove season"));
            box.setText(QStringLiteral("Remove this season and its %1 episode(s) from the library?").arg(n));
            box.setInformativeText(QStringLiteral("Your video files are NOT deleted or moved."));
            QPushButton* ok = box.addButton(QStringLiteral("Remove from library"), QMessageBox::DestructiveRole);
            QPushButton* cancel = box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(cancel);
            box.exec();
            if (box.clickedButton() == ok) m_app.library().deleteSeason(seasonId);
        });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }

    const bool single = episodes.size() == 1;
    QTreeWidgetItem* first = episodes.isEmpty() ? clicked : episodes.first();
    const Id mediaId = first->data(0, RoleMediaId).toLongLong();
    const bool missing = first->data(0, RoleMissing).toBool();
    if (single) {
        menu.addAction(Theme::icon(Icon::Play, Colors::text, 18), QStringLiteral("Play"), this, [this, mediaId] { emit playEpisodeRequested(mediaId); });
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Rename…  (F2)"), this, [this, first] { m_tree->editItem(first, ColTitle); });
        menu.addAction(QStringLiteral("Set episode number…"), this, [this, first] { m_tree->editItem(first, ColNumber); });
    }
    QMenu* moveMenu = menu.addMenu(QStringLiteral("Move to season"));
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* si = m_tree->topLevelItem(s);
        const Id seasonId = si->data(0, RoleId).toLongLong();
        QString title = si->text(ColTitle);
        title = title.left(title.indexOf(QStringLiteral("  ·  ")));
        moveMenu->addAction(title, this, [this, seasonId] { moveSelectionToSeason(seasonId); });
    }
    menu.addAction(QStringLiteral("Move up   (Ctrl+↑)"), this, [this] { moveSelection(-1); });
    menu.addAction(QStringLiteral("Move down (Ctrl+↓)"), this, [this] { moveSelection(+1); });
    menu.addSeparator();
    menu.addAction(Theme::icon(Icon::Check, Colors::text, 18), QStringLiteral("Mark as watched"), this, [this] { setWatchedForSelection(true); });
    menu.addAction(QStringLiteral("Mark as unwatched"), this, [this] { setWatchedForSelection(false); });
    menu.addAction(Theme::icon(Icon::Replay, Colors::text, 18), QStringLiteral("Reset progress"), this, [this] { resetSelection(); });
    menu.addSeparator();
    if (single && missing)
        menu.addAction(Theme::icon(Icon::Warning, Colors::warning, 18), QStringLiteral("Locate missing file…"), this, [this, mediaId] { m_actions->locateMissing(mediaId); });
    if (single)
        menu.addAction(QStringLiteral("Show file in Explorer"), this, [this, first] {
            const auto e = m_app.library().episode(first->data(0, RoleId).toLongLong());
            if (e) m_actions->revealInExplorer(e->file.path);
        });
    menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove from library…  (Del)"), this, [this] { removeSelection(); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

}  // namespace lf::ui
