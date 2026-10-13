#pragma once

#include <QString>

#include <stdexcept>

namespace lf {

// Thrown by the database layer; LibraryManager converts it into signals so no
// exception ever crosses a Qt slot boundary.
class DbError : public std::runtime_error {
public:
    explicit DbError(const QString& message)
        : std::runtime_error(message.toStdString()), m_message(message)
    {
    }
    const QString& message() const { return m_message; }

private:
    QString m_message;
};

}  // namespace lf
