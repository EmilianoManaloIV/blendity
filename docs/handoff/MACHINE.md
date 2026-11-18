# The development machine

What Blendity was built on, and the rules that machine imposes. A Codex cloud task does not need this file; a
task that runs on the owner's PC does. Paths are that PC's.

## Hardware and systems
- Windows 11, 32 logical cores, 32 GB of memory.
- Two NVIDIA cards: an RTX 4070 SUPER (12 GB; the card the views, the `gpu` tests and the timed rows use) and an
  RTX 3060 Ti (8 GB). Driver 610.88 at the time of writing. The owner's driver and power settings are never
  changed by an agent.
- WSL 2 with Ubuntu 26.04 (g++ 15.2), which sees all 32 cores but **15 GB** of memory.
- macOS is built and tested only on CI (Apple-chip runners).

## Toolchains
| What | Where / how |
|---|---|
| MSVC | Visual Studio's C++ tools, found by `build.bat` through `vswhere`. No CMake needed (`build.bat`, `build.sh`). |
| Git Bash | `C:\Program Files\Git\bin\bash.exe`; the shell the scripts assume. |
| GitHub CLI | `"/c/Program Files/GitHub CLI/gh.exe"` from Git Bash (not on its PATH); on PowerShell's PATH. |
| Python | `py` (3.14). The bare `python` name is the Microsoft Store stub and fails. Never `py -` (`PITFALLS.md`). WSL has `python3`. |
| GCC | In WSL: `wsl -e bash -lc "cd /mnt/c/... && taskset -c 0-9 ./build.sh release tests"`. |
| Blender | 5.2 LTS under `C:\Program Files\Blender Foundation\Blender 5.2\`, run headless (`blender.exe -b --factory-startup --python script.py -- args`) to check Blendity's exported files and make reference files (`BLENDITY_EXPORT_DIR`, `BLENDITY_BLENDER_DIR` in the tests). |
| Unity | 2022.3 (path in `tools/dev/README.md`, `BL_UNITY`), for the bake comparisons and the bridges. |
| CUDA Toolkit 12.8, OptiX SDK 9.1.0 | Only to *rebuild* the OptiX kernel. See "OptiX" below. |

## Blender's libraries: `../blender/lib/<platform>`
- The git root on this PC is `blender-main/`, holding `blendity/` and, beside it, a Blender checkout `blender/`
  whose `lib/` has Blender's prebuilt libraries: `windows_x64`, `linux_x64`, `macos_arm64`, `windows_arm64`.
  `build.bat` / `build.sh` look in `../blender/lib/<platform>` and switch each library on (`BL_WITH_*`) when it
  is there: TBB, Embree, OpenImageDenoise, OpenSubdiv, OpenEXR / Imath, OpenColorIO, Manifold, meshoptimizer,
  OpenPGL, Jolt, jpeg, png, zlib, zstd, Vulkan and shaderc, OpenXR, ffmpeg, and Intel's `dpcpp` runtime (fetched
  on 2026-11-15 with a sparse checkout; `build.bat` deploys its DLLs).
- `BLENDITY_LIBDIR` points the build at them from anywhere; **a worktree outside `blender-main/` does not find
  them by itself and silently builds dependency-free**, so set it:
  `BLENDITY_LIBDIR='C:\Users\trick\Downloads\blender-main\blender\lib\windows_x64'`.
- `BLENDITY_NO_LIBS=1` forces the dependency-free build (what CI builds). Both builds must pass a round's tests.
- `packaging/fetch_blender_libs.sh` fetches the libraries on a new machine; the `libraries` release on GitHub
  holds the archives the installers are built from.

## OptiX
- The OptiX kernel's compiled PTX is committed (`src/render/optix/optix_kernel_ptx.h`, written by
  `tools/optix_ptx_embed.bat`), so an ordinary build needs only OptiX's host headers: `OPTIX_ROOT_DIR`, or the
  newest `OptiX SDK *` folder under `C:\ProgramData\NVIDIA Corporation\` (9.1.0 here), or, on CI with
  `BLENDITY_FETCH_OPTIX=1`, NVIDIA's public headers at the tag `build.bat` and `build.sh` pin (keep the two and
  the committed PTX's version together). `BLENDITY_NO_OPTIX=1` leaves it out. At run time only the driver is
  needed.
- Rebuilding the kernel needs the CUDA Toolkit (12.8, installed without its driver component, `CUDA_PATH`) and
  the OptiX SDK (a free NVIDIA developer account; the owner installs it - an agent cannot sign in for them).
- Nothing of NVIDIA's is committed; builds without the SDK keep working (ADR 0054).

## WSL limits
- 15 GB: **one Linux compile at a time**, under the WSL lock, on ten cores:
  `bash /c/blab/wsl-lock.sh take <who>; wsl -e bash -lc "cd /mnt/c/<tree> && taskset -c 0-9 ./build.sh release tests"; bash /c/blab/wsl-lock.sh drop <who>`.
  Tests need no lock.
- **No local sanitizer builds** (about 1.9 GB per test file; they took WSL down repeatedly). CI's six sanitizer
  shards judge: fix the named line, push, read CI.
- If WSL stops answering ("Catastrophic failure"), check it really is down (`wsl -e bash -lc "echo up"`), then
  `wsl --shutdown` - by the coordinating task only, after checking nothing of the owner's is attached; every
  Linux build and test in flight is lost.
- Never done, the owner's system: a larger limit in `.wslconfig`.

## Windows build slots and priority
- At most **two** Windows compiles run at a time across all tasks (2026-11-16, after an out-of-memory crash).
  Use the launcher, which takes a slot, builds at below-normal priority and drops the slot inside one `cmd`:
  `cmd //c "C:\blab\build-low.bat <worktree> release|debug all|app|tests|stress <owner>"`
  with `BLENDITY_LIBDIR` or `BLENDITY_NO_LIBS=1` exported first. Its log is `<worktree>\hub_build.log`
  (untracked; never commit it).
