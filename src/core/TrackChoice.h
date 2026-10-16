#pragma once

#include "IPlaybackBackend.h"
#include "Types.h"

#include <QString>
#include <QVector>

namespace lf {

// The language of a track in a form that makes "pt", "por" and "pt-BR" the same, and empty when the container
// does not say ("", "und").
QString languageKey(const QString& language);

// What a track is, as a choice that can be found again in another file.
TrackChoice choiceFromTrack(const TrackInfo& track);

// The id of the track of `type` in `tracks` that `choice` names, or kTrackAuto when none does (the engine's own
// choice then stands). The language decides; the title only picks among tracks of that language, and a choice
// without a language matches only a track with exactly its title. When several tracks fit equally, the one the
// engine already selected wins, so nothing is switched needlessly.
int findTrackForChoice(const QVector<TrackInfo>& tracks, TrackType type, const TrackChoice& choice);

}  // namespace lf
