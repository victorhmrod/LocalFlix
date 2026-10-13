#pragma once

#include "LibraryManager.h"

#include <QObject>
#include <QTimer>

namespace lf {

// The ordered list of things that will play after one another, built from the
// user's manual order. It follows library edits (reordering, removals, watched
// state) so "next" is always computed from the current configuration.
class EpisodeQueueManager : public QObject {
    Q_OBJECT
public:
    explicit EpisodeQueueManager(LibraryManager* library, QObject* parent = nullptr);

    void setContext(const QueueContext& context);
    QueueContext context() const { return m_context; }
    // Re-reads the queue from the library now, keeping the current item.
    void reload();

    const QVector<QueueEntry>& entries() const;
    int indexOfMedia(Id mediaFileId) const;
    QueueEntry entryForMedia(Id mediaFileId) const;

    void setCurrentMedia(Id mediaFileId);
    QueueEntry current() const;
    // Next entry after the current one. With skipCompleted, already completed
    // entries are passed over (used for automatic advancement only).
    QueueEntry next(bool skipCompleted = false) const;
    QueueEntry previous() const;
    bool hasNext() const { return next().valid(); }
    bool hasPrevious() const { return previous().valid(); }

signals:
    void queueChanged();
    void currentChanged(lf::QueueEntry entry);

private slots:
    void onLibraryChanged();
    void onShowChanged(lf::Id showId);
    void onProgressChanged(lf::Id mediaFileId);

private:
    void scheduleReload();
    void flushIfDirty() const;
    void doReload();

    LibraryManager* m_library;
    QueueContext m_context;
    mutable QVector<QueueEntry> m_entries;
    Id m_currentMedia = kInvalidId;
    int m_currentIndex = -1;
    int m_anchor = 0;  // where the current item was, if it has been removed
    mutable bool m_dirty = false;
    QTimer m_reloadTimer;
};

}  // namespace lf
