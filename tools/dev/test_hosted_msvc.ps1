# SPDX-License-Identifier: GPL-2.0-or-later
# Runs on Windows using disposable VS, installer, and build fixtures.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$windowsPowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$engine = if ($PSVersionTable.PSEdition -eq 'Core') { Join-Path $PSHOME 'pwsh.exe' } else { $windowsPowerShell }
$cmd = "$env:SystemRoot\System32\cmd.exe"
$root = Join-Path ([IO.Path]::GetTempPath()) "Blendity hosted MSVC $([Guid]::NewGuid().ToString('N'))"
$dev = Join-Path $root 'tools\dev'
$fakeProgramFiles = Join-Path $root 'Program Files (x86)'
$installer = Join-Path $fakeProgramFiles 'Microsoft Visual Studio\Installer'
$instance = Join-Path $root 'Visual Studio\Enterprise'
$otherInstance = Join-Path $root 'Visual Studio\BuildTools'
$activation = Join-Path $instance 'VC\Auxiliary\Build'
$toolBin = 'bin\Hostx64\x64'
$runtimeTemp = Join-Path $root 'runner temp'
$envFile = Join-Path $root 'github.env'
$transcript = Join-Path $root 'commands.log'
$fakeExe = Join-Path $root 'fixture.exe'
$oldPath = $env:Path

function Assert-Case([bool]$Passed, [string]$Message) {
    if (!$Passed) { throw $Message }
}

function Add-FakeTools([string]$InstancePath, [string]$Patch, [string[]]$Tools = @('cl', 'link')) {
    $bin = Join-Path $InstancePath "VC\Tools\MSVC\$Patch\$toolBin"
    [IO.Directory]::CreateDirectory($bin) | Out-Null
    foreach ($tool in $Tools) { Copy-Item -LiteralPath $fakeExe -Destination (Join-Path $bin "$tool.exe") }
    $bin
}

function Invoke-Prepare([string]$Name, [int]$ExpectedCode, [string]$ExpectedMessage) {
    $output = & $engine -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$dev\hosted_msvc.ps1" -Mode Prepare 2>&1
    $actualCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $root "$Name.log")
    Assert-Case ($actualCode -eq $ExpectedCode -and ($output -join "`n") -match $ExpectedMessage) "Unexpected Prepare result for $Name (exit $actualCode): $output"
}

