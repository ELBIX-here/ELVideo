# Plays a quiet continuous 400 Hz tone for $Seconds from this (separate) process,
# to check that per-app audio leaves other apps' sound out.
param([int]$Seconds = 20, [double]$Amplitude = 0.05)
Add-Type @"
public static class Tone400 {
  public static byte[] Wav(int seconds, double amp) {
    int rate = 48000, frames = rate * seconds;
    var ms = new System.IO.MemoryStream(); var w = new System.IO.BinaryWriter(ms);
    w.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); w.Write(36 + frames * 2);
    w.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt ")); w.Write(16); w.Write((short)1); w.Write((short)1);
    w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
    w.Write(System.Text.Encoding.ASCII.GetBytes("data")); w.Write(frames * 2);
    for (int i = 0; i < frames; i++) w.Write((short)(32767 * amp * System.Math.Sin(2 * System.Math.PI * 400 * i / rate)));
    return ms.ToArray();
  }
}
"@
$player = New-Object Media.SoundPlayer (New-Object IO.MemoryStream (, [Tone400]::Wav($Seconds, $Amplitude)))
$player.PlaySync()
