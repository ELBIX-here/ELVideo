# Resizes the visible ELVideo main window (for screenshots of long pages).
param([int]$Width = 982, [int]$Height = 1400)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Rs { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, uint f); }
"@
$h = [Rs]::FindWindow([NullString]::Value, "ELVideo")
[Rs]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, $Width, $Height, 0x0014) | Out-Null  # SWP_NOZORDER | SWP_NOACTIVATE
"resized 0x{0:X}" -f $h.ToInt64()
