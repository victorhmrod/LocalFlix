#include "EpisodeQueueManager.h"

#include <algorithm>

namespace lf {

EpisodeQueueManager::EpisodeQueueManager(LibraryManager* library, QObject* parent) : QObject(parent), m_library(library)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] { flushIfDirty(); });
    connect(library, &LibraryManager::libraryChanged, this, &EpisodeQueueManager::onLibraryChanged);
    connect(library, &LibraryManager::showChanged, this, &EpisodeQueueManager::onShowChanged);
    connect(library, &LibraryManager::progressChanged, this, &EpisodeQueueManager::onProgressChanged);
}

void EpisodeQueueManager::setContext(const QueueContext& context)
{
    m_context = context;
    m_currentMedia = kInvalidId;
    m_currentIndex = -1;
    m_anchor = 0;
    doReload();
}

void EpisodeQueueManager::reload()
{
    doReload();
}

void EpisodeQueueManager::doReload()
{
    m_dirty = false;
    m_reloadTimer.stop();
    const int oldIndex = m_currentIndex;
    m_entries = m_context.type == ContextType::None ? QVector<QueueEntry>() : m_library->buildQueue(m_context);

    m_currentIndex = -1;
    if (m_currentMedia != kInvalidId) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].mediaFileId == m_currentMedia) {
                m_currentIndex = i;
                break;
            }
        }
    }
    if (m_currentIndex >= 0) {
        m_anchor = m_currentIndex + 1;
    } else if (m_currentMedia != kInvalidId) {
        // The playing item left the queue (removed): whatever now sits at its
        // old position becomes "next".
        const int old = oldIndex >= 0 ? oldIndex : std::max(0, m_anchor - 1);
        m_anchor = std::min<int>(old, int(m_entries.size()));
    } else {
        m_anchor = 0;
    }
    emit queueChanged();
}

void EpisodeQueueManager::scheduleReload()
{
    if (m_context.type == ContextType::None) return;
    m_dirty = true;
    m_reloadTimer.start();
}

void EpisodeQueueManager::flushIfDirty() const
{
    if (m_dirty) const_cast<EpisodeQueueManager*>(this)->doReload();
}

void EpisodeQueueManager::onLibraryChanged()
{
    scheduleReload();
}

void EpisodeQueueManager::onShowChanged(Id showId)
{
    bool relevant = m_context.type == ContextType::Collection;
    if (m_context.type == ContextType::Show && m_context.id == showId) relevant = true;
    if (relevant) scheduleReload();
}

void EpisodeQueueManager::onProgressChanged(Id mediaFileId)
{
    for (QueueEntry& e : m_entries) {
        if (e.mediaFileId != mediaFileId) continue;
        const Progress p = m_library->progress(mediaFileId);
        e.completed = p.completed;
        e.positionMs = p.resumePositionMs();
        if (p.durationMs > 0) e.durationMs = p.durationMs;
        break;
    }
}

const QVector<QueueEntry>& EpisodeQueueManager::entries() const
{
    flushIfDirty();
    return m_entries;
}

int EpisodeQueueManager::indexOfMedia(Id mediaFileId) const
{
    flushIfDirty();
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].mediaFileId == mediaFileId) return i;
    return -1;
}

QueueEntry EpisodeQueueManager::entryForMedia(Id mediaFileId) const
{
    const int i = indexOfMedia(mediaFileId);
    return i >= 0 ? m_entries[i] : QueueEntry{};
}

void EpisodeQueueManager::setCurrentMedia(Id mediaFileId)
{
    flushIfDirty();
    m_currentMedia = mediaFileId;
    m_currentIndex = mediaFileId == kInvalidId ? -1 : indexOfMedia(mediaFileId);
    m_anchor = m_currentIndex >= 0 ? m_currentIndex + 1 : 0;
    emit currentChanged(current());
}

QueueEntry EpisodeQueueManager::current() const
{
    flushIfDirty();
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ? m_entries[m_currentIndex] : QueueEntry{};
}

QueueEntry EpisodeQueueManager::next(bool skipCompleted) const
{
    flushIfDirty();
    const int start = m_currentIndex >= 0 ? m_currentIndex + 1 : m_anchor;
    for (int i = std::max(0, start); i < m_entries.size(); ++i) {
        if (skipCompleted && m_entries[i].completed) continue;
        return m_entries[i];
    }
    return {};
}

QueueEntry EpisodeQueueManager::previous() const
{
    flushIfDirty();
    const int start = m_currentIndex >= 0 ? m_currentIndex - 1 : m_anchor - 1;
    if (start >= 0 && start < m_entries.size()) return m_entries[start];
    return {};
}

}  // namespace lf
