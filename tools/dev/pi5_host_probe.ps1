# SPDX-License-Identifier: GPL-2.0-or-later
# Run in an exclusive host timing window. Results stay in a new ignored build subdirectory.
param([Parameter(Mandatory)][string]$Exe)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$runDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ("../../build/pi5-host-" + [guid]::NewGuid().ToString('N'))))
$project = Join-Path $runDir 'project'
$isolated = @{
    BLENDITY_PROJECT = $project
    BLENDITY_TRASH = Join-Path $runDir 'trash'
    BLENDITY_PREFS_DIR = Join-Path $runDir 'prefs'
    BLENDITY_CACHE_DIR = Join-Path $runDir 'cache'
}
foreach ($dir in @($isolated.Values) + @((Join-Path $project 'Assets'))) {
    [IO.Directory]::CreateDirectory($dir) | Out-Null
}

[IO.File]::WriteAllLines((Join-Path $isolated.BLENDITY_PREFS_DIR 'prefs.txt'), @('update_check=0', 'viewport_device=1'))

function Invoke-Probe([string]$Name, [string[]]$Commands) {
    $png = Join-Path $runDir "$Name.png"
    $arguments = @('--headless-screenshot', $png, '--size', '1280x720', '--frames', '1')
    foreach ($command in $Commands) { $arguments += @('--cmd', $command) }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exePath
    $info.WorkingDirectory = $runDir
    $info.Arguments = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($key in $isolated.Keys) { $info.EnvironmentVariables[$key] = $isolated[$key] }
    $info.EnvironmentVariables['BLENDITY_VIEWPORT_DEVICE'] = 'cpu'

    $timer = [Diagnostics.Stopwatch]::StartNew()
    $p = [Diagnostics.Process]::Start($info)
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    [long]$peak = 0
    while (!$p.WaitForExit(10)) {
        try {
            $p.Refresh()
            $peak = [Math]::Max($peak, $p.PeakWorkingSet64)
        } catch [InvalidOperationException] { break } # Process exited between the wait and the sample.
    }
    $p.WaitForExit()
    $timer.Stop()
    $log = $stdout.Result + [Environment]::NewLine + $stderr.Result
    [IO.File]::WriteAllText((Join-Path $runDir "$Name.log"), $log)
    if ($p.ExitCode -ne 0 -or $log -notmatch '\(1280x720\)' -or !(Test-Path -LiteralPath $png) -or !$peak) {
        throw "$Name failed. See $runDir/$Name.log"
    }
    $row = [pscustomobject]@{ stage = $Name; wall_ms = $timer.Elapsed.TotalMilliseconds; observed_peak_working_set_bytes = $peak }
    $row | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir "$Name.metrics.json")
    $row
}

Write-Host "Scratch results: $runDir"
$cold = Invoke-Probe 'launch-and-png' @('viewport device cpu', 'perf frames 1', 'viewport status', 'perf memory')
Copy-Item -LiteralPath (Join-Path $runDir 'launch-and-png.png') -Destination (Join-Path $project 'Assets/probe.png')
$commands = @('viewport device cpu', 'quality low', 'shading shaded', 'stress 64 Cube', 'create Sphere', 'select Sphere',
    'camera 30 15 18', 'perf frames 2', 'viewport status', 'perf memory', 'perf mark start',
    'perf drag at 550 290 640 290 mods alt frames 30', 'perf mark orbit')
# Shift the pivot on successive rendered frames; this measures pan redraw, separate from mouse dispatch.
foreach ($step in 1..12) {
    $pivot = ($step / 10).ToString([Globalization.CultureInfo]::InvariantCulture)
    $commands += @("camera 30 15 18 $pivot 0 0", 'perf frames 1')
}
$commands += @('perf mark pan-redraw', 'select Sphere', 'stats', 'subdivide 1', 'perf wait', 'perf frames 1',
    'perf mark select-and-subdivide', 'stats', 'undo', 'perf frames 1', 'perf mark undo', 'stats',
    'select Sphere', 'texture Assets/probe.png', 'perf frames 2', 'perf wait', 'perf mark texture',
    'save Probe', 'perf mark save', 'open Probe', 'perf frames 2', 'perf wait', 'perf mark reopen',
    'stats', 'viewport status', 'perf memory', 'window Editing', 'develop open project/Assets/probe.png',
    'perf wait', 'perf frames 1', 'develop list', 'perf mark image-editor-open', 'perf memory')
$journey = Invoke-Probe 'journey' $commands
$log = Get-Content -Raw -LiteralPath (Join-Path $runDir 'journey.log')
foreach ($pattern in @('Spawned 64 Cube', 'Assigned Assets/probe.png as Base Map on 1 object',
                       'Opened Probe\.scene', 'Views: CPU \(Viewport Device\)', '29 drag frame\(s\) mean', 'perf mark reopen:', 'Editing: probe\.png \(1280 x 720\)')) {
    if ($log -notmatch $pattern) { throw "Missing '$pattern' in $runDir/journey.log" }
}
$stats = [regex]::Matches($log, '(\d+) objects, (\d+) verts, (\d+) faces, scene memory')
if ($stats.Count -ne 4 -or [int]$stats[1].Groups[3].Value -le [int]$stats[0].Groups[3].Value -or
    $stats[2].Value -ne $stats[0].Value -or $stats[3].Value -ne $stats[0].Value) {
    throw "Subdivision, undo or reopened scene counts differ from expectation; see $runDir/journey.log"
}
$hasher = [Security.Cryptography.SHA256]::Create()
$file = [IO.File]::OpenRead($exePath)
try { $sha256 = [BitConverter]::ToString($hasher.ComputeHash($file)).Replace('-', '') }
finally { $file.Dispose(); $hasher.Dispose() }
$summary = [ordered]@{
    machine = $env:COMPUTERNAME
    cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
    os = [Environment]::OSVersion.VersionString
    executable = $exePath
    sha256 = $sha256
    backend = 'headless CPU viewport from first frame'
    size = '1280x720 editor framebuffer'
    journey_quality = 'Low (67% viewport scale)'
    stages = @($cold, $journey)
}
$summary | ConvertTo-Json -Depth 4 | Tee-Object -FilePath (Join-Path $runDir 'result.json')
