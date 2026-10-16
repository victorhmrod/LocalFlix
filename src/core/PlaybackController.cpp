#include "PlaybackController.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPlayback, "localflix.playback")

namespace lf {

PlaybackController::PlaybackController(IPlaybackBackend* backend, EpisodeQueueManager* queue, PlaybackProgressManager* progress,
                                       LibraryManager* library, SettingsManager* settings, QObject* parent)
    : QObject(parent), m_backend(backend), m_queue(queue), m_progress(progress), m_library(library), m_settings(settings)
{
    qRegisterMetaType<lf::PlaybackState>("lf::PlaybackState");
    qRegisterMetaType<lf::PlaybackFailure>("lf::PlaybackFailure");
    qRegisterMetaType<lf::QueueEntry>("lf::QueueEntry");
    qRegisterMetaType<lf::EndFileInfo>("lf::EndFileInfo");
    qRegisterMetaType<QVector<lf::TrackInfo>>("QVector<lf::TrackInfo>");
    qRegisterMetaType<QVector<lf::ChapterInfo>>("QVector<lf::ChapterInfo>");
    qRegisterMetaType<lf::SkipSegment>("lf::SkipSegment");

    connect(m_backend, &IPlaybackBackend::fileLoaded, this, &PlaybackController::onFileLoaded);
    connect(m_backend, &IPlaybackBackend::endFile, this, &PlaybackController::onEndFile);
    connect(m_backend, &IPlaybackBackend::positionChanged, this, &PlaybackController::onPosition);
    connect(m_backend, &IPlaybackBackend::durationChanged, this, &PlaybackController::onDuration);
    connect(m_backend, &IPlaybackBackend::pausedChanged, this, &PlaybackController::onPausedChanged);
    connect(m_backend, &IPlaybackBackend::tracksChanged, this, &PlaybackController::tracksChanged);
    connect(m_backend, &IPlaybackBackend::chaptersChanged, this, &PlaybackController::onChapters);
    connect(m_library, &LibraryManager::skipMarksChanged, this, &PlaybackController::onSkipMarksChanged);
    connect(m_queue, &EpisodeQueueManager::queueChanged, this, &PlaybackController::onQueueChanged);

    m_advanceTimer.setSingleShot(true);
    connect(&m_advanceTimer, &QTimer::timeout, this, &PlaybackController::onAdvanceTimeout);
}

void PlaybackController::setState(PlaybackState s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

bool PlaybackController::hasNext() const
{
    return m_queue->next(false).valid();
}

bool PlaybackController::hasPrevious() const
{
    return m_queue->previous().valid();
}

QueueEntry PlaybackController::nextForAdvance() const
{
    return m_queue->next(m_settings->skipCompleted());
}

// ------------------------------------------------------------------ starting

bool PlaybackController::play(const QueueContext& context, Id mediaFileId, StartMode mode)
{
    if (m_shutDown) return false;
    if (!(m_queue->context() == context)) m_queue->setContext(context);
    // A fresh viewing session starts at the configured default speed.
    m_backend->setSpeed(m_settings->playbackSpeed());
    return playEntry(mediaFileId, mode);
}

bool PlaybackController::playEntry(Id mediaFileId, StartMode mode)
{
    if (m_shutDown) return false;

    const QueueEntry entry = m_queue->entryForMedia(mediaFileId);
    if (!entry.valid()) {
        emit playbackFailed(QueueEntry{}, {FailureKind::NotInQueue, QStringLiteral("That item is not part of the current playback queue.")});
        return false;
    }

    // "Resume automatically" off: let the UI ask before touching anything.
    if (mode == StartMode::Auto && !entry.completed && entry.positionMs > 0 && !m_settings->resumeAutomatically()) {
        emit resumeDecisionNeeded(entry, entry.positionMs);
        return false;
    }

    // Leave whatever is active: stop the countdown, save the old position and
    // invalidate its token so its trailing END_FILE (reason Stop) is ignored.
    if (m_state == PlaybackState::AdvancePending) {
        m_advanceTimer.stop();
        emit autoAdvanceCancelled();
    }
    finishActiveItem(FlushReason::Switch);

    qint64 startMs = 0;
    if (!entry.completed && mode != StartMode::FromStart) startMs = entry.positionMs;

    if (!QFileInfo::exists(entry.path)) {
        m_backend->stop();
        m_current = entry;
        m_queue->setCurrentMedia(entry.mediaFileId);
        m_library->setMissing(entry.mediaFileId, true);
        setState(PlaybackState::Idle);
        emit playbackFailed(entry, {FailureKind::MissingFile,
                                    QStringLiteral("The file for “%1” was not found:\n%2").arg(entry.displayTitle(), QDir::toNativeSeparators(entry.path))});
        return false;
    }

    beginLoad(entry, startMs);
    return true;
}

void PlaybackController::beginLoad(const QueueEntry& entry, qint64 startMs)
{
    m_current = entry;
    m_positionMs = startMs;
    m_durationMs = entry.durationMs;
    // Replaying a completed item must not overwrite its completed state.
    m_trackingPosition = !entry.completed;
    m_queue->setCurrentMedia(entry.mediaFileId);
    setState(PlaybackState::Loading);
    m_token = m_backend->loadFile(QDir::toNativeSeparators(entry.path), double(startMs) / 1000.0);
    qCInfo(lcPlayback) << "load" << entry.displayTitle() << "at" << startMs << "ms token" << m_token;
}

void PlaybackController::finishActiveItem(FlushReason reason)
{
    m_progress->end(reason);
    m_token = 0;
    clearSegments();
}

// ------------------------------------------------------------------ backend events

void PlaybackController::onFileLoaded(quint64 token)
{
    if (token == 0 || token != m_token || m_state != PlaybackState::Loading) return;

    const double dur = m_backend->durationSeconds();
    if (dur > 0) m_durationMs = qint64(dur * 1000.0);
    setState(m_backend->isPaused() ? PlaybackState::Paused : PlaybackState::Playing);
    m_progress->begin(m_current.mediaFileId, m_trackingPosition, m_durationMs);

    // Restore the audio/subtitle choice made for this file earlier.
    const Progress saved = m_library->progress(m_current.mediaFileId);
    if (saved.audioTrack != kTrackAuto) m_backend->setAudioTrack(saved.audioTrack);
    if (saved.subtitleTrack != kTrackAuto) m_backend->setSubtitleTrack(saved.subtitleTrack);

    emit nowPlaying(m_current, hasPrevious(), hasNext());
    emit positionChanged(m_positionMs, m_durationMs);

    // Chapters are known by now. Starting (or resuming) inside an opening may
    // skip it right away, so this comes after the UI knows what is playing.
    m_chapters = m_backend->chapters();
    m_marks = m_library->seasonSkipMarks(m_current.seasonId);
    rebuildSegments();
    updateActiveSegment();
}

void PlaybackController::onEndFile(lf::EndFileInfo info)
{
    // Anything that is not the load we are tracking is a leftover of a file we
    // already replaced or stopped; it must never complete or advance anything.
    if (info.token == 0 || info.token != m_token) {
        qCDebug(lcPlayback) << "ignoring stale end-file, token" << info.token << "current" << m_token;
        return;
    }
    m_token = 0;
    clearSegments();
    const bool neverStarted = m_state == PlaybackState::Loading;

    switch (info.reason) {
    case EndReason::Eof:
        if (neverStarted) {
            // EOF without a loaded file is a broken file, not a finished one.
            m_progress->end(FlushReason::Stop);
            setState(PlaybackState::Idle);
            emit playbackFailed(m_current, {FailureKind::LoadError,
                                            QStringLiteral("“%1” ended before playback could start.").arg(m_current.displayTitle())});
            return;
        }
        handleNaturalEnd();
        return;
    case EndReason::Error: {
        m_progress->end(FlushReason::Stop);
        setState(PlaybackState::Idle);
        const bool exists = QFileInfo::exists(m_current.path);
        if (!exists) m_library->setMissing(m_current.mediaFileId, true);
        const QString detail = info.errorText.isEmpty() ? QStringLiteral("unknown error") : info.errorText;
        emit playbackFailed(m_current,
                            {exists ? FailureKind::LoadError : FailureKind::MissingFile,
                             exists ? QStringLiteral("“%1” cannot be played: %2").arg(m_current.displayTitle(), detail)
                                    : QStringLiteral("The file for “%1” was not found:\n%2")
                                          .arg(m_current.displayTitle(), QDir::toNativeSeparators(m_current.path))});
        return;
    }
    case EndReason::Stop:
    case EndReason::Quit:
    case EndReason::Redirect:
        m_progress->end(FlushReason::Stop);
        setState(PlaybackState::Idle);
        emit playbackStopped(m_current);
        return;
    }
}

void PlaybackController::handleNaturalEnd()
{
    const QueueEntry finished = m_current;
    m_progress->complete(m_durationMs);
    m_positionMs = 0;
    emit itemCompleted(finished);

    const QueueEntry next = nextForAdvance();
    if (!next.valid()) {
        setState(PlaybackState::Idle);
        emit queueFinished(finished);
        return;
    }
    if (!m_settings->autoPlayNext()) {
        setState(PlaybackState::Idle);
        emit playbackEndedWithoutAdvance(finished, next);
        return;
    }
    startPendingAdvance();
}

void PlaybackController::startPendingAdvance()
{
    m_pendingNext = nextForAdvance();
    m_advanceDelayMs = m_settings->autoPlayCountdownSeconds() * 1000;
    setState(PlaybackState::AdvancePending);
    m_advanceClock.start();
    m_advanceTimer.start(m_advanceDelayMs);
    emit autoAdvancePending(m_pendingNext, m_advanceDelayMs);
}

void PlaybackController::onAdvanceTimeout()
{
    if (m_state != PlaybackState::AdvancePending) return;
    // Recompute: the user may have reordered or removed episodes meanwhile.
    const QueueEntry next = nextForAdvance();
    if (!next.valid()) {
        setState(PlaybackState::Idle);
        emit queueFinished(m_current);
        return;
    }
    playEntry(next.mediaFileId, StartMode::Resume);
}

void PlaybackController::onPosition(double seconds)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    m_positionMs = qint64(seconds * 1000.0);
    m_progress->update(m_positionMs, m_durationMs);
    emit positionChanged(m_positionMs, m_durationMs);
    updateActiveSegment();
}

void PlaybackController::onDuration(double seconds)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    if (seconds <= 0) return;
    m_durationMs = qint64(seconds * 1000.0);
    m_progress->update(m_positionMs, m_durationMs);
    emit positionChanged(m_positionMs, m_durationMs);
    // the end of a last chapter is the duration
    rebuildSegments();
    updateActiveSegment();
}

