# Downloads the pinned libmpv build (shinchiro/mpv-winbuild-cmake, x86-64 baseline)
# into third_party\mpv and checks its SHA-256. The player needs it; ELVideo builds
# without it (player disabled) when it is missing.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\get-libmpv.ps1
$ErrorActionPreference = "Stop"
$name = "mpv-dev-x86_64-20261103-git-a687796d9d.7z"
$url = "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20261103/$name"
$sha256 = "41CBFDA596AAF1DC30053679BCD22828AFAEEC411340DE6BF4C0E786FED1480B"

$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "third_party\mpv"
$archive = Join-Path $dir $name
New-Item -ItemType Directory -Force $dir | Out-Null
if (-not (Test-Path $archive)) {
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
}
$actual = (Get-FileHash $archive -Algorithm SHA256).Hash
if ($actual -ne $sha256) {
    Remove-Item $archive
    throw "SHA-256 mismatch for $name (got $actual)"
}
$sdk = Join-Path $dir "sdk"
if (Test-Path $sdk) { Remove-Item -Recurse -Force $sdk }
New-Item -ItemType Directory -Force $sdk | Out-Null
tar -xf $archive -C $sdk  # Windows 10 1803+ tar (libarchive) reads 7z
if (-not (Test-Path (Join-Path $sdk "libmpv-2.dll"))) { throw "libmpv-2.dll missing after extraction" }
Write-Host "libmpv ready in $sdk"
