#pragma once

#include <QString>

namespace lf {

struct ParsedName {
    QString title;     // cleaned display title
    int season = -1;   // -1 = not found in the file name
    int episode = 0;   // 0 = not found
    bool episodeMarkerFound = false;  // true for SxxEyy / 1x02 / "Episode 4" style markers
};

// Extracts season/episode numbers and a display title from a file name
// (without directory). Understands S01E02, 1x02, "Episode 4", "Ep04",
// "Show - 05 [1080p]" and, as a last resort, a trailing number.
ParsedName parseFileName(const QString& fileName);

// Season hint from a folder name: "Season 2", "Temporada 03", "S2", "Specials" (= 0).
// Returns -1 when the folder name carries no season information.
int seasonFromFolderName(const QString& folderName);

// Natural, case-insensitive ordering: "ep2" < "ep10".
int naturalCompare(const QString& a, const QString& b);

// "my_movie.2019.1080p.x264" -> "My Movie 2019".
QString prettifyTitle(const QString& baseName);

}  // namespace lf
