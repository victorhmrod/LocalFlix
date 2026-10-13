@echo off
rem Saves what is on the connected phone's screen:  android\screenshot.cmd out.png
setlocal
if not defined ANDROID_HOME if exist "D:\Android\Sdk" set "ANDROID_HOME=D:\Android\Sdk"
if "%~1"=="" (echo usage: screenshot.cmd out.png & exit /b 1)
"%ANDROID_HOME%\platform-tools\adb.exe" exec-out screencap -p > "%~1"
exit /b %errorlevel%
