#include "Migrations.h"

namespace lf {

namespace {

Migration v1()
{
    Migration m;
    m.version = 1;
    m.description = QStringLiteral("Initial schema: media files, shows, seasons, episodes, movies, collections, progress, settings");
    m.statements = {
        // Every file the library knows about, regardless of what references it.
        // Paths are never changed by display-name or ordering edits; only
        // "Locate Missing File" rewrites a path, so progress (keyed by id) survives.
        QStringLiteral(R"sql(
CREATE TABLE media_files (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    size_bytes  INTEGER NOT NULL DEFAULT 0,
    mtime_ms    INTEGER NOT NULL DEFAULT 0,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    is_missing  INTEGER NOT NULL DEFAULT 0,
    added_at    INTEGER NOT NULL
))sql"),
        QStringLiteral(R"sql(
CREATE TABLE shows (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    title       TEXT    NOT NULL,
    description TEXT    NOT NULL DEFAULT '',
    cover_file  TEXT    NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL
))sql"),
        QStringLiteral(R"sql(
CREATE TABLE seasons (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    show_id       INTEGER NOT NULL REFERENCES shows(id) ON DELETE CASCADE,
    season_number INTEGER NOT NULL DEFAULT 0,
    title         TEXT    NOT NULL,
    sort_order    INTEGER NOT NULL
))sql"),
        QStringLiteral("CREATE INDEX idx_seasons_show ON seasons(show_id, sort_order)"),
        // sort_order is the user's explicit playback order within a season.
        QStringLiteral(R"sql(
CREATE TABLE episodes (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    season_id      INTEGER NOT NULL REFERENCES seasons(id) ON DELETE CASCADE,
    media_file_id  INTEGER NOT NULL UNIQUE REFERENCES media_files(id) ON DELETE RESTRICT,
    title          TEXT    NOT NULL,
    episode_number INTEGER NOT NULL DEFAULT 0,
    sort_order     INTEGER NOT NULL,
    created_at     INTEGER NOT NULL
))sql"),
        QStringLiteral("CREATE INDEX idx_episodes_season ON episodes(season_id, sort_order)"),
        QStringLiteral(R"sql(
CREATE TABLE movies (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    media_file_id INTEGER NOT NULL UNIQUE REFERENCES media_files(id) ON DELETE RESTRICT,
    title         TEXT    NOT NULL,
    description   TEXT    NOT NULL DEFAULT '',
    poster_file   TEXT    NOT NULL DEFAULT '',
    created_at    INTEGER NOT NULL
))sql"),
        QStringLiteral(R"sql(
CREATE TABLE collections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,
    description TEXT    NOT NULL DEFAULT '',
    created_at  INTEGER NOT NULL
))sql"),
        QStringLiteral(R"sql(
CREATE TABLE collection_entries (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    collection_id INTEGER NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
    item_type     TEXT    NOT NULL CHECK (item_type IN ('show', 'movie')),
    item_id       INTEGER NOT NULL,
    sort_order    INTEGER NOT NULL,
    UNIQUE (collection_id, item_type, item_id)
))sql"),
        QStringLiteral("CREATE INDEX idx_collection_entries ON collection_entries(collection_id, sort_order)"),
        // item_id is polymorphic (show or movie), so referential cleanup is a trigger.
        QStringLiteral(R"sql(
CREATE TRIGGER trg_show_deleted AFTER DELETE ON shows
BEGIN
    DELETE FROM collection_entries WHERE item_type = 'show' AND item_id = OLD.id;
END)sql"),
        QStringLiteral(R"sql(
CREATE TRIGGER trg_movie_deleted AFTER DELETE ON movies
BEGIN
    DELETE FROM collection_entries WHERE item_type = 'movie' AND item_id = OLD.id;
END)sql"),
        // One row per media file; keyed on the file so reordering or moving an
        // episode between seasons can never disturb it.
        QStringLiteral(R"sql(
CREATE TABLE playback_progress (
    media_file_id   INTEGER PRIMARY KEY REFERENCES media_files(id) ON DELETE CASCADE,
    position_ms     INTEGER NOT NULL DEFAULT 0,
    duration_ms     INTEGER NOT NULL DEFAULT 0,
    completed       INTEGER NOT NULL DEFAULT 0,
    play_count      INTEGER NOT NULL DEFAULT 0,
    last_watched_at INTEGER NOT NULL DEFAULT 0,
    audio_track     INTEGER NOT NULL DEFAULT -1,
    subtitle_track  INTEGER NOT NULL DEFAULT -1,
    updated_at      INTEGER NOT NULL DEFAULT 0
))sql"),
        QStringLiteral("CREATE INDEX idx_progress_last_watched ON playback_progress(last_watched_at)"),
        QStringLiteral(R"sql(
CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
))sql"),
    };
    return m;
}

Migration v2()
{
    Migration m;
    m.version = 2;
    m.description = QStringLiteral("Opening/ending marks per season (for files without chapters) and the audio/subtitle choice per show");
    m.statements = {
        // -1 = not set. The ending is stored as an offset from the end of the file so one mark fits every
        // episode of the season, whatever its length.
        QStringLiteral(R"sql(
CREATE TABLE season_skip_marks (
    season_id          INTEGER PRIMARY KEY REFERENCES seasons(id) ON DELETE CASCADE,
    intro_start_ms     INTEGER NOT NULL DEFAULT -1,
    intro_end_ms       INTEGER NOT NULL DEFAULT -1,
    ending_from_end_ms INTEGER NOT NULL DEFAULT -1
))sql"),
        // The audio/subtitle choice of a show, by what the track is (language, title) and not by its number:
        // mode -1 = nothing chosen (the engine's pick stands), 0 = off, 1 = a track.
        QStringLiteral(R"sql(
CREATE TABLE show_track_prefs (
    show_id     INTEGER PRIMARY KEY REFERENCES shows(id) ON DELETE CASCADE,
    audio_mode  INTEGER NOT NULL DEFAULT -1,
    audio_lang  TEXT    NOT NULL DEFAULT '',
    audio_title TEXT    NOT NULL DEFAULT '',
    sub_mode    INTEGER NOT NULL DEFAULT -1,
    sub_lang    TEXT    NOT NULL DEFAULT '',
    sub_title   TEXT    NOT NULL DEFAULT ''
))sql"),
    };
    return m;
}

}  // namespace

const std::vector<Migration>& defaultMigrations()
{
    static const std::vector<Migration> all = {v1(), v2()};
    return all;
}

}  // namespace lf
