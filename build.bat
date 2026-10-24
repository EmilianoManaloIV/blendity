@echo off
rem Blendity - Windows build without CMake. Needs only Visual Studio (or Build Tools) with C++.
rem Usage: build.bat [release|debug] [app|stress|tests|check|all|installer]
rem   check: the unit and stress tests from one compile (every source once, both linked from the same objects).
rem   BLENDITY_NO_LTCG=1: no whole-program optimization (/GL, /LTCG) - a faster build for quick local runs, not for checks:
rem   with /fp:fast, MSVC then compiles two code paths differently that some tests compare bit for bit.
rem   installer: the app, then dist\installers\Blendity-Setup-<VERSION>.exe with Inno Setup 6 (task 0076).
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
rem License texts and notices travel with the binaries (licenses\README.md).
xcopy /e /i /y /q licenses dist\windows\licenses >nul

rem --- Blender's prebuilt libraries (optional) ---------------------------------
rem Blendity uses them when blender\lib\windows_x64 exists (Blender's own layout,
rem fetched from projects.blender.org/blender/lib-windows_x64). Without them it
rem builds the dependency-free versions. BLENDITY_NO_LIBS=1 forces that build;
rem BLENDITY_LIBDIR points elsewhere.
if "%BLENDITY_LIBDIR%"=="" set "BLENDITY_LIBDIR=%~dp0..\blender\lib\windows_x64"
set USE_LIBS=0
set LIBDEFS=
set LIBINC=
set LIBLINK=
set LIBDIRS=
if "%BLENDITY_NO_LIBS%"=="1" goto libs_done
if not exist "%BLENDITY_LIBDIR%\tbb\include" goto libs_done
set USE_LIBS=1
set "L=%BLENDITY_LIBDIR%"
call :lib tbb              BL_WITH_TBB         "tbb\include"                                   "tbb\lib\tbb12.lib"
call :lib embree           BL_WITH_EMBREE      "embree\include"                                "embree\lib\embree4.lib"
call :lib openimagedenoise BL_WITH_OIDN        "openimagedenoise\include"                      "openimagedenoise\lib\OpenImageDenoise.lib"
call :lib eigen            BL_WITH_EIGEN       "eigen\include\eigen3"                          ""
call :lib opensubdiv       BL_WITH_OPENSUBDIV  "opensubdiv\include"                            "opensubdiv\lib\osdCPU.lib"
call :lib openexr          BL_WITH_OPENEXR     "openexr\include openexr\include\OpenEXR imath\include imath\include\Imath" "openexr\lib\OpenEXR.lib openexr\lib\OpenEXRCore.lib openexr\lib\Iex.lib openexr\lib\IlmThread.lib imath\lib\Imath.lib"
call :lib opencolorio      BL_WITH_OCIO        "opencolorio\include"                           "opencolorio\lib\OpenColorIO.lib"
call :lib manifold         BL_WITH_MANIFOLD    "manifold\include"                              "manifold\lib\manifold.lib"
call :lib meshoptimizer    BL_WITH_MESHOPT     "meshoptimizer\include"                         "meshoptimizer\lib\meshoptimizer.lib"
call :lib openpgl          BL_WITH_OPENPGL     "openpgl\include"                               "openpgl\lib\openpgl.lib"
call :lib jolt             BL_WITH_JOLT        "jolt\include"                                  "jolt\lib\Jolt.lib"
call :lib jpeg             BL_WITH_LIBJPEG     "jpeg\include"                                  "jpeg\lib\libjpeg.lib"
call :lib png              BL_WITH_LIBPNG      "png\include zlib\include"                      "png\lib\libpng.lib zlib\lib\libz_st.lib"
call :lib zstd             BL_WITH_ZSTD        "zstd\include"                                  "zstd\lib\zstd_static.lib"
rem Video files (task 0077): Blender's FFmpeg, whose DLLs sit in its lib folder (copied in :deploy_dlls).
call :lib ffmpeg           BL_WITH_FFMPEG      "ffmpeg\include"                                "ffmpeg\lib\avformat.lib ffmpeg\lib\avcodec.lib ffmpeg\lib\swscale.lib ffmpeg\lib\avutil.lib"
rem GPU rendering: the Vulkan headers (the loader comes with the GPU driver) and shaderc.
if exist "%L%\vulkan\include" call :lib shaderc BL_WITH_VULKAN "shaderc\include vulkan\include" "shaderc\lib\shaderc_shared.lib"
rem ABI settings the libraries were built with (blender\build_files\build_environment\cmake).
set LIBDEFS=%LIBDEFS% /DIMATH_DLL /DOPENEXR_DLL /DMANIFOLD_PAR=1 /DJPH_SHARED_LIBRARY /DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED /DJPH_DOUBLE_PRECISION /DJPH_CROSS_PLATFORM_DETERMINISTIC /DJPH_USE_CPU_COMPUTE /DJPH_OBJECT_STREAM /DJPH_USE_SSE4_1 /DJPH_USE_SSE4_2
:libs_done