function Read-PreparedPin([string]$ExpectedVersion) {
    $pin = ConvertFrom-StringData ((Get-Content -LiteralPath $envFile -Raw).Replace('\', '\\'))
    Assert-Case ($pin.BLENDITY_HOSTED_VS_PATH -eq $instance -and $pin.BLENDITY_VCVARS_VER -eq $ExpectedVersion -and $pin.BLENDITY_VCVARS_STRICT -eq '1') 'Prepare exported a different VS instance, patch, or strict setting.'
    foreach ($key in $pin.Keys) { [Environment]::SetEnvironmentVariable($key, $pin[$key]) }
}

function Invoke-FixtureBuild([string]$Name, [string]$BuildArguments, [int]$ExpectedCode, [string]$ExpectedMessage) {
    Push-Location -LiteralPath $root
    try {
        $output = & $cmd /d /c "call tools\dev\hosted_msvc.cmd $BuildArguments" 2>&1
        $actualCode = $LASTEXITCODE
    } finally { Pop-Location }
    $output | Set-Content -LiteralPath (Join-Path $root "$Name.log")
    Assert-Case ($actualCode -eq $ExpectedCode -and ($output -join "`n") -match $ExpectedMessage) "Unexpected wrapper result for $Name (exit $actualCode): $output"
}

foreach ($directory in @($dev, $installer, $activation, $runtimeTemp)) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
Copy-Item -LiteralPath "$PSScriptRoot\hosted_msvc.ps1", "$PSScriptRoot\hosted_msvc.cmd" -Destination $dev

# Build one stand-in for the external programs. The executable name selects the fixture behavior.
@'
using System;
using System.IO;
using System.Reflection;

[assembly: AssemblyFileVersion("19.51.49999.0")]
public class Fixture
{
    public static int Main(string[] args)
    {
        string name = Path.GetFileNameWithoutExtension(Assembly.GetExecutingAssembly().Location);
        string instance = Environment.GetEnvironmentVariable("FIXTURE_VS");
        string log = Environment.GetEnvironmentVariable("FIXTURE_LOG");
        File.AppendAllText(log, name + " | " + Environment.CurrentDirectory + " | " +
                           string.Join(" | ", args) + Environment.NewLine);
        if (name == "vswhere")
            Console.WriteLine(instance);
        if (name == "cl")
            Console.WriteLine("Fixture Microsoft C/C++ Compiler Version 19.51.49999 for x64");
        if (name != "setup")
            return 0;

        string status = Environment.GetEnvironmentVariable("FIXTURE_INSTALL");
        if (status == "install" || status == "3010")
        {
            string bin = Path.Combine(instance, @"VC\Tools\MSVC\14.51.47\bin\Hostx64\x64");
            Directory.CreateDirectory(bin);
            foreach (string tool in new[] { "cl.exe", "link.exe" })
                File.Copy(Environment.GetEnvironmentVariable("FIXTURE_EXE"), Path.Combine(bin, tool));
        }
        File.WriteAllText(Path.Combine(Path.GetTempPath(), "dd_setup_fixture.log"), "fixture installer diagnostics");
        int code;
        return int.TryParse(status, out code) ? code : 0;
    }
}
'@ | Set-Content -LiteralPath "$root\fixture.cs"
@'
param([string]$Root)
$ErrorActionPreference = 'Stop'
Add-Type -Path "$Root\fixture.cs" -OutputType ConsoleApplication -OutputAssembly "$Root\fixture.exe"
'@ | Set-Content -LiteralPath "$root\make-fixture.ps1"
& $windowsPowerShell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$root\make-fixture.ps1" $root
Assert-Case ($LASTEXITCODE -eq 0) 'Fixture executable could not be created.'
Copy-Item -LiteralPath $fakeExe -Destination "$installer\vswhere.exe"
Copy-Item -LiteralPath $fakeExe -Destination "$installer\setup.exe"

@'
@echo off
if /i not "%*"=="-vcvars_ver=%BLENDITY_VCVARS_VER%" exit /b 91
if "%FIXTURE_ACTIVATION%"=="fail" exit /b 92
set "VCToolsVersion=%BLENDITY_VCVARS_VER%"
if "%FIXTURE_ACTIVATION%"=="version" set "VCToolsVersion=14.52.37025"
if "%FIXTURE_ACTIVATION%"=="compiler" exit /b 0
set "PATH=%FIXTURE_VS%\VC\Tools\MSVC\%BLENDITY_VCVARS_VER%\bin\Hostx64\x64;%PATH%"
if "%FIXTURE_ACTIVATION%"=="linker" set "PATH=%FIXTURE_WRONG_LINK%;%PATH%"
exit /b 0
'@ | Set-Content -LiteralPath "$activation\vcvars64.bat"
@'
@echo off
>>"%FIXTURE_LOG%" echo build %* strict=%BLENDITY_VCVARS_STRICT% version=%VCToolsVersion% no_libs=%BLENDITY_NO_LIBS% out=%BLENDITY_OUT%
exit /b %FIXTURE_BUILD_EXIT%
'@ | Set-Content -LiteralPath "$root\build.bat"

$environment = @{
    GITHUB_ACTIONS = 'true'
    GITHUB_ENV = $envFile
    RUNNER_ENVIRONMENT = 'github-hosted'
    RUNNER_OS = 'Windows'
    RUNNER_ARCH = 'X64'
    RUNNER_TEMP = $runtimeTemp
    TEMP = $runtimeTemp
    TMP = $runtimeTemp
    'ProgramFiles(x86)' = $fakeProgramFiles
    FIXTURE_VS = $instance
    FIXTURE_LOG = $transcript
    FIXTURE_EXE = $fakeExe
    FIXTURE_BUILD_EXIT = '0'
    BLENDITY_HOSTED_VS_PATH = $null
    BLENDITY_VCVARS_VER = $null
    BLENDITY_VCVARS_STRICT = $null
    BLENDITY_NO_LIBS = $null
    BLENDITY_OUT = $null
    FIXTURE_ACTIVATION = $null
    FIXTURE_WRONG_LINK = $null
    FIXTURE_INSTALL = $null
}
$savedEnvironment = @{}
$passed = $false
try {
    foreach ($key in $environment.Keys) {
        $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key)
        [Environment]::SetEnvironmentVariable($key, $environment[$key])
    }
    $env:RUNNER_ENVIRONMENT = 'self-hosted'
    Invoke-Prepare 'non-hosted' 1 'requires a GitHub-hosted Windows x64 job'
    Assert-Case (!(Test-Path -LiteralPath $transcript)) 'A non-hosted run reached VS tooling.'
    $env:RUNNER_ENVIRONMENT = 'github-hosted'

    $wrongCompiler = Add-FakeTools $instance '14.52.37025'
    $env:Path = "$wrongCompiler;$oldPath"
    $env:FIXTURE_WRONG_LINK = Join-Path $root 'wrong linker'
    [IO.Directory]::CreateDirectory($env:FIXTURE_WRONG_LINK) | Out-Null
    Copy-Item -LiteralPath $fakeExe -Destination "$env:FIXTURE_WRONG_LINK\link.exe"
    Add-FakeTools $otherInstance '14.51.99' | Out-Null
    Add-FakeTools $instance '14.51.9' | Out-Null
    $expectedBin = Add-FakeTools $instance '14.51.10'
    Add-FakeTools $instance '14.51.20' @('cl') | Out-Null

    Invoke-Prepare 'preinstalled' 0 'already present\. Skipping the installer'
    Read-PreparedPin '14.51.10'
    Assert-Case (!(Select-String -LiteralPath $transcript -Pattern '^setup \|' -Quiet)) 'Prepare ran the installer for preinstalled tools.'
    Assert-Case ([bool](Select-String -LiteralPath $transcript -Pattern '\| -latest \| -products \| \* \| -requires \| Microsoft\.VisualStudio\.Component\.VC\.Tools\.x86\.x64 \| -property \| installationPath$' -Quiet)) 'Prepare did not use build.bat instance selection.'

    Invoke-FixtureBuild 'inherited-path' 'release all' 0 'Fixture Microsoft C/C\+\+ Compiler Version 19\.51\.49999 for x64'
    $activeLog = Get-Content -LiteralPath "$root\inherited-path.log" -Raw
    foreach ($tool in @('cl', 'link')) {
        Assert-Case ($activeLog.Contains("$expectedBin\$tool.exe")) "Activation did not identify the expected $tool."
    }
    $env:BLENDITY_OUT = 'dist\windows\Blendity-full.exe'
    Invoke-FixtureBuild 'release-full' 'release app' 0 'Active VCToolsVersion: 14\.51\.10'
    $env:BLENDITY_OUT = $null
    $env:BLENDITY_NO_LIBS = '1'
    Invoke-FixtureBuild 'release-lite' 'release app' 0 'Active VCToolsVersion: 14\.51\.10'

    $activationFailures = @{
        fail = 'vcvars64 failed with exit 92'
        version = "VCToolsVersion '14\.52\.37025', expected '14\.51\.10'"
        compiler = 'First cl on PATH'
        linker = 'First link on PATH'
    }
    foreach ($failure in $activationFailures.Keys) {
        $env:FIXTURE_ACTIVATION = $failure
        Invoke-FixtureBuild "activation-$failure" 'release all' 1 $activationFailures[$failure]
    }
    $env:FIXTURE_ACTIVATION = $null
    $buildCalls = @(Select-String -LiteralPath $transcript -Pattern '^build ' | ForEach-Object { $_.Line.TrimEnd() })
    Assert-Case ($buildCalls.Count -eq 3) 'An activation failure reached the build or a successful activation did not.'
    Assert-Case ($buildCalls[0] -eq 'build release all strict=1 version=14.51.10 no_libs= out=' -and
        $buildCalls[1] -eq 'build release app strict=1 version=14.51.10 no_libs= out=dist\windows\Blendity-full.exe' -and
        $buildCalls[2] -eq 'build release app strict=1 version=14.51.10 no_libs=1 out=') 'The wrapper changed arguments or release settings.'
    $env:FIXTURE_BUILD_EXIT = '7'
    Invoke-FixtureBuild 'build-failure' 'release all' 7 'Active VCToolsVersion'
    $env:FIXTURE_BUILD_EXIT = '0'

    # A second VS instance still has 14.51. The selected instance has only a partial patch and 14.52.
    foreach ($patch in @('14.51.9', '14.51.10')) {
        Remove-Item -LiteralPath "$instance\VC\Tools\MSVC\$patch\$toolBin\cl.exe"
    }
    Remove-Item -LiteralPath $envFile
    Move-Item -LiteralPath "$installer\setup.exe" -Destination "$installer\setup.off"
    Invoke-Prepare 'missing-setup' 1 'Installer is missing'
    Move-Item -LiteralPath "$installer\setup.off" -Destination "$installer\setup.exe"
    foreach ($case in @(@('failed', '1603', 'Installer failed \(exit 1603\)'),
                        @('absent', '0', 'Installer reported success but no usable MSVC 14\.51'),
                        @('reboot', '3010', 'Installer requires a restart \(exit 3010\)'))) {
        $env:FIXTURE_INSTALL = $case[1]
        Invoke-Prepare $case[0] 1 $case[2]
        Assert-Case (!(Test-Path -LiteralPath $envFile)) "$($case[0]) published a toolset for a failed prepare."
    }
    Remove-Item -LiteralPath "$instance\VC\Tools\MSVC\14.51.47\$toolBin\cl.exe", "$instance\VC\Tools\MSVC\14.51.47\$toolBin\link.exe"

    $env:FIXTURE_INSTALL = 'install'
    Invoke-Prepare 'cold' 0 'Selected MSVC 14\.51\.47'
    Read-PreparedPin '14.51.47'
    $installCalls = @(Select-String -LiteralPath $transcript -Pattern '^setup \|' | ForEach-Object { $_.Line })
    $expectedCall = "setup | $runtimeTemp | modify | --installPath | $instance | --add | Microsoft.VisualStudio.Component.VC.14.51.x86.x64 | --quiet | --norestart"
    Assert-Case ($installCalls.Count -eq 4 -and @($installCalls | Where-Object { $_ -ne $expectedCall }).Count -eq 0) 'Setup did not run once per missing-toolset attempt with the documented component, instance, flags, and working directory.'
    $coldLog = Get-Content -LiteralPath "$root\cold.log" -Raw
    Assert-Case ($coldLog -match 'Before setup inventory' -and $coldLog -match 'After setup inventory' -and
        $coldLog -match 'Installer exit 0, elapsed' -and $coldLog -match 'fixture installer diagnostics') 'Cold setup missed diagnostic evidence.'
    Remove-Item -LiteralPath $envFile
    Invoke-Prepare 'warm' 0 'already present\. Skipping the installer'
    Read-PreparedPin '14.51.47'
    Assert-Case (@(Select-String -LiteralPath $transcript -Pattern '^setup \|').Count -eq 4) 'Repeated Prepare reinstalled the toolset.'
    Invoke-FixtureBuild 'after-setup' 'release all' 0 'Active VCToolsVersion: 14\.51\.47'

    $passed = $true
    Write-Host 'Hosted MSVC fixture checks passed (8 setup paths, 9 build paths).'
} finally {
    foreach ($key in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key]) }
    $env:Path = $oldPath
    if ($passed) {
        $resolved = (Resolve-Path -LiteralPath $root).Path
        $scratchPrefix = [IO.Path]::Combine([IO.Path]::GetTempPath(), 'Blendity hosted MSVC ')
        if ($resolved -eq $root -and $resolved.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    } else {
        Write-Host "Fixture evidence: $root"
    }
}
