# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)]
    [ValidateSet('Prepare', 'Verify')]
    [string]$Mode
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$component = 'Microsoft.VisualStudio.Component.VC.14.51.x86.x64'
$patchPattern = '^14\.51\.\d+$'
$toolBin = 'bin\Hostx64\x64'
$toolNames = @('cl', 'link')

function Get-ToolsetInventory([string]$Instance, [string]$Stage) {
    $root = Join-Path $Instance 'VC\Tools\MSVC'
    Write-Host "[hosted-msvc] $Stage inventory under $root"
    if (!(Test-Path -LiteralPath $root -PathType Container)) {
        Write-Host '[hosted-msvc] No toolset directory.'
        return
    }
    foreach ($directory in Get-ChildItem -LiteralPath $root -Directory) {
        $bin = Join-Path $directory.FullName $toolBin
        $hasCompiler = Test-Path -LiteralPath (Join-Path $bin 'cl.exe') -PathType Leaf
        $hasLinker = Test-Path -LiteralPath (Join-Path $bin 'link.exe') -PathType Leaf
        Write-Host "[hosted-msvc] $($directory.Name) x64 cl=$hasCompiler link=$hasLinker"
        if ($directory.Name -match $patchPattern -and $hasCompiler -and $hasLinker) { $directory }
    }
}

function Install-Toolset([string]$InstallerDirectory, [string]$Instance) {
    $setup = Join-Path $InstallerDirectory 'setup.exe'
    if (!(Test-Path -LiteralPath $setup -PathType Leaf)) {
        throw "Visual Studio Installer is missing: $setup"
    }

    # setup.exe runs from runner temp because it cannot modify an install from its own directory.
    $arguments = "modify --installPath `"$Instance`" --add $component --quiet --norestart"
    Write-Host "[hosted-msvc] $setup $arguments"
    $started = [DateTime]::UtcNow
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -WorkingDirectory $env:RUNNER_TEMP -Wait -PassThru
    $timer.Stop()
    Write-Host "[hosted-msvc] Installer exit $($process.ExitCode), elapsed $([int]$timer.Elapsed.TotalSeconds)s."

    if ($env:TEMP) {
        $logs = Get-ChildItem -LiteralPath $env:TEMP -Filter 'dd_*.log' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^dd_(bootstrapper|client|setup)' -and $_.LastWriteTimeUtc -ge $started } |
            Sort-Object LastWriteTimeUtc -Descending
        foreach ($log in $logs) { Write-Host "[hosted-msvc] Installer log: $($log.FullName)" }
        $logs | Select-Object -First 4 | ForEach-Object {
            Write-Host "[hosted-msvc] Tail of $($_.Name):"
            Get-Content -LiteralPath $_.FullName -Tail 20 -ErrorAction Continue | Write-Host
        }
    }

    if ($process.ExitCode -in 1641, 3010) {
        throw "Visual Studio Installer requires a restart (exit $($process.ExitCode))."
    }
    if ($process.ExitCode -ne 0) {
        throw "Visual Studio Installer failed (exit $($process.ExitCode))."
    }
}

function Prepare-Toolset {
    if (!$env:GITHUB_ENV -or !(Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container)) {
        throw 'GitHub Actions must provide GITHUB_ENV and an existing RUNNER_TEMP directory.'
    }
    Write-Host "[hosted-msvc] Image $env:ImageOS $env:ImageVersion. Revision $env:GITHUB_SHA."
    Write-Host "[hosted-msvc] $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID (attempt $env:GITHUB_RUN_ATTEMPT)."

    # Match the instance lookup in build.bat. Provisioning must target that VS instance.
    $installerDirectory = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installerDirectory 'vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere is missing: $vswhere" }
    $instances = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1 -or !$instances[0].Trim()) {
        throw "vswhere did not return one Visual Studio C++ instance (exit $LASTEXITCODE)."
    }
    $instance = $instances[0].Trim()
    Write-Host "[hosted-msvc] Visual Studio instance: $instance"
    $vcvars = Join-Path $instance 'VC\Auxiliary\Build\vcvars64.bat'
    if (!(Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw "x64 activation script is missing: $vcvars" }

    $usable = @(Get-ToolsetInventory $instance 'Before setup')
    if ($usable.Count -eq 0) {
        Install-Toolset $installerDirectory $instance
        $usable = @(Get-ToolsetInventory $instance 'After setup')
        if ($usable.Count -eq 0) { throw 'Installer reported success but no usable MSVC 14.51 x64 toolset was found.' }
    } else {
        Write-Host '[hosted-msvc] MSVC 14.51 already present. Skipping the installer.'
    }

    $selected = $usable | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    Write-Host "[hosted-msvc] Selected MSVC $($selected.Name) under $($selected.FullName)."
    Add-Content -LiteralPath $env:GITHUB_ENV -Encoding UTF8 -Value @(
        "BLENDITY_HOSTED_VS_PATH=$instance"
        "BLENDITY_VCVARS_VER=$($selected.Name)"
        'BLENDITY_VCVARS_STRICT=1'
    )
}

function Confirm-ActivatedToolset {
    $patch = $env:BLENDITY_VCVARS_VER
    if ($patch -notmatch $patchPattern -or !$env:BLENDITY_HOSTED_VS_PATH -or $env:BLENDITY_VCVARS_STRICT -ne '1') {
        throw 'Hosted MSVC selection is missing or invalid. Run Prepare before the build.'
    }
    if ($env:VCToolsVersion -ne $patch) {
        throw "vcvars64 selected VCToolsVersion '$env:VCToolsVersion', expected '$patch'."
    }

    $bin = Join-Path $env:BLENDITY_HOSTED_VS_PATH "VC\Tools\MSVC\$patch\$toolBin"
    foreach ($tool in $toolNames) {
        $expected = Join-Path $bin "$tool.exe"
        $resolved = @(& "$env:SystemRoot\System32\where.exe" $tool 2>$null)
        if ($LASTEXITCODE -ne 0 -or $resolved.Count -eq 0 -or $resolved[0] -ne $expected) {
            throw "First $tool on PATH is '$($resolved | Select-Object -First 1)', expected '$expected'."
        }
        $version = (Get-Item -LiteralPath $expected).VersionInfo.FileVersion
        Write-Host "[hosted-msvc] $tool $version at $expected"
    }
    Write-Host "[hosted-msvc] Active VCToolsVersion: $env:VCToolsVersion"
    & "$env:SystemRoot\System32\cmd.exe" /d /c 'cl 2>&1' | Write-Host
}

try {
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
        $env:RUNNER_OS -ne 'Windows' -or $env:RUNNER_ARCH -ne 'X64') {
        throw 'This helper requires a GitHub-hosted Windows x64 job.'
    }
    if ($Mode -eq 'Prepare') { Prepare-Toolset } else { Confirm-ActivatedToolset }
    exit 0
} catch {
    Write-Host "[hosted-msvc] ERROR: $($_.Exception.Message)"
    exit 1
}
