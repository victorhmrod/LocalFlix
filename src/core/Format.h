#pragma once

#include "Types.h"

#include <QString>

namespace lf {

// "S1E3 · Title", "E3 · Title" or just "Title", depending on what is known.
QString episodeLabel(int seasonNumber, int episodeNumber, const QString& title);
inline QString episodeLabel(const Episode& e) { return episodeLabel(e.seasonNumber, e.episodeNumber, e.title); }

// "1:02:03" or "12:34".
QString formatClock(qint64 ms);

// "Today at 18:30", "Yesterday", "3 days ago", "12 Mar 2026".
QString formatRelativeTime(qint64 whenMs, qint64 nowMs);

// "1.4 GB", "820 MB".
QString formatFileSize(qint64 bytes);

}  // namespace lf
