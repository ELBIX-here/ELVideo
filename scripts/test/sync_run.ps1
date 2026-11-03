# Records the sync stimulus window with system audio and runs synccheck on it.
param([string]$Build = "debug-engine-only", [int]$Seconds = 12, [string]$Extra = "")
$cli = "C:\Projects\ELVideo\build\$Build\tools\cli\elvideo-cli.exe"
$out = "C:\Projects\ELVideo\build\sync.mp4"
Remove-Item $out -ErrorAction SilentlyContinue
$p = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File C:\Projects\ELVideo\scripts\test\sync_test.ps1 -Seconds $Seconds" -PassThru
# Find the form by title (the process's main window is its console).
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SyncWin { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t); }
"@
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 600 -and $hwnd -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 50; $hwnd = [SyncWin]::FindWindow([NullString]::Value, "ELV sync test") }
$hex = "0x{0:X}" -f $hwnd.ToInt64()
"window $hex"
$cliArgs = @("record", $hex, ($Seconds + 4), $out, "--fps", "60", "--audio", "system") + ($Extra -split " " | Where-Object { $_ })
& $cli @cliArgs | Select-String "started|finished|warning|error|done|t=   [1-3]s"
& $cli probe $out
& $cli synccheck $out
