#pragma once

#include "ApplicationController.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace lf::ui {

class LibraryActions;

// Collections: hand-made groupings of shows and movies with their own order.
// "Play collection" plays every item in that order, continuing automatically.
class CollectionsView : public QWidget {
    Q_OBJECT
public:
    CollectionsView(ApplicationController& app, LibraryActions* actions, QWidget* parent = nullptr);
    void refresh();

signals:
    void playRequested(lf::QueueContext context, lf::Id mediaFileId, lf::StartMode mode);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void markDirty();
    void reloadCollections();
    void reloadEntries();
    Id currentCollectionId() const;
    void createCollection();
    void renameCollection();
    void deleteCollection();
    void addShow();
    void addMovie();
    void removeSelectedEntry();
    void persistOrder();
    void playCollection();
    void activateEntry(QListWidgetItem* item);
    void entryMenu(const QPoint& pos);

    ApplicationController& m_app;
    LibraryActions* m_actions;
    bool m_dirty = true;
    bool m_loading = false;
    QTimer m_timer;

    QListWidget* m_collections;
    QListWidget* m_entries;
    QLabel* m_name;
    QLabel* m_summary;
    QPushButton* m_play;
    QPushButton* m_addShow;
    QPushButton* m_addMovie;
    QPushButton* m_rename;
    QPushButton* m_delete;
    QWidget* m_detail;
    QWidget* m_emptyState;
};

}  // namespace lf::ui
