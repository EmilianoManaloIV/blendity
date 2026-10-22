@echo off
rem Pass 13's table (task 0178): Blendity's stress rows and Blender 5.2's, measured on this machine, into
rem docs\PERFORMANCE.md between its pass13-table markers. Run on a quiet machine after `build.bat release stress`.
rem Usage: tools\bench.bat [--quick]   (BLENDER=path overrides where Blender is; BENCH_GPU=name part picks the card;
rem a --quick run prints the table and leaves the doc alone)
setlocal
cd /d "%~dp0.."
if "%BLENDER%"=="" set "BLENDER=C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
if "%BENCH_GPU%"=="" set "BENCH_GPU=4070"
set "OUT=build\bench"
if not exist "%OUT%" mkdir "%OUT%"
set "QUICK=%~1"
for %%s in (editor_render gpu uv bake serialization) do (
  echo [bench] stress %%s
  dist\windows\blendity_stress.exe --only %%s %QUICK% --out "%OUT%\%%s" > "%OUT%\%%s.log" 2>&1
  if errorlevel 1 echo [bench] stress %%s failed: see %OUT%\%%s.log & exit /b 1
)
echo [bench] Blender
"%BLENDER%" -b --python tools\bench_blender.py -- --out "%OUT%\blender.json" --gpu "%BENCH_GPU%" %QUICK% > "%OUT%\blender.log" 2>&1
if errorlevel 1 echo [bench] Blender failed: see %OUT%\blender.log & exit /b 1
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
if "%QUICK%"=="" (
  %PY% tools\bench_table.py --stress "%OUT%" --blender "%OUT%\blender.json" --gpu "%BENCH_GPU%" --write docs\PERFORMANCE.md
) else (
  %PY% tools\bench_table.py --stress "%OUT%" --blender "%OUT%\blender.json" --gpu "%BENCH_GPU%"
)
endlocal
