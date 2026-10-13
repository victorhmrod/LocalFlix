@echo off
rem Runs the Gradle wrapper of this project, e.g.  android\gradle.cmd assembleDebug
rem Needs JDK 17+ (JAVA_HOME) and the Android SDK (ANDROID_HOME, or sdk.dir in android\local.properties).
setlocal
if defined JAVA_HOME if "%JAVA_HOME:~-1%"=="\" set "JAVA_HOME=%JAVA_HOME:~0,-1%"
if not defined ANDROID_HOME if exist "D:\Android\Sdk" set "ANDROID_HOME=D:\Android\Sdk"
call "%~dp0gradlew.bat" --console=plain %*
exit /b %errorlevel%
