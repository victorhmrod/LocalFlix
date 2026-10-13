@echo off
rem Builds a Release package (see scripts\package.ps1).
rem   scripts\package.cmd                       -> dist\LocalFlix + dist\LocalFlix-<version>-win64.zip
rem   scripts\package.cmd -OutDir dist\release  -> another output folder
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
exit /b %errorlevel%
