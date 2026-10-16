# Architecture

## Layers and targets

```
 localflix (exe)         src/app        main.cpp: GL format, logging, startup/shutdown order
   └ localflix_ui        src/ui         Qt Widgets: MainWindow, views, player overlay, dialogs, theme
       └ localflix_mpv   src/mpv        MpvBackend (libmpv, events, tracks), MpvWidget (render context)
           └ localflix_core  src/core   everything that has no GUI and no libmpv: database, library,
                                        queue, progress, playback state machine, settings, import
```

`localflix_core` links only Qt Core + Qt Sql, so all of its logic is unit-testable headless; the playback state
machine is tested against a scripted `FakeBackend`, and the real engine is tested through `MpvBackend` in
headless mode (`vo=null`, `ao=null`).

### Spec component → source

| Component named in the spec | Implementation |
|---|---|
| ApplicationController | `core/ApplicationController` — owns the whole service graph and its lifecycle |
| DatabaseManager | `core/DatabaseManager`, `core/Migrations` — file, WAL, migrations, backup, per-thread sessions |
| MediaRepository | `core/MediaRepository` — all SQL; throws `DbError` |
| LibraryManager | `core/LibraryManager` — validation, signals for the UI, cover files, relink, queue building; never throws |
| PlaybackController | `core/PlaybackController` over `core/IPlaybackBackend` (`mpv/MpvBackend` is the real one) |
| PlaybackProgressManager | `core/PlaybackProgressManager` |
| EpisodeQueueManager | `core/EpisodeQueueManager` |
| MediaImportService | `core/MediaImportService` (+ `FilenameParser`) |
| SettingsManager | `core/SettingsManager` |
| MainWindow / LibraryView / ShowDetailsView / EpisodeManagerView / PlayerView | `ui/…` same names |
| (extra) | `ResumeLogic` (series-level resume rule), `SkipSegments` (chapter title → opening/ending), `LibraryActions` (all interactive library operations), `CardFactory`, `Theme` |

### Ownership and lifetime

`main()` creates `MpvBackend` → hands it to `ApplicationController` (declared first, destroyed last) → creates the
`MainWindow` in an inner scope. The window, and with it the `MpvWidget` that owns the libmpv *render context*, is
destroyed **before** the controller tears down the engine; `MpvBackend` also emits `aboutToDestroy` so a render
context is always freed first. Closing the window calls `PlaybackController::shutdown()` (flushes progress, stops
playback); `ApplicationController::shutdown()` then stops the workers and closes the database. All of it is idempotent.

## Playback correctness

* **Natural end vs. stop vs. error.** libmpv reports `END_FILE` with a reason. `EndReason::Eof` is the only reason that
  completes an item and may start the next one. Stop, Quit, Error and Redirect never advance.
* **Tokens.** `IPlaybackBackend::loadFile()` returns a token; `fileLoaded` and `endFile` carry it. libmpv maps tokens
  through `playlist_entry_id`. When the user presses Next, the old file reports `END_FILE(STOP)` with the *old* token and
  the controller ignores it, as it does any other event whose token is not the current one (a late EOF, a duplicate
  delivery). This is what prevents duplicated transitions and race conditions.
* **Completion is event-based.** Seeking to 99 % never completes an episode; reaching the end does.
* **Countdown.** One single-shot timer. When it fires the next item is recomputed from the *current* queue, so a
  reorder during the countdown changes what plays; `autoAdvancePending` is re-emitted with the remaining time.
* **Resume rules** (`Progress`, `ResumeLogic`): positions under 5 s are not resumable; completed items replay from 0 and
  stay completed; "Continue" for a show = the most recently watched episode if unfinished, else the next unfinished
  one in the configured order (wrapping), else "Watch again" from the top.
