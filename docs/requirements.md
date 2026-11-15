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
| R-11 | Global illumination like Unity's: baked lighting (Contribute GI, light modes Realtime / Mixed / Baked, generated lightmap UVs, Generate Lighting into lightmaps) and probe volumes (automatically placed light probes, as Unity 6's Adaptive Probe Volumes), kept current by realtime GI so lighting changes don't need a manual re-bake. |
| R-12 | Realtime global illumination from the research folder: voxel-based GI (Thiedemann et al., I3D 2011) for one bounce of light and sky occlusion in the rasterized views, feeding the lightmaps and probes of R-11. |
| R-13 | Animation with physics as in Cascadeur: the body weighed from its skin, its center of mass, contacts, ballistic flight and angular momentum, a full physics solve, spring bones, an active ragdoll and AutoPosing; corrections written as ordinary keys (tasks 0115-0123). |
| R-14 | Fingers and a pen on a touch screen (a 2-in-1): in Tablet Mode fingers navigate (one orbits, two pinch / pan / twist) and the pen selects and manipulates, with a pen quick menu for tools without a keyboard; Tablet Mode follows the laptop's tablet posture; the same model on Windows, Linux (XInput 2) and macOS (trackpad, pen tablets). |
| R-15 | Drawing in perspective with SketchBook-style rulers and guides (ruler, ellipse, French curves, symmetry, 1/2/3-point perspective) over the Scene view, pen strokes corrected toward what they meant (lines, arcs, ellipses, curves split at corners) and kept as scene data; the perspective guide is the scene camera, canvases are planes in the scene, and the model is built from the construction lines by the artist's own steps (boxes, cylinders, extrusions), never guessed. |

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

## Added in round 411 (ADR 0105)

| ID | Requirement |
|---|---|
| Q-06 | Responsiveness: while any job runs, no editor frame over 50 ms and the views keep their last picture; an operation that cannot be split takes one frame of at most 100 ms, else it runs on a worker listed in the job tray with progress and Cancel. Measured by `blendity_stress --only responsiveness` and `tools/perf_suite.py` against `stress/results/baseline.md`. (ADRs 0013, 0021, 0036, 0039, 0040, 0051 and 0052 cite "Q-01 (responsiveness)": they mean this requirement.) |

## Added in round 512 (Pass 25; ADRs 0085, 0145)

Usability, made checkable. Each row names what fails when it is broken. The live sweep is `tests/test_hci.cpp`'s
"the live sweep" test: the `hci` group runs it at 1366x768, Dark, UI scale 1 in every run, and `BLENDITY_SWEEP=full` runs
all 12 configurations (1920x1080, 1366x768, 1024x640 x Dark, Light x scale 1, 1.5; about 2,140 screens), written to JSON
for `py tools/hci_sweep.py <file> --check` against `tools/hci_sweep_baseline.json`. Its numbers: `docs/hci/sweep-505.md`
(before) and `sweep-509.md` (after).

