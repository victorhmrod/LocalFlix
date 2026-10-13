# Dependencies and toolchain

LocalFlix builds on Windows 10/11 x64 with MSVC. Everything runs offline; the only network access is the
one-time download of libmpv (and, optionally, `mpv.exe`) by `scripts\fetch-deps.cmd`.

| What | Version used | Where it is expected | Why |
|------|--------------|----------------------|-----|
| Visual Studio / Build Tools | VS 2026 (MSVC 14.50). Any VS with the **Desktop development with C++** workload works | found automatically through `vswhere` (`scripts\env.cmd`) | compiler, Windows SDK, Ninja, CMake |
| CMake | ≥ 3.24 (4.2.1 tested) | on `PATH` (VS ships one) | build system |
| Ninja | bundled with VS ("C++ CMake tools for Windows") | on `PATH` after `vcvars64` | generator |
| Qt | 6.7.3, kit `msvc2019_64` (Qt ≥ 6.5 should work) | `D:\Qt\6.7.3\msvc2019_64`, override with `LOCALFLIX_QT_DIR` | UI, SQL (SQLite driver), tests |
| libmpv | mpv v0.41 git `9d144e57b6` (client API 2.5), build tag `20261013` | `third_party\libmpv` | media engine |
| 7-Zip | any | `7z.exe` on `PATH` or `C:\Program Files\7-Zip` | unpack the `.7z` archives in `fetch-deps` |
| Python 3 | optional, 3.8+ | `python` on `PATH` | `make_test_videos.py`, `make_icon.py` (stdlib only) |

Qt modules used: Core, Gui, Widgets, OpenGL, OpenGLWidgets, Sql, Svg (and Test for the tests).

## Qt

Install Qt with the official online installer (Qt 6.7.x → *MSVC 2019 64-bit*; the MSVC 2019/2022/2026 toolsets
share one ABI, so the `msvc2019_64` kit links fine with a newer compiler). If your kit lives elsewhere:

```bat
set LOCALFLIX_QT_DIR=C:\Qt\6.7.3\msvc2019_64
build.cmd
```

## libmpv

```bat
scripts\fetch-deps.cmd            :: libmpv + mpv.exe (for test clips)
scripts\fetch-deps.cmd -SkipCli   :: libmpv only
```

The script downloads the pinned archives from
<https://github.com/shinchiro/mpv-winbuild-cmake/releases> (the Windows builds that mpv.io links to), verifies
their SHA-256 against the values pinned in `scripts\fetch-deps.ps1`, and unpacks them to:

* `third_party\libmpv\` — `libmpv-2.dll` and `include\mpv\*.h`
* `tools\mpv-cli\` — `mpv.exe`, used **only** to encode the synthetic test clips (`scripts\make_test_videos.py`);
  it is not needed to build or run LocalFlix.

Both folders are git-ignored.

### The MSVC import library

The package ships only a MinGW import library (`libmpv.dll.a`), which MSVC cannot link. At CMake configure time
`cmake\LibmpvImportLib.cmake` reads the DLL's export table with `dumpbin /exports`, writes a `.def` file and runs
`lib.exe`; the result is `build\<cfg>\mpv-implib\libmpv-2.lib`. Nothing is checked in and nothing needs to be done by hand.

LocalFlix links the DLL with `/DELAYLOAD:libmpv-2.dll`. `main()` first calls `MpvBackend::libraryAvailable()`, so a
missing DLL produces an explanatory message box instead of a Windows loader error. A post-build step copies
`libmpv-2.dll` next to every executable.

### Updating libmpv

1. Pick a release tag at <https://github.com/shinchiro/mpv-winbuild-cmake/releases>.
2. `set LOCALFLIX_MPV_BUILD=<tag>` and run `scripts\fetch-deps.cmd` (the digest published by the GitHub API is used
   for verification), or edit the pinned table in `scripts\fetch-deps.ps1` if you want the new tag to become the default.
3. Rebuild (`build.cmd`) — the import library is regenerated automatically — and run `test.cmd`. The tests in
   `tst_mpv_backend` exercise the exact libmpv behaviours LocalFlix depends on (end-file reasons, `loadfile` start
   offsets, track lists, errors).

## Licences

See `THIRD_PARTY_NOTICES.md`.
