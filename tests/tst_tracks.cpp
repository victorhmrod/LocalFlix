// The audio and subtitle choice of a show: made in one episode, found again in the next by what the track is
// (language, title) and not by its number, per kind, per show, and stored with the library.
#include "TestSupport.h"

#include "TrackChoice.h"

using namespace lf;
using namespace lf::test;

namespace {

TrackInfo audio(int id, const char* lang, const char* title = "", bool selected = false)
{
    TrackInfo t;
    t.id = id;
    t.type = TrackType::Audio;
    t.language = QString::fromLatin1(lang);
    t.title = QString::fromLatin1(title);
    t.selected = selected;
    return t;
}

TrackInfo sub(int id, const char* lang, const char* title = "", bool selected = false, bool external = false)
{
    TrackInfo t = audio(id, lang, title, selected);
    t.type = TrackType::Subtitle;
    t.external = external;
    return t;
}

TrackChoice choice(const char* lang, const char* title = "")
{
    TrackChoice c;
    c.kind = TrackChoice::Kind::Track;
    c.language = QString::fromLatin1(lang);
    c.title = QString::fromLatin1(title);
    return c;
}

// An episode's file reports `tracks`, then it is played from the start.
bool play(Env& env, Id showId, const Episode& e, const QVector<TrackInfo>& tracks)
{
    env.backend->setTracks(tracks);
    if (!env.app->playback().play(showContext(showId), e.file.id, StartMode::FromStart)) return false;
    startAndLoad(env.backend);
    return env.app->playback().state() == PlaybackState::Playing;
}

// Moves on to the next episode of the show, whose file reports `tracks`.
bool next(Env& env, const QVector<TrackInfo>& tracks)
{
    env.backend->setTracks(tracks);
    env.app->playback().next();
    startAndLoad(env.backend);
    return env.app->playback().state() == PlaybackState::Playing;
}

}  // namespace

