# Manual end-to-end test checklist

The automated suite (`test.cmd`) drives the acceptance scenario with real libmpv and real files, but it cannot
cover everything. Use this list before a release. Use **real, ordinary videos** of different formats
(`scripts\make_test_videos.py` produces five small clips plus a 40-second one if you have none).

Start with a clean slate if you want a first-run experience: `set LOCALFLIX_DATA_DIR=%TEMP%\lf-test` before
launching `localflix.exe` (or delete `%APPDATA%\LocalFlix`).

## A. The acceptance scenario (spec §16)

| # | Do this | Expect |
|---|---------|--------|
| 1 | Launch `localflix.exe` | Dark window, sidebar (Home, Continue Watching, TV Shows, Movies, Collections, Settings), welcome card on Home |
| 2 | **TV Shows → New show**, name it | The new show's page opens; it already has "Season 1" |
| 3 | **Add files**, select **5 or more** videos (also try **Add folder** with sub-folders, and dragging files from Explorer onto the page) | Toast "Added N episodes"; rows appear with titles/episode numbers parsed from the file names; file paths are shown; the files on disk are untouched |
| 4 | Check they are in a season | They are listed under "Season 1" (or under seasons detected from `S02E05` / `Season 2` folders) |
| 5 | Reorder: select an episode → **Move up/Move down**, **and** drag it with the mouse (also between seasons, and onto a season header) | The order changes immediately and survives closing the app |
| 6 | **Start watching** (or double-click an episode) | Player opens, video + audio play, controls visible |
| 7 | Click the seek bar at an arbitrary spot; also drag it | Playback jumps there; time label follows |
| 8 | Close the window (X) | Closes cleanly, no crash |
| 9–10 | Reopen. Home → **Continue Watching** | One card: cover, show title, current episode, percentage, "last watched", **Resume**. Clicking **Resume** continues from ≈ the saved position (±2 s) |
| 11–12 | Seek close to the end, let the episode finish | "Next episode — starting in 3 s" card with **Play now** / **Cancel**; after 3 s the next episode **in your order** starts by itself |
| 13–14 | Close and reopen | Finished episode shows ✓ "Watched"; the in-progress one shows its percentage; **Continue** points at it |
| 15–16 | Reorder again, start the first episode, press **Next** (button and `N`) | Next goes to the second episode in the **new** order |
| 17–19 | Move/rename one video in Explorer, restart, open the show | The row shows ⚠ "missing". Play it → "File not found" card. **Locate file…** → pick the moved file → playback resumes at its old position; title/order/progress unchanged. If other files moved with it (same folder/tree) you are offered to reconnect them too |

## B. Playback edge cases

* **Stop** (button) → "Playback stopped" card, **no** next episode starts. **Resume** continues.
* **Cancel** on the countdown → nothing starts; the finished episode stays watched.
* **Last episode** ends → "That's everything" card, playback stops, no error.
* Seek to the last seconds and quit → on reopening the episode is **not** marked watched.
* Replaying a watched episode starts at 0:00 and does not clear its "watched" state.
* Corrupt/unsupported file → "Cannot play this file" with Skip/Retry; nothing advances by itself.
* Settings: auto-play off → "Finished" card with *Play next*; resume off → asks "Resume from … / Start over"; skip completed on → auto-advance skips watched episodes (the **Next** button does not).
* Audio track menu and subtitle menu list the tracks of **files with several audio tracks / embedded subtitles**;
  switching works; **Load external subtitle…** works; a `.srt` next to the video loads automatically.
* **Audio/subtitle choice follows the show**: in an episode with two audio languages switch to the other one, then
  let the episode end by itself and also try **Next**: the following episodes start in the language you chose, even if
  that file lists its tracks in another order. Turn the subtitles off in one episode: they stay off in the next ones.
  An episode that does not have the chosen language plays in its default one, and goes back to your choice in the next
  episode that has it. Another show is not affected; a movie keeps its own choice.
* **Skip Intro / Skip Ending** (needs files whose chapters are titled `Opening`/`OP`/`Intro` and `Ending`/`ED`/`Credits`,
  e.g. a fansub `.mkv` or a Blu-ray rip; check with *Audio/Video → Chapters* in any other player if unsure):
  the button appears bottom right while the playhead is inside one and disappears when it leaves; clicking it or `Enter`
  jumps to the next chapter. Both automatic options (*Settings → Playback*) are on by default: the opening is skipped as
  soon as it starts (a "Skipped intro" note flashes), the ending is skipped and the next episode starts (after the usual
  countdown); seeking back into a skipped opening plays it; with the options off only the button remains. On the last
  episode, or with auto-play off, skipping the ending ends the episode ("That's everything" / "Finished"). A file with a
  post-credits scene in its own chapter keeps playing the scene. Files with no chapters show no button until marked:
* **Marks for files without chapters** (the flag button in the control bar): play an episode with no chapters, open the
  flag menu near where its ending starts and choose *Ending starts here*: the note says how long the ending is, the Skip
  Ending button appears but the video does **not** jump by itself right then; the menu now lists the mark. Play another
  episode of the same season: the ending is skipped by itself the moment it starts, whatever that episode's length.
  Try *Opening starts here*, later *Opening ends here*. A second season is unaffected. The menu clears each mark. A movie
  says marks are for episodes only. A file that does have chapters keeps using them.
* **Picture in picture** (`Ctrl+P` or the button next to fullscreen): the window becomes a small one in the corner of the
  screen the app is on, above other windows (try a browser maximised behind it), with the video still playing and only
  play/pause, the seek bar and a way back on screen; the Skip button still appears in the credits. Drag and resize it,
  `Esc` or `Ctrl+P` restores the window as it was (also if it was maximized); `F`/double-click also go back instead of
  fullscreen. Closing LocalFlix while in it and reopening gives the normal window. Try it from fullscreen, with two
  monitors, with a 4:3 video (the window should be 4:3), and with the "Next episode" card showing in it.
* Speed menu, `[` `]`, volume (wheel, ↑/↓), mute (`M`), `0`–`9` jump keys.
* Fullscreen (`F`, button, double-click): controls and cursor hide after ~3 s of no movement and return on movement; `Esc` leaves fullscreen first, then the player.

## C. Library management

* Create/rename/delete shows and seasons; cover image via **Edit**; description; **Remove from library** never deletes video files (check in Explorer).
* Rename an episode (F2 / inline), change its number, **Move to season**, mark watched/unwatched, **Reset progress**.
* Movies: add movies/folder, custom title + poster, independent progress, appears in **Movies**, not in TV Shows.
* Collections: create, add shows and movies (menu on any card → *Add to collection*), drag entries to reorder, **Play collection** plays through in that order.
* Importing the same file twice reports "already in the library" and adds nothing.
* Settings page: every control persists after a restart; **Default media import folder** is used by the file dialogs.

## D. What automation could not exercise (please check by hand)

* **OS-level drag and drop in the episode tree** (the drop handler is tested by emitting its signal; the drop-position
  geometry — above/below an episode, onto a season header, empty area, multi-selection drags — is manual only).
* **Dropping files/folders from Explorer** onto a show page, the Movies page and the TV Shows page.
* **Audible audio** (the tests run with a null audio device) and A/V sync on a long video.
* **Files with several audio tracks / embedded subtitle tracks** (the generated clips have one audio track; only the sidecar `.srt` path is tested).
* **Skip Intro / Skip Ending on real releases.** The automated tests give a generated clip its chapters through an mpv EDL
  file (Opening 0–8 s, Ending to the end); how real fansub/Blu-ray chapter titles are named is only checked against the
  title table in `tst_skip`, so try a few of your own files.
* **The file dialogs**: *Add files/folder*, *Locate missing file*, *Load subtitle*, *Choose image*, *Browse…* (the underlying calls are tested, the native dialogs are not).
* **Hardware OpenGL presentation on your GPU.** The automated run renders with Qt's software OpenGL. If the window
  appears blank on your machine, launch with `localflix.exe --software-opengl` and report your GPU/driver.
* Fullscreen on a **second monitor**, with different DPI scaling, and window restore after Alt+Tab.
* A **disconnected drive / network share** during playback and at startup.
* Very large libraries (thousands of files): import responsiveness and Continue Watching speed.
* Windows logoff/shutdown while a video is playing (progress should be saved by the close handler).
