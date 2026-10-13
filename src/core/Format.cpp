#include "Format.h"

#include <QDateTime>
#include <QLocale>

namespace lf {

QString episodeLabel(int seasonNumber, int episodeNumber, const QString& title)
{
    QString prefix;
    if (seasonNumber > 0 && episodeNumber > 0)
        prefix = QStringLiteral("S%1E%2").arg(seasonNumber).arg(episodeNumber);
    else if (episodeNumber > 0)
        prefix = QStringLiteral("E%1").arg(episodeNumber);
    if (prefix.isEmpty()) return title;
    if (title.isEmpty()) return prefix;
    return prefix + QStringLiteral(" · ") + title;
}

QString formatClock(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatRelativeTime(qint64 whenMs, qint64 nowMs)
{
    if (whenMs <= 0) return QStringLiteral("Never");
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(whenMs);
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(nowMs);
    const qint64 days = when.date().daysTo(now.date());
    if (days <= 0) {
        const qint64 mins = (nowMs - whenMs) / 60000;
        if (mins < 1) return QStringLiteral("Just now");
        if (mins < 60) return QStringLiteral("%1 min ago").arg(mins);
        return QStringLiteral("Today at %1").arg(when.time().toString(QStringLiteral("HH:mm")));
    }
    if (days == 1) return QStringLiteral("Yesterday");
    if (days < 7) return QStringLiteral("%1 days ago").arg(days);
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

QString formatFileSize(qint64 bytes)
{
    const double kb = 1024.0;
    const double b = double(bytes);
    if (b >= kb * kb * kb) return QStringLiteral("%1 GB").arg(b / (kb * kb * kb), 0, 'f', 1);
    if (b >= kb * kb) return QStringLiteral("%1 MB").arg(b / (kb * kb), 0, 'f', 0);
    if (b >= kb) return QStringLiteral("%1 KB").arg(b / kb, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

}  // namespace lf
