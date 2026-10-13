#pragma once

#include "Types.h"

namespace lf {

// Decides what "Continue" should play for a show.
//
// `ordered` must be the show's episodes in the user's playback order. Rules:
//  1. The reference episode is the one watched most recently.
//  2. If it was left partway, resume it at its saved position.
//  3. If it was completed, take the next unfinished episode after it in the
//     configured order (wrapping to earlier unfinished ones if none follow).
//  4. If nothing was ever played, start at the first unfinished episode.
//  5. If every episode is completed, target the first episode with
//     ResumeReason::AllCompleted so the UI can offer "Watch again".
ResumeTarget computeResumeTarget(const QVector<Episode>& ordered);

}  // namespace lf
