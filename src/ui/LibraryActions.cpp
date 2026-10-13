#include "LibraryActions.h"

#include "Dialogs.h"
#include "FilenameParser.h"
#include "Format.h"
#include "Theme.h"
#include "Toast.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>

namespace lf::ui {

LibraryActions::LibraryActions(ApplicationController& app, QWidget* dialogParent, Toast* toast)
    : QObject(dialogParent), m_app(app), m_parent(dialogParent), m_toast(toast)
{
    connect(&m_app.importer(), &MediaImportService::scanProgress, this, &LibraryActions::onScanProgress);
    connect(&m_app.importer(), &MediaImportService::importFinished, this, &LibraryActions::onImportFinished);
}

// ---------------------------------------------------------------- helpers

QString LibraryActions::startDirectory() const
{
    const QString configured = m_app.settings().defaultImportDir();
    if (!configured.isEmpty() && QFileInfo(configured).isDir()) return configured;
    if (!m_lastDir.isEmpty() && QFileInfo(m_lastDir).isDir()) return m_lastDir;
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
}

QString LibraryActions::videoFilter() const
{
    QStringList patterns;
    for (const QString& e : MediaImportService::videoExtensions()) patterns << QStringLiteral("*.") + e;
    return QStringLiteral("Video files (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

bool LibraryActions::confirm(const QString& title, const QString& text, const QString& detail, const QString& acceptText)
{
    QMessageBox box(m_parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(text);
    box.setInformativeText(detail);
    QPushButton* accept = box.addButton(acceptText, QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == accept;
}

QStringList LibraryActions::localPathsFromUrls(const QList<QUrl>& urls) const
{
    QStringList out;
    for (const QUrl& u : urls)
        if (u.isLocalFile()) out << u.toLocalFile();
    return out;
}

void LibraryActions::trackImport(quint64 requestId, const QString& what, Id showId)
{
    m_pending.insert(requestId, {what, showId});
    m_toast->showMessage(QStringLiteral("Scanning for %1…").arg(what), Toast::Kind::Info, 60000);
}

// ---------------------------------------------------------------- creating & importing

Id LibraryActions::createShowInteractive()
{
    bool ok = false;
    const QString title = QInputDialog::getText(m_parent, QStringLiteral("New show"), QStringLiteral("Show title:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || title.isEmpty()) return kInvalidId;
    const Id id = m_app.library().createShow(title);
    if (id != kInvalidId) {
        m_toast->showMessage(QStringLiteral("Created “%1”. Add episodes with “Add files” or “Add folder”, or drop them here.").arg(title), Toast::Kind::Success);
        emit openShowRequested(id);
    }
    return id;
}

Id LibraryActions::createShowFromFolder(const QString& dir)
{
    const QString folderName = QFileInfo(dir).fileName();
    QString name = prettifyTitle(folderName);
    if (name.isEmpty()) name = folderName.isEmpty() ? QStringLiteral("Imported show") : folderName;
    const Id id = m_app.library().createShow(name);
    if (id != kInvalidId) importEpisodes(id, kInvalidId, {dir});
    return id;
}

void LibraryActions::importFolderAsShow()
{
    const QString dir = QFileDialog::getExistingDirectory(m_parent, QStringLiteral("Choose a folder to import as a show"), startDirectory());
    if (dir.isEmpty()) return;
    m_lastDir = QFileInfo(dir).absolutePath();
    const Id id = createShowFromFolder(dir);
    if (id != kInvalidId) emit openShowRequested(id);
}

void LibraryActions::importDroppedAsShows(const QStringList& paths)
{
    int created = 0;
    bool sawFiles = false;
    Id last = kInvalidId;
    for (const QString& p : paths) {
        if (QFileInfo(p).isDir()) {
            const Id id = createShowFromFolder(p);
            if (id != kInvalidId) {
                last = id;
                ++created;
            }
        } else {
            sawFiles = true;
        }
    }
    if (created == 1) emit openShowRequested(last);
    if (sawFiles && created == 0)
        m_toast->showMessage(QStringLiteral("Drop folders here to create shows. To add files to a show, open it and drop them there."), Toast::Kind::Info, 7000);
}

void LibraryActions::importEpisodesDialog(Id showId, Id seasonId, bool folder)
{
    QStringList paths;
    if (folder) {
        const QString dir = QFileDialog::getExistingDirectory(m_parent, QStringLiteral("Add a folder of episodes (subfolders included)"), startDirectory());
        if (!dir.isEmpty()) paths << dir;
    } else {
        paths = QFileDialog::getOpenFileNames(m_parent, QStringLiteral("Add episodes"), startDirectory(), videoFilter());
    }
    if (paths.isEmpty()) return;
    m_lastDir = QFileInfo(paths.first()).absolutePath();
    importEpisodes(showId, seasonId, paths);
}

void LibraryActions::importEpisodes(Id showId, Id seasonId, const QStringList& paths)
{
    if (paths.isEmpty()) return;
    const quint64 id = m_app.importer().importEpisodesAsync(showId, seasonId, paths);
    trackImport(id, QStringLiteral("episodes"), showId);
}

void LibraryActions::importMoviesDialog(bool folder)
{
    QStringList paths;
    if (folder) {
        const QString dir = QFileDialog::getExistingDirectory(m_parent, QStringLiteral("Add a folder of movies (subfolders included)"), startDirectory());
        if (!dir.isEmpty()) paths << dir;
    } else {
        paths = QFileDialog::getOpenFileNames(m_parent, QStringLiteral("Add movies"), startDirectory(), videoFilter());
    }
    if (paths.isEmpty()) return;
    m_lastDir = QFileInfo(paths.first()).absolutePath();
    importMovies(paths);
}

void LibraryActions::importMovies(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    const quint64 id = m_app.importer().importMoviesAsync(paths);
    trackImport(id, QStringLiteral("movies"), kInvalidId);
}

void LibraryActions::onScanProgress(quint64 requestId, int files)
{
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd()) return;
    m_toast->showMessage(QStringLiteral("Scanning for %1… %2 video files found").arg(it->what).arg(files), Toast::Kind::Info, 60000);
}

void LibraryActions::onImportFinished(quint64 requestId, ImportReport report)
{
    const PendingImport pending = m_pending.take(requestId);
    if (!report.ok) {
        m_toast->showMessage(report.error.isEmpty() ? QStringLiteral("Import failed.") : report.error, Toast::Kind::Error);
        return;
    }
    QString msg = QStringLiteral("Added %1 %2").arg(report.added).arg(pending.what.isEmpty() ? QStringLiteral("items") : pending.what);
    if (report.added == 1) msg.chop(1);  // "episodes" -> "episode"
    if (!report.createdSeasonIds.isEmpty()) msg += QStringLiteral(" · %1 new season(s)").arg(report.createdSeasonIds.size());
    if (!report.duplicates.isEmpty()) msg += QStringLiteral(" · %1 already in library").arg(report.duplicates.size());
    if (!report.rejected.isEmpty()) msg += QStringLiteral(" · %1 skipped").arg(report.rejected.size());
    m_toast->showMessage(msg, report.added > 0 ? Toast::Kind::Success : Toast::Kind::Info, 6000);
    if (!report.duplicates.isEmpty() || !report.rejected.isEmpty() || report.added == 0) showImportReport(report);
}

void LibraryActions::showImportReport(const ImportReport& report)
{
    ImportReportDialog dlg(report, m_parent);
    dlg.exec();
}

// ---------------------------------------------------------------- editing & deleting

void LibraryActions::editShow(Id showId)
{
    auto& lib = m_app.library();
    const auto show = lib.show(showId);
    if (!show) return;
    EntityEditDialog dlg(QStringLiteral("Edit show"), show->title, show->description, lib.coverPath(show->coverFile), QStringLiteral("cover"), m_parent);
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.title() != show->title) lib.renameShow(showId, dlg.title());
    if (dlg.description() != show->description) lib.setShowDescription(showId, dlg.description());
    if (!dlg.newImagePath().isEmpty())
        lib.setShowCover(showId, dlg.newImagePath());
    else if (dlg.imageRemoved())
        lib.clearShowCover(showId);
}

void LibraryActions::editMovie(Id movieId)
{
    auto& lib = m_app.library();
    const auto movie = lib.movie(movieId);
    if (!movie) return;
    EntityEditDialog dlg(QStringLiteral("Edit movie"), movie->title, movie->description, lib.coverPath(movie->posterFile), QStringLiteral("poster"), m_parent);
    if (dlg.exec() != QDialog::Accepted) return;
    lib.updateMovie(movieId, dlg.title(), dlg.description());
    if (!dlg.newImagePath().isEmpty())
        lib.setMoviePoster(movieId, dlg.newImagePath());
    else if (dlg.imageRemoved())
        lib.clearMoviePoster(movieId);
}

void LibraryActions::editSeason(Id seasonId)
{
    auto& lib = m_app.library();
    const auto season = lib.season(seasonId);
    if (!season) return;
    SeasonDialog dlg(QStringLiteral("Edit season"), season->title, season->number, m_parent);
    if (dlg.exec() != QDialog::Accepted) return;
    lib.updateSeason(seasonId, dlg.title(), dlg.number());
}

void LibraryActions::deleteShow(Id showId)
{
    auto& lib = m_app.library();
    const auto show = lib.show(showId);
    if (!show) return;
    const int episodes = int(lib.episodes(showId).size());
    if (!confirm(QStringLiteral("Remove show from library"),
                 QStringLiteral("Remove “%1” from your library?").arg(show->title),
                 QStringLiteral("Its %1 episode(s) and their watch progress will be removed from LocalFlix.\n\nYour video files are NOT deleted or moved.").arg(episodes),
                 QStringLiteral("Remove from library")))
        return;
    if (lib.deleteShow(showId)) m_toast->showMessage(QStringLiteral("Removed “%1” from the library. Video files were left untouched.").arg(show->title));
}

void LibraryActions::deleteMovie(Id movieId)
{
    auto& lib = m_app.library();
    const auto movie = lib.movie(movieId);
    if (!movie) return;
    if (!confirm(QStringLiteral("Remove movie from library"),
                 QStringLiteral("Remove “%1” from your library?").arg(movie->title),
                 QStringLiteral("Its watch progress will be removed from LocalFlix.\n\nThe video file is NOT deleted or moved."),
                 QStringLiteral("Remove from library")))
        return;
    if (lib.deleteMovie(movieId)) m_toast->showMessage(QStringLiteral("Removed “%1” from the library. The video file was left untouched.").arg(movie->title));
}

void LibraryActions::addToCollection(CollectionItemType type, Id itemId)
{
    auto& lib = m_app.library();
    const QVector<Collection> collections = lib.collections();
    const QString newLabel = QStringLiteral("＋ New collection…");
    QStringList names;
    for (const Collection& c : collections) names << c.name;
    names << newLabel;

    bool ok = false;
    const QString choice = collections.isEmpty()
                               ? newLabel
                               : QInputDialog::getItem(m_parent, QStringLiteral("Add to collection"), QStringLiteral("Collection:"), names, 0, false, &ok);
    if (!collections.isEmpty() && (!ok || choice.isEmpty())) return;

    Id collectionId = kInvalidId;
    QString collectionName;
    if (choice == newLabel) {
        bool nameOk = false;
        const QString name = QInputDialog::getText(m_parent, QStringLiteral("New collection"), QStringLiteral("Collection name:"), QLineEdit::Normal, QString(), &nameOk).trimmed();
        if (!nameOk || name.isEmpty()) return;
        collectionId = lib.createCollection(name);
        collectionName = name;
    } else {
        for (const Collection& c : collections)
            if (c.name == choice) {
                collectionId = c.id;
                collectionName = c.name;
            }
    }
    if (collectionId == kInvalidId) return;
    if (lib.addToCollection(collectionId, type, itemId))
        m_toast->showMessage(QStringLiteral("Added to “%1”.").arg(collectionName), Toast::Kind::Success);
    else
        m_toast->showMessage(QStringLiteral("Already in “%1”.").arg(collectionName));
}

void LibraryActions::setShowWatched(Id showId, bool watched)
{
    QVector<Id> media;
    for (const Episode& e : m_app.library().episodes(showId)) media << e.file.id;
    m_app.library().setWatched(media, watched);
}

void LibraryActions::resetShowProgress(Id showId)
{
    const auto show = m_app.library().show(showId);
    if (!show) return;
    if (!confirm(QStringLiteral("Reset progress"), QStringLiteral("Reset watch progress for “%1”?").arg(show->title),
                 QStringLiteral("Every episode becomes unwatched and playback positions are cleared. Files and order are kept."), QStringLiteral("Reset progress")))
        return;
    m_app.library().resetShowProgress(showId);
    m_toast->showMessage(QStringLiteral("Progress reset for “%1”.").arg(show->title));
}

void LibraryActions::setMovieWatched(Id movieId, bool watched)
{
    if (const auto m = m_app.library().movie(movieId)) m_app.library().setWatched({m->file.id}, watched);
}

void LibraryActions::resetMovieProgress(Id movieId)
{
    if (const auto m = m_app.library().movie(movieId)) {
        m_app.library().resetProgress({m->file.id});
        m_toast->showMessage(QStringLiteral("Progress reset for “%1”.").arg(m->title));
    }
}

void LibraryActions::revealInExplorer(const QString& path)
{
    const QFileInfo fi(path);
    if (fi.exists()) {
        QProcess p;
        p.setProgram(QStringLiteral("explorer.exe"));
        p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fi.absoluteFilePath())));
        p.startDetached();
    } else if (fi.absoluteDir().exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    } else {
        m_toast->showMessage(QStringLiteral("That folder no longer exists."), Toast::Kind::Error);
    }
}

// ---------------------------------------------------------------- recovery

void LibraryActions::locateMissing(Id mediaFileId)
{
    auto& lib = m_app.library();
    const auto file = lib.mediaFile(mediaFileId);
    if (!file) return;

    const QFileInfo old(file->path);
    QString startDir = old.absoluteDir().exists() ? old.absolutePath() : startDirectory();
    const QString picked = QFileDialog::getOpenFileName(
        m_parent, QStringLiteral("Locate “%1”").arg(old.fileName()), startDir, videoFilter());
    if (picked.isEmpty()) return;
    m_lastDir = QFileInfo(picked).absolutePath();

    const RelinkResult r = lib.relinkFile(mediaFileId, picked);
    if (!r.ok) {
        QMessageBox::warning(m_parent, QStringLiteral("Locate file"), r.error);
        return;
    }
    m_toast->showMessage(QStringLiteral("Reconnected “%1”. Progress and order were kept.").arg(QFileInfo(picked).fileName()), Toast::Kind::Success);

    // Other files that moved the same way (same folder, or the whole tree)?
    const QVector<RelinkProposal> proposals = lib.proposeRelinks(file->path, LibraryManager::normalizePath(picked));
    if (!proposals.isEmpty()) {
        QMessageBox box(m_parent);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("More files found"));
        box.setText(QStringLiteral("%1 other missing file(s) were found in the same new location.").arg(proposals.size()));
        box.setInformativeText(QStringLiteral("Reconnect them too? Their progress and order are kept."));
        QStringList details;
        for (const RelinkProposal& p : proposals)
            details << QStringLiteral("%1\n  → %2").arg(QDir::toNativeSeparators(p.oldPath), QDir::toNativeSeparators(p.newPath));
        box.setDetailedText(details.join(QLatin1Char('\n')));
        QPushButton* yes = box.addButton(QStringLiteral("Reconnect %1 files").arg(proposals.size()), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Not now"), QMessageBox::RejectRole);
        box.setDefaultButton(yes);
        box.exec();
        if (box.clickedButton() == yes) {
            const int n = lib.applyRelinks(proposals);
            m_toast->showMessage(QStringLiteral("Reconnected %1 more file(s).").arg(n), Toast::Kind::Success);
        }
    }
    emit fileRelinked(mediaFileId);
}

// ---------------------------------------------------------------- menus

void LibraryActions::showCardMenu(CardKind kind, Id id, const QPoint& globalPos)
{
    QMenu menu(m_parent);
    auto& lib = m_app.library();

    if (kind == CardKind::Show) {
        const auto show = lib.show(id);
        if (!show) return;
        const ResumeTarget target = lib.resumeTarget(id);
        menu.addAction(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Open"), this, [this, id] { emit openShowRequested(id); });
        QAction* play = menu.addAction(Theme::icon(Icon::Play, Colors::text, 18),
                                       target.allCompleted() ? QStringLiteral("Watch again") : (target.reason == ResumeReason::InProgress ? QStringLiteral("Continue watching") : QStringLiteral("Play")),
                                       this, [this, id] { emit playShowRequested(id); });
        play->setEnabled(target.valid());
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Edit details…"), this, [this, id] { editShow(id); });
        menu.addAction(Theme::icon(Icon::Collections, Colors::text, 18), QStringLiteral("Add to collection…"), this,
                       [this, id] { addToCollection(CollectionItemType::Show, id); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Mark all as watched"), this, [this, id] { setShowWatched(id, true); });
        menu.addAction(QStringLiteral("Mark all as unwatched"), this, [this, id] { setShowWatched(id, false); });
        menu.addAction(Theme::icon(Icon::Replay, Colors::text, 18), QStringLiteral("Reset progress…"), this, [this, id] { resetShowProgress(id); });
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove from library…"), this, [this, id] { deleteShow(id); });
    } else {
        const auto movie = lib.movie(id);
        if (!movie) return;
        menu.addAction(Theme::icon(Icon::Play, Colors::text, 18), movie->progress.started() ? QStringLiteral("Resume") : QStringLiteral("Play"), this,
                       [this, id] { emit playMovieRequested(id); });
        if (movie->file.missing)
            menu.addAction(Theme::icon(Icon::Warning, Colors::warning, 18), QStringLiteral("Locate missing file…"), this,
                           [this, mid = movie->file.id] { locateMissing(mid); });
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Edit, Colors::text, 18), QStringLiteral("Edit details…"), this, [this, id] { editMovie(id); });
        menu.addAction(Theme::icon(Icon::Collections, Colors::text, 18), QStringLiteral("Add to collection…"), this,
                       [this, id] { addToCollection(CollectionItemType::Movie, id); });
        menu.addSeparator();
        if (movie->progress.completed)
            menu.addAction(QStringLiteral("Mark as unwatched"), this, [this, id] { setMovieWatched(id, false); });
        else
            menu.addAction(QStringLiteral("Mark as watched"), this, [this, id] { setMovieWatched(id, true); });
        menu.addAction(Theme::icon(Icon::Replay, Colors::text, 18), QStringLiteral("Reset progress"), this, [this, id] { resetMovieProgress(id); });
        menu.addAction(QStringLiteral("Show file in Explorer"), this, [this, path = movie->file.path] { revealInExplorer(path); });
        menu.addSeparator();
        menu.addAction(Theme::icon(Icon::Delete, Colors::danger, 18), QStringLiteral("Remove from library…"), this, [this, id] { deleteMovie(id); });
    }
    menu.exec(globalPos);
}

void LibraryActions::continueMenu(const ContinueItem& item, const QPoint& globalPos)
{
    QMenu menu(m_parent);
    const QueueContext ctx = item.kind == MediaKind::Movie ? QueueContext{ContextType::Movie, item.movieId} : QueueContext{ContextType::Show, item.showId};
    if (item.missing) {
        menu.addAction(Theme::icon(Icon::Warning, Colors::warning, 18), QStringLiteral("Locate missing file…"), this,
                       [this, mid = item.mediaFileId] { locateMissing(mid); });
    } else {
        menu.addAction(Theme::icon(Icon::Play, Colors::text, 18), item.positionMs > 0 ? QStringLiteral("Resume") : QStringLiteral("Play"), this,
                       [this, ctx, item] { emit playMediaRequested(ctx, item.mediaFileId, StartMode::Resume); });
        if (item.positionMs > 0)
            menu.addAction(Theme::icon(Icon::Replay, Colors::text, 18), QStringLiteral("Start from the beginning"), this,
                           [this, ctx, item] { emit playMediaRequested(ctx, item.mediaFileId, StartMode::FromStart); });
    }
    if (item.kind == MediaKind::Episode)
        menu.addAction(Theme::icon(Icon::Folder, Colors::text, 18), QStringLiteral("Open show"), this, [this, item] { emit openShowRequested(item.showId); });
    menu.exec(globalPos);
}

}  // namespace lf::ui
