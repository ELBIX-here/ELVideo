# A/V sync stimulus: window "ELV sync test" turns white while a 1 kHz beep plays.
# Beeps (200 ms) every 2 s from t=1 s, for $Seconds. Both are driven from the WAV
# playback start, so recorder offsets show up as a constant flash/beep offset.
param([int]$Seconds = 12, [double]$Amplitude = 0.15)
Add-Type -AssemblyName System.Windows.Forms

$rate = 48000
$frames = $rate * $Seconds
$ms = New-Object IO.MemoryStream
$w = New-Object IO.BinaryWriter($ms)
$w.Write([Text.Encoding]::ASCII.GetBytes("RIFF")); $w.Write([int](36 + $frames * 2))
$w.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt ")); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1)
$w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
$w.Write([Text.Encoding]::ASCII.GetBytes("data")); $w.Write([int]($frames * 2))
Add-Type @"
public static class Tone {
  public static byte[] Beeps(int rate, int frames, double amp) {
    var b = new byte[frames * 2];
    for (int i = 0; i < frames; i++) {
      double t = (double)i / rate;
      if (t >= 1 && ((t - 1) % 2) < 0.2) {
        short v = (short)(32767 * amp * System.Math.Sin(2 * System.Math.PI * 1000 * t));
        b[2 * i] = (byte)v; b[2 * i + 1] = (byte)(v >> 8);
      }
    }
    return b;
  }
}
"@
$w.Write([Tone]::Beeps($rate, $frames, $Amplitude))
$ms.Position = 0
$player = New-Object Media.SoundPlayer($ms); $player.Load()

$f = New-Object Windows.Forms.Form
$f.Text = "ELV sync test"; $f.Width = 480; $f.Height = 300; $f.BackColor = "Black"; $f.StartPosition = "CenterScreen"
$sw = New-Object Diagnostics.Stopwatch
$timer = New-Object Windows.Forms.Timer; $timer.Interval = 1
$timer.Add_Tick({
  $t = $sw.Elapsed.TotalSeconds
  $on = $t -ge 1 -and (($t - 1) % 2) -lt 0.2
  $color = if ($on) { [Drawing.Color]::White } else { [Drawing.Color]::Black }
  if ($f.BackColor -ne $color) { $f.BackColor = $color; $f.Refresh() }
  if ($t -gt $Seconds + 0.5) { $f.Close() }
})
$f.Add_Shown({
  # Give the recorder time to attach before the stimulus starts.
  Start-Sleep -Milliseconds 2500
  $player.Play(); $sw.Start(); $timer.Start()
})
[Windows.Forms.Application]::Run($f)