- One slot per task; a round's two builds go one after the other. Tests and runs need no slot.
- `bash /c/blab/win-lock.sh show` lists the holders and the queue. An owner named `hub-window*` (holding or queued)
  means a timed window is pending: start no test, stress or card run until it is gone.
- Builds, tests and stress start at below-normal priority so the owner's own use of the PC stays smooth; timed
  runs are the exception (normal priority, inside a window).

## The graphics card and timed windows
- Card work is one task at a time, granted by the coordinating task. Sub-tasks never touch the card without it.
- Timed runs go through `bash /c/blab/timed-window.sh <log-name> <script>...` (see `METHOD.md`): each task prepares a folder
  `C:\blab\<who>-timed-<round>\` with before/after exe folders - each with its DLLs **and**
  `dist/windows/shaders/` - and one script; the coordinator runs the window and sends back the numbers.
- Sample `nvidia-smi --query-gpu=pstate,clocks.gr,utilization.gpu -lms 250` beside card measurements.

## Folders
| Folder | What it is |
|---|---|
| `C:\Users\trick\Downloads\blender-main\` | The git root (branch `blendity`): `blendity/` and `blender/`. `blendity\dist\windows\Blendity.exe` here is **the owner's exe**, often running. `blendity\Assets\` is the owner's project: never edited, never committed. |
| `C:\blwt<round>` | One git worktree per round or task (`git -C <git root> worktree add C:/blwtNNN <commit> -b wt/NNNN-name`), made from a published branch, so `blendity/`'s content is at its root. Suffixes: `m` a merge, `f` a fold or fix, `b`/`c` a second tree, `lib` a libraries copy. |
| `C:\blwtexe` | The scratch worktree the owner's exe is built in (`METHOD.md`). |
| `C:\blab` | Shared scratch: the lock folders (`win-build-slot1`, `win-build-slot2`, `win-build-queue`, `wsl-build.lock`), the four helpers, build and window logs, the timed folders, the board's tooling. Not a repository. |
| `C:\blcmp<round>` | Before/after builds and their outputs for a round's comparison (pictures, timings). |
| `C:\blsuites*`, `C:\bltest*`, `C:\blperf*`, `C:\blcache*` | Older rounds' test directories, suites' logs, benchmark outputs and caches. |
| `%TEMP%\blendity_tests` | The tests' default scratch (a folder per run since round 550). |
| `~/.blendity/` | The owner's real preferences and caches. Tests must not read or write it (`BLENDITY_PREFS_DIR`). |

Most `C:\blwt*` and `C:\blcmp*` folders belong to merged rounds and can go; removing them is the owner's call
(`git worktree remove`, then `git worktree prune`; the local `wt/*` branches after them).

## The helper scripts (`tools/dev/machine/`)
Copies of the four files in `C:\blab`, with a header added; the running copies stay in `C:\blab`, and the paths
inside them (`/c/blab/...`) are that PC's.
| Script | What it does |
|---|---|
| `win-lock.sh take\|drop\|show <who>` | The two Windows build slots: a first-come queue, one slot per owner; a slot older than 90 minutes, or 10 minutes old with no `cl.exe` / `link.exe` anywhere, is reclaimed by the next taker. |
| `wsl-lock.sh take\|drop\|show <who>` | The single Linux compile lock; reclaimed when older than 2 hours, or 10 minutes old with nothing compiling or testing in WSL. |
| `build-low.bat <worktree> <release\|debug> <target> [owner]` | The Windows build launcher: below-normal priority, slot taken and dropped inside the same `cmd`, log to `<worktree>\hub_build.log`. Sets `ProgramFiles(x86)`, which Git Bash cannot pass. |
| `timed-window.sh <log-name> <script>...` | The locked timed window: holds both build slots and the WSL lock, waits for a quiet machine, runs the scripts, logs `TAINT` lines for any build or test process seen, drops the locks. Refuses to run unless both slots are held. |

The repository's own scripts for a round (`chain.sh`, `publish.sh`, `stack_check.sh`, `changelog.py`, ...) are
in `tools/dev/` and described in `tools/dev/README.md`; their machine paths are settings at the top
(`BL_W`, `BL_MAIN`, `BL_WSL_TREE`, ...).

## What an agent may not do here
- Kill the owner's `Blendity.exe`, or any process that is not its own.
- Touch `Assets/`, `Renders/`, `Screenshots/`, `research/`, or `~/.blendity/`.
- Change driver, power or system settings (`.wslconfig` included); download or install software unasked.
- Delete worktrees, scratch folders or branches it did not make, without the owner's word.
