# Checks the debug player through mpv's JSON IPC (ELV_PLAYER_IPC, debug builds only)
# and UI Automation. Uses build\media\a_sync.mp4 (+ a_sync.srt) and b_hevc.mp4.
param([string]$Exe = "C:\Projects\ELVideo\build\debug\app\ELVideo.exe")
$ErrorActionPreference = "Stop"
$t = $PSScriptRoot
$media = "C:\Projects\ELVideo\build\media"
$pipeName = "elvtest-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$results = New-Object System.Collections.Generic.List[string]
function Check($name, $ok, $detail) { $results.Add(("{0}  {1}  {2}" -f ($(if ($ok) { "PASS" } else { "FAIL" })), $name, $detail)) }

function Start-Player($file) {
    $psi = New-Object Diagnostics.ProcessStartInfo $Exe
    $psi.Arguments = "--player `"$file`""
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables["ELV_PLAYER_IPC"] = $pipeName
    $psi.EnvironmentVariables["PATH"] = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
    $p = [Diagnostics.Process]::Start($psi)
    $script:pipe = New-Object IO.Pipes.NamedPipeClientStream(".", $pipeName, [IO.Pipes.PipeDirection]::InOut)
    $script:pipe.Connect(15000)
    $script:writer = New-Object IO.StreamWriter($script:pipe); $script:writer.AutoFlush = $true
    $script:reader = New-Object IO.StreamReader($script:pipe)
    return $p
}
$script:id = 0
function Mpv([object[]]$command) {
    $script:id++
    $script:writer.WriteLine((@{ command = $command; request_id = $script:id } | ConvertTo-Json -Compress -Depth 5))
    while ($true) {
        $line = $script:reader.ReadLine()
        if ($line -match "`"request_id`":$($script:id)[,}]") { return ($line | ConvertFrom-Json) }
    }
}
function Get($name) { (Mpv @("get_property", $name)).data }
function WaitFor($name, $predicate, $timeoutMs = 5000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) { $v = Get $name; if (& $predicate $v) { return $v }; Start-Sleep -Milliseconds 100 }
    return (Get $name)
}

# --- open, tracks, playlist, hardware decoding
$p = Start-Player "$media\a_sync.mp4"
$duration = WaitFor "duration" { param($v) $v -gt 0 }
Check "open + duration" ($duration -gt 12 -and $duration -lt 13) ("{0:N2} s" -f $duration)
$tracks = Get "track-list"
$types = @($tracks | ForEach-Object { $_.type })
$sub = @($tracks | Where-Object { $_.type -eq "sub" })[0]
Check "tracks (video, audio, auto-loaded .srt)" (($types -join ",") -eq "video,audio,sub" -and $sub.external -and $sub.selected) ("{0}; sub external={1} selected={2}" -f ($types -join ","), $sub.external, $sub.selected)
Check "folder playlist" ((Get "playlist-count") -eq 2) ("count {0}" -f (Get "playlist-count"))
Check "hardware decoding" ((Get "hwdec-current") -notin @($null, "no", "")) (Get "hwdec-current")

# --- seek, pause, frame step
Mpv @("set_property", "pause", $true) | Out-Null
Mpv @("seek", "5", "absolute+exact") | Out-Null
$pos = WaitFor "time-pos" { param($v) [math]::Abs($v - 5) -lt 0.02 }
WaitFor "seeking" { param($v) -not $v } | Out-Null; Start-Sleep -Milliseconds 300  # a step sent mid-seek is merged into the seek
Check "exact seek to 5 s" ([math]::Abs($pos - 5) -lt 0.02) ("{0:N3}" -f $pos)
Mpv @("frame-step", "1", "seek") | Out-Null  # what the player's . / F keys send
$stepped = WaitFor "time-pos" { param($v) $v -gt $pos + 0.001 } 3000
Check "frame step (+1/60 s)" ([math]::Abs(($stepped - $pos) - (1 / 60)) -lt 0.004) ("{0:N4} -> {1:N4}" -f $pos, $stepped)
WaitFor "seeking" { param($v) -not $v } | Out-Null; Start-Sleep -Milliseconds 300
Mpv @("frame-back-step") | Out-Null
$back = WaitFor "time-pos" { param($v) $v -lt $stepped - 0.001 } 3000
Check "frame back step (-1/60 s)" ([math]::Abs($back - $pos) -lt 0.004) ("{0:N4} -> {1:N4}" -f $stepped, $back)

