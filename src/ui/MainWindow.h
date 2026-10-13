#pragma once

#include "ApplicationController.h"
#include "HomeView.h"
#include "Types.h"

#include <QMainWindow>

class QButtonGroup;
class QFrame;
class QStackedWidget;
class QToolButton;

namespace lf {
class MpvBackend;
}

namespace lf::ui {

class CollectionsView;
class LibraryActions;
class LibraryView;
class PlayerView;
class SettingsView;
class ShowDetailsView;
class Toast;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(ApplicationController& app, MpvBackend* backend, QWidget* parent = nullptr);
    ~MainWindow() override;

    void navigateTo(Page page);
    void openShow(Id showId);
    // Starts playback of `mediaFileId` inside `context` and shows the player.
    void startPlayback(const QueueContext& context, Id mediaFileId, StartMode mode);
    void setFullscreen(bool fullscreen);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onResumeDecisionNeeded(lf::QueueEntry entry, qint64 positionMs);

private:
    void buildUi();
    void enterPlayer();
    void leavePlayer();
    void playShow(Id showId);
    void playMovie(Id movieId);
    QWidget* pageWidget(Page page) const;

    ApplicationController& m_app;
    MpvBackend* m_backend;

    QFrame* m_sidebar;
    QStackedWidget* m_stack;
    QButtonGroup* m_navGroup;
    QToolButton* m_navButtons[6] = {};

    Toast* m_toast;
    LibraryActions* m_actions;
    HomeView* m_home;
    LibraryView* m_continue;
    LibraryView* m_shows;
    LibraryView* m_movies;
    CollectionsView* m_collections;
    SettingsView* m_settings;
    ShowDetailsView* m_details;
    PlayerView* m_player;

    QWidget* m_returnWidget = nullptr;          // where "back" from the player goes
    QWidget* m_detailsReturnWidget = nullptr;   // where "back" from a show page goes
    bool m_wasMaximized = false;
    bool m_darkTitleApplied = false;
};

}  // namespace lf::ui
