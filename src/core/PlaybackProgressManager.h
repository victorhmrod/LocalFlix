#pragma once

#include "LibraryManager.h"

#include <QObject>
#include <QTimer>

namespace lf {

enum class FlushReason { Periodic, Pause, Switch, Stop, Shutdown };

// Keeps the saved position of the item being played up to date without
// hammering SQLite: positions are cached in memory and written at most every
// `intervalMs` (default 5 s), plus immediately on pause, switch, stop and
// shutdown. Positions that did not move are never rewritten.
class PlaybackProgressManager : public QObject {
    Q_OBJECT
public:
    explicit PlaybackProgressManager(LibraryManager* library, QObject* parent = nullptr, int intervalMs = 5000);

    // Start tracking `mediaFileId`. With persistPosition=false (replaying an
    // already completed item) only the "last watched" time is updated.
    void begin(Id mediaFileId, bool persistPosition, qint64 durationMs);
    void update(qint64 positionMs, qint64 durationMs);
    // Writes the cached position if it changed. Tracking continues.
    void flush(FlushReason reason);
    // Final write for the item, then stop tracking.
    void end(FlushReason reason);
    // The item reached its natural end: record completion and stop tracking.
    void complete(qint64 durationMs);

    Id currentMediaId() const { return m_media; }
    bool isTracking() const { return m_media != kInvalidId; }
    // Number of database writes issued so far (used to check write frequency).
    int writeCount() const { return m_writes; }

private:
    void recordDurationOnce();
    void writePosition();

    LibraryManager* m_library;
    QTimer m_timer;
    Id m_media = kInvalidId;
    bool m_persist = true;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qint64 m_lastSaved = -1;
    bool m_hadHistory = false;
    bool m_durationRecorded = false;
    qint64 m_knownFileDuration = 0;
    int m_writes = 0;
};

}  // namespace lf
