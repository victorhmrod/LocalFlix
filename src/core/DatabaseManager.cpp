#include "DatabaseManager.h"

#include "SqlUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <atomic>

Q_LOGGING_CATEGORY(lcDb, "localflix.db")

namespace lf {

namespace {

std::atomic<int> g_connectionCounter{0};

void applyPragmas(QSqlDatabase& db)
{
    // foreign_keys is per-connection; WAL is persistent but cheap to re-assert.
    execSql(db, QStringLiteral("PRAGMA foreign_keys = ON"));
    execSql(db, QStringLiteral("PRAGMA journal_mode = WAL"));
    execSql(db, QStringLiteral("PRAGMA synchronous = NORMAL"));
}

int readUserVersion(const QSqlDatabase& db)
{
    Stmt s(db, QStringLiteral("PRAGMA user_version"));
    s.run();
    return s.next() ? s.i32(0) : 0;
}

}  // namespace

DbSession::~DbSession()
{
    // The QSqlDatabase handle must be gone before the connection is removed.
    QSqlDatabase::removeDatabase(m_name);
}

QSqlDatabase DbSession::database() const
{
    return QSqlDatabase::database(m_name, /*open=*/false);
}

DatabaseManager::DatabaseManager(QString databasePath, std::vector<Migration> migrations)
    : m_path(std::move(databasePath)), m_migrations(std::move(migrations))
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

QString DatabaseManager::createConnection(QString* error) const
{
    const QString name = QStringLiteral("localflix_%1_%2")
                             .arg(g_connectionCounter.fetch_add(1))
                             .arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        if (error) *error = QStringLiteral("Cannot open database %1: %2").arg(m_path, db.lastError().text());
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return {};
    }
    try {
        applyPragmas(db);
    } catch (const DbError& e) {
        if (error) *error = e.message();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return {};
    }
    return name;
}

bool DatabaseManager::open(QString* error)
{
    if (m_opened) return true;

    const QFileInfo fi(m_path);
    if (!QDir().mkpath(fi.absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create data directory %1").arg(fi.absolutePath());
        return false;
    }

    const bool existedBefore = fi.exists() && fi.size() > 0;

    QString err;
    const QString name = createConnection(&err);
    if (name.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    m_mainConnection = name;

    QSqlDatabase db = QSqlDatabase::database(m_mainConnection, false);
    const int current = [&] {
        try {
            return readUserVersion(db);
        } catch (const DbError&) {
            return -1;
        }
    }();
    if (current < 0) {
        if (error) *error = QStringLiteral("Cannot read schema version of %1 (not a SQLite database?)").arg(m_path);
        close();
        return false;
    }
    if (current > latestVersion()) {
        if (error)
            *error = QStringLiteral("The library at %1 was created by a newer version of LocalFlix (schema %2, this build supports %3).")
                         .arg(m_path)
                         .arg(current)
                         .arg(latestVersion());
        close();
        return false;
    }

    if (existedBefore && current > 0 && current < latestVersion()) {
        // Keep the pre-migration library next to the original; migrations are
        // transactional, but a copy costs little and protects against bugs.
        m_backupPath = QStringLiteral("%1.bak-v%2").arg(m_path).arg(current);
        // Fold the WAL into the main file so the copy is self-contained.
        try {
            execSql(db, QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        } catch (const DbError&) {
        }
        QFile::remove(m_backupPath);
        if (!QFile::copy(m_path, m_backupPath)) {
            qCWarning(lcDb) << "Could not write pre-migration backup" << m_backupPath;
            m_backupPath.clear();
        }
    }

    if (!migrate(db, &err)) {
        if (error) *error = err;
        close();
        return false;
    }
    m_opened = true;
    qCInfo(lcDb) << "Database ready:" << m_path << "schema" << schemaVersion();
    return true;
}

bool DatabaseManager::migrate(QSqlDatabase& db, QString* error)
{
    try {
        int current = readUserVersion(db);
        for (const Migration& m : m_migrations) {
            if (m.version <= current) continue;
            if (m.version != current + 1) {
                throw DbError(QStringLiteral("Migration gap: database is at %1 but next step is %2").arg(current).arg(m.version));
            }
            qCInfo(lcDb) << "Applying migration" << m.version << m.description;
            Transaction tx(db);
            for (const QString& sql : m.statements) execSql(db, sql);
            // PRAGMA does not accept bound parameters.
            execSql(db, QStringLiteral("PRAGMA user_version = %1").arg(m.version));
            tx.commit();
            current = m.version;
        }
    } catch (const DbError& e) {
        if (error) *error = QStringLiteral("Database migration failed: %1").arg(e.message());
        return false;
    }
    return true;
}

void DatabaseManager::close()
{
    if (!m_mainConnection.isEmpty()) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_mainConnection, false);
            if (db.isValid() && db.isOpen()) {
                // Leave a single self-contained file behind on clean shutdown.
                QSqlQuery q(db);
                q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_mainConnection);
        m_mainConnection.clear();
    }
    m_opened = false;
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_mainConnection, false);
}

std::unique_ptr<DbSession> DatabaseManager::openSession() const
{
    QString err;
    const QString name = createConnection(&err);
    if (name.isEmpty()) {
        qCCritical(lcDb) << err;
        return nullptr;
    }
    return std::unique_ptr<DbSession>(new DbSession(name));
}

int DatabaseManager::schemaVersion() const
{
    try {
        return readUserVersion(database());
    } catch (const DbError&) {
        return -1;
    }
}

int DatabaseManager::latestVersion() const
{
    return m_migrations.empty() ? 0 : m_migrations.back().version;
}

}  // namespace lf
