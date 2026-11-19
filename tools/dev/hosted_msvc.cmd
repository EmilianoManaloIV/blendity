@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Activate and check the toolset prepared by hosted_msvc.ps1 in the build shell.
setlocal DisableDelayedExpansion
set "ERRORLEVEL="
if /i not "%GITHUB_ACTIONS%"=="true" goto missing_setup
if /i not "%RUNNER_ENVIRONMENT%"=="github-hosted" goto missing_setup
if /i not "%RUNNER_OS%"=="Windows" goto missing_setup
if /i not "%RUNNER_ARCH%"=="X64" goto missing_setup
if not defined BLENDITY_HOSTED_VS_PATH goto missing_setup
if not defined BLENDITY_VCVARS_VER goto missing_setup
if not "%BLENDITY_VCVARS_STRICT%"=="1" goto missing_setup

pushd "%~dp0..\.." || exit /b 1
call "%BLENDITY_HOSTED_VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=%BLENDITY_VCVARS_VER%
if not "%ERRORLEVEL%"=="0" goto activation_failed
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0hosted_msvc.ps1" -Mode Verify
if not "%ERRORLEVEL%"=="0" goto verification_failed
call build.bat %*
set "build_exit=%ERRORLEVEL%"
popd
exit /b %build_exit%

:activation_failed
echo [hosted-msvc] ERROR: vcvars64 failed with exit %ERRORLEVEL%.
goto failed
:verification_failed
echo [hosted-msvc] ERROR: Toolset verification failed with exit %ERRORLEVEL%.
:failed
popd
exit /b 1

:missing_setup
echo [hosted-msvc] ERROR: Run hosted_msvc.ps1 -Mode Prepare in a GitHub-hosted Windows x64 job before building.
exit /b 1
