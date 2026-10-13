@echo off
rem Builds the Windows installer: dist\installer\LocalFlix-<version>-setup.exe (see scripts\make-installer.ps1).
rem Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-installer.ps1" %*
exit /b %errorlevel%
