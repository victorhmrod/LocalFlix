#include "CardFactory.h"

#include "Format.h"

#include <QDateTime>

namespace lf::ui::CardFactory {

namespace {

QString movieSubtitle(const Movie& m)
{
    if (m.progress.completed) return QStringLiteral("Watched");
    if (m.progress.started()) {
        const qint64 left = m.progress.durationMs > 0 ? qMax<qint64>(0, m.progress.durationMs - m.progress.positionMs) : 0;
        return left > 0 ? QStringLiteral("%1 left").arg(formatClock(left)) : QStringLiteral("Resume at %1").arg(formatClock(m.progress.positionMs));
    }
    if (m.file.durationMs > 0) return QStringLiteral("Movie · %1").arg(formatClock(m.file.durationMs));
    return QStringLiteral("Movie");
}

}  // namespace

PosterCard* showCard(ApplicationController& app, LibraryActions* actions, const ShowSummary& s, QWidget* parent)
{
    auto* card = new PosterCard(parent);
    PosterCard::Data d;
    d.title = s.show.title;
    d.subtitle = s.episodeCount == 0 ? QStringLiteral("No episodes yet")
                                     : QStringLiteral("%1 episode%2 · %3 watched").arg(s.episodeCount).arg(s.episodeCount == 1 ? QString() : QStringLiteral("s")).arg(s.completedCount);
    d.imagePath = app.library().coverPath(s.show.coverFile);
    d.progress = s.episodeCount > 0 ? double(s.completedCount) / s.episodeCount : 0.0;
    d.watched = s.episodeCount > 0 && s.completedCount >= s.episodeCount;
    card->setData(d);
    const Id id = s.show.id;
    QObject::connect(card, &PosterCard::clicked, actions, [actions, id] { emit actions->openShowRequested(id); });
    QObject::connect(card, &PosterCard::contextMenuRequested, actions, [actions, id](const QPoint& p) { actions->showCardMenu(CardKind::Show, id, p); });
    return card;
}

PosterCard* movieCard(ApplicationController& app, LibraryActions* actions, const Movie& m, QWidget* parent)
{
    auto* card = new PosterCard(parent);
    PosterCard::Data d;
    d.title = m.title;
    d.subtitle = movieSubtitle(m);
    d.imagePath = app.library().coverPath(m.posterFile);
    d.progress = m.progress.completed ? 0.0 : m.progress.fraction();
    d.watched = m.progress.completed;
    d.missing = m.file.missing;
    card->setData(d);
    const Id id = m.id;
    const Id media = m.file.id;
    const bool missing = m.file.missing;
    QObject::connect(card, &PosterCard::clicked, actions, [actions, id, media, missing] {
        if (missing)
            actions->locateMissing(media);  // offer recovery instead of failing silently
        else
            emit actions->playMovieRequested(id);
    });
    QObject::connect(card, &PosterCard::contextMenuRequested, actions, [actions, id](const QPoint& p) { actions->showCardMenu(CardKind::Movie, id, p); });
    return card;
}

PosterCard* libraryCard(ApplicationController& app, LibraryActions* actions, const LibraryCard& c, QWidget* parent)
{
    if (c.kind == CardKind::Show) {
        const auto summaries = app.library().showSummaries();
        for (const ShowSummary& s : summaries)
            if (s.show.id == c.id) return showCard(app, actions, s, parent);
    } else if (const auto m = app.library().movie(c.id)) {
        return movieCard(app, actions, *m, parent);
    }
    return nullptr;
}

ContinueCard* continueCard(ApplicationController& app, LibraryActions* actions, const ContinueItem& item, QWidget* parent)
{
    auto* card = new ContinueCard(parent);
    ContinueCard::Data d;
    d.item = item;
    d.coverPath = app.library().coverPath(item.coverFile);
    d.nowMs = QDateTime::currentMSecsSinceEpoch();
    card->setData(d);
    const QueueContext ctx = item.kind == MediaKind::Movie ? QueueContext{ContextType::Movie, item.movieId} : QueueContext{ContextType::Show, item.showId};
    QObject::connect(card, &ContinueCard::resumeRequested, actions, [actions, item, ctx] {
        if (item.missing)
            actions->locateMissing(item.mediaFileId);
        else
            emit actions->playMediaRequested(ctx, item.mediaFileId, StartMode::Resume);
    });
    QObject::connect(card, &ContinueCard::menuRequested, actions, [actions, item](const QPoint& p) { actions->continueMenu(item, p); });
    return card;
}

}  // namespace lf::ui::CardFactory
