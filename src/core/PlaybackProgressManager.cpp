#include "PlaybackProgressManager.h"

#include <cstdlib>

namespace lf {

namespace {
// Positions closer than this to the last saved value are not worth a write
// during normal playback.
constexpr qint64 kMinPeriodicDeltaMs = 1000;
// Opening an item and leaving within this time does not create history.
constexpr qint64 kMinHistoryMs = 2000;
}  // namespace

PlaybackProgressManager::PlaybackProgressManager(LibraryManager* library, QObject* parent, int intervalMs)
    : QObject(parent), m_library(library)
{
    m_timer.setInterval(intervalMs);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { flush(FlushReason::Periodic); });
}

void PlaybackProgressManager::begin(Id mediaFileId, bool persistPosition, qint64 durationMs)
{
    if (m_media != kInvalidId) end(FlushReason::Switch);
    const Progress saved = m_library->progress(mediaFileId);
    m_media = mediaFileId;
    m_persist = persistPosition;
    m_duration = durationMs > 0 ? durationMs : saved.durationMs;
    m_position = persistPosition ? saved.resumePositionMs() : 0;
    m_lastSaved = persistPosition ? saved.positionMs : -1;
    m_hadHistory = saved.lastWatchedAt > 0;
    m_durationRecorded = false;
    if (const auto file = m_library->mediaFile(mediaFileId)) m_knownFileDuration = file->durationMs;
    recordDurationOnce();
    if (!persistPosition) {
        // Replay of a finished item: remember that it was watched again, nothing more.
        m_library->touchLastWatched(mediaFileId);
        ++m_writes;
    }
    m_timer.start();
}

void PlaybackProgressManager::update(qint64 positionMs, qint64 durationMs)
{
    if (m_media == kInvalidId) return;
    m_position = std::max<qint64>(0, positionMs);
    if (durationMs > 0) {
        m_duration = durationMs;
        recordDurationOnce();
    }
}

void PlaybackProgressManager::recordDurationOnce()
{
    // The duration of a file does not change while it plays: store it once on
    // the media file instead of rewriting it with every progress save.
    if (m_durationRecorded || m_duration <= 0 || m_media == kInvalidId) return;
    m_durationRecorded = true;
    if (std::llabs(m_duration - m_knownFileDuration) > 500) {
        m_library->setMediaDuration(m_media, m_duration);
        ++m_writes;
    }
}

void PlaybackProgressManager::writePosition()
{
    // The entry may have been removed from the library while it was playing.
    if (!m_library->mediaFile(m_media)) {
        m_persist = false;
        return;
    }
    m_library->saveProgress(m_media, m_position, m_duration);
    m_lastSaved = m_position;
    m_hadHistory = true;
    ++m_writes;
}

void PlaybackProgressManager::flush(FlushReason reason)
{
    if (m_media == kInvalidId || !m_persist) return;
    if (!m_hadHistory && m_position < kMinHistoryMs) return;
    const qint64 delta = m_lastSaved < 0 ? m_position + 1 : std::llabs(m_position - m_lastSaved);
    if (reason == FlushReason::Periodic) {
        if (delta < kMinPeriodicDeltaMs) return;
    } else if (delta == 0) {
        return;
    }
    writePosition();
}

void PlaybackProgressManager::end(FlushReason reason)
{
    if (m_media == kInvalidId) return;
    flush(reason);
    m_timer.stop();
    m_media = kInvalidId;
    m_persist = true;
    m_lastSaved = -1;
}

void PlaybackProgressManager::complete(qint64 durationMs)
{
    if (m_media == kInvalidId) return;
    const Id media = m_media;
    const qint64 duration = durationMs > 0 ? durationMs : m_duration;
    m_timer.stop();
    m_media = kInvalidId;
    m_persist = true;
    m_lastSaved = -1;
    if (!m_library->mediaFile(media)) return;  // removed from the library while playing
    m_library->markCompleted(media, duration);
    ++m_writes;
}

}  // namespace lf
