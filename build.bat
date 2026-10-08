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
rem GPU rendering: the Vulkan headers (the loader comes with the GPU driver) and shaderc.
if exist "%L%\vulkan\include" call :lib shaderc BL_WITH_VULKAN "shaderc\include vulkan\include" "shaderc\lib\shaderc_shared.lib"
rem ABI settings the libraries were built with (blender\build_files\build_environment\cmake).
set LIBDEFS=%LIBDEFS% /DIMATH_DLL /DOPENEXR_DLL /DMANIFOLD_PAR=1 /DJPH_SHARED_LIBRARY /DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED /DJPH_DOUBLE_PRECISION /DJPH_CROSS_PLATFORM_DETERMINISTIC /DJPH_USE_CPU_COMPUTE /DJPH_OBJECT_STREAM /DJPH_USE_SSE4_1 /DJPH_USE_SSE4_2
:libs_done

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
if "%USE_LIBS%"=="1" (echo [blendity] Using Blender libraries from %BLENDITY_LIBDIR%:%LIBDIRS%) else (echo [blendity] Dependency-free build ^(no blender\lib\windows_x64^))
set LIBSRC=
for %%d in (core image render scene editor research platform deps) do (
  for %%f in (src\%%d\*.cpp) do set LIBSRC=!LIBSRC! %%f
)
rem Blender's own bundled dependencies (see extern\README.md)
set LIBSRC=!LIBSRC! extern\ufbx\ufbx.c extern\sky\source\sky_hosek.cpp
set OBJ=build\obj_%CONFIG%
set LINKFLAGS=/link /LTCG /INCREMENTAL:NO user32.lib gdi32.lib shell32.lib %LIBLINK%
if /i "%CONFIG%"=="debug" set LINKFLAGS=/link /DEBUG user32.lib gdi32.lib shell32.lib %LIBLINK%
if "%USE_LIBS%"=="1" call :deploy_dlls

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
exit /b 0

rem call :lib <folder> <define> "<include dirs>" "<link libs>"  (paths relative to the lib dir)
:lib
if not exist "%L%\%~1" exit /b 0
set LIBDEFS=!LIBDEFS! /D%~2
for %%i in (%~3) do set LIBINC=!LIBINC! /I"%L%\%%i"
for %%i in (%~4) do set LIBLINK=!LIBLINK! "%L%\%%i"
set LIBDIRS=!LIBDIRS! %~1
exit /b 0

rem Copy the release DLLs (no debug, GPU-device or Python files) and the MSVC
rem runtime next to the executables, so dist\windows runs on any PC.
:deploy_dlls
rem openjph (OpenEXR) and dpcpp's SYCL runtime (Embree) are runtime-only dependencies.
for %%d in (%LIBDIRS% imath openjph dpcpp) do (
  if exist "%L%\%%d\bin" (
    for %%f in ("%L%\%%d\bin\*.dll") do (
      echo %%~nxf| findstr /i /r "_d\. _d_ debug _cuda _hip _sycl eigen_ malloc_proxy" >nul || copy /y "%%f" dist\windows\ >nul
    )
  )
)
rem Blender's colour management config (OpenColorIO views: AgX, Filmic, ...).
set "OCIO_CFG=%~dp0..\blender\release\datafiles\colormanagement"
if exist "%L%\opencolorio" if exist "%OCIO_CFG%\config.ocio" xcopy /e /i /y /q "%OCIO_CFG%" dist\windows\datafiles\colormanagement >nul
if defined VCToolsRedistDir (
  for /d %%c in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%c\*.dll" dist\windows\ >nul
)
exit /b 0
