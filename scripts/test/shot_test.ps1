# Exercises every screenshot mode in the running debug app and prints image sizes.
# No mouse input: region/window use debug-build keyboard hooks, the rest UI Automation.
# Screenshots go to build\shots (settings are backed up and restored by the caller).
$t = $PSScriptRoot
$shots = "C:\Projects\ELVideo\build\shots"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class ShotWin { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public struct RECT { public int L, T, R, B; } }
"@
[ShotWin]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

function Last-Shot { Get-ChildItem $shots -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
function Size($f) { if (-not $f) { return "none" }; $i = [Drawing.Image]::FromFile($f.FullName); $s = "$($i.Width)x$($i.Height)"; $i.Dispose(); $s }
function Open-Overlay { powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_key.ps1" -F 12 | Out-Null; Start-Sleep -Milliseconds 1800 }

# Region and window use debug-only keyboard hooks: injected mouse input mixes
# with the real mouse when someone is using the PC.
Open-Overlay
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_key.ps1" -F 1 | Out-Null; Start-Sleep 1
"region   (expect 801x451): " + (Size (Last-Shot))

$h = [ShotWin]::FindWindow([NullString]::Value, "ELV test window"); $r = New-Object ShotWin+RECT
[ShotWin]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null
Open-Overlay
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_key.ps1" -F 2 | Out-Null; Start-Sleep 2
"window   (expect about $($r.R - $r.L)x$($r.B - $r.T)): " + (Size (Last-Shot))

Open-Overlay
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" "이 모니터" | Out-Null; Start-Sleep 1
"monitor  (expect 1920x1080): " + (Size (Last-Shot))

Open-Overlay
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" "전체 화면" | Out-Null; Start-Sleep 1
"all      (expect 3840x1106): " + (Size (Last-Shot))

$count = (Get-ChildItem $shots -File).Count
Open-Overlay
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" "취소 (Esc)" | Out-Null; Start-Sleep 1
"cancel   (expect no new file): " + ((Get-ChildItem $shots -File).Count -eq $count)

$clip = Get-Clipboard -Format Image
"clipboard: " + $(if ($clip) { "$($clip.Width)x$($clip.Height)" } else { "empty" })
