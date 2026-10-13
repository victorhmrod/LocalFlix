#pragma once

#include <QDateTime>

#include <functional>

namespace lf {

// Wall-clock source for timestamps stored in the database. Tests replace it to
// get deterministic "last watched" ordering without sleeping.
class Clock {
public:
    static qint64 nowMs()
    {
        const auto& f = override_();
        return f ? f() : QDateTime::currentMSecsSinceEpoch();
    }
    static void setOverride(std::function<qint64()> f) { override_() = std::move(f); }
    static void reset() { override_() = nullptr; }

private:
    static std::function<qint64()>& override_()
    {
        static std::function<qint64()> f;
        return f;
    }
};

}  // namespace lf