rem --- NVIDIA OptiX (task 0169, ADR 0054; ADR 0037 before it) --------------------------------------------
rem The kernel's PTX is committed (src\render\optix\optix_kernel_ptx.h, written by tools\optix_ptx_embed.bat), so a
rem build needs only OptiX's host headers: OPTIX_ROOT_DIR, the newest "OptiX SDK *" NVIDIA installs under ProgramData,
rem or - only when BLENDITY_FETCH_OPTIX=1 (CI) - NVIDIA's public optix-dev headers at a pinned tag, fetched into
rem build\optix\sdk (the builder takes them from NVIDIA under NVIDIA's terms; nothing NVIDIA's is committed). At run
rem time only NVIDIA's driver is needed. BLENDITY_NO_OPTIX=1 leaves it out. Without the committed header, a machine
rem with the CUDA Toolkit compiles the kernel itself as before (tools\ptx_compile.cpp through NVRTC).
set USE_OPTIX=0
set OPTIX_DEV_TAG=v9.1.0
rem (the same tag as build.sh's OPTIX_DEV_TAG and the committed PTX's OptiX version: change them together)
if "%BLENDITY_NO_OPTIX%"=="1" goto optix_done
rem Without OPTIX_ROOT_DIR, the most recently installed SDK under NVIDIA's default folder.
if "%OPTIX_ROOT_DIR%"=="" for /f "delims=" %%o in ('dir /b /ad /o-d "%ProgramData%\NVIDIA Corporation\OptiX SDK *" 2^>nul') do if not defined OPTIX_ROOT_DIR set "OPTIX_ROOT_DIR=%ProgramData%\NVIDIA Corporation\%%o"
if "%OPTIX_ROOT_DIR%"=="" if "%BLENDITY_FETCH_OPTIX%"=="1" if not exist build\optix\sdk\include\optix.h git clone --quiet --depth 1 --branch %OPTIX_DEV_TAG% https://github.com/NVIDIA/optix-dev.git build\optix\sdk || echo [blendity] Fetching NVIDIA's OptiX headers %OPTIX_DEV_TAG% failed
if "%OPTIX_ROOT_DIR%"=="" if exist build\optix\sdk\include\optix.h set "OPTIX_ROOT_DIR=%CD%\build\optix\sdk"
if not exist "%OPTIX_ROOT_DIR%\include\optix.h" goto optix_done
if not exist src\render\optix\optix_kernel_ptx.h (
  echo [blendity] No committed OptiX kernel ^(src\render\optix\optix_kernel_ptx.h: tools\optix_ptx_embed.bat writes it^): building without OptiX
  goto optix_done
)
:optix_on
set USE_OPTIX=1
set LIBDEFS=%LIBDEFS% /DBL_WITH_OPTIX
rem (optixInit finds the driver's OptiX through the registry and the device manager)
set LIBLINK=%LIBLINK% advapi32.lib cfgmgr32.lib
set LIBINC=%LIBINC% /I"%OPTIX_ROOT_DIR%\include" /Isrc\render\optix /Iextern\cuew\include
echo [blendity] With NVIDIA OptiX: its headers from %OPTIX_ROOT_DIR%, the kernel from src\render\optix
:optix_done
rem A build that asked for the fetch (CI, the release) must have OptiX: else it would ship without it, quietly.
if "%BLENDITY_FETCH_OPTIX%"=="1" if not "%USE_OPTIX%"=="1" if not "%BLENDITY_NO_OPTIX%"=="1" (
  echo [blendity] BLENDITY_FETCH_OPTIX=1 but OptiX isn't built in ^(no headers^): stopping
  exit /b 1
)

rem The build's identity (task 0079): VERSION's number, and CI's release tag (BLENDITY_BUILD) or "dev".
set /p BL_VERSION_NUMBER=<VERSION
if "%BLENDITY_BUILD%"=="" set BLENDITY_BUILD=dev
set LIBDEFS=%LIBDEFS% /DBL_VERSION_NUMBER=%BL_VERSION_NUMBER% /DBL_BUILD=%BLENDITY_BUILD%
set COMMON=/nologo /Iextern\sky\include /std:c++17 /EHsc /W3 /MP /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /permissive- /Zc:__cplusplus %LIBDEFS% %LIBINC%
rem Blender's libraries use the DLL runtime (/MD); the dependency-free build
rem stays a single static executable (/MT).
set CRT=/MT
if "%USE_LIBS%"=="1" set CRT=/MD
if /i "%CONFIG%"=="debug" (
  if "%USE_LIBS%"=="1" (set FLAGS=%COMMON% /Od /Zi /MD /DBL_DEBUG) else (set FLAGS=%COMMON% /Od /Zi /MTd /DBL_DEBUG)
) else (
  set FLAGS=%COMMON% /O2 /Oi /GL /fp:fast %CRT% /DNDEBUG
)
if "%USE_LIBS%"=="1" (echo [blendity] Using Blender libraries from %BLENDITY_LIBDIR%:%LIBDIRS%) else if "%BLENDITY_NO_LIBS%"=="1" (echo [blendity] Dependency-free build ^(BLENDITY_NO_LIBS^)) else (echo [blendity] Dependency-free build ^(no blender\lib\windows_x64: bash packaging\fetch_blender_libs.sh windows_x64 fetches them^))
set OBJ=build\obj_%CONFIG%
rem The sources in a response file: listed on the command line they passed cmd's 8191-character limit.
set SRCRSP=%OBJ%\sources.rsp
> "%SRCRSP%" (
  for %%d in (core image render scene texture editor research platform deps) do (
    for %%f in (src\%%d\*.cpp) do echo %%f
  )
  rem Blender's own bundled dependencies (see extern\README.md)
  echo extern\ufbx\ufbx.c
  echo extern\sky\source\sky_hosek.cpp
  if "%USE_OPTIX%"=="1" echo extern\cuew\src\cuew.c
)
set LIBSRC=@%SRCRSP%
set LINKFLAGS=/link /LTCG /INCREMENTAL:NO user32.lib gdi32.lib shell32.lib comdlg32.lib winhttp.lib ws2_32.lib %LIBLINK%
if "%BLENDITY_NO_LTCG%"=="1" set FLAGS=!FLAGS:/GL=!
if "%BLENDITY_NO_LTCG%"=="1" set LINKFLAGS=!LINKFLAGS:/LTCG=!
if /i "%CONFIG%"=="debug" set LINKFLAGS=/link /DEBUG user32.lib gdi32.lib shell32.lib comdlg32.lib winhttp.lib ws2_32.lib %LIBLINK%
if "%USE_LIBS%"=="1" call :deploy_dlls

if /i "%TARGET%"=="all" goto app
if /i "%TARGET%"=="app" goto app
if /i "%TARGET%"=="installer" goto app
if /i "%TARGET%"=="stress" goto stress
if /i "%TARGET%"=="tests" goto tests
if /i "%TARGET%"=="check" goto check

:app
echo [blendity] Building editor (%CONFIG%)...
rem BLENDITY_OUT lets you build while a copy of the editor is still running.
if "%BLENDITY_OUT%"=="" set BLENDITY_OUT=dist\windows\Blendity.exe
cl %FLAGS% /Fo%OBJ%\ /Fe:%BLENDITY_OUT% %LIBSRC% src\app\main.cpp %LINKFLAGS% /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
if errorlevel 1 exit /b 1
if /i "%TARGET%"=="installer" goto installer
if /i not "%TARGET%"=="all" goto done

:stress
echo [blendity] Building stress tests (%CONFIG%)...
cl %FLAGS% /Fo%OBJ%\ /Fe:dist\windows\blendity_stress.exe %LIBSRC% stress\stress_main.cpp %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
if /i not "%TARGET%"=="all" goto done

:tests
echo [blendity] Building unit tests (%CONFIG%)...
cl %FLAGS% /Fo%OBJ%\ /Fe:dist\windows\blendity_tests.exe %LIBSRC% tests\*.cpp %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
goto done

:check
echo [blendity] Building unit and stress tests (%CONFIG%, one compile)...
cl /c %FLAGS% /Fo%OBJ%\ %LIBSRC% stress\stress_main.cpp tests\*.cpp
if errorlevel 1 exit /b 1
rem The objects in response files, as the sources are (the command line has an 8191-character limit).
set TESTRSP=%OBJ%\tests_objects.rsp
set STRESSRSP=%OBJ%\stress_objects.rsp
> "%TESTRSP%" (
  for /f "usebackq delims=" %%f in ("%SRCRSP%") do echo %OBJ%\%%~nf.obj
  for %%f in (tests\*.cpp) do echo %OBJ%\%%~nf.obj
)
> "%STRESSRSP%" (
  for /f "usebackq delims=" %%f in ("%SRCRSP%") do echo %OBJ%\%%~nf.obj
  echo %OBJ%\stress_main.obj
)
cl %FLAGS% /Fe:dist\windows\blendity_tests.exe @%TESTRSP% %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
cl %FLAGS% /Fe:dist\windows\blendity_stress.exe @%STRESSRSP% %LINKFLAGS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
goto done

:installer
rem Inno Setup 6's compiler, then the setup .exe with the app, its DLLs, data, manual and licences.
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo [blendity] No Inno Setup 6 ^(jrsoftware.org/isinfo.php^): dist\windows holds the app, unpackaged.
  exit /b 1
)
set /p VERSION=<VERSION
python packaging\make_icon.py build\icons >nul || py packaging\make_icon.py build\icons >nul
"%ISCC%" /Q /DAppVersion=%VERSION% /DIcons=..\..\build\icons packaging\windows\blendity.iss
if errorlevel 1 exit /b 1
echo [blendity] Installer: dist\installers\Blendity-Setup-%VERSION%.exe
goto done

