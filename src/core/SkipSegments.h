#pragma once

#include "IPlaybackBackend.h"
#include "Types.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace lf {

enum class SegmentKind { Intro, Ending };

// What a chapter title says about the chapter.
enum class ChapterRole {
    Other,
    Intro,    // "Opening", "OP1", "Intro", "Abertura"
    Ending,   // "Ending", "ED", "Credits", "Créditos", "Encerramento"
    Preview   // "Preview", "Next episode": only meaningful straight after an ending
};

// A stretch of the playing file that can be skipped: the opening or the ending
// (credits) found through the file's chapters.
struct SkipSegment {
    SegmentKind kind = SegmentKind::Intro;
    qint64 startMs = 0;
    qint64 endMs = 0;      // exclusive; endMs <= startMs means "no segment"
    bool toEnd = false;    // runs to the end of the file: skipping it ends the playback
    QString title;         // title of the first chapter of the segment

    bool valid() const { return endMs > startMs; }
    bool contains(qint64 ms) const { return ms >= startMs && ms < endMs; }
    // Identifies the segment within one file for "already skipped / seeked into". An ending that
    // runs to the end of the file moves with the duration, so it is keyed by that, not by its start.
    qint64 key() const { return toEnd ? -1 - qint64(kind) : startMs; }
    QString label() const { return kind == SegmentKind::Intro ? QStringLiteral("Skip Intro") : QStringLiteral("Skip Ending"); }
    QString noun() const { return kind == SegmentKind::Intro ? QStringLiteral("intro") : QStringLiteral("ending"); }
};

// Segments shorter than this are not worth a button (nor an automatic jump).
constexpr qint64 kMinSkipSegmentMs = 2000;

// Decides from the chapter title alone. Whole words only ("OP" is an opening,
// "Operation" is not), the part after a separator is ignored ("Opening: Song
// name"), and any other word in the title ("Opening Night", "Post-Credits
// Scene") makes it ordinary content.
ChapterRole classifyChapterTitle(const QString& title);

// Opening and ending segments of a file. A segment starts at a chapter whose
// title says so and lasts until the next chapter that does not belong to it
// (neighbouring chapters of the same role are merged; a "Preview" right after
// an ending is part of the ending). It ends at the start of the next other
// chapter, so a post-credits scene that has its own chapter is kept, or at the
// end of the file (`toEnd`). `durationMs <= 0` (unknown) drops a last segment.
QVector<SkipSegment> detectSkipSegments(const QVector<ChapterInfo>& chapters, qint64 durationMs);

// detectSkipSegments() plus the season's marks as a fallback: for each kind, a segment found through the
// chapters wins, and the mark only fills in when the chapters give none of that kind. The opening mark is an
// absolute stretch of the file, the ending mark is "the last N ms" (a segment that runs to the end). A mark
// that does not fit this file (an opening past its end, an ending as long as the whole file) is ignored.
QVector<SkipSegment> skipSegmentsFor(const QVector<ChapterInfo>& chapters, const SeasonSkipMarks& marks, qint64 durationMs);

}  // namespace lf

Q_DECLARE_METATYPE(lf::SkipSegment)
