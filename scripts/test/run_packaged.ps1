# Runs a packaged/installed ELVideo with a PATH that has no Qt or build folders,
# opens every page through UI Automation, records 3 s via the hotkey, and reports
# QML/plugin errors from the log. Usage: run_packaged.ps1 [-Exe path]
param([string]$Exe = "C:\Projects\ELVideo\dist\ELVideo\ELVideo.exe")
$t = $PSScriptRoot
Get-Process ELVideo -ErrorAction SilentlyContinue | Stop-Process -Force
$log = "$env:LOCALAPPDATA\ELVideo\logs\elvideo.log"
$logStart = if (Test-Path $log) { (Get-Content $log).Count } else { 0 }

$psi = New-Object Diagnostics.ProcessStartInfo $Exe
$psi.UseShellExecute = $false
$psi.EnvironmentVariables["PATH"] = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($k in @($psi.EnvironmentVariables.Keys)) { if ($k -like "QT*" -or $k -like "QML*") { $psi.EnvironmentVariables.Remove($k) } }
$p = [Diagnostics.Process]::Start($psi)
Start-Sleep 6
if ($p.HasExited) { "app exited early with 0x{0:X8}" -f $p.ExitCode; exit 1 }

$before = @(Get-ChildItem "$env:USERPROFILE\Videos\ELVideo" -Filter *.mp4 -ErrorAction SilentlyContinue | ForEach-Object Name)
foreach ($name in "창", "디스플레이", "설정", "돌아가기") {
    powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" $name
    Start-Sleep -Milliseconds 700
}
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_key.ps1" -F 9 | Out-Null; Start-Sleep 3
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_key.ps1" -F 9 | Out-Null; Start-Sleep 2
$p.Refresh(); "running: $(-not $p.HasExited), private $([int]($p.PrivateMemorySize64/1MB)) MB"
$p.Kill()

$new = Get-ChildItem "$env:USERPROFILE\Videos\ELVideo" -Filter *.mp4 | Where-Object { $before -notcontains $_.Name }
"recordings: $($new.Count)"
$new | ForEach-Object { $_.FullName }
"log warnings/errors:"
Get-Content $log | Select-Object -Skip $logStart | Select-String "warn|error|fail" | ForEach-Object { "  " + $_.Line }
