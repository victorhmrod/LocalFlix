@echo off
rem Developer helper: incremental build that keeps going after errors.
rem Usage: scripts\dev-build.cmd [Release|Debug] [target]
setlocal
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
call "%~dp0env.cmd" || exit /b 1
if not exist "%~dp0..\build\%CFG%\build.ninja" (
    cmake -S "%~dp0.." -B "%~dp0..\build\%CFG%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% "-DCMAKE_PREFIX_PATH=%LOCALFLIX_QT_DIR%" || exit /b 1
)
if "%~2"=="" (
    cmake --build "%~dp0..\build\%CFG%" -- -k 0
) else (
    cmake --build "%~dp0..\build\%CFG%" --target %~2 -- -k 0
)
exit /b %errorlevel%
