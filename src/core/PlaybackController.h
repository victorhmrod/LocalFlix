#pragma once

#include "EpisodeQueueManager.h"
#include "IPlaybackBackend.h"
#include "LibraryManager.h"
#include "PlaybackProgressManager.h"
#include "SettingsManager.h"
#include "SkipSegments.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace lf {

enum class StartMode {
    Auto,      // follow the "resume playback automatically" setting
    Resume,    // continue from the saved position
    FromStart  // always begin at 0:00
};

enum class PlaybackState {
    Idle,           // nothing loaded
    Loading,        // load requested, waiting for the backend
    Playing,
    Paused,
    AdvancePending  // an item finished; the next one starts after the countdown
};

enum class FailureKind {
    MissingFile,  // the file does not exist (moved, renamed, drive disconnected)
    LoadError,    // the file exists but cannot be opened/decoded
    NotInQueue    // the requested item is not part of the current queue
};

struct PlaybackFailure {
    FailureKind kind = FailureKind::LoadError;
    QString message;
};

// Orchestrates one playback session: loads queue entries into the backend,
// tracks progress, and decides what happens when playback ends. The central
// rule: only EndReason::Eof on the *current* load completes an item and may
// start the next one; manual stops, replaced loads, errors and stale events
// never advance.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    PlaybackController(IPlaybackBackend* backend, EpisodeQueueManager* queue, PlaybackProgressManager* progress,
                       LibraryManager* library, SettingsManager* settings, QObject* parent = nullptr);

    // Starts `mediaFileId` in the given queue context (show, movie, collection).
    bool play(const QueueContext& context, Id mediaFileId, StartMode mode = StartMode::Auto);
    // Starts an entry of the current queue.
    bool playEntry(Id mediaFileId, StartMode mode = StartMode::Resume);

    void stop();
    void togglePause();
    void setPaused(bool paused);
    void next();
    void previous();
    void seekToMs(qint64 positionMs);
    void seekByMs(qint64 deltaMs);
    // Skips the countdown and starts the pending next item now.
    void advanceNow();
    // Cancels the pending automatic advance; playback stays stopped.
    void cancelAutoAdvance();
    // Skips the opening/ending the playhead is in (the "Skip" button). Playing
    // an ending that runs to the end of the file seeks to the end, so the item
    // then completes and advances through the normal end-of-file path.
    void skipActiveSegment();

    // ---- opening/ending marks of the season being watched ----------------
    // For files whose chapters do not say where the opening/ending are: the user marks them while watching and
    // they apply to every episode of the season that has no chapters for it. Only episodes have a season.
    bool canMarkSkips() const;
    SeasonSkipMarks skipMarks() const { return m_marks; }
    // The opening starts here / ends here (the end saves both, once it is after the start). `atMs` is the position
    // to mark; -1 = where the playhead is now (a menu that stays open while the video plays passes the position
    // it was opened at).
    bool markIntroStartHere(qint64 atMs = -1);
    bool markIntroEndHere(qint64 atMs = -1);
    qint64 pendingIntroStartMs() const { return m_pendingIntroStartMs; }
    // The ending starts here: stored as "the last N ms" so it fits episodes of other lengths.
    bool markEndingStartHere(qint64 atMs = -1);
    bool clearIntroMark();
    bool clearEndingMark();
    // Flushes progress and stops the backend; safe to call more than once.
    void shutdown();

    void setVolume(int percent);
    void setMuted(bool muted);
    void setSpeed(double speed);
    void selectAudioTrack(int trackId);
    void selectSubtitleTrack(int trackId);
    bool addExternalSubtitle(const QString& path);

    PlaybackState state() const { return m_state; }
    bool isActive() const { return m_state == PlaybackState::Playing || m_state == PlaybackState::Paused || m_state == PlaybackState::Loading; }
    QueueEntry currentEntry() const { return m_current; }
    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    // The opening/ending the playhead is in right now (invalid when none).
    SkipSegment activeSegment() const { return m_activeSegment; }
    // Every opening/ending found in the current file's chapters.
    QVector<SkipSegment> skipSegments() const { return m_segments; }
    bool hasNext() const;
    bool hasPrevious() const;
    IPlaybackBackend* backend() const { return m_backend; }

