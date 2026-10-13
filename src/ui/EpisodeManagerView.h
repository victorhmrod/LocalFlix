#pragma once

#include "ApplicationController.h"
#include "Types.h"

#include <QTreeWidget>
#include <QWidget>

class QCheckBox;
class QLabel;
class QToolButton;

namespace lf::ui {

class LibraryActions;

// Tree of seasons and episodes with drag-and-drop reordering. The tree never
// rearranges itself: a drop is turned into a complete layout, applied through
// LibraryManager::applyLayout (one transaction) and the tree reloads from the
// database, so what you see is always what will play.
class EpisodeTree : public QTreeWidget {
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

signals:
    void episodesDropped(QList<lf::Id> episodeIds, lf::Id seasonId, int indexAmongOthers);
    void seasonDropped(lf::Id seasonId, int indexAmongOthers);
    void filesDropped(QStringList paths, lf::Id seasonId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

class EpisodeManagerView : public QWidget {
    Q_OBJECT
public:
    EpisodeManagerView(ApplicationController& app, LibraryActions* actions, QWidget* parent = nullptr);

    void setShow(Id showId);
    void reload();
    // Updates one row's progress without rebuilding the tree.
    void refreshProgress(Id mediaFileId);
    void setNowPlaying(Id mediaFileId);
    Id selectedSeasonId() const;  // season of the selection, or the last season
    void expandAll();

signals:
    void playEpisodeRequested(lf::Id mediaFileId);
    void playSeasonRequested(lf::Id firstMediaFileId);
    void addFilesRequested(lf::Id seasonId);
    void addFolderRequested(lf::Id seasonId);
    void filesDropped(QStringList paths, lf::Id seasonId);
    void newSeasonRequested();

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemActivated(QTreeWidgetItem* item);
    void onContextMenu(const QPoint& pos);
    void updateButtons();

private:
    enum Role { RoleKind = Qt::UserRole + 1, RoleId, RoleMediaId, RoleFraction, RoleWatched, RoleMissing, RoleSeasonNumber };
    enum Kind { SeasonItem = 0, EpisodeItem = 1 };
    enum Column { ColStatus = 0, ColNumber, ColTitle, ColProgress, ColDuration, ColFile, ColCount };

    QVector<SeasonLayout> layoutFromTree() const;
    QList<QTreeWidgetItem*> selectedEpisodeItems() const;
    QList<Id> selectedEpisodeIds() const;
    QList<Id> selectedMediaIds() const;
    void applyEpisodeItem(QTreeWidgetItem* item, const Episode& e);
    void applySeasonSummary(QTreeWidgetItem* seasonItem, int total, int watched, const Season& season);
    void moveSelection(int delta);
    void moveSelectionToSeason(Id seasonId);
    void removeSelection();
    void renameCurrent();
    void setWatchedForSelection(bool watched);
    void resetSelection();
    QTreeWidgetItem* findEpisodeByMedia(Id mediaFileId) const;

    ApplicationController& m_app;
    LibraryActions* m_actions;
    Id m_showId = kInvalidId;
    Id m_nowPlayingMedia = kInvalidId;
    bool m_loaded = false;

    EpisodeTree* m_tree;
    QToolButton* m_up;
    QToolButton* m_down;
    QToolButton* m_moveTo;
    QToolButton* m_watched;
    QToolButton* m_unwatched;
    QCheckBox* m_showPaths;
    QLabel* m_hint;
};

}  // namespace lf::ui
