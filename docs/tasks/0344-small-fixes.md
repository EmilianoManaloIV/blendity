# Task 0344: the small fixes from round 350's productions

- **Status:** done (round 367, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - findings 7, 8, 12, 13 and 15 of
  docs/hci/compare-2026-10.md, and the new camera that faced +Z (the avatar's renders).

## What
- **create:**
  - **Capsule** (`primitives::capsule`, Unity's: 2 m tall, 1 m across) in the menus, the Tools window, the palette and
    `create`.
  - **An unknown kind is refused** with the list of kinds (it made an empty object named for the typo).
  - **Kinds match in any case.**
  - **Names as Unity names them:** a second Sphere is "Sphere (1)", so selecting by name finds the one meant.
- **Join** keeps the result on the active object when it's a mesh, else on the last mesh selected (Select All could
  end on a light, and Join refused).
- **Walk Cycle** is on from the start (Preferences > Animation Assistants still switches it off; a saved preference
  keeps the user's choice).
- **A new Camera** is placed at the Scene view's eye with its rotation (its field of view stays the default; Align
  Camera to View also matches that).
- **The console:**
  - `help` lists every command, then the examples it had;
  - `save [path]`, `open <path>` and `new` (asking about unsaved changes, as the menus do);
  - `rotate <x y z>`, `scale <s | x y z>` and `set Transform.Position|Rotation|Scale <x y z>`, on the selection's own
    transform;
  - `saverender [path]` writes there without asking, the type by its extension;
  - `ui scale <s>` is the UI's scale, which `scale` was.
  The verbs run before the long command chain (MSVC's nesting limit).

## Checked
- **`tests_round367`:**
  - the capsule 2 m tall and 1 m across, a second one "Capsule (1)", "Capsul" refused with nothing made, "point
    light" made;
  - Join after a light: the Sphere kept the Cube's 6 faces;
  - the new camera at the view's eye, facing its way;
  - help listing every command, rotate / scale / set Transform.*, save to a path, new, open it back, saverender to a
    path;
  - the Try-it.
- **Tests that named the newest Cube or Sphere** now take the created name, and the assistants' and Walk Cycle's tests
  expect Walk Cycle on.

## The Opus review's fixes (verdict: approve)
- **`save <path>` that couldn't be written** had already taken the path and name, so every later Ctrl+S failed too.
  It now refuses before changing anything, and leaves no empty file.
- **The Tools window** gets the Capsule too.
- **`create armature`, `lattice`, `curve: path`** in any case (only the primitives matched before).
- **`saverender <path>`:**
  - a folder gets the render's own name in it;
  - no extension gets the format's;
  - an extension it can't write (.bmp) is refused, instead of writing PNG bytes under that name.
- **A bare name** (`save Level1`, `open Level1`) means Assets/Scenes, where Save As puts it; `open` adds .scene as
  `save` does.
- **`scale 0`** is refused (a flat object's matrix has no inverse; Join turned it into NaNs).
- **A capsule as short as it's wide** has one rim, not two on top of each other.
- **Tests:** `ui scale`'s value, `create armature`, a failed save, a bare name, `saverender` without an extension and
  with .bmp.
- **Existing preferences:** a saved profile keeps Walk Cycle as the user left it (off before this round; Preferences >
  Animation Assistants switches it on). Users' own Wheel slices, panels or shortcuts holding the console line
  `scale 1.5` now scale the selection; nothing shipped uses it.

## Not done
- **Names with spaces** for `newtex`, `paper` and `board`: their second word is a preset or a subcommand, so the
  quoting rule needs its own round.
- **A capsule in Adjust Last Operation** (its radius and height after making it).
