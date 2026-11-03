# Builds a release and packages it into dist\:
#   dist\ELVideo\                       self-contained app folder (Qt, QML plugins, MSVC runtime)
#   dist\ELVideo-<ver>-portable.zip     that folder zipped
#   dist\ELVideo-<ver>-symbols.zip      exe + pdb for reading crash dumps
#   dist\ELVideo-<ver>-setup.exe        Inno Setup installer (if Inno Setup 6 is installed)
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-SkipBuild] [-NoInstaller]
param([switch]$SkipBuild, [switch]$NoInstaller)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\release"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "ELVideo"
$qtDir = if ($env:QTDIR) { $env:QTDIR } else { "C:\Qt\6.8.3\msvc2022_64" }

if (-not $SkipBuild) {
    & cmd /c "`"$root\scripts\build.cmd`" release"
    if ($LASTEXITCODE -ne 0) { throw "release build failed" }
}

$versionHeader = Get-Content (Join-Path $build "generated\elv_version.h") -Raw
if ($versionHeader -notmatch 'ELV_VERSION_STRING "([^"]+)"') { throw "version not found" }
$version = $Matches[1]
Write-Host "Packaging ELVideo $version"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $build "app\ELVideo.exe") $stage
Copy-Item (Join-Path $build "app\*.dll") $stage  # vcpkg libraries (spdlog, fmt)

# Qt libraries, plugins and the QML modules the app imports.
& (Join-Path $qtDir "bin\windeployqt.exe") --release --qmldir (Join-Path $root "app\qml") --no-translations `
    --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime --no-ffmpeg --dir $stage (Join-Path $stage "ELVideo.exe") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# The app uses only the Basic Controls style (QQuickStyle::setStyle in main.cpp), so the
# other styles (FluentWinUI3 alone is ~840 files), QML debugging and TLS/network
# plugins are dead weight. scripts\test\check_deps.ps1 verifies nothing needs them.
foreach ($style in "FluentWinUI3", "Fusion", "Imagine", "Material", "Universal", "Windows") {
    Remove-Item -Recurse -Force (Join-Path $stage "qml\QtQuick\Controls\$style") -ErrorAction SilentlyContinue
}
Get-ChildItem $stage -Filter "Qt6QuickControls2*.dll" |
    Where-Object { $_.Name -match "^Qt6QuickControls2(FluentWinUI3|Fusion|Imagine|Material|Universal|Windows)" } |
    Remove-Item -Force
foreach ($dir in "qmltooling", "tls", "networkinformation") {
    Remove-Item -Recurse -Force (Join-Path $stage $dir) -ErrorAction SilentlyContinue
}

# MSVC runtime next to the exe, so no vc_redist install is needed.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "x64\Microsoft.VC143.CRT") } |
    Sort-Object { [version]$_.Name } -ErrorAction SilentlyContinue | Select-Object -Last 1
if (-not $crt) { throw "MSVC runtime (Microsoft.VC143.CRT) not found" }
Copy-Item (Join-Path $crt.FullName "x64\Microsoft.VC143.CRT\*.dll") $stage

$size = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("App folder: {0:N1} MB, {1} files" -f $size, (Get-ChildItem $stage -Recurse -File).Count)

$portable = Join-Path $dist "ELVideo-$version-portable.zip"
Remove-Item $portable -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $portable

$symbols = Join-Path $dist "ELVideo-$version-symbols.zip"
Remove-Item $symbols -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $build "app\ELVideo.exe"), (Join-Path $build "app\ELVideo.pdb") -DestinationPath $symbols

if (-not $NoInstaller) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($iscc) {
        & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutputDir=$dist" (Join-Path $root "installer\ELVideo.iss")
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
    } else {
        Write-Warning "Inno Setup 6 not found; skipped the installer (install it from https://jrsoftware.org/isinfo.php)"
    }
}

Get-ChildItem $dist -File | ForEach-Object { "{0,-36} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }
