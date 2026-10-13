// File-name parsing: episode/season detection, titles, natural ordering.
#include "FilenameParser.h"

#include <QtTest>

using namespace lf;

class TstParser : public QObject {
    Q_OBJECT

private slots:
    void parsesEpisodeMarkers_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("season");
        QTest::addColumn<int>("episode");
        QTest::addColumn<QString>("title");

        QTest::newRow("sxxeyy") << "Breaking.Bad.S02E05.Breakage.720p.mkv" << 2 << 5 << "Breakage";
        QTest::newRow("sxxeyy dash") << "Show - S01E02 - The Pilot.mp4" << 1 << 2 << "The Pilot";
        QTest::newRow("lowercase") << "show.s3e12.mkv" << 3 << 12 << "Episode 12";
        QTest::newRow("1x02") << "Seinfeld 4x03 The Pitch.avi" << 4 << 3 << "The Pitch";
        QTest::newRow("episode word") << "downloaded_episode_04.mp4" << -1 << 4 << "Episode 4";
        QTest::newRow("episode word 2") << "episode_01.avi" << -1 << 1 << "Episode 1";
        QTest::newRow("Ep") << "Naruto Ep107.mkv" << -1 << 107 << "Episode 107";
        QTest::newRow("E only") << "Show E09.mkv" << -1 << 9 << "Episode 9";
        QTest::newRow("anime dash number") << "[SubGroup] My Anime - 05 [1080p].mkv" << -1 << 5 << "Episode 5";
        QTest::newRow("anime dash number v2") << "My Anime - 12v2.mkv" << -1 << 12 << "Episode 12";
        QTest::newRow("trailing number") << "video_27.mkv" << -1 << 27 << "Video 27";
        QTest::newRow("no number") << "anime_final.mkv" << -1 << 0 << "Anime Final";
        QTest::newRow("resolution is not an episode") << "movie_1080p.mkv" << -1 << 0 << "Movie";
        QTest::newRow("year is not an episode") << "Documentary 2019.mkv" << -1 << 0 << "Documentary 2019";
        QTest::newRow("1920x1080 is not 19x20") << "clip 1920x1080.mkv" << -1 << 0 << "Clip 1920x1080";
        QTest::newRow("portuguese") << "Serie Episodio 12.mkv" << -1 << 12 << "Episode 12";
        QTest::newRow("sxxeyy no space title") << "S01E01Pilot.mkv" << 1 << 1 << "Pilot";
    }

    void parsesEpisodeMarkers()
    {
        QFETCH(QString, file);
        QFETCH(int, season);
        QFETCH(int, episode);
        QFETCH(QString, title);
        const ParsedName p = parseFileName(file);
        QCOMPARE(p.season, season);
        QCOMPARE(p.episode, episode);
        QCOMPARE(p.title, title);
    }

    void seasonFromFolders_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<int>("season");
        QTest::newRow("Season 2") << "Season 2" << 2;
        QTest::newRow("season 03") << "season 03" << 3;
        QTest::newRow("Season.4") << "Season.4" << 4;
        QTest::newRow("S5") << "S5" << 5;
        QTest::newRow("Temporada 6") << "Temporada 6" << 6;
        QTest::newRow("2nd season") << "2nd Season" << 2;
        QTest::newRow("Specials") << "Specials" << 0;
        QTest::newRow("Extras") << "Extras" << 0;
        QTest::newRow("random") << "Downloads" << -1;
        QTest::newRow("show name") << "Breaking Bad (2008)" << -1;
        QTest::newRow("empty") << "" << -1;
    }

    void seasonFromFolders()
    {
        QFETCH(QString, folder);
        QFETCH(int, season);
        QCOMPARE(seasonFromFolderName(folder), season);
    }

    void naturalOrderingSortsNumbersNumerically()
    {
        QStringList names = {QStringLiteral("ep10.mkv"), QStringLiteral("ep2.mkv"), QStringLiteral("Ep1.mkv"), QStringLiteral("ep20.mkv")};
        std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return naturalCompare(a, b) < 0; });
        QCOMPARE(names, QStringList({QStringLiteral("Ep1.mkv"), QStringLiteral("ep2.mkv"), QStringLiteral("ep10.mkv"), QStringLiteral("ep20.mkv")}));
        QCOMPARE(naturalCompare(QStringLiteral("A"), QStringLiteral("a")), 0);
    }

    void prettifyTitleCleansReleaseNoise()
    {
        QCOMPARE(prettifyTitle(QStringLiteral("The.Matrix.1999.1080p.BluRay.x264")), QStringLiteral("The Matrix 1999"));
        QCOMPARE(prettifyTitle(QStringLiteral("some_movie")), QStringLiteral("Some Movie"));
        QCOMPARE(prettifyTitle(QStringLiteral("[Group] Film Name (2020) [720p]")), QStringLiteral("Film Name"));
        QCOMPARE(prettifyTitle(QStringLiteral("Already Fine")), QStringLiteral("Already Fine"));
    }
};

QTEST_GUILESS_MAIN(TstParser)
#include "tst_parser.moc"
