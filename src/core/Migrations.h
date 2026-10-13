#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace lf {

// One forward-only schema step. Every statement of a step runs inside a single
// transaction together with the PRAGMA user_version bump, so a failed step
// leaves the database exactly as it was.
struct Migration {
    int version = 0;  // must be strictly increasing, starting at 1
    QString description;
    QStringList statements;
};

// The migrations shipped with this build, ordered by version.
const std::vector<Migration>& defaultMigrations();

}  // namespace lf
