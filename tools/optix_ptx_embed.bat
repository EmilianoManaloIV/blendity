@echo off
rem Task 0169 (ADR 0054): compiles src\render\optix_kernel.cu to PTX with NVRTC and writes the committed header
rem src\render\optix\optix_kernel_ptx.h (the bytes, the sources' hash, the OptiX version). Run it after changing the
rem kernel or optix_params.h, on a machine with the CUDA Toolkit (CUDA_PATH) and the OptiX SDK (OPTIX_ROOT_DIR, or the
rem newest one under NVIDIA's ProgramData folder). Builds without CUDA use the committed header.
setlocal EnableDelayedExpansion
where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  set "VSDIR="
  if exist "!VSWHERE!" for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
  if "!VSDIR!"=="" for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do for /d %%e in ("%%v\*") do if exist "%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%%e"
  if "!VSDIR!"=="" (echo [optix] Could not find Visual Studio C++ tools & exit /b 1)
  call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
)
cd /d "%~dp0\.."
if "%OPTIX_ROOT_DIR%"=="" for /f "delims=" %%o in ('dir /b /ad /o-d "%ProgramData%\NVIDIA Corporation\OptiX SDK *" 2^>nul') do if not defined OPTIX_ROOT_DIR set "OPTIX_ROOT_DIR=%ProgramData%\NVIDIA Corporation\%%o"
if not exist "%CUDA_PATH%\lib\x64\nvrtc.lib" (echo [optix] CUDA_PATH has no NVRTC: install the CUDA Toolkit & exit /b 1)
if not exist "%OPTIX_ROOT_DIR%\include\optix.h" (echo [optix] no OptiX SDK: set OPTIX_ROOT_DIR & exit /b 1)
if not exist build\optix mkdir build\optix
cl /nologo /EHsc /O2 /I"%CUDA_PATH%\include" tools\ptx_compile.cpp /Fo:build\optix\ /Fe:build\optix\ptx_compile.exe /link /LIBPATH:"%CUDA_PATH%\lib\x64" nvrtc.lib
if errorlevel 1 exit /b 1
set "PATH=%CUDA_PATH%\bin;%PATH%"
if not exist src\render\optix mkdir src\render\optix
rem compute_75 (Turing, the first RT cores): older cards keep GPU Compute.
build\optix\ptx_compile.exe src\render\optix_kernel.cu build\optix\optix_kernel.ptx --gpu-architecture=compute_75 -use_fast_math -std=c++17 -default-device -rdc=true -I"%OPTIX_ROOT_DIR%\include" -I"%CUDA_PATH%\include" -Isrc\render --header=src\render\optix\optix_kernel_ptx.h --hash-file=src\render\optix_kernel.cu --hash-file=src\render\optix_params.h "--optix-h=%OPTIX_ROOT_DIR%\include\optix.h"
if errorlevel 1 exit /b 1
echo [optix] wrote src\render\optix\optix_kernel_ptx.h from %OPTIX_ROOT_DIR%
