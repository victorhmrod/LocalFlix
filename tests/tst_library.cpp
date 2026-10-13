// Importing, stable identity, manual ordering, moving files, deleting entries.
#include "SqlUtil.h"
#include "TestSupport.h"

using namespace lf;
using namespace lf::test;

namespace {

QStringList titlesInOrder(const QVector<Episode>& eps)
{
    QStringList t;
    for (const Episode& e : eps) t << e.title;
    return t;
}

QVector<Id> idsInOrder(const QVector<Episode>& eps)
{
    QVector<Id> ids;
    for (const Episode& e : eps) ids << e.id;
    return ids;
}

QByteArray fileFingerprint(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1) + QByteArray::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
}

}  // namespace

class TstLibrary : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ importing

    void importCreatesEntriesAndLeavesFilesAlone()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("My Show"));
        QVERIFY(showId != kInvalidId);
        const QStringList paths = env.makeFiles({QStringLiteral("a/S01E01 - Pilot.mkv"), QStringLiteral("a/S01E02 - Second.mkv")});
        const QByteArray before0 = fileFingerprint(paths[0]);
        const QByteArray before1 = fileFingerprint(paths[1]);

        const ImportReport r = env.app->importer().importEpisodesSync(showId, kInvalidId, paths);
        QVERIFY(r.ok);
        QCOMPARE(r.added, 2);
        QVERIFY(r.duplicates.isEmpty());

        const auto eps = lib.episodes(showId);
        QCOMPARE(eps.size(), 2);
        QCOMPARE(eps[0].title, QStringLiteral("Pilot"));
        QCOMPARE(eps[0].episodeNumber, 1);
        QCOMPARE(eps[0].seasonNumber, 1);
        QCOMPARE(eps[1].title, QStringLiteral("Second"));
        QCOMPARE(eps[0].file.path, paths[0]);  // path is kept, not copied anywhere

        // the user's files are exactly as they were: same content, same mtime, still in place
        QCOMPARE(fileFingerprint(paths[0]), before0);
        QCOMPARE(fileFingerprint(paths[1]), before1);
        QVERIFY(QFileInfo::exists(paths[0]));
    }

    void importFolderRecursivelyAndDetectSeasons()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Series"));
        env.makeFile(QStringLiteral("Series/Season 1/ep01.mkv"));
        env.makeFile(QStringLiteral("Series/Season 1/ep02.mkv"));
        env.makeFile(QStringLiteral("Series/Season 2/ep01.mkv"));
        env.makeFile(QStringLiteral("Series/Season 2/deep/ep02.mkv"));
        env.makeFile(QStringLiteral("Series/notes.txt"));      // not a video
        env.makeFile(QStringLiteral("Series/cover.jpg"));      // not a video

        const ImportReport r = env.app->importer().importEpisodesSync(showId, kInvalidId, {QDir(env.mediaDir()).filePath(QStringLiteral("Series"))});
        QVERIFY(r.ok);
        QCOMPARE(r.added, 4);

        const auto seasons = lib.seasons(showId);
        // "Season 1" (created with the show) and "Season 2" (created by the importer), ascending
        QCOMPARE(seasons.size(), 2);
        QCOMPARE(seasons[0].number, 1);
        QCOMPARE(seasons[1].number, 2);
        const auto eps = lib.episodes(showId);
        QCOMPARE(eps.size(), 4);
        QCOMPARE(eps[0].seasonId, seasons[0].id);
        QCOMPARE(eps[1].seasonId, seasons[0].id);
        QCOMPARE(eps[2].seasonId, seasons[1].id);
        QCOMPARE(eps[3].seasonId, seasons[1].id);
    }

    void importOrdersByEpisodeNumberNotByFileSystemOrder()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Ordered"));
        const QStringList paths = env.makeFiles({QStringLiteral("o/Show - 10.mkv"), QStringLiteral("o/Show - 2.mkv"), QStringLiteral("o/Show - 1.mkv")});
        const ImportReport r = env.app->importer().importEpisodesSync(showId, kInvalidId, paths);
        QCOMPARE(r.added, 3);
        const auto eps = lib.episodes(showId);
        QCOMPARE(eps[0].episodeNumber, 1);
        QCOMPARE(eps[1].episodeNumber, 2);
        QCOMPARE(eps[2].episodeNumber, 10);
    }

    void duplicateImportIsSkippedAndReported()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showA = lib.createShow(QStringLiteral("Show A"));
        const Id showB = lib.createShow(QStringLiteral("Show B"));
        const QStringList paths = env.makeFiles({QStringLiteral("d/one.mkv"), QStringLiteral("d/two.mkv")});

        QCOMPARE(env.app->importer().importEpisodesSync(showA, kInvalidId, paths).added, 2);

        // same show again
        const ImportReport again = env.app->importer().importEpisodesSync(showA, kInvalidId, paths);
        QCOMPARE(again.added, 0);
        QCOMPARE(again.duplicates.size(), 2);
        QVERIFY2(again.duplicates[0].existingAs.contains(QStringLiteral("Show A")), qPrintable(again.duplicates[0].existingAs));
        QCOMPARE(lib.episodes(showA).size(), 2);

        // a different show cannot take the same file either: one file, one library entry
        const ImportReport other = env.app->importer().importEpisodesSync(showB, kInvalidId, {paths[0]});
        QCOMPARE(other.added, 0);
        QCOMPARE(other.duplicates.size(), 1);
        QCOMPARE(lib.episodes(showB).size(), 0);

        // partially new batch: only the new file is added
        const QString extra = env.makeFile(QStringLiteral("d/three.mkv"));
        const ImportReport mixed = env.app->importer().importEpisodesSync(showA, kInvalidId, {paths[0], extra});
        QCOMPARE(mixed.added, 1);
        QCOMPARE(mixed.duplicates.size(), 1);

        // same path spelled with different case / separators is still the same file (Windows)
        const QString shouty = QDir::toNativeSeparators(paths[0]).toUpper();
        const ImportReport shout = env.app->importer().importEpisodesSync(showA, kInvalidId, {shouty});
        QCOMPARE(shout.added, 0);
    }

    void importReportsMissingAndEmptyFiles()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Bad input"));
        const QString good = env.makeFile(QStringLiteral("g/good.mkv"));
        const QString empty = QDir(env.mediaDir()).filePath(QStringLiteral("g/empty.mkv"));
        QFile e(empty);
        QVERIFY(e.open(QIODevice::WriteOnly));
        e.close();
        const QString nothing = QDir(env.mediaDir()).filePath(QStringLiteral("g/does-not-exist.mkv"));

        const ImportReport r = env.app->importer().importEpisodesSync(showId, kInvalidId, {good, empty, nothing});
        QCOMPARE(r.added, 1);
        QCOMPARE(r.rejected.size(), 2);
    }

    void importAsyncDoesNotBlockAndReportsResult()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Async"));
        for (int i = 1; i <= 40; ++i) env.makeFile(QStringLiteral("async/ep%1.mkv").arg(i, 2, 10, QLatin1Char('0')));
        QSignalSpy spy(&env.app->importer(), &MediaImportService::importFinished);
        const quint64 id = env.app->importer().importEpisodesAsync(showId, kInvalidId, {QDir(env.mediaDir()).filePath(QStringLiteral("async"))});
        QVERIFY(id > 0);
        // the call returned before anything was imported: the work happens on the worker
        QCOMPARE(lib.episodes(showId).size(), 0);
        QVERIFY(spy.wait(10000));
        const auto report = spy.first().at(1).value<ImportReport>();
        QCOMPARE(report.added, 40);
        QCOMPARE(lib.episodes(showId).size(), 40);
    }

    void importMoviesAsSeparateEntries()
    {
        Env env;
        auto& lib = env.app->library();
        const QStringList paths = env.makeFiles({QStringLiteral("m/The.Matrix.1999.1080p.BluRay.x264.mkv"), QStringLiteral("m/some_movie.mp4")});
        const ImportReport r = env.app->importer().importMoviesSync(paths);
        QCOMPARE(r.added, 2);
        const auto movies = lib.movies();
        QCOMPARE(movies.size(), 2);
        QStringList titles;
        for (const Movie& m : movies) titles << m.title;
        QVERIFY2(titles.contains(QStringLiteral("The Matrix 1999")), qPrintable(titles.join(QLatin1Char('|'))));
        QVERIFY2(titles.contains(QStringLiteral("Some Movie")), qPrintable(titles.join(QLatin1Char('|'))));

        // duplicate movie import
        QCOMPARE(env.app->importer().importMoviesSync({paths[0]}).added, 0);
        // a movie file cannot also become an episode
        const Id showId = lib.createShow(QStringLiteral("X"));
        QCOMPARE(env.app->importer().importEpisodesSync(showId, kInvalidId, {paths[0]}).added, 0);
    }

    // ------------------------------------------------------------ manual order

    void manualOrderIsExactlyWhatTheUserConfigured()
    {
        // The spec's example: file names say nothing about the order the user wants.
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Mixed"));
        const QStringList paths = env.makeFiles({QStringLiteral("x/video_27.mkv"), QStringLiteral("x/downloaded_episode_04.mp4"),
                                                 QStringLiteral("x/anime_final.mkv"), QStringLiteral("x/episode_01.avi")});
        QCOMPARE(env.app->importer().importEpisodesSync(showId, kInvalidId, paths).added, 4);

        auto eps = lib.episodes(showId);
        QMap<QString, Id> byFile;
        for (const Episode& e : eps) byFile.insert(QFileInfo(e.file.path).fileName(), e.id);

        const Id seasonId = lib.seasons(showId).first().id;
        const QVector<Id> wanted = {byFile[QStringLiteral("anime_final.mkv")], byFile[QStringLiteral("video_27.mkv")],
                                    byFile[QStringLiteral("episode_01.avi")], byFile[QStringLiteral("downloaded_episode_04.mp4")]};
        QVERIFY(lib.applyLayout(showId, {{seasonId, wanted}}));
        QCOMPARE(idsInOrder(lib.episodes(showId)), wanted);

        // explicit sort_order values in the database, strictly increasing
        Stmt s(env.app->database().database(), QStringLiteral("SELECT id, sort_order FROM episodes ORDER BY sort_order"));
        s.run();
        int expected = 0;
        QVector<Id> dbOrder;
        while (s.next()) {
            QCOMPARE(s.i32(1), expected++);
            dbOrder << s.i64(0);
        }
        QCOMPARE(dbOrder, wanted);

        // and it is the playback queue
        env.app->queue().setContext(showContext(showId));
        QVector<Id> queued;
        for (const QueueEntry& q : env.app->queue().entries()) queued << q.itemId;
        QCOMPARE(queued, wanted);

        // survives an application restart
        env.restart();
        QCOMPARE(idsInOrder(env.app->library().episodes(showId)), wanted);
    }

    void reorderKeepsIdentitiesAndProgress()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Stable"), 4, &showId);
        auto& lib = env.app->library();

        // give episodes distinct progress
        lib.saveProgress(eps[0].file.id, 65000, 600000);
        lib.markCompleted(eps[1].file.id, 600000);
        lib.saveProgress(eps[2].file.id, 123000, 600000);
        QVector<Id> mediaIds;
        for (const Episode& e : eps) mediaIds << e.file.id;

        const Id seasonId = eps[0].seasonId;
        QVERIFY(lib.applyLayout(showId, {{seasonId, {eps[3].id, eps[2].id, eps[1].id, eps[0].id}}}));  // fully reversed

        const auto after = lib.episodes(showId);
        QCOMPARE(after[0].id, eps[3].id);
        QCOMPARE(after[3].id, eps[0].id);
        // identities stable: the same episode id still owns the same file id and title
        for (const Episode& a : after) {
            for (const Episode& b : eps) {
                if (a.id != b.id) continue;
                QCOMPARE(a.file.id, b.file.id);
                QCOMPARE(a.title, b.title);
                QCOMPARE(a.file.path, b.file.path);
            }
        }
        // progress did not move or reset
        QCOMPARE(lib.progress(eps[0].file.id).positionMs, qint64(65000));
        QVERIFY(lib.progress(eps[1].file.id).completed);
        QCOMPARE(lib.progress(eps[2].file.id).positionMs, qint64(123000));
        QCOMPARE(lib.progress(eps[3].file.id).positionMs, qint64(0));
    }

    void moveEpisodeBetweenSeasons()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Moves"), 4, &showId);
        auto& lib = env.app->library();
        const Id s1 = lib.seasons(showId).first().id;
        const Id s2 = lib.addSeason(showId, QStringLiteral("Bonus"));
        QVERIFY(s2 != kInvalidId);
        lib.saveProgress(eps[2].file.id, 99000, 600000);

        QVERIFY(lib.moveEpisode(eps[2].id, s2, 0));  // ep3 -> Bonus
        QVERIFY(lib.moveEpisode(eps[0].id, s2, 1));  // ep1 -> Bonus, after ep3

        const auto after = lib.episodes(showId);
        QCOMPARE(titlesInOrder(after), QStringList({eps[1].title, eps[3].title, eps[2].title, eps[0].title}));
        QCOMPARE(after[0].seasonId, s1);
        QCOMPARE(after[1].seasonId, s1);
        QCOMPARE(after[2].seasonId, s2);
        QCOMPARE(after[3].seasonId, s2);
        QCOMPARE(lib.progress(eps[2].file.id).positionMs, qint64(99000));  // progress untouched by the move

        // seasons themselves can be reordered, carrying their episodes
        QVERIFY(lib.moveSeason(s2, 0));
        QCOMPARE(lib.episodes(showId)[0].id, eps[2].id);
    }

    void invalidLayoutIsRejectedAndLeavesOrderUntouched()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Atomic"), 3, &showId);
        auto& lib = env.app->library();
        const Id seasonId = lib.seasons(showId).first().id;
        const QVector<Id> original = idsInOrder(eps);

        QSignalSpy errors(&lib, &LibraryManager::errorOccurred);
        // missing an episode
        QVERIFY(!lib.applyLayout(showId, {{seasonId, {eps[2].id, eps[0].id}}}));
        // duplicated episode
        QVERIFY(!lib.applyLayout(showId, {{seasonId, {eps[2].id, eps[0].id, eps[0].id, eps[1].id}}}));
        // an episode from nowhere
        QVERIFY(!lib.applyLayout(showId, {{seasonId, {eps[0].id, eps[1].id, eps[2].id, 99999}}}));
        QCOMPARE(errors.count(), 3);
        QCOMPARE(idsInOrder(lib.episodes(showId)), original);
    }

    void renameEpisodeSeasonAndRenumber()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Names"), 2, &showId);
        auto& lib = env.app->library();
        QVERIFY(lib.updateEpisode(eps[0].id, QStringLiteral("  The Beginning  "), 7));
        QVERIFY(!lib.updateEpisode(eps[0].id, QStringLiteral("   "), 1));  // blank titles refused
        const auto e = lib.episode(eps[0].id);
        QVERIFY(e);
        QCOMPARE(e->title, QStringLiteral("The Beginning"));
        QCOMPARE(e->episodeNumber, 7);
        QCOMPARE(e->file.id, eps[0].file.id);

        const Id seasonId = lib.seasons(showId).first().id;
        QVERIFY(lib.updateSeason(seasonId, QStringLiteral("Arc One"), 5));
        QCOMPARE(lib.seasons(showId).first().title, QStringLiteral("Arc One"));
        QCOMPARE(lib.seasons(showId).first().number, 5);

        QVERIFY(lib.renameShow(showId, QStringLiteral("Renamed")));
        QVERIFY(!lib.renameShow(showId, QString()));
        QCOMPARE(lib.show(showId)->title, QStringLiteral("Renamed"));
    }

    // ------------------------------------------------------------ deleting

    void removingFromLibraryNeverDeletesVideoFiles()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Doomed"), 3, &showId);
        auto& lib = env.app->library();
        lib.markCompleted(eps[0].file.id, 1000);
        QStringList paths;
        for (const Episode& e : eps) paths << e.file.path;

        // remove one episode, then a season, then the whole show
        QVERIFY(lib.removeEpisodes({eps[0].id}));
        QCOMPARE(lib.episodes(showId).size(), 2);
        QVERIFY(lib.deleteSeason(lib.seasons(showId).first().id));
        QCOMPARE(lib.episodes(showId).size(), 0);
        QVERIFY(lib.deleteShow(showId));
        QVERIFY(!lib.show(showId).has_value());

        for (const QString& p : paths) QVERIFY2(QFileInfo::exists(p), qPrintable(p + QStringLiteral(" was deleted!")));

        // library rows are gone too: no orphan media files or progress
        Stmt s(env.app->database().database(), QStringLiteral("SELECT (SELECT COUNT(*) FROM media_files), (SELECT COUNT(*) FROM playback_progress)"));
        s.run();
        QVERIFY(s.next());
        QCOMPARE(s.i32(0), 0);
        QCOMPARE(s.i32(1), 0);

        // and the files can be imported again as new entries
        const Id again = lib.createShow(QStringLiteral("Back"));
        QCOMPARE(env.app->importer().importEpisodesSync(again, kInvalidId, paths).added, 3);
    }

    void deletingMovieKeepsFileAndPosterCopyIsRemoved()
    {
        Env env;
        auto& lib = env.app->library();
        const QString file = env.makeFile(QStringLiteral("mv/film.mkv"));
        QCOMPARE(env.app->importer().importMoviesSync({file}).added, 1);
        const Movie movie = lib.movies().first();

        const QString poster = env.makeFile(QStringLiteral("mv/poster.png"));
        QVERIFY(lib.setMoviePoster(movie.id, poster));
        const QString stored = lib.coverPath(lib.movie(movie.id)->posterFile);
        QVERIFY(!stored.isEmpty());
        QVERIFY(stored != poster);  // a private copy
        QVERIFY(QFileInfo::exists(stored));

        QVERIFY(lib.deleteMovie(movie.id));
        QVERIFY(QFileInfo::exists(file));        // the video stays
        QVERIFY(QFileInfo::exists(poster));      // the user's original image stays
        QVERIFY(!QFileInfo::exists(stored));     // only our copy is cleaned up
    }

    void showCoverIsCopiedAndReplaced()
    {
        Env env;
        auto& lib = env.app->library();
        const Id showId = lib.createShow(QStringLiteral("Covered"));
        const QString img1 = env.makeFile(QStringLiteral("img/a.png"));
        const QString img2 = env.makeFile(QStringLiteral("img/b.jpg"));
        QVERIFY(lib.setShowCover(showId, img1));
        const QString first = lib.coverPath(lib.show(showId)->coverFile);
        QVERIFY(QFileInfo::exists(first));
        QVERIFY(lib.setShowCover(showId, img2));
        const QString second = lib.coverPath(lib.show(showId)->coverFile);
        QVERIFY(first != second);
        QVERIFY(!QFileInfo::exists(first));  // the replaced copy is removed
        QVERIFY(QFileInfo::exists(second));
        QVERIFY(!lib.setShowCover(showId, env.makeFile(QStringLiteral("img/not-an-image.txt"))));
        QVERIFY(lib.clearShowCover(showId));
        QVERIFY(lib.show(showId)->coverFile.isEmpty());
        QVERIFY(QFileInfo::exists(img1) && QFileInfo::exists(img2));
    }

    // ------------------------------------------------------------ moved files

    void movingAFileAndRelinkingKeepsProgressAndMetadata()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Relink"), 3, &showId);
        auto& lib = env.app->library();
        const Episode victim = eps[1];
        lib.updateEpisode(victim.id, QStringLiteral("Custom title"), 42);
        lib.saveProgress(victim.file.id, 77000, 600000);
        lib.saveTrackSelection(victim.file.id, 2, 3);

        // the user moves the file somewhere else
        const QString newPath = LibraryManager::normalizePath(QDir(env.mediaDir()).filePath(QStringLiteral("moved/elsewhere/renamed.mp4")));
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        QVERIFY(QFile::rename(victim.file.path, newPath));

        // the library notices (background check) and flags the entry
        QSignalSpy verified(&lib, &LibraryManager::filesVerified);
        lib.verifyShowAsync(showId);
        QVERIFY(verified.wait(10000));
        QVERIFY(lib.episode(victim.id)->file.missing);
        QVERIFY(!lib.episode(eps[0].id)->file.missing);

        // locate it again
        const RelinkResult r = lib.relinkFile(victim.file.id, newPath);
        QVERIFY2(r.ok, qPrintable(r.error));

        const auto e = lib.episode(victim.id);
        QVERIFY(e);
        QCOMPARE(e->file.path, newPath);
        QVERIFY(!e->file.missing);
        QCOMPARE(e->file.id, victim.file.id);          // same media identity
        QCOMPARE(e->title, QStringLiteral("Custom title"));
        QCOMPARE(e->episodeNumber, 42);
        QCOMPARE(e->sortOrder, victim.sortOrder);      // same place in the sequence
        const Progress p = lib.progress(victim.file.id);
        QCOMPARE(p.positionMs, qint64(77000));          // progress preserved
        QCOMPARE(p.audioTrack, 2);
        QCOMPARE(p.subtitleTrack, 3);
        QCOMPARE(idsInOrder(lib.episodes(showId)), idsInOrder(eps));
    }

    void relinkRejectsBadTargets()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Bad relink"), 2, &showId);
        auto& lib = env.app->library();
        // nonexistent file
        QVERIFY(!lib.relinkFile(eps[0].file.id, QDir(env.mediaDir()).filePath(QStringLiteral("nope.mp4"))).ok);
        // a file that already belongs to another entry
        const RelinkResult dup = lib.relinkFile(eps[0].file.id, eps[1].file.path);
        QVERIFY(!dup.ok);
        QVERIFY2(dup.error.contains(QStringLiteral("already in the library")), qPrintable(dup.error));
        QCOMPARE(lib.episode(eps[0].id)->file.path, eps[0].file.path);  // unchanged
    }

    void movedFolderIsRelinkedInOneGo()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("FolderMove"), 3, &showId);
        auto& lib = env.app->library();
        lib.saveProgress(eps[2].file.id, 55000, 600000);

        // the whole "FolderMove" folder is moved to another drive/location
        const QString oldDir = QDir(env.mediaDir()).filePath(QStringLiteral("FolderMove"));
        const QString newRoot = QDir(env.root()).filePath(QStringLiteral("NewDrive/Media"));
        QDir().mkpath(newRoot);
        const QString newDir = QDir(newRoot).filePath(QStringLiteral("FolderMove"));
        QVERIFY(QDir().rename(oldDir, newDir));
        lib.setMissing(eps[0].file.id, true);
        lib.setMissing(eps[1].file.id, true);
        lib.setMissing(eps[2].file.id, true);

        // the user points the first file at its new home...
        const QString movedFirst = LibraryManager::normalizePath(QDir(newDir).filePath(QFileInfo(eps[0].file.path).fileName()));
        QVERIFY(lib.relinkFile(eps[0].file.id, movedFirst).ok);
        // ...and LocalFlix finds the siblings that moved the same way
        const auto proposals = lib.proposeRelinks(eps[0].file.path, movedFirst);
        QCOMPARE(proposals.size(), 2);
        QCOMPARE(lib.applyRelinks(proposals), 2);

        const auto after = lib.episodes(showId);
        for (const Episode& e : after) {
            QVERIFY2(QFileInfo::exists(e.file.path), qPrintable(e.file.path));
            QVERIFY(!e.file.missing);
            QVERIFY2(e.file.path.startsWith(LibraryManager::normalizePath(newDir), Qt::CaseInsensitive),  // Windows paths ignore case
                 qPrintable(e.file.path));
        }
        QCOMPARE(lib.progress(eps[2].file.id).positionMs, qint64(55000));
        QCOMPARE(lib.missingFiles().size(), 0);
    }

    // ------------------------------------------------------------ collections

    void collectionsKeepManualOrderAndFollowDeletions()
    {
        Env env;
        auto& lib = env.app->library();
        Id showA = 0, showB = 0;
        env.makeShow(QStringLiteral("Show A"), 2, &showA);
        env.makeShow(QStringLiteral("Show B"), 1, &showB);
        const QString movieFile = env.makeFile(QStringLiteral("mc/film.mkv"));
        env.app->importer().importMoviesSync({movieFile});
        const Id movieId = lib.movies().first().id;

        const Id col = lib.createCollection(QStringLiteral("Weekend"));
        QVERIFY(col != kInvalidId);
        QVERIFY(lib.addToCollection(col, CollectionItemType::Show, showA));
        QVERIFY(lib.addToCollection(col, CollectionItemType::Movie, movieId));
        QVERIFY(lib.addToCollection(col, CollectionItemType::Show, showB));
        QVERIFY(!lib.addToCollection(col, CollectionItemType::Show, showA));  // no duplicates

        auto entries = lib.collectionEntries(col);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].title, QStringLiteral("Show A"));

        // reorder: B, movie, A
        QVERIFY(lib.setCollectionOrder(col, {entries[2].id, entries[1].id, entries[0].id}));
        entries = lib.collectionEntries(col);
        QCOMPARE(entries[0].title, QStringLiteral("Show B"));
        QCOMPARE(entries[2].title, QStringLiteral("Show A"));

        // collection queue = every episode/movie in that order
        env.app->queue().setContext({ContextType::Collection, col});
        const auto q = env.app->queue().entries();
        QCOMPARE(q.size(), 4);  // 1 (B) + 1 (movie) + 2 (A)
        QCOMPARE(q[0].contextTitle, QStringLiteral("Show B"));
        QCOMPARE(q[1].kind, MediaKind::Movie);
        QCOMPARE(q[2].contextTitle, QStringLiteral("Show A"));

        // deleting a show removes its entry (trigger), not the whole collection
        QVERIFY(lib.deleteShow(showB));
        QCOMPARE(lib.collectionEntries(col).size(), 2);
        QVERIFY(lib.deleteCollection(col));
        QVERIFY(QFileInfo::exists(movieFile));
        QVERIFY(lib.movie(movieId).has_value());
    }
};

QTEST_GUILESS_MAIN(TstLibrary)
#include "tst_library.moc"
