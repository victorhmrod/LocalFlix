#pragma once

#include "ApplicationController.h"
#include "LibraryManager.h"
#include "Types.h"

#include <QHash>
#include <QObject>

class QMenu;
class QWidget;

namespace lf::ui {

class Toast;

// Every interactive library operation (create, import, edit, delete, locate a
// missing file, add to a collection...) lives here so each screen offers the
// same actions with the same confirmations. Views only emit "user wants X".
class LibraryActions : public QObject {
    Q_OBJECT
public:
    LibraryActions(ApplicationController& app, QWidget* dialogParent, Toast* toast);

    // ---- creating ---------------------------------------------------------
    Id createShowInteractive();
    void importFolderAsShow();
    // Drag-and-drop onto the shows page: every dropped folder becomes a new show.
    void importDroppedAsShows(const QStringList& paths);
    void importEpisodesDialog(Id showId, Id seasonId, bool folder);
    void importEpisodes(Id showId, Id seasonId, const QStringList& paths);
    void importMoviesDialog(bool folder);
    void importMovies(const QStringList& paths);
    // Used by drag-and-drop: decides what the dropped files should become.
    QStringList localPathsFromUrls(const QList<QUrl>& urls) const;

    // ---- editing / deleting ----------------------------------------------
    void editShow(Id showId);
    void editMovie(Id movieId);
    void editSeason(Id seasonId);
    void deleteShow(Id showId);
    void deleteMovie(Id movieId);
    void addToCollection(CollectionItemType type, Id itemId);
    void setShowWatched(Id showId, bool watched);
    void resetShowProgress(Id showId);
    void setMovieWatched(Id movieId, bool watched);
    void resetMovieProgress(Id movieId);
    void revealInExplorer(const QString& path);

    // ---- recovery ---------------------------------------------------------
    void locateMissing(Id mediaFileId);

    // ---- menus ------------------------------------------------------------
    void showCardMenu(CardKind kind, Id id, const QPoint& globalPos);
    void continueMenu(const ContinueItem& item, const QPoint& globalPos);

    QString startDirectory() const;
    void showImportReport(const ImportReport& report);

signals:
    void openShowRequested(lf::Id showId);
    void playShowRequested(lf::Id showId);   // "Continue" for that show
    void playMovieRequested(lf::Id movieId);
    void playMediaRequested(lf::QueueContext context, lf::Id mediaFileId, lf::StartMode mode);
    void fileRelinked(lf::Id mediaFileId);

private slots:
    void onScanProgress(quint64 requestId, int files);
    void onImportFinished(quint64 requestId, lf::ImportReport report);

private:
    struct PendingImport {
        QString what;
        Id showId = kInvalidId;
    };
    QString videoFilter() const;
    void trackImport(quint64 requestId, const QString& what, Id showId);
    Id createShowFromFolder(const QString& dir);
    bool confirm(const QString& title, const QString& text, const QString& detail, const QString& acceptText);

    ApplicationController& m_app;
    QWidget* m_parent;
    Toast* m_toast;
    QHash<quint64, PendingImport> m_pending;
    QString m_lastDir;
};

}  // namespace lf::ui
