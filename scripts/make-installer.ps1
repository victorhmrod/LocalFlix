<#
.SYNOPSIS
  Builds the Windows installer: dist\installer\LocalFlix-<version>-setup.exe (+ .sha256).

.DESCRIPTION
  1. package the application into dist\installer-staging\LocalFlix (scripts\package.ps1: Release build,
     windeployqt, MSVC runtime, smoke test)
  2. compile installer\LocalFlix.iss with Inno Setup 6 (https://jrsoftware.org/isinfo.php;
     `winget install JRSoftware.InnoSetup`). Override the compiler with $env:LOCALFLIX_ISCC.
  3. test the installer for real: silent per-user install into a temporary folder, start the installed
     exe, silent uninstall, and check that everything was removed.

.PARAMETER SkipPackage
  Reuse an existing dist\installer-staging.
.PARAMETER SkipTest
  Do not run the install/uninstall test.
#>
param([switch]$SkipPackage, [switch]$SkipTest)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern '^\s+VERSION\s+([0-9.]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$staging = Join-Path $root 'dist\installer-staging'
$outDir = Join-Path $root 'dist\installer'
$setup = Join-Path $outDir "LocalFlix-$version-setup.exe"

function Find-Iscc {
    if ($env:LOCALFLIX_ISCC -and (Test-Path $env:LOCALFLIX_ISCC)) { return $env:LOCALFLIX_ISCC }
    foreach ($p in "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") {
        if (Test-Path $p) { return $p }
    }
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'Inno Setup 6 (ISCC.exe) not found. Install it (winget install JRSoftware.InnoSetup) or set LOCALFLIX_ISCC.'
}
$iscc = Find-Iscc

if (-not $SkipPackage) {
    Write-Host '== packaging the application'
    & (Join-Path $PSScriptRoot 'package.ps1') -OutDir 'dist\installer-staging'
}
$appDir = Join-Path $staging 'LocalFlix'
if (-not (Test-Path (Join-Path $appDir 'localflix.exe'))) { throw "No packaged application in $appDir (run without -SkipPackage)." }

Write-Host "== compiling the installer ($iscc)"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (Test-Path $setup) { Remove-Item -Force $setup }
& $iscc /Qp "/DAppVersion=$version" "/DSourceDir=$appDir" (Join-Path $root 'installer\LocalFlix.iss') | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $setup)) { throw 'Inno Setup failed' }
$sha = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
Set-Content -Path "$setup.sha256" -Value "$sha  $(Split-Path -Leaf $setup)" -Encoding ascii
Write-Host ("Installer: {0} ({1:N1} MB)" -f $setup, ((Get-Item $setup).Length / 1MB))
Write-Host "SHA-256: $sha"

if ($SkipTest) { return }

Write-Host '== testing the installer (silent, per user, temporary folder)'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("localflix-install-test-" + [guid]::NewGuid().ToString('N'))
$installDir = Join-Path $tmp 'app'
$dataDir = Join-Path $tmp 'data'
$log = Join-Path $tmp 'install.log'
# The test installs under the real AppId, so it must not run over a genuine installation of LocalFlix.
$appId = [regex]::Match((Get-Content (Join-Path $root 'installer\LocalFlix.iss') -Raw), '(?m)^AppId=\{(\{[0-9A-Fa-f-]+\})').Groups[1].Value
if (-not $appId) { throw 'could not read AppId from installer\LocalFlix.iss' }
$keyPath = "Software\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1"
foreach ($hive in 'HKCU', 'HKLM') {
    if (Test-Path "${hive}:\$keyPath") { throw "LocalFlix is already installed on this PC ($hive uninstall entry exists). Uninstall it first, or run with -SkipTest." }
}

New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$installed = $false
try {
    $p = Start-Process -FilePath $setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/DIR=`"$installDir`"", "/LOG=`"$log`"" -PassThru -Wait
    $installed = $true
    if ($p.ExitCode -ne 0) { throw "installer exited with $($p.ExitCode); see $log" }
    foreach ($f in 'localflix.exe', 'libmpv-2.dll', 'Qt6Core.dll', 'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll', 'unins000.exe', 'LICENSE') {
        if (-not (Test-Path (Join-Path $installDir $f))) { throw "installed folder is missing $f" }
    }
    $uninstallKey = Get-Item "HKCU:\$keyPath" -ErrorAction SilentlyContinue
    if (-not $uninstallKey) { throw 'no LocalFlix entry in Apps & features (HKCU uninstall key)' }
    if ($uninstallKey.GetValue('DisplayName') -notlike 'LocalFlix*') { throw 'uninstall entry has an unexpected DisplayName' }
    Write-Host "   installed OK: $($uninstallKey.GetValue('DisplayName')), uninstall entry registered"

    # start the INSTALLED copy with no Qt on PATH and a throwaway data folder
    $shot = Join-Path $tmp 'installed.png'
    $env:LOCALFLIX_DATA_DIR = $dataDir
    $oldPath = $env:PATH
    $env:PATH = ($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch 'Qt') }) -join ';'
    try { $run = Start-Process -FilePath (Join-Path $installDir 'localflix.exe') -ArgumentList '--software-opengl', '--screenshot', "`"$shot`"" -PassThru -Wait }
    finally { $env:PATH = $oldPath; Remove-Item Env:LOCALFLIX_DATA_DIR -ErrorAction SilentlyContinue }
    if ($run.ExitCode -ne 0 -or -not (Test-Path $shot) -or (Get-Item $shot).Length -lt 5000) { throw 'the installed application did not start correctly' }
    if (-not (Test-Path (Join-Path $dataDir 'library.db'))) { throw 'installed application did not create its database' }
    Write-Host '   installed application starts and creates its library'

    # uninstall silently: program removed, the user's library (data folder) is never deleted silently
    $u = Start-Process -FilePath (Join-Path $installDir 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru -Wait
    if ($u.ExitCode -ne 0) { throw "uninstaller exited with $($u.ExitCode)" }
    for ($i = 0; $i -lt 40 -and (Test-Path (Join-Path $installDir 'localflix.exe')); $i++) { Start-Sleep -Milliseconds 250 }
    if (Test-Path (Join-Path $installDir 'localflix.exe')) { throw 'uninstall left localflix.exe behind' }
    if (Test-Path "HKCU:\$keyPath") { throw 'uninstall left its registry entry behind' }
    if (-not (Test-Path (Join-Path $dataDir 'library.db'))) { throw 'silent uninstall must not delete the library' }
    Write-Host '   uninstall OK: program and registry entry removed, library kept'
} finally {
    # a failed test must not leave a half-installed copy (and its Apps & features entry) behind
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if ($installed -and (Test-Path $uninstaller)) {
        Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait | Out-Null
    }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
Write-Host 'Installer test passed.'
