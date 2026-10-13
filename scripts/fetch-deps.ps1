<#
.SYNOPSIS
  Downloads the libmpv development package (and the mpv.exe used to generate test clips).

.DESCRIPTION
  Pinned release of https://github.com/shinchiro/mpv-winbuild-cmake (the Windows builds that
  mpv.io links to). Each archive is verified against its SHA-256 before it is extracted:
  for the pinned tag against the hashes below, for other tags against the digest that the
  GitHub release API publishes.

    third_party\libmpv   libmpv-2.dll + include\mpv\*.h        (required to build)
    tools\mpv-cli        mpv.exe                                (only to generate test videos)

  Requires 7-Zip (7z.exe on PATH or in "C:\Program Files\7-Zip"): the archives are .7z.

.PARAMETER Tag
  Release tag. Default: the pinned one. Override with -Tag or $env:LOCALFLIX_MPV_BUILD.
.PARAMETER SkipCli
  Do not download mpv.exe (you then cannot run scripts\make_test_videos.py).
#>
param(
    [string]$Tag = $(if ($env:LOCALFLIX_MPV_BUILD) { $env:LOCALFLIX_MPV_BUILD } else { '20261013' }),
    [switch]$SkipCli
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$pinned = @{
    '20261013' = @{
        dev = @{ name = 'mpv-dev-x86_64-20261013-git-9d144e57b6.7z'; sha256 = '10860837bc04491862c1e66a95dc1af1d7886d69757ce447a09e1e49ae993b52' }
        cli = @{ name = 'mpv-x86_64-20261013-git-9d144e57b6.7z';     sha256 = '551c3ec2e18db657d4f13775902145a72fb1900478719b9004ec446465fd93b9' }
    }
}

function Find-7Zip {
    $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in "$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") { if (Test-Path $p) { return $p } }
    throw "7-Zip is required to unpack the mpv archives (.7z). Install it from https://www.7-zip.org/ and re-run."
}

function Get-Asset([string]$kind) {
    if ($pinned.ContainsKey($Tag)) { return $pinned[$Tag][$kind] }
    # Unpinned tag: find the asset through the API and trust its published digest.
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/tags/$Tag" -Headers @{ 'User-Agent' = 'localflix-setup' }
    $pattern = if ($kind -eq 'dev') { '^mpv-dev-x86_64-\d{8}-git-[0-9a-f]+\.7z$' } else { '^mpv-x86_64-\d{8}-git-[0-9a-f]+\.7z$' }
    $asset = $rel.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $asset) { throw "Release $Tag has no '$kind' x86_64 archive." }
    if (-not $asset.digest -or $asset.digest -notmatch '^sha256:') { throw "The release API did not publish a SHA-256 digest for $($asset.name); refusing to use an unverified download." }
    return @{ name = $asset.name; sha256 = $asset.digest.Substring(7) }
}

function Install-Archive($asset, [string]$dest, [string]$sevenZip) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("localflix-fetch-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        $archive = Join-Path $tmp $asset.name
        $url = "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$Tag/$($asset.name)"
        Write-Host "Downloading $($asset.name) ..."
        Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
        $hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $asset.sha256.ToLower()) { throw "SHA-256 mismatch for $($asset.name): expected $($asset.sha256), got $hash. Nothing was extracted." }
        Write-Host "  sha256 OK"
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        New-Item -ItemType Directory -Path $dest | Out-Null
        & $sevenZip x $archive "-o$dest" -y | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7-Zip failed to extract $($asset.name)" }
        Set-Content -Path (Join-Path $dest 'LOCALFLIX-SOURCE.txt') -Value "$($asset.name)`nrelease tag: $Tag`nsha256: $($asset.sha256)"
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

$sevenZip = Find-7Zip
Install-Archive (Get-Asset 'dev') (Join-Path $root 'third_party\libmpv') $sevenZip
if (-not (Test-Path (Join-Path $root 'third_party\libmpv\libmpv-2.dll'))) { throw 'libmpv-2.dll is missing from the extracted package (unexpected archive layout).' }
if (-not (Test-Path (Join-Path $root 'third_party\libmpv\include\mpv\client.h'))) { throw 'include\mpv\client.h is missing from the extracted package.' }
Write-Host 'libmpv installed in third_party\libmpv'
if (-not $SkipCli) {
    Install-Archive (Get-Asset 'cli') (Join-Path $root 'tools\mpv-cli') $sevenZip
    Write-Host 'mpv.exe installed in tools\mpv-cli (used only to generate test videos)'
}
Write-Host 'Done. Next: build.cmd'