void PlaybackController::onChapters(QVector<ChapterInfo> chapters)
{
    // While loading, onFileLoaded() reads them from the backend; at any other
    // time they belong to a file that is no longer ours.
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    m_chapters = std::move(chapters);
    rebuildSegments();
    updateActiveSegment();
}

void PlaybackController::onPausedChanged(bool paused)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    setState(paused ? PlaybackState::Paused : PlaybackState::Playing);
    if (paused) m_progress->flush(FlushReason::Pause);
}

void PlaybackController::onQueueChanged()
{
    if (m_state == PlaybackState::AdvancePending) {
        const QueueEntry n = nextForAdvance();
        if (!n.valid()) {
            m_advanceTimer.stop();
            setState(PlaybackState::Idle);
            emit queueFinished(m_current);
            return;
        }
        if (n.mediaFileId != m_pendingNext.mediaFileId) {
            m_pendingNext = n;
            const int remaining = std::max<qint64>(0, m_advanceDelayMs - m_advanceClock.elapsed());
            emit autoAdvancePending(n, remaining);
        }
        return;
    }
    if (m_state == PlaybackState::Idle && !m_current.valid()) return;
    if (const QueueEntry refreshed = m_queue->current(); refreshed.valid()) m_current = refreshed;
    emit queueUpdated(m_current, hasPrevious(), hasNext());
}

