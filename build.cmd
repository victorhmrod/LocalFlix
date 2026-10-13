@echo off
rem Configure and build LocalFlix.
rem Usage:  build.cmd [Release|Debug] [extra cmake configure args...]
setlocal
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
shift

call "%~dp0scripts\env.cmd"
if errorlevel 1 exit /b 1

set "BUILD_DIR=%~dp0build\%CFG%"
cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% "-DCMAKE_PREFIX_PATH=%LOCALFLIX_QT_DIR%" %1 %2 %3 %4 %5 %6 %7 %8 %9
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1
echo.
echo Build OK: %BUILD_DIR%\localflix.exe
exit /b 0