# --- speed with pitch correction, A-B loop
Mpv @("set_property", "speed", 2) | Out-Null
Check "speed 2x, pitch correction on" (((Get "speed") -eq 2) -and ((Get "audio-pitch-correction") -eq $true)) ("speed {0}" -f (Get "speed"))
Mpv @("set_property", "speed", 1) | Out-Null
Mpv @("set_property", "ab-loop-a", 2) | Out-Null; Mpv @("set_property", "ab-loop-b", 3) | Out-Null
Mpv @("seek", "2", "absolute+exact") | Out-Null; Mpv @("set_property", "pause", $false) | Out-Null
$max = 0; $min = 99; for ($i = 0; $i -lt 30; $i++) { $v = Get "time-pos"; $max = [math]::Max($max, $v); $min = [math]::Min($min, $v); Start-Sleep -Milliseconds 100 }
Check "A-B loop 2-3 s for 3 s" ($max -le 3.05 -and $min -ge 1.95) ("range {0:N2}..{1:N2}" -f $min, $max)
Mpv @("set_property", "ab-loop-a", "no") | Out-Null; Mpv @("set_property", "ab-loop-b", "no") | Out-Null

# --- subtitle delay, screenshot
Mpv @("add", "sub-delay", "0.3") | Out-Null
Check "subtitle delay" ([math]::Abs((Get "sub-delay") - 0.3) -lt 0.001) ("{0}" -f (Get "sub-delay"))
$shot = Join-Path $env:TEMP "elv_player_shot.png"; Remove-Item $shot -ErrorAction SilentlyContinue
$reply = Mpv @("screenshot-to-file", $shot, "video"); Start-Sleep -Milliseconds 500
Check "frame screenshot" (Test-Path $shot) $(if (Test-Path $shot) { "{0:N0} KB" -f ((Get-Item $shot).Length / 1KB) } else { "missing: " + $reply.error })
Remove-Item $shot -ErrorAction SilentlyContinue

# --- UI buttons (UI Automation) change mpv state. Paused, so the controls stay visible.
Mpv @("set_property", "pause", $true) | Out-Null; Start-Sleep -Milliseconds 500
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" "구간 반복 A-B (L)" | Out-Null; Start-Sleep -Milliseconds 300
Check "UI: A-B button sets A" ((Get "ab-loop-a") -ne "no") ("a={0}" -f (Get "ab-loop-a"))
Mpv @("set_property", "ab-loop-a", "no") | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File "$t\ui_invoke.ps1" "재생 (Space)" | Out-Null; Start-Sleep -Milliseconds 300
Check "UI: play button resumes" ((Get "pause") -eq $false) ("pause={0}" -f (Get "pause"))

# --- next file (HEVC), resume position
Mpv @("seek", "7.5", "absolute+exact") | Out-Null; Start-Sleep -Milliseconds 300
Mpv @("write-watch-later-config") | Out-Null
Mpv @("playlist-next") | Out-Null
$path = WaitFor "path" { param($v) $v -like "*b_hevc.mp4" }
Start-Sleep -Milliseconds 800
Check "next file (HEVC) plays" (($path -like "*b_hevc.mp4") -and ((Get "video-codec") -match "HEVC|hevc|265")) ("{0}, {1}" -f (Split-Path $path -Leaf), (Get "video-codec"))
$pipe.Dispose(); $p.CloseMainWindow() | Out-Null; $p.WaitForExit(5000) | Out-Null; if (-not $p.HasExited) { $p.Kill() }

$p = Start-Player "$media\a_sync.mp4"
$resumed = WaitFor "time-pos" { param($v) $v -gt 7 }
Check "resume where it stopped (7.5 s)" ([math]::Abs($resumed - 7.5) -lt 0.6) ("{0:N2}" -f $resumed)
$pipe.Dispose(); $p.Kill()

$results
"{0}/{1} passed" -f ($results | Where-Object { $_ -like "PASS*" }).Count, $results.Count