// ------------------------------------------------------------------ user commands

void PlaybackController::stop()
{
    if (m_state == PlaybackState::Idle) return;
    if (m_state == PlaybackState::AdvancePending) {
        m_advanceTimer.stop();
        setState(PlaybackState::Idle);
        emit autoAdvanceCancelled();
        return;
    }
    // Save first, then invalidate the token, so the Stop end-file the backend
    // is about to send can neither complete nor advance anything.
    finishActiveItem(FlushReason::Stop);
    m_backend->stop();
    setState(PlaybackState::Idle);
    emit playbackStopped(m_current);
}

void PlaybackController::togglePause()
{
    if (m_state == PlaybackState::Playing)
        setPaused(true);
    else if (m_state == PlaybackState::Paused)
        setPaused(false);
}

void PlaybackController::setPaused(bool paused)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    m_backend->setPaused(paused);
}

void PlaybackController::next()
{
    const QueueEntry n = m_queue->next(false);
    if (n.valid()) playEntry(n.mediaFileId, StartMode::Resume);
}

void PlaybackController::previous()
{
    const QueueEntry p = m_queue->previous();
    if (p.valid()) playEntry(p.mediaFileId, StartMode::Resume);
}

void PlaybackController::advanceNow()
{
    if (m_state != PlaybackState::AdvancePending) return;
    m_advanceTimer.stop();
    onAdvanceTimeout();
}