class TstTracks : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ recognising a track

    void languagesAreComparedByMeaningNotSpelling()
    {
        QCOMPARE(languageKey(QStringLiteral("por")), languageKey(QStringLiteral("pt")));
        QCOMPARE(languageKey(QStringLiteral("POR")), languageKey(QStringLiteral("pt-BR")));
        QCOMPARE(languageKey(QStringLiteral("pt_PT")), languageKey(QStringLiteral("por")));
        QCOMPARE(languageKey(QStringLiteral("eng")), languageKey(QStringLiteral("en")));
        QVERIFY(languageKey(QStringLiteral("por")) != languageKey(QStringLiteral("eng")));
        QVERIFY(languageKey(QString()).isEmpty());
        QVERIFY(languageKey(QStringLiteral("und")).isEmpty());
        QVERIFY(languageKey(QStringLiteral("zxx")).isEmpty());
        QVERIFY(!languageKey(QStringLiteral("xx-unknown-code")).isEmpty());  // a code nobody knows still compares with itself
    }

    void aChoiceFindsItsTrackWhateverTheNumbers()
    {
        const QVector<TrackInfo> first = {audio(1, "eng", "EN", true), audio(2, "por", "BR")};
        const QVector<TrackInfo> second = {audio(1, "por", "BR"), audio(2, "eng", "EN", true)};
        QCOMPARE(findTrackForChoice(first, TrackType::Audio, choice("por", "BR")), 2);
        QCOMPARE(findTrackForChoice(second, TrackType::Audio, choice("por", "BR")), 1);
        QCOMPARE(findTrackForChoice(second, TrackType::Audio, choice("pt")), 1);  // spelled differently
    }

    void nothingFitsMeansTheEngineDecides()
    {
        const QVector<TrackInfo> tracks = {audio(1, "eng", "EN", true), sub(1, "por")};
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("jpn")), kTrackAuto);
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("por")), kTrackAuto);  // por exists, but as a subtitle
        QCOMPARE(findTrackForChoice(tracks, TrackType::Subtitle, choice("por")), 1);
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, TrackChoice{}), kTrackAuto);  // no choice
        TrackChoice off;
        off.kind = TrackChoice::Kind::Off;
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, off), kTrackAuto);  // "off" is not a track
        QCOMPARE(findTrackForChoice({}, TrackType::Audio, choice("por")), kTrackAuto);
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("", "")), kTrackAuto);  // nothing to recognise it by
    }

    void theTitleOnlyBreaksTiesBetweenTracksOfTheSameLanguage()
    {
        const QVector<TrackInfo> tracks = {audio(1, "por", "BR", true), audio(2, "por", "PT"), audio(3, "eng", "PT")};
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("por", "PT")), 2);  // not 3: the language comes first
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("por", "pt")), 2);  // case does not matter
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("por", "Commentary")), 1);  // no title fits: the selected one stays
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("por")), 1);
    }

    void aChoiceWithoutALanguageMatchesOnlyAnExactTitle()
    {
        const QVector<TrackInfo> tracks = {audio(1, "eng", "Original", true), audio(2, "por", "Dublado")};
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("", "Dublado")), 2);
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("", "Dub")), kTrackAuto);  // not "anything similar"
        QCOMPARE(findTrackForChoice(tracks, TrackType::Audio, choice("und", "Dublado")), 2);    // "undetermined" is no language
    }

    // ------------------------------------------------------------ following the show

    void anAudioChoiceFollowsTheShowToTheNextEpisodeWhateverTheTrackNumbers()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Dub"), 3, &showId);
        auto& pc = env.app->playback();
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "EN", true), audio(2, "por", "BR"), sub(1, "por", "BR", true)}));
        QVERIFY(env.backend->audioTrackRequests.isEmpty());  // nothing chosen yet: the engine's pick stands

        pc.selectAudioTrack(2);  // the user switches to Portuguese
        QCOMPARE(env.backend->audioTrackRequests.last(), 2);

        // the next episode lists them the other way round: Portuguese is track 1 now
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "por", "BR"), audio(2, "eng", "EN", true), sub(1, "por", "BR", true)}));
        QCOMPARE(env.backend->audioTrackRequests.size(), 1);
        QCOMPARE(env.backend->audioTrackRequests.last(), 1);

        // and so on for the one after, spelled with the two-letter code
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "eng", "", true), audio(2, "pt")}));
        QCOMPARE(env.backend->audioTrackRequests.last(), 2);
    }

    void changingTheAudioDoesNotSetAnySubtitlePreference()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("AudioOnly"), 2, &showId);
        auto& pc = env.app->playback();
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "EN", true), audio(2, "por", "BR")}));  // no subtitle selected at all
        pc.selectAudioTrack(2);

        const ShowTrackPrefs prefs = env.app->library().showTrackPrefs(showId);
        QCOMPARE(int(prefs.audio.kind), int(TrackChoice::Kind::Track));
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Auto));  // not "off"

        env.backend->subtitleTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "eng", "EN", true), audio(2, "por", "BR"), sub(1, "por", "BR", true)}));
        QVERIFY(env.backend->subtitleTrackRequests.isEmpty());  // the engine's subtitle stays
    }

    void changingTheSubtitleDoesNotSetAnAudioPreference()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("SubOnly"), 2, &showId);
        auto& pc = env.app->playback();
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "EN", true), sub(1, "eng"), sub(2, "por", "BR")}));
        pc.selectSubtitleTrack(2);
        const ShowTrackPrefs prefs = env.app->library().showTrackPrefs(showId);
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Track));
        QCOMPARE(int(prefs.audio.kind), int(TrackChoice::Kind::Auto));
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "jpn", "", true), sub(1, "por", "BR")}));
        QVERIFY(env.backend->audioTrackRequests.isEmpty());
        QCOMPARE(env.backend->subtitleTrackRequests.last(), 1);
    }

    void turningTheSubtitlesOffStaysOffInTheNextEpisode()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("NoSubs"), 2, &showId);
        auto& pc = env.app->playback();
        QVERIFY(play(env, showId, eps[0], {audio(1, "jpn", "", true), sub(1, "por", "BR", true)}));  // a subtitle on by default
        pc.selectSubtitleTrack(kTrackOff);

        env.backend->subtitleTrackRequests.clear();
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "jpn", "", true), sub(1, "por", "BR", true)}));
        QCOMPARE(env.backend->subtitleTrackRequests.size(), 1);
        QCOMPARE(env.backend->subtitleTrackRequests.last(), kTrackOff);
        QVERIFY(env.backend->audioTrackRequests.isEmpty());
    }

    void sameLanguageTracksAreToldApartByTitle()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Titles"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "por", "BR", true), audio(2, "por", "PT")}));
        env.app->playback().selectAudioTrack(2);  // the European one
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "por", "PT"), audio(2, "por", "BR", true)}));
        QCOMPARE(env.backend->audioTrackRequests.last(), 1);
    }

    void aLanguageTheNextEpisodeLacksLeavesTheEnginesChoice()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Missing"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por")}));
        env.app->playback().selectAudioTrack(2);
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "eng", "", true), audio(2, "jpn")}));  // no Portuguese here
        QVERIFY(env.backend->audioTrackRequests.isEmpty());
        QCOMPARE(int(env.app->library().showTrackPrefs(showId).audio.kind), int(TrackChoice::Kind::Track));  // still the show's choice
    }

    void aTrackThatIsAlreadySelectedIsNotSwitchedAgain()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Already"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por")}));
        env.app->playback().selectAudioTrack(2);
        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "eng"), audio(2, "por", "", true)}));  // this file already starts in Portuguese
        QVERIFY(env.backend->audioTrackRequests.isEmpty());
    }

    void anotherShowIsNotAffected()
    {
        Env env;
        Id showA = 0;
        Id showB = 0;
        const auto a = env.makeShow(QStringLiteral("Show A"), 2, &showA);
        const auto b = env.makeShow(QStringLiteral("Show B"), 2, &showB);
        QVERIFY(play(env, showA, a[0], {audio(1, "eng", "", true), audio(2, "por")}));
        env.app->playback().selectAudioTrack(2);

        env.backend->audioTrackRequests.clear();
        QVERIFY(play(env, showB, b[0], {audio(1, "eng", "", true), audio(2, "por")}));
        QVERIFY(env.backend->audioTrackRequests.isEmpty());
        QCOMPARE(int(env.app->library().showTrackPrefs(showB).audio.kind), int(TrackChoice::Kind::Auto));
    }

    void theChoiceSurvivesClosingTheApplication()
    {
        Env env;
        Id showId = 0;
        auto eps = env.makeShow(QStringLiteral("Restart"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por", "BR"), sub(1, "por", "BR", true)}));
        env.app->playback().selectAudioTrack(2);
        env.app->playback().selectSubtitleTrack(kTrackOff);

        env.restart();
        eps = env.app->library().episodes(showId);
        QVERIFY(play(env, showId, eps[1], {audio(1, "por", "BR"), audio(2, "eng", "", true), sub(1, "por", "BR", true)}));
        QCOMPARE(env.backend->audioTrackRequests.last(), 1);
        QCOMPARE(env.backend->subtitleTrackRequests.last(), kTrackOff);
    }

    void startingAnEpisodeNeverWritesAPreference()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("ReadOnlyLoad"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por"), sub(1, "por", "", true)}));
        QVERIFY(next(env, {audio(1, "eng", "", true), audio(2, "por"), sub(1, "por", "", true)}));
        const ShowTrackPrefs prefs = env.app->library().showTrackPrefs(showId);
        QCOMPARE(int(prefs.audio.kind), int(TrackChoice::Kind::Auto));
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Auto));
    }

    void aSubtitleLoadedFromAFileBelongsToThatEpisodeOnly()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("External"), 2, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), sub(1, "por", "BR"), sub(2, "por", "extra", true, /*external=*/true)}));
        env.app->playback().selectSubtitleTrack(2);
        QCOMPARE(int(env.app->library().showTrackPrefs(showId).subtitle.kind), int(TrackChoice::Kind::Auto));
    }

    void aTrackThatCannotBeToldApartForgetsTheOldChoice()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Unlabelled"), 1, &showId);
        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por"), audio(3, "")}));
        env.app->playback().selectAudioTrack(2);
        QCOMPARE(int(env.app->library().showTrackPrefs(showId).audio.kind), int(TrackChoice::Kind::Track));
        env.app->playback().selectAudioTrack(3);  // no language, no title: nothing to find it by in another file
        QCOMPARE(int(env.app->library().showTrackPrefs(showId).audio.kind), int(TrackChoice::Kind::Auto));
    }

    // ------------------------------------------------------------ older libraries and movies

    void aChoiceMadeForOneFileBeforeBecomesTheShowsWhenItWasDeliberate()
    {
        Env env;
        Id showId = 0;
        const auto eps = env.makeShow(QStringLiteral("Legacy"), 2, &showId);
        // as written by earlier versions: audio track 2 and subtitle track 1 chosen for episode 1 only
        env.app->library().saveTrackSelection(eps[0].file.id, 2, 1);

        QVERIFY(play(env, showId, eps[0], {audio(1, "eng", "", true), audio(2, "por"), sub(1, "por", "", true)}));
        QCOMPARE(env.backend->audioTrackRequests.last(), 2);  // restored for that file as before
        const ShowTrackPrefs prefs = env.app->library().showTrackPrefs(showId);
        QCOMPARE(int(prefs.audio.kind), int(TrackChoice::Kind::Track));   // differs from the engine's own pick: deliberate
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Auto));  // the same as the engine's own pick: not a choice

        env.backend->audioTrackRequests.clear();
        QVERIFY(next(env, {audio(1, "por"), audio(2, "eng", "", true)}));
        QCOMPARE(env.backend->audioTrackRequests.last(), 1);  // and the other episodes follow
    }

    void aMovieKeepsItsChoiceForItsOwnFile()
    {
        Env env;
        auto& lib = env.app->library();
        env.app->importer().importMoviesSync({env.makeFile(QStringLiteral("mv/film.mkv"))});
        const Movie movie = lib.movies().first();
        auto& pc = env.app->playback();
        env.backend->setTracks({audio(1, "eng", "", true), audio(2, "por")});
        QVERIFY(pc.play({ContextType::Movie, movie.id}, movie.file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        pc.selectAudioTrack(2);

        env.backend->audioTrackRequests.clear();
        QVERIFY(pc.play({ContextType::Movie, movie.id}, movie.file.id, StartMode::FromStart));
        startAndLoad(env.backend);
        QCOMPARE(env.backend->audioTrackRequests.last(), 2);  // by number, for this file, as before
        QCOMPARE(int(lib.showTrackPrefs(kInvalidId).audio.kind), int(TrackChoice::Kind::Auto));  // there is no show
    }

    // ------------------------------------------------------------ storage

    void theChoiceIsStoredPerKindAndGoesWithTheShow()
    {
        Env env;
        Id showId = 0;
        env.makeShow(QStringLiteral("Stored"), 1, &showId);
        auto& lib = env.app->library();
        QCOMPARE(int(lib.showTrackPrefs(showId).audio.kind), int(TrackChoice::Kind::Auto));

        lib.saveShowTrackChoice(showId, TrackType::Audio, choice("por", "BR"));
        TrackChoice off;
        off.kind = TrackChoice::Kind::Off;
        lib.saveShowTrackChoice(showId, TrackType::Subtitle, off);
        ShowTrackPrefs prefs = lib.showTrackPrefs(showId);
        QCOMPARE(int(prefs.audio.kind), int(TrackChoice::Kind::Track));
        QCOMPARE(prefs.audio.language, QStringLiteral("por"));
        QCOMPARE(prefs.audio.title, QStringLiteral("BR"));
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Off));

        lib.saveShowTrackChoice(showId, TrackType::Audio, choice("eng"));  // replaces one kind and leaves the other
        prefs = lib.showTrackPrefs(showId);
        QCOMPARE(prefs.audio.language, QStringLiteral("eng"));
        QCOMPARE(int(prefs.subtitle.kind), int(TrackChoice::Kind::Off));
        lib.saveShowTrackChoice(showId, TrackType::Audio, TrackChoice{});  // back to "nothing chosen"
        QCOMPARE(int(lib.showTrackPrefs(showId).audio.kind), int(TrackChoice::Kind::Auto));
        QCOMPARE(int(lib.showTrackPrefs(showId).subtitle.kind), int(TrackChoice::Kind::Off));

        QVERIFY(lib.deleteShow(showId));
        QCOMPARE(int(lib.showTrackPrefs(showId).subtitle.kind), int(TrackChoice::Kind::Auto));  // gone with the show
    }
};

QTEST_GUILESS_MAIN(TstTracks)
#include "tst_tracks.moc"
