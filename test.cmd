@echo off
rem Build (if needed) and run the automated tests.
rem   test.cmd                 all tests (the UI end-to-end test opens a window for ~30 s)
rem   test.cmd Release -LE gui headless tests only
rem   test.cmd Release -R tst_library   one suite
setlocal
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
shift

call "%~dp0build.cmd" %CFG%
if errorlevel 1 exit /b 1

call "%~dp0scripts\env.cmd"
if errorlevel 1 exit /b 1
set "PATH=%LOCALFLIX_QT_DIR%\bin;%PATH%"
ctest --test-dir "%~dp0build\%CFG%" --output-on-failure %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
