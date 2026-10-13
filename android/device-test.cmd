@echo off
rem Builds the app and its instrumented tests, installs both on the connected phone WITHOUT uninstalling
rem anything (so the library you built up on the phone survives), and runs the tests there.
rem   android\device-test.cmd                          -> everything
rem   android\device-test.cmd -e class io.github.victorhmrod.localflix.LibraryRepositoryTest
rem The tests only use their own in-memory databases and temporary files; they never touch the app's real data.
setlocal
if not defined ANDROID_HOME if exist "D:\Android\Sdk" set "ANDROID_HOME=D:\Android\Sdk"
set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
call "%~dp0gradle.cmd" assembleDebug assembleDebugAndroidTest || exit /b 1
"%ADB%" get-state >nul 2>&1 || (echo No phone connected: plug it in with USB debugging on, and allow this computer. & exit /b 1)
"%ADB%" install -r "%~dp0app\build\outputs\apk\debug\app-debug.apk" || exit /b 1
"%ADB%" install -r "%~dp0app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk" || exit /b 1
"%ADB%" shell am instrument -w %* io.github.victorhmrod.localflix.test/androidx.test.runner.AndroidJUnitRunner
exit /b %errorlevel%
