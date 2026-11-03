# Drags the mouse from (X1,Y1) to (X2,Y2) in physical screen pixels (left button),
# then puts the cursor back where it was.
param([int]$X1, [int]$Y1, [int]$X2, [int]$Y2)
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Threading;
public static class Drag {
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public struct POINT { public int X, Y; }
  public static void Run(int x1, int y1, int x2, int y2) {
    SetProcessDpiAwarenessContext(new IntPtr(-4));  // per-monitor v2: physical coordinates
    POINT old; GetCursorPos(out old);
    SetCursorPos(x1, y1); Thread.Sleep(100);
    mouse_event(0x2, 0, 0, 0, UIntPtr.Zero);  // left down
    for (int i = 1; i <= 20; i++) { SetCursorPos(x1 + (x2 - x1) * i / 20, y1 + (y2 - y1) * i / 20); Thread.Sleep(15); }
    Thread.Sleep(100);
    mouse_event(0x4, 0, 0, 0, UIntPtr.Zero);  // left up
    Thread.Sleep(100);
    SetCursorPos(old.X, old.Y);
  }
}
"@
[Drag]::Run($X1, $Y1, $X2, $Y2)
"dragged ($X1,$Y1) -> ($X2,$Y2)"
