# Pi 5 (8 GB) editing acceptance

Run this protocol on a physical Pi 5 with an X11 or Xwayland desktop. Pi results are pending. Record power supply, cooling, storage, display server, resolution, UI scale, commit and compiler. Use Bash in a disposable checkout. If a native release executable is needed and the compiler and X11 development headers are already installed, build it outside all measurements with `BLENDITY_NO_LIBS=1 BLENDITY_NO_OPTIX=1 bash build.sh release app`.

## Isolate the project and collect machine identity

```bash
set -eu -o pipefail
repo=$(pwd -P)
exe="$repo/dist/linux/Blendity"
test -x "$exe"
scratch=$(mktemp -d)
echo "Results in $scratch"
export BLENDITY_PROJECT="$scratch/project" BLENDITY_TRASH="$scratch/trash"
export BLENDITY_PREFS_DIR="$scratch/prefs" BLENDITY_CACHE_DIR="$scratch/cache"
export XDG_CACHE_HOME="$scratch/xdg-cache"
mkdir -p "$BLENDITY_PROJECT/Assets/Scenes" "$BLENDITY_PROJECT/Assets/Textures" \
  "$BLENDITY_PREFS_DIR" "$BLENDITY_CACHE_DIR" "$BLENDITY_TRASH" "$scratch/home" "$XDG_CACHE_HOME"
printf 'update_check=0\n' > "$BLENDITY_PREFS_DIR/prefs.txt"
auth=${XAUTHORITY:-"$HOME/.Xauthority"}
cd "$scratch"
{
  git -C "$repo" rev-parse HEAD
  sha256sum "$exe"
  file "$exe"
  uname -a
  cat /etc/os-release
  tr -d '\0' < /proc/device-tree/model && echo
  lscpu
  cat /proc/meminfo
  command -v vcgencmd >/dev/null && vcgencmd get_throttled
  command -v vulkaninfo >/dev/null && vulkaninfo --summary
  true
} > machine.txt 2>&1
if command -v vulkaninfo >/dev/null; then
  if ! vulkaninfo --show-formats > vulkan-formats.txt 2> vulkan-formats.err; then
    vulkaninfo > vulkan-full.txt 2>&1 || echo 'Enumeration failed' >> vulkan-full.txt
  fi
fi
run() {
  local name=$1
  shift
  /usr/bin/time -v -o "$name.time" env HOME="$scratch/home" XAUTHORITY="$auth" \
    "$exe" --no-libs "$@" >"$name.log" 2>&1
}
```

## Launch, select, edit, undo, open a texture, save and reopen

```bash
run launch-first --startup-check
run launch-repeat --startup-check
img="$BLENDITY_PROJECT/Assets/Textures/reference.png"
scene="$BLENDITY_PROJECT/Assets/Scenes/pi.scene"
# Generate a recognizable 1024-square image outside the measured editing process.
run fixture --headless-screenshot "$img" --size 1024x1024 --cmd "viewport device cpu"
run edit-720 --headless-screenshot "$scratch/edited.png" --size 1280x720 \
  --cmd "viewport device cpu" --cmd "quality low" --cmd "stress 64 Sphere" --cmd "perf frames 1" \
  --cmd "viewport status" --cmd "perf memory" --cmd "perf reset" --cmd "select Cube" \
  --cmd "edit face" --cmd "fsel 0" --cmd "perf frames 1" --cmd "perf mark select" --cmd "stats" --cmd "perf reset" \
  --cmd "editop extrude" --cmd "perf frames 1" --cmd "perf mark extrude" --cmd "stats" --cmd "perf reset" \
  --cmd "undo" --cmd "perf frames 1" --cmd "perf mark undo" --cmd "stats" --cmd "edit off" \
  --cmd "perf reset" --cmd "refimage $img front" --cmd "perf frames 1" \
  --cmd "perf wait" --cmd "perf mark texture" --cmd "stats" --cmd "perf reset" --cmd "save $scene" --cmd "perf mark save" --cmd "perf memory"
test -s "$scene"
run reopen-720 --headless-screenshot "$scratch/reopened.png" --size 1280x720 \
  --cmd "viewport device cpu" --cmd "quality low" --cmd "perf reset" --cmd "open $scene" \
  --cmd "perf frames 1" --cmd "perf wait" --cmd "perf mark reopen" --cmd "perf memory" --cmd "stats" \
  --cmd "viewport status" --cmd "perf drag at 600 340 780 340 mods alt frames 60"
grep -Ei 'Spawned|perf mark|perf memory:|perf drag at:|Views:|CPU badge:|warning|error' edit-720.log reopen-720.log

# Compare the five stats replies: selected, extruded, undone, image added, reopened.
python3 -c '
import re, sys
from pathlib import Path
pattern = r"(\d+) objects, (\d+) verts, (\d+) faces, scene memory"
def counts(path):
    return [tuple(map(int, row)) for row in re.findall(pattern, Path(path).read_text(errors="replace"))]
edited, reopened = counts("edit-720.log"), counts("reopen-720.log")
if len(edited) != 4 or len(reopened) != 1:
    sys.exit(f"Missing or extra topology replies: {edited}, {reopened}")
o, v, f = edited[0]
plane = (o+1, v+4, f+1)
expected = [(o,v,f), (o,v+4,f+4), (o,v,f), plane, plane]
actual = edited + reopened
if actual != expected:
    sys.exit(f"Topology mismatch (objects, vertices, faces): {actual} vs {expected}")
print("Verified extrusion, undo, image plane and reopen:", actual)
'
```