* **Skipping openings and endings** (`SkipSegments`, `PlaybackController`). Segments come from the chapters in the
  file (`IPlaybackBackend::chapters()`, libmpv's `chapter-list`) and, for a kind the chapters say nothing about, from
  the marks of the episode's season (below). `classifyChapterTitle` decides from whole words in the title (`OP` is an
  opening, `Operation` is not; `Opening Night` and `Post-Credits Scene` are content). A segment lasts until the next
  chapter that is not part of it; a `Preview` chapter right after an ending belongs to the ending. The controller offers
  the segment the playhead is in (`skipSegmentChanged`) and skips it when the user asks or the option is on (both options
  are on by default; a user who turned one off keeps it off), with these rules:
  * **Season marks** (`season_skip_marks`, schema v2) are the fallback for files without chapters: the user marks, in
    the player, where an opening starts and ends (absolute times) and where the ending starts, stored as "the last N ms"
    so one mark fits episodes of different lengths. Per kind, a chapter-derived segment wins and the mark only fills in
    when the chapters give none of that kind (`skipSegmentsFor`); a mark that does not fit a file (an opening past its
    end, an ending as long as the whole file) is ignored. Movies have no season, so they are chapters-only.
    Marking applies live (`LibraryManager::skipMarksChanged`), and the segment the playhead is in at that moment is
    treated as handled, so marking "the ending starts here" never makes the app jump out of it on the spot. The marks menu
    marks the position it was opened at, since the video keeps playing under an open menu.
  * **Completion stays event-based.** An ending that runs to the end of the file is skipped by *seeking to the end*;
    the engine then reports `END_FILE(Eof)` (`keep-open=no`, proven by `tst_mpv_backend`) and the normal path completes
    the item, runs the countdown and advances, or finishes the queue / shows "Finished" when auto-play is off. The skip
    itself never completes anything, so stopping right after it leaves the episode unfinished. An ending followed by
    its own chapter (post-credits scene) is skipped to the start of that chapter and playback continues.
  * **At most once per segment per load.** A skipped segment, and any segment the user seeks into, is remembered in
    `m_handled` until the next load, so stale position events after the seek cannot loop and seeking back into a skipped
    opening is left alone. The key (`SkipSegment::key()`) of an ending that runs to the end of the file does not depend
    on its start, which moves when the engine refines the duration.
  * **Only while `Playing`.** Not paused, loading or counting down; the button itself also works while paused. Skipping
    an ending that runs to the end of the file while paused resumes first: libmpv seeked to the end *while paused*
    parks on the last frame and never reports the end of the file (also covered by `tst_mpv_backend`).
  * Chapters are read from the backend when `fileLoaded` arrives (the backend refreshes them first), and again when
    they or the duration change; segments are cleared when a file ends or is replaced.
* **Audio and subtitle choice** (`TrackChoice`, `PlaybackController::restoreTrackChoices`). A choice made while
  watching an episode belongs to its **show** and is stored by what the track is (language, title: `show_track_prefs`),
  because a track number means nothing in the next file and, left to libmpv alone, the choice was lost when an episode
  ended by itself and the next one started. When a file loads (the backend refreshes its track list before
  `fileLoaded`, so the controller sees it) the show's preference is found again with `findTrackForChoice`: the language
  decides (`pt`, `por` and `pt-BR` are the same; `und` is none), the title only breaks ties, a choice without a language
  matches only an exact title, and when nothing matches the engine's own pick stands. It is applied straight to the
  backend, so loading never writes a preference; only the kind the user changed (audio or subtitle) is recorded, and an
  external subtitle (specific to one file) is not. A show preference beats the per-file choice and the global language
  settings; movies have no show and keep the per-file, by-number choice. A per-file choice written by earlier versions is
  still restored when the show has no preference, and becomes the show's when it differs from what the engine had
  picked itself (so the implicit values older versions stored for both kinds are not mistaken for choices).
* **Saving** (`PlaybackProgressManager`): in-memory position, written at most every 5 s and only if it moved ≥ 1 s;
  immediately on pause, switching, stopping and shutdown. Opening something and leaving within 2 s creates no history.
  The file's duration is stored once on `media_files`, not with every save.

## Threading

* The GUI thread owns the main SQLite connection (WAL, `synchronous=NORMAL`, 5 s busy timeout). A progress save is one
  small UPSERT, far below a frame. Reads for the UI are also on the GUI thread.
* Worker threads exist for exactly two jobs, each on its own single-thread `QThreadPool` that is cancelled and joined at
  shutdown: **directory scanning for imports** (`MediaImportService`) and **file-existence checks**
  (`LibraryManager::verifyFilesAsync`). Their results return through queued calls and are applied on the GUI thread.
  A worker that needs the database takes a `DbSession` (its own connection, removed when the session ends).
* libmpv's wakeup callback and the render update callback run on libmpv threads. Both only schedule a *coalesced*
  queued call (atomic flag) onto the GUI thread; no Qt or libmpv state is touched from them. Events are drained on the GUI
  thread with `mpv_wait_event(handle, 0)`.

## Video rendering

`MpvWidget` is a `QOpenGLWidget`; libmpv renders into its FBO through the **render API** (`vo=libmpv`).
The player's controls, title bar and state cards are ordinary widgets stacked above it, which is possible because Qt
composites the pixels. Rules that follow from that:

* the `MpvWidget` is never reparented (that would destroy its GL context); fullscreen only hides the sidebar and calls
  `showFullScreen()` on the main window;
