# Checks that every DLL/EXE in the packaged folder only imports DLLs that are in the
# folder (same directory or the exe's directory) or in System32. Run after package.ps1.
param([string]$Dir = "C:\Projects\ELVideo\dist\ELVideo")
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe | Where-Object { $_.FullName -match "Hostx64\\x64" } | Select-Object -First 1
$local = @{}
Get-ChildItem $Dir -Filter *.dll | ForEach-Object { $local[$_.Name.ToLower()] = $true }
$missing = 0
foreach ($bin in Get-ChildItem $Dir -Recurse -Include *.dll, *.exe) {
    $deps = & $dumpbin.FullName /nologo /dependents $bin.FullName | Where-Object { $_ -match "^\s+\S+\.dll\s*$" } | ForEach-Object { $_.Trim() }
    foreach ($d in $deps) {
        $n = $d.ToLower()
        if ($local[$n] -or (Test-Path (Join-Path $bin.DirectoryName $d)) -or (Test-Path "$env:SystemRoot\System32\$d") -or $n.StartsWith("api-ms-") -or $n.StartsWith("ext-ms-")) { continue }
        "{0} -> {1} MISSING" -f $bin.FullName.Substring($Dir.Length + 1), $d
        $missing++
    }
}
"checked; missing: $missing"