:done
echo [blendity] Done. Binaries are in dist\windows\
endlocal
exit /b 0

rem call :lib <folder> <define> "<include dirs>" "<link libs>"  (paths relative to the lib dir)
:lib
if not exist "%L%\%~1" exit /b 0
set LIBDEFS=!LIBDEFS! /D%~2
for %%i in (%~3) do set LIBINC=!LIBINC! /I"%L%\%%i"
for %%i in (%~4) do set LIBLINK=!LIBLINK! "%L%\%%i"
set LIBDIRS=!LIBDIRS! %~1
exit /b 0

rem Copy the release DLLs (no debug, SYCL-device or Python files) and the MSVC
rem runtime next to the executables, so dist\windows runs on any PC. OpenImageDenoise's
rem CUDA and HIP modules go too (task 0094), but never a Git LFS pointer left by a
rem clone that didn't fetch them (a few hundred bytes): every DLL must be over 4 KB.
:deploy_dlls
rem openjph (OpenEXR), dpcpp's SYCL runtime (Embree) and aom (FFmpeg's AV1) are runtime-only dependencies.
for %%d in (%LIBDIRS% imath openjph dpcpp aom) do (
  if exist "%L%\%%d\bin" (
    for %%f in ("%L%\%%d\bin\*.dll") do (
      echo %%~nxf| findstr /i /r "_d\. _d_ [0-9]d\.dll loaderd\.dll zerod\.dll v2d\.dll debug _sycl eigen_ malloc_proxy" >nul || if %%~zf GTR 4096 copy /y "%%f" dist\windows\ >nul
    )
  )
)
rem FFmpeg keeps its DLLs in lib\, beside the import libraries (not ffmpeg.exe: Blendity encodes in-process).
if exist "%L%\ffmpeg\lib" for %%n in (avcodec avformat avutil swscale swresample) do copy /y "%L%\ffmpeg\lib\%%n-*.dll" dist\windows\ >nul
rem Blender's colour management config (OpenColorIO views: AgX, Filmic, ...).
set "OCIO_CFG=%~dp0..\blender\release\datafiles\colormanagement"
if exist "%L%\opencolorio" if exist "%OCIO_CFG%\config.ocio" xcopy /e /i /y /q "%OCIO_CFG%" dist\windows\datafiles\colormanagement >nul
if defined VCToolsRedistDir (
  for /d %%c in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%c\*.dll" dist\windows\ >nul
)
exit /b 0
