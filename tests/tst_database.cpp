// Database creation, migrations, stable identity and settings persistence.
#include "DatabaseManager.h"
#include "SqlUtil.h"
#include "TestSupport.h"

using namespace lf;
using namespace lf::test;

class TstDatabase : public QObject {
    Q_OBJECT

private slots:
    void createsDatabaseWithLatestSchema()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("nested/deeper/library.db"));
        DatabaseManager db(path);
        QString err;
        QVERIFY2(db.open(&err), qPrintable(err));
        QVERIFY(QFileInfo::exists(path));  // directory tree was created
        QCOMPARE(db.schemaVersion(), db.latestVersion());
        QVERIFY(db.latestVersion() >= 1);

        const QStringList expected = {QStringLiteral("media_files"), QStringLiteral("shows"), QStringLiteral("seasons"),
                                      QStringLiteral("episodes"), QStringLiteral("movies"), QStringLiteral("collections"),
                                      QStringLiteral("collection_entries"), QStringLiteral("playback_progress"),
                                      QStringLiteral("settings")};
        for (const QString& table : expected) {
            Stmt s(db.database(), QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name=?"));
            s.bind(table).run();
            QVERIFY2(s.next(), qPrintable(QStringLiteral("missing table ") + table));
        }
    }

    void usesWalAndForeignKeys()
    {
        QTemporaryDir dir;
        DatabaseManager db(QDir(dir.path()).filePath(QStringLiteral("a.db")));
        QVERIFY(db.open());
        Stmt fk(db.database(), QStringLiteral("PRAGMA foreign_keys"));
        fk.run();
        QVERIFY(fk.next());
        QCOMPARE(fk.i32(0), 1);
        Stmt jm(db.database(), QStringLiteral("PRAGMA journal_mode"));
        jm.run();
        QVERIFY(jm.next());
        QCOMPARE(jm.str(0).toLower(), QStringLiteral("wal"));
    }

    void reopeningIsIdempotent()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("a.db"));
        {
            DatabaseManager db(path);
            QVERIFY(db.open());
            execSql(db.database(), QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES('Keep me', 1, 1)"));
        }
        DatabaseManager again(path);
        QVERIFY(again.open());
        Stmt s(again.database(), QStringLiteral("SELECT COUNT(*) FROM shows WHERE title='Keep me'"));
        s.run();
        QVERIFY(s.next());
        QCOMPARE(s.i32(0), 1);
    }

    void migrationKeepsExistingData_andWritesBackup()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("a.db"));

        // An "old release": only schema v1, with a library in it.
        {
            DatabaseManager v1(path);
            QVERIFY(v1.open());
            execSql(v1.database(), QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES('Old show', 1, 1)"));
            execSql(v1.database(), QStringLiteral("INSERT INTO media_files(path, added_at) VALUES('C:/x/y.mkv', 1)"));
            execSql(v1.database(), QStringLiteral("INSERT INTO playback_progress(media_file_id, position_ms) VALUES(1, 4242)"));
        }

        // The "new release" adds a column and a table in schema v2.
        std::vector<Migration> migrations = defaultMigrations();
        Migration v2;
        v2.version = int(migrations.size()) + 1;
        v2.description = QStringLiteral("add rating");
        v2.statements = {QStringLiteral("ALTER TABLE shows ADD COLUMN rating INTEGER NOT NULL DEFAULT 0"),
                         QStringLiteral("CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")};
        migrations.push_back(v2);

        DatabaseManager db(path, migrations);
        QString err;
        QVERIFY2(db.open(&err), qPrintable(err));
        QCOMPARE(db.schemaVersion(), v2.version);

        // old data intact, new column has its default
        Stmt s(db.database(), QStringLiteral("SELECT title, rating FROM shows"));
        s.run();
        QVERIFY(s.next());
        QCOMPARE(s.str(0), QStringLiteral("Old show"));
        QCOMPARE(s.i32(1), 0);
        Stmt p(db.database(), QStringLiteral("SELECT position_ms FROM playback_progress WHERE media_file_id = 1"));
        p.run();
        QVERIFY(p.next());
        QCOMPARE(p.i64(0), qint64(4242));

        // the pre-migration library was copied next to the original
        QVERIFY2(!db.backupPath().isEmpty(), "backup path");
        QVERIFY(QFileInfo::exists(db.backupPath()));
    }

    void releaseOneLibraryMigratesToSchemaTwoKeepingEverythingAndWritingABackup()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("library.db"));

        // A library as written by 1.0.0 and 1.1.0: only schema v1 existed.
        {
            DatabaseManager old(path, std::vector<Migration>{defaultMigrations().front()});
            QVERIFY(old.open());
            QCOMPARE(old.schemaVersion(), 1);
            execSql(old.database(), QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES('Old show', 1, 1)"));
            execSql(old.database(), QStringLiteral("INSERT INTO seasons(show_id, season_number, title, sort_order) VALUES(1, 1, 'Season 1', 0)"));
            execSql(old.database(), QStringLiteral("INSERT INTO media_files(path, added_at) VALUES('C:/x/y.mkv', 1)"));
            execSql(old.database(), QStringLiteral("INSERT INTO playback_progress(media_file_id, position_ms) VALUES(1, 4242)"));
            execSql(old.database(), QStringLiteral("INSERT INTO settings(key, value) VALUES('playback.autoPlayNext', '0')"));
        }

        DatabaseManager db(path);
        QString err;
        QVERIFY2(db.open(&err), qPrintable(err));
        QCOMPARE(db.schemaVersion(), 2);
        QCOMPARE(db.schemaVersion(), db.latestVersion());
        QVERIFY2(!db.backupPath().isEmpty() && QFileInfo::exists(db.backupPath()), "the v1 library was backed up before migrating");
        QVERIFY(db.backupPath().contains(QStringLiteral("bak-v1")));

        // everything that was there is still there
        Stmt show(db.database(), QStringLiteral("SELECT title FROM shows"));
        show.run();
        QVERIFY(show.next());
        QCOMPARE(show.str(0), QStringLiteral("Old show"));
        Stmt progress(db.database(), QStringLiteral("SELECT position_ms FROM playback_progress WHERE media_file_id = 1"));
        progress.run();
        QVERIFY(progress.next());
        QCOMPARE(progress.i64(0), qint64(4242));
        Stmt setting(db.database(), QStringLiteral("SELECT value FROM settings WHERE key = 'playback.autoPlayNext'"));
        setting.run();
        QVERIFY(setting.next());
        QCOMPARE(setting.str(0), QStringLiteral("0"));

        // the new table works, starts empty, and follows its season when the season is deleted
        Stmt marks(db.database(), QStringLiteral("SELECT COUNT(*) FROM season_skip_marks"));
        marks.run();
        QVERIFY(marks.next());
        QCOMPARE(marks.i32(0), 0);
        execSql(db.database(), QStringLiteral("INSERT INTO season_skip_marks(season_id, ending_from_end_ms) VALUES(1, 30000)"));
        execSql(db.database(), QStringLiteral("DELETE FROM seasons WHERE id = 1"));
        Stmt after(db.database(), QStringLiteral("SELECT COUNT(*) FROM season_skip_marks"));
        after.run();
        QVERIFY(after.next());
        QCOMPARE(after.i32(0), 0);

        // the audio/subtitle choice of a show: starts empty, defaults to "nothing chosen", goes with its show
        Stmt prefs(db.database(), QStringLiteral("SELECT COUNT(*) FROM show_track_prefs"));
        prefs.run();
        QVERIFY(prefs.next());
        QCOMPARE(prefs.i32(0), 0);
        execSql(db.database(), QStringLiteral("INSERT INTO show_track_prefs(show_id, audio_mode, audio_lang) VALUES(1, 1, 'por')"));
        Stmt row(db.database(), QStringLiteral("SELECT audio_mode, audio_lang, sub_mode FROM show_track_prefs WHERE show_id = 1"));
        row.run();
        QVERIFY(row.next());
        QCOMPARE(row.i32(0), 1);
        QCOMPARE(row.str(1), QStringLiteral("por"));
        QCOMPARE(row.i32(2), -1);
        execSql(db.database(), QStringLiteral("DELETE FROM shows WHERE id = 1"));
        Stmt gone(db.database(), QStringLiteral("SELECT COUNT(*) FROM show_track_prefs"));
        gone.run();
        QVERIFY(gone.next());
        QCOMPARE(gone.i32(0), 0);
    }

    void failedMigrationRollsBackAndReportsError()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("a.db"));
        {
            DatabaseManager v1(path);
            QVERIFY(v1.open());
        }
        std::vector<Migration> migrations = defaultMigrations();
        Migration bad;
        bad.version = int(migrations.size()) + 1;
        bad.description = QStringLiteral("broken");
        bad.statements = {QStringLiteral("CREATE TABLE half_done (id INTEGER)"), QStringLiteral("THIS IS NOT SQL")};
        migrations.push_back(bad);

        DatabaseManager db(path, migrations);
        QString err;
        QVERIFY(!db.open(&err));
        QVERIFY(err.contains(QStringLiteral("migration"), Qt::CaseInsensitive));

        // Re-open with only the good migrations: schema untouched, no half-applied table.
        DatabaseManager clean(path);
        QVERIFY(clean.open());
        QCOMPARE(clean.schemaVersion(), clean.latestVersion());
        Stmt s(clean.database(), QStringLiteral("SELECT name FROM sqlite_master WHERE name='half_done'"));
        s.run();
        QVERIFY(!s.next());
    }

    void refusesDatabaseFromNewerRelease()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("a.db"));
        {
            DatabaseManager db(path);
            QVERIFY(db.open());
            execSql(db.database(), QStringLiteral("PRAGMA user_version = 999"));
        }
        DatabaseManager db(path);
        QString err;
        QVERIFY(!db.open(&err));
        QVERIFY2(err.contains(QStringLiteral("newer version")), qPrintable(err));
    }

    void rejectsFileThatIsNotSqlite()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("garbage.db"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'z'));
        f.close();
        DatabaseManager db(path);
        QString err;
        QVERIFY(!db.open(&err));
        QVERIFY(!err.isEmpty());
    }

    void workerThreadGetsItsOwnConnection()
    {
        QTemporaryDir dir;
        DatabaseManager db(QDir(dir.path()).filePath(QStringLiteral("a.db")));
        QVERIFY(db.open());
        execSql(db.database(), QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES('Main thread row', 1, 1)"));

        int seen = -1;
        QThread* worker = QThread::create([&] {
            auto session = db.openSession();
            if (!session) return;
            Stmt s(session->database(), QStringLiteral("SELECT COUNT(*) FROM shows"));
            s.run();
            if (s.next()) seen = s.i32(0);
            execSql(session->database(), QStringLiteral("INSERT INTO shows(title, created_at, updated_at) VALUES('Worker row', 2, 2)"));
        });
        worker->start();
        QVERIFY(worker->wait(10000));
        delete worker;
        QCOMPARE(seen, 1);

        Stmt s(db.database(), QStringLiteral("SELECT COUNT(*) FROM shows"));
        s.run();
        QVERIFY(s.next());
        QCOMPARE(s.i32(0), 2);  // the GUI-thread connection sees the worker's committed row
    }

    void settingsPersistAcrossRestart()
    {
        Env env;
        auto& st = env.app->settings();
        // defaults
        QVERIFY(st.autoPlayNext());
        QCOMPARE(st.autoPlayCountdownSeconds(), 3);
        QVERIFY(st.resumeAutomatically());
        QVERIFY(!st.skipCompleted());
        QCOMPARE(st.defaultVolume(), 100);
        QCOMPARE(st.playbackSpeed(), 1.0);
        QCOMPARE(int(st.fullscreenBehavior()), int(FullscreenBehavior::Manual));

        st.setAutoPlayNext(false);
        st.setAutoPlayCountdownSeconds(7);
        st.setResumeAutomatically(false);
        st.setSkipCompleted(true);
        st.setPreferredAudioLanguage(QStringLiteral("jpn"));
        st.setPreferredSubtitleLanguage(QStringLiteral("por, eng"));
        st.setDefaultVolume(42);
        st.setPlaybackSpeed(1.25);
        st.setFullscreenBehavior(FullscreenBehavior::OnPlayback);
        st.setDefaultImportDir(QStringLiteral("D:/Anime"));
        st.setWindowGeometry(QByteArray("\x01\x02geom", 6));

        env.restart();
        auto& again = env.app->settings();
        QVERIFY(!again.autoPlayNext());
        QCOMPARE(again.autoPlayCountdownSeconds(), 7);
        QVERIFY(!again.resumeAutomatically());
        QVERIFY(again.skipCompleted());
        QCOMPARE(again.preferredAudioLanguage(), QStringLiteral("jpn"));
        QCOMPARE(again.preferredSubtitleLanguage(), QStringLiteral("por, eng"));
        QCOMPARE(again.defaultVolume(), 42);
        QCOMPARE(again.playbackSpeed(), 1.25);
        QCOMPARE(int(again.fullscreenBehavior()), int(FullscreenBehavior::OnPlayback));
        QCOMPARE(again.defaultImportDir(), QStringLiteral("D:/Anime"));
        QCOMPARE(again.windowGeometry(), QByteArray("\x01\x02geom", 6));
    }

    void settingsAreAppliedToTheBackend()
    {
        Env env;
        // initialize() pushes the persisted values into the backend
        QCOMPARE(env.backend->volume(), 100);
        env.app->settings().setPreferredSubtitleLanguage(QStringLiteral("por"));
        env.app->settings().setPreferredAudioLanguage(QStringLiteral("jpn"));
        QCOMPARE(env.backend->preferredSubtitle, QStringLiteral("por"));
        QCOMPARE(env.backend->preferredAudio, QStringLiteral("jpn"));
        env.app->settings().setDefaultVolume(55);
        QCOMPARE(env.backend->volume(), 55);

        env.restart();  // a fresh backend must be configured from the stored settings
        QCOMPARE(env.backend->volume(), 55);
        QCOMPARE(env.backend->preferredAudio, QStringLiteral("jpn"));
    }

    void settingsChangedSignalFiresOnlyOnRealChange()
    {
        Env env;
        QSignalSpy spy(&env.app->settings(), &SettingsManager::changed);
        env.app->settings().setAutoPlayNext(true);  // already the default
        QCOMPARE(spy.count(), 0);
        env.app->settings().setAutoPlayNext(false);
        QCOMPARE(spy.count(), 1);
        env.app->settings().setAutoPlayNext(false);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TstDatabase)
#include "tst_database.moc"