| ID | Requirement |
|---|---|
| U-01 | Targets: a control the toolkit draws is at least 20 px on its smaller side at UI scale 1 (a row at mouse density) and 7 mm - 26 px on a 96 dpi screen - at UI scale 1.5, the scale Tablet Mode sets; between the two, 20 px scaled and at most 26 (`target_limit` in `tests/test_hci.cpp`). The sweep asserts zero `small` controls in its configuration and in every full-sweep configuration. It still prints how many targets are under 7 mm at each scale (the Fitts audit's ground, `tools/hci_fitts_audit.py`, `docs/hci/fitts-505.md`): the owner kept mouse density, so that count is reported and not judged. The X / Y / Z letters of a vector field are `small-handle` (baseline 3): the number field beside each is the full target. |
| U-02 | Contrast: every theme's text and dim text against its panel is at least 4.5:1 (WCAG 2.1 AA, `ui::contrast_ratio`), High Contrast included; the keyboard focus ring carries a one-pixel outline in the theme's text colour, because three themes' own focus colours are under 3:1. Checked by the sweep test's contrast block, "task 0261" and "the sweep's fixes (task 0293)" in the `editor` and `hci` groups, and the focus test of round 509. |
| U-03 | Tooltips: every button and icon button has a tooltip, and it ends a sentence (full stop, more than ten characters); a disabled control says why (as its tooltip, by its label). The sweep asserts zero `no-tooltip` and zero `disabled-no-reason`; the tests named "ends a sentence" in the `hci` group pin the buttons the survey found. A shortcut in a label comes from the keymap, never a string literal: `py tools/hci_label_audit.py --check` finds none. Text that sends the artist to the Console where a control does the job is held at `tools/hci_console_baseline.txt` by `py tools/hci_console_audit.py --check`. |
| U-04 | Keyboard reach: every control drawn through the toolkit is a keyboard stop - F6 / Shift+F6 enter the window under the pointer and then the next or previous window (`ui.focus_window`), F10 or Alt pressed and let go alone the menu bar (`ui.menu_bar`); Tab and Shift+Tab walk every control in drawing order; Space or Enter presses, ticks, opens or folds; Left / Right step a slider; the arrows walk menus and lists; Esc lets go; a bare Tab over a window stays Edit Mode (ADR 0145). The sweep asserts zero `no-key` (disabled menu rows, which the arrows step over, excepted) and zero `tab-order` steps backwards; a test of the `hci` group drives a button, a checkbox, a dropdown, a slider, a menu and a dialog by keys alone. Hand-drawn widgets are recorded but not yet stops: an open item, counted per configuration. |
| U-05 | No cut labels: a label is shown whole or the control's tooltip says it whole, and a grid wraps or drops columns before it cuts. The sweep asserts zero `elided` at 1366x768 x1 Dark; the full sweep's baseline is 7 (all at 1024x640 or x1.5 in panes of 120-180 px, each named in `docs/hci/sweep-509.md`, "What is left") and `hci_sweep.py --check` fails on one more. |
| U-06 | Nothing on anything: no control lies on another of its layer (`covered`, comparing what each control's clip lets through), no labelled buttons overlap (`overlap`), no popup leaves its window and no label is clipped by a neighbour (`clipped`). The sweep asserts zero in its configuration and the full sweep holds them at zero in all 12. |
| U-07 | A change to a window's layout, a new control or a new window runs the `hci` group (and the full sweep when it touches how controls are laid out, with its numbers in the CHANGELOG), and a new toolkit widget records itself (`record_control`) and calls `focus_stop`, so U-01 to U-06 see it. |

## Added in round 512: Blockout and posing

| ID | Requirement |
|---|---|
| B-01 | A Blockout's mesh is a function of its pieces and settings: the same bytes for the same pieces on one thread or many; a piece the kernel cannot use is skipped and named, a refusal leaves the caller's mesh untouched, and every build is one closed surface (ADR 0128). Checked by the `blockout` group (the kernel's sweep of hostile arrangements, both paths) and the stress section `blockout`. |
| B-02 | A Blockout follows its pieces live and never builds in a window's frame: a drag, a handle, a style or number change, a snap or a join lands as one mesh from a worker, one build behind the mouse, and undo returns the pieces (ADR 0129). A counter pins that no kernel call is made in a frame; `blockout wait` / `blockout_jobs_wait` is how a save, an export, a bake and Play get the newest mesh. |
| B-03 | Everything a Blockout can make is reachable without the Console and with it: shapes (box, cylinder, sphere, wedge, stairs, arch, outline, mesh, convex), rooms, doors and windows with styles, the nine architectural pieces, Draw Piece with its Shape list (Door and Window under "Openings", drawn as a rectangle on a wall), snap, join, Blend radius, materials by class and Bake to Mesh. Each is reached from the console (`blockout ...`) and the interface, and has a test in the `blockout` group named for what the user sees. |
| B-04 | Scale never shears a piece (it goes into the piece's numbers, ADR 0135); opening feet, walls and floors are one rule for the buttons and the drag (`blockout_opening_feet`, a wall leaning more than 2 degrees is refused with the angle). |
| B-05 | A door's leaf is its own object: Open Amount turns it with no Blockout build and the Blockout's mesh bytes do not change with it; a hand turn writes Open Amount back in the same undo step; Bake to Mesh and export keep the leaves (ADR 0136). A Blockout piece's modifier stack runs on the build's worker on the piece's own shape, keyed by numbers and modifiers and not by place, and a result that is not one closed solid is refused with the reason in the status (ADR 0138). |
| B-06 | A Blockout's interactive rows stay inside ADR 0105's budgets on the bench interiors (100 and 400 pieces): the stress section `blockout`'s `bench_rows` and `docs/PERFORMANCE.md`, "Pass 23". Rows over their budget are listed there with a reason; this requirement is the measurement, as Q-06 is. |
| Q-07 | Posing frame rate: dragging an IK target or its pole, G on it, orbiting and moving a prop in the world stay at or under 16.7 ms a frame on a window on the card with a 140,000-triangle avatar (round 475: 10-13 ms against 48-56 before); the pose that results is the one the solver gave before (goldens kept, ADR 0127). **Not yet met:** whole-body poses (a Puppet handle, the chest, a scrub, Play with the springs) are 35-53 ms on the stand-in; what is left is the moved meshes sent to the card, their normals on the CPU and the outline shells (skinning on the card is the owner's open decision). Measured by `blendity_stress --only posing` and `tools/bench_world.py --posing`; the unit tests pin the behaviour (a skin is deformed once and only when its bones moved, a posed mesh follows in place, the solver writes each joint once), never a time (ADR 0056). |
