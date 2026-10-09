# Requirements

What Blendity must do and the constraints it works under. Task specs (`docs/tasks/`) and decision records
(`docs/decisions/`) cite these IDs. Changing a requirement is a human decision; record why in an ADR.

## Product

| ID | Requirement |
|---|---|
| R-01 | A Unity-style editor: Hierarchy, Inspector, Scene and Game views, Project and Console windows, Play mode, docking, Unity's shortcuts as the default keymap. |
| R-02 | Blender-equivalent modeling in Edit Mode (vertex / edge / face): extrude, inset, bevel (with profile), loop cut, knife, bridge, merge, dissolve, fill, spin, slice, modifiers, and the rest of the Edit Mode tool table. |
| R-03 | SketchUp / UModeler / Plasticity-style direct modeling: drawing shapes on faces and in space, Push/Pull with SketchUp semantics, Follow, guides and inference snaps, hard-surface tools (grid, pipe, array, taper, recess). |
| R-04 | UV mapping (seams, unwrap, UV editor with gizmo), materials and material assets, textures. |
| R-05 | Rendering: rasterized viewport (deferred PBR), path tracer, GPU devices where available, physical cameras, lights, colour management. |
| R-06 | File IO matching Blender 5.2: OBJ, FBX (binary / ASCII), glTF (glb / gltf), STL, PLY, USDA import and export, verified by opening the files in Blender. |
| R-07 | Every tool reachable from the menus, the Inspector / Modeling Tools window, a rebindable shortcut (Unity, Blender, Maya, 3ds Max and SketchUp presets) and the console. |
| R-08 | Adjustable operations: a tool's numbers live in its F9 panel or drag helper, re-run from the mesh as it was, kept for the tool's next use. |
| R-09 | A Learn tab citing the reference books in "Blender Documents". |
| R-10 | Camera filters: components on a camera that change how it renders wherever its image shows (Game view, Camera Preview, F12, sequences, optionally the Scene view), starting with old consoles' 3D looks (PS1, N64, Saturn, DOS). |

## Quality

| ID | Requirement |
|---|---|
| Q-01 | Robust geometry: odd inputs (degenerate faces, non-manifold edges, open surfaces, curved and non-planar regions, extreme scales, NaN) leave a structurally valid mesh or are refused cleanly. Closed meshes stay closed. |
| Q-02 | No new faces lying on top of each other (z-fighting) from modeling tools; the stress suite tracks the count and it must not rise unexplained. |
| Q-03 | Every behaviour has a unit test; the suite passes on Windows and Linux (and builds on macOS) before merging. |
| Q-04 | The editor stays interactive: a frame within the budget in `docs/PERFORMANCE.md` for ordinary scenes; heavy work is off the per-frame path. |
| Q-05 | Undo / redo covers every edit, as one step per operation. |

## Constraints

| ID | Constraint |
|---|---|
| C-01 | Plain C++17 built by `build.bat` / `build.sh` (CMake optional). Windows, Linux and macOS. |
| C-02 | The dependency-free build always works. Blender's own prebuilt libraries are allowed only behind `BL_WITH_*` switches with a fallback; nothing outside what Blender bundles (ADR 0001). |
| C-03 | Source GPL-2.0-or-later (SPDX header in every file), binaries GPL-3.0-or-later; third-party notices in `licenses/` (ADR 0002). |
| C-04 | Tooling and tests never touch the user's project files (`Assets/`, `Renders/`, `Screenshots/`); they use a scratch project and trash. |
| C-05 | Changes reach `main` only through a reviewed pull request with passing CI, merged by the human (ADR 0006). |
