<#
.SYNOPSIS
  Builds a Release package: dist\LocalFlix\ (runnable folder) and dist\LocalFlix-<version>-win64.zip.

.DESCRIPTION
  1. Release build (build.cmd)
  2. copy localflix.exe + libmpv-2.dll
  3. windeployqt: Qt DLLs, platform/style/image/SQL plugins, MSVC runtime, software OpenGL
  4. drop SQL drivers other than SQLite
  5. smoke test: start the packaged exe with a PATH that has no Qt in it and render the window
  6. zip
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$qt = if ($env:LOCALFLIX_QT_DIR) { $env:LOCALFLIX_QT_DIR } else { 'D:\Qt\6.7.3\msvc2019_64' }
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern '^\s+VERSION\s+([0-9.]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$dist = Join-Path $root 'dist\LocalFlix'
$build = Join-Path $root 'build\Release'

Write-Host '== building (Release)'
& cmd.exe /c "`"$root\build.cmd`" Release"
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host "== assembling $dist"
if (Test-Path (Join-Path $root 'dist')) { Remove-Item -Recurse -Force (Join-Path $root 'dist') }
New-Item -ItemType Directory -Path $dist | Out-Null
Copy-Item (Join-Path $build 'localflix.exe') $dist
Copy-Item (Join-Path $build 'libmpv-2.dll') $dist
foreach ($f in 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md') { if (Test-Path (Join-Path $root $f)) { Copy-Item (Join-Path $root $f) $dist } }

Write-Host '== windeployqt'
& "$qt\bin\windeployqt.exe" --release --no-translations --compiler-runtime --no-system-d3d-compiler (Join-Path $dist 'localflix.exe') | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# App-local MSVC runtime, so the package starts on a PC without the Visual C++ Redistributable.
# (windeployqt only copies it when run inside a VS developer prompt, so do it explicitly.)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' | Join-Path -ChildPath 'Installer' | Join-Path -ChildPath 'vswhere.exe'
$vsPath = & $vswhere -latest -products * -property installationPath
$redist = Get-ChildItem (Join-Path $vsPath 'VC\Redist\MSVC') -Directory | Where-Object { $_.Name -match '^\d' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$crt = Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'Visual C++ runtime redistributable not found; install the VS C++ workload.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $dist -Force
Write-Host "   MSVC runtime from $($crt.FullName)"

# Only SQLite is used; the other Qt SQL drivers would just add dependencies.
Get-ChildItem (Join-Path $dist 'sqldrivers') -Filter '*.dll' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '^qsqlite' } | Remove-Item -Force
# windeployqt also ships network/TLS/touch/style plugins; LocalFlix is fully offline, forces the Fusion
# style and imports no Qt Network code. (The smoke test below fails if anything needed was trimmed.)
foreach ($d in 'generic', 'tls', 'networkinformation', 'styles') { Remove-Item -Recurse -Force (Join-Path $dist $d) -ErrorAction SilentlyContinue }
Remove-Item (Join-Path $dist 'Qt6Network.dll') -ErrorAction SilentlyContinue

Write-Host '== verifying contents'
$required = 'localflix.exe', 'libmpv-2.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Sql.dll', 'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll',
            'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll', 'opengl32sw.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll'
$missing = $required | Where-Object { -not (Test-Path (Join-Path $dist $_)) }
if ($missing) { throw "package is missing: $($missing -join ', ')" }
Write-Host '   imageformats: ' ((Get-ChildItem (Join-Path $dist 'imageformats') -Filter *.dll | ForEach-Object Name) -join ', ')
Write-Host '   iconengines:  ' ((Get-ChildItem (Join-Path $dist 'iconengines') -Filter *.dll -ErrorAction SilentlyContinue | ForEach-Object Name) -join ', ')

Write-Host '== smoke test (no Qt on PATH, temporary data folder)'
$tmpData = Join-Path ([IO.Path]::GetTempPath()) ("localflix-smoke-" + [guid]::NewGuid().ToString('N'))
$shot = Join-Path $root 'dist\smoke.png'
$env:LOCALFLIX_DATA_DIR = $tmpData
$oldPath = $env:PATH
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch 'Qt') }) -join ';'   # prove the package does not rely on a Qt install
try {
    $proc = Start-Process -FilePath (Join-Path $dist 'localflix.exe') -ArgumentList '--software-opengl', '--screenshot', "`"$shot`"" -PassThru -Wait
} finally {
    $env:PATH = $oldPath
    Remove-Item Env:LOCALFLIX_DATA_DIR -ErrorAction SilentlyContinue
}
if ($proc.ExitCode -ne 0) { throw "packaged app exited with code $($proc.ExitCode)" }
if (-not (Test-Path $shot) -or (Get-Item $shot).Length -lt 5000) { throw 'packaged app produced no usable screenshot' }
if (-not (Test-Path (Join-Path $tmpData 'library.db'))) { throw 'packaged app did not create its database (SQLite driver missing?)' }
Remove-Item -Recurse -Force $tmpData -ErrorAction SilentlyContinue
Write-Host "   OK: exit 0, library.db created, screenshot $((Get-Item $shot).Length) bytes"

Write-Host '== zipping'
$zip = Join-Path $root "dist\LocalFlix-$version-win64.zip"
Compress-Archive -Path $dist -DestinationPath $zip -CompressionLevel Optimal
Write-Host ("Package: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
