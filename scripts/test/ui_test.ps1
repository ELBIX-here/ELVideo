# Launches the app (debug) with capturable windows and saves a PNG of its main window.
# Usage: ui_test.ps1 [-Keep] [-Wait seconds]
param([switch]$Keep, [int]$Wait = 5)
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic; using System.Text;
public static class UiWin {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  public struct RECT { public int L, T, R, B; }
  public static List<string> Visible(uint pid) { var res = new List<string>(); EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if (p==pid && IsWindowVisible(h)) { RECT r; GetWindowRect(h, out r); var sb = new StringBuilder(256); GetWindowText(h, sb, 256); res.Add(String.Format("0x{0:X} {1}x{2} {3}", h.ToInt64(), r.R-r.L, r.B-r.T, sb)); } return true; }, IntPtr.Zero); return res; }
}
"@
Get-Process ELVideo -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill(); $_.WaitForExit(3000) | Out-Null }

$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
$env:ELV_TEST_CAPTURABLE = "1"
$build = "C:\Projects\ELVideo\build\debug"
$out = "C:\Projects\ELVideo\build\ui"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }

$app = Start-Process "$build\app\ELVideo.exe" -PassThru
Start-Sleep $Wait
$wins = [UiWin]::Visible($app.Id)
$wins
$main = ($wins | Where-Object { $_ -match " ELVideo$" -and $_ -notmatch " 236x" } | Select-Object -First 1)
if ($main) {
  $h = $main.Split(" ")[0]
  & "$build\tools\cli\elvideo-cli.exe" capture $h 1 $out | Select-String "done"
}
if (-not $Keep) { $app.Kill(); "app killed" } else { "app pid $($app.Id)" }
