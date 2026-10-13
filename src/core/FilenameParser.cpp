#include "FilenameParser.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace lf {

namespace {

using RE = QRegularExpression;
constexpr auto kOpts = QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;

const RE& reSxxEyy()
{
    static const RE re(QStringLiteral(R"((?<![A-Za-z0-9])S(\d{1,2})[ ._-]*E(\d{1,4})(?:[ ._-]*E\d{1,4})*(?!\d))"), kOpts);
    return re;
}
const RE& reNxNN()
{
    static const RE re(QStringLiteral(R"((?<![A-Za-z0-9])(\d{1,2})x(\d{2,3})(?![A-Za-z0-9]))"), kOpts);
    return re;
}
const RE& reEpisodeWord()
{
    static const RE re(QStringLiteral(R"((?<![A-Za-z])(?:episode|episodio|episódio|ep|cap|capitulo|capítulo|folge)[ ._-]*(\d{1,4})(?!\d))"), kOpts);
    return re;
}
const RE& reBareE()
{
    static const RE re(QStringLiteral(R"((?<![A-Za-z0-9])E(\d{2,3})(?![A-Za-z0-9]))"), kOpts);
    return re;
}
const RE& reDashNumber()
{
    static const RE re(QStringLiteral(R"([ _.]-[ _.]*(\d{1,4})(?:v\d)?(?![\dA-Za-z]))"), kOpts);
    return re;
}
const RE& reBrackets()
{
    static const RE re(QStringLiteral(R"(\[[^\]]*\]|\([^)]*\)|\{[^}]*\})"));
    return re;
}
const RE& reNoiseTokens()
{
    static const RE re(QStringLiteral(
                           R"((?<![A-Za-z0-9])(?:2160p|1080p|1080i|720p|576p|480p|4k|uhd|x264|x265|h\.?264|h\.?265|hevc|avc|xvid|divx|)"
                           R"(bluray|blu-ray|bdrip|brrip|dvdrip|webrip|web-dl|webdl|hdtv|hdrip|remux|10bit|8bit|aac|ac3|dts|flac|)"
                           R"(dual[ ._-]?audio|multi|proper|repack|extended|uncut)(?![A-Za-z0-9]))"),
                       kOpts);
    return re;
}

QString stripNoise(QString s)
{
    s.remove(reBrackets());
    s.remove(reNoiseTokens());
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    // Dots between words are separators, but keep ones that are part of "1.5".
    s.replace(QRegularExpression(QStringLiteral(R"((?<!\d)\.|\.(?!\d))")), QStringLiteral(" "));
    s = s.simplified();
    // Trim leading/trailing separators such as " - ".
    static const QRegularExpression edge(QStringLiteral(R"(^[\s\-–—:.]+|[\s\-–—:.]+$)"));
    s.remove(edge);
    return s;
}

QString capitalizeIfPlain(const QString& s)
{
    // File names such as "anime_final" read better as "Anime Final". Names that
    // already contain capitals are left alone.
    bool hasUpper = false;
    for (QChar c : s) {
        if (c.isUpper()) {
            hasUpper = true;
            break;
        }
    }
    if (hasUpper || s.isEmpty()) return s;
    QString out;
    out.reserve(s.size());
    bool startOfWord = true;
    for (QChar c : s) {
        out += startOfWord ? c.toUpper() : c;
        startOfWord = c.isSpace();
    }
    return out;
}

bool plausibleEpisodeNumber(int n)
{
    return n > 0 && n < 10000;
}

}  // namespace

QString prettifyTitle(const QString& baseName)
{
    return capitalizeIfPlain(stripNoise(baseName));
}

