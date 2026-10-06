@echo off
rem Blendity - Windows build without CMake. Needs only Visual Studio (or Build Tools) with C++.
rem Usage: build.bat [release|debug] [app|stress|tests|all]
setlocal enabledelayedexpansion

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=release
set TARGET=%2
if "%TARGET%"=="" set TARGET=all

where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  set "VSDIR="
  if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
  )
  if "!VSDIR!"=="" (
    for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
      for /d %%e in ("%%v\*") do if exist "%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%%e"
    )
  )
  if "!VSDIR!"=="" (
    echo [blendity] Could not find Visual Studio C++ tools. Install "Desktop development with C++".
    exit /b 1
  )
  call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

cd /d "%~dp0"
if not exist build\obj_%CONFIG% mkdir build\obj_%CONFIG%
if not exist dist\windows mkdir dist\windows

set COMMON=/nologo /Iextern\sky\include /std:c++17 /EHsc /W3 /MP /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /permissive- /Zc:__cplusplus
if /i "%CONFIG%"=="debug" (
  set FLAGS=%COMMON% /Od /Zi /MTd /DBL_DEBUG
) else (
  set FLAGS=%COMMON% /O2 /Oi /GL /fp:fast /MT /DNDEBUG
)
set LIBSRC=
for %%d in (core image render scene editor research platform) do (
  for %%f in (src\%%d\*.cpp) do set LIBSRC=!LIBSRC! %%f
)
rem Blender's own bundled dependencies (see extern\README.md)
set LIBSRC=!LIBSRC! extern\ufbx\ufbx.c extern\sky\source\sky_hosek.cpp
set OBJ=build\obj_%CONFIG%
set LINKFLAGS=/link /LTCG /INCREMENTAL:NO user32.lib gdi32.lib shell32.lib
if /i "%CONFIG%"=="debug" set LINKFLAGS=/link /DEBUG user32.lib gdi32.lib shell32.lib

if /i "%TARGET%"=="all" goto app
if /i "%TARGET%"=="app" goto app
if /i "%TARGET%"=="stress" goto stress
if /i "%TARGET%"=="tests" goto tests

:app
echo [blendity] Building editor (%CONFIG%)...
rem BLENDITY_OUT lets you build while a copy of the editor is still running.
if "%BLENDITY_OUT%"=="" set BLENDITY_OUT=dist\windows\Blendity.exe
cl %FLAGS% /Fo%OBJ%\ /Fe:%BLENDITY_OUT% %LIBSRC% src\app\main.cpp %LINKFLAGS% /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
if errorlevel 1 exit /b 1
if /i not "%TARGET%"=="all" goto done

:stress
echo [blendity] Building stress tests (%CONFIG%)...
cl %FLAGS% /Fo%OBJ%\ /Fe:dist\windows\blendity_stress.exe %LIBSRC% stress\stress_main.cpp %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if /i not "%TARGET%"=="all" goto done

:tests
echo [blendity] Building unit tests (%CONFIG%)...
cl %FLAGS% /Fo%OBJ%\ /Fe:dist\windows\blendity_tests.exe %LIBSRC% tests\test_main.cpp %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

:done
echo [blendity] Done. Binaries are in dist\windows\
endlocal
