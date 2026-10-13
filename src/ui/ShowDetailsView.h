#pragma once

#include "ApplicationController.h"
#include "Types.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QToolButton;

namespace lf::ui {

class EpisodeManagerView;
class LibraryActions;

class ShowDetailsView : public QWidget {
    Q_OBJECT
public:
    ShowDetailsView(ApplicationController& app, LibraryActions* actions, QWidget* parent = nullptr);

    void setShow(Id showId);
    Id showId() const { return m_showId; }
    void refreshPage();

signals:
    void backRequested();
    void playRequested(lf::QueueContext context, lf::Id mediaFileId, lf::StartMode mode);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void scheduleRefresh();
    void onProgressChanged(lf::Id mediaFileId);
    void onNowPlaying(lf::QueueEntry entry, bool hasPrevious, bool hasNext);
    void onStateChanged(lf::PlaybackState state);

private:
    void buildUi();
    void updateHeader();
    void onContinueClicked();
    void showMoreMenu();

    ApplicationController& m_app;
    LibraryActions* m_actions;
    Id m_showId = kInvalidId;
    ResumeTarget m_target;
    QTimer m_refreshTimer;

    QLabel* m_cover;
    QLabel* m_title;
    QLabel* m_stats;
    QLabel* m_description;
    QProgressBar* m_overall;
    QPushButton* m_continue;
    QLabel* m_continueHint;
    QPushButton* m_addFiles;
    QPushButton* m_addFolder;
    QPushButton* m_newSeason;
    QPushButton* m_edit;
    QToolButton* m_more;
    EpisodeManagerView* m_episodes;
    QScrollArea* m_scroll;
};

}  // namespace lf::ui
