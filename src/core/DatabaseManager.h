#pragma once

#include "Migrations.h"

#include <QSqlDatabase>
#include <QString>

#include <memory>
#include <vector>

namespace lf {

// A connection that belongs to one non-GUI thread. QSqlDatabase connections
// must not cross threads, so workers take a session, use database() on their
// own thread and drop the session when done.
class DbSession {
public:
    ~DbSession();
    DbSession(const DbSession&) = delete;
    DbSession& operator=(const DbSession&) = delete;
    QSqlDatabase database() const;

private:
    friend class DatabaseManager;
    explicit DbSession(QString connectionName) : m_name(std::move(connectionName)) {}
    QString m_name;
};

// Owns the SQLite file: creation, forward-only migrations (with a backup of
// pre-existing libraries) and per-thread connections.
class DatabaseManager {
public:
    explicit DatabaseManager(QString databasePath,
                             std::vector<Migration> migrations = defaultMigrations());
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Creates the directory and file if needed and migrates to the latest
    // version. Returns false and fills `error` on failure.
    bool open(QString* error = nullptr);
    void close();
    bool isOpen() const { return m_opened; }

    // The connection of the thread that called open() (the GUI thread).
    QSqlDatabase database() const;
    // A fresh connection for the calling (worker) thread.
    std::unique_ptr<DbSession> openSession() const;

    int schemaVersion() const;
    int latestVersion() const;
    QString path() const { return m_path; }
    // Set when open() found an older database and copied it before migrating.
    QString backupPath() const { return m_backupPath; }

private:
    QString createConnection(QString* error) const;
    bool migrate(QSqlDatabase& db, QString* error);

    QString m_path;
    std::vector<Migration> m_migrations;
    QString m_mainConnection;
    QString m_backupPath;
    bool m_opened = false;
};

}  // namespace lf