* **picture in picture** is the same window made small, not a second window: `MainWindow::setPictureInPicture` leaves
  fullscreen (without touching the remembered fullscreen state), lowers the minimum size, resizes to ~480 px wide in the
  corner of the screen it is on (keeping the video's aspect ratio) and puts it on the always-on-top layer with
  `QWindow::setFlag(Qt::WindowStaysOnTopHint)`, which changes the layer in place; `QWidget::setWindowFlag` may re-create
  the native window and a bare `SetWindowPos` is undone by Qt's own bookkeeping. `tst_ui_e2e` checks that the window
  handle stays the same and the video keeps rendering. `PlayerView::setPictureInPicture` shrinks the overlays to
  play/pause, the seek bar and the way back; leaving restores size, position, minimum size and the maximized state, and
  closing the application while in it saves the normal geometry, never the small one;
* render calls pass `BLOCK_FOR_TARGET_TIME=0` and the engine runs with `video-timing-offset=0` so the GUI thread is never
  blocked waiting for vsync;
* video loading waits for the render context (`PlayerView::whenVideoReady`), with a visible error after 6 s.

Alternative considered and rejected: `--wid` embedding of mpv's own window. It would not need a GL context in Qt, but
Qt widgets cannot be drawn on top of a foreign native window (the "airspace" problem), so the overlay, countdown card and
auto-hiding controls would need separate translucent top-level windows, and mouse/keyboard input would have to be
bridged back from mpv. `--software-opengl` (Qt's bundled software GL) exists as a troubleshooting switch for machines
whose GPU driver cannot present OpenGL windows.

## Database

SQLite through Qt's `QSQLITE` driver. Location: `%APPDATA%\LocalFlix\library.db` (override: `LOCALFLIX_DATA_DIR`).
Migrations are forward-only steps in `core/Migrations.cpp`; each step and the `PRAGMA user_version` bump run in one
transaction; an existing library is copied to `library.db.bak-v<old>` before it is migrated; a database from a newer
release is refused rather than touched.

```
media_files(id, path UNIQUE NOCASE, size_bytes, mtime_ms, duration_ms, is_missing, added_at)
shows(id, title, description, cover_file, created_at, updated_at)
seasons(id, show_id → shows, season_number, title, sort_order)
episodes(id, season_id → seasons, media_file_id UNIQUE → media_files, title, episode_number, sort_order, created_at)
movies(id, media_file_id UNIQUE → media_files, title, description, poster_file, created_at)
collections(id, name, description, created_at)
collection_entries(id, collection_id → collections, item_type 'show'|'movie', item_id, sort_order, UNIQUE(collection, type, item))
playback_progress(media_file_id PK → media_files, position_ms, duration_ms, completed, play_count,
                  last_watched_at, audio_track, subtitle_track, updated_at)
season_skip_marks(season_id PK → seasons ON DELETE CASCADE, intro_start_ms, intro_end_ms, ending_from_end_ms)   -- v2, -1 = not set
show_track_prefs(show_id PK → shows ON DELETE CASCADE, audio_mode, audio_lang, audio_title,
                 sub_mode, sub_lang, sub_title)                                                  -- v2, mode -1 none / 0 off / 1 a track
settings(key PK, value)
```

Design points:

* **`media_files` is separate from episodes and movies.** Paths never mix with names or order; "Locate Missing File"
  rewrites one row, so title, position in the sequence and progress are untouched. `UNIQUE(path)` is the duplicate-import
  guard (one file = one library entry).
* **Playback progress is keyed by the media file**, so reordering, renaming or moving an episode between seasons can
  never reset or misattribute it.
* **Order is explicit**: `seasons.sort_order` and `episodes.sort_order`. A drag-and-drop becomes a complete layout that
  `MediaRepository::applyLayout` validates (every season and episode exactly once) and applies in one transaction. The
  playback queue is the flattened (season order, episode order) list, rebuilt on every library change.
* Removing something from the library deletes rows (and its covers/posters, which are LocalFlix's own copies in
  `%APPDATA%\LocalFlix\covers`) — never the video file.

## Other decisions

* **Qt Widgets, not QML:** native dialogs, drag-and-drop item views and a `QOpenGLWidget` video surface with real widget
  overlays are simplest and most reliable here; the UI is a few dozen classes with no custom rendering pipeline.
* **libmpv is delay-loaded** with an import library generated from the DLL (see `docs/DEPENDENCIES.md`): a missing DLL
  is reported with a clear message.
* **Qt resources live in the executable target**, because a static library's resources are dropped by the linker.
* **Warnings are errors** (`/W4 /WX`) in all LocalFlix targets.
