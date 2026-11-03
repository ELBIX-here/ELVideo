# Clicks a control in the running ELVideo window through UI Automation (no mouse).
# Usage: ui_invoke.ps1 <name> [-List]
param([string]$Name, [switch]$List)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
# The build under test, not an installed copy that may be running too.
$p = Get-Process ELVideo -ErrorAction Stop | Sort-Object { $_.Path -notlike 'C:\Projects\*' } | Select-Object -First 1
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$wins = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
foreach ($w in $wins) {
  $all = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  foreach ($e in $all) {
    $n = $e.Current.Name
    if ($List) { if ($n) { "{0} [{1}]" -f $n, $e.Current.ControlType.ProgrammaticName } ; continue }
    if ($n -eq $Name) {
      $pat = $null
      if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pat)) { $pat.Invoke(); "invoked $Name"; exit 0 }
      if ($e.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$pat)) { $pat.Toggle(); "toggled $Name"; exit 0 }
    }
  }
}
if (-not $List) { "not found: $Name"; exit 1 }
