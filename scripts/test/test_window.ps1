# A plain window titled "ELV test window" that closes itself after $Seconds.
param([int]$Seconds = 20)
Add-Type -AssemblyName System.Windows.Forms
$f = New-Object System.Windows.Forms.Form
$f.Text = "ELV test window"; $f.Width = 640; $f.Height = 360; $f.BackColor = "SteelBlue"
$t = New-Object System.Windows.Forms.Timer; $t.Interval = $Seconds * 1000; $t.Add_Tick({ $f.Close() }); $t.Start()
[System.Windows.Forms.Application]::Run($f)
