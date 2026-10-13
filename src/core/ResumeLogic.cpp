#include "ResumeLogic.h"

namespace lf {

namespace {

ResumeTarget targetFor(const Episode& e, ResumeReason reason, bool useSavedPosition)
{
    ResumeTarget t;
    t.reason = reason;
    t.episodeId = e.id;
    t.mediaFileId = e.file.id;
    t.positionMs = useSavedPosition ? e.progress.resumePositionMs() : 0;
    return t;
}

int firstUnfinished(const QVector<Episode>& eps, int from, int to)
{
    for (int i = from; i < to; ++i)
        if (!eps[i].progress.completed) return i;
    return -1;
}

}  // namespace

ResumeTarget computeResumeTarget(const QVector<Episode>& ordered)
{
    if (ordered.isEmpty()) return {};

    int last = -1;
    qint64 lastWatched = 0;
    for (int i = 0; i < ordered.size(); ++i) {
        if (ordered[i].progress.lastWatchedAt > lastWatched) {
            lastWatched = ordered[i].progress.lastWatchedAt;
            last = i;
        }
    }

    if (last < 0) {
        const int i = firstUnfinished(ordered, 0, int(ordered.size()));
        if (i >= 0) return targetFor(ordered[i], ResumeReason::NotStarted, true);
        return targetFor(ordered.first(), ResumeReason::AllCompleted, false);
    }

    if (!ordered[last].progress.completed) return targetFor(ordered[last], ResumeReason::InProgress, true);

    int next = firstUnfinished(ordered, last + 1, int(ordered.size()));
    if (next < 0) next = firstUnfinished(ordered, 0, last);
    if (next >= 0) return targetFor(ordered[next], ResumeReason::NextUnfinished, true);
    return targetFor(ordered.first(), ResumeReason::AllCompleted, false);
}

}  // namespace lf