signals:
    void stateChanged(lf::PlaybackState state);
    // Emitted when the backend has the file open and playback starts.
    void nowPlaying(lf::QueueEntry entry, bool hasPrevious, bool hasNext);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void tracksChanged(QVector<lf::TrackInfo> tracks);
    // The playhead entered or left an opening/ending (an invalid segment = left).
    void skipSegmentChanged(lf::SkipSegment segment);
    // An opening/ending was skipped, by the user or automatically.
    void segmentSkipped(lf::SkipSegment segment, bool automatic);
    // The item played to its natural end and was marked completed.
    void itemCompleted(lf::QueueEntry entry);
    // A countdown to the next item started (or its target changed).
    void autoAdvancePending(lf::QueueEntry next, int remainingMs);
    void autoAdvanceCancelled();
    // The queue was edited (reorder, rename, removal): refreshed current entry
    // and whether Previous/Next are available.
    void queueUpdated(lf::QueueEntry current, bool hasPrevious, bool hasNext);
    // The last item of the queue finished (nothing left to play).
    void queueFinished(lf::QueueEntry last);
    // Playback ended normally but auto-play is off; `next` may be invalid.
    void playbackEndedWithoutAdvance(lf::QueueEntry finished, lf::QueueEntry next);
    void playbackStopped(lf::QueueEntry entry);
    void playbackFailed(lf::QueueEntry entry, lf::PlaybackFailure failure);
    // "Resume playback automatically" is off and the item has a saved position.
    void resumeDecisionNeeded(lf::QueueEntry entry, qint64 positionMs);

private slots:
    void onFileLoaded(quint64 token);
    void onEndFile(lf::EndFileInfo info);
    void onPosition(double seconds);
    void onDuration(double seconds);
    void onChapters(QVector<lf::ChapterInfo> chapters);
    void onSkipMarksChanged(lf::Id seasonId);
    void onPausedChanged(bool paused);
    void onQueueChanged();
    void onAdvanceTimeout();

private:
    void setState(PlaybackState s);
    void beginLoad(const QueueEntry& entry, qint64 startMs);
    void finishActiveItem(FlushReason reason);
    void handleNaturalEnd();
    void startPendingAdvance();
    QueueEntry nextForAdvance() const;
    void seekTo(qint64 targetMs);
    void rebuildSegments();
    void clearSegments();
    void updateActiveSegment();
    void markSegmentsHandledAt(qint64 positionMs);
    void skipSegment(const SkipSegment& segment, bool automatic);
    bool autoSkipEnabled(SegmentKind kind) const;

    IPlaybackBackend* m_backend;
    EpisodeQueueManager* m_queue;
    PlaybackProgressManager* m_progress;
    LibraryManager* m_library;
    SettingsManager* m_settings;

    PlaybackState m_state = PlaybackState::Idle;
    QueueEntry m_current;
    quint64 m_token = 0;  // token of the load we currently care about (0 = none)
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_trackingPosition = true;

    // Openings/endings of the current file, from its chapters. A segment whose
    // start is in m_handled is never skipped automatically again: it was skipped
    // already, or the user seeked into it on purpose. Both reset with every load.
    QVector<ChapterInfo> m_chapters;
    SeasonSkipMarks m_marks;  // of the season of the current episode, the fallback where chapters say nothing
    qint64 m_pendingIntroStartMs = -1;
    QVector<SkipSegment> m_segments;
    SkipSegment m_activeSegment;
    QSet<qint64> m_handled;   // SkipSegment::key()

    QTimer m_advanceTimer;
    QElapsedTimer m_advanceClock;
    int m_advanceDelayMs = 0;
    QueueEntry m_pendingNext;
    bool m_shutDown = false;
};

}  // namespace lf

Q_DECLARE_METATYPE(lf::PlaybackState)
Q_DECLARE_METATYPE(lf::PlaybackFailure)
