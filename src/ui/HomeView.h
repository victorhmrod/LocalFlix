#pragma once

#include "ApplicationController.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace lf::ui {

class CardRow;
class LibraryActions;

enum class Page { Home, Continue, Shows, Movies, Collections, Settings };

// Landing screen: Continue Watching, Recently Added, TV Shows, Movies.
class HomeView : public QWidget {
    Q_OBJECT
public:
    HomeView(ApplicationController& app, LibraryActions* actions, QWidget* parent = nullptr);
    void refresh();

signals:
    void navigateRequested(lf::ui::Page page);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void markDirty();

    ApplicationController& m_app;
    LibraryActions* m_actions;
    bool m_dirty = true;
    QTimer m_timer;

    QScrollArea* m_scroll;
    QWidget* m_welcome;
    QWidget* m_sections;
    CardRow* m_continue;
    CardRow* m_recent;
    CardRow* m_shows;
    CardRow* m_movies;
};

}  // namespace lf::ui
