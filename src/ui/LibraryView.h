#pragma once

#include "ApplicationController.h"

#include <QTimer>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace lf::ui {

class FlowLayout;
class LibraryActions;

// Grid page for TV Shows, Movies and Continue Watching: header with actions,
// filter box, reflowing card grid and a helpful empty state. Accepts dropped
// files/folders (movies: imported as movies; shows: one new show per folder).
class LibraryView : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Shows, Movies, Continue };

    LibraryView(ApplicationController& app, LibraryActions* actions, Mode mode, QWidget* parent = nullptr);
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void markDirty();
    void applyFilter();
    void buildEmptyState();

    ApplicationController& m_app;
    LibraryActions* m_actions;
    Mode m_mode;
    bool m_dirty = true;
    QTimer m_timer;

    QLabel* m_title;
    QLabel* m_count;
    QHBoxLayout* m_toolbar;
    QLineEdit* m_filter;
    QScrollArea* m_scroll;
    QWidget* m_gridHost;
    FlowLayout* m_grid;
    QWidget* m_empty;
    QLabel* m_emptyTitle;
    QLabel* m_emptyText;
    QList<QPair<QWidget*, QString>> m_cards;
};

}  // namespace lf::ui
