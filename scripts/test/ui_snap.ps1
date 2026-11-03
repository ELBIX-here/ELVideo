# Saves a PNG of the running (debug, ELV_TEST_CAPTURABLE) ELVideo main window as build\ui\<name>.png.
# Usage: ui_snap.ps1 <name> [-Title <window title>]
param([Parameter(Mandatory)][string]$Name, [string]$Title = "ELVideo")
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Collections.Generic;
public static class SnapWin {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr h, IntPtr r, IntPtr rgn, uint flags);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public static string Find(uint pid, string title) { string found = null; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); var sb = new StringBuilder(256); GetWindowText(h, sb, 256); if (p==pid && IsWindowVisible(h) && sb.ToString()==title && found==null) found = String.Format("0x{0:X}", h.ToInt64()); return true; }, IntPtr.Zero); return found; }
}
"@
$h = $null
foreach ($p in Get-Process ELVideo -ErrorAction Stop | Where-Object { $_.Path -like 'C:\Projects\*' }) {
  $h = [SnapWin]::Find([uint32]$p.Id, $Title)
  if ($h) { break }
}
if (-not $h) { "window not found: $Title"; exit 1 }
$hwnd = [IntPtr][Convert]::ToInt64($h, 16)
if ([SnapWin]::IsIconic($hwnd)) { [SnapWin]::ShowWindow($hwnd, 4) | Out-Null; Start-Sleep -Milliseconds 400 }  # restore without activating
$tmp = Join-Path $env:TEMP ("elv-snap-" + [guid]::NewGuid().ToString("N"))
# A window that is not repainting sends no frame; nudge it to repaint while capturing.
$cli = Start-Process "C:\Projects\ELVideo\build\debug\tools\cli\elvideo-cli.exe" -ArgumentList "capture", $h, "2", "`"$tmp`"" -NoNewWindow -PassThru
foreach ($i in 1..6) { Start-Sleep -Milliseconds 250; [SnapWin]::RedrawWindow([IntPtr][Convert]::ToInt64($h, 16), [IntPtr]::Zero, [IntPtr]::Zero, 0x0185) | Out-Null }
$cli.WaitForExit()
$png = Get-ChildItem $tmp -Filter *.png | Select-Object -First 1
if (-not $png) { "no frame"; exit 1 }
New-Item -ItemType Directory -Force "C:\Projects\ELVideo\build\ui" | Out-Null
Move-Item -Force $png.FullName "C:\Projects\ELVideo\build\ui\$Name.png"
Remove-Item -Recurse -Force $tmp
"C:\Projects\ELVideo\build\ui\$Name.png"
