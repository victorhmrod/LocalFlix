@echo off
rem Sets up the MSVC x64 toolchain, Ninja and Qt for LocalFlix builds.
rem Usage:  call scripts\env.cmd
rem Optional overrides (environment variables):
rem   LOCALFLIX_QT_DIR   Qt kit root, default D:\Qt\6.7.3\msvc2019_64

if not defined LOCALFLIX_QT_DIR set "LOCALFLIX_QT_DIR=D:\Qt\6.7.3\msvc2019_64"
if not exist "%LOCALFLIX_QT_DIR%\bin\Qt6Core.dll" (
    echo [env] Qt not found in "%LOCALFLIX_QT_DIR%". Set LOCALFLIX_QT_DIR to your Qt 6 MSVC kit.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [env] vswhere.exe not found; install Visual Studio with the "Desktop development with C++" workload.
    exit /b 1
)
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo [env] No Visual Studio installation with the MSVC x64 toolset was found.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [env] vcvars64.bat failed.
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo [env] ninja.exe not on PATH. Install the "C++ CMake tools for Windows" component or add Ninja to PATH.
    exit /b 1
)
exit /b 0