void PlaybackController::cancelAutoAdvance()
{
    if (m_state != PlaybackState::AdvancePending) return;
    m_advanceTimer.stop();
    setState(PlaybackState::Idle);
    emit autoAdvanceCancelled();
}

void PlaybackController::seekToMs(qint64 positionMs)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    qint64 target = std::max<qint64>(0, positionMs);
    if (m_durationMs > 0) target = std::min(target, m_durationMs);
    markSegmentsHandledAt(target);  // the user went there on purpose: no automatic skip out of it
    seekTo(target);
    updateActiveSegment();
}

void PlaybackController::seekTo(qint64 targetMs)
{
    m_positionMs = targetMs;
    m_progress->update(m_positionMs, m_durationMs);  // so an immediate quit saves the new spot
    m_backend->seekAbsolute(double(targetMs) / 1000.0);
    emit positionChanged(m_positionMs, m_durationMs);
}

void PlaybackController::seekByMs(qint64 deltaMs)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    qint64 target = m_positionMs + deltaMs;
    target = std::max<qint64>(0, target);
    if (m_durationMs > 0) target = std::min(target, m_durationMs);
    markSegmentsHandledAt(target);
    m_positionMs = target;
    m_progress->update(m_positionMs, m_durationMs);
    m_backend->seekRelative(double(deltaMs) / 1000.0);
    emit positionChanged(m_positionMs, m_durationMs);
    updateActiveSegment();
}

// ------------------------------------------------------------------ opening / ending

bool PlaybackController::autoSkipEnabled(SegmentKind kind) const
{
    return kind == SegmentKind::Intro ? m_settings->autoSkipIntro() : m_settings->autoSkipEnding();
}

void PlaybackController::clearSegments()
{
    m_chapters.clear();
    m_marks = SeasonSkipMarks{};
    m_pendingIntroStartMs = -1;
    m_segments.clear();
    m_handled.clear();
    if (m_activeSegment.valid()) {
        m_activeSegment = SkipSegment{};
        emit skipSegmentChanged(m_activeSegment);
    }
}

void PlaybackController::rebuildSegments()
{
    m_segments = skipSegmentsFor(m_chapters, m_marks, m_durationMs);
}

void PlaybackController::markSegmentsHandledAt(qint64 positionMs)
{
    for (const SkipSegment& s : m_segments)
        if (s.contains(positionMs)) m_handled.insert(s.key());
}

// ------------------------------------------------------------------ marks of the season

void PlaybackController::onSkipMarksChanged(Id seasonId)
{
    if ((m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) || seasonId == kInvalidId || seasonId != m_current.seasonId)
        return;
    m_marks = m_library->seasonSkipMarks(seasonId);
    rebuildSegments();
    // Marking where the ending starts puts the playhead inside it: that must not trigger the skip it was meant to set up.
    markSegmentsHandledAt(m_positionMs);
    updateActiveSegment();
}

bool PlaybackController::canMarkSkips() const
{
    return (m_state == PlaybackState::Playing || m_state == PlaybackState::Paused) && m_current.seasonId != kInvalidId;
}

bool PlaybackController::markIntroStartHere(qint64 atMs)
{
    if (!canMarkSkips()) return false;
    m_pendingIntroStartMs = atMs >= 0 ? atMs : m_positionMs;
    return true;
}

bool PlaybackController::markIntroEndHere(qint64 atMs)
{
    const qint64 at = atMs >= 0 ? atMs : m_positionMs;
    if (!canMarkSkips() || m_pendingIntroStartMs < 0 || at - m_pendingIntroStartMs < kMinSkipSegmentMs) return false;
    SeasonSkipMarks marks = m_marks;
    marks.introStartMs = m_pendingIntroStartMs;
    marks.introEndMs = at;
    m_pendingIntroStartMs = -1;
    return m_library->setSeasonSkipMarks(m_current.seasonId, marks);
}

bool PlaybackController::markEndingStartHere(qint64 atMs)
{
    if (!canMarkSkips() || m_durationMs <= 0) return false;
    const qint64 fromEnd = m_durationMs - (atMs >= 0 ? atMs : m_positionMs);
    if (fromEnd < kMinSkipSegmentMs) return false;
    SeasonSkipMarks marks = m_marks;
    marks.endingFromEndMs = fromEnd;
    return m_library->setSeasonSkipMarks(m_current.seasonId, marks);
}

