# Presses Ctrl+Shift+<F-key> system-wide (default F9 = ELVideo start/stop, F10 = pause).
param([int]$F = 9)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Keys {
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public static void Combo(byte vk) {
    keybd_event(0x11, 0, 0, UIntPtr.Zero); keybd_event(0x10, 0, 0, UIntPtr.Zero);
    keybd_event(vk, 0, 0, UIntPtr.Zero); keybd_event(vk, 0, 2, UIntPtr.Zero);
    keybd_event(0x10, 0, 2, UIntPtr.Zero); keybd_event(0x11, 0, 2, UIntPtr.Zero);
  }
}
"@
[Keys]::Combo([byte](0x70 + $F - 1))
"pressed Ctrl+Shift+F$F"
