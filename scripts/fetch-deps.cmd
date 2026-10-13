@echo off
rem Downloads and verifies libmpv (and mpv.exe for test clips). See scripts\fetch-deps.ps1.
rem   scripts\fetch-deps.cmd              pinned release
rem   scripts\fetch-deps.cmd -SkipCli     only libmpv
rem   set LOCALFLIX_MPV_BUILD=<tag>       use another mpv-winbuild release (verified by its published digest)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-deps.ps1" %*
exit /b %errorlevel%