ParsedName parseFileName(const QString& fileName)
{
    ParsedName out;
    const QString base = QFileInfo(fileName).completeBaseName().isEmpty() ? fileName : QFileInfo(fileName).completeBaseName();

    auto finish = [&](const QString& remainder) {
        const QString cleaned = capitalizeIfPlain(stripNoise(remainder));
        out.title = cleaned.isEmpty() ? QStringLiteral("Episode %1").arg(out.episode) : cleaned;
    };

    // The remainder after a marker is usually the episode title; the part
    // before it is usually the show name and is dropped.
    if (auto m = reSxxEyy().match(base); m.hasMatch()) {
        out.season = m.captured(1).toInt();
        out.episode = m.captured(2).toInt();
        out.episodeMarkerFound = true;
        finish(base.mid(m.capturedEnd(0)));
        return out;
    }
    if (auto m = reNxNN().match(base); m.hasMatch()) {
        out.season = m.captured(1).toInt();
        out.episode = m.captured(2).toInt();
        out.episodeMarkerFound = true;
        finish(base.mid(m.capturedEnd(0)));
        return out;
    }
    if (auto m = reEpisodeWord().match(base); m.hasMatch()) {
        out.episode = m.captured(1).toInt();
        out.episodeMarkerFound = true;
        finish(base.mid(m.capturedEnd(0)));
        return out;
    }
    if (auto m = reBareE().match(base); m.hasMatch()) {
        out.episode = m.captured(1).toInt();
        out.episodeMarkerFound = true;
        finish(base.mid(m.capturedEnd(0)));
        return out;
    }
    if (auto m = reDashNumber().match(base); m.hasMatch()) {
        out.episode = m.captured(1).toInt();
        out.episodeMarkerFound = true;
        finish(base.mid(m.capturedEnd(0)));
        return out;
    }

    // Last resort: the final standalone number that is not a year/resolution.
    out.title = capitalizeIfPlain(stripNoise(base));
    if (out.title.isEmpty()) out.title = base;
    // Resolutions ("1080p", "1920x1080") and codec tags are not episode numbers.
    static const RE resolutionPair(QStringLiteral(R"((?<!\d)\d{3,4}[xX]\d{3,4}(?!\d))"));
    QString noBrackets = QString(base).remove(reBrackets());
    noBrackets.remove(resolutionPair);
    noBrackets.remove(reNoiseTokens());
    static const RE digits(QStringLiteral(R"((?<!\d)(\d{1,4})(?!\d))"));
    int found = 0;
    auto it = digits.globalMatch(noBrackets);
    while (it.hasNext()) {
        const auto m = it.next();
        const int n = m.captured(1).toInt();
        const bool yearLike = m.captured(1).size() == 4 && n >= 1900 && n <= 2100;
        if (!yearLike && plausibleEpisodeNumber(n)) found = n;
    }
    out.episode = found;
    return out;
}

int seasonFromFolderName(const QString& folderName)
{
    const QString name = folderName.trimmed();
    static const RE words(QStringLiteral(R"(^(?:season|temporada|saison|staffel|series)[ ._-]*(\d{1,2})$)"), kOpts);
    static const RE shortForm(QStringLiteral(R"(^S(\d{1,2})$)"), kOpts);
    static const RE ordinal(QStringLiteral(R"(^(\d{1,2})(?:st|nd|rd|th|ª|º|a|°)?[ ._-]*(?:season|temporada)$)"), kOpts);
    static const RE specials(QStringLiteral(R"(^(?:specials?|extras?|ovas?|especiais|bonus)$)"), kOpts);
    if (auto m = words.match(name); m.hasMatch()) return m.captured(1).toInt();
    if (auto m = shortForm.match(name); m.hasMatch()) return m.captured(1).toInt();
    if (auto m = ordinal.match(name); m.hasMatch()) return m.captured(1).toInt();
    if (specials.match(name).hasMatch()) return 0;
    return -1;
}

int naturalCompare(const QString& a, const QString& b)
{
    // Case-insensitive; runs of digits compare by numeric value ("ep2" < "ep10").
    const qsizetype na = a.size();
    const qsizetype nb = b.size();
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < na && j < nb) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            qsizetype si = i;
            qsizetype sj = j;
            while (si < na && a.at(si) == QLatin1Char('0')) ++si;  // skip leading zeros
            while (sj < nb && b.at(sj) == QLatin1Char('0')) ++sj;
            qsizetype ei = si;
            qsizetype ej = sj;
            while (ei < na && a.at(ei).isDigit()) ++ei;
            while (ej < nb && b.at(ej).isDigit()) ++ej;
            const qsizetype lenA = ei - si;
            const qsizetype lenB = ej - sj;
            if (lenA != lenB) return lenA < lenB ? -1 : 1;
            for (qsizetype k = 0; k < lenA; ++k) {
                const QChar da = a.at(si + k);
                const QChar db = b.at(sj + k);
                if (da != db) return da < db ? -1 : 1;
            }
            i = ei;
            j = ej;
            continue;
        }
        const QChar fa = ca.toCaseFolded();
        const QChar fb = cb.toCaseFolded();
        if (fa != fb) return fa < fb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < na) return 1;
    if (j < nb) return -1;
    return 0;
}

}  // namespace lf