The named Cube belongs to the starter scene. The generated 64 spheres share a mesh. The reference plane loads the PNG from disk. Verify the grid, textured plane and six-face cube after reopen. Inspect logs for refused commands or missing assets even after exit code zero. The startup check initializes a real window and draws three frames. The first process has fresh application caches; the kernel's file caches may be warm.

Record command-to-completion time from each mark as well as the longest measured frame. Synchronous work between frames can make the total larger than any recorded frame. The drag log separates first move, subsequent mean, longest and release. Repeat optionally at 1920x1080 with drag coordinates inside Scene.

## Real window and physical input

```bash
run orbit-window --scene "$scene" --windowed-check "$scratch/orbit.png" --frames 120 \
  --command "viewport device cpu" --command "quality low" --command "viewport status" \
  --command "perf drag at 680 380 830 380 mods alt frames 60 loop"
grep -Ei 'windowed check:|presented by|perf drag at:|warning|error' orbit-window.log
run manual --scene "$scene" --hci-log "$scratch/manual.hci"
```

The real window requests 1600x900. Its windowed-check line reports the actual dimensions. Only the headless commands above force 1280x720. In the manual run resize the window to a 1280x720 client area and verify the dimensions (select the window with `xwininfo` if available). Keep the default Unity keymap. Enter `quality low`, `viewport device cpu` and `viewport status` in the console. With the pointer over Scene, Alt-left-drag horizontally ten times and middle-drag horizontally ten times. Verify that orbit rotates the viewpoint and pan shifts the framing. Before each series issue `perf reset`, then `perf mark orbit-manual` or `perf mark pan-manual` after it. Record longest frame and missed input. Elapsed manual mark totals include human input delays.

Click Cube in Hierarchy, select a face in Edit Mode, extrude and undo. Check that one undo restores the six faces. Reopen the saved scene and verify the image. Quit to flush the GNU time report. For a Vulkan-enabled native executable, repeat with that executable in fresh scratch and request `viewport device gpu`. Capture the actual view backend and fallback reason from `viewport status`, selected physical device, driver and windowed-check logs. The windowed-check header reports whether the card presents the window at that point. The card/system timing row labels echo the requested preferences. Record the actual views-per-frame counter and CPU fallback reason for each row. Mark the presentation route during a row unknown if the logs leave it unresolved. Match the selected device name to its physical-device block in the Vulkan dump. Record vendorID, deviceID, deviceUUID when available, driver, features, limits and format properties. Record unavailable format evidence if the fallback dump omits it, and leave ambiguous device matches unresolved. Label software Vulkan and CPU fallback separately.

## Report and budgets

Retain three runs per condition, raw logs and output pictures. Report median elapsed time, worst individual frame and every maximum RSS (GNU time, KiB). A process peak spans its entire launch; the internal `perf memory` rows break down tracked editor data at the indicated checkpoints; one edit-720 peak covers all commands in that process. Record system MemAvailable before/after, swap-in/out with `vmstat 1` in a separate observation run, and any undervoltage or throttling before/after. If heaptrack is already present, rerun the editing command under it outside the timing series and record allocation calls, temporary allocations and peak heap bytes. Mark unavailable metrics and preserve the difference between process RSS, heap bytes and GPU shared memory.

| Machine, architecture, build, backend, client size | Journey | First / repeat elapsed ms | First / mean / worst frame ms | Process peak RSS KiB | Swap / throttle | Correct output or failure |
|---|---|---:|---:|---:|---|---|
| Pi 5, 8 GB, awaiting physical run | Launch, orbit, pan, select, extrude, undo, texture, save, reopen (one row each) | | | | | |

Q-06 / ADR 0105 allow up to 50 ms per frame while jobs run and one frame up to 100 ms for an indivisible operation. Use 16.7 ms as the 60 Hz comparison for steady orbit/pan; Q-07 defines its own posing workload. Pi acceptance requires correct edits and reopen, responsive input, an identified backend and measured frame and memory rows from the board.
