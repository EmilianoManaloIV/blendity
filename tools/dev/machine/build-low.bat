@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem A copy of a helper from the development machine's scratch folder (C:\blab), kept so it is not lost
rem (docs/handoff/MACHINE.md). Every path in it (C:\blab, C:\Program Files\Git) is THAT machine's: change them
rem before use elsewhere. Nothing here is built, shipped or run by CI.
rem
rem The hub's shared Windows build launcher (2026-11-16): below-normal priority, no quoting from Git Bash needed, and
rem (with a 4th argument) the build slot taken and dropped HERE, inside the same cmd that runs the build - so an agent
rem shell that is stopped mid-build can no longer leave the slot held (the cmd and its build keep running and drop it).
rem Usage from Git Bash:  cmd //c "C:\blab\build-low.bat C:\blwtNNN release tests <who>"
rem   %1 = the worktree folder, %2 = release|debug, %3 = all|app|tests|stress, %4 = the lock owner (optional: without it
rem   the caller takes and drops the slot itself). The log goes to <worktree>\hub_build.log.
rem   Set BLENDITY_LIBDIR (or BLENDITY_NO_LIBS=1) before calling, as for build.bat: a worktree outside blender-main does
rem   NOT find ..\blender\lib by itself and silently builds without the libraries.
if "%~1"=="" (echo usage: build-low.bat ^<worktree^> ^<release^|debug^> ^<all^|app^|tests^|stress^> [owner] & exit /b 2)
set "BL_BASH=C:\Program Files\Git\bin\bash.exe"
rem (Git Bash cannot pass a variable whose name has parentheses, so build.bat's vswhere lookup lost ProgramFiles(x86).)
if not defined ProgramFiles(x86) set "ProgramFiles(x86)=C:\Program Files (x86)"
if not "%~4"=="" "%BL_BASH%" /c/blab/win-lock.sh take %4
cd /d "%~1" || goto :drop
start "" /belownormal /b /wait cmd /c ""%~1\build.bat" %2 %3 > "%~1\hub_build.log" 2>&1"
set "BL_RC=%errorlevel%"
:drop
if not "%~4"=="" "%BL_BASH%" /c/blab/win-lock.sh drop %4
exit /b %BL_RC%