bool PlaybackController::clearIntroMark()
{
    if (!canMarkSkips()) return false;
    SeasonSkipMarks marks = m_marks;
    marks.introStartMs = marks.introEndMs = -1;
    m_pendingIntroStartMs = -1;
    return m_library->setSeasonSkipMarks(m_current.seasonId, marks);
}

bool PlaybackController::clearEndingMark()
{
    if (!canMarkSkips()) return false;
    SeasonSkipMarks marks = m_marks;
    marks.endingFromEndMs = -1;
    return m_library->setSeasonSkipMarks(m_current.seasonId, marks);
}

// Tracks which opening/ending the playhead is in (the Skip button follows it)
// and, if the setting asks for it, skips it. An automatic skip happens at most
// once per segment and only while playing, never while paused, loading or
// counting down; seeking back into a segment that was skipped leaves it alone.
void PlaybackController::updateActiveSegment()
{
    SkipSegment inside;
    if (m_state == PlaybackState::Playing || m_state == PlaybackState::Paused) {
        for (const SkipSegment& s : m_segments) {
            if (s.contains(m_positionMs)) {
                inside = s;
                break;
            }
        }
    }

    const SkipSegment& was = m_activeSegment;
    if (inside.kind != was.kind || inside.startMs != was.startMs || inside.endMs != was.endMs || inside.toEnd != was.toEnd) {
        m_activeSegment = inside;
        emit skipSegmentChanged(inside);
    }

    if (inside.valid() && m_state == PlaybackState::Playing && autoSkipEnabled(inside.kind) && !m_handled.contains(inside.key()))
        skipSegment(inside, /*automatic=*/true);
}

void PlaybackController::skipActiveSegment()
{
    if (m_activeSegment.valid()) skipSegment(m_activeSegment, /*automatic=*/false);
}

void PlaybackController::skipSegment(const SkipSegment& segment, bool automatic)
{
    if (m_state != PlaybackState::Playing && m_state != PlaybackState::Paused) return;
    m_handled.insert(segment.key());
    // For an ending that runs to the end of the file this seeks to the very end:
    // the engine then reports a natural end-of-file, which completes the item and
    // advances exactly as if it had been watched. Nothing here completes anything.
    qint64 target = segment.endMs;
    if (m_durationMs > 0) target = std::min(target, m_durationMs);
    qCInfo(lcPlayback) << (automatic ? "auto-skip" : "skip") << segment.noun() << segment.startMs << "->" << target << "ms";
    // A paused engine seeked to the end parks on the last frame and never reports the end of the file
    // (checked against libmpv in tst_mpv_backend), so the ending would not finish. Resume first.
    if (segment.toEnd && m_state == PlaybackState::Paused) m_backend->setPaused(false);
    seekTo(target);
    emit segmentSkipped(segment, automatic);
    updateActiveSegment();
}

void PlaybackController::shutdown()
{
    if (m_shutDown) return;
    m_shutDown = true;
    m_advanceTimer.stop();
    const bool wasActive = isActive();
    finishActiveItem(FlushReason::Shutdown);
    if (wasActive) m_backend->stop();
    m_state = PlaybackState::Idle;
}

// ------------------------------------------------------------------ audio / video options

void PlaybackController::setVolume(int percent)
{
    m_backend->setVolume(std::clamp(percent, 0, 130));
}

void PlaybackController::setMuted(bool muted)
{
    m_backend->setMuted(muted);
}

void PlaybackController::setSpeed(double speed)
{
    m_backend->setSpeed(std::clamp(speed, 0.25, 4.0));
}

namespace {
int selectedTrackId(const QVector<TrackInfo>& tracks, TrackType type)
{
    for (const TrackInfo& t : tracks)
        if (t.type == type && t.selected) return t.id;
    return kTrackOff;
}
}  // namespace

void PlaybackController::selectAudioTrack(int trackId)
{
    m_backend->setAudioTrack(trackId);
    if (m_current.valid() && isActive())
        m_library->saveTrackSelection(m_current.mediaFileId, trackId, selectedTrackId(m_backend->tracks(), TrackType::Subtitle));
}

void PlaybackController::selectSubtitleTrack(int trackId)
{
    m_backend->setSubtitleTrack(trackId);
    if (m_current.valid() && isActive())
        m_library->saveTrackSelection(m_current.mediaFileId, selectedTrackId(m_backend->tracks(), TrackType::Audio), trackId);
}

bool PlaybackController::addExternalSubtitle(const QString& path)
{
    return m_backend->addExternalSubtitle(path);
}

}  // namespace lf
