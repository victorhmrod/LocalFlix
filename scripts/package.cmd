@echo off
rem Builds a Release package into dist\ (see scripts\package.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
exit /b %errorlevel%
