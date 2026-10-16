#include "SkipSegments.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace lf {

namespace {

const QSet<QString>& introWords()
{
    static const QSet<QString> w = {QStringLiteral("opening"), QStringLiteral("intro"), QStringLiteral("abertura"), QStringLiteral("ncop")};
    return w;
}

const QSet<QString>& endingWords()
{
    static const QSet<QString> w = {QStringLiteral("ending"),      QStringLiteral("credits"),   QStringLiteral("creditos"),
                                    QStringLiteral("créditos"),    QStringLiteral("outro"),     QStringLiteral("encerramento"),
                                    QStringLiteral("nced")};
    return w;
}

const QSet<QString>& previewWords()
{
    static const QSet<QString> w = {QStringLiteral("preview"), QStringLiteral("previa"),  QStringLiteral("prévia"),
                                    QStringLiteral("trailer"), QStringLiteral("next"),    QStringLiteral("proximo"),
                                    QStringLiteral("próximo")};
    return w;
}

// Words that may accompany a keyword without turning the chapter into content:
// "Opening Theme", "End Credits", "Ending 2", "Next Episode".
const QSet<QString>& fillerWords()
{
    static const QSet<QString> w = {QStringLiteral("the"),   QStringLiteral("theme"), QStringLiteral("song"),    QStringLiteral("sequence"),
                                    QStringLiteral("part"),  QStringLiteral("end"),   QStringLiteral("closing"), QStringLiteral("final"),
                                    QStringLiteral("of"),    QStringLiteral("de"),    QStringLiteral("do"),      QStringLiteral("da"),
                                    QStringLiteral("tema"),  QStringLiteral("musica"), QStringLiteral("música"), QStringLiteral("episode"),
                                    QStringLiteral("episodio"), QStringLiteral("episódio"), QStringLiteral("ep")};
    return w;
}

bool isHeadTerminator(const QString& s, int i)
{
    const QChar c = s[i];
    if (QStringLiteral(":([{)]}|–—").contains(c)) return true;
    // a spaced hyphen separates a name from a description ("OP - Song"); a plain one joins words ("Post-Credits")
    return c == QLatin1Char('-') && i > 0 && i + 1 < s.size() && s[i - 1].isSpace() && s[i + 1].isSpace();
}

// The part of the title before the first separator, e.g. "Opening" of "Opening: Song name".
QString titleHead(const QString& title)
{
    int start = 0;
    while (start < title.size() && (title[start].isSpace() || QStringLiteral("([{").contains(title[start]))) ++start;  // "[OP] Song"
    int end = start;
    while (end < title.size() && !isHeadTerminator(title, end)) ++end;
    return title.mid(start, end - start);
}

QStringList words(const QString& head)
{
    QStringList out;
    QString current;
    auto flush = [&] {
        if (!current.isEmpty()) out << current;
        current.clear();
    };
    for (const QChar c : head.toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('\'') || c == QChar(0x2019))
            current += c;
        else
            flush();
    }
    flush();
    return out;
}

bool isNumber(const QString& w)
{
    return !w.isEmpty() && std::all_of(w.begin(), w.end(), [](QChar c) { return c.isDigit(); });
}

// "op1", "ed2": the abbreviation with its number glued to it.
bool isAbbreviation(const QString& w, const QString& abbr)
{
    if (!w.startsWith(abbr)) return false;
    return isNumber(w.mid(abbr.size()));  // "op" itself is the empty string -> not a number
}

bool isAbbreviationWord(const QString& w, const QString& abbr)
{
    return w == abbr || isAbbreviation(w, abbr);
}

qint64 toMs(double seconds)
{
    return seconds <= 0.0 ? 0 : qint64(std::llround(seconds * 1000.0));
}

}  // namespace

