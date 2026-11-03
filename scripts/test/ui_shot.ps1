# Saves a PNG of each visible window of the running ELVideo into build\ui\<tag>.
param([string]$Tag = "shot", [string]$Match = "")
$p = Get-Process ELVideo -ErrorAction Stop | Select-Object -First 1
$build = "C:\Projects\ELVideo\build\debug"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class ShotWin {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static List<string> Visible(uint pid) { var res = new List<string>(); EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if (p==pid && IsWindowVisible(h)) res.Add(String.Format("0x{0:X}", h.ToInt64())); return true; }, IntPtr.Zero); return res; }
}
"@
$i = 0
foreach ($h in [ShotWin]::Visible($p.Id)) {
  & "$build\tools\cli\elvideo-cli.exe" capture $h 1 "C:\Projects\ELVideo\build\ui\$Tag\$i" | Select-String "done"
  $i++
}
