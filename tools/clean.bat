@echo off
rem Removes what the builds and the tools leave behind that is not source (task 0177): build\, the stress reports a run
rem wrote (latest.md is restored from git), the inbetweening model's .venv and __pycache__.
rem Usage: tools\clean.bat [--all]   (--all also removes dist\ and the inbetweening model's training runs)
setlocal
cd /d "%~dp0.."
if exist build rmdir /s /q build
if exist tools\inbetween\.venv rmdir /s /q tools\inbetween\.venv
if exist tools\inbetween\__pycache__ rmdir /s /q tools\inbetween\__pycache__
for /f "delims=" %%f in ('git ls-files --others --exclude-standard stress\results 2^>nul') do del /q "%%f"
git checkout -q -- stress\results\latest.md 2>nul
if /i "%~1"=="--all" if exist dist rmdir /s /q dist
if /i "%~1"=="--all" if exist tools\inbetween\runs rmdir /s /q tools\inbetween\runs
if /i "%~1"=="--all" if /i "%~1"=="--all" echo [blendity] clean.