ChapterRole classifyChapterTitle(const QString& title)
{
    const QStringList ws = words(titleHead(title));
    if (ws.isEmpty()) return ChapterRole::Other;

    bool intro = false;
    bool ending = false;
    bool preview = false;
    for (const QString& w : ws) {
        if (isNumber(w) || fillerWords().contains(w)) continue;
        if (introWords().contains(w) || isAbbreviationWord(w, QStringLiteral("op"))) {
            intro = true;
        } else if (endingWords().contains(w) || isAbbreviationWord(w, QStringLiteral("ed"))) {
            ending = true;
        } else if (previewWords().contains(w)) {
            preview = true;
        } else {
            return ChapterRole::Other;  // some other word: this is content ("Opening Night")
        }
    }
    // Mixed keywords ("Opening Ending") are not something we understand.
    if (int(intro) + int(ending) + int(preview) != 1) return ChapterRole::Other;
    return intro ? ChapterRole::Intro : (ending ? ChapterRole::Ending : ChapterRole::Preview);
}

QVector<SkipSegment> detectSkipSegments(const QVector<ChapterInfo>& input, qint64 durationMs)
{
    QVector<ChapterInfo> chapters = input;
    std::stable_sort(chapters.begin(), chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) { return a.startSeconds < b.startSeconds; });

    const int n = int(chapters.size());
    QVector<ChapterRole> roles;
    roles.reserve(n);
    for (const ChapterInfo& c : chapters) roles.push_back(classifyChapterTitle(c.title));

    QVector<SkipSegment> out;
    for (int i = 0; i < n; ++i) {
        const ChapterRole role = roles[i];
        if (role != ChapterRole::Intro && role != ChapterRole::Ending) continue;

        int next = i + 1;
        while (next < n && (roles[next] == role || (role == ChapterRole::Ending && roles[next] == ChapterRole::Preview))) ++next;

        SkipSegment seg;
        seg.kind = role == ChapterRole::Intro ? SegmentKind::Intro : SegmentKind::Ending;
        seg.title = chapters[i].title;
        seg.startMs = toMs(chapters[i].startSeconds);
        seg.toEnd = next >= n;
        seg.endMs = seg.toEnd ? durationMs : toMs(chapters[next].startSeconds);
        if (durationMs > 0 && seg.endMs >= durationMs) {
            seg.endMs = durationMs;
            seg.toEnd = true;
        }
        i = next - 1;

        if (seg.endMs - seg.startMs >= kMinSkipSegmentMs) out.push_back(seg);
    }
    return out;
}

QVector<SkipSegment> skipSegmentsFor(const QVector<ChapterInfo>& chapters, const SeasonSkipMarks& marks, qint64 durationMs)
{
    QVector<SkipSegment> out = detectSkipSegments(chapters, durationMs);
    auto has = [&out](SegmentKind kind) {
        return std::any_of(out.begin(), out.end(), [kind](const SkipSegment& s) { return s.kind == kind; });
    };

    if (!has(SegmentKind::Intro) && marks.hasIntro() && marks.introEndMs - marks.introStartMs >= kMinSkipSegmentMs &&
        (durationMs <= 0 || marks.introEndMs <= durationMs)) {
        SkipSegment s;
        s.kind = SegmentKind::Intro;
        s.startMs = marks.introStartMs;
        s.endMs = marks.introEndMs;
        s.title = QStringLiteral("Season mark");
        out.push_back(s);
    }
    if (!has(SegmentKind::Ending) && marks.hasEnding() && marks.endingFromEndMs >= kMinSkipSegmentMs && durationMs > 0 &&
        marks.endingFromEndMs < durationMs) {
        SkipSegment s;
        s.kind = SegmentKind::Ending;
        s.startMs = durationMs - marks.endingFromEndMs;
        s.endMs = durationMs;
        s.toEnd = true;
        s.title = QStringLiteral("Season mark");
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const SkipSegment& a, const SkipSegment& b) { return a.startMs < b.startMs; });
    return out;
}

}  // namespace lf
