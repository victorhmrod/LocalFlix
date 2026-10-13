#pragma once

#include "DbError.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace lf {

// Thin prepared-statement wrapper: positional binds, throws DbError on failure.
class Stmt {
public:
    Stmt(const QSqlDatabase& db, const QString& sql) : m_sql(sql), m_query(db)
    {
        m_query.setForwardOnly(true);
        if (!m_query.prepare(sql))
            throw DbError(QStringLiteral("SQL prepare failed: %1 [%2]").arg(m_query.lastError().text(), sql));
    }

    Stmt& bind(const QVariant& v)
    {
        // A null QString would be stored as SQL NULL; every text column here is
        // NOT NULL with '' as the "empty" value, so bind null strings as ''.
        if (v.metaType().id() == QMetaType::QString && v.toString().isNull())
            m_query.addBindValue(QStringLiteral(""));
        else
            m_query.addBindValue(v);
        return *this;
    }

    Stmt& run()
    {
        if (!m_query.exec())
            throw DbError(QStringLiteral("SQL exec failed: %1 [%2]").arg(m_query.lastError().text(), m_sql));
        return *this;
    }

    bool next() { return m_query.next(); }
    QVariant value(int i) const { return m_query.value(i); }
    qint64 i64(int i) const { return m_query.value(i).toLongLong(); }
    int i32(int i) const { return m_query.value(i).toInt(); }
    bool flag(int i) const { return m_query.value(i).toInt() != 0; }
    QString str(int i) const { return m_query.value(i).toString(); }
    qint64 lastInsertId() const { return m_query.lastInsertId().toLongLong(); }
    int rowsAffected() const { return m_query.numRowsAffected(); }

private:
    QString m_sql;
    QSqlQuery m_query;
};

// Runs a statement that returns no rows.
inline int execSql(const QSqlDatabase& db, const QString& sql, std::initializer_list<QVariant> binds = {})
{
    Stmt s(db, sql);
    for (const auto& b : binds) s.bind(b);
    s.run();
    return s.rowsAffected();
}

// RAII transaction. Rolls back unless commit() was reached. Not re-entrant:
// public repository operations own the transaction, private helpers do not.
class Transaction {
public:
    explicit Transaction(const QSqlDatabase& db) : m_db(db)
    {
        QSqlDatabase mutableDb = m_db;
        if (!mutableDb.transaction())
            throw DbError(QStringLiteral("BEGIN failed: %1").arg(mutableDb.lastError().text()));
        m_active = true;
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction()
    {
        if (m_active) {
            QSqlDatabase mutableDb = m_db;
            mutableDb.rollback();
        }
    }
    void commit()
    {
        QSqlDatabase mutableDb = m_db;
        if (!mutableDb.commit())
            throw DbError(QStringLiteral("COMMIT failed: %1").arg(mutableDb.lastError().text()));
        m_active = false;
    }

private:
    QSqlDatabase m_db;
    bool m_active = false;
};

}  // namespace lf
