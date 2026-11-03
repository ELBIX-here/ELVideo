@echo off
rem Usage: scripts\build.cmd [debug|release|debug-engine-only]
rem Sets up the MSVC environment and builds with CMake presets.
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=debug"

rem vcvars64.bat needs vswhere on PATH to finish setting up CMake/Ninja.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (echo MSVC toolset not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=%VCINSTALLDIR%vcpkg"
if "%QTDIR%"=="" set "QTDIR=C:\Qt\6.8.3\msvc2022_64"

cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
