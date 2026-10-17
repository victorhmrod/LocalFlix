# LocalFlix for Android

The Android version of LocalFlix: an offline video player with a Netflix-style library. Same ideas as the desktop
app (your own episode order, resume to the exact second, Continue Watching, automatic next episode, recovery of
moved files), written natively in Kotlin with Jetpack Compose, Room (SQLite) and Media3/ExoPlayer.

* Your videos are never copied, moved or deleted. LocalFlix keeps *access* to the files and folders you pick with the
  system file picker (Storage Access Framework) and plays straight from them. No storage permission is requested.
* Works entirely offline. No account, no tracking, and no internet permission (the app cannot even open a connection).
* If the phone loses access to a file (moved, deleted, app reinstalled), the entry is flagged as missing and keeps
  its place in the order and its progress; **Locate missing file** or **Reconnect folder** (Settings) fix it.

## How the code is organised

| Package | What it holds |
|---|---|
| `core` | Plain Kotlin, no Android classes, unit-tested on the JVM: filename parser, import planner, resume logic, episode queue, progress tracker, playback controller (the "only a natural end of file advances" rule), relink matcher, settings. A direct port of the desktop `src/core`. |
| `data` | Room database (schema exported to `app/schemas`, explicit migrations only, never destructive) and `LibraryRepository`. |
| `files` | Storage Access Framework: scanning a granted folder, reading picked files, existence checks, cover images. |
| `playback` | `Media3Backend`: ExoPlayer behind the same `PlaybackBackend` interface the tests script. |
| `app` | `AppContainer` (object graph), `ImportService`, main-looper scheduler. |
| `ui` | Compose screens: Home, TV shows, show page (reorder, seasons, selection), Movies, Collections, Settings, Player. |

Differences from the desktop app, on purpose: files are `content://` URIs instead of paths, so "Locate missing file"
works on documents and "Reconnect folder" re-matches a whole moved or re-granted tree by file name and path;
playback pauses and saves when the app leaves the screen; the engine is ExoPlayer (hardware decoding, no native
build) instead of libmpv, so the set of playable formats is the phone's, not mpv's.

## Skipping openings and endings

Like the desktop app, LocalFlix skips openings and endings found in the **chapters stored in the video file** (titled
`Opening`, `OP`, `OP1`, `Intro`, `Abertura` / `Ending`, `ED`, `Credits`, `Créditos`, `Encerramento`...). While the
player is inside one, a **Skip Intro** / **Skip Ending** button appears above the seek bar (it also shows with the
controls hidden). Two switches in *Settings → Playback* press it for you; both are off by default.

* Skipping an opening jumps to the next chapter. Skipping an ending that lasts until the end of the file **seeks to the
  end**, so the player reports its natural end and the episode finishes, counts down and moves on exactly as if you had
  watched it; the skip itself never marks anything completed. An ending followed by a chapter of its own (a
  post-credits scene) jumps to that chapter and keeps playing.
* Automatic skipping happens at most once per opening/ending per playback, only while playing (not paused), and never
  for a part you seeked into on purpose. Nothing is stored in the database.
* The rules are the desktop ones, ported as they are (`core/SkipSegments.kt`, `core/PlaybackController.kt`, tests in
  `SkipSegmentsTest`): the title is read word by word, so `OP` is an opening but `Operation` is not, and `Opening Night`
  or `Post-Credits Scene` stay ordinary content.
* **Where the chapters come from.** Media3 1.11 has no chapter API on the player. Its extractors parse them and attach
  them to the `Format.metadata` of each track as `androidx.media3.extractor.metadata.Chapter` entries. `Media3Backend`
  reads them from `player.currentTracks` as soon as playback is ready, merges the copy every track carries, and reports
  them before `onFileLoaded`. Chapters that Media3 does not parse (see *Known limitations*) simply give a file nothing
  to skip.
* The engine decides what is possible: files that carry no chapters in a form Media3 reads (most plain downloads) have
  no button.

## Building

You need a JDK 17+ and the Android SDK (command-line tools are enough; no Android Studio). Versions used:
AGP 9.4, Gradle 9.6 (wrapper), Kotlin 2.4, compileSdk 37, minSdk 26.

```bat
:: point sdk.dir at your SDK once (android\local.properties is git-ignored):
echo sdk.dir=D:/Android/Sdk > android\local.properties

android\gradle.cmd assembleDebug            :: -> app\build\outputs\apk\debug\app-debug.apk
android\gradle.cmd testDebugUnitTest        :: JVM unit tests (the ported core logic)
```

`android\gradle.cmd` is a thin wrapper around `gradlew` that sets `ANDROID_HOME` and tidies `JAVA_HOME`.

## Testing on a phone

Plug in an Android phone, enable *Developer options → USB debugging*, allow the computer, then:

```bat
android\device-test.cmd                     :: installs (never uninstalls) and runs the instrumented tests on the phone
android\screenshot.cmd shot.png             :: what is on the phone's screen right now
```

The instrumented tests cover the SQLite library on a real device, the real ExoPlayer on the synthetic clips of
`scripts\make_test_videos.py` (they ride along in the *test* APK only), the full viewing story (play, seek, close,
reopen, resume, finish, auto-advance) and a smoke test of the real screens. The skip tests (`ChapterSkipDeviceTest`,
two cases in `UiSmokeTest`) need three small chaptered clips that only ffmpeg can write; generate them once with
`python android\scripts\make_chaptered_clips.py` (they go to `android\testdata`, git-ignored, and are skipped
automatically when absent). They check that the real ExoPlayer exposes the chapters when the file is reported as
loaded, that seeking to the end gives the natural end of file (playing and paused), and the whole flow: automatic
skipping of the opening and the ending, the next episode starting through end-of-file, the post-credits scene, the
Skip button in the real player screen. Real releases are another matter: try a few fansub `.mkv` files from your own
library on the phone (the button should appear inside the opening and the ending, nothing should skip by itself with
the switches off), since how real chapters are named is only covered by the title table in `SkipSegmentsTest`.
files, so they never touch the library you built on the phone. `docs/MANUAL_TEST_CHECKLIST.md` lists what automation
cannot cover (the system file picker, audible audio, rotation, second displays...).

## Known limitations

* Skipping needs chapters Media3 1.11 reads. In its extractor module only three classes build chapter entries:
  `MatroskaExtractor` (Matroska and WebM chapters), `Mp4Extractor` (MP4/MOV: Nero `chpl` atoms, and QuickTime chapter
  text tracks) and the ID3 decoder (MP3). Other containers (AVI, MPEG-TS, FLV...) give none, so those files show no
  Skip button even if other players list chapters for them. Checked against real files: Matroska, WebM and MP4/MOV with
  `chpl` chapters. Not tested: QuickTime chapter text tracks (no tool here writes one), and nested, multi-edition or
  ordered Matroska chapters.
* Hidden Matroska chapters are treated like any other (the hidden flag is ignored).
* The player position is polled twice a second, so an automatic skip can start up to half a second after the opening
  begins.
* There is no audio fingerprinting and no manual marking of openings and endings.
