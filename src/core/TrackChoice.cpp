#include "TrackChoice.h"

#include <QLocale>

namespace lf {

QString languageKey(const QString& language)
{
    QString code = language.trimmed().toLower();
    for (int i = 0; i < code.size(); ++i) {
        if (code[i] == QLatin1Char('-') || code[i] == QLatin1Char('_')) {  // "pt-BR" -> "pt"
            code.truncate(i);
            break;
        }
    }
    // undetermined, uncoded, no linguistic content: the container says nothing
    if (code.isEmpty() || code == QLatin1String("und") || code == QLatin1String("mis") || code == QLatin1String("zxx")) return {};
    const QLocale::Language known = QLocale::codeToLanguage(code);
    return (known != QLocale::AnyLanguage && known != QLocale::C) ? QStringLiteral("#%1").arg(int(known)) : code;  // "pt" == "por"
}

TrackChoice choiceFromTrack(const TrackInfo& track)
{
    TrackChoice c;
    c.kind = TrackChoice::Kind::Track;
    c.language = track.language.trimmed();
    c.title = track.title.trimmed();
    return c;
}

int findTrackForChoice(const QVector<TrackInfo>& tracks, TrackType type, const TrackChoice& choice)
{
    if (choice.kind != TrackChoice::Kind::Track) return kTrackAuto;
    const QString key = languageKey(choice.language);
    const QString title = choice.title.trimmed();
    if (key.isEmpty() && title.isEmpty()) return kTrackAuto;  // nothing to recognise it by

    QVector<const TrackInfo*> fits;
    for (const TrackInfo& t : tracks) {
        if (t.type != type) continue;
        if (!key.isEmpty() ? languageKey(t.language) == key : t.title.trimmed().compare(title, Qt::CaseInsensitive) == 0) fits.push_back(&t);
    }
    if (fits.isEmpty()) return kTrackAuto;
    if (fits.size() == 1) return fits.first()->id;

    if (!key.isEmpty() && !title.isEmpty())
        for (const TrackInfo* t : fits)
            if (t->title.trimmed().compare(title, Qt::CaseInsensitive) == 0) return t->id;
    for (const TrackInfo* t : fits)
        if (t->selected) return t->id;
    return fits.first()->id;
}

}  // namespace lf
