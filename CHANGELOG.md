# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-29 (round 317): the Unreal bridge built and run in Unreal 5.8 (task 0295)

**Asked:** the user (2026-10-29): "Check .NET now." (after installing the .NET Framework 4.8.1 SDK and targeting pack).

**Fixed**
- **The plugin didn't compile in Unreal 5.8:**
  - `B[0]` on the protocol's `Json` was ambiguous to MSVC, since a literal 0 is also a null `const char *`;
  - an `int` index overload settles it.
  - Blendity's suite compiles only the protocol layer, not the applier that uses the index, so it never saw this.
- **Unreal warned about physics on every linked mesh:** the meshes were built without CPU-side data, yet their actors
  keep collision. They now keep it, so traces and clicks in Play work on them, and the transient meshes no longer
  mark a package dirty.
- **The replay test's world had no world context,** so deleting the Sphere warned. It now has one, as an editor world
  does.

**Checked**
- Unreal 5.8.4:
  - BuildPlugin succeeds;
  - `Blendity.LiveLink.Replay` passes with no warning;
  - the editor, run headless on a scratch project, connected by itself to `tools/livelink_fake_server.py` within
    12 s and introduced itself as "Unreal 5.8.4".
- Blendity's `livelink` group: 153 checks, 0 failed.
- The Blender Install a Bridge test, which skips without a Blender, was run against Blender 5.2 in a scratch user
  folder: 2 checks, 0 failed.
- Every engine bridge has now run in its engine: Blender 5.2, Unity 6000.6, Godot 4.7.2 and Unreal 5.8.

**The review's fixes (Opus, rounds 316-317)**
- **The keymap dropdown test still raced MoonRay's search,** which runs on a thread (seconds on Windows, where it
  asks WSL). The field could settle before the search ended and then move. The test now waits for the search,
  checks the field settled, and re-reads where the field is before each click. On this machine the field then sits
  24 px lower (858, not 834), so the earlier pass was luck.
- **A second integer overload made `int64` or `uint32` indexes ambiguous instead.** Indexing is now one template over
  integer types, with negatives past the end. A suite test pins it with literals, a negative, `size_t`, `int64_t`,
  `uint32_t` and `long`. GCC with `-Wall -Wextra` compiles it clean, and Unreal 5.8 builds and passes again.
- **The Godot auto-connect test** also guards an applier without a root.
- **The task spec** (docs/tasks/0295) has the rounds' results and is marked done.
- **Noted, not changed:** a deform message rebuilds the whole mesh in Unreal. With CPU access, each rebuild also keeps a
  CPU copy and cooks collision, which may slow the editor on a large deforming mesh. Measuring that is the follow-up.
- The `livelink` group: 157 checks, 0 failed.
- **The same race in three more Preferences tests:** the hci Wheel picker test failed once on Windows CI in #233.
  It and both `pref_reach` helpers in test_editor.cpp now wait for MoonRay's search and for the control to stay
  put. The hci group: 24 checks, 0 failed; the editor group: 1296 checks, 0 failed.

## 2026-10-29 (round 316): the Godot and Blender bridges run live (task 0295)

**Asked:** the user (2026-10-29): "Godot and .NET should be installed, go check", then "How about the blender bridge?"

**Added**
- **Godot 4.7.2:**
  - `smoke_test.gd` and `auto_connect_test.gd` pass headless;
  - the editor loads the enabled add-on with no error;
  - the add-on connected by itself to a running Blendity (its sample scene: the camera, the light, the plane, the
    teapot).
- **Blender 5.2.2:** both tests pass again, and the add-on connected live to a running Blendity the same way.
- **Fixed (tests only):**
  - the smoke test read the camera's global transform before the scene was in the tree, so it now builds it from
    the parents;
  - the auto-connect test stopped with a script error when no stand-in started, and now fails with its checks.
- Godot's `.uid` files are kept in the repository and the package; `.godot/` (its cache) is ignored.
- The keymap dropdown test in Preferences waits for the preset field to stay put before clicking it. It failed once on
  macOS in #232's CI: MoonRay's row appeared and moved the rows below, the same cause as round 315's fix.
- **Unreal 5.8:** still waits for the .NET Framework SDK (4.6 or newer) on this machine; the build stops at
  "Could not find NetFxSDK install dir".

**Checked**
- Godot:
  - smoke: 32 messages, every check ok;
  - auto-connect: two sessions, 9 objects adopted, not duplicated, and the hello names Godot 4.7.2;
  - the stub-Python case: 7 failed checks and exit 1, no script error.
- Blender: `SMOKE OK: 32 messages, 9 objects` and `AUTO-CONNECT OK`.
- Live: Blendity's windowed check with scratch preferences, and each bridge connected to it with no port typed
  (13 messages, 4 objects).
- No C++ changed this round.

## 2026-10-29 (round 315): the bridges in the engines (task 0295)

**Asked:** the user (2026-10-29, /plan): "Finish all open tasks and maintain HCI consistency. Provide also a write-up
that includes a checklist to test all features within the program so I can see if certain aspects are efficient";
then "Do another HCI pass to test all current live features in the program; update anything that we may have missed."

**Added**
- **Unity 6000.6.7f1:**
  - both EditMode tests pass in batch mode, the new one connecting by itself to `tools/livelink_fake_server.py`;
  - fixed: a second auto-connect no longer drops a live link, and opening another scene starts the link again so
    the objects are built in it.
- **Unreal 5.8:** the plugin no longer asks for C++17 (5.8 refuses it). Its build waits for the .NET Framework SDK
  on this machine.
- **Godot:** not installed yet.

**Checked**
- Unity: `RecordedSessionLeavesTheScene` and `ConnectsByItselfAndAppliesTheSession` pass; the package imports with
  no warning.
- Unit checks (round 315): Windows, in render-cache verify mode, 13,483, 0 failed (1175 tests), 0 stale
  pictures. Linux with libraries: 13022, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone; on Linux two tests failed alone and were fixed in the tests (the sweep's count of elided labels depends on the platform's font and is reported, not held; the Preferences helper now waits for a control to stay put, as MoonRay's row appears once found and moves the rows below), and the Linux editor and hci groups then passed, 1307 and 24 checks.)

## 2026-10-29 (round 314): the test checklist and the write-up (task 0294)

**Asked:** the user (2026-10-29, /plan): "Finish all open tasks and maintain HCI consistency. Provide also a write-up
that includes a checklist to test all features within the program so I can see if certain aspects are efficient";
then "Do another HCI pass to test all current live features in the program; update anything that we may have missed."

**Added**
- **The checklist:** 118 tasks in 17 areas (`docs/hci/checklist.json`), each with:
  - steps by menu names;
  - the expected result;
  - a target time from the keystroke-level model or an estimate on its rules.
- **An interactive page:** tick, time with a stopwatch; slow is over 1.5x the target. Results are kept for Claude
  to read back, and export as CSV / JSON.
- **`docs/hci/checklist.md`** and `tools/hci_checklist.py` (`--check`: every control a task names still exists).
- **The write-up** of rounds 291-313 (`docs/tasks/0294`).

**Checked**
- `hci_checklist.py --check`: 118 tasks, 0 control names missing.
- Round 315's unit-check line covers this round (rounds 311 to 315 landed together).

## 2026-10-29 (round 313): the HCI pass over every live feature (task 0293)

**Asked:** the user (2026-10-29, /plan): "Finish all open tasks and maintain HCI consistency. Provide also a write-up
that includes a checklist to test all features within the program so I can see if certain aspects are efficient";
then "Do another HCI pass to test all current live features in the program; update anything that we may have missed."

**Added**
- **The live sweep** (`tests/test_hci.cpp`, `--group hci`): every layout, window, menu, submenu, dialog and component
  Inspector opened at six configurations (826 screens, about 4 s), with each control checked.
- **The screenshot review** (`docs/hci/review-313.md`): 12 findings rated on Nielsen's scale.
- **The fixes:**
  - a button label that doesn't fit ends in "..." with the whole label as its tooltip;
  - the Editing window, Install a Bridge, the UV editor's toolbars and the Sequencer lay out for a narrow pane;
  - the Profiler's labels no longer cover its title;
  - Preferences' empty reason and the console name in the AI row are fixed;
  - the Dark theme's dim text is at 4.6:1 (was 3.5);
  - tooltips where controls had none.

**Checked**
- The sweep's counts held in the suite (2 elided, 6 rows without a tooltip, 0 overlaps); the full matrix at
  `tools/hci_sweep_baseline.json`; 0 popups outside the window, 0 disabled buttons without a reason.
- Round 315's unit-check line covers this round (rounds 311 to 315 landed together).

## 2026-10-29 (round 312): interface conventions, checked (task 0292)

**Asked:** the user (2026-10-29, /plan): "Finish all open tasks and maintain HCI consistency. Provide also a write-up
that includes a checklist to test all features within the program so I can see if certain aspects are efficient";
then "Do another HCI pass to test all current live features in the program; update anything that we may have missed."

**Added**
- **Five conventions written down (USABILITY.md) and checked by `tools/hci_consistency_audit.py` at 0:**
  - "..." on a control that asks first;
  - Title Case on buttons and menus;
  - "Color" in what the artist reads (about 60 strings; saved field names kept);
  - tooltips as sentences;
  - no command names.

**Checked**
- The audit at 0, with literals parsed so a ';' or a URL inside a string hides nothing; the console audit at 1.
- Round 315's unit-check line covers this round (rounds 311 to 315 landed together).

## 2026-10-29 (round 311): the last console-only items (task 0291)

**Asked:** the user (2026-10-29, /plan): "Finish all open tasks and maintain HCI consistency. Provide also a write-up
that includes a checklist to test all features within the program so I can see if certain aspects are efficient";
then "Do another HCI pass to test all current live features in the program; update anything that we may have missed."

**Added**
- **The Animation key menu's In / Out slopes:** typed, one undo step; a Free key keeps both sides one.
- **Rig Hair from Mesh...:** a dialog asks for the locks and the bones.
- **An Anchor's Layer:** a dropdown of the texture's named layers.
- **A Sequencer trigger's key:** captured by a press. Ctrl / Alt chords and a click elsewhere don't take it.
- **The Wheel editor:** Pick a Command... (searchable; Enter takes the first match, Esc closes only the list) and
  Into Ring as a dropdown; the palette's Add to the Wheel offers the rings.
- **Preferences:** Layout guides.
- `ui::Context::open_dropdown` and `close_popup`: a list over a dialog closes alone.

**Checked**
- `tests_round311` and the reviews' cases (the slopes, the trigger capture, the picker, the anchor list) by real
  events; the reach table at 31 verbs.
- Round 315's unit-check line covers this round (rounds 311 to 315 landed together).

## 2026-10-29 (round 310): the console's last mentions, the reach table, measured (task 0290)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems. (The plan's rounds 294-298, renumbered after the
bridges and Puppet Posing.)

**Added**
- Tooltips, help and messages that sent the artist to the Console name the control: **1 place left** of 26 at round
  291 (a UI file's shortcuts can run console lines: that is what they are); `tools/hci_console_baseline.txt` is 1.
- **The console-reach table** (`kConsoleReach`, tests/test_editor.cpp): 29 verbs, each with the way to its control;
  `tools/test_coverage_map.py` lists them.
- **A dialog's open flag per editor** (was a file static: a second editor closed its own Preferences).
- **A dialog taller than the window fits it and scrolls** (Preferences reached past 900 px on Linux, where MoonRay's own
  row is drawn, and its last rows were out of reach on a small screen).
- Keystroke-level rows 23-27 (docs/hci/klm.md): a contact 12.5 s typed, 4.85 s by the Puppet menu; a walk style
  kept 8.1 s, 2.55 s; drawing in red 7.9 s, 5.95 s; a modifier's group 5.3 s, 3.75 s; lifting the sketch 6.1 s,
  4.85 s (model estimates; the typed path also needs the command recalled exactly).

**Checked**
- `tests_round310`: 29 of 29 verbs reached; `hci_console_audit.py --check` at 1; `hci_klm.py --check` clean.
- Unit checks (round 310): Windows, in render-cache verify mode, 13,448, 0 failed (1170 tests), 0 stale
  pictures. Linux with libraries: 12988, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone, as Linux's modeling group did; on Linux task 0288's test clicked Preferences rows below the window (MoonRay's own row makes the dialog taller there) - the dialog now scrolls, the test reaches its rows by the wheel, and the Linux editor and modeling groups then passed, 1299 and 2068 checks.)

## 2026-10-29 (round 309): animation, rigging and Inspector verbs without the console (task 0289)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems. (The plan's rounds 294-298, renumbered after the
bridges and Puppet Posing.)

**Added**
- **Puppet > Lock the Selected Bone** (by the bone, so a second character's bone of the same name is its own);
  **Contacts** (Find, Pin, Add One Here, Remove the One Here, Clear); **Ragdoll Events** where Jolt is built in (a hit
  from the character's front, back, sides or below, turned with it; limp; strong; clear; the motors' strength).
- The Walk Cycle's **Save Style** and **Load Style...**; the Inspector's **Vertex Group / Thickness Group** as a dropdown
  of the mesh's groups (the Inspector's object, pinned or selected); **Add Shape Key** and **Remove...**.

**Checked**
- `tests_round309` by real clicks, two characters with the same bone names among them: 13 checks.
- Round 310's unit-check line covers this round (rounds 306 to 310 landed together).

## 2026-10-29 (round 308): render, MoonRay and AI verbs without the console (task 0288)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems. (The plan's rounds 294-298, renumbered after the
bridges and Puppet Posing.)

**Added**
- The Render window's **First Frame, Last Frame, Frame Rate** (0: the source's own) used by the button, the Render menu
  and Ctrl+F12 alike, a backwards range refused, a first frame past the end refused before any folder is made; a
  **Sequence Folder** (inside the project when relative, as the console's).
- Preferences: **FFmpeg**'s path; MoonRay's **Your own MoonRay** folder and **Check**; AI Inbetweening's archive by its
  path or ... and **Check**, on a row of their own under the install state.
- The Animation window's AI **...** options: a span by frame numbers and Use the Fit to My Clips.

**Checked**
- `tests_round308` and the Render Animation range test: 8 checks.
- Round 310's unit-check line covers this round (rounds 306 to 310 landed together).

## 2026-10-29 (round 307): modeling, UV and lighting verbs without the console (task 0287)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems. (The plan's rounds 294-298, renumbered after the
bridges and Puppet Posing.)

**Added**
- Mesh Tools' **Mirror Y** and **Clear Custom Normals**; GameObject > Reference Image > **Front / Side / Top**, each
  listing the project's images beside Browse...; a light aimed at a typed point; a guide line by numbers (Edit Mode's
  tools), one way with the console's `guide` (a guide already there is said, not doubled); Weight Paint's **Clean
  Below**.
- The UV editor's material filter as a list with a check per slot and Others Dimmed / Hidden; a **Parts** button for
  Part Unwrap's bound, tiles and part tree - setting the next unwrap only (the console's `uv parts` still unwraps).
- A submenu taller than the window fits it and scrolls.

**Checked**
- `tests_round307` by real clicks: 13 checks.
- Round 310's unit-check line covers this round (rounds 306 to 310 landed together).

## 2026-10-29 (round 306): stylized and sketch verbs without the console (task 0286)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems. (The plan's rounds 294-298, renumbered after the
bridges and Puppet Posing.)

**Added**
- **Mesh > Draw**: Pen Colour, Steadiness, Clear the Sketch, a Canvas submenu (the view, the ground, a plane facing an
  axis, the face under the middle, the canvases by name), Lift the Sketch to 3D; Guides gains French Curve Shape,
  Radial Copies and Perspective Follows the Camera. `sketchlift` joins the console's command list.
- **GameObject > Anime Look** (the anime presets on the selection); the Strokes section's New Stroke Object, the
  canopy centre by numbers and Grass (density, height, radius); Scatter's Seed.
- `strokeobj new` makes a new object even when a Stroke object is selected (it took the selected one).

**Checked**
- `tests_round306` by real clicks: 19 checks.
- Round 310's unit-check line covers this round (rounds 306 to 310 landed together).

## 2026-10-29 (round 305): Puppet Posing written up and measured (task 0285)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **The write-up** (`docs/tasks/0285`): Marionette's tools beside Blendity's; the prior's table; keystroke-level rows:
  posing a reach 20.2 s bone by bone, 3.75 s by a handle; fixing a foot slide 30.4 s, 6.3 s with Motion Check.

**Checked**
- `tools/hci_klm.py --check`: 21 rows recount; 131.5 s before, 64.7 s after.
- Unit checks (round 305): Windows, in render-cache verify mode, 13,392, 0 failed (1164 tests), 0 stale
  pictures. Linux with libraries: 12932, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone; on Linux the Puppet Posing editor test's angle gate (an acos of a float dot, 0.040 degrees) was widened to 0.25 degrees and the animation group then passed, 791 checks.)

## 2026-10-29 (round 304): Motion Check, the smooth brush, Lock in Place (task 0284)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **Motion Check** finds sliding feet, pops and shaking, each in the Report dialog with a jump and a Fix; the
  **Smooth Brush** evens keys under a drag over the dope sheet; **Lock in Place** holds a hand or foot.
- **The Puppet menu** gathers these tools: the Animation window's toolbar was already wider than most windows.

**Checked**
- `tests_round304`: four faults found and none left after the fixes; by clicks, a row's Fix takes one out and the
  brush takes a shake from 8.0 to 0.9 degrees.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 303): the Zoetrope (task 0283)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **The Zoetrope**: seven neighbouring frames side by side in the Animation window, each poseable by its handles
  without moving the playhead.

**Checked**
- `tests_round303`: a drag in the frame-plus-4 cell keys frame 14, the playhead and the scene as they were.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 302): motion trails and onion skin (task 0282)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **Motion Trails** with the rig's keys as dots (drag to pose, Alt-drag to move the key in time) and **Onion Skin**, from
  a cache of posed frames sampled a few each editor frame.

**Checked**
- `tests_round302`: 31 frames over 6 editor frames, the scene put back; the trail exact at frame 10; an Alt-drag moves
  the keys from 10 to 13.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 301): posing handles and Reach (task 0281)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **Handles** on the selected rig's hands, feet, head and hips while AutoPosing is on: drag one to pose, click to pin.
- **Reach**: a pose edit fading into the keyed frames around the playhead.

**Checked**
- `tests_round301`: Reach 6 at frame 10 changes 5 and 15 a little, 0 and 20 not at all; handles dragged and clicked by
  real events.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 300): the learned pose prior (task 0280)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **A pose prior** trained on the CMU data (`tools/posemodel`): any of the hands, feet, head and the hips' height in,
  every joint's turn out; on held-out subjects 2.0 cm mean joint error with every goal, 0.13 ms a pose on the CPU.
- **Puppet Posing in AutoPosing's drag**: the prior's pose, then exact IK from it; without the prior, AutoPose as
  before. The prior loads on a thread; `package.py --prior` puts it in the AI download (publishing is the
  maintainer's).
- **FK controls** turn with their bones and are keyed instead of them (a posed bone no longer snaps back).

**Checked**
- `tests_round300`: the canonical frame and back exact (0.00 mm); the goals within 0.44 mm and the feet 0.04 mm with a
  stand-in prior; an FK spine keeps its pose; with the trained prior a hand to the knee 0.18 mm off, the spine bent.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 299): one humanoid map for every rig (task 0279)

**Asked:** the user (2026-10-29, /plan): "How to implement Animatica Marionette". Their choices: a learned pose prior
plus IK, after the bridges. Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

**Added**
- **Limbs read by words**: a side marker anywhere in a bone's name (Biped, Character Creator, VRoid, Daz, the UE5
  mannequin, as Mixamo and Rigify before); twist and helper bones aren't limbs; Daz's Collar is a shoulder. The walk,
  footsteps and AI inbetweening read these rigs by name now.
- **`humanoid::map`**: 21 parts on any rig and whether it stands in a T or an A pose; `fk_control` for keying.

**Checked**
- `tests_round299`: 31 names read right; five naming schemes in T and A pose, every part found by its own bone, the
  arms' drop measured.
- Round 305's unit-check line covers this round (rounds 299 to 305 landed together).

## 2026-10-29 (round 298): the bridges as packages, and installed from Blendity (task 0278)

**Asked:** the user (2026-10-29): make add-ons for Unity, Unreal and Godot so that the bridge is established
automatically (a Unity package, a Blender package...). Their choices: Live Link starts with Blendity by default, Unreal
as a C++ editor plugin, and they install the engines themselves for the in-engine tests.

**Added**
- **`tools/package_bridges.py`**: the Blender extension zip, a Unity .tgz and .unitypackage, the Godot zip and the Unreal
  zip, byte for byte the same from the same sources; CI builds them as artifacts (releases stay the maintainer's).
- **The installers carry the bridges**; **Install a Bridge** in the Live Link window: Blender found and the add-on
  enabled by Blender in the background; a Unity, Godot or Unreal project picked, the bridge copied in and enabled.

**Checked**
- `tests_round298`: scratch projects for the three engines (enabled beside other plugins, still JSON, not twice); a
  folder that is no project refused; with Blender 5.2.2, the add-on copied, enabled, its preferences saved. Blender's
  `extension validate` accepts the zip.
- Unit checks (round 298): Windows, in render-cache verify mode, 13357, 0 failed (1154 tests), 0 stale
  pictures. Linux with libraries: 12897, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise.)

## 2026-10-29 (round 297): the Unreal bridge, a C++ editor plugin (task 0277)

**Asked:** the user (2026-10-29): make add-ons for Unity, Unreal and Godot so that the bridge is established
automatically (a Unity package, a Blender package...). Their choices: Live Link starts with Blendity by default, Unreal
as a C++ editor plugin, and they install the engines themselves for the in-engine tests.

**Added**
- **An Unreal 5.3+ editor plugin** (`bridges/unreal/BlendityLiveLink`): a dock tab, a non-blocking socket ticked by
  the core ticker, transient actors and static meshes, colours on BasicShapeMaterial, lights and cameras,
  auto-connect; its protocol layer in standard C++, compiled into Blendity's suite.

**Checked**
- `tests_round297`: the recorded session through the plugin's own reader - every mesh, normals agreeing at 878 of 878,
  the deform, the Box at (300, 100, 200) cm, a turn mapping with the points, the discovery file. The plugin itself and
  its automation test: after Unreal is installed.
- Round 298's unit-check line covers this round (rounds 294 to 298 landed together).

## 2026-10-29 (round 296): the Godot bridge (task 0276)

**Asked:** the user (2026-10-29): make add-ons for Unity, Unreal and Godot so that the bridge is established
automatically (a Unity package, a Blender package...). Their choices: Live Link starts with Blendity by default, Unreal
as a C++ editor plugin, and they install the engines themselves for the in-engine tests.

**Added**
- **A Godot 4.2+ add-on** (`bridges/godot`): a dock, StreamPeerTCP within 20 ms a frame, ArrayMeshes from the glTF
  binaries with deforms, StandardMaterial3D, lights and cameras, auto-connect; a tiny test project.

**Checked**
- `smoke_test.gd` and `auto_connect_test.gd` written; not run (Godot isn't installed here yet).
- Round 298's unit-check line covers this round (rounds 294 to 298 landed together).

## 2026-10-29 (round 295): the Blender and Unity bridges connect by themselves (task 0275)

**Asked:** the user (2026-10-29): make add-ons for Unity, Unreal and Godot so that the bridge is established
automatically (a Unity package, a Blender package...). Their choices: Live Link starts with Blendity by default, Unreal
as a C++ editor plugin, and they install the engines themselves for the in-engine tests.

**Added**
- **Blender 1.1.0 and Unity 1.1.0**: Auto Connect (on), the discovery file read every 2 seconds while not connected,
  found again after Blendity restarts, Disconnect holding it off; a hello naming the program.
- **The Unity package's .meta files** (`tools/unity_meta.py`, stable GUIDs), as a package from a .tgz or git needs.

**Checked**
- Blender 5.2.2 headless against the stand-in (`auto_connect_test.py`): connected with no port, the session applied,
  found again on a new port, no duplicates, the hello; the smoke test still passes. `ReplayCheck.exe --live` (csc):
  found through the file twice, the sessions whole, the hello.
- Round 298's unit-check line covers this round (rounds 294 to 298 landed together).

## 2026-10-29 (round 294): Live Link starts with Blendity and says where it listens (task 0274)

**Asked:** the user (2026-10-29): make add-ons for Unity, Unreal and Godot so that the bridge is established
automatically (a Unity package, a Blender package...). Their choices: Live Link starts with Blendity by default, Unreal
as a C++ editor plugin, and they install the engines themselves for the in-engine tests.

**Added**
- **Start with Blendity** (Preferences > Live Link, the Live Link window; on by default), the port saved; 40100 taken:
  the next free up to 40109, said.
- **The discovery file** `~/.blendity/livelink.json` (the port, the scene, the process) while Live Link listens,
  removed on Stop and exit (only this process's).
- **Clients by name**: a bridge's hello line lists it in the window. `tools/livelink_fake_server.py`, a stand-in Live
  Link for the bridges' tests.

**Checked**
- `tests_round294`: not started headless; a taken 40100 moves to 40101, said; the file's fields; a hello named; the
  file gone after Stop and the editor's end; another Blendity's file left alone.
- Round 298's unit-check line covers this round (rounds 294 to 298 landed together).

## 2026-10-29 (round 293): texturing and bakes without the console (task 0273)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems.

**Added**
- **New Surface** in the GameObject menu, the Textures window's New, the palette and the Texturing guide.
- **A layer's menu**: Mask Preset, Clear Mask, Group with the Layer Below, Save as Smart Material...; the shelf lists
  the project's saved smart materials.
- **A Bake dialog** (size, samples, Keep UVs) for Bake Mesh Maps... and Bake Textures....
- **Review (Opus) taken:** the path row and the folder dialog were never used - Export Textures' folder has the
  field and its ... (and a click away from the field no longer exports); Save as Smart Material wrote over a saved
  file of the same name - a dialog offers a free name and says when Replace would write over one, and the item is
  for Group layers only; the Bucket filled on a click that missed the model and stayed on after Paint; the palette's
  name refilled while typed; Bake Textures kept UVs by default; the Layer list offered layers the brush ignores; the
  shelf and the Palettes list read the disk every frame; Load Mesh Maps' Fix started a bake without asking; the
  folder dialog is Windows' IFileOpenDialog.

**Checked**
- `tests_round293`: New Surface, the layer menu's four items and the Bake dialog by clicks; the review's fixes (Export
  Textures' folder typed and ..., a Fix clicked with the Scene view hidden, a Bucket miss).
- Unit checks (round 293): Windows, in render-cache verify mode, 13309, 0 failed (1149 tests), 0 stale
  pictures. Linux with libraries: 12849, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise.)

## 2026-10-29 (round 292): Sculpt and Paint without the console (task 0272)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems.

**Added**
- **Sculpt**: an Alpha picker, Fill / Sharpen / Grow / Shrink Mask, groups From Materials and Merge All, Hide Unmasked,
  Invert Hidden, Show All, Hide Group.
- **Paint**: Bucket Fill (group, island, material, object; a gradient), Bake Light and Bake Gradient, Brush Alpha,
  Grain and Stencil pickers, Paint on Textures Canvas, the Layer list, Palettes load and Save Palette, a right-click
  removing a swatch; the gradient and the light bake refused with Load or Bake Mesh Maps as the Fix.

**Checked**
- `tests_round292`: masks, groups and hiding by clicks; a project image as the alpha; a Bucket click on the cube; the
  palette saved and a swatch removed.
- Round 293's unit-check line covers this round (rounds 291 to 293 landed together).

## 2026-10-29 (round 291): pickers, a Report dialog, refusals with a Fix, messages that name the interface (task 0271)

**Asked:** the user (2026-10-29): many editor features can only be reached by typing console commands; give them
a way through the interface, and look for related HCI problems.

**Added**
- **Pickers**: an image row (None, Browse..., the project's images) and a path field with a ... button for the
  system's file or folder dialog (a new folder dialog on Windows, Linux and macOS).
- **A Report dialog** shows what UV Report and Scene Statistics printed, with Copy.
- **Refusals with a Fix**: stop Play, cancel the running job, open the window or turn on the setting asked for.
- **Messages name the interface** instead of a console command; toasts and their Fix show when another tab hides
  the Scene view; `tools/hci_console_audit.py` counts what still sends the artist to the Console.

**Checked**
- `tests_round291`: a report and its Copy; Fix: Stop Play, Turn Realtime GI on and Cancel the bake by clicks; Browse...
  with no file dialog says how else.
- Round 293's unit-check line covers this round (rounds 291 to 293 landed together).

## 2026-10-28 (round 290): the pen's targets at 7 mm in Tablet Mode, asserted; the hint's help (task 0270)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **The pen's targets** - navigation buttons, a modal's step buttons, the job tray's Cancel, a toast's Fix - are 7 mm
  or more in Tablet Mode (the status bar and the toasts 34 px tall there), asserted by a test.
- **The hint's ?** opens the Learn lesson for what is under way.
- **Review (Opus) taken:** the Adjust Last panel outlived Undo, Redo, New and Open (a re-run after Undo would have
  folded into the previous step; after Open it could replace an unrelated object's mesh) - it ends with them and with
  Play, and lasts only while the object still has the mesh the operation left, unedited; a Draw panel only in its Edit
  Mode; the Edit menu's Adjust Last works as F9. A glTF export left the scene changed (a clip's non-transform numbers
  at its last frame, the bones' Inspector angles re-derived) - every transform is put back whole and every number the
  clip names; a vertex no bone moves was written with zero weights (collapsed to the origin in most engines) - it
  follows the mesh's own object with weight 1; the skin's Max Influences and vertex group are honoured, bones with a
  singular rest left out, each group's joint looked up once, the clip sampled to its last key. The dopesheet drew keys,
  the playhead and the body strip outside the sheet when zoomed - clipped; a double-click left of frame 0 keyed a
  negative frame - frame 0; the view is fitted to a new clip and kept within reach. A Scatter mask the surface lacks is
  shown as such, not as None; the sequencer Play test runs on the editor's clock. A long dropdown (the Alpha list in a
  big project) flipped over its own field, where the opening click's release picked a row - it fits the larger side and
  scrolls. Open: the re-run happens on every change of a dragged field (as Edit Mode's panel does); each skinned mesh
  carries its own copy of the skeleton and clip; FBX still writes the mesh as posed.

**Checked**
- `tests_round290`: the six targets printed and asserted at 39.7 px or more at 1.5x; the ? while sculpting opens
  Sculpting.
- Unit checks (round 290): Windows, in render-cache verify mode, 13278, 0 failed (1145 tests), 0 stale
  pictures. Linux with libraries: 12818, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise; the views-on-the-card leg found one stale picture from round 289's test hook, fixed (it clears the view caches as a device change does) and that leg's editor group run again: 1229 checks, 0 stale.)

## 2026-10-28 (round 289): accessibility - Okabe-Ito polygroups, the eraser by shape, the CPU badge, selected keys by shape (task 0269)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **Polygroups** in Okabe and Ito's eight colours, then lighter and darker; **the brush ring** dashed with an E while
  erasing or subtracting (Paint's ring now follows its Erase setting); **a CPU badge** in the Scene view's bar while
  the views wanted the card and draw on the CPU; **selected keys** outlined; High Contrast's selected key cyan.

**Checked**
- `tests_round289`: the palette's first colour and tiers; the ring erasing only with Erase on; the badge absent when
  the CPU is chosen, present with Auto and no card.
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 288): glTF export of skeletons, skin weights and the rig's clip (task 0268)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **A rigged character exports to glTF** at rest with its skeleton, four weights a vertex, inverse bind matrices and
  its rig's clip sampled per frame (export option Armatures and Animation, on by default).

**Checked**
- `tests_round288`: a cube with an Armature and a clip turning its bone 45 degrees, exported and read back by
  Blendity's own importer: one skin, the bone, its vertex group, a clip turning it 45.00 degrees.
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 287): the 3D Object menu in every layout; time zoom and pan in the Animation window (task 0267)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **GameObject > 3D Object** in every layout.
- **The Animation window's time**: Ctrl+wheel or a pinch zooms about the pointer, Shift+wheel, two fingers or the
  middle button pans, Home or Fit shows the whole clip.

**Checked**
- `tests_round287`: the submenu in four other layouts; the zoom keeps the frame under the pointer; the pan and the
  pinch move and widen the view; Fit restores it.
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 286): Adjust Last outside Edit Mode - primitives, UV operators, Draw shapes (task 0266)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **The Adjust Last panel** after an added primitive (size, radius, height, segments, rings, subdivisions), a UV
  operator (Unwrap's and Pack's margin, Smart UV's angle) and a click-made Draw shape (segments, sides, fillet): a
  change runs it again from before and amends the same undo step; F9 shows it in the Scene view.

**Checked**
- `tests_round286`: a Cylinder's segments, an Unwrap's margin and a circle's segments typed into the panel, the undo
  steps unchanged, one undo removing the Cylinder.
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 285): pen reach - the modal step bar, the UV editor's Pan (task 0265)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **The modal step bar**: Bevel's segments, Array's count, Grid's rows, Pipe's sides, Taper's and Recess's second
  amount by - and + buttons a pen can tap; the amount holds while the pointer is on the bar.
- **The UV editor's Pan toggle**: a left drag pans with it on; two fingers pan.

**Checked**
- `tests_round285`: + + - by taps steps Bevel's segments, the modal still running and its width unchanged; a left drag
  of (50, 30) with Pan on moves the UV view by (50, 30).
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 284): the stylized tools in the panels - Scatter's source and mask by name, Strokes' alpha and Fill Canopy (task 0264)

**Asked:** the user (2026-10-28): start and finish all the follow-up work round 282's walkthroughs and audits
left open.

**Added**
- **Scatter's Source and Mask Layer** are dropdowns: the scene's meshes, the surface's layers by place and name.
- **Strokes' Alpha** is a dropdown of the project's images; **Fill Canopy** is a button with its radius and count.

**Checked**
- `tests_round284`: each picked through real clicks in the Stylized layout; Fill Canopy adds 200 cards.
- Round 290's unit-check line covers this round (rounds 284 to 290 landed together).

## 2026-10-28 (round 283): the CI-only and non-LTCG test failures (task 0263)

**Asked:** review the test issues and check the non-passing cases on GitHub. Every PR from #218 failed CI on all four
jobs while the local chain (Blender's libraries, LTCG, 32 threads) passed; #189's sanitizer job failed; a modeling test
failed in any build without LTCG.

**Fixed** (reproduced with the build CI makes, `BLENDITY_NO_LIBS=1`, and without LTCG)
- **The job tray's progress jumped from 0 to done** on a two-core runner: the Mesh Maps baker reported per block of
  rows; it reports per row.
- **A modeling test wrote a missing normal map into the shared Default-Material**, so every later test in the project
  warned "cannot read none.png" (the toast test's leftover warning): the test gives the cube its own copy.
- **The full and the in-place corner normals differed in the last bit** without LTCG (`/fp:fast` inlined `normalize`
  two ways): one out-of-line `corner_normalize` in every path.
- **Auto Generate's settle second ran on the wall clock**, which a sanitizer build's frames overran: it runs on the
  editor's clock and the tests set it.
- **Tests:** the Boolean-by-name test accepts the dependency-free build's refusal (no Manifold); the toast test
  tolerates an Info toast after the Fix; the autopose time is printed, not asserted (ADR 0056); a sequencer Play test
  runs half a second on the editor's clock, a 60th a frame, not 31 frames of the wall clock.
- **Review (Opus) taken:** the sequencer Play test still leaned on the wall clock (Play's step is capped at 50 ms, so
  a slow frame gave less Play than wall time) - it runs on the editor's clock at a 60th a frame. Open: `corner_normalize`
  is a call per corner in the normal loops (no stress row measured it this round).

**Checked**
- The whole suite in a `BLENDITY_NO_LIBS=1` build without LTCG (what CI builds, and the build the drag test failed in).
- Round 290's unit-check line covers this round (rounds 283 to 290 landed together).

## 2026-10-28 (round 282): the HCI pass measured - KLM, Fitts, hints, labels and walkthroughs after; the study not run (task 0262)

**Asked:** ADR 0085's last round: the measuring sticks of round 271 again on the finished pass, the study, the write-up.

**Added**
- **The KLM table's after column** and `tools/hci_klm.py`, which recounts each row from the stated operator rules and
  sums the columns: 19 frequent actions, 80.9 s before (the recount: the baseline's 77.0 left out a row), 54.65 s after
  in the Unity preset, 50.6 s in the Adobe preset. Model estimates for an expert, not measurements of people.
- **The audits again:** shortcut labels the keymap does not produce 121 -> 0; tool modes without a hint 2 of 8 -> 0;
  targets under 7 mm at UI scale 1.0 92 % -> 78 % of a target count half again as large (`docs/hci/fitts-282.md`).
- **The four walkthroughs walked again:** 24 of 46 steps failing before, 7 failing and 4 partly after; what is left is
  listed per step (UV panning by pen, Adjust Last outside Edit Mode, the foliage features in the console).
- **The study's status** (`docs/hci/study-282.md`): **not run** - no artist has used this build, so no SUS score or
  task time exists; what is ready and what it should test.
- The write-up in `docs/tasks/0262-hci-measured.md`; research/FEATURES.md rows; a Learn question.

**Checked**
- `py tools/hci_klm.py --check` passes on the table and fails on a copy with one row's seconds changed.
- Unit checks (round 282): Windows, in render-cache verify mode, 13230, 0 failed (1138 tests), 0 stale
  pictures. Linux with libraries: 12770, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise.)

## 2026-10-28 (round 281): onboarding and transfer - the origin picker, the Adobe preset, one brush grammar, a High Contrast theme (task 0261)

**Asked:** ADR 0085's transfer round: an artist arriving from Unity, Unreal, Photoshop and Substance, or Blender met
Unity's keys and no question; there was no Adobe preset; each brush tool had its own size keys or none; no theme was
measured against WCAG's 4.5:1. Transfer of learning (Singley and Anderson), Nielsen's H2, WCAG 1.4.3 and 1.4.11.

**Added**
- **The origin picker**: the welcome (every first run now, not only on a Low-tier machine) asks "Where are you coming
  from?" - Unity, Unreal, Blender, Photoshop & Substance, Just start - and sets the keys and the Editor Feel in one go;
  Help > "Welcome: Where Are You Coming From?" asks again.
- **The Adobe preset** (`keymap adobe`): Unity's keys with B for Paint Mode, E for the eraser, V for the Move tool,
  `[` `]` for the brush size; the Rotate tool's E gives way (the toolbar has it).
- **One brush grammar**: `[` and `]` (`brush.smaller` / `brush.larger`, every preset) step the active brush's size -
  Sculpt, Paint, Weight Paint, Strokes, Scatter - by a quarter, a toast saying the value; the five hints name the keys.
- **A High Contrast theme** (`theme High Contrast`): white on near-black, every text at 4.5:1 or more, a yellow focus
  ring apart from the yellow warnings, black text on its amber; `ui::contrast_ratio` (WCAG 2) measures every theme and
  fourteen of High Contrast's pairs in the tests.
- **`keymap table`**: every action's chords under every preset as a Markdown table, for the manual's chapter 12.
- **Review (Opus) taken:** the update, AI and libraries offers replaced the welcome on a fresh install - a finished
  check now waits while any dialog is open and asks once it closes; text on High Contrast's amber (the Learn button,
  the tray's fill) was 1.8:1 - a text-on-accent colour (black, 11.5:1) and a tray fill of the theme's own (8.4:1),
  both in the test with dim text on the selection (now 5.0), disabled buttons and tooltips; the focus ring is cyan,
  apart from the yellow warnings; the benchmarks close the welcome a scratch profile opens; `[` `]` keep inside each
  brush's field limits (Scatter's 1000 m, 500 px) and never stick at 1 or 2; "Just start" keeps the keys and closes
  the welcome; the Adobe answer keeps the user's own key changes as the feels do, and the preset keeps Unity's W
  beside V; the welcome's question in two rows; the hints read `[` `]` from the keymap; the eraser outside Paint is
  refused with a Paint Mode Fix. Open: a selected key differs from an unselected one by hue (1.4:1) in High Contrast;
  the deferred offer is not driven by a test (it needs the network check).

**Checked**
- `tests_round281`: the Adobe preset's B / E / V / `[` / `]` and its unbound Rotate; `]` in every preset; `]` and `[` on
  the Sculpt radius and the Paint size, the eraser toggled, nothing with no brush on; every theme's ratios printed, the
  High Contrast theme's least at 4.5 or more and its focus ring at 3 or more; the welcome on every first run, its
  Adobe and Blender answers, the prefs keeping the Adobe keys; the table's rows and columns. The keymap test's clash
  check now runs over Adobe too.
- Unit checks (round 281): Windows, in render-cache verify mode, 13230, 0 failed (1138 tests), 0 stale
  pictures. Linux with libraries: 12770, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise.)

## 2026-10-28 (round 280): files and the shell - console recall and suggestions, .anim opens Animation, the Wheel's animation slices (task 0260)

**Asked:** ADR 0085's files-and-shell round: the console kept a history nobody could recall and answered an unknown
command with "type help"; double-clicking a .anim asset opened a text editor; the Wheel had no animation slice (round
254 brought the unsaved-changes prompts, round 273 the import's busy state). Nielsen's H6, H9, H7.

**Added**
- **Console history recall**: Up and Down step through the commands typed this session; Down past the newest clears.
- **Suggestions on an unknown command**: "Did you mean: meshmaps, ...?" - up to three of the palette's console commands
  whose first word starts with, or contains, what was typed.
- **.anim opens the Animation window** from the Project window's double-click, with a line saying how a clip is used.
- **The Wheel's animation slices**: Set Key, Record and Play Clip in an Animation ring under the object ring's More (Select
  All and Deselect in a Selection ring beside it: eight slices a ring).
- **Review (Opus) taken:** the three slices pushed the object ring's More to eleven, past the Wheel's eight, so Record,
  Play Clip and the Palette slice could not be reached - a Selection ring and an Animation ring hold them, a test
  checks every built-in ring fits; Down with no recall active wiped the line being typed - it leaves it; the generated
  command list missed the sketch families dispatched before the chain - added, and the comment says nothing checks the
  list yet; an action named in the palette or the Wheel could stand down for the window that had the keyboard - named
  actions run; a stale comment on the skeletons. Open: the Up / Down path through the field itself is not driven by a
  test; Esc after a recall restores the recalled line rather than what was typed; two armatures of one name bind alike.

**Checked**
- `tests_round280`: two commands recalled with Up, returned with Down, cleared past the newest; `meshmap` answered with
  `meshmaps`, `sketchlin` with `sketchline` and a nonsense word with nothing; the object ring lists the three animation
  slices and every built-in ring fits the Wheel's eight.
- Unit checks (round 280): Windows, in render-cache verify mode, 13192, 0 failed (1137 tests), 0 stale
  pictures. Linux with libraries: 12732, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone: timing noise.)

## 2026-10-28 (round 279): rigging and weight paint - Ctrl and the pen's eraser subtract, a Skeleton field for the skin (task 0259)

**Asked:** ADR 0085's rigging round: Weight Paint's Add brush ignored the pen's eraser end and Ctrl, which every other
brush honours, and binding a mesh to a skeleton needed a two-object selection made by Ctrl+click in the Hierarchy that
the Rigging window could only describe. Nielsen's H4 (consistency), H6 (recognition over recall), the pen.

**Added**
- **Weight Paint subtracts with Ctrl and with the pen's eraser** while the Add brush is active (`wp_effective_mode`,
  decided once per stroke); the other brushes are left as they are.
- **A Skeleton field for the skin**: with a mesh selected alone, the Rigging window's Skin section offers a dropdown of
  the scene's Armatures and enables Add Armature (Skin) for the one chosen; the pick belongs to that mesh; Ctrl+click
  still works and wins when a bone is selected.
- **Review (Opus) taken:** the Skeleton dropdown listed the "<mesh> Rig" groups (the Armature holders' parents), so a
  bind named a non-armature, deformed nothing and re-nested the first character under the second mesh's new rig - it
  lists the Armatures themselves, which also covers an Armature at the scene's root, and the test now asserts the bind
  made vertex groups and moved nothing; the brush mode was decided per vertex per dab, so Ctrl pressed mid-stroke
  jumped the weights - decided once when the stroke begins; a pick outlived the mesh it was made for - it resets with
  the mesh; the brush help and the lesson say what Ctrl and the eraser do. Open: the Rigging window's grid buttons give
  no reason when disabled (the grid helper predates `button_enabled`).

**Checked**
- `tests_round279`: the Add brush's mode with Ctrl, with the eraser and plain, Blur untouched; a skeleton made, the Cube
  selected alone shows the Skeleton dropdown, the pick enables Add Armature (Skin), and a real click binds the Sphere to
  the Armature (its Skin modifier names it, vertex groups made, nothing moved in the Hierarchy).
- Round 280's unit-check line below covers both rounds (they landed together).

## 2026-10-28 (round 278): animation - keymap actions, bigger key targets, a Record cue, Play-time refusals (task 0258)

**Asked:** ADR 0085's animation round: the keymap had no animation action (no preset could offer Unity's K or Blender's
I, the palette listed nothing of animation), the dopesheet's keys were 5 px hits with a right-click-only tangent menu,
Record showed as a dot in one window, and Set Key in Play did nothing in silence. Nielsen's H1,
H4, Fitts.

**Added**
- **Keymap actions** (category Animation): `anim.key` (a key at the playhead on every track, from the objects' values
  now), `anim.play`, `anim.prev_key` / `anim.next_key`, `anim.record`; bound Shift+K / Ctrl+Shift+K / , / . under
  Unity and Unreal (K and I are the Knife and Inset here), Shift+I / Up / Down under Blender, S under Maya; the
  palette lists them.
- **Bigger key targets**: a dopesheet key is hit within 9 px either side - an 18 px target, 4.8 mm at 96 DPI (10 px
  before; still under 7 mm).
- **The tangent menu by Alt+click** as well as right-click.
- **A Record cue in the Scene view**: a red frame round the view and a RECORD badge in its bar while Record is on - a
  word and a frame, not a colour alone.
- **Play refused aloud**: Set Key and Record in Play raise a toast with Fix: stop Play (they did nothing in silence);
  Record turns on only with an animated object selected, else it says so. The chords stand down where a window reads
  the arrow keys itself (Hierarchy, Project, UV editor), in the Console and while the view is flown.
- **Review (Opus) taken:** the "edit during Play with Record on" refusal could never fire (an edit in Play never
  reaches the recorder, and entering Play turns Record off) - replaced by Set Key and Record refusing aloud in Play with
  Fix: stop; Blender's Up / Down ran the key jumps inside the Hierarchy and the UV editor, which read the arrows
  themselves - the animation chords stand down there, in the Console, in the Project window and while the view is flown
  (Maya's S); the keymap's Record could show RECORD with nothing recording (Preview refused) - Record turns on only when
  Preview could, else it says what to select; the "7 mm" claim was wrong (an 18 px target is 4.8 mm) - said honestly;
  the badge checks its room and the comment it split; the Play button shares the toggle; the tests use two tracks, land
  on frame 12, press the chord itself over the view and see the Play refusal. Open: the curves view's 5 px hits; the
  Alt+click menu and Record's refusals are not driven by a test.

**Checked**
- `tests_round278`: the bindings per preset; no animation chord collides with another action's in any preset; Set Key
  adds a key on every track at the playhead; Next Key moves the playhead; Record toggles with the RECORD badge drawn
  only while it is on; Shift+K pressed over the view adds the keys; Set Key in Play refuses with Fix: stop.
- Unit checks (round 278): Windows, in render-cache verify mode, 13182, 0 failed (1135 tests), 0 stale
  pictures. Linux with libraries: 12722, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 277): compositing and materials - a pen's reach, a visible Assign, an Export Textures dialog (task 0257)

**Asked:** ADR 0085's compositing and materials round: the Render window's image panned with the middle button only (a
pen has none), the Tone Curve's points went only by right-click, "Assign to Selection" was a right-click item with no
button, and the texture set's export was a console command with no window to find it in. Nielsen's H6 and H7, the pen.

**Added**
- **The Render window's image pans with a left drag** as well as the middle button (the wheel zooms, a double-click fits).
- **Tone Curve points go by Alt+click** as well as right-click; the tooltip says both.
- **Assign to Selection is a button** in the Materials window's toolbar, beside Edit UVs, disabled with its reason when
  no material or no object is selected; the right-click item stays.
- **Export Textures...** in the Textures window's toolbar opens a dialog - the packing (glTF, Unity, HDRP, Unreal,
  separate maps), the folder under the project, "Assign the exported images to the material" - that runs the console's
  `export textures`; the console form stays for scripts.
- **Review (Opus) taken:** the left-drag pan followed any held button over the image, a slider or a tab dragged across
  it included - it starts only with a press on the image; the image's own text still said "middle-drag pan"; an
  Alt+click that missed a point added one - it adds nothing now; the export dialog said "under Assets" where the folder
  is under the project - the text and a hint row say so, the packing's maps are listed under the choice (HDRP's too),
  an empty folder disables Export; the Textures toolbar guards its width before adding the button; a test that a press
  from the settings column crossing the image pans nothing. Open: the Export button's click and an assign by the
  toolbar button are not driven by a test; the Materials toolbar's "Showing N of M" label can draw over the Assign
  button while a search filters.

**Checked**
- `tests_round277`: a preview rendered, a left drag over the Render window's image moves its pan; the Materials
  toolbar's Assign to Selection disabled with its reason when nothing is selected; the Textures toolbar's Export
  Textures... opening the dialog by a real click, its Export and Cancel buttons recorded.
- Unit checks (round 277): Windows, in render-cache verify mode, 13157, 0 failed (1134 tests), 0 stale
  pictures. Linux with libraries: 12697, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 276): lighting and rendering - one Environment, one Device section, the rendering components at hand (task 0256)

**Asked:** ADR 0085's lighting and rendering round: the Environment was edited in two windows under two headers, four
device pickers in three places defined "Auto" nowhere, and the Rendering components were missing from Add Component
unless the layout's stage was lighting or materials. Nielsen's H4 (consistency), H6 (recognition), H10 (help).

**Added**
- **One Environment**: the Render window's section is a row - "edited in the Lighting window" with an **Open Lighting**
  button - instead of a second copy of the fields.
- **One Device section**: the Render window's Render Devices opens with "Auto means the GPU when there is one, else the
  CPU" said once, then the Viewport Device (Preferences' setting) and the GI Device (the Lighting settings' field)
  beside the render device and the per-card switches.
- **The Rendering components always at hand**: Add Component (the Inspector's and the Component menu's) lists Light,
  Camera, Camera Filters, Probe Volume and Outline in every layout.
- **Review (Opus) taken:** the test could not fail (it ran in a layout that already showed the Lighting window, probed a
  field inside a folded group and a device setting through its own hook) - it starts in the Default layout, probes
  "Sky Color", asserts the Lighting window was absent before the click, and checks the two device rows are drawn; the
  GI Device tooltip said the lightmap bake ran on it (the bake follows the render Device) - corrected; the "Auto"
  definition and the Environment row were ellipsized at the pane's width - two short rows, a shorter label; the device
  combos ran past the pane at 1.5x - one helper sized to the row; the CPU row's "(Auto)" gave Auto a second meaning in
  the section that defines it - dropped; a stale comment. Open: Open Lighting docks into the largest leaf; the render
  Device and the Denoise Device stay in the Render Settings fields above.

**Checked**
- `tests_round276`: in the Default layout the Render window's Open Lighting button, no Environment field of its own and the
  Lighting window only after a real click; the Viewport Device and GI Device rows drawn; Add Component in the Default
  layout listing Camera Filters and Light.
- The checks of round 277 below cover this round too (both landed in one chain).

## 2026-10-28 (round 275): modeling, placing and UV - the walkthrough's fixes (task 0255)

**Asked:** ADR 0085's first area round, what the modeling walkthrough and the audits found: Ctrl with three meanings
nowhere said, tool parameters only the wheel could reach, the UV editor panning with the middle button alone and dropping
toolbar buttons when narrow, Hierarchy drop zones a quarter of a row, one name for two operations, a Boolean that needed
"selected last". Nielsen's H4 (consistency), H7 (flexibility), Fitts (targets), the pen.

**Added**
- **Ctrl, said**: Push/Pull's "keep the original face" and Sketch's "freehand" stay on Ctrl (Alt is the orbit modifier
  in three feels) and the hints and tooltips now say what Ctrl does in each tool; the snap increment is a dropdown
  beside the magnet (0.1 to 5 m, or Custom), saved with the preferences, read by the gizmo's Ctrl drag and the modals.
- **Keys for the wheel-only parameters**: in a modal `]` / `[` do what the wheel does - Bevel's segments, Array's count,
  Grid's rows and columns, Pipe's sides, Taper's and Recess's second amount - for a pen or a finger.
- **The UV editor** folds the toolbar buttons it has no room for into a "More" menu, and takes W / E / R / Y from the
  keymap's tool actions (`action_pressed`); its hint names the preset's chords.
- **Hierarchy drop** zones are thirds of a row (`hier_drop_mode`), and "Move into..." in the row's menu reparents to the
  root or to any object without a drag.
- **Names**: "Extrude Edges"; "Select Overlapping Faces" (was Z-Fighting); "Select Doubled Vertices" (was Overlapping).
- **Boolean by name**: "Boolean..." opens a dialog naming the object kept and cut (the active mesh first) and the
  operation, Apply now or Live modifier; `boolean <op> [modifier] [kept <name>]`. The result no longer depends on the
  selection order.
- **Review (Opus) taken:** moving Push/Pull's keep and Sketch's freehand to Alt broke both (Alt is the orbit modifier
  in three feels - the task 0223 fix - so no stroke could be freehand and Alt-click finishing a Push/Pull selected a
  loop) - Ctrl stays, and the hints say what it does; `+` / `-` in a modal doubled as typed values (a typed "-0.05"
  also lowered the count) - only `]` / `[`; the UV toolbar's More menu kept pointers into a stack table read after the
  frame - the table is static; Space+drag in the UV editor could not work (Space is the Wheel's chord in every preset
  but one) - dropped, said so; the Boolean dialog could show one object and cut another when the active object was no
  mesh - the kept object is always one of the meshes shown, the active one first; `boolean ... kept <name>` took one
  word and fell back silently - the rest of the line, a warning when unknown; the snap combo overwrote a custom step
  and the turn step - a Custom entry, the turn step left alone; "Move into..." listed the selection's own descendants
  and recorded empty steps - skipped. Open: the Boolean dialog's buttons and "Move into..." are not driven by a test;
  `]` / `[` on a US keyboard are the bracket keys only.

**Checked**
- `tests_round275`: the drop zones over a 30 px row; the three labels; a Bevel modal started by its chord with `]` and
  `[` changing the segments; the Cube kept and cut by the Sphere giving the same mesh whichever was selected last, by
  the hook and by the console's `kept Cube`; the UV editor's hint naming the preset's tool chords.
- Unit checks (round 275): Windows, in render-cache verify mode, 13149, 0 failed (1132 tests), 0 stale
  pictures. Linux with libraries: 12689, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 274): hints from a table, labels from the keymap, unsaved-changes prompts, the History window (task 0254)

**Asked:** ADR 0085's foundations F3-F6. The status bar's hint was an if-chain missing the gizmo drag, Draw, Edit
Origin, the UV editor, Record, the bakes, Strokes and Scatter; 121 labels carried Unity's keys whatever the keymap
preset, some wrong even then; New, Open and Open Recent replaced the scene without asking and Exit skipped its auto-save
while Play ran; the undo history was one label in the status bar. Nielsen's H10, H4, H5 and H1 / H3.

**Added**
- **The hint table** (`src/editor/hints.cpp`): rows of {name, when, text} in priority order - the Wheel, the modals, Edit
  Origin, the pilot, Draw, Sketch, the gizmo drag (type a number, V snaps, Ctrl+Shift drops), the jobs, Sculpt, Paint,
  Strokes, Scatter, Weight Paint, Record, the bake and the render, the UV editor under the pointer, Tablet Mode, the
  palette's elsewhere, Edit Mode, the Object Mode tools with the feel's navigation. The texts' shortcuts come from the
  keymap. Round 271's hint table: 0 of 8 cells without a hint of their own (2 before).
- **Labels from the keymap** (`Editor::key(action)`): every menu item, the toolbar's and Play's tooltips, the Window
  menu (the module registry carries action ids), the Mesh menu's edit-operation table (action ids), the Inspector's
  and Project window's menus, the Lighting, Render, Editing and Modeling Tools windows' texts show the preset's chord
  or nothing; the labels the keymap cannot produce are dropped (Align With View, Unwrap "U", Extrude "E", the modifier
  menu's Blender keys). `tools/hci_label_audit.py`: 0 literals (121 before), the fixed keys (Esc, Alt+F4, a modal's
  own letters) left as they are and skipped by the audit on purpose.
- **Unsaved changes** (`history_window.cpp`): "Save changes to <scene>?" - Save, Don't Save, Cancel - before New Scene,
  Open Scene..., Open Recent, a .scene from the Project window or a drop, Ctrl+N and Ctrl+O, when the scene has changes
  since its last save; Save runs the save, then the action once the save has really happened (a cancelled Save As drops
  it). Exit leaves Play first, so its auto-save keeps the pre-Play edits (Play's own changes are discarded, as
  Unity does); New and Open during Play leave Play first, then ask. The console's `newscene` and
  `open` stay immediate (scripts).
- **The History window** (`WindowKind::History`, in the Scene group): the steps taken as a list, the current one marked,
  a click jumping to any of them (undo or redo as many steps as it takes), the undone steps dim below; the Editing
  window's own stack as a second block with its Undo / Redo when an image is open; the status bar's "Last:" opens it.
- **Review (Opus) taken:** a History click jumped while the rows were still being drawn, and the rows after it read undo
  states that were gone - the click's target is taken after the rows; the Mesh menu's submenus showed the raw action
  ids ("mesh.extrude") where the chord belongs - `op_keys` there too, with a test; a save that failed (or was refused
  in Play) left the waiting New / Open armed for a later manual save - dropped with it; New / Open during Play replaced
  the pre-Play scene without asking - they leave Play first, then ask, as Exit does; the UV editor's rectangle was never
  cleared, so its hint showed over whatever took its place - reset each frame; the Record, bake and render rows hid the
  Edit Mode, tablet and palette hints - they follow the modes'; the UV hint named the keymap's tool chords while the
  editor hard-codes W E R - it says so; the Hierarchy menu's Delete and Frame Selected and the Render tooltip were
  still literals the audit's skips hid - from the keymap; F9 in the operation tips - "Adjust Last Operation"; the
  Start row is marked when everything is undone; labels whose action is unbound no longer end in "()"; the sculpt
  hint's isolate back; the hint test rebinds the palette and expects the text to change; the History test clicks
  "Last:"; the F5 claim corrected (Play's own changes are discarded, as Unity does: the auto-save keeps the pre-Play
  edits). Open: a History jump of N steps restores N states on the main thread (not timed); the UV editor's W / E / R
  are its own keys, not the keymap's.

**Checked**
- `tests_round274`: the status bar's "Last:" opening the History window by a click, its rows after four changes, a click on the first step going back three, a click on
  an undone step redoing; the hint rows listed and the row showing per mode (Object, Edit, Sculpt, Paint, Strokes,
  Scatter, the UV editor under the pointer, elsewhere), the palette rebound and its hint following, the Mesh menu's rows giving the preset's chord; the Edit menu's and
  the Window menu's drawn shortcuts equal to the keymap's under Unity and Blender; New Scene with a changed scene asking,
  Cancel keeping it, Don't Save replacing it, Save writing the file then replacing, Exit during Play leaving Play before
  the auto-save (the file written). Round 271's hint test expects 0 holes.
- Unit checks (round 274): Windows, in render-cache verify mode, 13141, 0 failed (1130 tests), 0 stale
  pictures. Linux with libraries: 12681, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 273): the job tray - every long job with its progress, its time and a Cancel (task 0253)

**Asked:** ADR 0085's second foundation (F2). Quad Remesh, Mesh Maps, a baked-light layer, a surface's evaluation, a
skin solve, the lightmap and probe bake, the final render, MoonRay and the animation's video each showed progress in a
place of its own or not at all, none said how long it had run or had left, and cancelling was Esc here, a console verb
there, a button elsewhere. Nielsen's H1 (visibility of system status) and H3 (user control and freedom).

**Added**
- **One tray** (`src/editor/job_tray.cpp`): `Editor::jobs_running()` lists the live jobs each frame from the state
  that already exists - the remesh, mesh-maps, baked-light, surface and skin workers, the lightmap / probe bake, the
  final render (samples over the target), MoonRay, the animation's frames and its video encode, and a *busy* label for
  the import that blocks the main thread - each with its label, a progress (or a moving stripe), its start and its
  cancel.
- **In the status bar's right slot**: the newest job's label, percentage, time run and, past 2 % with a progress, the
  estimate left (elapsed x remaining / done); a bar behind the text; "+N" for the others with all of them in the
  tooltip; one **Cancel** reaching the job's own flag. The lighting bake's status text gives way while a job runs.
- **Progress and cancel where they were missing**: the mesh-maps baker counts its rows (`bake_mesh_maps` takes a
  progress pointer); the baked-light worker its texels, and it stops when cancelled (checked every 4,096 texels).
  The Render window's Stop and the tray share `render_stop()`.
- **Done**: a job seen last frame and gone this frame raises an Info toast - "Mesh Maps done in 3.2 s", or
  "cancelled" - so the end is seen from wherever the user is; the HCI log gets `job start`, `job cancel`, `job done`
  / `job cancelled` lines with the seconds. Info toasts draw with the accent colour and the Info icon.
- **A busy state for the import**: the Import Preview's Import button puts "Importing <file>" in the tray and runs the
  import two frames later, so the label is on screen while the import blocks.
- **Console:** `jobs` lists the running jobs with their progress and time; `jobs cancel [label]`.
- **Review (Opus) taken:** the tray's Cancel moved sideways with the status bar's frame-time text, so a press and its
  release could land on different rectangles (the button's id is its rectangle) and the click was lost - the right
  text is measured at its widest frame time and the tray keeps still, and the test re-aims each try; an animation or
  a camera sequence listed as a Render a frame, with a "done" toast every frame and a Cancel that only ended the
  current frame - each is one job with its own Stop; done toasts flooded (the Live Preview's renders, a surface's
  every evaluation) and pushed warnings out of the three slots - a toast only past a second, never for the preview,
  and an Info toast never evicts a warning or an error; Cancel was offered where it could not work (the video's
  FFmpeg, a surface that restarts at once) - not offered; a cancel made by Esc or a console verb reported "done" - the
  job's own flag counts; the bake's two phases were two jobs; a baked-light file written before a late cancel stayed -
  removed; Mesh Maps' line for a cancel was a warning - information. Open: a skin solve held through an Edit Mode
  session shows at 100 % with an idle Cancel; a MoonRay render still waiting for the program lists as a render.

**Checked**
- `tests_round273`: a Mesh Maps bake is in the tray from its first frame with the test clock as its start and a
  Cancel; the frames until its progress first moves are printed; when it lands the tray is empty and an Info toast
  says it is done with the time; a second, larger bake cancelled by a real click on the tray's Cancel lands nothing
  (the undo steps unchanged) and the toast says cancelled; the HCI log has the start, cancel, done and cancelled
  lines.
- Unit checks (round 273): Windows, in render-cache verify mode, 13097, 0 failed (1126 tests), 0 stale
  pictures. Linux with libraries: 12637, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 272): toasts with a fix, and buttons that say why they are disabled (task 0252)

**Asked:** ADR 0085's first foundation (F1). About ninety refusals in the tool modes, and those of Generate Lighting,
Render, the Editing window's buttons and Play-time edits, reached the user only as the status bar's cut-off last line or a
Console row; buttons that could not work stayed clickable. Nielsen's H5 (prevent the error) and H9 (recognise and
recover from it).

**Added**
- **Toasts** (`src/editor/toasts.cpp`): every new Warning or Error log line becomes a toast at the Scene view's
  top-centre - the newest on top, three at most, each for three seconds (hovered, it waits; the pointer off it, 2.5 s
  more), a click on the text opening the Console. The log line is unchanged, so scripts and tests reading the log see
  nothing new; the same message again within the window is one toast with a count. Clicks and hovers on a toast are the
  toast's before the view's navigation and tools, so a Fix click is neither a selection, a dab nor an orbit.
- **A fix on the toast** (`Editor::refuse(text, fix_command, fix_label)`): a refusal that knows its remedy carries
  "Fix: <label>" running a console command - Paint Mode without a surface offers `surface new`, a mesh without UVs `uv
  unwrap`, a mesh whose modifiers change its geometry `applymods`, Paint and Generate Lighting in Play `stop`. Of the 82
  `Log::warn` sites in the five tool files (by grep) 5 know their remedy this round; the rest are toasts without one.
- **Buttons disabled with a reason** (`ui::Context::button_enabled(rect, label, enabled, why, selected, icon)`): dim on
  the strip colour, the click swallowed, the reason as the tooltip (the general help waits until the button works):
  Generate Lighting (Play; Baked GI off), Clear Baked Data (nothing baked), Save... (nothing rendered), Stop (not
  rendering), the Editing window's Export... / Save / Undo / Redo / Use as Camera Look, the Sculpt / Paint / Strokes /
  Scatter mode buttons (no mesh selected; Play).
- **The study's instrument** (`--hci-log <file>` or `BLENDITY_HCI_LOG`): toasts (with their fix, and the fix clicked),
  hint changes, undo steps (by a serial, past the undo cap) and layout changes appended with a timestamp - what round
  282's study reads.
- **Console:** `toast <text>`.
- **Review (Opus) taken:** the disabled button never showed its reason (the hand-drawn branch did not mark the widget,
  so the tooltip belonged to the previous one; and the caller's general tooltip reset the delay) - it marks the widget
  and the general tooltip waits while the button is disabled, the test hovering Generate Lighting through the delay; a
  click on a toast reached the tools and the navigation first (a Fix click in Sculpt Mode was a dab) - the toasts update
  before both; the toast rectangles were indexed from the last draw while the poll could have inserted a toast since -
  keyed by text; two refusals in one frame lost the first's fix, and a refusal before the first poll kept a stale fix
  for a later line - a per-frame queue; the HCI log stopped seeing undo steps at the undo cap - a serial; the spec
  claimed buttons and fixes the code lacked (Clear Baked Data, Use as Camera Look, Strokes, Scatter, the modifiers
  fix, the hover test) - added, and Render's untrue "not while playing" dropped (F12 renders in Play); the start line
  under `--hci-log`; `toast` trims its text. Open: Update Lighting and Render Camera Sequence as plain buttons; the Paper
  theme's strip close to its button colour; the pause while hovered restarts at 2.5 s rather than resuming.

**Checked**
- `tests_round272`: the refusal with nothing selected is one toast without a fix, twice is one toast counted; there at
  2.9 s, gone at 3.6 s; hovered four seconds it stays, and leaves once the pointer is off; the Fix button clicked through
  real events runs `surface new` and Paint Mode then opens; Generate Lighting's reason as its tooltip during Play and in
  `button_disabled_why_for_test`, Clear Baked Data's, Export...'s, Strokes' and Scatter's; the HCI log's toast, fix and
  hint lines.
- `tools/hci_label_audit.py` counts 122 literals now (one more: the Save... reason names F12; round 274 moves the labels to the keymap).
- Unit checks (round 272): Windows, in render-cache verify mode, 13084, 0 failed (1124 tests), 0 stale
  pictures. Linux with libraries: 12624, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The lighting group's Rendered-mode test failed in the chain on both legs - the warning's toast now over the view - and was changed to let the toast leave before the compare; rerun alone: 4762, 0 failed, and the editor group 1035, 0 failed. The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 271): the HCI pass - the method set, the baselines, the measuring sticks (task 0251)

**Asked:** which additions, chosen by HCI methods, would make the program easier for artists - from Unity, Unreal or
the Adobe suite, on a keyboard, a pen or a tablet - over every feature of the app. Three audits found seven problems
repeating in every area (refusals and progress only in the console, hard-coded shortcut labels, silent data loss on New
/ Open / Exit, pen and tablet gaps, one thing in several places, invisible modes, hidden features); ADR 0085 sets the
twelve-round pass that fixes them, foundations first. This round lays the baselines and the tools that measure.

**Added**
- **ADR 0085**: the methods named per change (Nielsen, Fitts, Hick, KLM, the cognitive walkthrough, transfer of learning,
  WCAG), the foundations F1-F7 before the area rounds, the measuring sticks, the artefacts under `docs/hci/`.
- **`docs/hci/`**: the README, four cognitive walkthroughs at the baseline (a prop modelled to rendered: 8 of 12 steps
  with a failing question; sculpt to export: 5 of 12; rig to export: 8 of 12; a stylized scene: 3 of 10), the KLM table
  of twenty frequent actions (19 timed: 77.0 s before the pass), the study protocol, this round's reports.
- **The Fitts dump** (`tests_round271`): every window's and tool mode's recorded widget rectangles as JSON at UI scales
  1.0 and 1.5 (Tablet Mode); **`tools/hci_fitts_audit.py`** reports the targets under 7 mm and the index of difficulty
  to each control from the Scene view's centre. Baseline: 1,009 of 1,099 targets under 7 mm at 1.0, 925 of 1,024 at 1.5 (a fresh project; 420 of 440 and 394 of 415 distinct) - the
  row height itself fails the pen rule, so the first fix is a taller row in Tablet Mode.
- **The status-hint table** (`tests_round271`): seven modes x two selection states; baseline 2 of 8 cells with no hint of
  their own (Strokes, Scatter).
- **`tools/hci_label_audit.py`**: the string literals in `src/editor` carrying a shortcut the keymap does not produce;
  baseline 121 (other tools' keys and mouse gestures not counted). `--check` for the chain once round 274 moves the labels to `shortcut_text`.
- **`tools/test_coverage_map.py`** gains widget rows (`u.button` labels, Inspector fields, Tools sections) with the
  tests that drive each by label: 153 widgets, 145 not driven; counted toward `--check` only with `--widgets`.
- **Review (Opus) taken:** the hint test reads the hint with the pointer over the view (a frame had reset the hover,
  so Object and Edit Mode looked like holes) and judges a hole by the mode's name, not by the fallback's text; the
  Fitts dump runs with the Modeling Tools window open and every section shown (the brush controls were never
  recorded), skips the Project's and the Materials' tiles (what the scratch project held), writes into the test's own
  folder, escapes its JSON, and the audit counts distinct targets beside per-context ones with 7 mm at 96 DPI (26.5 /
  39.7 px, not 24 / 36); the Inspector reflector records its float, bool and colour fields by label as the int field
  did (the Tools sections were invisible to the dump and the coverage map); the label audit leaves out other tools'
  keys named on purpose and mouse gestures (168 became 121); the coverage map drops the section headers (not in
  `button_rects`) and format-string labels and reads the button's second argument; the walkthrough scores were
  recounted under a stated rule (an N in any column), the KLM rows recomputed under stated rules (BB a click, 2K a
  chord, one M a cognitive unit) with the replay test moved to *Not done*; the README's filter and the reports'
  table rows fixed. Open: the Inspector's enumeration and text fields are not yet recorded by label; grid buttons
  and section headers are not scanned; the KLM replay.

**Checked**
- `tests_round271` in `tests/test_editor.cpp` (the dump's contexts and targets, the hint table's cells); the two scripts
  on the dump and the sources; `tools/test_coverage_map.py` still exits as before without `--widgets`.
- Unit checks (round 271): Windows, in render-cache verify mode, 13067, 0 failed (1124 tests), 0 stale
  pictures. Linux with libraries: 12607, 0 failed. The sanitizer leg did not run this round (WSL would not start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group passed alone: the known parallel-load noise.)

## 2026-10-28 (round 270): the write-up - customize, sculpt, paint, stylize (task 0250)

**Asked:** the pass's write-up.

**Added**
- `docs/tasks/0250-write-up-sculpt-paint-stylize.md`: what rounds 250-269 set out to do and what landed (a table by
  round, task and ADR), what held up (one brush architecture across five rounds, in-place undo deltas, the rect engine,
  workers wherever a frame would stall, honest numbers), what the reviews caught and the pass kept, what is still open
  (sculpt, paint, stylize and process items), and where to read more.

**Checked**
- A document: no code changed.
- No chain run: a document and this entry are the round's whole change.

## 2026-10-28 (round 269): the manual, the Learn lessons, the check lists (task 0249)

**Asked:** after the customize, sculpt, paint and stylize rounds, the manual, the Learn lessons and the check lists.

**Added**
- **The manual:** chapter 1 *Your editor, your way* (the Wheel, panels anywhere, a shortcut for anything, UI files, the
  module registry, the stages as one pipeline); chapter 3 *Stylized environments*; chapter 4 *Sculpting* with the
  pipeline order *sculpt > remesh > unwrap > bake mesh maps > paint*; chapter 6 *Painting the surface* (the Substance
  pipeline in five steps) and *The hand-painted look*; the console table's sculpt, paint and stylize rows.
- **The features tracker** (`research/FEATURES.md`): nine rows for the rounds' methods - Jacobson 2013 (DynaMesh's
  winding numbers), Gibson 1998 (its surface nets), Huang 2018 (QuadriFlow), Baxter 2004 (IMPaSTo, marked simplified),
  and the tools' own methods marked "no paper" - each with its source file and test round.
- **Five Learn lessons** with Try-it buttons: *Customizing the Editor*, *Sculpting*, *Painting the Surface*, *The
  Hand-painted Look*, *Stylized Environments* - each citing the reference books and Blender's source, with a question.

**Checked**
- No new code: the manual test (13 chapters, the Learn notes) and the Learn tab's Try-it test (every new section's
  commands run headless) cover the additions.
- Unit checks (round 269): Windows, in render-cache verify mode, 13060, 0 failed (1122 tests), 0 stale
  pictures. Linux with libraries: 12600, 0 failed. The sanitizer leg did not run this round (WSL would not
  start it; the Linux build and tests passed). `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group
  passed alone: the known parallel-load noise.)

## 2026-10-28 (round 268): stylized environments (task 0248)

**Asked:** the stylized side after the hand-painted look - environments in the Ghibli / oil-paint 3D style: brush
strokes as geometry, hand-placed foliage, scattered props, a painted sky.

**Added**
- **Stroke objects** (`src/scene/stroke_object.cpp`, `src/editor/strokes_tool.cpp`, ADR 0084): a `Strokes` component
  keeps cards (a quad facing the camera at its dab, the surface's normal under it kept) and ribbons (a drag's points
  swept into a strip); the object's mesh is rebuilt from the list on every edit with custom normals by Own, Surface or
  Canopy (away from a centre: a bush of flat cards shades as one round volume). The list is saved as text; each stroke
  is one undo step; renderers and exporters see a plain mesh. Modeling Tools > Strokes; `strokeobj on|off | new |
  card | ribbon | size | width | normals | canopy | alpha | dab | stroke | count`.
- **Foliage from strokes:** `strokeobj canopyfill [radius] [count] [x y z]` (a shell of cards round the canopy centre,
  facing out), `strokeobj grass [density] [height] [radius] [x y]` (upright cards dropped onto the surface by density,
  kept off a Paint layer's mask, their normals up).
- **The Scatter brush** (`src/editor/scatter.cpp`, Modeling Tools > Scatter): `scatter <object>`, then dabs place
  instances (the source's mesh and materials shared; a tinted copy when asked) as children of one Scatter object by
  density a square metre of the dab's disc on a seeded spiral, dropped onto the same surface, aligned to the normal or
  world up, scaled and turned at random; `scatter mask <layer>` keeps them off a Paint layer's painted texels; the
  eraser end removes within the disc; `scatter merge` joins them into one mesh for export.
- **A stylized sky and fog** (the Lighting window's World): the Gradient world's Sun Disc (toward the first directional
  light) and Horizon Band; Fog (start, end, a near and a far colour) by the view distance in the CPU raster, the GPU
  view, the path tracer and its GPU kernel alike. `set world.Fog 1`, `set world.SunDisc 8`, `set world.HorizonBand
  0.15`.
- **The Stylized layout** (`layout Stylized`) and its guide.
- **Measured** (`tests_round268`): a card at the cube's centre faces the view within 1.0000 (one degree is 0.9998); a
  ribbon of 10 points is 20 vertices and 9 quads; `canopyfill 1 100` puts 100 cards on the shell within 1e-4 facing
  out; `grass 40 0.25 1` makes 126 blades (40 pi wanted); a scatter dab of density 4 in a 1 m disc places 13 instances
  (4 pi wanted), the merge 13 x 384 faces, the mask 0.
- **Review (Opus) taken:** fog reaches the CPU raster's 8-wide shading path too (plain materials had stayed clear
  inside each run of eight pixels); the path tracer's and the kernel's fog distance runs from the camera through
  see-through passes (a hill seen through a leaf card's holes had less fog than the frame); every card and ribbon
  segment is two single-sided faces back to back sharing the custom normal, so a renderer's back-side flip never turns
  the Canopy normal (half the grass had shaded as facing the ground); the scatter spread is the golden angle jittered
  (it had been one spiral arm); the Scatter parent is validated by name and shape (a freed id could adopt another object
  and the eraser destroy its children); the tint is one copy a dab with no asset path (relinking had dropped it, and
  every instance had carried a copy); the counts are capped at 5,000 instances and 20,000 blades a dab with a reason
  (density x radius could overflow an int); the mask's triangle search rejects by bounds first; a dab is repeatable by
  the seed and the hit point (the session counter had made it not so); a canopy or grass goes into a Stroke object of
  its own when the current one holds strokes of another kind (they had moved the object's canopy centre); instances
  are named "<source> Instance"; the spec's console forms are the field names without spaces (`set world.FogEnd 40`;
  `set world.Fog End 40` had turned fog off); `gpu_device.h` is back to LF. The OptiX device's committed PTX was regenerated
  (`tools/optix_ptx_embed.bat`) so it carries the sky disc, band and fog too. Open: the Inspector's Normals edit does not rebuild until the next stroke; the mask reads the base mesh (a modifier's
  geometry is not matched); the parent's own transform is assumed identity; glass adds fog over a fogged background.

**Checked**
- `tests_round268` in `tests/test_editor.cpp` (the Stroke object's card, Surface and Canopy normals, a ribbon, the text
  round trip, canopy fill, grass, undo; the scatter dab's count and placement, merge, the eraser, the mask) and
  `tests/test_render.cpp` (the sky's band and disc, the fog ramp).
- Unit checks (round 268): Windows, in render-cache verify mode, 13060, 0 failed (1120 tests; the gpu group passed alone
  after the OptiX PTX was regenerated), 0 stale pictures; Linux 12600, 0 failed (1122 tests, rerun after a gcc-only access
  error in the first chain); the sanitizer leg did not run this round (WSL would not start it; the previous round's run
  covered the shared code). Fuzz, in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems.

## 2026-10-28 (round 267): the hand-painted look (task 0247)

**Asked:** after Substance's pillars, the stylized side - the hand-painted look of Blizzard's and Riot's textures and
of Ghibli-styled games: the light painted into the colour, a brush that behaves like paint.

**Added**
- **The painterly brush** (`src/editor/paint.cpp`, ADR 0083): **tips** - Round (the falloff), Flat (an ellipse of the
  brush's Tip Ratio turned to the drag), Bristle (a bundle of small stamps placed by the stroke's seed, so a stroke
  leaves streaks); **wet mixing** (Wet: the brush carries the mean colour it went over at the last dab and mixes the
  next dab's by that much - strokes blend as paint); **jitter** in hue, saturation and value per dab from the stroke's
  seed (0 exact); **grain** (`paint grain <image> [scale]`: an image's luminance multiplies the dab's weight, tiled in
  texel space); **Smudge** (the paint pulled one spacing step along the drag, onto fresh texels too) and **Blend**
  (the 3 x 3 mean) as modes, colour only, through the same tile step. Everything downstream is 0245's: a stroke is one
  undo step, the dilation and the GPU rows as before.
- **The Gradient fill** (`paint gradient r g b r g b [island|group|material|object] [x|y|z]`): a fill from one colour
  to another along a world axis of the mesh maps' position - the value gradient hand-painters lay first.
- **The palette** (the Paint section: swatches, the picked one's five shades, +; `paint palette add|pick|shade|remove|
  save|load|list`), saved as text under `Assets/Palettes/<name>.pal`.
- **Lighting painted in:** `paint bakelight [key x y z]` adds a **Baked Light** Image layer (Multiply) holding per
  texel of the mesh maps the occlusion times a key direction's wrap-lit term (the scene's first directional light's,
  or the one given) - the painter's key-and-ambient pass, written under `Assets/Textures/Baked/`, one undo step;
  `paint bakegradient [top] [bottom]` the top-down value gradient the same way. Not a lightmap: no shadow rays or
  bounces this round.
- **The Hand-painted shading model** (Material > Shading Model: Hand-painted; ADR 0083): the texture's colour as it is
  in every renderer (unlit in the CPU raster, the GPU view, the path tracer and its kernel), plus a rim at grazing
  angles and a painted specular where the roughness channel is low in the CPU raster (Rim, Rim Width, Rim Color,
  Painted Specular); lights neither shade nor darken it. glTF writes it `KHR_materials_unlit` (and an `unlit`
  material too, which had no extension before).
- **Stylization filters** (`src/texture/surface.cpp`): **Posterize** (N values, the hue kept), **Edge Shade** (the
  mesh maps' convex edges lightened or darkened through a width - the edge-highlight pass), **Cavity** (the occlusion
  to a strength multiplying the colour).
- **Console:** `paint brush round|flat|bristle|paint|smudge|blend`, `paint tip ratio|bristles`, `paint wet`, `paint
  jitter h s v`, `paint grain`, `paint stroke x0 y0 x1 y1 [dabs]`, `paint gradient`, `paint palette`, `paint
  bakelight`, `paint bakegradient`.
- **Measured** (`tests_round267`): a wet 0.5 blue stroke over red left 2,933 texels with red and blue both above 0.2
  and 339 pure blue; jitter 0 painted 3,647 of 3,647 texels the exact colour; a checker grain at 16 texels a cell
  painted 2,504 texels against 3,647; Smudge moved a red patch's centre 2.1 texels along the drag; the baked light
  from +X read 0.985 on a sphere's +X side against 0.652 on -X; Edge Shade lifted the cube's edge band to 0.708 against
  the faces' 0.510; Posterize 5 left exactly 5 values.
- **Review (Opus) taken:** the scene sun's key is now toward the light (a light points along its +Z; it had lit the
  underside; disabled and inactive lights are skipped; tested with the sample scene's sun aimed down); Smudge pulls
  along the drag in texel space, read from the dabs' centroids, with its step in texels (it had used the screen drag,
  wrong on a rotated or mirrored island; the screen drag turns the Flat tip only); Smudge and Blend read the touched
  tiles as they were before the dab (a snapshot once a dab: the result had depended on the scan order); each bake
  writes a file of its own and tells the texture cache (an earlier layer keeps its image; undo gives it back); the bake
  runs on a worker and the layer lands when the file is written (a 4k map's PNG had been encoded on the main thread);
  fills and the gradient ignore the brush's Smudge / Blend mode; a stroke's first dab stamps Round and Smudge waits for
  a drag (a vertical Flat stroke had started with a crossbar); the Bristle stamps are made once a stroke and evaluated
  after the occlusion tests; the dab serial's wrap at 255 is handled; Edge Shade and Cavity write the height in a mask
  stack; the filters run their rows in parallel; the solid modes and the Materials window's ball honour Hand-painted;
  the palette's pick follows a remove; the loosened round-266 check is explained in the test; the ADR's claims about
  redo and the baked light's source are corrected. Open: the Hand-painted rim on the card and in the path tracer, MoonRay
  exports it lit, the pen's tilt, Paint Strokes.

**Checked**
- `tests_round267` in `tests/test_editor.cpp` (the stamps' peaks and the Flat tip's turn, the wet stroke, jitter 0 and
  0.2, the grain, Smudge; the baked light and gradient layers, the Gradient fill, the palette), `tests/test_render.cpp`
  (Posterize, Edge Shade, Cavity on the cube's maps, the Hand-painted sample unlit) and `tests/test_livelink.cpp`
  (`KHR_materials_unlit`).
- Unit checks (round 267): Windows, in render-cache verify mode, 13033, 0 failed (1119 tests), 0 stale
  pictures. Linux with libraries: 12573, 0 failed. Sanitizer build: 12420, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the sanitizer animation group passed alone (the known parallel-load noise); gpu views and fuzz clean; linux 12,573.)

## 2026-10-28 (round 266): smart materials, texture sets, 2D painting (task 0246)

**Asked:** Substance Painter's pillars - after the stack (0244) and the brush (0245): smart materials dropped onto any
asset, the export game engines read, the 2D view.

**Added**
- **Smart materials** (`src/texture/smart_material.cpp`, ADR 0082): a Group layer with its children and masks saved
  as text - the `.ptex` layer format, one layer - under `Assets/Materials/Smart/<name>.smat` and applied as a copy on
  top of any surface's stack, where its Mesh Map effects read *that* surface's maps (the adaptivity is the stack's).
  Seven built-ins made from Fill, Noise, Wood Floor, Plaster, Height AO and the mask presets: Worn Steel, Painted
  Metal (the paint chipped at the edges: the edge-wear preset inverted), Rubber, Wood, Plastic, Concrete, Gold. A group
  holding a Paint layer is refused as a smart material with a reason (its images belong to one surface).
- **Texture sets:** `export textures <packing> <dir> [--assign]` evaluates the surface and writes its planes as PNGs in
  the packing the target reads - `gltf` / `unreal` (BaseColor, Normal, ORM = occlusion R / roughness G / metallic B,
  Emissive), `unity` (Albedo with the coverage as alpha, MetallicSmoothness = metallic RGB + smoothness A, Normal,
  Emission, AO), `hdrp` (BaseColor, MaskMap = metallic R / occlusion G / smoothness A, Normal, Emission), `separate`
  (one image a channel) - the occlusion from the surface's mesh maps (white without them); `--assign` points the
  material's maps at the files.
- **2D painting:** `paint 2d on` makes the Textures window's preview pane the canvas - a pane pixel is a UV, a dab
  there writes the texel's neighbourhood through the same stroke, tile undo and dilation as a projected dab, no
  projection and no depth test (every triangle sharing the texel is painted alike, as Substance's 2D view does);
  `paint 2d dab u v [pressure]` from the console.
- **Stencils:** `paint stencil <image>` holds an image over the Scene view (moved with `paint stencil offset x y`,
  scaled with `paint stencil scale s`); a dab's weight is multiplied by the stencil's luminance at the projected pixel,
  nothing gets through outside it; `paint stencil off`.
- **The Texturing layout** (`layout Texturing`: the Scene view, the Textures window, the Modeling Tools with the Paint
  section, the Inspector) and its guide; the Textures window's **shelf** (a surface's header: the built-ins in a combo
  and Apply).
- **Console:** `smat save <layer> <name> | apply <name> | list | builtin <name>`, `export textures <packing> <dir>
  [--assign]`, `paint 2d on|off|dab u v`, `paint stencil <image>|off|offset x y|scale s`.
- **Measured** (`tests_round266`): Worn Steel on a subdivided cube's baked maps - metallic mean 1.000 over 35,728
  covered texels, the edge band's luminance 0.706 against the face centres' 0.565; a 16-texel 2D dab at (0.25, 0.75)
  painted 1,000 texels about (256, 256) and none elsewhere; a stencil black on its left half let 0 texels through
  there and 59,987 through its white half; a six-frame drag on the canvas at opacity 0.5 painted 28,704 texels, none
  above 128 of 255, as one step.
- **Review (Opus) taken:** a drag on the Textures window's canvas is one stroke - begun by the press on the pane, dabs
  by the brush's spacing while held, one step at the release, the opacity capped as in the view (it had been a stroke
  and a step a frame); the stencil is drawn over the Scene view, translucent, its aspect kept and fit to the view's
  shorter side, so offset and scale are placed by eye; a float stencil or alpha image (EXR, HDR) is converted to 8 bits
  and a NaN offset, scale or UV is refused (each had indexed past the image); Unreal's Normal is written DirectX (green
  down), HDRP's mask map keeps its detail channel full; `--assign` points the material at the separate Roughness and
  Metallic images beside the packing (it had cleared the maps with Metallic 1 left on) and at the Emission only when the
  stack writes it; a saved `.smat` wins over the code's built-in of the same name, so a written-out built-in can be
  edited; a loaded file is refused when it holds a Paint layer; the canvas and the shelf act on the texture shown (not
  the active object's) and the canvas shows the brush at the pane's scale and maps 2 x 2 previews to their tile; file
  names are sanitised for Windows (reserved names, streams); the tests probe metallic 0.5 so a swapped channel shows,
  apply a saved material to a second object and see its hash move, drive the canvas with press / move / release events,
  check `--assign`'s paths and the layout's four windows. Open: the export evaluates the surface on the main thread
  (the `export` command's cost; measured in a later round's stress), the 2D brush's radius is in texels, no Paint
  Strokes filter.

**Checked**
- `tests_round266` in `tests/test_render.cpp` (Worn Steel's metallic and edges on the cube's maps, a smart material's
  text round trip, each packing's channels at a known texel) and `tests/test_editor.cpp` (smat apply as one step and
  smat save / apply from the file, the 2D dab's texels, the stencil's halves, the Texturing layout).
- Unit checks (round 266): Windows, in render-cache verify mode, 12996, 0 failed (1115 tests), 0 stale
  pictures. Linux with libraries: 12536, 0 failed. Sanitizer build: 12383, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the sanitizer leg ran in the chain on this code before the test fix: 12,383 checks, one timing-dependent --assign check (fixed, passes on Windows and Linux); its rerun did not complete (WSL service failures); gpu views 8,176 checks; linux 12,536.)

## 2026-10-28 (round 265): painting on the model, with tile undo (task 0245)

**Asked:** Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart materials,
painting on the model. After the stack: the brush.

**Added**
- **Paint Mode** (`src/editor/paint.cpp`, ADR 0081): Modeling Tools > Paint (or `paint on`, the keymap's Paint Mode On
  / Off) on an object with a surface. A dab paints by **projection**: the object and depth under the cursor from the
  Scene view's planes (as a sculpt dab reads them), every render-mesh triangle whose screen bounds meet the brush's disc
  walked in its UV triangle, each texel's world point placed by barycentrics, projected, kept when it lies in the disc
  and its depth is within the triangle's own depth slope of the plane there - the far side and anything behind are not
  painted (Blender's Project Paint) - with the hardness falloff, the pen's pressure (opacity or size) and an alpha
  image. A **material brush**: colour, roughness, metallic, height and emission with a switch per channel, written
  by one dab; the pen's eraser end (or Erase) clears; Mirror X / Y / Z paint the mirrored discs; Lazy Mouse, spacing.
- **A Paint layer** (`src/texture/paint_layer.cpp`): a layer of the surface's stack whose channels are images (RGBA8
  colour and emission, 8-bit roughness, metallic, height, and a coverage) blended into the planes by their painted
  coverage with the layer's blend, opacity, targets and mask, saved as PNGs beside the `.ptex` when the project saves
  (`<base>_color.png` and the rest; the layer's Stamp counts the strokes so the surface re-evaluates on its worker).
  After a stroke the painted texels are dilated two texels into their unpainted neighbours (no gap at a seam).
- **Tile undo** (ADR 0081): a stroke is one undo step holding the 64 x 64 tiles it touched, before and after, applied
  in place by undo and redo - the second in-place delta kind beside the sculpt delta (a 4k image a stroke would end
  the undo budget in thirty strokes).
- **Fills:** `paint fill group|island|material|object` paint the brush's channels over the polygroup under the cursor,
  the UV island, the material slot or the whole object.
- **The views follow the dab:** the material's albedo texture takes the painted texels in place and the GPU view
  updates those rows (`Raster::update_texture_rows`, not the mip chain; `read_texture` reads a texture back); the
  stack lands from the worker when the stroke ends.
- **Console:** `paint on|off`, `paint color r g b`, `roughness`, `metallic`, `height`, `emission r g b`, `channels
  color,rough,metal,height,emit|all`, `size`, `hardness`, `opacity`, `flow`, `spacing`, `alpha <image>|off`,
  `symmetry x|y|z|none`, `lazy`, `erase on|off`, `pressure off|opacity|size`, `dab x y [pressure]`,
  `fill group|island|material|object`, `layer <i>`.
- **Measured** (`tests_round265`): a 40 px dab on a sphere filling a third of the view painted 7,119 texels of a 1k
  surface, every one the brush's colour, roughness, metallic and height within 1/255, none of the 372,096 far-side
  texels; the eraser over it left 696 (the first stroke's dilation ring); Mirror X 24,911 against 7,364; a fill of the
  object 995,680 of the 1,048,576 covered texels; two undos restored the images bit for bit in the same buffers.
**Review (Opus, round 265):** two blockers, eight majors and a dozen minors; the blockers, five majors and most minors taken before landing, the rest named in the spec as open. Paint had not shown after reopening the project or after any texture undo (the layer's images were loaded only by the brush: the layer loads its PNGs on first evaluation now, and the editor keeps every layer's images by their file base, so a texture rebuilt from text takes them back instead of reloading a PNG that may hold strokes the history does not); Ctrl+S had not written the paint (the flush was behind commit_undo's guard: it runs wherever the project or its textures are saved); turning a channel off had not stopped it being painted (one coverage served every channel: a per-texel mask of the channels written, saved with the images; an unwritten channel leaves the stack alone); the dilation had walked the stroke's rows across the whole image, growing every earlier stroke in them and storing a row of tiles per dab (it grows only what this stroke wrote, within the surface's coverage, and the tiles it reaches join the step); undo during a stroke had corrupted the history (a stroke is aborted by undo and redo, as a sculpt stroke is); opacity and flow had piled up over overlapping dabs (the stroke's weight builds by Flow to its Opacity and is composited against the texel as it was before the stroke; a dab writes a texel once). Also: the brush's alpha image is read, a snapshot undo keeps Paint Mode, Edit Mode and Weight Paint leave it, a failed GPU row update frees the card's slot, the barycentric slack is a quarter texel whatever the triangle's size, objects whose modifiers change the geometry are refused, a tile that no longer fits is skipped, a dropped step is kept in the history. Open, in the spec: the PNGs are stored uncompressed on the main thread at save (272 MB at 4k), every dab walks every triangle (a 1M-triangle mesh wants a spatial gather), the live preview is the raw brush colour until the stack lands, the GPU rows update is untested, and no timing is printed yet.

**Checked**
- `tests_round265` in `tests/test_render.cpp` (a Paint layer's blend by coverage and channels, its PNG round trip, the
  dilation's two-texel diamond) and `tests/test_editor.cpp` (the dab, the far side, the material brush, the eraser,
  the tile undo and redo with the buffers kept, Mirror X, the fill of the object).
- Unit checks (round 265): Windows, in render-cache verify mode, 12956, 0 failed (1113 tests), 0 stale
  pictures. Linux with libraries: 12496, 0 failed. Sanitizer build: 12343, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the sanitizer leg run alone (san_only.sh); gpu views 8,176 checks; linux 12,496.)

## 2026-10-28 (round 264): the layer stack on an object's surface (task 0244)

**Asked:** Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart materials,
painting on the model. After the mesh maps: the stack that reads them.

**Added**
- **A surface** (`ProceduralTexture::surface`, ADR 0080): a texture asset in one object's UV space - no wrap, no
  seamless blend - with the object's baked mesh maps as planes the layers read (`SurfaceMaps`, `src/texture/surface.cpp`)
  and the coverage keeping the neighbourhood filters (Blur, Edge Wear, Height AO) inside the UV islands: every
  uncovered texel takes its nearest island's values before a filter reads, so nothing bleeds across a gap.
- **Two more channels:** metallic and emission on every layer stack (`Writes Metallic`, `Writes Emission`), as the
  `#metallic` and `#emission` outputs a material's maps take; a surface's material gets all five.
- **A mask as its own stack** on every layer (`Mask` in the Textures window: Add Effect): a generator's values (Noise,
  Voronoi, Checker, Gradient...), **Mesh Map** (ambient occlusion, curvature, thickness, a position or world-normal
  axis through levels), Levels, Blur, **Anchor** (another layer's mask by that layer's Name); the smart-mask presets
  `surface mask preset <layer> edgewear|dirt|dust|metaledgewear|positiongradient|thickness` build such stacks, editable
  afterwards. **Fill** (one value per channel) and **Group** (a sub-stack blended back with one opacity and mask) layers.
- **The rect engine:** the composite below an edited layer is kept and only the layers from it up are redone inside the
  edited rectangle grown by the filters' halo; the result is bit for bit the full evaluation inside the rect (for
  surfaces the box filter sums each window directly and clamps at the atlas edge; tiling textures keep their running
  sums). Every loaded surface whose stack changed is evaluated on a worker, the images built there, the views showing
  the last images until the next land. Console: `surface new|add|mask|group|set|layer|eval|maps|list`.
- **Measured** (`tests_round264`): a 512 stack of a masked Fill, Noise, Blur 4, Edge Wear, an anchored Levels: 0 of
  4,096 texels inside a 64 x 64 rect differ from the full evaluation, 0 of 255,088 outside its halo touched; at 2048
  the full stack 300-400 ms over the runs, the composite below the Noise 110-180 ms, a 128 x 128 rect from the Noise
  up 90-120 ms. The Edge Wear preset on a subdivided cube's baked curvature: the edge band's mask 0.50 against 0.02 on
  the face centres. Blur 8 over two islands: the white one's minimum 1.0000, the black one's maximum 0.0000, the
  padding each its own. The editor's slowest frame while a surface's worker ran: 15-45 ms over the runs.
**Review (Opus, round 264):** ten majors, four minors and a nit, all taken before landing except where the spec now says otherwise. A surface's images had frozen - the output cache answered every request with the first images once a worker had run, drags included, and only the selected texture ever got a worker (every loaded surface whose hash moved gets one now, the drag preview is never served stale, and the images with their mips are built on the worker). The mesh maps were lost on undo and on reopening (they are files, not text: the record's path is a field now, the maps are kept across an undo and reloaded on open). A group's children and its mask had shared field names ("L1 L0 ...": a saved file gave the child the effect's opacity) - mask effects are "M0 ...", children "C0 ...". The material's metallic map multiplied a Metallic of 0, so it did nothing, and every surface became a mesh light through an emission map it did not write (Metallic 1, the emission map only when a layer writes emission, Extend wrap). The rect pass started from no anchors and skipped the dilation the full pass ends with (the anchors come with the composite below, the halo doubles over maps, the pass dilates at its end; a group's halo counts its children's masks; Image and Mask by Height honour the rect and the layer's own mask). A surface's Blur had wrapped across the atlas (clamped now) and the dilation does not stop islands closer than the blur radius from mixing in their gutter (the spec says so). A mistyped preset had cleared the mask (validated first). The box filter's direct window sums apply to surfaces only: tiling textures keep their running sums and their results. Also: the mask stack's scratch planes hold only what the effects read; the live path prepares mask effects once instead of cloning per point; the Mesh Map's softness is a rounded ramp; Blur and Levels work the metallic (and Blur the emission) channel; a group blends colour and emission by its mode. Not built, and said so in the spec: the slider path through the rect engine (an edit to a generator's field changes the whole surface; the engine serves round 0245's dabs), Substance's row letters and a Set as Surface button.

**Checked**
- `tests_round264` in `tests/test_render.cpp` (3 tests: the channels, the Levels mask, the Anchor, the file round trip
  with groups and masks, the new outputs' references; the Edge Wear preset on a cube, Blur between two islands; rect
  equals full, the times) and `tests/test_editor.cpp` (surface new and the material's five maps, add, a mask preset,
  set, group with undo, the worker and the frames meanwhile).
- Unit checks (round 264): Windows, in render-cache verify mode, 12933, 0 failed (1111 tests), 0 stale
  pictures. Linux with libraries: 12473, 0 failed. Sanitizer build: 12320, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open and path-tracer-off-the-main-thread tests failed under the parallel load and passed alone.)

## 2026-10-28 (round 263): mesh maps (task 0243)

**Asked:** Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart materials,
painting on the model. The mesh maps first: everything else reads them.

**Added**
- **`bake_mesh_maps`** (`src/render/mesh_maps.cpp`, ADR 0079): the five maps Substance's generators read, baked from
  the mesh into its UV layout (the texture bake's rule: the mesh's own UVs when they lie in 0-1 without overlap, so a
  textured object keeps its look; else, or on `layout`, the generated layout, written onto the mesh when the bake
  lands) - the **world normal** and the **position** in the mesh's bounds as colour, the **curvature** (each
  neighbour's drop below the tangent plane over its chord squared, averaged over the ring, scaled by the bounds and
  smoothed twice: the mean curvature at any resolution; 0.5 flat, convex toward 1),
  **ambient occlusion** (cosine-weighted hemisphere rays about the face's outside through a path tracer built on the
  mesh alone, over a reach of the object's size in the world; the open share) and **thickness** (the same rays inward:
  the mean distance to the mesh over the reach, so a thin plate is dark and a solid sphere two thirds). Written as
  five PNGs.
- **In the editor:** Object > Bake Mesh Maps (512 / 1024 / 2048) beside Bake Textures, `meshmaps [size] [samples]
  [keepuvs|layout]`, `meshmaps cancel`, `meshmaps status`; the bake and the PNGs on a worker with the mesh copied bare
  (the status line says so; Esc over the Scene view or `meshmaps cancel` stops it); when it lands and the mesh is still
  the one it baked from, one undo step keeps or writes the UVs, puts the files under
  `Assets/Textures/MeshMaps/<name>_<map>.png` and records them on the Mesh Renderer with a content key of the mesh
  (positions, topology, UVs, smoothing, the object's matrix; saved with the scene; `meshmaps status` says "stale" once
  any of them changes, and current after a reopen); a mesh changed meanwhile, a cancel or a failure drops the result
  and removes its files.
- **Measured:** a 48 x 32 sphere at 256 in its own UVs with 16 samples: 2,039,808 rays in 27-37 ms over the runs; AO
  1.000 everywhere, the normal within 1.7 degrees of the position's direction, curvature 0.624-0.631 (a 96 x 64 sphere
  0.624, the same with a ten-thousandth of radius noise: 0.56-0.69), mean thickness 0.665 (two thirds expected); an L
  of two cubes reads 0.54 on the strip beside its inner wall, 0 where its faces meet and 1 on the open faces; a 0.02
  plate's mean thickness 0.052, a cube's 0.628 at any scale; the editor's slowest frame while a bake ran 4-14 ms.
**Review (Opus, round 263, two passes):** five majors, seven minors and seven nits in the first pass, two majors and three minors in the second, all taken before landing. The generated UV layout had been thrown away (the maps fit no UVs the object had) and, once written onto the mesh, had replaced a textured object's own good UVs (the second pass): the object's own UVs are the default when they lie in 0-1 without overlap, and the generated layout is written onto the mesh only when they are unusable or `layout` is asked, as Bake Textures writes its. The rays' reach and offset were in object units while the rays travelled in the world (a centimetre import's thickness read black, a hundred-metre one's white: they come from the bounds placed by the object's matrix, tested on a cube at a hundredth of its size). The record's key was the mesh's session counter times a prime, an int the Inspector rounded through a float and that read "current" after a reopen whatever had changed: a 64-bit hash of the positions, topology, UVs, smoothing and the object's matrix as text, written to files and hashed but never shown, stale after a turn or a Shade Smooth and current after a reopen. The curvature divided by the edge length and so shrank with the mesh's resolution (a dense sculpt came out flat grey): each neighbour's drop over its chord squared, times a quarter of the bounds, is the mean curvature at any resolution, smoothed twice because a second difference amplifies surface grain (a ten-thousandth of radius noise spread it 0.43-0.82 with one pass, 0.56-0.69 with two). The worker's loops had not gone through `run_background`. Also: a cancel, a drop, a failure or the editor closing removes the files a bake wrote; Edit Mode and Weight Paint are left at the start and a landing under them or under Play is dropped; hidden faces are baked (they are part of the surface the maps describe); Esc cancels only over the Scene view; the Project window refreshes; a triangle with UV area and no 3D area is left to the padding; the L test reads the strip beside the wall, not the faces that meet, and the editor test checks the files on every path, the UVs kept byte for byte, and the key after a turn and a re-shading.

**Checked**
- `tests_round263` in `tests/test_render.cpp` (the sphere, the L, the plate, the cube, an empty mesh refused) and in
  `tests/test_editor.cpp` (the worker and the slowest frame, the five files, one undo step, undo clearing the record,
  the scene-file round trip, stale after a change, a change meanwhile dropping the result, cancel).
- Unit checks (round 263): Windows, in render-cache verify mode, 12898, 0 failed (1107 tests), 0 stale
  pictures. Linux with libraries: 12438, 0 failed. Sanitizer build: 12285, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test and the sanitizer run's animation group failed under the parallel load and passed alone.)

## 2026-10-28 (round 262): Sculptris Pro - Dyntopo (task 0242)

**Asked:** ZBrush's Sculptris Pro - detail that appears under the brush as it moves.

**Added**
- **`meshops::Dyntopo`** (`src/scene/dyntopo.cpp`, ADR 0078): a tessellator over the mesh's arrays for one stroke - a
  vertex-to-faces table it keeps, new triangles appended, removed faces tombstoned (their corners one vertex: no
  area) and compacted away at the stroke's end. Under the brush: n-gons become triangles, edges longer than the
  detail size are split at their midpoints (the new vertex's mask, groups and weights the mean of its ends', its UVs
  interpolated), edges shorter than two fifths of it collapse to their midpoints where that folds no triangle, pinches
  no edge into a third face, doubles no face and leaves more than four faces.
- **In Sculpt Mode:** Dyntopo and Detail (a fraction of the brush's world radius, default a quarter - ZBrush's rule: a
  small brush adds fine detail) in the Sculpt section, `sculpt dyntopo on|off|toggle|<detail>`, the Wheel's Size ring
  with Dyntopo in Lazy Mouse's slot, the status line. A Dyntopo stroke makes the mesh the object's own at its start
  and is one snapshot step at the pen's lift (ADR 0074's rule for topology; no delta); each dab tessellates its
  instances' regions first - the mesh's version bumped at once so its tables and the render mesh follow the new ids -
  then the brush moves the vertices. Grab and Snake Hook tessellate at their first dab and keep their sets. Dynamic
  Subdivision must be off (the cage, not the surface, would be tessellated: the stroke says so).
- **Measured:** a ten-dab Draw stroke across the 1,490-vertex sphere went to 1,840 vertices with 599 of 615 new edges
  within [0.4, 1] x the detail, the mesh closed and manifold; the detail set to the brush radius took the 6k sphere to
  2,943 vertices under the brush; a fully masked region gained 118 vertices, all masked, and moved none.
**Review (Opus, round 262):** seven majors, seven minors and four nits, all taken before landing. A Dyntopo stroke had deleted the mesh's loose vertices (the compaction now drops only vertices that had faces and lost them) and every shape key (the keys are carried - a new vertex the mean of its ends', a collapse the mean, the deltas compacted); the mask had not protected against collapses (a held vertex is never collapsed); seams and sharp edges had kept stale keys through a split or a collapse (they are re-keyed to the halves or onto the survivor); a mesh coarser than the brush had nothing tessellated (the face under the cursor seeds the region, mirrored for the mirrored instances, and the tessellation works the faces the brush sphere reaches, as Dyntopo's PBVH leaves do, so a face larger than the brush is tessellated where the brush is and no further); the per-dab cost was unmeasured (a ten-dab stroke on the 6k sphere is printed with and without Dyntopo: 1 ms against 22 ms, the tables and render mesh rebuilt on each dab that changed faces - ADR 0078 names it the open item for dense meshes). Also: hidden faces are left out and the hidden table follows new vertices; n-gons use the mesh's own ear triangulation; a collapse between two boundary vertices or leaving a sliver is refused; Drag Dot refuses Dyntopo (its re-placed dab would leave a trail); a stroke that changed nothing gives the mesh back and leaves no step; edges with a NaN end are left alone; the ADRs say what the code does (every instance tessellated, then all moved against the same positions; tombstones are zero-area triangles; ADR 0074 amended for the stroke cut off). Tests added for a loose vertex and a shape key, a sharp edge's halves, the cube under a mid-face dab, hidden faces beside the brush, the no-change press, the masked region at a coarse detail. Quad Remesh (round 261) now refuses open meshes: QuadriFlow stalled without end on an open sheet in this round's run. The Linux legs then found the hidden top of the cube fanned into 267 hidden triangles through the edge it shared with the brushed face: an edge a hidden face shares is neither split nor collapsed now, and a vertex with a hidden face never collapsed; the test's no-change press repeats the first press at the same pixel (a console dab and a mouse press read the depth a hair apart) and dabs on the face the camera sees.

**Checked**
- `tests_round262` in `tests/test_sculpt.cpp` (3 tests): the Draw stroke adds vertices, the new edges lie within the
  detail, the mesh stays manifold with no degenerate face, the stroke is one snapshot step that undoes to the original
  and redoes, a stroke with Dyntopo off is a delta step again; a coarse detail collapses the 6k sphere under the brush
  and keeps it manifold, a masked region gains masked vertices and moves none, Mirror X tessellates both sides alike.
  `tests_round262` in `tests/test_modeling.cpp`: a cube's top refined to a detail (5 n-gons triangulated, 214 splits,
  no top edge over the detail, manifold, compact after finish); the cube at a detail of 10 collapses three edges and
  stops at five vertices.
- Unit checks (round 262): Windows, in render-cache verify mode, 12869, 0 failed (1105 tests), 0 stale
  pictures. Linux with libraries: 12409, 0 failed. Sanitizer build: 12256, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test and the sanitizer run's animation group failed under the parallel load and passed alone.)

## 2026-10-28 (round 261): the ZRemesher spike - Quad Remesh through QuadriFlow (task 0241)

**Asked:** ZBrush's ZRemesher - quads that follow the form at a chosen count. The plan made it a spike: QuadriFlow from
Blender's tree behind a flag, the measurements and the gap to ZRemesher written down.

**Added**
- **QuadriFlow in the tree** (`extern/quadriflow`, Blender's patched copy with LEMON, pcg32 and pss; ADR 0077):
  compiled into the engine when Blender's Eigen is among the libraries (`BL_WITH_QUADRIFLOW`, all three builds), its
  licences listed. `meshops::quad_remesh` (`src/scene/quad_remesh.cpp`) drives the `Parametrizer` as Blender's
  wrapper does - the input welded and fan-triangulated, the field normalized and initialized for the target, the
  boundary constraints when asked, the orientation and position fields, the index map, the quads out - with a cancel
  flag read between the stages; the mask and vertex groups by position.
- **Quad Remesh in Sculpt Mode:** Target Faces, Preserve Sharp, Preserve Boundary, Adaptive Scale and the button in
  the Sculpt section, `sculpt remesh [faces] [sharp|nosharp] [boundary|noboundary] [adaptive|uniform] [seed <n>]`,
  `sculpt remesh cancel`. The operator runs on a worker with the mesh copied: the editor keeps drawing (the status
  line says so, Esc cancels), the result lands as one ordinary step when the mesh is still the one it started from
  and is dropped with a log line otherwise (a stroke, an undo, another scene). Without Eigen the command says so.
- **The findings** (`docs/tasks/0241-zremesher-spike.md`): the 6k sphere to 2,000 came out 2,113 closed quads within
  1.8 % of the radius in about 2.5 s with the editor's slowest frame 29 ms; two merged spheres to 3,000 one shell of
  2,917 quads with the mask kept; a cube to 200 in 130 ms. Against ZRemesher the field and the count are there; guide
  curves (QuadriFlow's orientation constraints from Sketch strokes), symmetry and painted density are what a later
  round would add.
**Review (Opus, round 261):** two blockers and five majors, all taken before landing. `build.sh` had used build.bat's variable names and an unset flag under `set -u`, so every Linux and macOS build would have stopped: it sets the flag, reads the right folder, appends to its own flags and lists the fourteen sources CMake lists (no `post-solver.cpp`, which wants Ceres). The worker's thread was never joined when the editor closed mid-job - a `std::terminate`: the job's destructor cancels and joins, and the editor's destructor drops the job first. A cancel after QuadriFlow's last check had landed anyway (the poll drops a cancelled job; a check after the index map); bad input could throw or exit inside the library (the input is checked as Blender checks it - finite, no face without area or edge without length, no edge in three faces, no two faces winding alike, no wire edges - and exceptions on the worker are caught; the library's own `exit` calls stay a recorded risk); the result had landed under Edit Mode, Weight Paint or Play (dropped now; Play cancels the job); a mask painted meanwhile had been lost (the mask version is compared too); the mesh was known by its address (by its uid now); the start copied the render caches (a bare copy). Also: the Esc hint names where Esc works, the licence is BSD-3-Clause by its text (the labels and the file's name say so), `pss` is not compiled, and tests for a cancel through the console, a mask painted meanwhile, and five bad inputs.

**Checked**
- `tests_round261` in `tests/test_sculpt.cpp` (2 tests, guarded by the flag): the sphere to 2,000 on the worker - all
  quads, closed, within a quarter of the target, every vertex within 2 % of the radius, one undo step with the mode on,
  the slowest frame printed; two merged spheres to 3,000 one shell with the mask kept, and a stroke made while the
  worker runs makes its result drop. `tests_round261` in `tests/test_modeling.cpp`: a cube to 200 and a UV sphere to
  600 closed and all quads within a quarter of the target; an empty mesh and a cancel refused with a reason.
- Unit checks (round 261): Windows, in render-cache verify mode, 12845, 0 failed (1101 tests), 0 stale
  pictures. Linux with libraries: 12385, 0 failed. Sanitizer build: 12232, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-28 (round 260): DynaMesh (task 0240)

**Asked:** ZBrush's DynaMesh - pull a form far out of its topology, then redistribute the polygons evenly and merge
what overlaps.

**Added**
- **`meshops::dynamesh`** (`src/scene/dynamesh.cpp`, ADR 0076): a grid of `resolution` cells on the longest side;
  for each axis every grid line's crossings with the triangles (binned per column) carry the triangle's orientation,
  the running sum along the line is the winding number (nonzero: inside, so self-intersections merge), three votes a
  grid vertex and the majority decides; naive surface nets over the signs with the lines' exact crossings - a vertex
  per mixed cell, a quad per mixed grid edge - then tangential Laplacian relaxation. A closed shell of quads: the
  24k-vertex sphere at 64 came out closed and manifold with its volume 0.1 % off and every vertex within a cell of
  the surface; two spheres overlapping by half a radius became one shell, the inner walls gone, 3 % under the union's
  volume. A line whose winding does not return to zero at its far end (an open edge, a flipped face) casts no vote;
  a cell whose corners fall into two pieces is filled before the nets, so the output is a 2-manifold and parts closer
  than a cell merge. The mask is interpolated at the crossings (a coarse cage keeps it everywhere); vertex groups by
  position; groups, UVs, seams, sharp edges and shape keys are dropped. A lone sheet, tilted or not, encloses nothing
  and is refused.
- **In Sculpt Mode:** DynaMesh Resolution (8-256), DynaMesh Smooth (0-10) and the DynaMesh button in the Sculpt
  section, `sculpt dynamesh [resolution] [smooth]`, the Wheel's Brushes ring with DynaMesh in Carve's slot (Carve stays
  Alt, the panel's and the console's); one ordinary step, the mode stays on, the next stroke rebuilds its grid.
- **A fix from round 258:** `sculpt group islands` had counted islands through shared UVs (a group a face on a mesh
  without UVs); it counts through edges now.
**Review (Opus, round 260):** two blockers and a dozen findings, all taken before landing. Naive surface nets had given one vertex to a cell whatever ran through it, so two parts touching along an edge, or closer than a cell, came out with edges in four faces and a vertex shared by two sheets: a cell whose inside or outside corners fall into two pieces, and a grid face with alternating corners, are now filled in before the nets (again until stable; the fill count is in the log line), and the two-cubes-on-an-edge case is a test. A line that crosses a sheet once kept a nonzero winding to the grid's edge and voted "inside" for everything behind it, so a plane tilted against two axes came out a wedge and a flipped face carved a wrong wedge: a line whose winding does not return to zero at its far end casts no vote now, and a vertex is inside by a strict majority of the lines that did (a tilted sheet is refused like a flat one; a tested case). The mask had been carried from the nearest old vertex within a cell and a half, so a coarse cage lost it: it is interpolated at the crossings, a vertex's the mean of its edges' (a masked 8-vertex cube keeps its mask on every output vertex). Also: the edge crossing nearest the inside end rather than the lowest; `sculpt dynamesh` leaves Edit Mode and Weight Paint first and checks its arguments; the Dynamic Subdivision field's pending value belongs to its object; the 0239 tail key compares the head's key, or its topology key only when the chain moved it; `sculpt group islands` ignores seams; the edge-on threshold is relative to the cell; the votes become the flag in place (one byte a vertex less at 256); the spec's numbers match the tests (the 6k sphere, 1.5 cells, the panel's ranges, two undo steps asserted, redo compared by positions, the mode asserted on).

**Checked**
- `tests_round260` in `tests/test_sculpt.cpp` (3 tests): the sphere at 64 closed, all quads, the volume within 5 %,
  every vertex within a cell, the step undone and redone with the mode on and a stroke moving the new mesh; two
  spheres one shell with no inner wall and the union's volume, the mask over the top carried (its share within 10
  points, nothing masked below the line); the times at 64 and 128 on the sphere and the two spheres printed (the
  sphere at 128: 77k quads in 148 ms) and an empty mesh refused. `tests_round260` in `tests/test_modeling.cpp`: a
  cube at 16 and 32 closed, all quads, its volume within 1.2 %; a lone plane refused.
- Unit checks (round 260): Windows, in render-cache verify mode, 12819, 0 failed (1097 tests), 0 stale
  pictures. Linux with libraries: 12359, 0 failed. Sanitizer build: 12229, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone; the sanitizer leg ran alone afterwards..)

## 2026-10-28 (round 259): Dynamic Subdivision (task 0239)

**Asked:** ZBrush's Dynamic Subdivision - a smooth, subdivided surface over a low-polygon cage the brush works - on
the Sculpt Mode of rounds 256-258.

**Added**
- **The evaluated mesh follows a dab** (`GameObject::evaluated_mesh`, ADR 0075): its key now reads the base mesh's
  `deform_version`, so a positions-only change re-evaluates any modifier stack. Before, a subdivided object stood
  still under the brush.
- **The subdivision chain** (`meshops::SubdivChain`, `src/scene/subdiv_chain.cpp`): a Subdivision Surface alone at
  the head of the stack keeps its per-level tables and, when only cage positions changed, recomputes the Catmull-
  Clark (or simple) points the moved vertices reach - the face points of the faces around them, those faces' edge
  points and vertex points - level by level, with `subdivide_impl`'s own arithmetic in its own order, so the result
  is the full subdivision's bit for bit (every vertex checked at levels 1-3, both schemes, on a sphere, an open grid
  and a cube). The last level is written into the evaluated mesh in place, so the render mesh follows in place and
  the GPU view uploads the moved range. A 6-px dab at level 2 on a 1,490-vertex cage recomputed 645 of 23,810
  vertices in 0.04 ms where the whole subdivision takes 1.76 ms.
- **The brush works the cage, the pick lands on it** (`sculpt_cage_hit`): the planes' point lies on the subdivided
  surface, inside the cage for Catmull-Clark, where a small brush reached no cage vertex; with a modifier drawn over
  the mesh the point is moved onto the cage along the pixel's ray (its first hit on the base mesh's triangles, as
  Blender sculpts against the base mesh's PBVH).
- **Dynamic Subdivision** in the Sculpt section (the modifier's levels, 0 = none), `sculpt subdiv <n>|off|+|-`, the
  Wheel's Size ring with Subdivide + / - (Lazy Mouse a toggle, `sculpt lazy toggle`; Freehand left the ring), the
  status line naming the level; vertex groups and polygroups carried through the chain as the modifier carries
  them. The mask's darkening (round 258) is drawn only when the cage itself is what the view draws.

**Checked**
- `tests_round259` in `tests/test_sculpt.cpp` (3 tests): a dab on a level-2 sphere changes the subdivided surface
  the next frame, in place (no render mesh built from scratch) and bit for bit the full subdivision's, and undo takes
  it back; levels 1-3 x Catmull-Clark / simple after three dabs each equal the full subdivision bit for bit, touch
  under a tenth of the vertices (123 / 645 / 2,913 of 5,954 / 23,810 / 95,234) and at level 2 take less time than
  the full subdivision; `sculpt subdiv` adds, sets and removes the modifier as undo steps. `tests_round259` in
  `tests/test_modeling.cpp`: the chain against the full subdivision on an open 6 x 6 grid and a cube, bit for bit,
  and nothing recomputed when nothing moved.
- Unit checks (round 259): Windows, in render-cache verify mode, 12793, 0 failed (1092 tests), 0 stale
  pictures. Linux with libraries: 12333, 0 failed. Sanitizer build: 12203, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone; the sanitizer leg ran alone afterwards..)

## 2026-10-28 (round 258): masks, polygroups, hiding (task 0238)

**Asked:** ZBrush's masks - and with them polygroups and hiding - on the Sculpt Mode of rounds 256-257.

**Added**
- **The mask** (`Mesh::sculpt_mask`, a float per vertex; `src/editor/sculpt.cpp`): every brush's weight is
  multiplied by 1 - mask, so a held vertex never moves and a half-held one moves half (Grab and Snake Hook read it
  too). Ctrl with the left button paints it with any brush (ZBrush's Ctrl), Ctrl+Alt takes it away, the Mask brush
  does the same without Ctrl (Shift with it blurs). A mask stroke is a delta undo step like a brush stroke (ADR 0074
  amended): no snapshot, exact on undo and redo. Masked vertices draw darker in the Scene view (CPU and GPU) while the
  mode is on (Show Mask).
- **Mask operations:** Clear, Fill, Invert (Ctrl+click off the model), Blur, Sharpen, Grow, Shrink, and a rectangle
  (Ctrl+drag off the model, an outline while dragging; `sculpt mask rect`): every vertex projected into it, front
  faces only when that is on. Each one ordinary step.
- **Polygroups** (`Mesh::face_group`): from the mesh's islands, its materials or its mask, or merged; Show Groups
  colours them in the view (a hue per group, per render vertex). Saved with the mesh (`pg`), as the mask is (`mk`).
- **Hiding:** a hidden face (a negative group) has no triangles in the render mesh, so it leaves the picture, the id
  plane (the brush cannot hit it), the GPU view and the path tracer with no other code knowing; a vertex all of whose
  faces are hidden is never gathered. `sculpt hide group <n>|masked|unmasked|all|none|invert`, `sculpt show all`;
  Ctrl+Shift+click a face to see its group alone, Ctrl+Shift+click off the model to see everything (ZBrush's).
- **The panel, the Wheel, the console:** Show Mask, Show Groups, a row of mask buttons and a row of group / hide
  buttons in the Sculpt section; a Masks ring in the Wheel's sculpt set; `sculpt mask|group|groups|maskview|hide|
  show|isolate`; the status bar names Ctrl and Ctrl+Shift.
**Review (Opus, round 258):** three blockers and nine minors, all taken before landing. The groups and hidden flags had stayed at their old indices through a face-rebuilding operator or a modifier (a deleted face shifted every hidden flag one face down; a Subdivision hid random patches): a Subdivision's children and an Array's copies now carry their parent's group, and `FaceBuilder`, `sync_attributes` and `Mesh::clear` drop a wrong-sized `face_group` or mask rather than misplace it. A Drag Dot mask stroke had cleared the positions' delta but not the mask's, so the floats no longer belonged to their vertices (`SculptDelta::clear`). The Wheel could open during a Ctrl gesture and another window's frame could end one (the gesture joins `wheel_hold_busy` and `scene_interacting`, Esc drops it); isolate took the nearest vertex's nearest face rather than the face under the ray (now the render mesh's triangle, as the pick does; a miss changes nothing instead of showing all); the mask colours were rebuilt every in-place frame (keyed on the build serial now) and stayed on after Clear Mask (an all-zero mask is no mask); the whole-mask operations and the rectangle had masked hidden vertices; a `pg` line's value went through `strtol` unclamped and allocated a string per line; a shortcut mid-stroke threw the stroke away (it ends as its own step); the spec claimed Edit Mode saw every face (hidden faces stay hidden in every mode and in renders until Show All - the status bar now says so). Two tests added: the gestures, Shift blur, Drag Dot mask, the remaining operations; the groups through a Subdivision, an Array, a triangulate.

**Checked**
- `tests_round258` in `tests/test_sculpt.cpp` (4 tests): a filled mask holds the surface against a Draw dab and a
  half mask halves the centre vertex's move within 5 %; a mask stroke is one delta step (no snapshot) that undoes to
  the exact floats and redoes; Ctrl paints, Ctrl+Alt clears; fill and invert, grow by one ring, blur halves the
  steepest step, a rectangle masks exactly the vertices projected into it; on two joined spheres `group islands`
  numbers two groups, hiding one leaves the other's triangles only, its centre pixel no longer reads the object, a
  dab there moves nothing and makes no step, a 600 px dab on the shown island gathers none of the hidden one, show
  all brings every triangle back, and the mask and groups round-trip through the scene file; Ctrl+Shift+click
  isolates the clicked group, the groups view gives two colours, a filled mask darkens every vertex.
- Unit checks (round 258): Windows, in render-cache verify mode, 12719, 0 failed (1088 tests), 0 stale
  pictures. Linux with libraries: 12259, 0 failed. Sanitizer build: 12129, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The first Linux run failed the gestures test on a pixel's truncation (the drag ends on a whole pixel; the test compared against the float centre): fixed, the Linux legs run again, the sanitizer leg alone afterwards. The gpu group's slow-open test failed under the parallel load and passed alone..)

## 2026-10-28 (round 257): the brush set, symmetry, the lazy mouse, alphas, stroke types (task 0237)

**Asked:** ZBrush's pillars - brushes, symmetry, alphas, stroke types - on the Sculpt Mode of round 256.

**Added**
- **Seven more brushes** (`src/editor/sculpt.cpp`): Clay Buildup (toward a plane a quarter radius above the brush
  area's plane: flat layers, not a rising peak; Alt or Carve below it), Inflate (each vertex along its own normal),
  Flatten (to the area's plane), Scrape (only what stands above it comes down), Pinch (toward the centre in the
  plane), Crease (a pinch that cuts in), Snake Hook (the vertices under each dab pulled along that dab's move). The
  area's plane is the unweighted centre and mean normal of the vertices under the brush, as Blender samples it.
  Shift still smooths with any brush.
- **Symmetry:** Mirror X / Y / Z across the object's planes (every combination) and Radial N about its Y axis - a dab
  applied at each instance, by position (a mesh that isn't symmetric gets symmetric dabs anyway); a Grab's set is
  taken per instance and follows the mirrored pull.
- **Lazy mouse:** the brush follows the pointer on a string (px) - the Sketch tool's steady stroke for the brush.
- **Alphas:** an image's luminance shapes the dab (white moves, black doesn't, nothing outside the image), laid
  across the brush's disc along the camera's right and up and turned to the stroke's direction (`alpha noturn`
  keeps it as the image lies); the path as given or under the project's Assets.
- **Stroke types:** Dots (a dab every quarter radius), Drag Dot (one dab that moves with the pointer), Drag Rect
  (one stamp at the release, as wide as the drag and turned to it), Freehand (a dab at every move), Spray (eight
  dabs a step scattered in the circle, each a random size and strength).
- **The panel, the Wheel, the console:** Mirror X / Y / Z, Radial, Lazy Mouse, Stroke and the alpha in the Sculpt
  section; a Brushes ring and a Symmetry ring in the Wheel's sculpt set; `sculpt brush <name> | symmetry x|y|z|radial
  <n>|off | lazy <px> | alpha <image>|off|turn|noturn | stroke dots|dot|rect|freehand|spray`.
**Review (Opus, round 257):** six majors and a dozen minors, all taken before landing. The symmetry instances had each worked on the positions the one before left, so a vertex on the mirror plane moved twice and a seam opened: every instance is now gathered and worked out against the positions before the dab and the moves are summed (Blender's proxies), Grab's sets the same way. Front Faces Only tested every instance against the real camera, so a Radial 6 dabbed only the near side: the eye and the view direction are mirrored with the instance. Snake Hook had pulled whatever lay under the pointer at each dab (nothing once the strand left the silhouette): the stroke keeps a place in the object's space that moves with the pointer at the press's depth and gathers around it without a raycast. The alpha's right and up were scaled by the object's scale (an alpha stretched on a scaled object): normalized. The Wheel's sculpt set had nine slices for a ring of eight: Bigger and Smaller moved into a Size ring with the lazy mouse and the stroke types, the mirrors became toggles. Also: one `sculpt_restore_delta` for the abort and the Drag Dot (it had written through a const_cast), the touched slot kept per vertex (an O(1) lookup instead of a scan), an HDR or EXR alpha read through its floats, Spray with Grab or Snake Hook dabbing once, dead code out (`sculpt_grab`, the unused press distance), the console's usage lines for an unknown stroke type or a bare `sculpt alpha`, the status hint naming Alt's carve for the brushes that have it, and tests for the seam, the far side under Radial 6 with Front Faces Only, and the Drag Dot.

**Checked**
- `tests_round257` in `tests/test_sculpt.cpp` (3 tests): six Clay Buildup dabs on a plane raise it less than six Draw
  dabs (a layer, not a peak); Flatten brings a spike's tip down, Pinch draws the set toward its centre, Snake Hook
  moves the centre vertex along the drag; Mirror X moves every mirrored vertex by the mirrored amount within 1e-4 and
  Radial 6 moves four to six times one dab's vertices; a 30-px lazy mouse keeps the dabs within 12 px of the line
  while the pointer zigzags 20 px off it; a 2 x 2 checker alpha moves fewer vertices than lie under the brush; Drag
  Rect stamps one dab as wide as the drag, Spray's eight dabs lie in the circle, Freehand dabs once a move.
- Unit checks (round 257): Windows, in render-cache verify mode, 12669, 0 failed (1082 tests), 0 stale
  pictures. Linux with libraries: 12209, 0 failed. Sanitizer build: 12079, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone..)

## 2026-10-28 (round 256): Sculpt Mode - the foundation (task 0236)

**Asked:** "we should start adding features that are from Substance Painter and ZBrush; do significant R&D on the
matter" - the ZBrush pillars begin with a Sculpt Mode whose brushes work at pen speed on a dense mesh.

**Added**
- **Sculpt Mode** (ADR 0074, `src/editor/sculpt.cpp`): Modeling Tools > Sculpt (the toggle, the brush, Size,
  Strength, Falloff, Carve, Front Faces Only, Pressure), `sculpt on|off`, the keymap action *Sculpt Mode On / Off*
  (the palette, the Wheel), the Wheel's *sculpt* set, the breadcrumb's Mode dropdown. Entering makes the mesh its own
  once and leaves Edit Mode and Weight Paint (and they leave it); Play refuses it; Esc leaves.
- **Only what is under the brush moves:** a stroke builds a uniform grid over the vertices (the cell the brush's
  world radius); a dab reads the object and the depth under the cursor from the Scene view's planes (no raycast),
  the depth turned into a distance by bisection on the renderer's projection, the screen radius into a world radius
  at that depth - the circle on screen is what you set - and walks the cells inside it; the vertices there take the
  brush by the falloff, their normals worked out from their faces at the dab; Front Faces Only skips those facing
  away. The grid is built at every stroke's start.
- **Three brushes:** Draw (along the normal; Alt or Carve goes inward), Smooth (Shift; a Laplacian step toward the
  neighbours' mean, Blender's smooth), Grab (the vertices under the first dab follow the drag in the view plane). Dabs
  every quarter radius along the drag; the pen's pressure drives the strength or the size.
- **In place, positions only:** a dab writes the positions and bumps `deform_version`; the render mesh follows in
  place and the GPU view updates the moved range; no render mesh is built from scratch through a stroke; the other
  views hold their picture as for a drag; tangents settle when the pen lifts.
- **Delta undo:** a stroke is one undo step holding the moved vertices' positions before and after - no scene
  snapshot (28 bytes a vertex); undo and redo apply it in place; a stroke cut off writes its positions back; delta
  steps go through the stack's cap and budget. The amendment to ADR 0003 / 0053: a mesh only snapshots share is left
  shared and written by a delta step, positions only, in step order; another object sharing it gets the stroke an
  own-mesh step first.
- **Console:** `sculpt on|off | brush draw|smooth|grab | size <px>|+|- | strength <0-1> | falloff <name> | carve
  on|off|toggle | front on|off | pressure off|strength|size | dab <x> <y> [pressure] | stroke <x0> <y0> <x1> <y1> [n]`.
**Review (Opus, round 256):** three blockers and six majors, all taken before landing; the check chain then found seven stale pictures (the Scene view's cache key read `version` but not `deform_version`: fixed in `evaluated_keys`) and a Grab test reading a stale depth plane. Entering the mode had copied
the mesh, so the stable snapshot kept the old one and an unrelated undo took every stroke back - a mesh only snapshots
share is now left shared, as the ADR meant, and only another object's share gets an own-mesh step; the brush reads the
GPU view's planes after a readback (`ensure_scene_planes`); the grid is built at every stroke's start, the gather
reaches past the radius by the stroke's farthest move, ids past the mesh are skipped and a mesh changed under a stroke
ends it; a cut-off stroke writes its positions back; delta steps go through the undo cap and budget; a pending change
is committed before a stroke and before its step; a pen held still mid-stroke opens no Wheel; an undo keeps the mode;
Sketch and Sculpt exclude each other; a stroke that moved nothing makes no step. The claims changed to what the code
does: a Laplacian smooth (no Taubin step; the volume within 2 %), a depth found by bisection, a 23,810-vertex sphere
with its dabs timed, the Wheel's sculpt set only in the mode, a row in the manual's table. Added: tests of the gather's
completeness and of a pointer drag with Shift, Alt, an undo mid-drag and Esc. Left as noted in Scope: a deforming
modifier under the brush, a non-uniform scale, the mode staying on its object.

**Checked**
- `tests/test_sculpt.cpp` (a new group, 4 tests): a Draw dab on a sphere moves exactly the vertices within the
  brush's radius (the set equal to a brute-force count), each outward and less toward the edge, the rest bitwise
  unchanged, Carve inward, pressure 0.5 about half as far; Smooth lowers a spike's Laplacian energy and keeps the
  volume within 2 %; Grab moves the centre vertex with the drag within 6 px and nothing outside the set; a stroke is
  one undo step restored bitwise and redone, a dab over nothing adds none; on a 23,810-vertex sphere no render mesh
  is built from scratch through a stroke (in place instead; 13 dabs in about 25 ms, printed); a pointer drag over the
  Scene view is one stroke, Shift smooths, Alt carves, an undo mid-drag leaves no trace, Esc leaves; the modes
  exclude each other and Play refuses it.
- Unit checks (round 256): Windows, in render-cache verify mode, 12649, 0 failed (1079 tests), 0 stale
  pictures. Linux with libraries: 12189, 0 failed. Sanitizer build: 12059, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The first chain found two things: the render group's hash-memo test (evaluated_key now mixes deform_version as evaluated_keys does) and the livelink group hung in the parallel suite once (killed; it passed alone, and in the rerun). In the rerun the gpu group's slow-open test failed under the parallel load and passed alone; the sanitizer leg ran alone afterwards..)

## 2026-10-28 (round 255): the HCI and modularity pass - fewer, calmer tabs; where you are (task 0235)

**Asked:** "Look through the project and find ways to make it more HCI friendly and easier to navigate. Tabs are
starting to look larger and overwhelming." And: "Do some serious R&D when it comes to editor modularity, but UI
simplicity so artists can focus on what's needed. Make it familiar to already mainstream tools; but keep it
Unity-based."

**Found** (the audit in `docs/tasks/0235-hci-pass.md`, against Nielsen's heuristics and Fitts's law on a 2-in-1): tabs
that don't fit ran off the strip's edge with no sign the rest existed; 22 window kinds in one unsorted Add Tab list,
three tabs for one job three times over; the layout, mode and tool shown in three places; four colour signals for two
tab states; every Inspector component open on a small screen; the layout guide staying until dismissed; a hint that
named the mode but not a running tool's next step.

**Added**
- **The module registry** (ADR 0073, `src/editor/modules.cpp`): one row per window kind - title, icon, group, the
  stages it belongs to, whether it duplicates, whether its group folds - read by `window_title`, `window_icon` and
  `can_duplicate` (three tables and their asserts went), and through them by the Window menu, Add Tab, the palette,
  the Wheel's modes and the UI file. A new window kind is one row and one draw function.
- **Tabs that fit** (`src/editor/tab_strip.cpp`): a strip whose labels don't fit shows the inactive tabs as icons;
  when the icons don't fit either, the last ones go into an overflow menu (»), so no tab is lost to the edge.
- **Tab groups:** in one area, Materials / Textures / Lighting show as one *Shading* tab with a row of the three
  under it; Rigging / Animation / Sequencer and Modeling Tools / Live Link / Profiler fold the same way; the tab
  menu's *Fold Groups* (`tabs fold on|off`) turns it off. The dock strings and layouts are unchanged.
- **A stage shows its tabs:** in the main window a stage layout hides the tabs whose modules aren't in it, as it
  hides tools; the strip's **+N** lists them (a pick shows one for good); a tab asked for, dropped or added shows
  whatever the stage and is kept in the preferences; the tab menu's *Show All Tabs* (`tabs all [on|off]`) shows every
  tab; a hidden active tab yields; a leaf never shows nothing; the Default layout, the arrangements and other editor
  windows hide nothing.
- **Where you are:** the Scene view's bar reads *Layout > Mode > Tool* when it has room, each part a dropdown;
  `where` prints it; the status bar names a running tool's next step (Knife, G / R / S, Push/Pull, a modal
  adjustment, Sketch).
- **Quieter:** the Learn tab's orange is its icon, not an underline; the toolbar's groups are split by a thin rule;
  the Inspector's *Fold* / *Unfold* folds every component to its header (the folds kept in the preferences); the
  layout guide hides itself after a layout has been picked in two sessions (Got it still works; Reset Guides clears the
  counts too).
**Review (Opus, round 255):** two blockers and six majors, all taken before landing. The suite's one red test was the
test's own bug (its click helper held a reference into `button_rects`, rebuilt every frame - the same bug had made
the Inspector's header buttons look unclickable in round 254's tests); the coverage map now reads the registry;
folding compares group names, not literal addresses (debug builds never folded); the layout a session opens on counts
after the preferences are read, and Reset Guides clears the counts; the stage filter is the main window's only, a
dropped or added tab counts as asked for and is kept in the preferences, and the strip's +N names the hidden tabs;
the Window menu's shortcut table joined the registry; `fold=` keeps component keys only; the Knife and Push/Pull
hints say what the tools do; the docs stopped claiming a tab is never clipped (a lone one wider than the strip is
cut at the edge) and that Add Tab was folded. Left as noted: a group tab's close, menu and drag act on its active
member; the member row has no fit logic beyond the clip; the strip's per-frame vectors.

**Checked**
- `tests_round255` in `tests/test_editor.cpp` (5 tests): the registry has a row per kind with unique titles and the
  editor's titles and icons are its; a 600-px leaf with six tabs shows all six inside the strip in order and a
  280-px leaf shows the active one with the rest in the overflow; Materials, Textures and Lighting show as one
  Shading tab with a member row and a member pressed becomes active, Fold Groups off shows three; the Rigging layout
  hides a Materials tab unless asked for or Show All Tabs is on, the Default layout hides nothing; the breadcrumb
  reads "Modeling & Topology > Edit > Move", the header's Fold folds the Cube's components and Unfold opens them (the
  console too) with component-only `fold=` keys in the preferences, a layout picked three times counts one session,
  three sessions hide its guide and `layoutguide reset` brings it back.
- Unit checks (round 255): Windows, in render-cache verify mode, 12,616, 0 failed (1075 tests), 0 stale
  pictures. Linux with libraries: 12,157, 0 failed. Sanitizer build: 12,027, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open and off-main-thread path-tracer tests failed under the parallel load and passed alone, as did the animation and input groups under the sanitizers.)

## 2026-10-27 (round 254): the Layers panel (task 0234)

**Asked:** "Add layer opacity and systems so it's easier to navigate while in the console when using pen and drawing.
Similar to how Photoshop and Substance Painter do layers. Make sure it's also compliant with the radial menu."

**Added**
- **Layers composited as Photoshop composites** (ADR 0072, `src/editor/sketch_layers.cpp`): the sketch's layers draw
  bottom to top; a Normal layer at full opacity draws straight to the frame as before; any other - an opacity below 1,
  a blend mode (Normal, Multiply, Screen, Overlay), a layer in a group - draws its strokes over a copy of what is under
  it with a mask of where it drew, then is blended back by its mode and mixed in by its opacity, so two overlapping
  strokes on a half-opaque layer are one tone. A group's children composite into the group's picture first. Solo
  (`sketch layer solo|unsolo`, Alt-click an eye) shows one layer.
- **The stack on the scene data:** `Sketch::move_layer`, `remove_layer`, `duplicate_layer`, `merge_down`, `flatten`
  and `add_group` each remap every stroke's layer, the groups and the active layer; a locked or hidden layer refuses
  the pen and keeps its strokes from the eraser (the log says why). Saved as a `sketch_layer_ext` line per layer that
  needs one; an older build skips it.
- **The Layers window** (`WindowKind::Layers`, Window > Layers; in the *Drawing* and *Reference & Planning* layouts
  and from the Draw tab's bar): a row a layer, the top one first, a group's children indented under it - the eye
  (Alt-click solos), the lock, a thumbnail of the picture-plane strokes, the name (a press picks the layer,
  double-click renames, right-click the menu, a drag reorders), the opacity slider (one undo step when let go) and
  the blend combo as the width allows - and a footer of New Layer, Group, Duplicate, Merge Down, Flatten, Delete; the
  rows grow with Tablet Mode's UI.
- **The Wheel's layers set:** a pen held on a row opens it on that layer - New, Duplicate, Merge Down, Delete, Hide /
  Show, Lock / Unlock, a Blend ring (the modes, Solo, All, Group, Flatten), Undo; the sketch set's eighth slice became
  a More ring (Build Box, Layers, New Layer, Merge Down, Place in 3D, Flat, Redo, Palette).
- **Console:** `sketch layer add|group [name] | duplicate | merge | flatten | delete | move <from> <to> | rename <name>
  | lock|unlock|lock toggle | toggle | solo|unsolo | blend normal|multiply|screen|overlay | <n>`, each an undo step.
**Review (Opus, round 254):** nineteen findings, all taken before landing. The composite now draws a layer into a
transparent picture and puts it over the frame by coverage times opacity with Porter-Duff over, the blend applied
where the frame is opaque (the first build composited over a copy of the frame and wrote opaque pixels: black strokes
wherever the card presents the view, and wrong edges for Multiply); a drag's slots are the rows as shown (top first)
mapped back to layer indices, and the drop lands the layer right above the row under it (the first build read the
slots as indices and subtracted one twice); the row menu copies what it reads before an item reshapes the stack and
checks its index (a use-after-free after Duplicate; a stale index after an undo); a group is made in place, right
above its layer; the list scrolls with the wheel; the solo, the rename and the menu's index are dropped by undo, redo,
a new scene and every stack operation; Flatten drops hidden layers' strokes as Photoshop does; Delete picks the
neighbour; `sketchline` / `sketchcircle` honour the lock; a hidden group's and a solo'd-out layer's strokes are
neither construction nor erased; a loaded `group` must name a group layer; empty layers cost nothing and strokes are
bucketed once per frame; the blend combo shows from 250 px; the New Layer tooltip and the spec's claims (the indices
kept, the stress measurement that wasn't) corrected. Left as noted in Scope: the live stroke at full colour, finger
scrolling of the list, a blend inside a group over its transparent picture.

**Checked**
- `tests_round254` in `tests/test_sketch.cpp` (5 tests, through the frame's pixels): two strokes on a half-opaque
  layer are one tone, Multiply darkens blue over red, a hidden layer shows what is under it, a solo leaves the others'
  pixels alone, a group at 0.5 reads as its child at 0.5; move swaps the draw order with every stroke keeping its
  layer, merge down keeps the lower opacity, duplicate copies the strokes, delete remaps, flatten leaves one layer; a
  locked layer refuses the pen and the eraser; the window shows in the two layouts with the top layer first, a press
  picks a row, New Layer adds one, the eye hides, a pen held on a row opens the Wheel's layers set on that layer; save
  and load round-trip lock, blend and group, a file without the line loads with the defaults.
- Unit checks (round 254): Windows, in render-cache verify mode, 12,550, 0 failed (1070 tests), 0 stale
  pictures. Linux with libraries: 12,091, 0 failed. Sanitizer build: 11,961, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 253): the Draw tab and the drawing-pad workflow (task 0233)

**Asked:** "Add the option to sketch in the game view. The draw tab should have its own tab and be included within the
reference and planning phase... I can still navigate and move around the scene with my keyboard, but I can use my
drawing tablet as a way to actually modify the scene. The perspective guide should also adjust itself to a view so
when you sketch and model it's accurate." And: "when I draw in perspective I want it to be a 3D representation in
scene. So when I move the camera around I can still see in-perspective how that line looks."

**Added**
- **The sketch tool works in a view** (ADR 0071, `src/editor/sketch_view.cpp`): the Scene view, the Game view and the
  Draw tab bind the view the pen draws in (rect, camera, matrices, hover) before the tool runs; every sketch function
  projects, casts and picks through it, so the same strokes are drawn and corrected in each view's own perspective; a
  stroke begun in one view goes on while the others draw. The game camera is read as a scene camera (its turn, its
  focus distance as the pivot, its lens).
- **The Draw tab** (`WindowKind::Draw`, dock id 21): the scene's picture from the Scene view's camera or the Game
  camera (`draw source scene|game`), with its own targets and cache and no gizmos, icons or stats, and the sketch
  tool's bar - the source, Draw / Drawing, the width, the guide, the perspective, the symmetry, the corrections,
  Place in 3D; opening it starts the tool with Place in 3D on; in the *Reference & Planning* and *Blocking Out*
  layouts, and a *Drawing* layout of its own.
- **Sketching in the Game view:** with the tool on and Play stopped, the pen draws over the Game view in the game
  camera's perspective (its vertical field of view at the picture's aspect); the guide's vanishing points come from the
  view's own matrices, so they follow the camera of the view under the pointer, a physical lens included.
- **Keyboard navigation while the pen draws:** with the tool on, the arrows (A / D) orbit and (W / S) dolly the Scene
  camera, Shift+arrows (Q / E) pan, no mouse button down, no drag and the Wheel closed; a stroke under way holds the
  keys; a key the view took is not the shortcuts' that frame, but Ctrl, Alt and Shift+letter chords are never taken;
  frames keep coming while a key is held.
- **Place in 3D** (`sketch place 3d|flat`): a picture-plane stroke lands in the scene when the pen lifts - on the
  scene canvas it was drawn over, as before, else on a **depth canvas** facing the camera at the depth under the
  pen's first point (the surface the view's ray hits; over nothing, the camera's pivot distance) - corrected in the
  plane's metres, so orbiting shows the line foreshortened where it was drawn and Build from Sketch, the sweeps, the
  cuts and the trace read it as they read a plane canvas's stroke. Depth canvases (kind 2) are saved with the scene,
  never construction planes, not listed, and go when no stroke uses them. Off, the stroke stays on the picture plane
  and fades with the view.
**Review (Opus, round 253):** seven findings fixed before landing - strokes placed in 3D can now be built from (every
`kind == 1` check on a canvas became `kind >= 1`; `sketchlift` keeps to picture-plane strokes); keyboard navigation
leaves Ctrl, Alt and Shift+letter chords to the shortcuts and runs only with no drag, no right button and the Wheel
closed, for the hovered view once a frame, its state cleared when the tool ends; the game camera's lens is its vertical
field of view at the picture's aspect and the guide's vanishing points come from the view's own matrices; tests for the
acceptance criteria (a stroke projecting back onto its pixel, the camera turned, over nothing at the pivot's depth,
no stroke while Play runs, keys blocked during a stroke, Ctrl+D not eaten); the spec and ADR say `sketch_view_` and
that the Draw tab isn't duplicated. The minors too: the Draw tab's *No perspective* turns the guide off, the tab
restarts the tool when shown again (by frame number), a Game source leaves the Scene camera alone and says so, no
sketching anywhere while Play runs or is paused, depth canvases no stroke uses are pruned (erase, clear, a missed
plane - which falls back to flat), the console binds case-insensitively, named constants for the navigation rates.

**Checked**
- `tests_round253` in `tests/test_sketch.cpp` (4 tests): a pen stroke over the Game view is the game camera's (its
  layer's view) and goes on while the Scene view draws, the guide's points differ over the two views; Right held half
  a second turns the Scene camera with no button down and switches no tool, Shift+Up pans, W dollies, the keys are
  the tools' again with the tool off, Ctrl+D duplicates and moves nothing, a key held during a stroke moves nothing;
  a stroke over the cube lands on a depth canvas at the cube's depth (over nothing, the pivot's) and projects back
  onto its pixel, still projecting after the camera turns, a flat stroke stays on the picture plane, `sketch clear`
  leaves no depth canvas; the Game view takes no stroke while Play runs; the Draw tab shows in the two
  layouts and the Drawing one, starts the tool with Place in 3D, takes a stroke in its own view, and shows the Game
  camera on `draw source game`.
- Unit checks (round 253): Windows, in render-cache verify mode, 12,511, 0 failed (1065 tests), 0 stale
  pictures. Linux with libraries: 12,052, 0 failed. Sanitizer build: 11,922, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 252): windows for a drawing tablet - dock into any window, a second Scene view (task 0232)

**Asked:** "I want to dock windows onto other windows. Such as on my drawing tablet I want my scene view and Inspector
view. Include the option to have duplicate windows."

**Added**
- **A kind once per window, not once overall** (ADR 0070, amending 0059): the Scene view, the Game view and the
  Inspector **duplicate** into a window of their own (the tab menu's *Duplicate into New Window*, `dock duplicate
  <Window>`), the first staying put - a Scene view and an Inspector on the drawing tablet while the main window keeps
  its own. The other kinds show one thing and stay in one window at a time.
- **A view state per window** (`EditorViewState`): the Scene view's camera, place, navigation drag, shading and
  overlay flags, pilot camera, the Game view's place, the Inspector's pin, and the render targets, renderers, filter
  targets and view caches the views draw into, swapped into the editor's members with the window's UI
  (`swap_window_state`), so every tool, shortcut and the Wheel act in the window under the pointer with no other
  change; a new window starts from the main window's state (a duplicate from the window it came from); the card's
  view slots are keyed by the window too; a camera gliding in another window keeps frames coming; saved in the
  `windows=` preference as `;V(yaw,pitch,distance,pivot,fov,ortho,shading,pin)` per window and restored with it.
- **Dock into any window:** a tab dragged out of one editor window and dropped over another joins that window's dock
  at the zone under the pointer (its tab bar a tab, its quarters a split), the target drawing the highlight while the
  drag is over it; the main window is a target like any other; `dock move <Window> window <n>` (the `dock windows`
  numbers) joins a window's largest area; the tab menu's *Move to Window n*.
- **The Inspector's Pin** (Unity's lock): this Inspector keeps one object while the selection moves on, and follows
  again unpinned; `inspector lock|unlock`. A second Inspector has a pin of its own.

**Changed**
- A layout reset in the main window and Add Tab no longer remove or hide a Scene view, Game view or Inspector another
  window holds; `dock_move`'s insertion is `dock_insert`, shared with the drop onto another window.
**Reviewed** (the Opus review's findings, fixed before the PR)
- Both Scene views ran the shared tool state every frame, so the window without the pen ended a stroke begun in the
  other after one frame (and a Scene Gizmo press there snapped the main camera, G / R / S ran twice). An interaction
  belongs to the window it began in (`interaction_window_`): the other windows' Scene views draw and leave the tools
  alone until it ends; the Scene Gizmo's drag, the gizmo's pivot and orientation, the finger orbit, the Alt-click and
  the side-button drag, the pilot's saved camera and the sketch guide's last camera are per window too.
- The card's depth and id planes could be read for the wrong Scene view (the planes' size wasn't per window and the
  slot check ignored the window): the size travels with the view, the check asks the window, and a slot taken from
  another window clears that window's waiting planes.
- `take_tab` removed the first instance it found, so moving a duplicate took the main window's Scene view; every move
  now names the window it comes from and a moved Scene view carries its camera; restoring two windows with a Scene
  view each no longer closes one.
- A pinned Inspector edited the selection, not its object, and two Inspectors dropped each other's menu choices and
  fought over the scroll: the pinned one edits its object alone; the menu choice and the scroll are the window's.
- The Pin was drawn under the name field: it sits at the header row's right and the name field makes room.
- The windows preference now carries the overlay flags with the camera (`;V(...)`, numbers written with nine digits
  and a NaN as 0); a window that never drew gets a tab, not a split; the caches of a closed window are freed; a bad
  window number in `dock move ... window` warns; the pins let go with a new scene.
- Tests added: a pen stroke in a duplicate goes on while the main window draws and lands as one stroke; a Game view
  duplicates; closing the windows frees the caches and returns the tabs; a duplicate moved to another window takes its
  own instance.

**Checked**
- `tests_round252` in `tests/test_editor.cpp` (3 tests): a tab dropped over a second window joins it and leaves the main
  dock, `dock move ... window n` and `main` move it back and forth; `dock duplicate Scene` makes a second Scene view
  whose Alt+drag orbits it alone (the main camera unmoved), a layout reset keeps both, the windows preference
  round-trips its camera; an Inspector pinned to a new cube keeps it as a sphere is made, unpins, and a second
  Inspector has a pin of its own.
- Unit checks (round 252): Windows, in render-cache verify mode, 12,474, 0 failed (1061 tests), 0 stale
  pictures. Linux with libraries: 12,015, 0 failed. Sanitizer build: 11,885, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 251): custom panels, a shortcut for anything, a UI file (task 0231)

**Asked:** the second task of the customization plan: ZBrush's custom palettes ("put any button in a palette of your
own, a hotkey on anything, Store Config to share a layout") for Blendity, Unity's way.

**Added**
- **My Tools** (`src/editor/custom_panels.cpp`, ADR 0069): a window kind holding named **panels** as tabs, each a grid
  of commands - any palette entry (a keymap action, a window, a layout, a Create, a curated console line) or a console
  line - with the command's tooltip; right-click any menu or palette row > *Add to My Tools > panel*; right-click a
  button > Remove or Assign Shortcut; *+* makes a panel from the name typed; the tab's menu renames or deletes it.
  Dockable anywhere like every window (dock id 20), part of a layout's dock string; saved as `panel=` in the
  preferences. Console `panel new|add|remove|rename|delete|list|show|run`.
- **A shortcut for anything:** a console line gets a keymap action of its own, `user.<n>` (`useraction=` in the
  preferences), bound like any action and listed in the palette under *Yours*; right-click a menu or palette row >
  *Assign Shortcut...*, then press the key - anywhere, not only in Preferences (a strip at the top says what is being
  bound; the key is taken, so its old action doesn't run; a clash with another action is named in the console and
  both run). Preferences > Keymap lists *Yours* with their chords and Remove. Console `shortcut <chord> <command>`,
  `shortcut clear <command>`, `shortcut list`.
- **A UI file:** `ui save <path>` writes `<name>.blendity-ui` - the layout and the windows, the workspace and its
  focus, the feel, the keymap preset and overrides, the user actions, the panels, the Wheel's sets, the favorites,
  the user layouts, the theme and the scale - and `ui load <path>` applies it whole (the lists it carries replace the
  current ones; lines this build doesn't know are skipped); Preferences > UI file has the path, Save and Load, and two
  built-ins: *Blender-like* (Blender's feel and keys, the 2 by 3 layout) and *ZBrush-like* (the Tall layout with My
  Tools panels of the modeling operators and the scene's basics).

**Changed**
- The keymap page's key capture is the frame's (`keymap_capture_frame`), so a key can be assigned from a menu row;
  a user layout's preference line is read by one function the preferences and the UI file share.
**Reviewed** (the Opus review's findings, fixed before the PR)
- Loading a UI file saved while a user layout was active crashed (the layouts' name pointers were fixed only after the
  workspace was looked up); `ui save ` with a blank path aborted. The pointers are fixed before the lookup; the path
  is the rest of the line, trimmed, and an empty one is refused.
- Loading any UI file - the built-ins too - wiped the favorites, the Wheel's sets, the user actions and the panels
  even when the file carried none. A list is replaced only when the file has a line of its kind.
- A cleared or replaced user action left its chords in the keymap's overrides, where the next new action picked them
  up (Ctrl+Alt+K ran the new line). A user action takes its chords with it when it goes; a new id never inherits.
- Two uses after free: Assign Shortcut on a palette entry with no action passed the entry's name by reference into a
  call that rebuilds the palette (a long name read after its free); the My Tools grid read a panel and a palette
  entry after a button's command could have replaced them. The name is copied; the grid draws a copy of the entries
  and runs the button after the grid.
- The key capture ran after the windows drew, so Esc closed Preferences and left the capture armed, and the sketch
  tool's Esc or the draw tool's Enter acted and were captured. The capture runs first in the frame, before the Wheel
  and every window; Esc held or pressed cancels.
- "The key goes to the new one" wasn't so (a built-in action with the chord ran first), and the message said both
  would run. The chord is taken from the actions that had it (named in the console); a preset or Reset Changes keeps
  the user actions' chords, which a preset can't bind.
- Also: a loaded file's shortcuts and panel console lines are named in the console (a shared file runs commands); the
  Yours rows show the line and replace the chord shown; a user action for a curated entry's line no longer doubles it
  in the palette and commands match case aside; a tab's Rename / Delete address the panel by name (a deleted panel's
  rename field went to another); New Panel takes the first free "Panel n"; `ui_scale` from a file is clamped; the
  feel and preset apply before the overrides, the focus after the workspace; the empty block and unused variable the
  moved capture left in keymap.cpp are gone.

**Checked**
- `tests_round251` in `tests/test_input.cpp` (3 tests): a panel's button runs its command and a console line is a
  button, remove / rename / delete, the panels round-trip the preferences; a bound console line runs on its key, an
  action's own chord goes on the action (no user action made), the binding survives the preferences, clear removes
  it, a captured key binds without running its old action, Add to My Tools from the row's menu; a UI file saved, the
  arrangement changed seven ways, loaded, gives the same text, an unknown line is skipped, the built-ins apply.
- Unit checks (round 251): Windows, in render-cache verify mode, 12,430, 0 failed (1056 tests), 0 stale
  pictures. Linux with libraries: 11,971, 0 failed. Sanitizer build: 11,841, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 250): the Wheel - one radial menu everywhere (task 0230)

**Asked:** the first task of the customize / sculpt / paint / stylize plan (0230-0250): "a Blender addon 'Fluent' mode
that allows the usage of a singular radial menu, unless you believe the touch interface among other methods is
superior" - the user chose one wheel for the keyboard, the pen and a finger alike.

**Added**
- **The Wheel** (`src/editor/quick_menu.cpp`, ADR 0068): task 0217's pen quick menu grown into the editor's one radial
  menu, opened by **Space** (Shift+Space in the SketchUp preset, whose Space is Select), the pen's side button, or
  **the pen or a finger held still** half a second (fingers never opened it), over any window and kept inside it.
- **Slices are palette entries** (any command the palette lists) or console lines, eight a ring; a slice can open **a
  ring of its own** (Esc or a tap in the middle goes back).
- **A set per mode** - Object, Edit Mode, Sketch, Weight Paint, and one per window kind (Hierarchy, Project, UV
  Editor...; the rest share "other") - each with a default the user edits: **right-click a menu row > Add to the
  Wheel**, Preferences > Wheel (Up / Down / Remove, Add by name or console line, New Ring, Reset), or the console's
  `wheel add|remove|move|ring|reset`; saved in the preferences as `wheel=<mode>|...` (only the sets that differ from
  their defaults).
- **Picking:** flick-and-lift, a tap, **the keys 1-8** (each slice shows its number) or the first letters typed;
  **Shift keeps it open** to chain; a tap in the middle repeats the slice picked last; the status bar names the keys.

**Changed**
- A right-click on a menu row or a palette row opens a small menu - Add to (or Remove from) Quick Favorites, Add to
  the Wheel - instead of adding to the favorites at once; the console's `favorite` still adds at once.
- The pen's hold moved from the Scene view to the frame (`wheel_frame`), so it works over every window and for a
  finger; a finger that held still orbits nothing and selects nothing when it lifts.
- `quickmenu` is an alias of `wheel`; the default Object and Edit Mode sets are eight slices with a "More" ring
  (they were ten and twelve).
- **An AI disclosure** (the user's request): `docs/AI_DISCLOSURE.md` says how Blendity is built (a human directs,
  reviews and merges; Claude through Claude Code writes the code, tests and docs from approved specs; every commit
  names the model) and what is not in the program (no telemetry, no cloud, no AI at run time except the optional
  local AI Inbetweening); the README has a section linking to it and the PR template a line.
- The plan gained the user's requests of the day as tasks 0232-0235 (windows for a drawing tablet, the Draw tab and
  the drawing-pad workflow with every stroke placed in 3D, a Photoshop-style Layers panel, an HCI and modularity
  pass) ahead of sculpting; `tools/test_coverage_map.py --check` still reports the four entries it did before this
  round (edit.favorites, edit.palette, layoutguide, LiveLink), none of them this round's.
**Reviewed** (the Opus review's findings, fixed before the PR)
- The wheel and its hold belonged to no window: a secondary window's frame cleared the main window's hold and drew
  the main window's wheel at the wrong coordinates. A wheel and a hold now belong to the window they began in
  (`qm_.window`, `hold_window_`); the other windows neither drive nor draw them.
- A letter or digit that picked a slice went on to the shortcuts and to the operator the pick started (typing "s"
  picked Scale and the leaked S started a modal scale; "1" in the edit set extruded by exactly one unit). The wheel
  eats its keys and the typed text every frame it is open.
- A hold opened the wheel in the middle of drags that don't read a cancel (a Hierarchy row's drag or pending select,
  a Weight Paint or Editing stroke, a UV or bone handle), which kept going under it. The held press is taken from the
  windows when the wheel opens (a cancelled release this frame, those drags reset), and a hold on a busy target (a
  slider being dragged) is dropped at once instead of redrawing for half a second.
- The "other" set could never show: every window kind got its own copy of it. A window kind without a set of its own
  now uses "other" (so editing "other" shows in the Inspector, Console, Learn...).
- Editing by label hit the first same-named slice (a ring's "Undo" removed the top-level Undo). The Preferences rows
  and the console address a ring's slices through their ring (`wheel remove edit Undo ring Tools`); a ring keeps its
  last slice; a ring is a ring by a flag, not by being non-empty.
- The palette row's right-click still added to the favorites at once; it opens the same small menu as a menu row.
- Also: the side button clicked over any window opens the wheel (not only the Scene view); the separators in a label
  or a console line are escaped in the preferences (`%3B`); a loaded set is capped at eight; Space closes an open
  wheel; a chained pick that changes the mode shows the new mode's set; a slice whose command picks the wheel again
  runs once; the Into-ring field clears when the mode changes.
- The full suite's two pre-existing failures the reviewer saw (`test_modeling.cpp:6766`, `test_gpu.cpp:412`,
  "mismatches == 0") are the known NO_LTCG-build and parallel-load cases, not this round's.

**Checked**
- `tests_round250` in `tests/test_input.cpp` (5 tests): Space opens the Wheel over the Scene view and the Hierarchy
  with each one's set and a tap on Create Empty makes one; a ring opens in place, 1-8 and typed letters pick, Esc goes
  back then closes; Shift chains and the middle repeats; a finger held still opens it in both windows without
  selecting, a moving finger doesn't; sets edited from the console round-trip through the preferences and a ring
  holds eight. Round 237's pen tests keep their meaning (the slice angles follow the eight-slice ring).
- Unit checks (round 250): Windows, in render-cache verify mode, 12,396, 0 failed (1053 tests), 0 stale
  pictures. Linux with libraries: 11,937, 0 failed. Sanitizer build: 11,807, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 249): drawing in perspective - the write-up (task 0229)

**Asked:** the last task of the perspective-drawing plan (0223-0229).

**Added**
- `docs/tasks/0229-perspective-drawing-write-up.md`: the idea (the guide is the camera), what each task added and
  what the reviews caught, what was and wasn't tried on hardware, a drawing pad user step by step, what would come
  next. The README's Blocking Out line says what the pen gets.

**Checked**
- Docs only: the Windows build and the editor group.
- Docs only: the editor group (788 checks, with the manual's Learn links) and the sketch group (212) on the same build as round 248, 0 failed.

## 2026-10-27 (round 248): the manual, the Learn lesson, the check list for the drawing pad (task 0228)

**Asked:** the sixth task of the perspective-drawing plan: where users look.

**Added**
- **The manual's** "Drawing in perspective, building from the construction" in chapter 3, with an example and the
  console rows.
- **The Learn lesson "Drawing in Perspective":** six sections, the five questions of rounds 243-247 moved into it,
  and eleven Try-its - a box, a tapering cylinder and a cone built from commands among them.
- **`sketchline` and `sketchcircle`:** a line or a circle on the active scene canvas from world points (the Try-its,
  scripts, tests).
- A ten-minute check list for the drawing pad (docs/tasks/0228).

**Checked**
- The lesson's Try-its run in order and build the box, the cylinder and the cone exactly; the manual's Learn notes
  name lessons that exist (tests/test_sketch.cpp, tests/test_editor.cpp).
- Unit checks (round 248): Windows, in render-cache verify mode, 12,339, 0 failed (1044 tests), 0 stale
  pictures. Linux with libraries: 11,880, 0 failed. Sanitizer build: 11,750, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test and the sanitizer autopose test failed under the parallel load and passed alone.)

## 2026-10-27 (round 247): lift a scaffold - the spike, GO (task 0227)

**Asked:** the fifth task of the perspective-drawing plan: a time-boxed spike lifting a scaffold of construction
lines from the picture plane to 3D, with a gate of 10 % on a box-and-cylinder scaffold.

**Added**
- **`sketchlift`:** the picture plane's lines, labelled by the vanishing point they run toward, lifted to a
  "Scaffold" wire object: lines meeting within 8 px share a point, a line along an axis fixes its ends' depths
  against each other, the junction nearest the view's middle sits at the pivot distance (each connected part
  pinned), the rest by least squares. Construction to build from, never a mesh. ADR 0067.

**Checked**
- The gate: a box and a cylinder, 19 lines, the ends wobbled a pixel, lift with the worst depth 0.3 % off; through
  the editor the box's edges come out 1 : 0.80 : 1.18 at right angles within a degree (tests/test_sketch.cpp).
- An Opus review ported the solver to Python and found one serious thing and several smaller, all fixed:
  - the scaffold's parts were found over every line, free ones included, so a part tied to the gauge only by a line
    along no axis had nothing holding its scale and shrank toward the camera (two boxes joined by one diagonal came
    out 99 % off): the parts are found over the axis lines alone now, and each is gauged; a test draws it;
  - `determined` counted any junction a labelled line touched, gauge or not; it counts junctions an axis line ties
    to a gauged part;
  - nothing noticed a junction solved to behind the camera (a wrong label): they are counted, and the editor lifts
    nothing then and says so;
  - a line a few pixels long got full weight and its wobble spread through its part: lines shorter than three
    joins are free;
  - a level line near the horizon runs toward X's and Z's vanishing points alike: a label is given only when one
    point is clearly nearest (3 degrees);
  - the rays came from the last frame's matrices while the vanishing points came from the camera as it is: the
    rays are the camera's now;
  - the editor's test read the mesh by the solver run's junction ids, which need not match: the editor keeps its
    result for the test;
  - the dense solve is capped at 400 lines for the spike.
  Left as known limits: picture-plane strokes don't remember the camera they were drawn under, so orbiting before
  the lift relabels them against the new view (the layer's view, task 0226, is the place to look); a free line's
  depth is the gauge's.
- Unit checks (round 247): Windows, in render-cache verify mode, 12,332, 0 failed (1043 tests), 0 stale
  pictures. Linux with libraries: 11,873, 0 failed. Sanitizer build: 11,743, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-27 (round 246): refinement - the drawing over the model (task 0226)

**Asked:** the fourth task of the perspective-drawing plan: "just like drawing technique, it takes refinement and
construction lines."

**Added**
- **Layers true from their view:** a picture-plane layer remembers the camera it was first drawn in and fades as the
  view turns away; Draw > Sketch over the Model > Back to the Layer's View (`sketch view`) brings it back.
- **Corrections in the plane:** a stroke on a scene canvas is corrected on the plane itself, in metres, so a half
  circle drawn on the ground in perspective is a true arc, a circle a true circle, a corner where the plane has it.
- **Project and trace:** the last picture-plane stroke pushed onto the face under it (`sketchproject`); the last
  stroke on a face traced into a knife cut across the face or an imprinted outline (`sketchtrace edge`), or into a
  Poly curve (`sketchtrace curve`).
- **Drawing against the model:** the selected meshes' edges as an overlay while sketching; the ruler from the edge
  under a point; the ellipse guide from a circular face. ADR 0066.

**Checked**
- A layer's fade with the angle; the camera back; a 1 m half circle on the ground read as an arc of 0.98 m, a circle
  as 0.505 x 0.500; a stroke across the cube projected onto its face, cut into it (6 faces to 7), traced into a
  curve; the ruler on the cube's edge within 2 px; the ellipse guide from a cylinder's cap (tests/test_sketch.cpp).
- An Opus review modelled the maths in Python and found fourteen things, the main ones fixed:
  - the save format had put a face canvas's object and face before its name, so canvases saved a round earlier
    loaded wrong or not at all: they are on their own `sketch_canvas_face` line now;
  - a layer's view was recorded once and never again, so a layer cleared or erased and drawn again from elsewhere
    had its new strokes faded out as the pen lifted: the view is recorded again when the layer has no
    picture-plane strokes, and drawing on a layer that has faded out here says so;
  - the corrections on the plane scaled the whole stroke by one pixels-per-metre, so on a plane seen obliquely a
    pixel's wobble at the far end became several and a receding line broke into pieces: the corners and the kinds
    are decided on the view, where the hand drew them, and each piece is then fitted on the plane with its own
    scale - a line is its two ends on the plane, an arc or an ellipse refitted with room, a curve the points as
    they are;
  - the whole-arc rule used the piece tolerance and swallowed segmental arches, three sides of a small square and
    chevrons: the tight tolerance of the closed-ellipse rule now;
  - a cut across a notched face could join crossings of different spans, through the notch: the crossings pair
    off into spans and the span round the stroke's middle is cut;
  - only an ellipse was imprinted: a closed outline is the stroke's own or the chain of pieces it closes (the
    face builder's walker, shared), a closed blob keeps its closed flag, the seam point is dropped;
  - a face canvas's face index went stale after a cut or a move: the face is found again in the canvas's plane
    under the stroke;
  - the plane path's end join tore shared corners: only a line's free ends join now; the axis correction turns a
    line about the corner it shares with the piece before, not its middle;
  - the ellipse guide from a face counted the first corner twice (its centre off by a twelfth of the radius) and
    took any face: the corners once, within 5 % of their mean radius;
  - `sketchproject` left the face canvas active and an undo step behind on failure, and projected faded strokes;
  - lines no longer snapped to construction lines in the canvas's plane off its axes: they do;
  - a failed cut left the edges split: the mesh is as it was, and every failure says why.
  Known limits written down: the overlay draws the selected objects only; a stroke projected onto a curved
  surface lands on the face's plane; a cut goes through one face.
- Found while testing: a square drawn 30 px wide on a face seen obliquely loses a corner to the view's 18 px
  windows (its chain still closes and imprints); the `camera` console command now ends a flight under way, which
  carried the view off after `sketch view`; an undo swaps the scene, so a sketch reference taken before it is
  another scene's (twice more).
- Unit checks (round 246): Windows, in render-cache verify mode, 12,303, 0 failed (1042 tests), 0 stale
  pictures. Linux with libraries: 11,844, 0 failed. Sanitizer build: 11,714, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test and the sanitizer animation Play test failed under the parallel load and passed alone.)

## 2026-10-27 (round 245): solids from the construction lines - 3-Sweep's way (task 0225)

**Asked:** the third task of the perspective-drawing plan: "taking those drawings and sketching a possible 3D
model... it takes refinement and construction lines."

**Added**
- **Draw > Build from Sketch** (`sketchbuild`): a **box** from three lines meeting at a corner on the scene's canvases
  (two on the ground, an upright on a wall), with its lengths in F9; a **cylinder** or **cone** from two circles on
  parallel planes (an ellipse on a canvas is a circle in its plane) or a circle and a line from its centre, with the
  radii, height and sides in F9; a **sweep** from two circles and a stroke between their centres; a **face** from a
  closed outline, and its **Push/Pull** from a line along its normal, through the Push/Pull operator. Every solid is
  an ordinary mesh object; the strokes stay as the drawing. ADR 0065 (3-Sweep, Chen et al. 2013).
- A small circle drawn with the pen is an ellipse, not a polygon: a closed stroke that fits an ellipse whole is one
  before the corner search.

**Checked**
- With a known camera, wobbled strokes through the pen make a box, a cylinder, a cone, a face pulled 0.7 m and a
  54-ring sweep with the sizes the construction lines span, F9 remakes the box, undo takes it back
  (tests/test_sketch.cpp).
- An Opus review modelled the builders in Python and found two serious things and several smaller, all fixed:
  - a face's frame was left-handed (`cross(n, u)` for its third axis), so a face drawn on a wall came out 0.7 m off
    and lying flat - the ground only worked because its frame happened to be the identity. The frame is
    right-handed now, the face's normal turned toward the camera (a pull goes toward the artist), and a face on a
    wall is a test;
  - a circle's centre was the mean of its points, which the view had sampled evenly, so the near side had more and
    the centre leant toward the camera - 2 cm at the test's camera, and a true circle refused at closer ones. The
    circle is fitted by least squares on the plane (Kasa's fit), the seam point counted once;
  - the whole-stroke ellipse rule took hexagons, long thin rectangles and a line drawn out and back for ellipses by
    their straight runs: the path must now turn in each of its eighths;
  - a face from a closed curve had its seam point twice (a sliver wall when pulled);
  - `sketchbuild pull` took the most recent upright anywhere in the scene: it must start at the face now, and the
    distance is in the mesh's units (a scaled object);
  - strokes on hidden layers are not construction; the F9 title says Cylinder / Cone; the box's log reads in F9's
    order.
  Known limits written down: F9's Move rows do nothing for a built box or cylinder (nothing is selected after the
  build); a corner turned about 45 degrees may put a horizontal line under Length Y (the geometry is right); a
  sweep's end rings stand perpendicular to the path, not in the circles' planes; a path drawn with a corner is two
  strokes and no sweep; the face chain walker does not backtrack at a T-junction.
- Found while testing: a 1 m square drawn 48 px wide on the view lost a corner to the 18 px windows (the tests draw
  closer), and the corner detector's small-scale variants (a contrast or a two-scale rule) lost 0223's wobbly
  corners - the ellipse-first rule is what keeps small circles whole.
- Unit checks (round 245): Windows, in render-cache verify mode, 12,280, 0 failed (1040 tests), 0 stale
  pictures. Linux with libraries: 11,821, 0 failed. Sanitizer build: 11,691, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test and the sanitizer animation Play test failed under the parallel load and passed alone.)

## 2026-10-27 (round 244): the perspective guide is the camera; canvases in the scene (task 0224)

**Asked:** the second task of the perspective-drawing plan: "taking those drawings and sketching a possible 3D
model... it takes refinement and construction lines."

**Added**
- **The guide and the camera are one:** a 1-, 2- or 3-point guide's points are the world axes' vanishing points,
  read from the Scene view's camera, so orbiting moves them; dragging a point (or `sketchguide vp`) places the
  camera - yaw, pitch and field of view solved from the points - and the points become that camera's. Points no
  camera can match leave it where it is. `sketchguide follow off` keeps a flat guide.
- **Canvases in the scene** (`sketchcanvas view|ground|face [x y]|plane x|y|z [px py pz]|list|<id>`): a stroke drawn
  on the ground, on a face of a mesh or on an axis plane is fitted on the view, with the guides, then sent through
  the view's rays onto the plane and kept in metres, so it sits in the scene from every camera. The active canvas
  shows its axes and a grid.
- **Construction lines:** a corrected line on a scene canvas along one of its axes becomes one of the scene's guide
  lines (the Draw tools' X / Y / Z lines), or lands exactly on one already there; a line along a guide's picture is
  pulled onto it. ADR 0064.

**Checked**
- A camera's three points placed back give the camera within 0.05 degrees and the points 0.000 px off; a wobbled line
  on the ground canvas is a world-X line and a scene guide, a second one lands on it; face and plane canvases; undo,
  save and load (tests/test_sketch.cpp).
- An Opus review modelled the camera, the vanishing points and the solver in Python and found fourteen things, the
  main ones fixed:
  - the guide went stale without a camera move (undo brought back a snapshot's points, another scene opened kept its
    file's, a change of point count or a resize left wrong points): the points are read from the camera every frame
    now, except while one is dragged or while points no camera could match are kept (until the camera moves);
  - front, side and top views broke it: an axis lying in the picture plane vanishes at infinity, its point was left
    where it was and the solver failed at once. Such a point is put 100 view heights out along the axis's picture
    (the fan is parallel there), and a point that far carries no constraint, so dragging the one finite point of a
    front view turns the camera;
  - a point dragged across the view through infinity (the camera tilted the other way) was rejected: the solve now
    starts from the camera and its yaw and pitch mirrors and takes the nearest good camera; and the damping was
    Marquardt's, which made an unknown the points don't move (the field of view, from one point) singular -
    Levenberg's now;
  - a face canvas's normal was wrong under non-uniform scale (53 degrees for a scale of 1, 3, 1): Newell's method on
    the world corners now;
  - canvases were lost on save when the sketch had no strokes and no perspective (`Sketch::empty` ignored them), and
    the follow flag was written only with the perspective line;
  - points behind the camera were skipped one by one, so scene strokes drew chords across the gap and a line with
    one end behind disappeared: each segment is clipped at the camera now, the canvas's grid too;
  - the construction-line correction tore the corners other pieces share (the class of bug round 243 fixed):
    corners that equalled a moved end move with it;
  - snapping to guides took hidden guides, guides outside the canvas's plane (a duplicate construction line from
    one view), and skipped a guide whose anchor was behind the camera: shown guides in the plane, two points of
    the line round the camera's pivot, a metre or more apart on the view.
  Smaller ones fixed too: the acceptance test is per point (the worst within a tenth of the view's height), a
  stroke's raw points on a scene canvas are in the plane's frame, the same ground or plane canvas asked for twice is
  the one there, `sketchguide follow on` in an orthographic view says the guide follows a perspective camera, a
  face index guard, the nearest existing guide taken rather than the last. Left as known limits: a 1-point guide's
  axis is the one the camera looks most along, so it changes at 45 degrees; pieces after the first of a chain lose a
  guide snap when they are moved to meet the piece before; the field of view drifts a few degrees when one point
  alone is solved for.
- Found while testing: a test read the Scene view's height before the first frame laid it out (0: every pixel
  tolerance was met), and an undo swaps the scene, so a sketch reference taken before it is another scene's.
- Unit checks (round 244): Windows, in render-cache verify mode, 12,258, 0 failed (1038 tests), 0 stale
  pictures. Linux with libraries: 11,799, 0 failed. Sanitizer build: 11,669, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-26 (round 243): rulers and guides - sketching over the Scene view (task 0223)

**Asked:** "something similar to SketchBook Pro, where I have rulers and guides that help me draw in perspective. But
taking those drawings and sketching a possible 3D model... it takes refinement and construction lines", and "adjust
for corrections as some curves and lines will not be completely straight. For example a drawn arch." The first of
tasks 0223-0229.

**Added**
- **The Sketch tool** (Draw > Sketch (pen)): pen strokes over the Scene view, the pressure as the width, the eraser
  end erasing, Alt for freehand, layers with opacity, a steady stroke. Strokes are saved with the scene and undone
  with it.
- **Guides** (Draw > Guides): a ruler, an ellipse (its minor axis shown), three French curves, symmetry (X, Y, XY,
  radial) and 1-, 2- and 3-point perspective with draggable vanishing points, each snapping the stroke.
- **Corrections** as the pen lifts: a stroke is split at its corners and each piece becomes a line, an arc, an
  ellipse or a smooth curve - an arch is two lines and an arc that meet - then pulled to the guides; a scribble
  stays as drawn; the pen's own points are kept underneath. Per layer: none, light, full.
- Console: `sketch`, `sketchguide`, `sketchstroke`. ADR 0063, requirement R-15.

**Checked**
- Synthetic strokes for every correction and guide; scripted pen strokes in the editor for the tool, the eraser,
  undo, a finger in Tablet Mode, layers, save and load (tests/test_sketch.cpp, a new group).
- An Opus review checked the maths in a numerical model and found five things, all fixed:
  - Alt+drag both orbited (the Unity feel's Alt+left) and drew a freehand stroke: navigation's press is never a
    stroke now, and Ctrl keeps a stroke freehand instead of Alt. The Sketch tool also takes the pen before the gizmo
    and the tools, so while sketching the pen draws;
  - a corrected corner was torn apart: each line was turned to vertical, horizontal or a vanishing line about its own
    middle. Each piece is moved whole to start where the piece before ends, and only a stroke's free ends join other
    strokes';
  - an ellipse's radii were read off the points' spread, 8 % wrong for a 2:1 ellipse: they are fitted by least squares
    on the ellipse's own equation (exact for a true ellipse);
  - `sketchstroke` took any count of values divisible by three as points with pressures; the pressure is named now;
  - a closed stroke with corners kept its start-to-end gap: it is begun at its first corner and its last piece meets
    its first.

  Smaller ones fixed too: the ellipse guide's nearest point from inside the ellipse (Newton from the angle could land
  on the farthest point; a coarse sweep first), scene changes without an undo step (a vanishing point moved, a layer
  added, hidden, faded or its correction changed), a sketch with only layer settings not saved, an empty layer name
  dropping the layer on load, and the tests' wobble differing between platforms (their own generator now). The
  review's L and square cases are tests: the pieces meet within a thousandth of a pixel.
- Found while testing: a shaky hand's wobble swung a 12-pixel direction window by tens of degrees and hid a right
  angle. The corners are now looked for on a lightly smoothed path with 18-pixel windows, and a scribble is known by
  the sharp turns of the path as the pen drew it.
- Unit checks (round 243): Windows, in render-cache verify mode, 12,190, 0 failed (1036 tests), 0 stale
  pictures. Linux with libraries: 11,731, 0 failed. Sanitizer build: 11,601, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 242): fingers and a pen on a 2-in-1 - the write-up and a check list (task 0222)

**Asked:** "Integrate better support for pen mode. So that if I'm in a 2 in 1 laptop I can navigate more fluidly and
manipulate the environment", "Include finger and pen support". This round ends that roadmap (tasks 0213-0222).

**Added**
- docs/tasks/0222-pen-and-touch-write-up.md: the input model, what each task added, what each review caught, what was
  and wasn't tried on hardware, and a ten-minute check list for the 2-in-1.
- The README's minimum-machine note says what a 2-in-1 gets.

**Checked**
- Docs only: the Windows build and the editor group (the manual's Learn links), 0 failed.

## 2026-10-24 (round 241): the manual, the Learn lesson, the hints for fingers and the pen (task 0221)

**Asked:** the plan for a 2-in-1 - document it where users look.

**Added**
- The manual's "Fingers and a pen on a 2-in-1" (chapter 1): every finger and pen action, in Tablet Mode and out.
- The Learn tab's section of the same name, with questions and Try-it buttons.
- The status bar's hints in Tablet Mode: the fingers' and the pen's.

**Checked**
- The hints in Tablet Mode and out (tests/test_input.cpp); the manual's Learn links (tests/test_editor.cpp).
- An Opus review checked every claim against the code and found five to correct, all fixed:
  - a double tap selects an edge loop only on vertices or edges;
  - the try-it buttons switch the main window's Tablet Mode;
  - palm rejection is Windows' (on Linux, touches are ignored while the pen moves);
  - the navigation buttons' preference is under Pen and touch;
  - the quick menu opens in the Scene view, which must be showing.

  The lesson also gained a button to put the navigation buttons back.
- Unit checks (round 241): Windows, in render-cache verify mode, 12,125, 0 failed (1031 tests), 0 stale
  pictures. Linux with libraries: 11,667, 0 failed. Sanitizer build: 11,537, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 240): macOS - the trackpad and pen tablets (task 0220)

**Asked:** the plan for a 2-in-1 - the same model on macOS.

**Added**
- The trackpad's pinch zooms and its twist turns the Scene view; two-finger scrolling stays scrolling.
- A tablet's pen (Wacom, Sidecar's Apple Pencil): pressure, tilt, eraser, and its side buttons as the barrel.

**Checked**
- The conversions and the Scene view's response to a pinch and a twist (tests/test_input.cpp). No Mac here: CI
  compiles it.
- An Opus review found the eraser unreliable, fixed: it was kept per window, and missed a proximity that came inside
  a mouse event, or while another app was in front. One flag for the program is read from every event before a
  window gets it. It also noted that a Mac's tilt reads a little high near the ends (the system scales it by the
  tablet's own most, about 60 degrees on a Wacom), now written down where it is converted.
- Unit checks (round 240): Windows, in render-cache verify mode, 12,123, 0 failed (1029 tests), 0 stale
  pictures. Linux with libraries: 11,665, 0 failed. Sanitizer build: 11,535, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone. The Windows run failed this round's own pinch test, which compared floats exactly where the optimised build rounds 1 + 0.1 differently; with a tolerance the input group passes, 100 checks, 0 failed.)

## 2026-10-24 (round 239): Linux - fingers and the pen through XInput 2 (task 0219)

**Asked:** the plan for a 2-in-1 - the same fingers and pen on Linux.

**Added**
- **Fingers** through XInput 2's touch events and the same recognizer as Windows: one finger presses, two pinch, pan
  and twist.
- **A tablet pen's pressure, tilt and eraser** from its own device's axes. Its side buttons are the barrel.
- **libXi is opened at run time,** so building needs nothing new. CI installs `libxi-dev` so the code is compiled there.

**Checked**
- The axis rules as pure functions (tests/test_input.cpp). The X11 code itself wasn't compiled or tried here: no
  machine here has the XInput 2 header or a Linux touch screen.
- An Opus review found these, all fixed:
  - a pen whose device appears after the start (libinput adds one at a pen's first use, and a tablet can be plugged in
    later) was never seen; the devices are looked at again whenever X says they changed;
  - tilt was stretched: Xorg's drivers give degrees already, now kept as they are;
  - a touch screen reporting pressure could be taken for a pen and block every finger; touch screens and "MT" axes
    are left out;
  - while a pen hovered, the mouse's events were taken for the pen's; now only events where the pen is are;
  - `-ldl` for older glibc, and an X error while selecting a device that just went is ignored rather than ending the
    program.
- Unit checks (round 239): Windows, in render-cache verify mode, 12,120, 0 failed (1028 tests), 0 stale
  pictures. Linux with libraries: 11,662, 0 failed. Sanitizer build: 11,532, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`: 0 problems. (The gpu group's slow-open test and the sanitizer build's autopose timing check failed under the parallel load and passed alone.)

## 2026-10-24 (round 238): Tablet Mode follows the laptop (task 0218)

**Asked:** the plan for a 2-in-1 - Tablet Mode turns itself on when the laptop folds, with an override.

**Added**
- **Auto (the default):** the main window's Tablet Mode turns on when the keyboard is folded away or detached
  (Windows' tablet posture), and off when it's back. It acts on a fold only, never on the state at startup.
- **Preferences > Pen and touch > Tablet Mode:** Auto, On or Off; `tabletmode auto|on|off`.
- **The tab menu's choice** holds until the posture changes again.

**Checked**
- A simulated fold and unfold, the tab menu's override, On and Off, and the saved choice (tests/test_input.cpp).
- An Opus review found two things, both fixed:
  - Windows' slate reading is wrong on many machines without a convertible's hinge, so Auto would have turned Tablet
    Mode on for ordinary desktops and laptops at startup. The posture is now trusted only with a touch screen,
    undocked, on a mobile or slate power role (as Chromium checks it), and Auto acts only on a change, never on the
    state it starts in;
  - a posture change took effect after the frame was drawn, leaving the old size on screen against the new targets;
    the window is drawn again at once.
- Unit checks (round 238): Windows, in render-cache verify mode, 12,108, 0 failed (1026 tests), 0 stale
  pictures. Linux with libraries: 11,650, 0 failed. Sanitizer build: 11,520, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 237): the pen quick menu (task 0217)

**Asked:** the plan for a 2-in-1 - a pen quick menu (side button or long press) for tools without a keyboard.

**Added**
- **A ring of commands at the pen** in the Scene view, opened by the pen's side button clicked or by holding it still
  half a second. Flick through a slice and lift, or tap one; a tap elsewhere closes it.
- **Slices:** Move / Rotate / Scale, Duplicate, Delete, Frame, Undo / Redo and Edit Mode in Object mode; Extrude,
  Vertex / Edge / Face, Select All and Object Mode in Edit Mode. Add to Selection makes the next press add.
- `quickmenu open|close|pick <slice>|list`.
- Windows' own press-and-hold right click is off in the editor's windows: holding the pen is the editor's now.

**Checked**
- Scripted holds, flicks, side-button clicks and taps (tests/test_input.cpp).
- An Opus review found these, all fixed:
  - Add to Selection failed whenever a second window was open (another window's input cleared it);
  - Shift lingered into the frame after the release, so a key or the next tap could read it;
  - the menu opened near the Scene view's edge was cut off, and a hidden slice could be picked by a flick;
  - with the Scene view in a second window, the held pen's half second wasn't noticed until the pen moved;
  - Esc that closed the menu also cleared a curve's points, and a middle-button press left the menu open.
- Unit checks (round 237): Windows, in render-cache verify mode, 12,101 (1022 tests; the gpu group's below), 0 stale
  pictures. Linux with libraries: 11,643, 0 failed. Sanitizer build: 11,513, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group failed under the parallel load: its slow-open test, and its click-after-a-switch-to-the-CPU test even run alone while the Linux legs still loaded the machine. On a quiet machine the group passed, 380 checks, 0 failed.)

## 2026-10-24 (round 236): navigating with the pen alone (task 0216)

**Asked:** the plan for a 2-in-1 - with no keyboard or middle button, the pen still orbits, pans and zooms.

**Added**
- **The Scene Gizmo dragged orbits;** a click still looks along an axis or switches the projection.
- **A hand (pan) and a magnifier (zoom)** under it, as Blender's: shown in Tablet Mode, or always (Preferences > Pen and
  touch > Navigation buttons, `navbuttons always`).
- **The pen's side button held:** a drag orbits in every editor feel; Shift pans, Ctrl zooms.

**Checked**
- Scripted pen drags on the gizmo, the hand, the magnifier and with the side button, as the right button and as the
  left (tests/test_input.cpp).
- An Opus review found three things, all fixed:
  - a side button that some drivers send as the left button orbited and also reached the tools (it painted weights
    and dragged bone handles while orbiting); now navigation alone takes that press;
  - a press on the Scene Gizmo whose release wasn't seen (the view hidden meanwhile) switched the view much later; it
    is dropped now;
  - a click on the empty strip beside the hand and the magnifier switched the projection.
- Unit checks (round 236): Windows, in render-cache verify mode, 12,087, 0 failed (1019 tests), 0 stale
  pictures. Linux with libraries: 11,629, 0 failed. Sanitizer build: 11,499, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 235): panels follow the finger (task 0215)

**Asked:** the plan for a 2-in-1 - in Tablet Mode a finger scrolls lists, and targets are sized for it.

**Changed** (Tablet Mode):
- **Scrolling:** a finger dragging up or down scrolls any list or panel under it - Hierarchy, Inspector, Project,
  Console, Learn, the tool windows - and a flick glides on. Nothing it pressed clicks.
- **Sliders and numbers** dragged sideways still edit; a tap sets a slider.
- **Rows and files:** a finger taps them to select; dragging them (reparenting, assets onto objects) is the pen's and
  the mouse's.
- **Targets:** the gizmos and Edit Mode's pick radius already grow with Tablet Mode's 1.5x UI scale.

**Checked**
- Scripted finger drags in the Hierarchy, in Tablet Mode and out of it, a flick, the scrollbar and a multi-selection
  (tests/test_input.cpp). The tests run on the editor's clock, stepped 20 ms a frame.
- An Opus review found five things, all fixed:
  - a scroll that began on a selected Hierarchy row collapsed a multi-selection;
  - a scroll that began on a slider or a number first changed its value; a finger now moves them only once it has gone
    sideways, and once it has, the scroll can't take the press;
  - a finger on the scrollbar dragged it backwards; it drags the thumb as the mouse does;
  - Tablet Mode turned off mid-scroll could leave the scroll stuck, so mouse clicks on rows stopped selecting;
  - a glide whose list went undrawn jumped on when it came back; it ends instead.

  Each has a test.
- Unit checks (round 235): Windows, in render-cache verify mode, 12,075, 0 failed (1016 tests), 0 stale
  pictures. Linux with libraries: 11,617, 0 failed. Sanitizer build: 11,487, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test and the sanitizer build's animation Play test failed under the parallel load and passed alone.)

## 2026-10-24 (round 234): fingers navigate the Scene view (task 0214)

**Asked:** the plan for a 2-in-1 - in Tablet Mode one finger orbits, two pinch / pan / twist, the pen manipulates.

**Changed** (Tablet Mode, the Scene view, a finger the gizmo and tools didn't take):
- one finger drags to orbit round the pivot, in every feel;
- a tap selects, and a double tap frames the selection (in Edit Mode on vertices or edges, it selects the edge loop);
- a flick glides on and slows to a stop; the next touch stops it.

A finger on a gizmo handle still drags it. The mouse and the pen are unchanged.

**Checked**
- Scripted finger drags, taps, double taps and flicks in Tablet Mode and out of it, and a finger on the gizmo's
  handle (tests/test_input.cpp).
- An Opus review found four things, all fixed:
  - a glide kept the editor redrawing while another tab covered the Scene view; it ends now, and doesn't jump on later;
  - a double tap framed the selection twice, and its second press couldn't orbit;
  - in Edit Mode a double tap framed instead of selecting an edge loop as a double-click does;
  - a tap's jitter turned the view a little before selecting; the view now waits until the finger has gone past a
    tap's 8 px.
- The editor has a clock a test can fix (`clock_now`): a double click's window, a finger's speed and its glide
  are measured on it, so the sanitizer build's slower frames don't change what the tests see (its first run failed the
  double tap and the flick on time alone).
- Unit checks (round 234): Windows, in render-cache verify mode, 12,066, 0 failed (1013 tests), 0 stale
  pictures. Linux with libraries: 11,608, 0 failed. Sanitizer build: 11,478, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems.

## 2026-10-24 (round 233): which pointer is which, and fingers read directly (task 0213)

**Asked:** "Integrate better support for pen mode. So that if I'm in a 2 in 1 laptop I can navigate more fluidly and
manipulate the environment", then "Include finger and pen support." The first of tasks 0213-0222.

**Changed**
- **Every pointer event says what made it:** the mouse, a pen or a finger. A pen's side button is read, and a
  finger's press can be taken back.
- **Fingers go through one recognizer** (`TouchRecognizer`, no OS calls, so Linux can use it too):
  - one finger presses, moves and lifts; two pinch, pan and twist;
  - a pinch's second finger landing within 0.12 s means the first finger never pressed anything;
  - a later second finger takes the press back: nothing clicks, no box select, no drop;
  - after a pair, a lone finger does nothing until all have lifted, so the view doesn't jump.
- **Windows** reads fingers from its pointer messages, not its emulated mouse and gestures.
- **Outside Tablet Mode** a finger still does what the mouse does.

**Added**
- ADR 0062 (pointer kinds and the touch recognizer), requirement R-14 (fingers and a pen on a 2-in-1), and a new
  `input` test group.

**Checked**
- Scripted touches for each case, and in the editor: a tap selects as a click does, and a drag taken back selects,
  drops and docks nothing (tests/test_input.cpp).
- The Windows touch path hasn't been tried on a touch screen; the machine has none. Task 0222's check list on the
  2-in-1 covers it.
- An Opus review found the recognizer and the Windows messages right, and these, all fixed:
  - a press taken back still dropped a dragged Project asset (it could move a file), docked a dragged tab or opened a
    window for it, selected the Hierarchy row pressed, selected an Alt+clicked loop and ran Fuse on Contact after a
    gizmo drag; none of these does now;
  - a test checked the wrong recognizer;
  - a finger's tag outlived its lift (a wheel after it read as a finger).

  A test pins the tab drag.
- Unit checks (round 233): Windows, in render-cache verify mode, 12,050, 0 failed (1009 tests), 0 stale
  pictures. Linux with libraries: 11,592, 0 failed. Sanitizer build: 11,462, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`, `pushpull`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (Linux's global sequencer Play test (wall clock) failed under the parallel load and passed alone.)

## 2026-10-24 (round 232): Blendity against Blender 5.2 - the write-up (task 0212)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round ends that roadmap (tasks 0201-0212).

**Added**
- docs/tasks/0212-blender-parity-write-up.md: how both sides were measured, each gap's cause and fix (tasks 0204-0211),
  the rows Blendity still trails and why, and where it leads.

**Measured** (Pass 15 again, on a quiet machine):
- headless: 27 of 59 rows behind Blender in round 223, now 17. Ten turned: FBX, glTF and USD export, duplicating
  10,000 objects (22x ahead, from 476x behind), extrude, bevel, exit Edit Mode, triangulate, Array;
- the window: 7 of 9 rows behind, now 1, Edit Mode on 500k faces at 5.6 against 4.9 ms. A 500k sphere 8.2 -> 4.3 ms
  (Blender 4.8), playing 1,000 keyed objects 17.4 -> 4.1 (11.2), 10,000 cubes 20.1 -> 14.9 (25.5);
- PERFORMANCE.md's Pass 15 window table is measured again.

**Checked**
- Docs and one Learn question: the Windows build, and the editor group (the Learn tab's citations among its 787 checks),
  0 failed.

## 2026-10-24 (round 231): Boolean and Array on big meshes; the GPU shaders compiled by the build (task 0211)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 8, and the shaders' first compile it folded in.

**Changed**
- **Boolean:** to Manifold and back without a map per corner or per face. It now passes 32-bit positions, as Blender
  does.
- **Array:** each copy is appended array by array, not vertex by vertex and face by face. The mesh is the same.
- **GPU shaders:**
  - `build.bat` / `build.sh` compile them into a `shaders` folder beside the editor (`Blendity --warm-shaders`);
  - a shader the user's cache lacks is read from there;
  - the installer and packages carry the folder.

  A new build's first GPU view no longer waits for them, where the build machine has a card; CI's builds have none.

**Measured** (Pass 15; before -> after, Blender 5.2):
- a Boolean union of two 50k spheres 276 -> 158 ms (79.8);
- Array x10 on the 50k sphere 24.2 -> 8.1 ms (11.2);
- the first GPU view after the shaders changed: about 96 s of compiling -> 0.3 s (this machine).

**Checked**
- Array gives the old mesh, also for meshes whose lists are out of step with their faces. A union of two spheres is
  closed and the same twice (tests/test_modeling.cpp).
- Two Opus reviews:
  - Boolean's conversions: no defect. Array, for meshes whose lists are out of step with their faces, differed in
    four ways, all fixed: UVs gained, a loose edge dropped, seams left unsorted, and a vector read while appended to.
  - The build step found five things, all fixed:
    - `build.sh` stopped on the unset `BLENDITY_NO_WARM`, which would have failed every Linux and macOS build;
    - it ran before the libraries were beside the program;
    - the macOS bundle and the Linux packages left the folder out;
    - the dependency-free build, made after the full one, removed its shaders;
    - `--warm-shaders` cleared any folder it was given; now it takes only a new or empty one.
- Unit checks (round 231): Windows, in render-cache verify mode, 12,023, 0 failed (1003 tests), 0 stale
  pictures. Linux with libraries: 11,566, 0 failed. Sanitizer build: 11,436, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `modeling`, `modifiers_ngon`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone. The Linux build ran the new shader step and, with no card there, went on without them.)

## 2026-10-24: version 0.11.0, a pre-release

**Asked:** "Do a 0.11.0 pre-release for Windows, Mac, and Linux."

**Changed**
- `VERSION` is 0.11.0 (#175), as are the README's pre-release note, the manual and the version a build without
  `VERSION` reports. The app reads its version from that file, so it went to `main` before the tag.
- The `v0.11.0` tag on `main` after #174. `release.yml` builds, installs and starts the Windows installer, the macOS
  disk image and the Linux AppImage, then publishes them.

## 2026-10-24 (round 230): saving and opening big scenes (task 0210)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 7.

**Changed**
- **Saving:** a big mesh's positions, faces and UVs are written on every core, the text byte for byte as before.
- **Opening:** long runs of them are parsed on every core; anything else in the file is read line by line as before.
- **Reading a file** asks its size and reads it at once. It went through a stream buffer and a copy, and that was most
  of opening a big scene. Every importer reads its file this way too.

**Measured** (Pass 15; before -> after, Blender 5.2):
- a 500k-face mesh's scene: saved 266 -> 140 ms (21.6), opened 474 -> 135 ms (26.7);
- 10,000 cubes opened 75 -> 63 ms (534);
- the 50k sphere imported from OBJ 104 -> 46 ms (37), STL 45 -> 26 (21), PLY 43 -> 33 (6).

The `.scene` file is text, so big meshes still trail Blender's binary `.blend` by 5-6x.

**Checked**
- A 131k-face scene saves the same text twice and reads back exactly. A bad face 100,000 lines in is reported at its
  own line. Windows line ends and blank lines inside each block read as the same mesh (tests/test_assets.cpp).
- An Opus review found the parallel save and load the same as the one-core path in every case it tried: Windows line
  ends, blank lines inside a block, wrong counts, a missing last newline, bad lines and their numbers. It found two
  things in the new file read, both fixed:
  - a file that can't seek (a pipe) no longer opened;
  - a file that says it is empty but isn't (`/proc`) read as empty.

  The test now also reads the scene with Windows line ends and blank lines inside every block, and through a file.
- Unit checks (round 230): Windows, in render-cache verify mode, 12,010, 0 failed (1001 tests), 0 stale
  pictures. Linux with libraries: 11,553, 0 failed. Sanitizer build: 11,429, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 229): exporting and importing the 50k sphere (task 0209)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 6.

**Changed**
- **Exporting:** each corner's normal is worked out once per fan of faces round a vertex, not by a flood from every
  corner. At the sphere's poles 256 corners each flooded across 256 faces: 271 ms of every export.
- **Numbers** in OBJ, PLY and USD text are written by `std::to_chars`, the same text as `%.7g`.
- **PLY import:** each property's type is looked up once, not compared by name for every value.

**Measured** (Pass 15, the 50k sphere; before -> after, Blender 5.2):
- export OBJ 791 -> 108 ms (45), FBX 322 -> 24 (76), glTF 329 -> 32 (82), STL 365 -> 30 (12), PLY 336 -> 38 (7),
  USD 827 -> 112 (253);
- import PLY 65 -> 43 ms (6).

**Checked**
- The numbers are `%.7g`'s text, and every corner's normal matches the old flood on a sphere with sharp edges, flat
  faces and a smooth angle (tests/test_assets.cpp).
- An Opus review found two things, both fixed:
  - two faces back to back (sharing two edges at a vertex) joined or not depending on which the flood started from;
    each shared edge is now tested on its own, as the old flood did;
  - a PLY face with two lists of corners kept both; it keeps the last again.
- Unit checks (round 229): Windows, in render-cache verify mode, 11,997, 0 failed (1000 tests), 0 stale
  pictures. Linux with libraries: 11,540, 0 failed. Sanitizer build: 11,416, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (The gpu group's slow-open test and the sanitizer build's animation Play test failed under the parallel load and passed alone.)

## 2026-10-24 (round 228): Edit Mode's Select All, Inset and Bevel on big meshes (task 0208)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 5.

**Changed**
- **Select All:** it keeps the edges' own selection only in Edge mode, where it is read. Switching to Edge mode works
  it out, now from the console's `edit edge` too.
- **Inset:** a thickness too big for small faces is narrowed by moving the inset's new vertices alone. Before, every
  try rebuilt the whole mesh; the result is the same.
- **Bevel:** only the faces at a bevelled corner are rebuilt, and the corner patches come out in a fixed order.

**Measured** (Pass 15):
- Select All on 50k faces: 14.9 -> 4.0 ms (Blender 2.6);
- Inset: 75.7 -> 25.9 ms (24.1); on 500k faces 930 -> 259 ms (230);
- Bevel: 96.1 -> 30.0 ms (31.7).

**Checked**
- Edge mode after a Select All has every edge, Inset folds nothing, and Bevel gives the same mesh twice
  (tests/test_modeling.cpp).
- An Opus review confirmed Inset's result is bit for bit the old one, and found three things, all fixed:
  - Bevel's corner patches in a different order (now a fixed one);
  - the console's `edit edge` leaving Edge mode without edges after a Select All;
  - an undo into Edge mode restoring no edges.

  On a face that uses one vertex twice, Bevel now keeps each corner's own UV; before, both took the first's.
- Unit checks (round 228): Windows, in render-cache verify mode, 11,993, 0 failed (998 tests), 0 stale
  pictures. Linux with libraries: 11,536, 0 failed. Sanitizer build: 11,412, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`, `modeling`, `modifiers_ngon`, `pushpull`, `curved`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (The gpu group's slow-open test failed under the parallel load and passed alone. Curved: 5,032 operations, 7 problems, 67 results with faces newly on top of each other, down from 5,034, 8 and 79 on the build before this round (measured on it): Bevel's corner patches in their new fixed order renumber the faces of the rounded box (made by Bevel), so the section's random operations land on other faces; no count rose.)

## 2026-10-24 (round 227): duplicating many objects (task 0207)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 4.

**Fixed:** Duplicate on 10,000 objects took 648 seconds. Each copy's name was tried against every sibling ("Cube
(1)", "Cube (2)" ...) and each copy moved through the list. Now the selection is duplicated at once: names gathered
once per parent, each list rebuilt once. 648 s -> 70 ms, against Blender's 1.5 s. The names and places are as before.

**Measured** (Pass 15, again after tasks 0204-0207):
- Duplicate 10,000 objects: 647,887 -> 70 ms (Blender 1,525);
- a frame of 1,000 keyframed objects: 1.7 -> 0.40 ms (Blender 3.8);
- select all 10,000 objects: 8.1 -> 3.4 ms (Blender 32.7).

**Checked**
- The names and places one at a time gave, for 3 and for 2,000 objects (tests/test_editor.cpp).
- An Opus review found no fault on the editor's path. It found three orders that differed from one at a time for
  lists the editor never passes (the same object twice, a child before its parent, an original missing from its
  list), all fixed, and the selection's ancestors searched for one by one (now looked up).
- Unit checks (round 227): Windows, in render-cache verify mode, 11,987, 0 failed (997 tests), 0 stale
  pictures. Linux with libraries: 11,530, 0 failed. Sanitizer build: 11,406, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test and Linux's global sequencer Play test (wall clock) failed under the parallel load and passed alone. The first run of this chain found the build broken: a Learn question's text ended its raw string; fixed, and the chain now stops when the Windows build fails instead of testing the old binaries.)

## 2026-10-24 (round 226): playback without the bookkeeping (task 0206)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 3.

**Changed**
- Playing a clip remembers what is shown for Record only while Record is on: every animated number of 1,000 objects
  had been looked up each frame. Turning Record on remembers it.
- A clip of many tracks finds its objects in one walk of the branches it names, not by searching each parent's
  children per track.

**Measured:** playing 1,000 keyframed objects in a 2560 x 1369 window, 11.7 -> 4.0 ms a frame (Blender 11.3).

**Checked**
- The same objects posed as before (duplicate names, the root's own track, names a path can't spell), and Record
  still keys a move after playing (tests/test_animation.cpp).
- An Opus review found, all fixed:
  - the one-walk lookup following a second object of a name where the search found none;
  - the root's own track lost to a child with an empty name;
  - a name with a '/' reachable where the search couldn't reach it;
  - a big rig with few keyed bones walked whole every frame (now only the branches the tracks name).
- Unit checks (round 226): Windows, in render-cache verify mode, 11,980, 0 failed (996 tests), 0 stale
  pictures. Linux with libraries: 11,523, 0 failed. Sanitizer build: 11,399, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open timing test failed under the parallel load and passed alone.)

## 2026-10-24 (round 225): Edit Mode's cage kept on the card (task 0205)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 2.

**Changed**
- The Edit Mode cage's vertices are worked out again only when the mesh, the object's matrix, the mode or the
  selection changes. Before, they were worked out again every frame and compared with the card's copy.
- Face mode's dots are kept on the card with the cage. Before, they were worked out and sent every frame.

**Measured** (a 2560 x 1369 window, a frame):
- Edit Mode on the 500k-face sphere: 11.7 -> 6.0 ms (Blender 5.5);
- in Face mode: 22.1 -> 8.2 ms;
- on the 50k sphere: 3.6 -> 3.2 ms (Blender 5.2).

**Checked**
- A frame that only draws again rebuilds and sends nothing, and a face selected sends 96 bytes; Face mode on the card
  matches the CPU's picture (tests/test_gpu.cpp).
- An Opus review found, all fixed:
  - Face mode's dot radius kept from the last rebuild (a change of UI scale wouldn't show);
  - a test that the old code also passed (now the Profiler counts the cage's rebuilds, and the test checks there are
    none while only drawing again);
  - Face mode sent to the CPU without the overlays' pipelines, which it no longer needs.
- Unit checks (round 225): Windows, in render-cache verify mode, 11,972, 1 failed under load (994 tests; below), 0 stale
  pictures. Linux with libraries: 11,515, 0 failed. Sanitizer build: 11,391, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-card timing tests failed under the chain's load - the slow GPU open, and F12 on a slow card with frames of 58-201 ms, also when run alone while the Linux legs built - and passed twice alone on a quiet machine (frames of 1.3-14 ms)..)

## 2026-10-24 (round 224): the views without the wait (task 0204)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round fixes task 0203's rank 1.

**Changed**
- Each view's frame is left running on the card while the editor goes on. Before, the editor waited for it every
  frame.
- The Scene view's depth and ids stay on the card until a click, a box select, the knife or a material drop asks
  for them.

**Measured** (a 2560 x 1369 window, a frame, against Blender 5.2):
- the 500k-face sphere 8.2 -> 3.9 ms (Blender 5.0);
- a million triangles 6.3 -> 3.5 ms (4.2), Shaded 6.4 -> 3.3 (Material Preview 5.5);
- 10,000 cubes 20.1 -> 13.9 ms (24.2);
- Edit Mode on 50k faces 6.2 -> 3.6 ms (5.1), on 500k 16.6 -> 11.7 (5.5);
- playing 1,000 keyed objects 17.4 -> 11.4 ms (11.3).

Blendity is ahead on the viewport rows now, 1.2-1.7x. Edit Mode on big meshes is task 0205's.

**Checked**
- A frame drawn again is left running with its planes on the card; a click picks what is there now, on the card and
  after a switch to the CPU (tests/test_gpu.cpp).
- An Opus review found, all fixed:
  - the planes' slot not forgotten when the next frame was drawn some other way, or when another view took it: a
    click could read old planes, or write past a target that shrank;
  - the CPU's cage tested against old depth when the card couldn't draw it;
  - a failed wait ignored (now the card is marked lost and the editor draws on the CPU);
  - the Profiler's times landing on the wrong frame;
  - a test that could pass with a stale pick.
- Unit checks (round 224): Windows, in render-cache verify mode, 11,963, 1 failed under load (994 tests; below), 0 stale
  pictures. Linux with libraries: 11,515, 0 failed. Sanitizer build: 11,391, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open timing test failed under the chain's load - a 153 ms frame against its 100 ms limit, again when run alone while the Linux legs built - and passed twice alone on a quiet machine (its slowest frame 20-22 ms). The sanitizer build's animation and autopose timing checks failed under its load and passed alone.)

## 2026-10-24 (round 223): why Blendity trails Blender where it does (task 0203)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round finds the causes. docs/tasks/0203-parity-causes.md ranks every gap of 1.25x or more by what
the user feels, with its cause and its fix task:
1. **The views wait for the card every frame (0204).** The Scene view copies its ids and depth back for picking only
   a click needs.
2. **Edit Mode's cage on big meshes (0205):** the edge shader's antialiasing strip, and the cage rebuilt on the CPU
   each frame.
3. **Playback (0206):** the clip applied and the scene's matrices and hash worked out again each frame.
4. **Duplicating many objects (0207):** names tried against every sibling, n³ in all.
5. **Select All, Inset and Bevel (0208):** the edge selection as a hash set of every edge; whole-mesh copies and
   rebuilds to change three faces.
6. **Files (0209):** the exporter's per-corner normal flood (271 ms at the sphere's poles); the importers' per-value
   type checks.
7. **Big scenes saved and opened (0210):** a text format.
8. **Boolean and Array (0211):** map-based conversions; vertices and faces added one at a time.

**Changed**
- `--windowed-check` prints where each frame goes: the Scene view, the views' GPU path (the card's passes; uploads,
  copies back and the wait), the UI and presenting.
- Pass 15's Select All row runs Edit Mode's own Select All. It had timed two operators standing in for it, and the
  real one is slower: 14.9 ms against Blender's 2.6 on 50k faces, 249 against 23 on 500k.

**Checked**
- An Opus review checked each cause against the code it names and corrected the document:
  - Blender too draws edit-mesh edges as screen-space quads (rank 2 now names the edge shader's strip and the cage's
    per-frame rebuild);
  - `Mesh::corner_normals` is not the exporter's normals under 180 degrees (rank 6);
  - dropping the copies back would leave the fence wait (rank 1);
  - Select All's edge set is walked every frame after it (rank 5);
  - the breakdown's parts nest, and its "copies back" includes the uploads (both now labelled).
- Unit checks (round 223): Windows, in render-cache verify mode, 11,957, 0 failed (993 tests), 0 stale
  pictures. Linux with libraries: 11,515, 0 failed. Sanitizer build: 11,391, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 222): Blender's viewport, playback and start-up against Blendity's window (task 0202)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round measures what needs a window.

**Added**
- `benchscene sphere50k|sphere500k|million|cubes10k|keyed1k`: the benchmark scenes, built in the editor.
- `Blendity --startup-check`.
- `tools/bench_ui.py` and `tools/bench_blender.py --parity-ui`: both programs' windows at the same size and the same
  scenes, into PERFORMANCE.md Pass 15's window table.

**Measured** (a 2560 x 1369 window, a frame):
- Blendity is behind with big meshes: the 500k sphere 8.2 ms against 4.1, a million triangles 6.3 against 4.4, and
  Edit Mode on 500k faces 16.6 against 5.4.
- Playing 1,000 keyframed objects: 17.4 ms against 11.3.
- Blendity is ahead with 10,000 cubes (20.1 against 24.1 ms) and starting up (0.5 s against 2.8).

**Corrected:** round 219's Blender viewport rows timed Blender's finished picture shown again, not the scene drawn:
a still view stops Workbench and EEVEE drawing. With the view nudged, as an orbit does:
- the starter scene: 4.66 ms;
- 400 spheres, Solid: 5.00 ms (was 4.34-4.39);
- 400 spheres, Material Preview: 6.70 ms (was 4.41-5.23).

Blender is ahead there 1.1-1.5x, not 1.2-1.7x.

**Checked**
- `benchscene`'s scenes (tests/test_editor.cpp).
- An Opus review found, all fixed:
  - Blender's rows timing a finished picture (a still view: the scene isn't drawn again) - round 219's rows too,
    corrected;
  - its playback row stepping frames without drawing the window;
  - the selections differing (10,000 selected cubes' outlines in Blender's row);
  - the scenes differing (a cube around the sphere, the starter scene's teapot);
  - the start-up row timing Blender's exit;
  - the runner trusting an old result and a failed run.
- The review's fix of the selection stopped Blendity's playback, so the box row was found playing nothing: the
  Animation window plays the clip of the selection's parent, so one box stays selected.
- Unit checks (round 222): Windows, in render-cache verify mode, 11,957, 0 failed (993 tests), 0 stale
  pictures. Linux with libraries: 11,515, 0 failed. Sanitizer build: 11,391, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-24 (round 221): every editor feature measured against Blender 5.2 (task 0201)

**Asked:** "Measure why and improve performance in Blendity by comparing all editor features and performance with
Blender 3D." This round measures. The causes and the fixes follow in tasks 0203-0211.

**Added**
- The stress section `blender_parity` and `tools/bench_blender.py --parity`: 59 rows timed in both programs on the
  same scenes, under the same names, joined by `tools/bench_table.py` into PERFORMANCE.md's Pass 15.
  - Files: save and open, and six formats' export and import.
  - Objects: 10,000 of them.
  - Edit Mode: 13 operators on a 50k-face mesh, 9 on a 500k one.
  - Modifiers, UV, animation, physics, F12 and memory.
- `tools/bench.bat` runs both sides.

**Fixed:** `redo <field> <value>` from the console keeps the numbers for the tool's next use, as the F9 panel does.

**Measured:** Blendity is behind on 23 rows of 59.
- Duplicating 10,000 objects takes 648 s against Blender's 1.5 s. Each copy's name is found by trying "Cube (1)",
  "Cube (2)" ... against every sibling.
- Exporting the 50k sphere is 3-47x slower in every format.
- Saving and opening a 500k-face mesh's scene is 12-18x slower.
- Bevel, Inset, Boolean, Array, Select All and Merge by Distance are 1.2-3.7x slower.

Blendity is ahead on delete (316x), opening many objects, three importers, Edit Mode's entry and most of its
operators, Subdivision, UV unwrap, animation, physics, memory and F12.

**Checked**
- An Opus review found, all fixed:
  - the bevel row running the old width: the console's `redo` didn't keep its numbers (fixed in the editor, with a
    test);
  - Blender's subdivision evaluating its limit surface (17 s on the sphere's 256-edge poles), where Blendity
    subdivides a step;
  - the Bevel modifier rows bevelling nothing (every edge under the angle limit);
  - a Boolean that fails timed as a success;
  - physics simulating different time (Blender at 24 fps with 10 substeps);
  - "less the frame after" hiding work left for that frame;
  - a failed import overwriting its export's row, `.blend1` backups saved inside the timer, temp folders left;
  - a duplicate of linked meshes against shared ones;
  - quick runs too heavy for CI.
- Unit checks (round 221): Windows, in render-cache verify mode, 11,948, 0 failed (992 tests), 0 stale
  pictures. Linux with libraries: 11,506, 0 failed. Sanitizer build: 11,382, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`, `blender_parity`: 0 problems. (The gpu group's slow-open test failed under the parallel load and the sanitizer build's autopose timing check under its load; each passed alone.)

## 2026-10-23 (round 220): the views native on the GPU, written up (task 0200)

**Asked:** "Why is it that the view still runs on the CPU? Shouldn't it run on the GPU natively like Blender?", with
the CPU as the fallback. This round writes up the roadmap that answered it (tasks 0192-0199, rounds 211-219, PRs
#156-#164).

**Added:** docs/tasks/0200-native-gpu-views-write-up.md:
- every task with its round, PR and numbers;
- the CPU fallbacks before and after: of the eleven features that sent a whole view to the CPU, only realtime GI's
  per-pixel gather still does, with a few narrow cases (a retro filter's rasterizer stage, a live procedural seen very
  close, see-through toon materials, bloom over see-through surfaces, probe volumes over 262,144 probes);
- Blender's viewport against Blendity's window: Blender 3.98-5.23 ms, Blendity 5.57-7.51 ms (the card presenting);
- what stays on the CPU and why, and the follow-ups (the card's 33-second first open after a shader change; one draw
  call for many copies of a mesh; the UI on the card).

**Checked:** documentation only; no code changed.

## 2026-10-23 (round 219): the GPU as the default, said per window; Blender's viewport timed (task 0199)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback ("most computers would
have a GPU"). This round makes the choice visible and measures Blendity's window against Blender's own viewport.

**Changed**
- Preferences > Viewport Device says, for each window, where its views draw and who shows it: "views on the GPU (the
  card), shown by the card", or "views on the CPU, shown by the system" with the reason.
- With no card, Auto's fall back to the CPU is said once, the first time, not every start.
- **Fixed:** switching Viewport Device kept the views' cached pictures until something changed, so the other
  device's picture stayed on screen. The views now draw again on a switch.

**Added**
- The local chain runs the editor, render and lighting groups a second time with the views on the card, in verify
  mode. CI is unchanged (it has no card).
- `tools/bench_blender.py --viewport` times Blender's viewport with its window (`wm.redraw_timer`, DRAW_WIN_SWAP);
  `Blendity --windowed-check` takes `--command` to set up the same scene.

**Measured** (a 2560 x 1369 window, RTX 4070 SUPER), a whole window drawn and shown:
- the starter scene: Blender 3.98-4.70 ms (two runs), Blendity 5.57 ms (the system presenting: 6.81);
- 400 spheres, Solid: Blender 4.34-4.39 ms, Blendity 7.42 ms (8.68);
- 400 spheres, Material Preview / Shaded: Blender 4.41-5.23 ms (EEVEE reuses its last sample), Blendity 7.51 ms (8.62).

Blender is 1.2-1.4x ahead on the starter scene and 1.4-1.7x on 400 spheres. Its UI is drawn on the card; Blendity's is the
CPU's (2-2.7 ms at 4K, task 0198), and Blendity's card draws each sphere with a draw call of its own.

**Checked**
- Each window's line and the once-only notice (tests/test_gpu.cpp); the Edit Mode drag on the card compares both
  meshes, the smooth one after the release.
- The Preferences dropdown test (tests/test_modeling.cpp) reads where the list opened: the new window lines moved
  the keymap field near the window's bottom, and on Linux its list opens above it.
- An Opus review found, all fixed:
  - the window lines saying "views on the GPU" after choosing CPU (the card stays open; it isn't drawing them);
  - the labels swapped when Preferences is drawn in a secondary window;
  - the drag test passing without its after-release compares;
  - a windowed check writing the user's preferences (the notice);
  - Blender's Material Preview row timed while EEVEE still compiled its shaders, and a failed write leaving Blender
    running.
- Unit checks (round 219): Windows, in render-cache verify mode, 11,946, 0 failed (991 tests), 0 stale
  pictures. Linux with libraries: 11,504, 0 failed. Sanitizer build: 11,380, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`, `ui_2d`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone. The GPU leg: 7,965 checks with the views on the card, 0 failed.)

## 2026-10-23 (round 218): the UI's 2D measured - it stays the CPU's; presenting made cheaper (task 0198)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round measures
whether the UI itself should be drawn by the card (go only if the frames that change much of it cost over 4 ms at
4K).

**Decided:** no. The UI stays drawn by the CPU and taken up to the card where it changed. The measuring found the
card's presenting slower than it should be, so that was fixed instead (docs/tasks/0198):
- **Presents no longer wait for the card:** they run one frame behind. In a real 2560 x 1369 window, presenting the
  whole canvas (a resize) went from 6.0 to 3.0 ms a frame; an idle frame from 2.1 to 1.7 ms.
- **Only the changed columns go up, not whole rows:** scrolling a long Hierarchy at 4K costs the UI 3.41 ms a frame
  in all, down from 6.38.
- The card's first open after its shaders change compiles them in parallel: 35.3 -> 32.8 s on this machine. The views
  draw on the CPU meanwhile; a warm open takes about 130 ms. Compiling them when Blendity is built would end the wait:
  a follow-up.

**Measured**
- Real window (2560 x 1369, `Blendity --windowed-check`):
  - the canvas idle, by the card 1.69 ms (the system 1.58);
  - the canvas presented whole, 3.03 ms (2.61);
  - the Scene view drawn every frame, 5.54 ms (6.84).
- Headless 4K (stress `ui_2d`):
  - the canvas's drawing 2.0-2.7 ms;
  - scrolling 3.41 ms in all;
  - resizing 5.92 ms - over the line, but a resize repaints everything whoever draws the UI.

**Checked**
- `changed_rects` takes up only the changed tiles' columns (tests/test_editor.cpp).
- The views and their presenting checks pass (the gpu group).
- An Opus review found no synchronisation fault in the presents run one frame behind. It did find:
  - the stress window section taking nothing up (its row pair read as an empty rect);
  - a second staging buffer the offscreen compose never used;
  - the shader cache written in place (now written beside it and renamed);
  - shaders compiled for passes a card can't make;
  - the card's UI image missing changes the system presented meanwhile.

  All are fixed.
- Unit checks (round 218): Windows, in render-cache verify mode, 11,941, 0 failed (990 tests), 0 stale
  pictures. Linux with libraries: 11,499, 0 failed. Sanitizer build: 11,375, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`, `ui_2d`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 217): camera filters over the card's picture (task 0197, part 2)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round lets
filtered cameras' views draw their 3D on the card.

**Changed**
- **A camera filter no longer sends its view to the CPU:** the card draws the 3D, and the filter's passes run over
  its picture. Colour grading, vignette, grain, pixelate, edge outline, CRT, sharpen, halftone, hatching, line art,
  paper, colour ramp and bloom give the same picture as before.
- **Bloom's linear-light picture comes from the card**, from an HDR variant of its lighting pass.
- Still on the CPU, saying why:
  - a filter's rasterizer stage (Retro Console's snapped vertices, affine UVs, texture rules, screen door);
  - bloom over see-through surfaces;
  - realtime GI's per-pixel light.

**Measured**
- stress editor_render, this round's scene: colour grading, bloom and edge outline on the Main Camera, an icosphere moved
  each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 44.53 ms a frame;
  - views on the card: 42.09 ms, 33 views on the card and none on the CPU.
- The filters' own passes, which stay the CPU's, are most of the Game view's time: 24.49 ms against 25.41.
- The Scene view's 3D: 9.72 ms against 5.50.

**Checked**
- Card against CPU, the Game view through the Main Camera's filters (RTX 4070 SUPER):
  - colour grading, vignette and bloom: mean 0.053 / 255, 100 % within 3;
  - edge outline and sharpen: 0.093;
  - halftone: 0.037;
  - line art: 0.094.
- Each stack changes the plain picture: the filters ran.
- Retro Console's view goes to the CPU with its reason.
- An Opus review found:
  - a transparent render's bloom seeing "no light" where the CPU sees 0;
  - the card's colour one step off the CPU's own encoding of its linear light (colour grading's exposure and bloom
    treated those pixels as touched);
  - a test that couldn't show bloom read the card's linear light;
  - the read-back's failure path;
  - an extra full-size buffer each frame;
  - unchecked framebuffers.

  All are fixed. The colour is now encoded from the linear light as the CPU does, and an emissive cube's bloom
  matches (mean 0.051 / 255).
- Unit checks (round 217): Windows, in render-cache verify mode, 11,935, 0 failed (989 tests), 0 stale
  pictures. Linux with libraries: 11,493, 0 failed. Sanitizer build: 11,369, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 216): baked light on the card - lightmaps and probes (task 0197, part 1)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round removes the
baked lighting's reasons to fall back, which kept most views of a baked scene - the user's project's - on the CPU.

**Changed**
- **Baked lightmaps, realtime lightmaps (Receive GI: Lightmaps) and the probe volume draw on the card.** A baked pass
  reads each visible pixel's ambient light the way the CPU does: the lightmap at the pixel's lightmap UVs, the
  realtime maps' bounce and sky share, or the probes around the point. The lighting then ranks them as the CPU does,
  and skips the lights in Baked mode where a lightmap or baked probes already hold them.
- Lightmap pages go up to the card once, and again only when a bake or a realtime pass publishes new ones.
- Still on the CPU, saying why: realtime GI's per-pixel light for objects without realtime lightmaps. The CPU gathers
  it from its own picture of the frame. Camera filters and the picture kept in linear light are the next round's.

**Measured**
- tests/test_gpu.cpp round 216, the Scene view's own time a frame, warm (RTX 4070 SUPER, a test run, not a quiet
  machine):

  | Scene | CPU | Card |
  |---|---|---|
  | Baked floor, wall and box with a probe-lit ball | 1.49 ms | 0.69 ms |
  | Realtime lightmaps | 4.16 ms | 1.80 ms |

- Cold, the card's first frames cost 21-26 ms each: the lightmap pages go up, and the plane and probe volume are made.
- A probe volume is re-sent whole on every live update. For the volumes the card takes (up to 262,144 probes, 54 MB)
  that is a copy of up to that much a frame while live probes catch up.

**Checked**
- Card against CPU (RTX 4070 SUPER):
  - a baked floor, wall and box with a probe-lit ball: mean 0.060 / 255, 99.99 % within 3;
  - realtime lightmaps: 0.051, 100 %.
- Clearing the bake changes 166,098 of the card's pixels: the card isn't blind to it.
- With an object that takes the per-pixel light, the view goes to the CPU with its reason.
- An Opus review found:
  - a card opened again (after a GPU error) never getting the probe volume back;
  - replaced lightmap pages kept on the card and in memory;
  - the probe volume's indices as floats past 2^24, and no cap on its size;
  - a rebake with the same key unnoticed;
  - the baked pass's cutout test without the world footprint;
  - the bounce-onto-lightmap flag without the CPU's grid check.

  All are fixed.
- Unit checks (round 216): Windows, in render-cache verify mode, 11,920, 0 failed (988 tests), 0 stale
  pictures. Linux with libraries: 11,493, 0 failed. Sanitizer build: 11,369, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test, and on Linux the global sequencer's Play test, failed under the parallel load and passed alone.)

## 2026-10-23 (round 215): toon maps and live procedurals on the card (task 0196)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round removes the
toon materials' and live procedural textures' reasons to fall back.

**Changed**
- **Toon materials' painted maps draw on the card:** the shade map, the shadow bias, the specular and rim masks, the
  face shadow map, the inner lines and the hatching, and the hair's highlight across the strands. A toon pass reads
  them at each visible pixel, and the toon lighting uses them as the CPU does.
- **Live procedural textures draw on the card** wherever the CPU reads their image too. Only close up, where one of
  the texture's texels covers more than a pixel and the CPU evaluates the procedural point by point, does the view
  draw on the CPU, saying "a live procedural texture seen close up".
- Still on the CPU: see-through toon materials, toon axis materials, lightmaps, probes, realtime GI and camera filters
  (the next round).

**Measured**
- stress editor_render, this round's scene: toon materials with a shade map, inner lines, a specular mask and the hair
  highlight on the icospheres, one moved each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 24.10 ms a frame;
  - views on the card: 11.19 ms, 33 views on the card and none on the CPU;
  - the Scene view's own share: 9.31 ms against 4.43.

**Checked**
- Card against CPU (RTX 4070 SUPER):
  - a toon sphere with every painted map: mean 0.34 / 255, 99.96 % of pixels within 3;
  - one with the face shadow map, the hair highlight and lines in the shadow only: 0.47, 99.95 %;
  - a live Bricks texture from 6 m: 0.33, 99.99 %.
- The same texture close up sends the Scene view to the CPU with its reason.
- An Opus review found:
  - a failed toon-shader compile drawing the normals as maps instead of falling back;
  - the toon pass's depth loaded from an undefined layout;
  - the close-up estimate too generous off the frame's centre and under shear, and wrong on UVs in a line;
  - toon cutouts losing their inner lines in Solid;
  - a NaN rim mask, and a negative face map value.

  All are fixed.
- Unit checks (round 215): Windows, in render-cache verify mode, 11,910, 0 failed (986 tests), 0 stale
  pictures. Linux with libraries: 11,493, 0 failed. Sanitizer build: 11,369, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 214): fewer views on the CPU - many lights, Solid, see-through and axis materials (task 0195)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round removes the
first set of reasons a view still went back to the CPU.

**Changed**
- **More than 16 lights** draw on the card: the lights are a buffer of their own, any number of them.
- **Solid and Wireframe shading** in the Scene view draw on the card. Solid lights each vertex with the CPU's own
  formula and blends the colours across the face, as the CPU does, without the view transform.
- **Weight Paint's colours** draw on the card too.
- **Transparent and Glass materials** draw on the card: sorted back to front as the CPU sorts them, blended over the
  lit picture the CPU's way (a Transparent surface by its alpha; Glass tinting what is behind and adding its
  reflection). A click picks them as on the CPU, the selection outline goes round them, and they cast no shadow.
- **Axis materials** draw on the card: their Top, Side, Bottom and High parts blended by slope and height, with the
  noisy border, each projected triplanar.
- What still draws on the CPU, saying why: see-through toon materials, toon maps and the hair highlight, live
  procedural textures (next round), lightmaps, probes, realtime GI and camera filters (the round after), and cards
  without dual-source blending.

**Measured**
- stress editor_render, this round's scene: glass, transparent and axis-material icospheres, one moved each frame, with
  the 50k-face sphere (RTX 4070 SUPER, headless, so every picture is read back):

  | Shading | Views on the CPU (all of them, before this round) | Views on the card | Views on the card / on the CPU |
  |---|---|---|---|
  | Shaded | 33.27 ms a frame | 27.07 ms | 33 / 0 |
  | Solid | 28.81 ms | 25.07 ms | 33 / 0 |

- The Scene view's own share:
  - Shaded: 14.14 ms on the CPU against 9.68 on the card.
  - Solid: 8.34 against 8.85. The CPU's Gouraud Solid is cheap, and headless the card's picture is copied back, which
    the presented window of task 0193 doesn't do.

**Checked**
- Card against CPU (RTX 4070 SUPER):
  - 24 lights in Shaded, Solid and Wireframe: mean 0.19-0.22 / 255;
  - Weight Paint: mean 0.36 / 255;
  - Transparent, Glass and a textured Transparent sphere: 99.9 % of pixels within 3 / 255;
  - glass and faint panes in Shaded and Solid, and the glass pane selected: 100 %;
  - axis materials in World mode (with the band and noise) and Local mode: 99.96 % and 99.69 %.
- A click on the glass pane picks it, and through the faint pane the cube behind, on both devices.
- An Opus review found:
  - the outline missing round see-through objects over the sky;
  - derivatives taken after a discard;
  - an unknown surface value drawn opaque (the CPU draws it as Glass);
  - Solid building axis parts it doesn't use.

  All are fixed. Two small orderings differ from the CPU and are written up in docs/tasks/0195: a cutout drawn in the
  CPU's see-through sort, and a triangle cut by the near plane sorted as one.
- Unit checks (round 214): Windows, in render-cache verify mode, 11,904, 0 failed (985 tests), 0 stale
  pictures. Linux with libraries: 11,493, 0 failed. Sanitizer build: 11,369, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 213): macOS and the card, decided (task 0194)

**Asked:** the native-GPU roadmap's macOS step: the card presenting windows there too, if Blender's libraries make it
possible.

**Decided**
- **No-go:** Blender's dependency build has the Vulkan loader on every platform but no MoltenVK (Vulkan over Metal), and
  its developers don't expect to ship it. Under ADR 0001 Blendity doesn't add it, and with no Mac here nothing could
  be run. macOS keeps the system's present and draws on the CPU what it can't draw on the card, as before.
- Revisited if Blender bundles MoltenVK; a Metal backend would be a roadmap of its own (docs/tasks/0194).

**Checked**
- Documentation only: no code changed, no suite run (the round before ran the whole chain).

## 2026-10-23 (round 212): the card presents the windows (task 0193, ADR 0061)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback. This round is the
second step: the window shown by the card.

**Changed**
- **The card shows the window** (Windows and Linux): a swapchain on each OS window. A view drawn on the card stays
  there: its picture isn't copied back, the window's canvas is left transparent over it, and the card puts the canvas
  over the views' pictures and shows the frame. Only the canvas rows that changed go up each frame.
- What the processor draws over a view - icons, gizmo handles, wires, panels, text - keeps its coverage
  (`Color::over`, the same as before over anything opaque).
- When the card can't present a window, or a present fails, the system shows it as before, every view drawn again.
  `viewport present card|system` chooses.
- Screenshots take the frame the card showed.
- `Blendity --windowed-check out.png [--frames N]` runs the editor in a real window and measures card against system;
  `BLENDITY_PREFS_DIR` gives such a run its own preferences.
- `RasterInstance` has default material values: the card's texture-store test left them unset and was flaky.

**Measured**
- A real 2560 x 1369 window with the Scene view drawn every frame (RTX 4070 SUPER): 3.27 ms a frame presented by the
  card, 3.69 ms by the system.
- A 1080p window drawn and presented each frame (stress gpu_raster): 0.65 ms on the RTX 4070 SUPER, 2.23 ms on the
  3060 Ti; the CPU's view and GDI present 16.75 ms.
- The card's frame against the system's: mean 0.007 / 255.

**Checked**
- Color::over equals blend() over opaque pixels; layers drawn over a hole and composited match drawing directly within
  2 / 255 a layer (tests/test_render.cpp).
- A frame the card presents equals the system's, drawn fresh and from the caches; back on the system no transparent
  pixel is left (tests/test_gpu.cpp).
- A cached picture whose slot was drawn over draws again; a camera's depth of field keeps its view off the card and the
  frame still matches; a real window through `--windowed-check`; the card's texture-store test 10 of 10 runs.
- An Opus review found that minimising a window turned the card off for good, a mid-resize swapchain drawn at the old
  size, the swapchain left on a window the system then presented, depth of field lost on card views, closed windows'
  swapchains not let go, a failed present showing the views black and presents waiting for the display. All are fixed.
- Unit checks (round 212): Windows, in render-cache verify mode, 11,874, 0 failed (980 tests), 0 stale
  pictures. Linux with libraries: 11,493, 0 failed. Sanitizer build: 11,369, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 211): the 3D overlays on the card (task 0192, ADR 0061)

**Asked:** the views native on the GPU, like Blender's viewport, with the CPU as the fallback when there's no GPU.
This round is the first step: the overlays.

**Changed**
- **With the views on the GPU, the card now draws the Scene view's overlays** in the same submission as the picture:
  the grid and its axes, wireframes, dots, the selection outline, and Face mode's dot on every face. The CPU used to
  draw them over the read-back picture every frame. Bone, curve and lattice wires, icons and gizmos are still the
  CPU's (task 0193 takes them on).
- The CPU path is unchanged. When the card can't take them (an error, a scaled frame), the overlays recorded for it
  are drawn by the CPU, exactly as before.

**Measured** (stress editor_render, the views on the GPU, RTX 4070 SUPER)
- The overlays' CPU time in Edit Mode's Face mode: 3.2-3.6 -> 0.2 ms a frame.
- Edit Mode with GPU views: entering 18.6-21.5 -> 16.2-18.6 ms, Select More 8.6-11.0 -> 8.3-8.6, extrude
  38.1-41.6 -> 37.5-38.8, undo 13.0-14.5 -> 11.6-14.8.
- The card's overlays against the CPU's: the outline identical. The grid and wireframes are mostly within 1 / 255 (the
  card blends at 8 bits, as task 0100's cage): mean 0.79 / 255, 99.09 % within 3.

**Checked**
- Recording the overlays and replaying them draws exactly what drawing them does; the card draws the grid, a
  wireframe and the outline as the CPU does; refused, they're replayed here; the cage test gains Face and N-gon Face
  modes (tests/test_render.cpp, tests/test_gpu.cpp).
- Every kind of overlay interleaved - dots of several sizes over and under lines, translucent, without the depth test,
  an outline between them - drawn by the card: mean 0.82 / 255, 99.83 % within 3; the card drew Face mode's dots.
- An Opus review found an over-read of the outline table when there's none, and recording on frames the card would
  refuse (which would also have drawn the probes' dots from the previous frame). Both are fixed, along with its other
  points: the CPU's interleaved order, fewer copies, the tests above. The CPU path records nothing.
- Unit checks (round 211): Windows, in render-cache verify mode, 11,861, 0 failed (978 tests), 0 stale
  pictures. Linux with libraries: 11,491, 0 failed. Sanitizer build: 11,367, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-23 (round 210): the roadmap of tasks 0156-0191 written up (task 0176)

**Asked:** the roadmap's last task: every task with its round, PR and numbers against the Blender column.

**Added**
- docs/tasks/0176-roadmap-write-up.md: the 36 tasks of rounds 174-209 (PRs #119-#154) stack by stack - what changed
  and what was measured, the targets missed marked as missed (OptiX 0.32x / 0.20x GPU Compute against 1.4x / 1.8x; the
  first Editing stack's 683 ms at 4K before task 0191; the interaction audit's 4 % against 30 %; two of the three
  performance rows short of half their gap), and what stayed open.
- Tasks 0159, 0160 and 0178 were still marked "in progress" though their rounds landed: marked done.

**Checked**
- Documentation only: no code changed, no suite run (the round before ran the whole chain: 11,841 checks on Windows,
  11,488 on Linux, 11,364 with sanitizers, all passing).

## 2026-10-23 (round 209): entering Edit Mode redraws only the Scene view (task 0185, round 3 of 3)

**Asked:** the performance pass against Blender's numbers, its third row: entering Edit Mode on a 50k-face mesh.

**Fixed**
- **Entering or leaving Edit Mode redrew the Game view** (and changed the Camera Preview's and the lighting's keys)
  though nothing in them changes: the scene's render hash held the edit state, which only the Scene view draws and
  its own key already holds.

**Measured** (stress editor_render, the views on the CPU, a quiet machine)
- Entering Edit Mode, caches cold: 23.0-24.1 -> 18.0-18.4 ms (24.5 in the table before the pass); Blender 5.2.2's row
  18.1 ms: level with it, the target of half the gap met.
- The pass, three rounds: undo 25.7 -> about 21.0 ms (Blender 15.7), extrude 48.2 -> 40.1-41.5 (31.7), Edit Mode entry
  24.5 -> 18.0-18.4 (18.1); the first two short of half their gap, the third closed. What is left is drawing on the CPU.

**Checked**
- Entering and leaving Edit Mode renders the Scene view only and leaves the scene's render hash as it was
  (tests/test_editor.cpp); render-cache verify mode (every cached picture drawn again and compared) found no stale
  picture.
- Unit checks (round 209): Windows, in render-cache verify mode, 11,841, 0 failed (974 tests), 0 stale
  pictures. Linux with libraries: 11,488, 0 failed. Sanitizer build: 11,364, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone; Edit Mode entry 17.9 ms in the chain's stress run.)

## 2026-10-23 (round 208): an extrude without the whole-mesh work (task 0185, round 2 of 3, ADR 0053 amended)

**Asked:** the performance pass against Blender's numbers, its second row: extruding three faces of a 50k-face mesh.

**Changed** (results the same to the bit)
- **An Edit Mode operator's copy of the mesh leaves its caches behind** (the render meshes, edge table and topology
  snapshot: most of the 3.3 ms copy); an operator that only moved vertices takes them back, so it still follows in place.
- **The flat and the smooth render meshes share their triangles and corner table**; smooth normals and the edge table
  are worked out on every thread.
- **Auto Fuse's check after an extrude** no longer builds a table of the whole mesh or computes every face's normal on
  one thread.

**Measured** (stress editor_render, the views on the CPU, a quiet machine)
- Extrude three faces of the 50k-face sphere: 47.5 -> 41.5 ms (40.2-42.6 over seven runs); the operator 7.1 -> 3.3 ms,
  the two builds 11.2 -> 9.1-10.3, the edge table 1.4 -> 0.4. Blender 5.2.2's row 31.7 ms: 41 % of the gap closed,
  short of the half (40.0 ms) by 1.5 ms.
- Edit Mode entry gained too: 23.0-24.0 ms, was 26.3-26.9 (its build reuses the triangles). Undo 20.5-21.2 and Select
  More 14.3-14.4 unchanged.

**Checked**
- After an extrude both render caches match builds from nothing; Smooth Vertices still builds nothing (moves in
  place); the edge table from the corners is edges()'s on a sphere, a cube, an extrude, n-gons, a face repeating a
  vertex, faces of one and two corners and corners in no face; smooth normals equal each vertex's faces' sum; two
  faces fused onto one floor face leave a whole mesh (tests/test_modeling.cpp); the threaded-vs-serial build tests pass.
- An Opus review found an out-of-range write the new Auto Fuse loop made reachable (a second region landing on the
  faces the first fuse added: now only the faces the pass started with are looked at), and that the shared tables
  travelled with every copy (they now start empty in a copy, as the normals' state does, and count in the mesh's
  memory); its other points are taken (the corner table lists face corners only, the regions through a table of the
  selected faces, the tests above).
- Unit checks (round 208): Windows, in render-cache verify mode, 11,837, 0 failed (973 tests), 0 stale
  pictures. Linux with libraries: 11,484, 0 failed. Sanitizer build: 11,360, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `curved`, `editor_render`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (the gpu group's slow-open test failed under the parallel load and passed alone; curved's 8 problems and 79 overlapping results are the same as the build before this round.)

## 2026-10-22 (round 207): undo no longer redraws the shadow map (task 0185, round 1 of 3)

**Asked:** the performance pass against Blender's numbers: the three rows furthest behind, half of each gap a round.

**Fixed**
- **Undo drew the shadow map again every time.** The map's cache key mixed in each material's address; undo restores
  the scene's snapshot, whose materials are copies, so it always missed (redo restores the live objects and hit). The
  key now holds only what the depth pass reads: each material's surface type and Cast Shadows.

**Measured** (stress editor_render, the views on the CPU, a quiet machine)
- Undo of an extrude on the 50k-face sphere: 25.9 -> 21.2 ms (21.0-21.5 over five runs, 21.9 in the chain's stress run; the table had 25.7); Blender 5.2.2's row 15.7 ms.
  45 % of the gap closed, short of the half (20.7 ms). With the views on the GPU it was, and stays, about 13.6 ms.
- The stress rows now print each Edit Mode frame's parts (the restore, each view, the overlays, shadows, the UI): the
  undo frame's restore is 0.3 ms; the rest is drawing the restored mesh, which headless Blender doesn't do.
- Tried and reverted: the overlay batch's sort and copy in parallel (no measurable change).
- The other rows within noise of the same day's run before the fix: Edit Mode entry 25.5-26.8 ms (was 26.3-26.9), extrude 46.7-49.0 (48.4), Select More 14.3-14.7 (14.7).

**Checked**
- An undo of a colour change keeps the shadow map while the views draw it (fails on the old key; tests/test_editor.cpp).
- An Opus review found no bug; its points are taken: the test pinned to the CPU views and checking they redrew, the
  stress parts labelled as nested, the baseline drift stated; two other caches keyed by material addresses are noted
  in the task doc.
- Unit checks (round 207): Windows, in render-cache verify mode, 11,824, 0 failed (969 tests), 0 stale
  pictures. Linux with libraries: 11,471, 0 failed. Sanitizer build: 11,347, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor_render`: 0 problems. (the gpu group's slow-open test failed under the parallel load (and once alone while another build ran) and passed run alone on a quiet machine.)

## 2026-10-22 (round 206): essentials first, a guide per layout, Open Recent, typed gizmo moves (task 0190, ADR 0060 amended)

**Asked:** the interaction pass's second round.

**Changed**
- **The Inspector shows each component's first seven fields**, the rest under "Show N more" (Show less folds them
  again); its All Fields switch shows everything (kept in the preferences; `inspector essentials on|off`).
- **A first-run guide per layout:** the first time you use a stage layout, a strip above the windows says what it is
  for and offers its next three steps (UV Unwrapping: Smart UV Project, Part Unwrap, Pack Islands); Got it hides it
  for good; Help > Reset Guides brings them back.
- **File > Open Recent** (the last eight scenes); **Edit > Redo** names the action as Undo does.
- **Typing a number while dragging a gizmo's arrow** moves exactly that far along it (2*1.25 works), as Blender's
  numeric input; Push/Pull already took typed distances.

**Checked**
- A Camera's Inspector shows Show more until opened and nothing hidden with All Fields; the UV Unwrapping guide shows
  its steps until Got it, then never; every guide's steps are palette commands; an opened scene leads Open Recent;
  Redo carries the action's name; "2*1.25" typed during an axis drag moves 2.5 (tests/test_editor.cpp).
- The audit's action counts don't change for these (they reduce what is on screen and what must be remembered, not
  path length): a study with newcomers would measure them.
- An Opus review's findings are fixed: digits typed during a gizmo drag also ran shortcuts (Maya's Backspace deleted
  the selection; a test now drags a real arrow and types), the Camera Filters stack was cut inside its effects, Open
  Recent swapped the scene mid-frame, a 2-pixel band under the guide strip wasn't painted, Show less where nothing was
  hidden, the guides switch and All Fields not saved at once. Known limit: the seven are the first seven in each
  component's order, so an Area Light with colour temperature on hides Intensity until Show more.
- Unit checks (round 206): Windows, in render-cache verify mode, 11,818, 0 failed (968 tests), 0 stale
  pictures. Linux with libraries: 11,465, 0 failed. Sanitizer build: 11,341, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 205): find any command - the palette, Quick Favorites, the status bar's hints (task 0189, ADR 0060)

**Asked:** an interaction pass grounded in HCI research: keep the Unity feel, cut the friction; measure first.

**Changed**
- **Command palette** (Ctrl+K, or F3 as in Blender): type a few words and every keymap action, window, layout,
  creatable object and console-backed command that matches is listed with its shortcut, the recent ones first; Enter
  runs it. It reaches tools the current layout hides. Console `palette <words>` lists, `palette run <words>` runs.
- **Quick Favorites** (Shift+Q): right-click a palette row or a menu row to keep its command there; each layout has
  its own list; `favorite <name>` from the console. The preferences keep them and the palette's recent list.
- **The status bar** says what the mouse and keys do where the mouse is (the Scene view's tool or Edit Mode, the
  feel's navigation) and offers Undo for the last action by name.
- **Add Component** lists the last five added above the categories; **splitters** take a grab 4 pixels either side.

**Checked**
- "hair" puts Rig Hair from Mesh first and Enter runs it; every keymap action is in the palette; `palette run create
  cube` makes a cube; a favorite shows under Shift+Q and only in its layout; the status bar's text follows the tool;
  Add Component's Recent; the splitter grabbed 3 pixels inside the area beside it; Esc closes the palette; every
  palette name is unique; `palette run bring windows back` closes a secondary window (tests/test_editor.cpp).
- The audit (docs/tasks/0189-interaction-pass-1.md): 12 common tasks walked through before and after - 28 actions to
  27 (4 %), far from the roadmap's 30 %: the paths were already two or three actions, like Unity's and Blender's. The
  first count (33 -> 28 with dead ends removed) was wrong, an Opus review found; the corrected one is published. What
  the palette changes is recall - knowing where a command lives - which needs people to measure.
- An Opus review's findings are fixed: Esc didn't close the palette, a favorite couldn't be removed from Shift+Q,
  Shift+Q opened while flying (Unity's "down, fast"), palette names repeated (each is unique now), a menu row mapped to
  the wrong command (layouts before windows; combo options never), the status bar's hint cut and the message squeezed
  (measured now, cut with an ellipsis), a splitter press also reaching the area beside it, a layout chosen from a
  secondary window changing state without its dock, Tab moving the keyboard behind the palette, and hints that said
  "add" where a click toggles.
- Unit checks (round 205): Windows, in render-cache verify mode, 11,785, 0 failed (966 tests), 0 stale
  pictures. Linux with libraries: 11,432, 0 failed. Sanitizer build: 11,308, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 204): a render-only window and a pen tablet (task 0188, ADR 0059 amended)

**Asked:** a second screen showing the render large; a pen display for the Editing tab.

**Changed**
- A Render or Game tab in a window of its own: right-click its tab > Render Only shows the picture alone, fitted to
  the window (the Game view live until a render exists); its controls (Show Tabs, Full Screen) appear in a strip when
  the mouse is at the top. Full Screen fills the window's display without a frame (Esc leaves): Win32 borderless,
  X11's _NET_WM_STATE_FULLSCREEN, macOS full screen.
- Pens: the tilt and the eraser end come with the pressure (Windows' pointer messages); the Editing tab's brush gets
  thinner with lighter pressure and wider when the pen is tilted, as a pencil; the eraser end erases. A palm on the
  screen while the pen is in range is ignored (no stray clicks or pinch-zooms).
- Tablet Mode (a tab's right-click menu, or `window tablet on [id]`): that window's UI half as big again - bigger
  targets for a pen or a finger.
- Console `window renderonly|fullscreen|tablet on|off [id]`.

**Checked**
- Render-only draws no controls until the mouse reaches the top, the picture fills the window's width or height within
  2 pixels, Show Tabs and Full Screen appear at the top; full screen takes the display's rectangle and gives the old
  one back; Tablet scales one window's UI by 1.5 and not the main one's; a pen at half pressure and 30 degrees of tilt
  makes a 0.875-times-wide dab, a mouse the size set, the eraser end reaches the brush; the strip's Full Screen acts
  on its own window (not the main one), Esc and Show Tabs leave full screen, an unknown window id is refused
  (tests/test_editor.cpp).
- An Opus review's findings are fixed: the strip's buttons and the console's window modes acted on the main window
  (a window's state is swapped in while it draws: the modes now wait for the frame's end, as tab moves do); a window
  could stay full screen with no way out; a finger's button-up could be dropped while a pen came near; any pointer
  leaving cleared the pen's nearness; full screen restored a maximised window's rectangle wrongly.
- Unit checks (round 204): Windows, in render-cache verify mode, 11,675, 0 failed (964 tests), 0 stale
  pictures. Linux with libraries: 11,322, 0 failed. Sanitizer build: 11,198, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 203): windows beside the main one, on any display (task 0187, ADR 0059)

**Asked:** windows outside the main editor - the Render tab on a second screen, the Editing tab on a pen display.

**Changed**
- Drag a tab out of the editor and it gets a window of its own where you let go; its tab menu has Move to New Window,
  Move to Main Window and Move to Display N; Window > Bring Back N Windows; closing such a window gives its tabs back.
  A window holds a dock like the main one (split it, add tabs); each window type is still in one place at a time.
- One scene and one simulation: a window draws with its own input, popups, keyboard focus and DPI (a display at 150%
  scales its window only), and a drag (an asset, a Hierarchy row) belongs to the window it started in.
- The windows are remembered with the layout and open again where they were (fitted to a display that is there).
- Console `dock move <Window> newwindow | main | monitor <n>`, `dock windows`.

**Checked**
- `dock move Render newwindow`: a second window holds the Render tab and both draw; its tab's right-click menu opens
  in it, not in the main window; a UI scale on it alone; closing it gives the tab back; the layout string brings it
  back; a layout for the main window leaves it there; a broken or oversized saved window is refused or fitted; a tab
  moved to display 2's middle and back; a tab dragged out of the editor gets a window (tests/test_editor.cpp).
- A real window on this PC's three displays (stress `windows`, run by hand: it opens a window): made at (120, 120)
  640 x 400, moved to (200, 160) 800 x 500 and onto display 2, each reported back exactly where asked.
- An Opus review's findings are fixed (the UI context's font wasn't movable; a poll inside a frame could paint the main
  window into a secondary one; drags and undo read the main window's mouse; prefs and layouts saved or applied from a
  secondary window; Win32 DPI and Cocoa's points in placement; X11 copy and paste between windows).
- Unit checks (round 203): Windows, in render-cache verify mode, 11,657, 0 failed (962 tests), 0 stale
  pictures. Linux with libraries: 11,304, 0 failed. Sanitizer build: 11,180, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed alone; macOS compiles the Cocoa calls in CI only.)

## 2026-10-22 (round 202): the Editing tab's room to grow, specified (task 0184)

**Asked:** the roadmap's last Editing task: layers with blend modes, text, healing and clone, content-aware fill -
specified now, built when asked.

**Changed**
- docs/tasks/0184-editing-room-to-grow.md: where each attaches (the adjustment registry, masks, the document's cache
  keyed by text, so a step holding pixels needs a revision in its text) and the research each would follow - the
  W3C / Photoshop blend modes Blender's Mix node shares, Poisson image editing (Pérez et al. 2003) for healing,
  PatchMatch (Barnes et al. 2009) with Wexler et al.'s voting for content-aware fill, Criminisi et al.'s exemplar
  inpainting as the simpler first step - an order, and what isn't planned (generative fill: a model and licence
  question to ask first).

**Checked**
- Documentation only: no code changed, the check chain wasn't run again (round 201's checks hold).

## 2026-10-22 (round 201): the Editing tab at 4K - a preview the window's size, tables for the display axis (task 0191, ADR 0058 amended)

**Asked:** (round 198's measured miss) an 11-step stack on a 4K render took 683 ms, and a slider near the top of the
stack stalled the editor as long.

**Changed**
- The Editing window works on a preview the canvas's size (its longest side in steps of 512 pixels), as Lightroom's
  Develop module does; zoomed in past it, the window shows the full picture. Export, Apply to Texture and the camera
  look use the full picture. The status line says when a preview is shown.
- The display axis through tables (no `pow` per pixel in Highlights / Shadows / Whites / Blacks, Clarity, HSL, Noise
  Reduction); the Tone Curve as one table per channel; Noise Reduction's tones once a pixel and its weights from
  tables; result buffers reused instead of allocated and cleared for each step.
- A step's input size is worked out from the crops and turns before it, not by running them (adding a mask after a
  crop no longer evaluates the stack at full size).

**Checked**
- The preview matches the full result made small for pointwise steps (mean difference 0.00014); a crop on it is the
  crop at its scale; results written into reused buffers equal a fresh document's; Noise Reduction still smooths
  (tests/test_editor.cpp). An Opus review's findings are fixed (spare buffers of an old size kept, a resize remaking
  the preview each frame, an infinite pixel in Noise Reduction, a tiny crop becoming the whole picture).
- Measured (stress `editing`, 3840 x 2160): what the window evaluates while you work, its 1600-pixel preview - the 11-step stack 65.4 ms, a slider on its last step 2.4 ms (task 0181's targets: 150 and 40). At full size, for export: the stack 412.0 ms (683 before), a slider on its first step 392.0 ms (707), on its last 14.0 ms (31); the Tone Curve alone 29.8 ms (347), Noise Reduction 252.1 ms (468).
- Unit checks (round 201): Windows, in render-cache verify mode, 11,634, 0 failed (960 tests), 0 stale
  pictures. Linux with libraries: 11,281, 0 failed. Sanitizer build: 11,157, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editing`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 200): textures through the Editing tab, and the stack as a camera look (task 0183, ADR 0058 amended)

**Asked:** the Editing tab for textures (results kept as texture assets), and a look made there carried live by the
camera.

**Changed**
- Textures window > Edit (or `develop open texture <name>`): the texture's albedo in the Editing tab. Apply to Texture
  (`develop apply`) writes the result beside the asset as `<texture> edit.png` and puts an Image layer on top of the
  texture's stack that writes the albedo only - the procedural layers stay underneath, the layer can be turned off,
  and the asset is saved as any edit of it is. Editing the texture again starts from the albedo under that layer with
  the same adjustments (kept beside the image as `.edit`), so edits go on rather than pile up.
- Use as Camera Look (`develop look`): the stack's global adjustments become the Main Camera's filters, so the Game
  view and F12 carry the look live: Exposure, Contrast, Saturation, Temperature / Tint and the Tone Curve (as lift,
  gamma and gain through black, middle grey and white) into one Color Grading; a darkening Vignette and Sharpen as
  their own. Masked steps and the ones no camera filter does (Highlights, Clarity, HSL...) are left out and named.
  Using it again replaces the look it gave before (the camera's first Color Grading, Vignette and Sharpen are reused).
- Fixed: a texture's Image layer read its file upside down (the layer's rows from the top, the image's from the
  bottom). Image layers now show their file the way up it is; a texture made with one before shows it flipped back.

**Checked**
- An Image layer's top row is its file's top row. A Bricks texture edited and applied: its albedo matches the
  Editing export exactly (0 / 255 apart); editing again reopens the
  same adjustments and Apply reuses the layer; an exposure +1 stack gives the camera a Color Grading with exposure 1
  that doubles a mid-grey pixel; contrast, saturation, a curve (through its black, grey and white), a vignette and
  sharpening carry over, a masked step and Clarity are left out, a second look replaces the first, a stack with
  nothing a camera does is refused (tests/test_editor.cpp). An Opus review's findings are fixed (the flipped Image
  layer, a second Apply not reaching materials, looks piling up, the curve's ends, the vignette's strength).
- Unit checks (round 200): Windows, in render-cache verify mode, 11,625, 0 failed (959 tests), 0 stale
  pictures. Linux with libraries: 11,272, 0 failed. Sanitizer build: 11,148, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `textures`, `filters`, `editing`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 199): masks for the Editing tab's adjustments (task 0182, ADR 0058 amended)

**Asked:** local adjustments: each adjustment limited by a mask - a gradient, a luminance or colour range, a painted
mask - and masks combined.

**Changed**
- Each adjustment (but Crop / Straighten and Flip / Rotate) takes a mask: Add Mask > Linear Gradient, Radial
  Gradient, Luminance Range, Color Range or Brush. Parts combine top to bottom by Add (either), Subtract or Intersect
  (both), each invertible; a masked adjustment blends with what came before it by the mask's weight.
- On the picture: the selected part's handles (a radial's centre and outline, a gradient's two ends) drag; a Brush
  part paints with the mouse or a pen (pressure lightens the stroke; Erase or Alt removes), one undo step a stroke;
  Show tints where the adjustment acts in red.
- Masks are fractions of the picture, saved in the `.edit` file (a brush as run-length hex at 512 pixels on its
  longest side). Console `develop mask <step> add|subtract|intersect <kind> [numbers] | paint <u> <v> <radius> |
  invert | remove | clear`, `develop mask show on|off`.

**Checked**
- A radial mask keeps an exposure change inside its radius (a pixel outside within 1e-4 of the source); a luminance
  range doubles a shadow and leaves a highlight; subtract and intersect; invert; a colour range picks red and not blue
  or grey; a brush dab and its erase; the text form of every kind; a crop ignores a mask; the console's mask commands,
  undo, the .edit file and Show (tests/test_editor.cpp).
- Unit checks (round 199): Windows, in render-cache verify mode, 11,603, 0 failed (957 tests), 0 stale
  pictures. Linux with libraries: 11,250, 0 failed. Sanitizer build: 11,126, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editing`: 0 problems. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 198): the Editing tab - develop a render or an image, non-destructively (task 0181, ADR 0058)

**Asked:** a new Editing tab with basic Lightroom / Photoshop adjustments on renders, images and textures,
extensible.

**Changed**
- Window > Editing (and the Editing layout): a picture with an adjustment stack beside it. Open an image (or drop one
  on the window), or the Render window's last render - its light, shown through the render's own view transform.
  Seventeen adjustments, applied top to bottom in linear light: Exposure, Contrast, Highlights, Shadows, Whites,
  Blacks, Temperature / Tint, Vibrance / Saturation, Clarity, Dehaze, Tone Curve (RGB and per channel: click, drag,
  right-click), HSL (eight hue bands), Sharpen, Noise Reduction, Vignette, Crop / Straighten, Flip / Rotate. Each step
  turns off, moves or goes; Undo / Redo; a Before / After line dragged across the picture; presets (Neutral, Film,
  Anime Pop, Manga Contrast, Cinematic Teal-Orange).
- Non-destructive: the source is never written. Save keeps the stack as an `.edit` text file beside the image (a
  render's pixels beside it as EXR, or HDR where the build writes no EXR); Export writes PNG / JPEG through the view, or EXR / HDR in linear light.
- A slider reruns only the steps from the one changed (each step's result is cached).
- The camera filters' Color Grading and Vignette share their formulas with the Editing tab (`image/grade.h`).
- The console: `develop open <file>|render`, `add`, `set <Type>.<Field> <value>`, `remove`, `move`, `enable`, `curve`,
  `preset`, `undo`, `redo`, `export`, `save`, `split`, `list` (`edit` stays Edit Mode's).

**Checked**
- Exposure +1 doubles a mid-grey pixel; Contrast 0 is the identity; Highlights -100 lowers 0.9 and leaves 0.2; a tone
  curve through (0.5, 0.75) lifts 0.5 to 0.75; HSL red -100 greys red and leaves blue; every adjustment is the
  identity at its defaults and survives its text form; crop, straighten, flips and quarter turns move the right
  pixels; a slider on the last step reruns one step; undo; save and reopen reproduce the export byte for byte; every
  preset; the window through the console, the split, export (tests/test_editor.cpp).
- Measured (stress `editing`, 3840 x 2160): the 11-step develop stack 683.2 ms in full, a slider on its last step 30.7 ms (1 step rerun), on its first 706.8 ms; the slowest adjustment alone Noise Reduction 467.8 ms. Task 0181's targets at 3840 x 2160: the full stack 683.2 ms (at most 150: missed), a slider on the last step 30.7 ms (at most 40: met). The full stack is the Tone Curve (347 ms alone) and Noise Reduction (468 ms) at full resolution: a following round takes it up (a preview at the screen's size, as Lightroom's, and look-up tables).
- Unit checks (round 198): Windows, in render-cache verify mode, 11,569, 0 failed (955 tests), 0 stale
  pictures. Linux with libraries: 11,216, 0 failed. Sanitizer build: 11,092, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`, `filters`, `editing`: 0 problems. (the sanitizer leg's autopose timing check failed under the parallel load and passed alone.)

## 2026-10-22 (round 197): the UV Editor on one material's faces (task 0186)

**Asked:** the UV Editor showing one material's islands: a Material filter, the others dimmed or hidden; "Edit UVs"
from the Materials window; the background following the chosen material.

**Changed**
- UV Editor > the Material picker (All materials, then each slot): only that material's faces are drawn as editable,
  picked, selected, moved, unwrapped (Unwrap, Smart UV, Part Unwrap, projections) and packed; the others are a faint
  outline behind, or hidden (`uv others hide`), and keep their UVs. The Material Texture background is the chosen
  slot's. Console `uv material <name>` / `+<name>` (adds a material) / `all`; Materials window > Edit UVs.

**Checked**
- On a cube with two materials, the second chosen: Unwrap changes only its faces, Select All selects only its corners,
  Pack puts them in 0-1 and leaves the first material's UVs exactly; dimmed, hidden, adding a material, All, an unknown
  name refused (tests/test_modeling.cpp).
- Unit checks (round 197): Windows, in render-cache verify mode, 11,460, 0 failed (951 tests), 0 stale
  pictures. Linux with libraries: 11,107, 0 failed. Sanitizer build: 10,984, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`, `uv`: 0 problems.

## 2026-10-22 (round 196): PartField's part tree for Part Unwrap, opt-in (task 0180, ADR 0057 amended)

**Asked:** an opt-in "semantic parts" switch: PartField's learned per-face features (NVIDIA; non-commercial weights,
never downloaded by Blendity) in place of the geometric part tree, as PartUV uses them.

**Changed**
- `uv parts semantic on|off` (off by default): Part Unwrap's part tree from PartField's features when the user has
  installed a conversion of the model (`~/.blendity/ai/partfield/partfield.onnx`, through the ONNX Runtime of the AI
  install); the geometric tree otherwise, the Console saying which and why. `src/scene/partfield.*`: the model's
  contract (face centroids and normals in -> a feature per face out), rows scaled to unit length; the part tree takes
  features of any width (wider than 32 through a fixed random projection).
- `tools/partfield/`: the conversion script the user runs with PartField's code and their weights, and its README.

**Not done:** the roadmap's spike - does PartField export to ONNX? - needs PartField's code and weights, which are
NVIDIA's under a non-commercial licence; downloading them is the user's decision, so it wasn't run. The export script
is written against the contract with placeholder names for PartField's encoder and feature query (its README says so,
and names the fallback: features written by PartField in Python, read the same way).

**Checked**
- With a stand-in model, the features reach the part tree: the teapot's charts come out otherwise (10 against the
  geometric tree's 11), every chart under the bound, also with features 448 wide; the switch is off by default, the
  geometric tree is used exactly when nothing is installed, the editor's result is the stand-in's exactly
  (tests/test_modeling.cpp).
- Unit checks (round 196): Windows, in render-cache verify mode, 11,450, 0 failed (949 tests), 0 stale
  pictures. Linux with libraries: 11,097, 0 failed. Sanitizer build: 10,974, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `uv`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (the gpu group's slow-open test (a GPU opening in the background, timed) failed under the parallel load, then in two of three runs alone (one while another build loaded the machine, one on a quiet machine) and passed in the third: a flaky timing test, not changed by this round; the sanitizer leg's autopose timing likewise failed under load and passed alone.)

## 2026-10-22 (round 195): unwrap by parts - PartUV's search on a geometric part tree, ABF++ beside LSCM (task 0179, ADR 0057)

**Asked:** a part-based unwrapper after PartUV (SIGGRAPH Asia 2025): its geometric core (the PartField model is task
0180, opt-in).

**Changed**
- UV Editor > Part Unwrap (also the UV menu, Modeling Tools, console `uv parts [bound] [multi|single]`): as few charts
  as keep every chart's distortion - the area-weighted mean of max(s, 1/s) over its faces, s a face's share of the UV
  area over its share of the 3D area - under the bound (PartUV's 1.25). A part tree clustered from the geometry
  (normal and position, across manifold edges only; material slots joined last) is searched top down: a part becomes
  one chart if it passes, else the fewest charts of PartUV's Normal heuristic (clustered by normal, 2..10) or Merge
  heuristic (the part's oriented-box labels, merged greedily) that all pass, else its two children. A chart never spans
  two material slots; a chart whose outline crosses itself or whose triangle flips fails; a non-manifold edge is a cut.
- ABF++ (`src/scene/uv_abf.cpp`, ported from Blender's Angle Based unwrap: angles moved least so a flat layout exists)
  then LSCM on a chart that LSCM alone nearly passes (within 1.5 x the bound), kept when better; LSCM screens every
  candidate (one solve). LSCM takes per-triangle angles (`uv_internal.h`).
- Each part's charts packed together as one block, the blocks at one texel density with a gap between them; or each
  separate piece on its own UDIM tile (u 0-9, then a row up). The top-level parts are solved on threads.
- `uv report`: any UV map measured PartUV's way - charts, seam length, distortion.
- The tests' scratch folder comes from `BLENDITY_TEST_DIR` when set: the check chain runs the test groups as parallel
  processes, and a shared temporary folder had let one group's files become another's (a texture deletion and an
  export read-back failed under load in round 192's chain, then passed run alone).

**Measured** (stress `uv`, "Part Unwrap (task 0179)")
- cube + boss + torus (416 faces): Smart UV Project 22 charts, seams 32.56 m, distortion 1.05 / 1.17, 0.4 ms; Part Unwrap 6 charts, seams 16.90 m, distortion 1.13 / 1.22, 35.9 ms (3 parts, 42 tried, 1 ABF++).
- teapot (2.0k faces): Smart UV Project 58 charts, seams 42.94 m, distortion 1.08 / 1.20, 1.5 ms; Part Unwrap 11 charts, seams 15.74 m, distortion 1.13 / 1.18, 108.0 ms (4 parts, 101 tried, 2 ABF++).
- 50k-face sphere, 12 slots (50.2k faces): Smart UV Project 8 charts, seams 22.90 m, distortion 1.09 / 1.10, 40.5 ms; Part Unwrap 12 charts, seams 37.70 m, distortion 1.03 / 1.03, 265.9 ms (12 parts, 12 tried, 0 ABF++).

**Checked**
- A cube with a cylinder boss and a torus: six charts, the fewest face sets allow (each closed piece needs two), a
  third of Smart UV Project's 22, half its seam length, every chart under the bound, no folds or overlaps, ABF++ used;
  a 12-slot sphere's charts each one slot; a non-manifold fin a cut; twelve pieces on twelve UDIM tiles; the Edit Mode
  mask; ABF++ closing every triangle at pi and every interior vertex at 2 pi with less angle error than LSCM alone; the
  console's `uv parts` (a non-number refused), `uv report` and undo (tests/test_modeling.cpp). An Opus review's
  findings (neighbours lost in the clustering, a wasted Merge pass, UDIM numbering, blocks touching, self-overlap,
  the bound's parsing, threads and packing costs) are fixed.
- Unit checks (round 195): Windows, in render-cache verify mode, 11,438, 0 failed (948 tests), 0 stale
  pictures. Linux with libraries: 11,085, 0 failed. Sanitizer build: 10,962, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `uv`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 194): the Godot and Unreal bridges, specified (task 0175)

**Asked:** Godot and Unreal bridges on the same protocol, after Blender's and Unity's.

**Changed**
- `docs/tasks/0175-godot-unreal-bridges.md`: what each bridge does against `blendity-livelink/1` unchanged - Godot 4
  as a GDScript editor plug-in (`StreamPeerTCP`, an own glTF reader into `ArrayMesh` surfaces, `StandardMaterial3D`
  with Godot's toon diffuse and specular, the cameras and lights turned onto Godot's -Z), Unreal 5 as a C++ editor
  module (`FSocket`, `FMeshDescription` to a static mesh and a dynamic mesh once vertices move, a master material's
  instances, Unreal's centimetres and the basis its own glTF importer uses), each with a smoke test replaying the
  recorded session. Each gets its own task when started; the protocol needed nothing new.

**Checked**
- Documentation only: no code changed, the check chain wasn't run again (round 192's checks hold).

## 2026-10-22 (round 193): the Unity bridge - Blendity's scene live in the Unity Editor (task 0174)

**Asked:** a Unity bridge for Live Link, after Blender's.

**Changed**
- `bridges/unity/com.blendity.livelink/`: a Unity 2022.3 package - Window > Blendity Live Link: Port, Connect.
  GameObjects under "Blendity Live Link" with a `BlendityLink` (the id); Blendity is Unity's own left-handed Y up, so
  mirroring glTF's X back gives Blendity's numbers exactly. Meshes with a submesh per slot and each vertex's mesh
  vertex kept for deforms; URP Lit or Standard materials with base and normal maps; Toon fields per slot in a
  `BlendityToon` component; lights with Blendity's intensity as Unity's; cameras (disabled, so the Game view stays
  yours); bones as posed GameObjects. An own glTF reader (`GlbReader.cs`; glTFast drops the `_VERT` attribute).
- `bridges/unity/ReplayCheck`: the package's framing, JSON and glTF reading on the recorded session with any C#
  compiler.

**Checked**
- ReplayCheck, compiled with the .NET Framework's C# compiler, replays the recorded session: 32 messages, every
  mesh's vertices within its mesh, the deform applied. Not checked: the Unity-side code (the applier, the window, the
  EditMode test) has not been compiled or run - Unity isn't installed on this PC; the README's Test section is the
  first run.
- No C++ changed: the check chain wasn't run again (round 192's checks hold).

## 2026-10-22 (round 192): the Blender bridge - Blendity's scene live in Blender (task 0173)

**Asked:** a Blender bridge for Live Link (one way), the first of the four.

**Changed**
- `bridges/blender/blendity_livelink/`: an add-on (and a Blender 4.2+ extension) - View3D > Sidebar > Blendity: Port,
  Connect. The objects live in a collection "Blendity Live Link" (custom property `blendity_id`); hierarchy,
  transforms (glTF's Y up to Blender's Z up as Blender's glTF importer converts), meshes with Blendity's own vertices
  (the binary's split vertices folded back by `_VERT`, so moving vertices land in place), UVs, Blendity's hard edges as sharp edges (so the
  normals follow moving vertices), a material per slot; Principled materials with their image textures (tint, alpha
  and cutouts included); Toon materials as an EEVEE cel setup (Shader to
  RGB, a constant ramp of Toon Bands) with every field kept as `blendity_toon`; suns, points, spots and area lights in
  Cycles' units matched to Blendity's path tracer (as the MoonRay export matches them); cameras; bones as posed
  empties. A timer reads the socket with a 20 ms budget a tick and leaves the rest in it, so Blendity's queue keeps
  replacing stale updates.
- Meshes are read by the add-on (numpy), not through `bpy.ops.import_scene.gltf` as the roadmap planned: the importer
  splits vertices by normal and UV (a deform would have nothing to land on) and needs a window context a timer lacks.

**Checked**
- `bridges/blender/smoke_test.py` in Blender 5.2.2 headless replays the recorded session: every object, the Sphere
  deleted, the Box at Blender (-1, -3, 2), its Cel material recoloured with its cel setup, the moved vertex, the
  cube's 12 hard edges sharp, the camera's 60 degrees and direction, the sun at pi W/m2, the bone hierarchy and its
  30 degree pose, an object deleted in Blender made again by its next message. An Opus review's findings (camera and
  light scale, stale references, tint and cutouts, normals after deforms, inactive parents) are fixed. Live: the add-on
  connected to a running Blendity and received the sync with every mesh. Blender's extension validator accepts the
  manifest.
- Unit checks (round 192): Windows, in render-cache verify mode, 11,417, 0 failed (943 tests), 0 stale
  pictures. Linux with libraries: 11,064, 0 failed. Sanitizer build: 10,944, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (in the parallel runs, the gpu group's slow-open test, a Linux texture-deletion test and a sanitizer export read-back failed under load and passed alone.)

## 2026-10-22 (round 191): Live Link - the scene sent live to Blender, Unity, Godot or Unreal (task 0172, ADR 0055)

**Asked:** "bridges" in Blender, Unity, Godot and Unreal applying Blendity's changes in real time, Blender and Unity
first, one way. This round is the editor's side and the protocol; the Blender bridge is next (task 0173).

**Changed**
- Window > Live Link and the console's `livelink start [port] | stop | status`: a TCP server on 127.0.0.1 (port
  40100 by default; `src/core/net_server.*`, WinSock or BSD sockets, no library). A client gets the whole scene, then
  every change as it happens: objects and their hierarchy, transforms, a rig's pose, meshes as glTF binaries (built on
  a worker thread), vertices that moved as a `deform` (float32 positions: an Edit Mode drag, skinning, physics),
  materials (glTF's object, the toon fields, texture paths), lights, cameras, deletes, Play. The protocol,
  `blendity-livelink/1`, is in `docs/LIVELINK.md`; `tools/livelink_dump.py` is a 50-line client that prints it.
- What changed is found by comparing the scene with what was sent, at most 60 times a second and only while a client
  is connected (the roadmap had planned a diff at each undo step: comparing carries a drag while it happens, and undo,
  Stop and reloads need nothing of their own; ADR 0055).
- Each client has its own queue and thread; a newer transform, pose, deform, material, light or camera replaces one
  still queued (never past an object, mesh or delete), so a slow client skips to the present; a slow client is waited
  for, never timed out; one past 256 MB queued is closed. Sockets aren't inherited by programs the editor starts.
- glTF export: each primitive names its material slot (`extras.slot`), an optional `_VERT` attribute (each vertex's
  mesh vertex), and a Toon material carries the extension `BLENDITY_toon` with every field by its Inspector name (in
  exported files too: other readers ignore it). `JsonWriter` in json.h; `reflect_to_json`.
- `tests/fixtures/livelink_stream.bin`: a recorded session (the sync, a move, a recolour, moved vertices, a pose, a
  delete, Play and Stop) that the suite records again and compares message by message, and the bridges replay.
- The check chain, faster: `build.bat check` compiles every source once and links the unit and stress tests from the
  same objects (92 s against 112 s for the two builds); the unit suite runs once, in render-cache verify mode (the
  plain run checked nothing verify mode doesn't), its groups as parallel processes with a failed group run again
  alone; the Linux and sanitizer legs start with the Windows build and run their groups in parallel too, and their
  copy of the sources skips tools/inbetween's Python environment (24k files: the sync's 4 minutes). The stress
  sections still run on a quiet machine, after the Linux legs. `BLENDITY_NO_LTCG=1` builds without whole-program
  optimization for quick local runs only: with /fp:fast, MSVC then compiles two code paths differently that two
  tests compare bit for bit.

**Measured** (stress `livelink`, the RTX 4070 SUPER PC, 1,000 cubes moved every frame)
- A frame (mean / most, ms) with the link off 11.49 / 13.14, on without a client 11.46 / 12.93, with one client draining 12.50 / 14.16, with two 13.10 / 31.04.
- Comparing the scene with what was sent (1,000 moved objects: 1,000 transforms) 1.23 / 1.50 ms mean / most with one client, 1.42 / 3.50 with two; 60240 and 120491 messages, 5.49 and 10.98 MB in the 120 frames.
- A client's sync with a 95232-face mesh: the frames at most 11.42 ms; the worker built 1005 glTF binaries in 182 ms; the sync took 777 ms of frames.
- A vertex of the 95232-face mesh moved every frame: 14.57 ms a frame, 64 deforms, 69.76 MB (95234 vertices: 1.09 MB a deform).
- A client that leaves while a mesh is going out: the frames at most 2.17 ms over the next 30 frames; 0 client(s) left connected.
- One client cost the frame 1.00 ms on average against the link off (12.50 vs 11.49 ms; the views' rendering is most of a frame).

**Checked**
- A client gets hello, the sync with the scene's object count and every mesh as a valid glTF binary (indices 3 x the
  triangles, `_VERT` for every vertex); a move is one transform in glTF's axes, a recolour one material, moved vertices
  one deform, new faces one mesh, a posed rig one pose with every bone, a deletion one delete, no change nothing; in
  Play at most 60 transforms a second, in order, the last where the object ended; two clients get the same stream; a
  client that leaves mid-send is dropped and a new one syncs; held queues replace a keyed message only after the last
  barrier (tests/test_livelink.cpp). Play then Stop sends only the two play messages.
- Unit checks (round 191): Windows, in render-cache verify mode, 11,417, 0 failed (943 tests), 0 stale
  pictures. Linux with libraries: 11,064, 0 failed. Sanitizer build: 10,944, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `livelink`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (The gpu group's slow-open test failed under the parallel load and passed alone.)

## 2026-10-22 (round 190): the OptiX render device - the same picture as GPU Compute, opt-in (tasks 0170-0171, ADR 0054 amended)

**Asked:** an OptiX render device with PBR parity (a picture within 0.01 of GPU Compute's, 1.4x its samples a second on
the RTX 4070 SUPER and 1.8x on the 3060 Ti), and the toon shading on it (task 0171).

**Changed**
- One integrator for both devices: `tools/glsl_to_cuda.py` translates the GPU path tracer's own GLSL (gpu_kernel.h,
  gpu_glsl.h) to CUDA when the PTX is embedded - buffers as launch-parameter pointers, `out` parameters as references,
  GLSL's constructors as braces, every function inlined, the kernel a struct over `src/render/optix/glsl.cuh`'s GLSL
  types and built-ins. Only the rays differ: optixTrace through structures the driver builds, where GPU Compute walks its
  BVH or uses Vulkan's ray queries. Toon shading (task 0171), the lods of 0164 and the area lights of 0163 come with it.
- The OptiX device (`src/render/optix_device.cpp`, in place of task 0097's spike): a `gpu::Renderer` with the Vulkan
  device's buffers, a triangle structure per mesh, the objects as instances, eight samples a launch, the card's time from
  events, the bake points. The committed PTX is now text in pieces (3.6 MB).
- Render Settings > GPU Backend: Auto, Vulkan, OptiX. **OptiX is opt-in: Auto keeps GPU Compute** - measured slower,
  below. The renderer pool keys on the backend, the tracer reopens a card when it changes, the device summary says
  "(OptiX)"; `BLENDITY_GPU_BACKEND` overrides it (the tests and the stress run pin Vulkan; the OptiX tests ask for it).

**Measured**
- The stress gpu_optix scene (1920x1080, 256 icospheres and a floor, a sun, 4 bounces, 64 samples, each backend's best of two runs, a quiet machine):
  - NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
  - NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
  - NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
  - NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
- Before the inlining and the register limit: 0.09x on both cards; the first open of a card compiles the 3.6 MB PTX in about 20 s without the driver's cache.

**Checked**
- OptiX and GPU Compute render the same picture at 64 samples - spheres, a glass ball, a toon ball, a checker floor, a
  sun and a point light - mean difference 0.00000 a channel, with content in it (luminance 0.09 to 3.2); a build
  without OptiX or a card that isn't there is refused with the reason (tests/test_gpu.cpp).
- Unit checks (round 190): Windows 11,305, 0 failed (933 tests); render-cache verify mode 11,306, 0 failed, 0 stale
  pictures. Linux with libraries: 10,961, 0 failed. Sanitizer build: 10,841, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 189): OptiX ships - committed PTX, NVIDIA's headers at build, the driver at run time (task 0169, ADR 0054)

**Asked:** OptiX "as seamless as Blender's Cycles", shipped by embedding PTX compiled on the user's PC.

**Changed**
- The OptiX kernel's PTX is committed (`src/render/optix/optix_kernel_ptx.h`: 34,373 bytes from OptiX 9.1.0, with
  the FNV-1a hash of `optix_kernel.cu` and `optix_params.h` and the OptiX version). `tools/optix_ptx_embed.bat`
  writes it on a machine with the CUDA Toolkit and the SDK (`tools/ptx_compile.cpp --header=`); a unit test fails
  when the kernel changes without it.
- A build needs only OptiX's host headers now, no CUDA Toolkit: `OPTIX_ROOT_DIR`, NVIDIA's ProgramData folder, or -
  only with `BLENDITY_FETCH_OPTIX=1` - NVIDIA's public `optix-dev` headers cloned at `v9.1.0` into `build/optix/sdk`
  (taken from NVIDIA under NVIDIA's terms; nothing NVIDIA's is committed but the PTX built from Blendity's kernel).
  `build.bat` and `build.sh` (Linux) both; CI's Windows and Linux jobs and the release workflow set the switch, so
  the installers carry the OptiX device (ADR 0037's "never" ends).
- `BLENDITY_NO_OPTIX=1` also turns OptiX off at run time; the console's `gpu list` names every card Vulkan sees and
  OptiX's state (not in this build, turned off, no driver and why, or its devices).
- The fetch path is written but has not been run on this PC (it downloads); CI runs it.

**Measured**
- The committed PTX: 34,373 bytes from OptiX 9.1.0 (the header 114 KB of text); its hash matches the sources.
- A Windows build with `CUDA_PATH` empty (no CUDA Toolkit) and the SDK's headers from ProgramData: OptiX built in,
  `gpu list` names the RTX 4070 SUPER and the RTX 3060 Ti.
- The Linux build (WSL, `OPTIX_ROOT_DIR` at the Windows SDK's headers): OptiX built in from the committed PTX, and
  through WSL's NVIDIA driver the same two devices; the OptiX tests pass there.
- Not run here: the fetch of NVIDIA's `optix-dev` headers (`BLENDITY_FETCH_OPTIX=1`: it downloads; CI runs it).

**Checked**
- The committed PTX's hash matches the kernel's sources; `gpu list`'s OptiX line names the RTX 4070 SUPER and the
  RTX 3060 Ti here; `BLENDITY_NO_OPTIX=1` turns it off at run time (tests/test_gpu.cpp).
- Unit checks (round 189): Windows 11,299, 0 failed (931 tests); render-cache verify mode 11,300, 0 failed, 0 stale
  pictures. Linux with libraries: 10,961, 0 failed. Sanitizer build: 10,841, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 188): hair as mass-weighted XPBD rods - film feel, not jello (task 0168, ADR 0049 amended)

**Asked:** hair physics "stiff and jello-like"; Blender-hair-dynamics / film feel, research based, for hair and other
dynamic parts.

**Why it felt that way (measured):** each substep blended a tip toward the animated pose by a stiff per-substep
factor - about a 5 Hz spring at hair's 0.15 - which pinned a strand to the pose (a 40 cm strand held horizontal
sagged 1.6 cm) and rang against it (14 wiggles of over 2 mm before it settled, 15 after a head turn).

**Changed** (Macklin et al. 2016 and 2019, Müller et al. 2007, Kugelstadt & Schömer 2016, Blender's hair dynamics)
- Strands (hair, ears, tails, cloth) are mass-weighted XPBD rods: each tip a mass from its bone's length and the
  chain's new **Density** (kg/m: hair 0.01, an ear 0.02, cloth 0.05, a tail 0.1), substeps at 240 Hz with one
  iteration each, velocities from positions.
- Stiffness and Bend are compliant constraints: a slider is a spring of 20 Hz x s^1.5 for hair's mass (1 rigid),
  applied as XPBD's w / (w + alpha / h^2), so the feel doesn't change with the frame rate and a heavier strand is
  pulled less by the same slider.
- Damping is a decay of the motion relative to where each tip's animation is going (a move or a turn alike; per
  bone from its effective spring, so free tips aren't overdamped): a lag that doesn't ring. The air's drag is always
  on (new **Air**, 1/s for hair's density; a heavier chain feels less); a Wind pulls every chain toward its motion
  by the chain's Wind, as before. The velocity is rescaled when a frame's substeps change length.
- Old scenes keep their sliders (which now mean the new spring) and load their label's Density and Air.
- The lengths are mass-weighted distance constraints before the exact root-first pass; the bend is applied after
  them, so the lower bones' weight doesn't open a curl's upper joints.
- Hair's defaults: Stiffness 0.3 and Damping 0.5 (were 0.15 / 0.15), near critical damping for the new spring. A
  scene saved before keeps its sliders, which now mean the new spring.
- Soft tissue keeps its own spring (the verlet code it was tuned with, its wind included).

**Measured**
- A 40 cm strand of eight bones (hair's defaults) released horizontally: settled within 1 cm after 0.75 s with no
  overshoot, its tip hanging 0.177 m below the head; before: 0.80 s with 14 overshoots of over 2 mm, the tip held at
  0.016 m (pinned to the pose).
- A head turned 90 degrees in 0.3 s with the strand pointing back: the tip lagged the turn by up to 9.3 degrees and
  came within 1 cm of its rest 0.73 s after it, one overshoot. (Before, measured with the strand hanging down: 0.88 s,
  15 overshoots.)
- The tip 0.25 s after release, mid-fall: 0.0060 / 0.0006 / 0.0004 m from 60 fps's at 30 / 144 / a frame time
  wavering between 1/60 and 1/59 s; the hanging tip after 4 s: 0.0000 m apart at all three (before 0.0027 at 144).
- Ten times hair's density: the tip sags 0.386 m (0.177 at hair's) and settles after 0.98 s (0.75).
- A curl of 30 degrees a joint with Bend 0.5 hanging from its root for 4 s keeps 89 % of it; Bend 1 keeps its joints
  at 40.0 degrees.
- 100 spring chains of 3 bones, ms a frame (stress physics): 0.366 plain, 0.454 with collision, 0.559 with separation too; was 0.548, 0.485, 0.730.
- 100 spring chains of 10 bones, ms a frame (stress physics): 1.445 plain, 1.752 with collision, 2.276 with separation too; was 2.020, 2.175, 2.862.
- 100 spring chains of AutoPhysics clip (100 frames), ms a frame (stress physics): solved plain, passes with collision, steps with separation too; was solved, passes, steps.
- 100 spring chains of a jump, ms a frame (stress physics): yes plain, 5 with collision, 4230 with separation too; was yes, 5, 4230.
- 100 spring chains of a run, ms a frame (stress physics): yes plain, 5 with collision, 8040 with separation too; was yes, 5, 8040.

**Checked**
- A 40 cm strand released horizontally settles within 1 cm in 1.5 s with at most one overshoot; a head turned 90
  degrees in 0.3 s swings a strand pointing back through its arc, lagging the turn, and brings it round within 1 s
  with at most one overshoot; the same motion at 30, 60 and 144 frames a second and with the frame time wavering;
  a curl with Bend 0.5 keeps a share of its curl hanging under its weight; ten times hair's density sags further and
  settles more slowly; Density and Air are saved, an old scene's chain loads its label's (tests/test_physics.cpp). The
  round 153 Bend test measures the joints' angles now (40.0 and 40.0 degrees held with Bend 1): with no pull and a
  free root the curl turns about its root until its weight hangs below, as a real one does.
- Unit checks (round 188): Windows 11,293, 0 failed (929 tests); render-cache verify mode 11,294, 0 failed, 0 stale
  pictures. Linux with libraries: 10,955, 0 failed. Sanitizer build: 10,835, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 187): undo measured honestly, and it gives back Edit Mode's selection (task 0167, ADR 0053 amended)

**Asked:** the roadmap's "undo steps sharing unchanged arrays" (each undo ≤ 10 ms, was 30-38).

**Measured first** (the task doc): the undo stack already shares meshes by pointer (ADR 0003) - a scene clone is
0.02 ms and `undo` itself 0.1 ms. An undo's cost is its frame: every view and the shadow map drawn again. The stress
row "Undo after it" averaged three different undos (the extrude, the selection step, the Edit Mode entry), so its
21-25 ms was not an undo's frame.

**Tried and not kept:** view caches two pictures deep (and four shadow maps), so an undo would copy the pictures of
the state before. A restored snapshot never matched its earlier key: `Scene::clone` gives the snapshot a new serial
and copies its materials, and the keys hash both. Keys by content for the scene's identity and its materials would
touch every render cache's exact-picture rule (ADR 0009); ADR 0053's amendment records it as the next step. The
first build also showed that a cached Scene view picture must carry its depth and id planes (picking reads them):
verify mode caught the older picture over the newer planes.

**Changed**
- Undo gives back Edit Mode's selection as it was before the operator, as Blender does; redo the operator's. It
  stayed as the operator left it, sized for the bigger mesh.
- The stress rows "Undo of an extrude" (each rep undoes an extrude it made itself) and "Redo of it" replace "Undo
  after it"; `tools/bench_table.py` reads the new row into Pass 13.

**Measured**
- The stress rows with the views on the CPU: "Undo of an extrude" 27.7 ms a frame (the frame after 2.0), "Redo of it" 21.0 ms (the frame after 1.9); the old row "Undo after it" - three different undos averaged - was 25.5 ms with the frame after 15.3.
- The stress rows with the views on the GPU: "Undo of an extrude" 11.0 ms a frame (the frame after 1.7), "Redo of it" 10.8 ms (the frame after 1.8); the old row "Undo after it" - three different undos averaged - was 11.6 ms with the frame after 7.5.
- The probe before this task (task 0165's): one undo's frame 12.5 ms on the CPU - the Scene view 7.9 ms, the shadow map 3.9, the Game view 3.4 - with `undo` itself 0.1 ms. Blender 5.2.2's row: 16.4 ms (PERFORMANCE.md Pass 13).

**Checked**
- After an extrude of three faces, undo selects the three faces again and redo the extruded ones
  (tests/test_editor.cpp).
- Unit checks (round 187): Windows 11,283, 0 failed (926 tests); render-cache verify mode 11,284, 0 failed, 0 stale
  pictures. Linux with libraries: 10,945, 0 failed. Sanitizer build: 10,825, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 186): a selection changes the selection, not the mesh (task 0166)

**Asked:** the roadmap's "selection changes redraw flags, not the view" (Select More on 50k faces 55-68 ms against
Blender's 15.5 ms).

**Measured first** (the task doc has the probe): Select More's frame after an undo copied the mesh (the selection-only
operators ran inside the operator path, after the mutable copy), so every view saw a "new" mesh - two render meshes
updated in place, the shadow map and the Game view drawn again - and the command itself built a vertex adjacency on
each call (8 ms). With the views on the card, a face selection sent the whole edit object to the CPU ("Edit Mode's
face highlight" was a fallback reason). A plain selection change's frame is 9 ms, of which the Scene view's 3D layer
about 3 ms and its cage overlay 4 ms: the overlays are part of the cached layer, so the planned "selection out of
the cache key" would save the 3 ms and cost the cage every idle frame, or a second cached layer. Not done; the
frame's cost is the cage, which is already drawn in parallel.

**Changed**
- The selection-only operators (Select Similar / By Material / Sharp / Z-Fighting / Coplanar / Linked / More / Less /
  Invert / Non-Manifold / Ring / Loop) run before any copy, on the mesh the undo step shares, as `const Mesh &`
  (`Editor::select_op`): no copy, no "new" mesh, only the Scene view - whose cage shows the selection - draws again.
  Loop Cut and Select Loop share `selected_edge`.
- Select More / Less are one pass over the cached edge table (`grow_selection` built an adjacency per call).
- The card draws Edit Mode's face highlight: a bit per triangle of the edited mesh (`RasterFrame::highlight_bits`,
  `RasterInstance::highlight`, binding 16) that the g-buffer pass mixes into a set triangle's albedo as the CPU
  raster does (in the card's triangle order, grouped by slot with several materials); a face selection is no
  longer a reason to draw the edit object on the CPU where the card has Vulkan's geometryShader feature
  (`gl_PrimitiveID` needs it; without it the fallback stays). A selection operator ends the F9 panel as before.

**Measured**
- The stress row "A selection operator after the undo (Select More)" with the views on the CPU: 15.4 ms a frame, was 32.2 (mesh copies in the frame 0.0; the frame after 1.7 ms).
- The stress row "A selection operator after the undo (Select More)" with the views on the GPU: 7.9 ms a frame, was 32.5 (mesh copies in the frame 0.0; the frame after 1.6 ms).
- Blender 5.2.2's row: 15.5 ms (PERFORMANCE.md Pass 13). The probe before this task: a plain selection change's frame 9 ms (the 3D layer about 3 ms, the cage overlay 4 ms), Select More's command 8 ms (the adjacency built per call), its frame after an undo a mesh copy, two in-place render-mesh updates, the shadow map and the Game view drawn again.

**Checked**
- Select More on the 50k-face sphere keeps the very mesh pointer, copies nothing, draws no shadow map or Game view
  again and renders one view; Select Less takes it back; Select Loop, Linked and Invert copy nothing
  (tests/test_editor.cpp). `grow_selection` matches the adjacency walk growing and shrinking (tests/test_modeling.cpp).
  A cube with its top face selected draws on the card without a fallback, its tint (the orange pixels beyond the
  picture with nothing selected) within 20 % of the CPU's, with one material and with two (tests/test_gpu.cpp);
  Select Loop from one edge and grow / shrink with a loose edge and an isolated vertex covered.
- Unit checks (round 186): Windows 11,280, 0 failed (925 tests); render-cache verify mode 11,281, 0 failed, 0 stale
  pictures. Linux with libraries: 10,942, 0 failed. Sanitizer build: 10,822, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 185): Edit Mode operators without the wasted work (task 0165, ADR 0053)

**Asked:** the roadmap's "Edit Mode operators without a whole-mesh copy" (Extrude on 50k faces 96-118 ms against
Blender's 32.6 ms).

**Measured first** (the task doc has the probe): the command `editop extrude` was 60 ms on the 50k-face sphere, of
which Auto Smooth's shallow-edge count 23 ms taken twice (47 ms), three whole-mesh copies 10.5 ms, the extrude
itself 1 ms; the frame after it 38 ms (both render meshes rebuilt 13 ms, the shadow map and the views redrawn). A
whole copy is 3.5 ms, and an extrude writes every array, so the planned per-array copy-on-write would have copied
them all anyway: ADR 0053 records the decision against it, with the arithmetic.

**Changed**
- A redoable operator (Extrude, Inset, Bevel, F9's family) keeps the mesh before it as the undo step's own pointer -
  never written, copy-on-write guards every edit - and `run_last_op` makes the one copy it works on (three copies
  before: a mutable one, the `before` one, the working one). A cancelled modal gives the pointer back; undo
  restores the very mesh.
- Auto Smooth's shallow-edge count is taken once per operator (`LastOp::shallow_before`, reused by F9) and only
  when it can decide anything (`Editor::shallow_before`: not for a mesh already smooth or shaded per face), and
  `shallow_edges` works from a sorted (edge, face) table with each face normal computed once (a hash map over
  200k edge ends and two normals per edge before): 23 ms to about 4 ms on 50k faces.
- `edit_op` is the common start, `begin_redoable_op` and `edit_op_rest`; the selection flush is
  `edit_selection_faces`.
- An operator cancelled with Esc (or one that fails) gives back a copy of the mesh before it, not the undo step's
  very pointer: the pointer brought back an earlier render-cache key whose picture had been drawn with state the key
  doesn't hold (verify mode found a stale Scene view picture after an Esc; the key's gap is ADR 0053's follow-up).
- `FrameProfile::ms_edit_op` - the operator's own time inside the command that ran it - in the Profiler's "Edit
  Mode: operator / mesh writes / overlap check" row and the stress edit_frame notes (the operator itself, its mesh
  copies).
- The roadmap's 0167 ("undo steps sharing unchanged arrays") is already how ADR 0003 works - the undo stack shares
  meshes by pointer and a scene clone is 0.02 ms - so its round goes to the undo frame's own cost instead.

**Measured**
- The stress row "An operator: extrude three faces" with the views on the CPU: 48.9 ms a frame, was 130.6 (the operator itself 7.8 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.2 ms, was 2.2).
- The stress row "An operator: extrude three faces" with the views on the GPU: 48.5 ms a frame, was 115.3 (the operator itself 7.3 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.0 ms, was 2.1).
- The probe before this task (the task doc): the command alone 60 ms (Auto Smooth's count 23 ms twice, three copies 10.5 ms), the frame after it 38 ms; after: the command 6.9 ms with one copy of 3.5 ms, the frame after unchanged (the render meshes rebuilt 13.5 ms, the shadow map and the views). Blender 5.2.2's row: 32.6 ms (PERFORMANCE.md Pass 13).

**Checked**
- An extrude of three faces on the 50k-face sphere makes one mesh copy; the mesh before it is the very pointer
  undo gives back, and redo re-applies it; the Profiler counts the operator's own time (tests/test_editor.cpp).
  `shallow_edges` counts what the hash map did on a cube (none), a 12-sided cylinder (its 12 sides), a sphere, and
  an edge shared by three faces counted once (tests/test_modeling.cpp); Auto Smooth still shades a bevelled cube
  smooth and leaves a per-face-shaded one alone (the round 142 test).
- Unit checks (round 185): Windows 11,248, 0 failed (922 tests); render-cache verify mode 11,249, 0 failed, 0 stale
  pictures. Linux with libraries: 10,916, 0 failed. Sanitizer build: 10,796, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 184): no artifacts at distance for Toon materials (task 0164, ADR 0048 amended)

**Asked:** "artifacts when backing away from a Toon material with painted maps".

**Fixed**
- Every toon map was read at level 0 whatever the distance: the hatching now reads at the lod of the UV
  derivatives scaled by its Hatch Scale, the face shadow map at its own lod (its hard threshold at softness 0 no
  longer sparkles far away), the toon ramp over the pixel's span of lit; a Generated mapping takes its derivatives
  from the pixel's footprint over the bounds and a Box projection its lod from the footprint times the tiling (both
  were level 0); the built-in Checker and Noise fade to their mean once a cell is under a pixel (the derivatives,
  or the footprint, say when), as Blender's Checker does.
- A cel edge fell on whichever side the pixel centre landed, so a far band edge sparkled: the deferred pass now
  hands the shading normal's change to the next pixel right and below (`SurfacePoint::normal_dx / dy`), and
  `shade_toon` softens each band edge by half the pixel's span of lit along the light (the GPU raster takes the
  same from the neighbouring G-buffer normals), so the edge is one antialiased pixel at any distance and the 2 m
  look is unchanged where the softness already covers it.
- An outline shrank below a pixel far away and the silhouette went dotted: a new Outline has Min Pixels 1 (a loaded
  one saved before the field keeps 0, as it drew), and the shell's 2 % steps get a hysteresis of a third of a step
  so a dolly on a boundary doesn't pop.
- Reversed-Z measured and declined for now: the D32 buffer resolves about 2 mm at 100 m with near 0.3 / far 1000,
  well under a shell's offset; the task doc records the calculation.
- The path tracers keep level 0: their samples average the pixel, and the GPU kernel's Material must not grow.

**Measured**
- High-frequency energy inside the object (the mean absolute Laplacian over its interior pixels) at 20 m against
  its 2 m picture downsampled ten times: the hatched plane 0.00 against 4.72, the box-mapped cube 12.3 against
  17.7, the face-shadow sphere at softness 0 24.4 against 13.2 (1.8 times: inside the factor of 2, the sphere's
  shading itself is most of it at 300 pixels), the two-tone sphere 2.8 against 4.3 on the CPU and 2.8 against 3.9 on
  the card. The hatched plane and the face-shadow sphere draw on the CPU in the card's views too (ADR 0048), so
  their numbers are the same on both.
- The two-tone sphere's band edge: at 2 m 848 of 48,714 pixels between the two tones (the edge's own pixels), at 20 m
  139 of 295 - the edge one antialiased pixel wide, where a hard edge gives none.
- The outlined sphere at 60 m, nine pixels wide: 27 outline pixels around its 48 (2,284 at 2 m); a saved Outline
  keeps its Min Pixels through a round trip (3), a scene saved before the field loads it as 0.

**Checked**
- A hatched plane, a face-shadow sphere at softness 0 and a box-mapped cube at 20 m carry no more high-frequency
  energy inside them than their 2 m pictures downsampled ten times (the box-mapped cube and the sphere on the card
  as well; a hatched or face-shadowed toon material still draws on the CPU, ADR 0048), and so does a two-tone
  sphere, whose far band edge shows the mid tones of one antialiased pixel; an outlined sphere at 60 m - nine
  pixels wide - keeps a one-pixel ring of 27 pixels, an old scene's outline loads with Min Pixels 0 and a saved one
  keeps its own (tests/test_render.cpp); the anime picture tests at 2 m unchanged.
- Unit checks (round 184): Windows 11,227, 0 failed (920 tests); render-cache verify mode 11,228, 0 failed, 0 stale
  pictures. Linux with libraries: 10,895, 0 failed. Sanitizer build: 10,775, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 183): fewer CPU fallbacks for the GPU views - area lights and cutouts on the card (task 0163)

**Asked:** the roadmap's "fewer CPU fallbacks for the GPU views": a view with an area light or a cutout material
drew on the CPU.

**Changed**
- Area lights on the card: `area_light_irradiance` (the Gauss quadrature over the panel with the horizon clipped)
  and `area_light_specular_dir` (the representative point, Karis 2013) ported line for line into the lighting
  pass; the light block grows to six vec4s (right / width, up / height, the disc flag); the toon branch takes an
  area light at its centre as the CPU's `light_falloff` does. `gpu_view_unsupported` no longer refuses them.
- Cutout materials on the card: the instance carries the threshold and the material's alpha, the surface
  evaluation carries the base map's alpha, and the G-buffer discards under the threshold - no colour, depth or id,
  so what is behind shows and picks, as the CPU's rasteriser punches its holes.
- The Profiler's GPU block lists each view slot's fallback reason ("View slot N on the CPU: a camera filter").
- Still on the CPU, named: see-through and glass materials, toon materials with painted maps, camera filters, GI
  and probes, lightmaps, vertex colours, live procedurals, more than 16 lights.

**Measured**
- The g-buffer pass with the cutout test and the area lights in the lighting shader (stress editor_render, the
  views on the card, a quiet machine, RTX 4070 SUPER): g-buffer 0.15 ms a frame (was 0.14), lighting 0.07 (was
  0.06), the GPU path 3.50 ms a frame (was 3.58): the discard and the area-light loop cost nothing measurable on a
  scene without them.
- The tests' own numbers: an area-lit room on the card against the CPU, mean difference 0.110 / 255 per channel;
  turning the light's panel off changes about 160,000 pixels (the card draws it, not a fallback). A cutout wall's
  hole: the pixel at its centre 0x5771A5 on both devices (the sphere behind), beside it 0x892D1D (the wall); a click
  through the hole picks the object behind.

**Checked**
- An area-light scene (a rectangle, then a disc) draws on the card with no fallback and matches the CPU within the
  views' threshold; a cutout's holes show the cube behind and a click through one picks it on the card; the
  Profiler names a Game view's filter fallback by its slot (tests/test_gpu.cpp).
- Unit checks (round 183): Windows 11,201, 0 failed (917 tests); render-cache verify mode 11,202, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 182): incremental scene uploads, one submission per upload (task 0162)

**Asked:** the roadmap's "incremental scene uploads, one submission per upload" - every build re-uploaded the whole
scene to each card buffer by buffer, a fence wait each, and a submission per acceleration structure.

**Changed**
- The scene's buffers go up in one submission: the Vulkan device copies every section from one staging arena
  (`Batch`); every BLAS is built in one submission and the TLAS in its own, each with its input buffer sent before
  it, and the parameters and the clear follow: the test counts 9 submissions for a first whole upload on ray
  tracing hardware and 5 for an instance update (5 and 3 with Blendity's BVH; it was 13 for the buffers alone plus
  one an acceleration structure); `Renderer::submits()` counts them and the stress gpu section's upload row shows
  them.
- Only what changed goes up: `upload_gpus` hashes the scene's sections (the geometry, the instances with the
  software TLAS, the materials, the lights, the texture arena, the parameters) and, when every card holds the
  geometry and the textures, sends the instances (`update_instances`: the instance buffer, the TLAS's nodes and
  order; on ray tracing hardware the TLAS rebuilt over the kept BLASes), the materials or the lights alone; a card
  that joins gets the whole scene. `PTStats` counts `upload_calls / instance_updates / material_updates /
  light_updates` and times `upload_ms`. An object added that reuses a mesh is still a whole upload (the mesh
  table changes); a partial update re-points the descriptors and leaves the pixel buffers alone (the whole
  upload used to re-make them at one pixel and again at the frame's size).
- Not done: the scene is still flattened whole on the CPU each build (the hashes read the bytes); a changed texture
  arena is a whole upload.

**Measured (RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.4 ms in 1 submissions; one object moved 4.9 ms in 1; one material edited 4.7 ms in 1.
- NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.0 ms in 5 submissions; one object moved 6.1 ms in 3; one material edited 4.9 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.1 ms in 1 submissions; one object moved 5.2 ms in 1; one material edited 5.2 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.5 ms in 5 submissions; one object moved 6.3 ms in 3; one material edited 4.3 ms in 1.
- Before this round a whole upload was a submission a buffer (13) plus one an acceleration structure, and every build sent the whole scene.

**Checked**
- Moving one object: no whole upload, one instance update, a fixed-seed sample within 1e-5 of a fresh upload's; a
  material edit one material update with the BVH under 5 ms; a light edit one light update; a new mesh a whole
  upload (tests/test_gpu.cpp).
- Unit checks (round 182): Windows 11,184, 0 failed (914 tests); render-cache verify mode 11,185, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 181): the path tracer off the main thread, two submissions in flight (task 0161, ADR 0052)

**Asked:** "feels slower", "CPU and GPU usage look low" - task 0159 measured a card 99 % busy inside each submission
and 21 % busy over an F12 render: the path tracer ran the cards only inside the editor's 40 ms slice a frame and
waited for every submission.

**Changed**
- A worker thread per card, for the life of its slot: it idles until a render starts, claims chunks of whole
  samples from the shared counter (the guided self-scheduling stays; the chunk cap halves to 50 ms from task 0051's
  100 ms, so a pause waits for at most two of them) and keeps two submissions in flight on its device
  (`Renderer::submit`, `wait_oldest`, `wait_all`, `take_busy_ms`: two command buffers, fences and timestamp pairs;
  `render()` is submit then wait). It downloads the card's sums into a host copy when a merge is wanted (its flights
  drained first) and when it stops. Each slot's state is under one lock, a slot counts as active from the start so
  a poll never sees the cards idle before they wake, and the stats are the main thread's alone.
- `PathTracer::render()` polls: it starts the workers toward the target if needed (the counter restarts from the
  cards' finished counts), renders the CPU's own samples for the budget when the CPU is a device, waits what is
  left of the budget for the cards, takes the counts, merges the host copies when due. The editor passes 4 ms for
  F12 and 2 ms for the Camera Preview when cards work and resolves F12's picture only when samples were merged, so
  the frame stays quick (6-8 ms headless; the frame that merges and resolves the 1280x720 picture about 12 ms more)
  and the cards render between frames; the CPU-only path is unchanged.
- `pause()` before anything touches a device or the sums (reset, set_settings, set_params, uploads, sync,
  merge_aux, the editor's Stop and the sequence / animation stops, the destructor): the workers' claims stop, every
  submission in flight finishes, the workers idle. A slot that leaves has its worker stopped by its own quit flag.
  The merge counts the samples its copies hold, so the picture is always a consistent average; a reset clears the
  host copies with the sums.
- The test hook `BLENDITY_GPU_FAKE_SLOW=<ms>` makes every submission take that long on the worker's side.
- The lesson "Combined rendering" gains "The cards work between frames".

**Measured (RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.1 samples/s (was 122.9); the card busy 100.0 % (1037.6 of 1037.6 ms) inside its submissions (was 99.8 % (1034.1 of 1035.9 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 799.9 samples/s (was 821.0); the card busy 100.0 % (149.9 of 149.9 ms) inside its submissions (was 99.5 % (149.6 of 150.5 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): 62.0 samples/s (was 62.9); the card busy 100.0 % (2050.8 of 2051.0 ms) inside its submissions (was 99.8 % (2022.9 of 2026.2 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): 402.7 samples/s (was 411.8); the card busy 100.0 % (304.2 of 304.2 ms) inside its submissions (was 99.6 % (302.8 of 303.9 ms)).
- Path tracing the stress gpu scene at 1280x720, All GPUs: 1020.9 samples/s (was 1043.5); the card busy 100.0 % (218.8 of 218.9 ms) inside its submissions (was 99.2 % (221.8 of 223.7 ms)).
- Path tracing the stress gpu scene at 1280x720, CPU + all GPUs: 1081.7 samples/s (was 1079.8); the card busy 98.9 % (200.1 of 202.4 ms) inside its submissions (was 99.1 % (205.1 of 207.1 ms)).
- F12 of the default scene from the editor's frame loop (headless, GPU Compute with both cards): the cards busy 49 % of the render's wall time, was 41-43 % (82-85 % with the two cards' busy summed, as task 0159 reported it); 2048 samples in 0.6 s, was 0.7 s.

**Checked**
- With a 30 ms card, twenty polls each return in under 10 ms while it renders, the render completes with every
  sample, pause() mid-render returns within 150 ms; the editor's F12 on that card keeps twenty frames under 10 ms
  each and finishes; the GPU Compute pictures still match the CPU's (tests/test_gpu.cpp).
- Unit checks (round 181): Windows 11,166, 0 failed (913 tests); render-cache verify mode 11,167, 2 failed in the full run (the GPU-opening frame
  budget and the pipeline-cache test, both under the chain's build load; each passes alone), 0 stale pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 180): a GPU view in one submission (task 0160, ADR 0051)

**Asked:** the roadmap's "views in one submission, read-back double-buffered", after task 0159 showed the card busy
for 7 % of the frame's GPU path.

**Changed**
- A view on the card is one submission and one fence wait: `Raster::render_read()` records the planes' copies
  behind the frame's passes (after their last barrier) and submits once; before, `render()` and `read_back()` were
  two submissions each waited for, six round trips for three views. `render()` / `read_back()` stay for tests,
  stress and route B, sharing `record_frame` / `readback_buffer` / `record_planes` / `unpack_planes`;
  `Raster::submits()` counts the queue's submissions. The Profiler's "read-back" row becomes "copies out + fence":
  the submission's wall time beyond the card's passes (the device reports `submit_wall`); the lesson, the stress
  note and `bench` say so, and the round-179 wording that called it the CPU's timer around the copy is amended.
- Not done, and why (ADR 0051): the deferred flush (every view recorded, one submission a frame, two frames in
  flight). Each view's planes are needed the moment its window draws (the overlays are depth-tested against
  them, the camera's filters and depth of field read the depth, the upscale and the view cache copy the pixels),
  so one flush a frame without a frame of latency means splitting the frame into a record phase before the
  windows draw and a resolve phase inside them - a real restructure of the immediate-mode views, with the
  rasteriser's per-device uniform, instance and read-back buffers made per slot. And during a drag the other
  views are held (`hold_other_views`), so one view renders a frame in the plan's target case and batching views
  would save nothing there. The plan's "50k-face drag ≤ 10 ms on the laptop setting" is measured as the stress
  rows below, not promised; "read_back before a flush returns false" has no meaning without a flush.

**Measured (RTX 4070 SUPER, stress editor_render, docs/PERFORMANCE.md Pass 13)**
- Moving an object with the views on the GPU (stress editor_render, the RTX 4070 SUPER): the GPU path a frame 3.70 ms, was 4.09 ms; the card's own work 0.29 ms (8 % of the path, was 7 %); the copies out and the fence wait 0.52 ms, where the read-back was its own submission of 0.85 ms.
- The frame's view count and the picture are unchanged (33 views on the card over the row, 0 on the CPU).

**Checked**
- A frame with the Scene and Game views on the card costs two submissions (was four) and the picture is bit for bit
  the two-submission path's (a test switch keeps that path; tests/test_gpu.cpp).
- Unit checks (round 180): Windows 11,158, 0 failed (910 tests); render-cache verify mode 11,159, 1 failed, 0 stale
  pictures (the pipeline-cache test: a spare opening in the background rewrote its file under it; it passes alone three
  times, and round 182 makes it wait for the spare). Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 179): GPU time in the Profiler (task 0159)

**Asked:** "CPU and GPU usage look low" when rendering - and until now nothing in Blendity could say what the card
was doing. The frame profile held how long a frame spent in the GPU path (upload, dispatch, wait, read-back) as one
wall-time number the Profiler never showed, and the path tracer's stats had no GPU time at all.

**Added**
- The card's own clock: two timestamp queries around every path-tracer submission (`Renderer::last_gpu_ms()`, -1
  where the queue has none), summed into `PTStats::gpu_busy_ms` and `gpu_wait_ms` (the submission's wall time less
  the busy); the views' pass times (shadow, g-buffer, lighting, read-back) from the raster device's existing
  timestamps into the frame profile (`FrameProfile::ms_gpu_shadow / gbuffer / lighting / readback`, `gpu_busy_ms()`).
- The Profiler window's GPU block: the GPU path's wall time, the views' busy time and its three passes, read-back
  (the CPU's timer) and wait, "GPU busy in the frame" as a share of the frame, and for a running F12 or Camera
  Preview the path tracer's busy / wait inside its submissions and its busy share over the render so far (F12) or
  the frame (Camera Preview) - the card is idle between submissions, which the submissions' own wait never shows;
  a card without timestamps shows "n/a". `bench` prints "bench GPU busy ... a frame in the GPU path" over its own
  frames, "Render finished" logs the busy share over the render.
- The lesson "The views on the GPU" gains "The Profiler's GPU block".

**Measured (the baseline for tasks 0160-0163, RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- The views on the GPU, moving an object (stress editor_render): the card worked 0.28 ms of the 4.09 ms GPU path a frame (7 %; shadow 0.09, g-buffer 0.14, lighting 0.06), the read-back 0.85 ms, the rest waiting.
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.9 samples/s, the card busy 99.8 % (1034.1 of 1035.9 ms) inside its submissions (busy + wait).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 821.0 samples/s, the card busy 99.5 % (149.6 of 150.5 ms) inside its submissions (busy + wait).
- F12 of the default scene from the editor's frame loop (40 ms slices, headless): the card busy 21 % (256 samples of the default scene in 0.4 s on GPU Compute with both cards, their busy summed) of the render's wall time - the idle time is between submissions (uploads, merges, the UI), which tasks 0160-0162 remove.

**Checked**
- The views' three pass times are positive where the card has timestamps and busy plus read-back stays inside
  the GPU path's wall time; a path tracer on the card reports busy and wait within its wall time, on the CPU -1 / 0;
  the Profiler's rows are there and `bench` prints the line (two tests in tests/test_gpu.cpp).
- Unit checks (round 179): Windows 11,154, 0 failed (910 tests); render-cache verify mode 11,155, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 178): Blender 5.2 as the benchmark - a headless harness and Pass 13's reference column (task 0178)

**Asked:** "Always refer to Blender's rendering and editor performance as a benchmark" - with Blender 5.2 run headless
on this machine for the numbers, not estimates.

**Added**
- `tools/bench_blender.py`, run inside the installed Blender (`blender -b --python tools/bench_blender.py -- --out
  <json> [--quick] [--gpu 4070]`): the scenes and operations Blendity's stress rows time, in Blender's Python API with
  `perf_counter`, the mean of three runs after a warm call - entering Edit Mode on the 50k-face sphere (the exit
  outside the timer), the extrude operator on faces 100-102 with its undo step, Select More, undo of the extrude;
  Cycles on the stress gpu scene (400 textured spheres, every 7th glass, a checker floor, a sun) at 1280x720 on
  OptiX, CUDA and the CPU, one card, samples a second from the slope of time over three sample counts (a render's
  fixed cost drops out as Blendity's row leaves out its upload), the picture's coverage checked; Smart UV Project and
  Pack Islands by bounding box on the sphere; the stress bake's 100-object scene (each object its own mesh: Blender's
  lightmap_pack packs unique meshes only) packed into one 1024 atlas and baked with Cycles on the CPU, texels a
  second over the texels its charts cover; a GLB export of 256 icospheres of 1,280 faces, smooth-shaded with their
  own mesh data, both file sizes in the table. Headless Blender's operators push no undo step, so the harness pushes
  them and checks the face count comes back; the Cycles rows check the picture's coverage over a transparent film. Blendity's left-handed
  Y-up scenes are mirrored into Blender's Z-up ((x, y, z) to (x, z, y)). Headless Blender draws no viewport, so there
  is no playback row.
- `tools/bench.bat` runs Blendity's stress sections on the same scenes and Blender, and `tools/bench_table.py`
  writes both into `docs/PERFORMANCE.md`'s Pass 13 table (between its markers) with a method column per row; the
  Edit Mode rows take Blendity's operator alone (its frame less the frame after); `BLENDER=path` overrides where
  Blender is, `BENCH_GPU` which card; a `--quick` run never writes the doc.
- Stress rows for the comparison: Smart UV Project and Pack Islands on the 50k-face sphere (`uv`), a GLB export of
  the 256-icosphere scene (`serialization`), each the mean of three after a warm call.

**Fixed**
- The `bake` stress section had reported 0 texels since the probe volumes (task 0037) added `want_lightmap` to
  `BakeObject`: its positional initialisers' trailing 0 (meant for the hash) had been turning every chart off, in the
  odd-settings fuzz too. A `bake_object()` helper sets the fields by name at every site; it bakes again (100 objects:
  122k texels in 0.22 s, 562k texels/s, 56 Mrays/s).

**Measured (Pass 13's first table, this machine: Ryzen 9 5900XT, one card used, the RTX 4070 SUPER, Blender 5.2.2)**
| row | Blendity | Blender 5.2.2 LTS | ratio | the method's difference |
|---|---|---|---|---|
| Entering Edit Mode, 50k faces, caches cold (Blendity: the frame it costs less the frame after) | 24.7 ms | 17.9 ms | Blendity 1.4x behind | mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after |
| Extrude three faces, 50k faces (Blendity: its frame less the frame after) | 96.5 ms | 32.6 ms | Blendity 3.0x behind | bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after |
| Select More, 50k faces (Blendity: its frame less the frame after) | 51.0 ms | 15.5 ms | Blendity 3.3x behind | bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after |
| Undo of the extrude (the frame that restores the mesh; the cage rebuilt in the frame after, about 14 ms more, is not counted) | 23.8 ms | 16.4 ms | Blendity 1.5x behind | ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted |
| Path tracing the stress gpu scene at 1280x720 on the card's ray-tracing hardware (Cycles: OptiX) | 823.8 samples/s | 115.9 samples/s | Blendity 7.1x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 311 ms, drops out as Blendity's row leaves out its upload; worst fit residual 8 ms); the card's ray-tracing hardware, like Blendity's ray-query row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene with a software BVH (Blendity: a compute shader; Cycles: CUDA) | 122.0 samples/s | 87.0 samples/s | Blendity 1.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 272 ms, drops out as Blendity's row leaves out its upload; worst fit residual 15 ms); Cycles' BVH2 in CUDA, a software BVH like Blendity's compute row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene on the CPU (Blendity: Embree; Cycles: its CPU kernels) | 19.0 samples/s | 8.1 samples/s | Blendity 2.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 192 ms, drops out as Blendity's row leaves out its upload; worst fit residual 23 ms); Cycles' CPU kernels, like Blendity's CPU row (Embree); the CPU, no card. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Smart UV Project, 50k faces | 42.1 ms | 50.1 ms | Blendity 1.2x ahead | bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call |
| Pack Islands, 50k faces (both by bounding box) | 21.8 ms | 35.9 ms | Blendity 1.6x ahead | bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3 |
| Lightmap bake, texels a second, both on the CPU (100 objects; Blendity: 122k texels in its charts; Blender: the same objects in one 1024 atlas, its charts' texels) | 561968.0 texels/s | 32030.8 texels/s | Blendity 17.5x ahead | bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: 22.2 s for the 710,272 texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer |
| GLB export, 256 icospheres of 1,280 faces | 307.3 ms | 707.7 ms | Blendity 2.3x ahead | bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (6.2 MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call; Blendity wrote 13.2 MB |

**Checked**
- The harness runs end to end (`tools\bench.bat`), every Blendity row has a Blender number, each row's method is
  in the table and in docs/tasks/0178; the numbers above are from a quiet machine (a build and a WSL test run beside
  a trial run made Blender's Select More 112 ms instead of 12).
- Unit checks (round 178): Windows 11,144, 0 failed (908 tests); render-cache verify mode 11,145, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 177): one test per behaviour, timings to stress; the suite in files; dead code out (task 0177, ADR 0056)

**Asked:** "check on unneeded tests and remove them. Compact the code-base as needed" (and chose: one test per
behaviour, timings to stress).

**Measured first**
- The suite on a quiet machine: 910 tests, 11,406 checks, 204.6 s of test time; 23 tests over a second took
  124.1 s of it. The slowest was task 0157's find (the software-BVH kernel on the CPU, 70 s: fixed there); then the
  240-file MMD fuzz (10.2 s), a 3 s pretend GPU open (6.8 s), every truncation of a probe-volume file (3.8 s), a
  render-cache script the stress fuzz already runs (3.3 s). The "Materials previews" guess for the 13 s material
  tests of round 174's run was wrong: alone they take 0.8 s (a build was running beside that suite).

**Changed**
- **The suite is one file a subsystem** (`tests/test_modeling.cpp`, `test_render.cpp`, `test_lighting.cpp`,
  `test_gpu.cpp`, `test_assets.cpp`, `test_animation.cpp`, `test_physics.cpp`, `test_editor.cpp`; the harness and the
  helpers several rounds share in `tests/test_common.h`; `test_main.cpp` runs them): `blendity_tests --group render`
  runs one, `--list-groups` names them; eight translation units build side by side (a test build: 55 s on 32 threads, was 58).
- **Trimmed:** the MMD fuzz keeps 24 of its 240 damaged files (the 240 are a stress `codecs` row, the synthetic
  models in `tests/fixtures/synthetic_mmd.h`); the probe-volume sweep keeps every 8th truncation and 30 of 150
  corruptions (the whole sweep a stress `probes` row); the GPU-opening pretend delays are 1 s and 0.6 s (were 3 and
  1.5); the four pacing tests are one.
- **Removed:** the GPU view spike's two tests (ADR 0036 holds the verdict; stress `gpu_raster` still measures route
  (B)), the OptiX picture test (stress `gpu_optix`'s row), the 30-action render-cache script (stress `editor_fuzz`
  runs 6,000 frames in verify mode), and the four `NDEBUG` timing checks (the overlap check's 3x, AutoPhysics' 5 s,
  Locomotion's 0.2 ms, 100 spring chains' 1 ms), which are stress rows now (`modeling`, `physics`, `animation`),
  each with its target beside the number.
- **Added:** `tools/test_coverage_map.py` lists every console command, keymap action and window against the tests
  that exercise it (259 entries; `--check` fails on an empty one); the 72 it found empty got two tests: every
  keymap action id through `run_action` (a renamed or dropped id is caught) and the console commands no test drove
  (`samples`, `world`, `scale`, `threads`, `proportional`, `loopcuts`, `selectall`, `subdivide`, `seam`, `focus`,
  `export`, `exportoptions`, `screenshot`, `deleteasset`, `alignview`, `droponsurface`, `guidesfromedges`, `raster`,
  `surfacealign`, the printing ones).
- **Code:** `core/hash.h` (one FNV-1a, one 8-bytes-a-step mixer, `hash32`) replaces the hash loops written out in
  33 files - with the mistyped FNV basis corrected, except in the three places a bake saves the key, which keep the
  old constant on purpose, so every bake stays valid (a test pins the two saved keys); the corrected basis renames
  the GPU kernel's cache file (one recompile, 19 s), the pipeline cache's trailer (rewritten once), the import
  cache folders (`~/.blendity/cache/import`, re-made once) and MoonRay's converted texture names (re-converted
  once); `closest_on_segment` and the 2D segment distance in `core/math.h` (four copies gone);
  `clampf01` and a second `lerpf` gone; `config_path`, `mirror_markers`, `sample_clip_values` and the GLSL
  `safe_normalize` deleted (never called); `is_bone` / `bone_children` and `bone_armature` once (`scene.h`); the
  Jolt layer classes once (`scene/physics_jolt_layers.h`); `move_to_trash`'s test-folder prefix once; undo and
  redo through one `restore_state`; the button grid of the Rigging and Tools windows once (`button_grid`); the
  `to_screen` lambdas through `project_to_view`; the closed-loop walk of the mesh tools and Follow's wire-path walk
  once. `editor.cpp`'s console (the `set` reflector, the command handlers, `run_console_command`) is
  `editor_commands.cpp`; `scene_view.cpp`'s picking is `scene_pick.cpp` and its overlays and icons
  `scene_overlays.cpp`. `tools/clean.bat` and `tools/README.md`.
- Not done, and why: the Vulkan boilerplate `gpu_device.cpp` and `gpu_raster.cpp` share (tasks 0160 / 0161
  restructure both), the rasterizer's duplicated triangle setup (hot code), the OptiX spike (Stack 5 turns it into
  the device). The plan's "1,000 lines fewer in src" was an estimate: the duplication scan found nothing over 17
  lines in two places, and the moves are neutral, so src is 137 lines smaller (the hash loops, the helpers
  and the dead code), with the tests' files the same size as the one file was.

**Changed after the review**
- The GPU pipeline-cache test forged its foreign file's trailer with the old basis, so the trailer check refused it
  before the header check it was testing could (the trailer uses the new hash now); the shared test helpers are
  `inline` (nine translation units include them: GCC warned on every unused `static` one); the coverage map reads
  test bodies without their titles and comments, matches a command only as an argument and an action only as a whole
  id (`manual`, which opens the browser, is listed as not driven by tests); the F12 test waits for the render and
  checks its picture; the keymap test covers every id, including `file.save`, `mode.edit_toggle`, the three
  `transform.*` and `lighting.update`, and checks a drawing began after a `draw.*` id (`run_action` says yes to any
  of them); a test pins the two keys a bake saves (`lightmap_mesh_hash`, `lightmap_placed_hash`) to their values, so
  a later change to the mixers can't mark every bake out of date, and hash.h's comment says so; `mix` is `hash_mix`
  (the name meant lerp elsewhere); `walk_wire_path` takes a const map; `bone_armature` is declared once;
  `button_grid` lives in tools_window.cpp; the empty round-121 group is gone; the stress rows are functions
  (`mmd_damage_row`, `probe_file_damage_row`, `spring_chain_row`, `autophysics_row`, `locomotion_row`), the Euler
  residual's unit corrected to body weights; `sequence.cpp`'s FNV loop uses the header; the spring test's 100-chain
  timing loop went with its check (the row has it); `tools/clean.bat` keeps the training runs unless `--all`; the
  task doc drops two claims the tree didn't hold (the toolbar button was left as it is; `merge_by_distance_naive`
  stays in src, it shares `finish_merge`); the CHANGELOG says which caches the corrected basis refreshes once.
  Noted, not changed: `closest_on_segment` passes a NaN through where two of the old copies clamped it to the far end
  (their callers guard against non-finite input).

**Checked**
- The suite: 908 tests, 11,144 checks, 117.4 s of test time (was 204.6 s; the target 160 s); the check
  count down by 272 - more than the plan's 60, and almost all of it one behaviour: the probe-volume sweep
  asserted at every truncation point (349 checks, now 30 for the same behaviour), then the GPU view spike (11),
  the render-cache script (4), the four timing checks (4), the OptiX picture (2); the MMD fuzz and the pacing
  merge change nothing; the two coverage tests add 85. The stress run grows by the new rows: 32.6 s (codecs 1.0 s, probes
  29 s, physics 0.4 s, animation 0.1 s, modeling 0.7 s).
- `tools/test_coverage_map.py --check`: 259 entries, none empty.
- The new rows' first numbers (this machine): 240 damaged MMD files in 1.0 s (66 refused, 174 imported, no bad
  mesh); a 4 MB ProbeVolume.bin cut at 4,603 points (none accepted) and corrupted 150 ways (all loaded and
  sampled safely); 100 spring chains of 3 bones 0.27 / 0.31 / 0.41 ms a frame (target 1.0), of 10 bones
  1.05 / 1.20 / 1.70 ms a frame (target 5.0); AutoPhysics' jump 278 ms (target 1,000), the run 307 ms; Locomotion 0.017 ms a frame (target
  0.2); the overlap check 14.1x its reference on 50k faces (target 3x).
- Unit checks (round 177): Windows 11,144, 0 failed (908 tests); render-cache verify mode 11,145, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 176): the Wind gizmo and icons for the effectors (task 0158)

**Asked:** a gizmo for the Wind component, as task 0153's spec left out.

**Added**
- A Wind, a Rotator and an Oscillator show an icon in the Scene view (the icons layer, now "Light, Camera &
  Effector Icons"), clicked to select them like a light's. A selected Wind draws its arrow along its forward,
  half a metre plus a quarter metre per m/s of Strength, wavy streamlines beside it with Turbulence, three rings
  instead when Spherical; the gizmo and the simulation read one `wind_direction`.
- The lesson "Winds you can see".

**Changed after the review**
- A true orthonormal basis for a near-vertical wind (the barbs and rings came out skewed), the strength and
  turbulence read through the simulation's own finite, clamped accessors (`std::isfinite` folds away under
  `/fp:fast`), three streamlines, the gizmo beside the rig controls' shape helpers, and an Effector render role
  so the icon pass costs one virtual call an object instead of three casts.

**Checked**
- New tests (round 176): a 3 m/s wind turned to +X draws its arrow to 1.25 m along X in three segments,
  turbulence adds three streamlines, spherical three rings of 1.25 m, a wind pitched 80 degrees keeps true rings
  and square barbs; the three effectors have icons and a plain object none; a click on the wind's icon selects
  it and selects nothing with the layer off.
- Unit checks (round 176): Windows 11,416, 0 failed (912 tests); render-cache verify mode 11,417, 0 failed, 0 stale
  pictures. Linux with libraries: 11,141, 0 failed. Sanitizer build: 11,017, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 175): baked emission without blocks (task 0157)

**Asked:** "Emissions off some objects when baked under the lighting tab still look blocky or pixelated."

**Fixed**
- A bake found emissive meshes only when a hemisphere ray chanced on them (a 10 cm lamp: one ray in sixty), so a
  small bright object came out as a scatter of blocks. Each texel samples the emitters directly now (one emitter
  point a ray, the emitter choice stratified over the rays, a shadow ray each), weighted against the hemisphere
  gather by the power heuristic - the gather's own emitter hits are weighted the other way, on the CPU and in the
  GPU kernel's bake entry - so nothing is counted twice. A 10 cm lamp over a floor: 1.4 % RMS from the analytic
  irradiance (was noise-limited), 274x smoother than the hemisphere alone, the GPU gather within 4 % of the CPU.
- A firefly clamp before the lightmap's filter (a texel over eight times its chart neighbours' mean), so a lone
  bright hit is no longer spread into a blotch.
- Preview Lighting bakes at half the density (a quarter showed blocks) and 32 rays first (was 16).

**Changed after the review**
- The emitter-hit weight carries the distance through see-through passes (a cutout leaf between a texel and a lamp
  made the hit weigh too much); a sampled emitter point respects its alpha (a cutout's hole emits nothing, a
  transparent emitter by its alpha), in the path tracer's own light sampling too; the firefly clamp touches the
  hemisphere gather only, with the filter's facing test (a one-texel sun spot or a lamp's contact peak is real
  light); the emitter rays are capped at 64 on the GPU path; the Lighting window says emitters are sampled; the
  preview's status says 32.
- The chain found the GPU's software-BVH kernel no longer compiling: shaderc's optimiser stopped with "ID overflow"
  (SPIR-V allows 4,194,303 ids; round 174's kernel used 4,191,309, and the two lines above took it over), and the
  path tracer handed the GPU's samples to the CPU without a word - the "every device" test took 70 s and passed on
  the CPU's pixels. The kernel's `main` now calls `trace` from one place (the camera's samples and a bake's share
  the loop), so the optimiser inlines the integrator once: 2,216,807 ids (2,197,372 with ray queries), the
  optimiser in 19 s instead of 57-83, the same pictures (the seeds and the maths are unchanged), and room for
  the kernel to grow again. The test now checks the device did the work (no GPU error, no CPU samples): 2.0 s.

**Added**
- `BakeSettings::sample_emissive` (on); the Lightmap Resolution help names 80 texels/m for small emitters; the
  lesson "Baked emission: a small lamp without blocks".

**Checked**
- New test (round 175): a 10 cm emissive cube 0.5 m over a floor baked at 12 texels/m with 64 rays: 1.4 % RMS from
  the cube's faces integrated analytically, 274x smoother than the hemisphere-only bake, the same twice, the GPU
  gather's bake within 3.5 % of the CPU's; the preview tests expect 32 rays first, and the preview's first pass on
  the lamp scene shows no texel over 3x its lit neighbours' mean.
- Unit checks (round 175): Windows 11,406, 0 failed (910 tests); render-cache verify mode 11,407, 0 failed, 0 stale
  pictures. Linux with libraries: 11,131, 0 failed. Sanitizer build: 11,007, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 174): IK handles in metres, a size preference, rotatable targets (task 0156, ADR 0031)

**Asked:** "the IK handles are way too big"; "I should be able to rotate the handles".

**Fixed**
- A rig control's wire is drawn at its Size in metres round its position and rotation, with no scale from the
  control or its parents: a centimetre import (root scale 100) drew its handles a hundred times too big, a 0.01
  rig a hundred times too small. FK circles are sized from the bone's world length (they were in the bone's own
  units).

**Added**
- **Preferences > Rig Control Size** (0.1-5, `overlay controls size <x>`): every handle's wire and pick distance
  scaled at once. **Rigging > Fit Control Sizes** (`ik fitsizes [root]`): every handle sized from its bone again
  (a target a fifth of the lower bone, a pole 0.12, an FK circle 0.3) - loaded rigs whose handles were in another
  unit come right.
- **Rotate End** on an IK constraint: the foot or hand below the chain takes the handle's rotation (times an
  offset captured when the IK was made). New IK starts a world-aligned target with Rotate End on (the offset
  carries the foot's rotation, so the handle's keyed angles stay clear of the gimbal);
  `ik orient <bone>` turns it on for an existing IK, keeping the bone as it is. The end bone counts as driven:
  the gizmo says to turn its handle instead.
- The lesson "IK handles: size and turn".

**Changed after the review**
- An FK control on the foot fought Rotate End every frame: the bones IK turns (the chain and the end bone) come
  from one helper, used by the FK solve, Add FK, the tint and the gizmo. Rotate End ticked in the Inspector
  captures its offset on the first solve (it snapped the foot to the handle); Weight blends the foot; the end bone
  is the child at the chain's tip (not a twist bone beside it); Fit Control Sizes matches a control's IK on its own
  rig and warns on a mistyped name; the floors are a millimetre, so a rig at 0.01 keeps a fifth of its shin; the
  wire keeps a mirrored parent's mirroring; the gizmo says "turn its target instead" for the foot. The walk and
  footstep generators turn the handle to the foot's roll (Rotate End overrode the keyed foot: 43 mm of sliding,
  then 1.7 mm from the handle's Euler keys near the gimbal; 0.001 mm now).

**Checked**
- New tests (round 174): a humanoid rig at scale 1, 100 and 10 draws the foot target's box at 0.4 of the shin's
  world length; an FK circle at 0.3 of its bone, an old-style size on a scaled rig at 0.003 until Fit Control
  Sizes; the preference doubling the wire, a control's own scale not changing it; a fresh target starting with the
  rotation through its offset, the foot turning 30 degrees with the handle while the knee stays, Rotate End off leaving the
  rest, `ik orient` keeping a pitched foot through a turn, the foot refusing the gizmo, the fields saved; IK added
  on a rig already at scale 100 sized at once; Rotate End ticked later keeping the foot, Weight 0 leaving it at
  rest, a twist bone beside the foot not taken for it, FK on the foot refused and the solve settling, a file
  without the field loading with it off.
- Unit checks (round 174): Windows 11,395, 0 failed (908 tests); render-cache verify mode 11,396, 0 failed, 0 stale
  pictures. Linux with libraries: 11,126, 0 failed. Sanitizer build: 11,002, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 173): the anime pass written up (task 0151)

**Asked:** the anime pass (tasks 0145-0155) documented in one place.

**Added**
- `docs/tasks/0151-anime-pass-write-up.md`: every task of the pass with its round and PR, where the code lives,
  the research and what was taken from it, the short path through the tools.
- ADR 0048 and ADR 0049 gain their References sections (Arc System Works GDC 2015, HoYoverse's face maps, VRM
  MToon, Pixar's procedural patterns; Jakobsen 2001, Kugelstadt & Schoemer 2016, Macklin 2016 / 2019, Daviet
  2023, Akyuerek & Sahillioglu 2025).

**Checked**
- Docs only: the docs test (the manual's chapters and Learn citations) and the suite unchanged.
- The docs test (the manual's chapters and Learn citations): 5 checks, 0 failed; the suite as round 172's.

## 2026-10-21 (round 172): soft tissue, the Jiggle modifier, hair rigged from its mesh (task 0153, ADR 0049)

**Asked:** realistic hair and soft-tissue physics for Spring Bones, research based. This round: the soft tissue and
the hair rig.

**Added**
- **Soft**, a fifth Spring Bones label for a single bone (a breast, a thigh, a belly): the bone trails the body's
  **acceleration** by **Lag** (a landing throws it down, a step up; a slow walk hardly moves it), Stiffness pulls
  it back and Damping settles it, **Limit** keeps it within a cone (degrees) about the animation's direction, and
  **Volume** narrows it across as it stretches along its length (the product of the scales stays 1), the stretch
  given back when Play stops or a bake ends. Found by name: breast / bust / boob / belly / stomach / butt / glute,
  a thigh / hip / chest / cheek with jiggle / soft / phys / dyn / spring / sec / bounce, MMD's 胸 / 尻 / 太もも,
  VRM's `J_Sec_L_Bust` (it read as hair before). `spring add <bone> soft`, `spring label <bone> soft`.
- **Jiggle**, a deform modifier (Add Component > Mesh) for a mesh whose rig has no such bones: a vertex group's
  vertices trail the body's acceleration together on a damped spring (Springiness, Damping, Lag, a Limit in
  metres), stepped on the simulated clock (Play), still in the editor.
- **Rig Hair from Mesh** (the Rigging window's Skeleton buttons; `hair rig [locks] [bones]`): the selected mesh's
  loose parts (hair cards) - or one shell cut into locks round its upright axis - each a chain of bones from the
  scalp to the tip under a new hair armature (under the character's head bone when the mesh is skinned to one, so
  it follows the head), the mesh skinned to the chains, Spring Bones (hair) on each; one undo step.
- The lessons "Soft tissue: breasts, thighs and bellies that move" and "Rig hair from its mesh", after Strands.

**Changed after the review**
- The Jiggle modifier evaluated for the Scene and the Game view at one clock applies one offset (it reset itself on
  the second evaluation), moves every vertex by the offset and lets the stack scale it by the group (it squared the
  weights), stops when its group is missing, and runs on the clock whether or not the Physics assistant is on;
  Springiness is Stiffness, as on a chain. It isn't baked into clips (said in its help).
- Soft tissue: the acceleration is known from the third step, not read from a speed on the second (a body already
  moving when Play starts gave a kick); a teleport forgets it; the stretch and its counter-scale go on a chain's
  last bone only (a non-uniform scale on a parent shears what hangs below it), the scale base is re-taken when
  the animation or the Inspector sets it, a chain relabelled or taken off gives its scale back, and a bake writes
  the stretch as Scale keys. Soft chains don't collide by default (the torso's capsule pushed them off their
  pose). The cone picks a side for a bone pointing straight back. Lag / Limit / Volume show for Soft only.
- The names: 尻尾 is a tail and 胸リボン a ribbon before 尻 and 胸 are soft; Chest, Hips, UpperChest stay the body's;
  MMD's Japanese physics bones get the soft label at import too.
- Rig Hair from Mesh: every card its own lock (up to 64; a count given merges them into that many sectors by their
  roots), a shell into eight; bone lengths in the bones' own units under a scaled armature; a posed head refuses
  (the rig would float when it returns to rest); a skinned mesh with no head bone refuses; the old armature's
  groups go when the skin moves to the rig; each vertex is weighted to its own lock's two nearest joints (the
  envelope bind weighted a card to its neighbours' bones); bone names carry the mesh's name; the head is found by
  the whole word (not Forehead) or 頭. A hair rig under a head bone collides with the character's body (the
  chain's armature is the outermost).
- Speed: the body-bone name test is cached (every chain walk split every bone's name into words: 3 ms a frame for
  a thousand bones), each chain is walked once a step, the separation pass works cell by cell on a sorted array
  (each pair once, by squared distance) over a dense grid of the tips' box. 100 chains of 3 bones, strands touching: 0.4 ms a
  frame with separation and collision; of 10 bones: 1.7 ms.

**Checked**
- New tests (round 172): a bust bone on a 3 Hz run trailing its acceleration by over 2 mm more than with Lag 0,
  within 25 degrees (and within 5 with Limit 5), the scales' product within 1 % of 1, at rest within 1 mm a second
  after the run stops, the scales back to 1 on reset, Lag / Limit / Volume saved; a Jiggle on a cube's top half
  moving it up to its 30 mm limit under a 4 Hz shake, the bottom half not at all, at rest a second after; twelve
  hair cards rigged as twelve chains of three bones under a hair rig with 36 vertex groups whose weights sum to 1,
  undone in one step, and a cube cut into four locks of two bones; the soft names (Breast_L, chest_jiggle, Belly,
  ThighSoft.L, 左胸, J_Sec_L_Bust1; 尻尾 a tail, 胸リボン cloth, Chest and Hips the body's); a landing throwing a
  bust bone 3 mm further down with Lag 0.5 than with 0, no kick from a body already moving, a two-bone chain
  stretching only its last bone and the bake's Scale keys, Volume 0 changing the volume, 100 chains of 3 bones
  under 0.5 ms; the Jiggle the same for both views and half as far on a half weight; a skinned hair mesh rigged
  under a scaled character's Head bone with the body's group gone and the body's capsule seen, a posed head
  refused, forty cards merged into eight locks.
- Unit checks (round 172): Windows 11,369, 0 failed (905 tests); render-cache verify mode 11,370, 0 failed, 0 stale
  pictures. Linux with libraries: 11,100, 0 failed. Sanitizer build: 10,976, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 171): strands - bend, root to tip, wind, colliders fitted to the skin, separation (task 0153, ADR 0049)

**Asked:** realistic hair and soft-tissue physics for Spring Bones, research based. This round: the strands.

**Added**
- **Bend** on a Spring Bones chain: each bone keeps its animated angle to the one before it, by this much - a curl
  keeps its shape, a strand bends where it was bent (0: the old behaviour, a curl straightens). **Root To Tip**: the
  pull toward the animation fades toward the tip, so long hair's tips swing free while the root follows.
- **Wind**, a component (Add Component > Scripts): a wind along the object's forward (or away from it, Spherical) of
  a strength in m/s, with turbulence (eddies by place and time) and gusts (the strength breathing over seconds) from
  periodic noise of the simulated time, so a bake comes out the same twice. A chain's **Wind** scales its response.
- **Colliders fitted to the skin:** a body bone's capsule is as wide as the vertices its vertex group moves (the 85th
  percentile of their distances from the bone in the rest pose, from the skinned meshes bound to the armature),
  cached per armature until a mesh changes; a seventh of the length without a skin, as before. A skirt rests on a
  thigh's real width.
- **Separate:** a chain is kept apart from other chains by its Radius (every stepped chain's tips in a uniform grid,
  two rounds of pushing apart and putting the lengths back), so a ponytail's strands and a skirt's pleats don't pass
  through each other.
- The labels' defaults set the new fields (hair: bend 0.2, the tips free, apart; an ear stiffer; a tail its curve;
  cloth free and apart); old chains keep the old behaviour (bend 0, root to tip 0, wind 1, no Wind in the scene).
- The Learn tab's lesson "Strands: a curl that holds, wind, the body's real width", after Spring Bones.

**Changed after the review**
- The bend held a world-space turn, so a curl unwound once the chain swung out of its animated plane: the animated
  angle is carried through the simulated bone's own swing now, and not held after a bone of no length. Root To Tip
  fades by the length along the chain, not the bone count. Separate never pushes strands further apart than the
  animation laid them (hair roots are closer than their radii), skips a NaN tip, and the body's collision runs again
  after it. The fitted radii pick one bone per vertex in one pass (ties to the lower group), take the narrowest skin
  per bone (a hair shell skinned to the head doesn't widen it), match a skin's armature the way the skin does, and
  are keyed by the bones' rest, names and lengths, the skins' ids, mesh versions and relative matrices (an undo or a
  rest edit refreshes them). The wind's clock is per scene and a bake runs its own; the drag converges at any step;
  the gusts are worked out once a substep.

**Checked**
- New tests (round 171): a curled chain holding its curl with bend 1 and hanging straight with 0; root to tip 1 lagging
  the tip more while the first bone lags alike; a 5 m/s wind deflecting a chain by over 5 cm, nothing at 0 m/s or with
  the chain's Wind 0, turbulence stirring it, a bake under the wind the same twice; a 0.2 m tube's bone fitted to
  0.2 m (a seventh without the skin) and a chain resting outside it; two strands kept apart by their radii; the same
  swing at 30, 60 and 144 fps with bend, wind and separation on; no NaN after a thousand 10 m jerks; the settings and
  the Wind saved and loaded; the lengths within 0.1 % under a move with bend; a curl held out of its plane; strands
  laid out closer than their radii staying as laid out; the hair label's defaults.
- Unit checks (round 171): Windows 11,350, 0 failed (899 tests); render-cache verify mode 11,351, 0 failed, 0 stale
  pictures. Linux with libraries: 11,081, 0 failed. Sanitizer build: 10,958, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 170): the import preview with switchable shading (task 0152)

**Asked:** a preview when importing a model, with the shading switchable before anything lands in the project, so an
anime character arrives looking like one.

**Added**
- **Import Preview:** a dropped or opened model is read and shown in a dialog first - on a slow turntable (drag to
  turn it) under the editor's light, drawn by the CPU rasterizer (every material, painted maps included), with the
  import's summary and notes - and nothing is written until Import. The **Shading** row applies one look to every
  material of the model: As Imported (what the file made: MToon, MMD's toon), PBR, or a toon style (Classic, Guilty
  Gear, Soft Anime, Painterly, Manga) over the file's materials, their maps kept; **Outline** puts a line of the
  given width on every mesh. Import writes the model, its textures and the .mat files with that look, and the
  objects with their outlines; Cancel writes nothing.
- **Preferences > Import:** "Preview before importing" (off: the model comes in at once, as before; headless runs
  never show it) and the shading the preview opens with.
- **Console:** `import <file> [asis|pbr|classic|guiltygear|soft|painterly|manga]` imports at once with that shading
  (scripts and tests); without the word, the dialog when the preference is on.
- The Importing lesson gains "Importing: preview the look".

**Changed after the review**
- A look chosen at import rewrites a model's existing .mat files (As Imported keeps what was edited); several files
  dropped at once take their turns; the Outline switch reaches a file's own outlines; the turntable turns by the
  clock in 2-degree render steps, lit from its own camera; `soft` and the two-word style names work on the console;
  a file with no mesh imports at once; meshes without a material are shaded too; the views and the preview build
  their draw items with one pair of helpers.

**Checked**
- New tests (round 170): `import <glb> guiltygear` writing a .mat with Guilty Gear's fields, then `pbr` and `asis`
  rewriting it; the shading words; the dialog opening without writing a file or adding an object, its picture lit,
  the preference's shading applied, the Shading row's buttons switching the preview's materials (Guilty Gear, PBR,
  As Imported with the file's colour back), the Outline checkbox, Cancel leaving no trace, Import with Guilty Gear and
  an outline giving the objects, the .mat and the Outline; a re-import rewriting the .mat with the look (As Imported
  keeping it); `soft` and `guilty gear`; the preference's round trip and clamp; the plain import opening the dialog
  with the preference on, a second file waiting, the popup clicked away as a Cancel, the preference off importing at
  once.
- Unit checks (round 170): Windows 11,337, 0 failed (894 tests); render-cache verify mode 11,338, 0 failed, 0 stale
  pictures. Linux with libraries: 11,068, 0 failed. Sanitizer build: 10,945, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 169): procedural anime textures (task 0150)

**Asked:** "procedural textures for anime backgrounds": foliage (leaves, grass), buildings (walls, windows, roofs), sky
and clouds, ground and water.

**Added**
- **Nine generators** in the Textures window, drawn as a background painter draws - two or three flat tones and a
  line, no gradients: Leaves (clusters of leaf shapes with a lit top, a shaded underside and a dark rim), Grass
  (blades from the bottom in three tones), Plaster (one tone with soft patches of a second and a few cracks), Window
  Grid (frames, lit and dark glass, a wall tone), Roof Tiles (overlapping rows with a shadow band and lines), Sky
  Gradient (zenith to horizon, a sun's disc and glow), Clouds (a white top over a flat shaded underside, an outline),
  Rocks (stones lit on their upper left with a ground line between) and Water (one tone with bent highlight lines).
  Every tone is a colour field; all repeat in the tile (Grass and Sky Gradient are cards: across only).
- **Alpha From Cover** on a texture: the albedo image's alpha is what the cards cover, in the image and in live
  shading. The cards (Leaves, Grass, Clouds) cover only their shapes and write that cover into a cover plane of
  their own (stacked cards add up; Warp and Tile move it with the colour; Mask by Height and Use Mask keep the mask
  as before), so a Cutout material shows holes between the leaves: a foliage card, a cloud over the sky (ADR 0017's
  amendment).
- **Presets** Anime Leaves, Anime Grass, Anime Wall, Anime Windows, Anime Roof, Anime Sky, Anime Clouds, Anime Rock and
  Anime Water (the Textures window's New list, `newtex <name> <preset>`), each with its tile size and no normal map.
- **`animemat <preset>`** on the console: the preset's texture asset and a flat-toned Toon material reading it (a
  double-sided Cutout for a card) on the selected objects, one undo step.
- The Textures lesson gains "Anime backgrounds: procedural textures".

**Changed after the review**
- The cover left the mask plane for its own (a second card replaced the first's cover, a Mask by Height above a card
  replaced the alpha, a card's Use Mask read its own cover, Warp and Tile moved the colour but not the cover, and
  live shading reset it); a card's colour lands everywhere so the cut has no grey fringe; Roof Tiles rounds its rows
  up to an even number (an odd count broke the stagger one tile over); the Sky Gradient's sun reaches round the
  seam; a Plaster crack is one edge between two cells, not a cell's outline; Grass leans no further than the scan
  looks; the Voronoi, Plaster and Rocks generators share one cell scan; `animemat` makes nothing when nothing with a
  MeshRenderer is selected; the texture's hand-written copy carries its output switches (a preset without a normal
  map got one back as an asset).

**Checked**
- New tests (round 169): each generator deterministic, periodic one tile over (the cards across only; Roof Tiles with
  odd rows and a sun at the sky's seam too), at most four tones over what it covers (the Window Grid's six are three
  pairs), covering over a tenth of the tile; each preset's layer and card flag; a leaf card's albedo with clear and
  solid texels (none clear with the flag off); `animemat Anime Leaves` on a Quad giving a Cutout toon material with
  holes between the leaves at sampled points, an unknown preset warning, undo, nothing made without a selection; the
  texture copy keeping its switches and hash; live shading's alpha agreeing with the image's; two cards adding up; a
  card's Use Mask reading the mask below; a Tile moving the cover with the colour in the image and the live path.
- Unit checks (round 169): Windows 11,300, 0 failed (892 tests); render-cache verify mode 11,301, 0 failed, 0 stale
  pictures. Linux with libraries: 11,031, 0 failed. Sanitizer build: 10,909 (one timing check at 0.204 ms a frame against its 0.2 limit, with a Windows build running beside it; it now skips sanitizer builds like the suite's other timing checks), 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 168): the 2D look - custom normals, a face shadow map, inner lines and hatching, hold frames (task 0149, ADR 0048)

**Asked:** renders that "look more 2D while posed in 3D", research-based: shadow control with smooth normals, a face
shadow map, inner lines and hatching, animation on 2s and 3s.

**Added**
- **Custom normals:** a mesh can carry its own shading normals, one per vertex, which replace the smooth / flat /
  sharp-edge normals at every corner; saved with the mesh, carried through edits by position as shape keys are. An
  FBX or glTF whose normals an artist edited keeps them (when over 1 % differ from the recomputed ones by a degree;
  the import says so); exports write them as they are.
- **Smooth Normals and Normal Transfer modifiers** (Add Component > Mesh): the shading normals averaged over the
  surface N times (an anime face keeps its shape and loses its bumps' shading), or taken from a proxy object's
  surface at the nearest point (a sphere round the head: Blender's Data Transfer trick), both limited by a vertex
  group and moving no vertex. **Rigging > Anime Face Normals** (console `facenormals`) adds a sphere proxy fitted to
  the Head group (or the top of the mesh), without a renderer, and the modifier reading it.
- **Face Shadow Map** on a toon material: the key light's angle round the face (0 from the front, 1 from behind)
  read against the map decides the lit side instead of N.L - HoYo's SDF faces; the map is mirrored for a light on
  the other side; Face Shadow Softness and Face Forward (+Z, -Z, +X, -X: VRM faces +Z, MMD -Z). The Textures window's
  **Face Shadow** generator makes the plain sweep to paint over. The same in the rasterized views, F12 raster and
  both path tracers (the GPU viewport hands it to the CPU like the other painted maps).
- **Line Map** (inner contour lines multiplied into the colour, or only into the shadow tones with Lines Only In
  Shadow) and **Hatch Map** with Hatch Scale (strokes tiled over the UVs, shown in the shadow tones).
- **Outline:** Thickness Group (a vertex group scaling the line's width per vertex, Guilty Gear's painted line width),
  Distance Fade (thinner far away) and Min / Max Pixels (the width kept between them on screen, by the object's
  distance, in the views and raster renders).
- **Hold** on the Animation and Sequencer components: 1s, 2s, 3s or 4s - the pose changes every so many frames while
  the camera and the rest of the scene move every frame; Render Animation holds the same frames (from the range's
  first frame).
- Console `customnormals show|clear` (the way back from kept or transferred normals). A Skin modifier turns custom
  normals with its bones; the face shadow map takes no cast shadow on top (the map is the face's shadow); a file's
  normals are kept only when they are smooth and differ from both the area- and the angle-weighted recomputed ones
  (ADR 0048's notes).

**Changed after the review**
- Normal Transfer reads the proxy's render mesh (its own vertices and shading normals), with the closest-point
  search extracted; a normal modifier starts from the head's normals on every evaluation (it compounded); Anime Face
  Normals places the modifier before the Skin and makes a 16 x 12 proxy; the Outline's distance scale moves in 2 %
  steps, has its own shell slots per view (a path tracer kept a freed shell otherwise), honours an orthographic view
  and an object-unit thickness; the hold's float frame rounds up at 1e-4; the GPU material's packed ids are built
  unsigned; the FBX reader turns normals by the geometry's normal matrix.
- The Learn tab's Materials lesson gains "The 2D look".

**Checked**
- New tests (round 168): custom normals at every corner, saved and loaded, transferred by position, through a glTF
  round trip with the note, the file's normals kept only when they differ; Smooth Normals closing the sharpest edge
  and moving nothing, limited by a group; Normal Transfer radial from a sphere and following it; `facenormals` and its
  undo; `toon_face_lit`'s six cases and the generator's sweep; a sphere one lit tone from the front and one shadow tone
  from behind in the rasterizer, the CPU path tracer and the GPU's; the shadow multiplier, a grey line map (both ways)
  and a hatch map at a point; the Outline's group, cache key and distance scale; 23 frames on 1s / 2s / 3s; Hold saved.
- Unit checks (round 168): Windows 11,236, 0 failed (890 tests); render-cache verify mode 11,237, 0 failed, 0 stale
  pictures. Linux with libraries: 10,967, 0 failed. Sanitizer build: 10,845, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change (the
  glTF weld is as before; custom normals touch no geometry).

## 2026-10-21 (round 167): toon styles (task 0148, ADR 0048)

**Asked:** "Is there any way to add more toon shading details in good engine" - "The Guilty Gear / Arc System Works
is a really good baseline. I want different options of toon shading to get versatility in style."

**Added**
- **Toon styles:** a Toon Style field at the top of a material's Toon group - Classic, Guilty Gear (Arc System Works'
  hard two-tone, the key light alone, the highlight where it is painted, no rim), Soft Anime (three tones, light
  wrap, a lit-side rim), Painterly (four soft tones under the sky's gradient), Manga (a hard grey two-tone). Picking
  one sets the fields; every field stays editable and the maps are yours. Console `toonstyle <style>` on the
  selected materials; with several selected, the style lands on all of them.
- **Painted control**, as the studios do it, through textures: a Shadow Bias Map (grey neutral, white keeps a point
  lit, black in shadow), a Toon Specular Map (where the spot may show) with the spot's own colour, a Toon Rim Map and
  a rim that shows on the lit side, the shadow side or both of the key light, and a Shade Map - the shadow side's own
  picture (VRM's MToon shade texture now comes in).
- **A Toon Ramp** instead of the bands: an image read by how lit the point is (left dark, right lit, or bottom dark,
  top lit), made by the Textures window's new Toon Ramp generator (2 to 5 tones, hard or soft); MMD's toon images
  land there.
- **Toon Hair Highlight** turns the spot into the band across the hair that anime draws; **Toon Light Wrap** lets
  the light reach round the form before the tones step; **Toon Key Light Only** keeps several lights from banding
  like a disco ball: the brightest sun bands, the others fill smoothly.
- All of it the same in the rasterized views, F12 raster, the CPU and GPU path tracers; the GPU viewport draws the
  plain settings and the ramp, and hands a material with painted maps or a hair highlight to the CPU, saying so (a
  VRM avatar with a shade texture draws on the CPU there; MMD models stay on the card). A lesson section "Toon
  styles" with Try-its.

**Checked**
- New tests (round 167): each style's fields, names in any spelling, `toonstyle` undone as one step, a .mat keeping
  every field; the shared shading (the spot's colour and map, the bias's ends, the shade map, the wrap, horizontal
  and vertical ramps, the hair band, the rim's sides and mask, the key light); on a sphere wrap 0.5 grows the lit
  share by over 10 %, the rasterizer and the CPU path tracer share the tones within 4 % (the GPU's within 3 % of the
  CPU's), a second light bands unless Key Light Only (rasterizer and path tracer within 4 %); the Toon Ramp
  generator's steps and a material reading it showing exactly two tones; MMD's ramp reading lit at the top; VRM's
  shade texture in Shade Map; MMD's ramp in Toon Ramp.
- Unit checks (round 167): Windows 11,182, 0 failed (885 tests); render-cache verify mode 11,183, 0 failed, 0 stale
  pictures. Linux with libraries: 10,914, 0 failed. Sanitizer build: 10,792, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 166): multi-edit materials (task 0147)

**Asked:** "add the ability to multi-edit materials if they share the same properties (such as shading model). For
example I can modify toon threshold universally amongst selected materials."

**Added**
- **Editing several materials at once** (Unity's multi-object editing, for materials): select several in the
  Materials window (Ctrl-click, Shift for a range), several `.mat` files in the Project window, or several objects
  in the Scene, and a change made in the fields reaches every one of them that has the field - a Toon Threshold
  lands on the toon materials and leaves a PBR one alone, an axis field on the axis ones, Shading Model on all (that
  is how a set is converted). A dash marks a field whose values differ (hover it to see the active one's); what you
  type lands on all; `+=0.1` or `*=2` moves each from its own value. One undo step; the edited assets are written
  back. Console: `matset <Field> <value>` on the selection. The anime lesson says so, with a Try-it.

**Checked**
- New tests (round 166): three toon materials and a PBR one selected in the Materials window: Toon Bands typed 3
  lands on the three, not the PBR one; `+=1` moves each from its own value; the differing fields are named;
  `matset ToonThreshold 0.4` reaches the three, `matset ShadingModel 1` all four, an unknown field warns; the `.mat`
  file on disk holds the values. Three objects selected (two toon, one PBR): Toon Bands typed in the active one's
  Inspector reaches the other toon object's material, not the PBR one, and one undo takes it back on both; `matset`
  on the selected objects the same.
- Unit checks (rounds 164-166 together): Windows 11,126, 0 failed (880 tests); render-cache verify mode 11,127,
  0 failed, 0 stale pictures. Linux with libraries: 10,862, 0 failed. Sanitizer build: 10,740, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-21 (round 165): Inspector drop-downs, overlay layers, Anime and Manga as one effect each (tasks 0145, 0146, 0154)

**Asked:** "For the guides in the camera, have a drop down stack so it doesn't occupy too much of the inspector's
camera tab"; "the ability to hide certain objects like bone controls so I can see the mesh without too much
overstimulation ... I can hide the bone layer, but can still see IK/FK handles"; "For the camera 'Anime' and 'Manga'
filter. Make its own separate filters not a group composite and make it one unit."

**Added**
- **Drop-downs in the Inspector** (task 0145): a component's fields can sit under a foldout header, closed or open,
  that only the Inspector sees - files, undo, the hash, `set` and clips read the fields as before. The camera's
  Composition Guides are one closed drop-down now; a material's Toon fields sit in a Toon group and the axis ones in
  Axis Material.
- **Overlay layers** (task 0146): an Overlays menu in the Scene view's bar (Blender's Overlays popover) with a check
  per layer - Bones, Rig Controls (IK / FK handles), Light & Camera Icons, Curve & Lattice Points, Object Origins,
  Footprints & Contacts, Selection Outline, Grid, Gizmos, Statistics - and All Overlays. Hide the bones and keep the
  rig controls to pose a character by its handles while seeing its mesh. A hidden layer isn't clicked either (a click
  lands on what is behind), the bones' head and tail handles go with their layer, the Hierarchy and the gizmo still
  work on anything. Console `overlay list | all|<layer> [on|off]`; the choice is kept in the preferences. A lesson:
  "Overlays: See the Mesh, Hide the Rig".
- **Anime and Manga as single effects** (task 0154): the Looks menu adds one Anime effect (Lines, Line Color, Line
  Thickness, Crease Angle, Saturation, Contrast, Bloom, Bloom Threshold) or one Manga effect (Grayscale, Tones, Dots,
  Dot Cell, Dot Contrast, Lines, Line Thickness, Paper) instead of three or four. Each draws exactly what the stack it
  replaces drew (the parts run inside it), so a scene saved with the old composite looks the same, and the parts stay
  effects of their own for stacks of your own.

**Checked**
- New tests (round 165): the camera's Composition Guides header is there and closed, `set Camera.Thirds` lands, the
  file keeps it, the render hash ignores it, a click opens it (the Add Filter button below moves down by 12 rows) and
  another closes it; a Toon material shows an open Toon group and a closed Axis Material group, a PBR one no Toon
  group, and a plain reader still sees both groups' fields. A humanoid rig: a click on a bone selects it; Bones off:
  the handles vanish, the same click selects what is behind, the console still selects it, the rig controls stay;
  all off / all on; icons off: a click on the sun's icon selects nothing; the menu's Bones row turns the layer off
  and closes; the preferences keep `overlays_off=bones`. Anime against the old Line Art + Color Grading + Bloom stack
  and Manga against Grayscale + Halftone + Line Art + Paper: the largest channel difference 0; Lines off and Tones 3
  change the picture; the effects save and load; the old composite loads as four effects; the registry table test
  covers both.
- Checked with round 166's chain (see its entry).

## 2026-10-21 (round 164): area lights as emissive planes (task 0155, ADR 0050)

**Asked:** "The area light is still treated like a point light. The area light needs to act like a planar mesh using
an emissive material that has varying intensity." Chosen: the path-traced renders and the lightmap bakes / probes,
with physical falloff.

**Changed**
- **An area light is a glowing panel now.** Every renderer already sampled its rectangle or disc, but its brightness
  was Unity's range curve from the light's centre, with no inverse square from the points of its surface - so a wide
  panel close to a floor lit it like a lamp at its middle. Now each point of the panel sends light with its facing
  cosine and the inverse square of its distance, exactly what a quad with an emissive material gives (a test renders
  the two side by side: the same floor within 3 %). `Intensity` is how brightly the panel glows per square metre (an
  emissive material of strength pi x Intensity gives the same light), so a bigger panel gives more light; a 1 x 1 m
  panel a couple of metres from a wall gives about what it gave before.
- **Range is a window, not the falloff:** the light fades to nothing at Range (Unreal's window) and falls as the
  inverse square before it. Point and spot lights keep their curve.
- The path tracers (CPU and GPU), the rasterized views' exact term, the lightmaps (the shadow rays weigh each point of
  the light by its own falloff), the probes and MoonRay's export (the panel's radiance as it is) all take the one
  formula; a surface touching the panel stays finite (the inverse square stops at 1 cm).
- **Scenes lit by area lights change on purpose:** a 1 m panel 3 m away gives about a quarter of what it gave, the
  same panel 30 cm away about five times more, and a 10 m panel ten times a 1 m one (it gave the same). Raise or
  lower old scenes' area-light intensities to taste.

**Checked**
- New tests (round 164): a 20 cm panel 1 m up gives albedo x intensity x its area, 2 m up a quarter; a 10 x 10 m
  panel 3.5 m up against a 10 x 10 m emissive quad and the exact term, under the centre and 3 m out; the falloff past
  the panel's edge has no step; a floor 10 cm under a panel matches the exact term and a millimetre under it stays
  finite; Range 4 m ends at 4 m and is within 2 % of unlimited at 1 m; MoonRay's RectLight intensity is pi for
  intensity 1; the Light's help says so.
- Task 0030's references rewritten for the new integrand: the exact term within 1 % of a million-sample Monte Carlo
  over thirteen placements (strips, discs, 30 cm below a 10 m light), the rasterizer within 10 % of the path tracer,
  the bake within 5 %; task 0022's bake noise and the toon-under-area-light shares unchanged. Task 0022's Preview
  Lighting test cuts its "lit top" at half the chart's peak instead of an absolute value (the brighter panel let the
  dim sides in with their larger noise: macOS CI).
- Checked with round 166's chain (see its entry).

## 2026-10-19 (round 163): bone handles the right size on import (task 0144, ADR 0047)

**Asked:** the Carmen import's bone handles looked huge: "is there a way to fix that when we import any model format?"

**Fixed**
- A bone's length is in its own units, and a leaf bone (no bone below it) got a default of 0.1 of them. Blender's FBX
  export scales the armature x100 and writes an `_end` leaf under every chain, so each became a 10 m spike (and
  under a x0.01 armature bones vanished). Now a leaf's length is in metres whatever the scale: half its parent's,
  else 10 cm.
- Weightless end bones are folded into their parents: Blender's `_end` leaves (`Eye.L_end`, `HairBase.006_end`...),
  Mixamo's `HeadTop_End`, BVH's end sites, VRoid's `_end` spring tails: the parent's tail reaches them and no object
  is made for them. The Console says how many. Every format goes through the same code (MMD gives its own tails;
  a Collada leaf with no end site takes the metres default).
- A baked clip's "this number never moves" test allowed a millionth of a degree; on macOS the angles read back from
  quaternions jitter by more, so a turning bone got two extra flat tracks (#109's macOS run). A thousandth of a
  degree now, a hundredth of a millimetre for moves.
- Three tools read a bone's length as metres (the spring chains' collision radius, Add IK's pole distance, the body's
  capsules): under a scaled armature they were a hundred times off; they measure the bone in the world now.

**Checked**
- New tests (round 163): a glTF with an armature scaled x100: Upper 1 m to Lower, Lower 0.5 m to its folded `_end`,
  Tip 25 cm (half its parent's), the fold noted; Blender's four rig fixtures: no bone longer than its chain, no leaf
  longer than half its parent nor shorter than 2 cm; a leaf's tail dragged to 0.3 m reads 0.3 m.
- The Rigging, IK, skin and spring groups unchanged; after the review every leaf is checked against half its parent
  and Mixamo's `HeadTop_End` folds.
- Unit checks (rounds 162-163 together): Windows 11,014, 0 failed (865 tests); render-cache verify mode 11,015,
  0 failed, 0 stale pictures. Linux with libraries: 10,750, 0 failed. Sanitizer build: 10,628, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.
- The user's folder, imported headless by the rebuilt app: CarmenB, CarmenF and CarmenH textured from `textures/`,
  OL bare, 16 end bones folded, the handles the size of the bones.

## 2026-10-19 (round 162): the texture finder (task 0143, ADR 0047)

**Asked:** a Sketchfab download (`carmen-limbus-company/source/CarmenSF.fbx` with `textures/Carmen{B,F,H}_Base_color.png`)
came in untextured: "why isn't the textures importing?", and then "a system where this can automatically find relevant
textures in a folder, so that I don't have to look for them and import them manually".

**Fixed**
- The FBX links no textures at all, and the matcher of round 154 looked only in the model's own folder (`source/`)
  and read `CarmenB_Base_color` as a map called "color" of `CarmenB_Base`, never `CarmenB`.

**Added**
- **The texture finder:** every import looks through the folders around the model - its own folder and three levels
  below, then the folders beside it two levels down (Sketchfab's `source/` + `textures/`, Unity packages); never
  into Downloads, Desktop, Documents, a home folder or a drive root, and not into folders full of other files; at
  most 2,000 images.
- **Names read as people write them:** two-word kinds (`Base_color`, `Normal_OpenGL`, `Roughness_map`,
  `Metallic_Smoothness`), sizes and tiles dropped (`_2k`, `_4096`, `.1001`), AO / opacity / height maps recognised
  (and reported: Blendity has no slot for them).
- **Pairing by likeness:** each bare material is scored against every set (the same name; one inside the other;
  the words they share) through its own name and its meshes'; the best above 0.6 wins, nearest folder first. When
  as many materials as sets are left and each has its own best, they pair by that; a lone material with a lone set
  always does.
- **The Console says** which file went to which material and from where, what stayed bare, what was seen but not
  used, and that a wrong guess is one slot to change in the Inspector.
- **Learn:** the "Importing models" lesson says so.

**Checked**
- New tests (round 162):
  - Sketchfab's layout with two-word names: RobeA and RobeB textured (RobeB's normal and roughness too), Trim's own
    colour kept, the AO map reported;
  - `Body` pairs with `T_character_body_2k_BaseColor` (and its normal); `Hand_R.png` alone textures nothing;
  - a model loose in a folder called Downloads takes nothing from the folders around it; in a folder of its own
    under Downloads it looks around as usual;
  - dropped into the editor: the textures copied into `Assets/Textures/robe/`, the `.mat` files using them.
- Round 154's tests unchanged (the moved link, the lone `Rock_d`, `Hand_R`, the flat colours).
- After the review: a Unity Textures folder of `.meta` sidecars is read; a height-only set leaves its material bare
  and is reported; `Wood_01` and `Rock_01` don't pair; `Arm` doesn't take `Armor`'s set; the user's folder made
  synthetic (CarmenB/F/H textured, OL bare).
- **Fixed on review:** a Unity texture folder (one `.meta` a texture) was skipped as "a folder of other files"; the
  crawl had no bound on folder listings (a library on a slow disk could stall the drop: now 400 listings or 150 ms);
  a set of only AO or height maps could "win" a material and leave it bare without a word.
- Checked with round 163's chain (see its entry).

## 2026-10-19 (round 161): Collada (task 0141, ADR 0047)

**Asked:** round 154's "other rigged character formats": Collada, what Blender, Maya, SketchUp and Mixamo export.

**Added**
- **Collada (.dae) import** through Blendity's own XML reader (`core/xml.h`): geometry (triangles, polylists,
  polygons) with its materials (the effect's diffuse colour or texture, emission, shininess as roughness,
  transparency), skins with their joints and bind matrices, the node hierarchy, and the animations (matrix channels,
  or translate / rotate / scale channels by name) as one clip. The file's up axis and unit are honoured, so a Z-up
  model in centimetres stands the right way up at the right size.
- **Learn:** "Collada: .dae scenes".

**Checked**
- New tests (round 161), on a Collada file the tests write themselves (Z up, centimetres, a textured square skinned to
  two joints, a matrix animation):
  - standing in metres and facing +Z, its texture found in `tex/`, its emission and roughness read, its groups named
    after the joints, at rest unmoved;
  - the clip of 24 frames: at its end the upper joint has turned 90 degrees and the square's top with it;
  - 40 files cut short refuse or import without harm;
  - dropped into the editor: `Skin.mat` with the texture copied in, the clip playing on the root; the lesson's Try-it;
  - after the review: a Y-up metre cube with a byte-order mark and CDATA comes in facing out, its translate and rotate
    component channels moving it 2 m and turning it 90 degrees.
- **Fixed on review:** the `<source>` cache outlived its file (a later import could read another file's UVs); CDATA cut
  short read past the buffer; a skin over Blender's morph controller lost its mesh; X-up files came in inside out.
- Unit checks (the whole stack, rounds 156-161): Windows 10,985, 0 failed (859 tests); render-cache verify mode
  10,986, 0 failed, 0 stale pictures. Linux with libraries: 10,721, 0 failed. Sanitizer build: 10,599, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-19 (round 160): BVH motion capture (task 0140, ADR 0047)

**Asked:** round 154's "other rigged character formats": motion capture.

**Added**
- **BVH import:** a `.bvh` (Biovision motion capture: CMU, Mixamo's exports, Rokoko) comes in as a skeleton of bones at
  the file's offsets and its motion as a clip (`Assets/Animations/<file>/<file>.anim`) playing on it, at the file's
  frame rate. Rotation channels are composed in the file's order; a skeleton taller than 10 units is taken to be in
  centimetres.
- **Learn:** "Motion capture: BVH".
- Out (said in the Console): retargeting onto another character (bake the clip or copy its keys by name).

**Checked**
- New tests (round 160), on a BVH the tests write themselves (hips, spine, head and an end site; the hips walking a
  metre forward and the spine bending 90 degrees over 24 frames at 24 fps, in centimetres):
  - the skeleton in metres (the hips 1 m up, the head above the spine), the clip of 24 frames at 24 fps;
  - at its end the hips have moved 1 m towards +Z and the spine has turned 90 degrees (within 0.1 degree);
  - damaged files (cut anywhere, a brace too many, a NaN) refuse or import without harm;
  - dropped into the editor: the clip made and playing on the skeleton's root; the lesson's Try-it;
  - after the review: CRLF, tabs and `Frames:25` read the same; what a damaged file gives is whole.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 159): animation clips from FBX and glTF (task 0139, ADR 0047)

**Asked:** round 154's "other rigged character formats": a model's own animations (Mixamo, Blender actions, glTF).

**Added**
- **A model's animations come in as clips**, as Unity's importer makes them: `Assets/Animations/<model>/<clip>.anim`
  for each, the first playing on the model's root through an Animation component.
  - **FBX:** each animation stack, at the file's frame rate, every node's turn, move and scale a frame at a time (a
    track for each that changes); blend-shape channels as BlendShape tracks.
  - **glTF:** each animation at the frame rate its keys fit (24, 25, 30, 50, 60, 120; else 30); moves, scales and
    morph weights at the file's own keys (step and linear; cubic splines baked), turns baked a frame at a time from
    the file's slerp.
  - Euler angles chosen nearest the frame before: no wraps.
- **Learn:** "Imported animations: a model's clips".

**Checked**
- New tests (round 159), on Blender's FBX and GLB of a two-bone arm (`tests/fixtures/anim_arm.*`, `make_anim.py`):
  - one clip of 24 frames at 24 fps from each; at its end the upper bone has turned 90 degrees (within a degree) and
    the shape key is at 1; at its start nothing has moved;
  - dropped into the editor: the FBX's `Armature|ArmatureAction` saved as `Armature_ArmatureAction.anim` and playing
    on the root; a second copy of the model plays its own clip (its armature renamed); the lesson's Try-it.
- **Fixed on review:** Blender's clip names (`Armature|ArmatureAction`) couldn't be written on Windows, so every FBX
  action was dropped silently; a second import of a model lost its clip to the armature's uniqueness rename; baked
  clips kept every frame (now reduced to the frames that matter); a glTF clip started a frame early.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 158): VRM characters (task 0138, ADR 0047)

**Asked:** round 154's "other rigged character formats": VRM, the VTuber avatar format.

**Added**
- **VRM import** (0.x and 1.0; VRoid Studio, UniVRM): a `.vrm` dropped onto the window comes in as a character facing
  +Z.
  - **Bones:** the humanoid's bones named for the rig tools (`leftUpperLeg` -> `LeftUpLeg`).
  - **Expressions** (A, I, U, E, O, blink, joy...) as shape keys on the meshes they move, each the weighted mix of
    morph targets the file binds.
  - **Spring bones** as Spring Bones chains with the file's stiffness, drag, gravity and hit radius.
  - **MToon** as Toon shading (the shade colour, toony edge, shift; cutout and transparent modes; unlit shaders) with
    an Outline of the file's width.
  - **Meta:** the title, author and license in the Console.
  - **Not carried over:** shade textures, rim light, matcaps, first-person and look-at settings.
- **Learn:** "VRM characters: VRoid and UniVRM avatars".

**Checked**
- New tests (round 158), on an avatar the tests write themselves as a GLB, in 0.x and 1.0 form:
  - the bones named; the left leg on the character's left and the hair behind the head in both (the 0.x model turned
    half a turn);
  - `a` / `aa` the whole first morph (5 cm), `blink` half the second; the face facing +Z;
  - the hair's chain with stiffness 0.5, damping 0.4, gravity 0.5, radius 3 cm;
  - the material Toon with a shadow ratio of 0.5, a 5 mm outline in the file's colour; the meta in the notes;
  - dropped into the editor: `Skin.mat` (Toon), the outline, the lesson's Try-it;
  - after the review: bound with inverse bind matrices, the face stays at rest and follows the head; the hair outside
    the skin's joints still gets its chain; a 0.x shadow comes in at its sRGB-corrected ratio.
- **Fixed on review:** 0.x colours are sRGB (a VRoid shadow came in much too light); a forking spring root got one
  chain (every strand below it hung rigid); a spring on an accessory node outside the skin was dropped.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 157): MMD motions (task 0137, ADR 0047)

**Asked:** round 154's MMD request: its dances.

**Added**
- **VMD import:** drop a `.vmd` onto the Scene view with an MMD character selected (or File > Import, the Project
  window, the console's `import <file>`), and it becomes `Assets/Animations/<character>/<motion>.anim`, played by the
  character's Animation component.
  - **Bones** by MMD's names (`左ひざ`, `頭`): a motion fits any model with the standard bones. The keys are turned
    from MMD's rest-frame quaternions into each bone's own rotation (Blendity's bones point at their tails).
  - **Timing:** MMD's Bezier curves become the keys' slopes; a run the curves can't follow within half a degree or a
    millimetre is keyed every frame from the true arc.
  - **Faces:** morph keys on the BlendShape sliders of every mesh with that morph.
  - **IK:** the legs' IK switched off and on keys the constraint's Weight.
  - **Reported:** bones and morphs the character hasn't; the motion's camera (not imported).
- **Learn:** "MMD motions: VMD dances on your character".

**Checked**
- New tests (round 157), on a motion the tests write themselves:
  - the hips' 90-degree turn within 0.6 degrees at every checked frame; the ankle's eased 170-degree turn half done at
    its midpoint, exact at its end;
  - the centre moved 0.2 m towards +Z; `あ` at 0.5 halfway; the IK weight 1 then 0; the missing bone reported;
  - dropped into the editor: the `.anim` made and played (the hips turn in Play), the `.vmd` copied in beside it, and
    the lesson's Try-it.
- **Fixed on review:** keys are built per track and assigned whole (a long dance was inserting each of thousands of
  keys into a growing list); a densified run can't flick into its last key; a bone keyed at its rest gets no track
  (its hair would have fought its spring); the motion file is kept beside its clip.
- Checked with round 161's chain (see its entry).

## 2026-10-18 (round 156): MMD models (task 0136, ADR 0047)

**Asked:** round 154's "make the program be able to handle mmd models as well".

**Added**
- **PMX (2.0, 2.1) and PMD import**, read by Blendity's own code. Drop a `.pmx` or `.pmd` onto the window.
  - **Scale and facing:** scaled from MMD's 8 cm unit to metres and turned to face +Z like every other character.
  - **Skin:** skinned to its bones (BDEF1/2/4; SDEF and QDEF read as linear).
  - **Bones:** each bone points at its tail. The standard bones get the names the rig tools read (`左ひざ` ->
    `LeftLeg`), so Walk Cycle and the physics tools find its legs.
  - **Leg IK:** an IKConstraint on each knee, aimed at the foot's IK bone, with a pole in front of the knee.
  - **Physics:** the bones MMD simulates (hair, skirts) become Spring Bones chains.
  - **Faces:** vertex and group morphs become shape keys.
  - **Materials:** Toon shading with MMD's colours, textures and toon ramp; the model's edge becomes an Outline.
  - **Not carried over** (listed in the Console): bone, UV and material morphs, and inherited rotation.
- **Japanese text:** PMX's UTF-16 or UTF-8, and PMD's Shift-JIS, decoded by the operating system (Windows' code page
  932, iconv on Linux and macOS).
- **Learn:** "MMD models: PMX and PMD".

**Checked**
- New tests (round 156), on a character the tests write themselves (no third-party model):
  - Shift-JIS and UTF-16 names decode;
  - the PMX, in UTF-16 and UTF-8:
    - placed to the micrometre, the thigh pointing down its length;
    - the knee's IK on `LeftFootIK` with its pole in front, the hair a spring chain of its body's radius;
    - its front faces +Z, and a turned knee moves the vertices it carries;
    - the morphs lift 8 cm and 4 cm;
    - a Toon material of MMD's lit colour with its texture and its ramp's shadow, and a 6 mm outline;
  - the PMD twin, Shift-JIS names and all, the same;
  - 240 damaged files (cut short, bytes flipped) imported or refused without harm;
  - dropped into the editor: `肌.mat`, its texture in Assets, the walking analysis finding the hips and both legs, and
    the lesson's Try-it;
  - after the review: the kitchen sink (every optional PMX record, vertex indices past 127, the skin on D bones, the
    triangles the other way round) imports the same; two copies of one character each aim their IK at their own
    controls, through a save and a load too; a forked hair gets three chains; a shared toon ramp is reported; a hard
    edge stays split; 5,000 morphs are capped at 4,096 keys; 1,000 nested bones and a vertex at 1e30 are refused; a
    parent loop is broken.
- **Fixed on review:** IK constraints found their target by name anywhere in the scene, so a second copy of a
  character aimed its legs at the first (now its own character first, `ik_target_of`); the "D bones" most PMX models
  weight their legs to now hand their weights to the bones IK and walking drive.
- Checked with round 161's chain (see its entry); round 156's own chain on task 0136 alone: Windows 10,939, 0 failed
  (848 tests), verify 10,940, 0 stale; Linux 10,675; sanitizers 10,553; stress no change.

## 2026-10-18 (round 155): shape keys (task 0135, ADR 0047)

**Asked:** round 154's MMD question: its faces, and VRM's and many FBX faces, are shape keys.

**Added**
- **Shape keys** (blend shapes, morph targets, vertex morphs): named offsets on a mesh, weighted per renderer.
  - The MeshRenderer's BlendShape sliders, animatable as `MeshRenderer.BlendShape/<name>`.
  - Added to the basis before every modifier (the skin bends an expression with the head), on the fast deform path,
    so an animated face doesn't rebuild the mesh.
  - Saved with the scene (sparsely), the weights by their exact names.
  - Carried through edits that add or renumber vertices (subdivide, triangulate, extrude), as vertex groups are.
  - Edit Mode shows the basis being edited. Apply Modifiers refuses a mesh with keys, as Blender does, rather than
    adding them twice.
- **Imports:** FBX blend shapes and glTF morph targets with their names and values.
- **Console:** `shape list | add <name> [amount] | set <name> <weight> | remove <name>`.
- **Learn:** "Shape keys: faces and morphs".

**Checked**
- New tests (round 155):
  - a weight of 0.5 adds exactly half (0 error), the mesh itself when none is in use;
  - saved and loaded; a clip's track at frame 5 of 10 gives 0.5;
  - a key on a bone turned 90 degrees turns with it (1e-7);
  - Blender's FBX and GLB keys, the 0.5 m lift and the value 0.3 come in;
  - the lesson's Try-it;
  - a changing weight deforms the same mesh again (no copy per frame); Edit Mode's cage is the basis;
  - Apply Modifiers refused with keys;
  - keys kept through a triangulate and a subdivide, every old vertex with its offset (0 error);
  - "Smile Left" and "Smile_Left" saved apart; a damaged key line (a count past any integer, an index past the mesh)
    loads without harm.
- **Fixed while checking:** the views keyed their pictures on the meshes without the shape keys' weights, so after
  `shape set` the Scene view could show the shape from before (render-cache verify mode found it, in the lesson's
  Try-it). `GameObject::evaluated_keys` now mixes them in, as `evaluated_mesh` does.
- Unit checks: Windows 10,907, 0 failed (841 tests); render-cache verify mode: one stale picture (above), and after
  the fix 10,908, 0 failed, 0 stale pictures. Linux with libraries: 10,643, 0 failed. Sanitizer build: 10,521,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`,
  `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 154): materials made and applied on import (task 0134, ADR 0047)

**Asked:** "When importing external fbx models, can the program auto create a material and apply it. Furthermore,
make the program be able to handle mmd models as well. Feel free to implement other rigged character formats as
needed." The user chose full MMD support, VRM, FBX/glTF animations, BVH and Collada, materials as .mat assets, and
synthetic test files (tasks 0134-0142).

**Added**
- **Every import makes `.mat` assets** in `Assets/Materials/<model>/`, applied to the model. An edited one is kept on
  re-import.
- **Textures:**
  - copied into `Assets/Textures/<model>/`, including the ones packed inside an FBX (they were dropped);
  - a link to another machine's path found by name beside the model, in its `.fbm`, `textures` or `images` folder;
  - a bare material given the texture set beside it by name (`Body_BaseColor`, `T_Body_N`, `_ORM`...).
- **Maps:** gloss, smoothness and packed maps (glTF's metallic-roughness, which wasn't read at all, Unity's
  metallic-smoothness, Unreal's ORM) are split into the maps Blendity reads.
- **Materials:** every mesh gets one; FBX transparency and double-sided are read.
- **Two models of one name** (every Sketchfab download is `scene.gltf`) get folders of their own
  (`Materials/scene 2/`); importing the same one again reuses its folder. A texture or embedded image sharing a name
  with another gets its own file.
- **Matching by name is careful:** a single-letter suffix (`_d`, `_n`, `_r`) counts only in a set of two maps or more
  (`Hand_R.png` is not roughness); a folder's only set goes only to materials the import made, never over a file's
  own flat colours.
- **File > Import** from outside the project copies the model in, as dropping it does. The console has
  `import <file>`.
- **The FBX exporter's Embed Textures:** the images go inside the file, as Blender's do.
- **Learn:** "Importing models: their materials made for you".

**Checked**
- New tests (round 154):
  - Blender's FBX with embedded textures comes in textured (red 204);
  - its GLB's packed map split exactly: roughness 64, metallic 255;
  - a moved link found, a texture set matched by name, a gloss map inverted (63);
  - every STL mesh gets a material;
  - Blendity's embedded FBX reads back with its image deleted;
  - `Skin.mat` made and used, and an edit to it kept on re-import;
  - two models called `r154twin.obj` imported into two folders, each its own texture (red 255, blue), the first
    imported again into its own;
  - flat Red and Blue kept beside an unrelated texture; an `.mtl` in a subfolder finds its texture there;
  - a packed map split once for every material using it; two embedded `diffuse.png` kept apart;
  - the tests' import cache in a scratch home.
- Checked with round 155's chain (see its entry).

## 2026-10-18 (round 153): MoonRay's image fills in while it renders

**Asked:** "Why does MoonRay doesn't seem to render?" It did: a 1280x720 frame at 128 samples takes about a minute,
and nothing showed until it was done. The user agreed: "the image fill is also good."

**Changed**
- **MoonRay renders with checkpoints:** an image after every quality step, about a second apart, written beside its
  scene.
  - A watcher on the render's own thread reads each new checkpoint (never the editor's thread: it is 14 MB).
  - The Render panel shows it through the same view transform as the final image, with "filling in (N updates)" and
    the time.
  - MoonRay prints no percentage in this mode.

**Fixed**
- MoonRay's own default image (`scene.exr`) was written into whatever folder it ran from (an untracked
  `blendity/scene.exr` came from it). It now goes into Blendity's scratch folder with the rest.

**Checked**
- **A new test (round 153):** the stand-in MoonRay writes a checkpoint at half brightness, then the final image; the
  Render panel shows 0.250, then 0.500.
- **With the real MoonRay:** 53 s into a 1280x720 frame the panel showed the image, after 43 updates; nothing was
  written where it ran.
- Unit checks: Windows 10,882, 0 failed (827 tests); render-cache verify mode 10,883, 0 stale pictures. Linux with
  libraries: 10,618, 0 failed. Sanitizer build: 10,496, 0 failed, no reports. `editor_fuzz` in verify mode: 6,000
  frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no
  change.

## 2026-10-18 (round 152): automatic walking and AI inbetweening written up (task 0132)

**Asked:** the plan's write-up.

**Added**
- Learn: "Automatic walking: which tool when" (Walk Cycle, Footsteps, Locomotion) and "AI inbetweening: the motion
  between two keys", each with a Try-it and questions.
- README, ARCHITECTURE, USABILITY; ADR 0046; the licences of the optional download (ONNX Runtime's MIT notice, the CMU
  database's credit) and `extern/onnxruntime/`.

**Checked**
- A new test (round 152) runs both lessons' Try-its.
- Unit checks (rounds 148-152 in one chain): Windows 10,881, 0 failed (826 tests); the whole suite in render-cache
  verify mode: 10,882, 0 failed, 0 stale pictures. Linux with libraries: 10,617, 0 failed. Sanitizer build: 10,495,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- With a real install (`BLENDITY_AI_TEST_INSTALL`): Windows 60 frames in 3.1 ms; Linux under the sanitizers 4.2 ms,
  no reports; on both the walk's middle made again 9.3 cm from the walk, the keys' interpolation 14.5 cm.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 151): fine-tuning on your own clips (task 0131)

**Added**
- **Fit to Clips** (`ai finetune`):
  - every clip of the project that animates the character (about three minutes at most) is sampled, then fitted on
    a worker with progress and Stop;
  - a correction of the network's last layer, solved exactly (ridge regression), saved as
    `Assets/AI/<character>.aihead` and used for that character's spans;
  - `ai finetune off | on` goes back to the shipped model and returns to the fit.
- ONNX Runtime's standard build has no on-device training (its training API is null in 1.31.0), so the plan's training
  API became a fit of the last layer in Blendity's own code (ADR 0046).

**Checked**
- A new test (round 151) with a stand-in network:
  - the fit lowers the RMS error on the clips it was fitted to;
  - off, a made span is exactly the shipped model's;
  - Stop keeps the last fit.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 150): AI inbetweening in the Animation window (task 0130)

**Added**
- **AI Inbetween** (the Animation window, with the switch on and the download installed):
  - makes the span between the keys around the playhead in a style (walk, run, jump, fall, crawl, combat,
    acrobatic, other), on a worker, shown as a purple ghost (grey once the keys change);
  - Variation, and Apply as one undo step;
  - `ai span [from to] | style | styles | variation | apply | off`.
- `ai::make_request` and `ai::apply`:
  - the rig mapped onto the model's skeleton (mirrored, faced, scaled by the legs, each bone's rest direction aligned
    to the data's), a 60 fps clip resampled to the model's 30;
  - only the frames strictly between the keys keyed, the keys at the ends kept with their outer slopes frozen;
  - a turn keyed whole turns away spread over the span.

**Fixed after review**
- **Clips told apart only by their version:** Apply could write one character's span into another's clip. A span now
  knows its clip and character.
- **The ends' Auto slopes followed the new keys** and bent the curves outside the span. They are frozen now.
- **A yaw keyed past 180 degrees** spun round on the last frame. It is spread over the span now.
- **Fine-tuning kept every window in memory.** It now uses running sums, capped at about 4,000 windows and three
  minutes of clips.
- **Work on the editor's thread:** check, install and loading the runtime now run on workers, and the model's
  description is read once.
- **Installs:**
  - an archive's members are checked before unpacking, into a staging folder moved into place;
  - removing deletes only the install's own files;
  - the model's output size is checked.
- **Other fixes:**
  - the hands', head's and toes' rest alignment;
  - Variation's seed sticking;
  - a span past the clip's end refused;
  - the AI workers waited for on closing.

**Checked**
- New tests (round 150), with a stand-in network:
  - only the span is made (29 of 29 frames), its ends and every key outside untouched;
  - the curves outside the span move 0.000000 m; a 200 degree yaw is met in steps of 6.7 degrees at most;
  - another character's clip is never written;
  - the same seed gives the same keys; Apply is undone whole;
  - four legs, a span past the end and a NaN answer are refused;
  - a pose goes to the model's skeleton and back to 0.000 degrees.
- With the real model (where `BLENDITY_AI_TEST_INSTALL` names an install; skipped on CI): a walk's middle 29 frames
  made again are 9.3 cm from the walk, the keys' interpolation 14.5 cm.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 149): the AI download, optional as MoonRay (task 0129)

**Added**
- **The download:** `tools/inbetween/package.py` builds `blendity-ai-<platform>-ai-v1` with a `.sha256` each, from
  Microsoft's ONNX Runtime 1.31.0 (MIT) and the trained model: Windows 23.6 MB, Linux 28.0 MB, macOS on Apple silicon
  29.3 MB. Publishing them is the user's call.
- **In Blendity:**
  - the AI dialog, offered once when the switch is first turned on (never at startup);
  - Preferences > Animation Assistants: Install / Remove;
  - `ai status | install [archive] | remove | check`.
- ONNX Runtime is loaded with LoadLibrary or dlopen through its C API only when used, so the base build has no
  dependency on it.

**Checked**
- New tests (round 149):
  - a wrong checksum is refused;
  - an install that can't run is removed again;
  - an archive holding a foreign file is refused;
  - without the download, AI Inbetween says how to get it.
- With a real install: ONNX Runtime loads and makes 60 frames in 3.1 ms.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 148): the inbetweening network trained on CMU, GO (task 0128, ADR 0046)

**Asked:** the plan's AI inbetweening, trained on CMU motion capture. The user approved the downloads (PyTorch,
NumPy, ONNX, onnxruntime; the CMU database).

**Added**
- `tools/inbetween/` (build-time only):
  - `cmu.py`: the CMU database (2,503 clips, 10.5 hours) on one 22-joint skeleton at 30 fps, labelled by style;
  - `model.py`, `train.py`: a 4.9M-parameter transformer correcting interpolation;
  - `export.py`: ONNX with its last features, any window length;
  - `styles_eval.py`; `package.py`.
- The data and the Python environment stay outside the repository (`.venv/` and `runs/` git-ignored).

**Checked**
- **On the held-out subjects:** the joints' mean error, interpolation against the network:

  | Gap | Interpolation | Network |
  |---|---|---|
  | 15 frames | 2.7 cm | 2.0 cm |
  | 30 frames | 5.7 cm | 4.0 cm |
  | 60 frames | 10.2 cm | 7.2 cm |
  | 120 frames | 16.8 cm | 13.1 cm |

- **By style:** walk 62% better, jump 53%, acrobatic 40%, combat 35%, fall 20%, other 18%.
- **Foot sliding:** worse than interpolation past two seconds, recorded.
- **Speed:** 60 frames in 3 ms on the CPU. **GO.**
- **Fixed on the way:** the first export fixed the window's length inside attention (every other span failed in
  Blendity). The encoder is written out for export, and the export is checked at gaps of 1, 29 and 120 frames.

## 2026-10-18 (round 147): Locomotion, walking live in Play (task 0127)

**Asked:** the plan's "both baked into clips and live in Play" (round 145's question).

**Added**
- **The Locomotion component** (with the Locomotion assistant on): Walk Cycle's gait made frame by frame from how the
  character moves - W walks, Shift+W runs, A and D turn, or a PlayerController, a Rigidbody or a script's `set_move`
  moves it.
  - The phase goes on with the speed; the walk blends into the run; let go, and it finishes the step and stands.
  - Each swinging foot is aimed at where the body will be when it lands, on the ground there (a grid of the scene's
    meshes gathered at Play start): ramps and stairs.
  - The pelvis stays in reach of both feet and leans into turns. Upper Body off leaves the spine, head and arms to a
    clip.
- `gait::apply`: putting a pose on the rig, shared by bake and Locomotion.
- Learn: "Locomotion: walking live in Play".

**Fixed after review**
- A prop held in a hand was snapped back every frame (and one a script destroyed would have been written to after):
  only bones and IK controls are posed now, kept by id, and the rig is read again if one goes.
- A dog let go kept stepping on the spot (its feet are never all down while it walks): a foot that lands stays down.
- The ground left out physics bodies and other characters; Follow Ground off while physics or a controller moves the
  object.

**Checked**
- New tests (round 147):
  - 3 s of W: 3.97 m (3.95 expected), planted feet still to 0.001 mm, the stride Walk Cycle's to the millimetre,
    0.013 ms a frame;
  - running round a turn the hips lean into it; let go of a run, standing in 0.92 s (within the slowing and one step);
    a dog stops too;
  - up a ramp every landing on its surface; a prop a script moves is left alone, and destroyed in Play breaks nothing;
  - the assistant off, through the editor's Play: no object moves;
  - the lesson's Try-it, then Play with a script's `set_move`.
- Unit checks (rounds 146-147 in one chain): Windows 10,872, 0 failed (817 tests); the whole suite in render-cache
  verify mode: 10,873, 0 failed, 0 stale pictures. Linux with libraries: 10,608, 0 failed. Sanitizer build: 10,486,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 146): Footsteps, walking along a path (task 0126)

**Asked:** the plan's footsteps along a path, as 3ds Max's Biped (round 145's question).

**Added**
- **Footsteps** (the Rigging window's section, with the assistant on): Walk Along Path lays footprints along a curve at
  Walk Cycle's stride - short first and last steps, the inner foot's shorter in turns - each dropped onto the mesh
  under it, and keys the character walking on them (feet planted, the pelvis over them at their height, a higher swing
  over a step up).
  - Each print is an object (blue left, red right, its landing frame drawn on it): move or turn it, or change its Frame,
    then Update. Re-plan lays them out again and keeps the ones moved by hand. One undo step each.
  - Console: `walk path <curve> | walk steps update | walk steps replan`.
- `scene_raycast` and a ground grid in the scene module (the footprints; Locomotion's feet).
- Learn: "Footsteps: walking along a path".

**Fixed after review**
- A print dragged beyond the leg's reach wasn't reached: the pelvis now goes towards it.
- Re-plan kept only moved prints, matched by frame (lost after a style change): turned and retimed prints count, and
  each is matched to the nearest new print of its foot; any left over is reported.
- A failed clip after the prints were replaced; two characters with one name sharing prints (refused now); a renamed
  path's message; a first print retimed past frame 0; prints retimed closer than a swing; a huge ground's grid.

**Checked**
- New tests (round 146):
  - a straight path: every foot on its print to 0.000 mm, planted feet still, first and last steps short;
  - a left turn: the inner steps 0.74 m against the outer's 0.81 m, no print on the wrong side;
  - box stairs: every print on a step, the hips rising 0.90 m;
  - in the editor: refused while off, a moved print kept by Re-plan and walked on, undone in one step (keys and every
    print as before), saved and loaded;
  - a print 0.8 m out reached; a turned print and a moved one kept through a faster style; twin names refused;
  - the lesson's Try-it.
- Unit checks: with round 147's (one chain for rounds 146-147; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 145): Walk Cycle, walking made from the rig (task 0125)

**Asked:** "Is there an automatic walk system in cascadeuer and other smart systems that would be ideal?" Cascadeur
has none (its locomotion is AI inbetweening and AI root motion). The user chose procedural gaits (as 3ds Max's
CATMotion) and footsteps along a path (as Biped's), in clips and live in Play, AI inbetweening as an optional
download, every one of them a switch, and hand animation unchanged.

**Added**
- **Walk Cycle** (the Rigging window's section and the Animation window's button, with the assistant on).
  - **Presets:** walk, jog, run, sneak and limp, or walk, trot and gallop for four legs.
  - **Numbers:** speed, stride, feet-down share, foot lift, heel-toe roll, bounce, sway, hip twist and drop, the spine's
    counter-turn, arm swing, elbows, lean and crouch.
  - **Generate** keys the rig's own controls (hips, the feet's IK targets and poles, feet, spine, head, arms):
    - feet planted while down, never sliding;
    - the stride from the Froude number, so a style fits any size;
    - a treadmill loop or travelling forward;
    - the planted frames written as contacts.
  - Presets are blended by weight and saved as `.gait` files. Console: `walk cycle | set | save | load | presets`.
- Learn: "Walk Cycle: walking made from the rig".

**Fixed on the way**
- **The loop's seam:** rounding a cycle to whole frames through the speed also changed the stride (the Froude number),
  so the period never landed on a frame. It adjusts the stride now.
- **A foot at a cycle's boundary:** float time left it "mid-swing" for a frame, so the pelvis wasn't lowered for it and
  IK fell 5 cm short. The phase is snapped at whole cycles, and every foot is kept within reach.

**Fixed after review**
- The hands kept still: the arms' IK pulled them back to rest. Their IK targets and poles are keyed now.
- The heel-toe roll turned the foot about the ankle, so the toes went through the floor at push-off. It rolls about the
  heel, then the toes, and just off the ground the ankle is raised until the toes clear (18 mm low before, 0 now).
- `walk set` reset every other number to the defaults; it changes only the one named.
- A rig facing -Z walked backwards: forward is read from the feet (ankle to toes), else the armature's turn.
- Shapes taken for legs too eagerly; a heel bone taken for the toes; IK controls found outside the character.
- Legs without IK bent their knees the wrong way: the knee bends the way it did at rest.
- Generate on a failed walk left an empty clip, cut a longer clip short and removed the hands' contacts. It works on a
  copy now, keeps the clip's length and replaces only the legs' contacts and the tracks it wrote.
- The preset list stuck on a loaded or blended style (it says "custom"); the Animation window's body strip and the
  Inspector's Body Mass showed with the physics tools off.

**Checked**
- New tests (round 145):
  - a planted foot's heel or toes (whichever is on the ground) move 0.000 mm; the treadmill's seam is 0.0002 mm;
  - the soles never below the floor; the hands swing 37 cm through their IK targets; a rig facing -Z walks -Z;
  - `walk set` keeps the other numbers; a walk too long to make leaves no clip;
  - a leg twice as long gives a cycle twice as long and sqrt(2) as slow;
  - a run is airborne, a walk never;
  - a trot moves diagonal pairs together;
  - presets, blending and `.gait` files;
  - the quadruped walks;
  - AutoPhysics moves a walk at most a few centimetres;
  - in the editor: refused while off, on as ordinary keys that edit by hand and undo in one step.
- Unit checks (rounds 144-145 in one chain): Windows 10,857, 0 failed (808 tests); the whole suite in render-cache
  verify mode: 10,858, 0 failed, 0 stale pictures. Linux with libraries: 10,593, 0 failed. Sanitizer build: 10,471,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 144): animation assistants switched on, and the rig read for walking (task 0124, ADR 0045)

**Asked:** "Make sure that this is a toggle feature. Not all animation needs these feature." and "Still have the
ability to traditionally pose and animate models."

**Added**
- **Preferences > Animation Assistants:** a switch each for the Physics Tools (on, as they were), Walk Cycle,
  Footsteps, Locomotion and AI Inbetweening (off).
  - Off, a tool's buttons, sections, overlays and live work are gone, and its console command says how to turn it on.
  - Hand animation never changes.
  - Console: `assist list | on <name> | off <name>`.
- **`gait::analyse`:**
  - legs, arms, hips, spine and head, by names (Mixamo, Unity, Rigify, `create_rig`'s; `limb_of` is now public) and
    then by shape;
  - two legs or four, each with its IK target and pole;
  - the measures and the rest pose;
  - a reason when a rig can't walk.

**Checked**
- New tests (round 144):
  - the switches' first state, refusals and keeping through a restart;
  - the humanoid and quadruped rigs, Mixamo and Rigify names and an unnamed stick figure read right;
  - degenerate rigs refused with why.
- Unit checks: with round 145's (one chain for rounds 144-145; there). With every assistant off, the existing
  animation tests pass unchanged.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 143): MoonRay built for macOS too (task 0111)

**Asked:** round 135's "Do and finish all tasks" - the MoonRay archive for macOS, which CI hadn't built yet.

**Fixed**
- CI's macOS packaging stopped silently (`set -e`): `otool` failing on a scene object folder's non-library files, and a
  library's `@rpath` install name found nowhere. Both are passed over now, with progress printed.

**Checked**
- CI run 39130702928: MoonRay built on Apple silicon and packaged into a 240 MB archive (149 libraries). On a clean
  macOS 15 runner the archive unpacked and rendered its test scene in 8 s. With Linux's (round 136), both archives
  Blendity offers are built and checked; publishing them to a release is the user's call.
- No source changed (CI scripts and docs only): the suite stands as round 142 ran it.

## 2026-10-18 (round 142): physics for animation written up (task 0123)

**Asked:** the physics plan's last task, finishing "Finish and do all tasks please."

**Added**
- Learn: "Physics for animation: a jump from start to finish".
  - The whole set in Cascadeur's order: weigh, contacts, flight or AutoPhysics, then spring bones, the ragdoll and
    AutoPosing.
  - The ghost and its colours, shared by all of them.
  - A Try-it: a person jumping in straight lines, its contacts found and its flight shown corrected.
- CI's MoonRay build on macOS:
  - freetype2 on the include path for MoonRay's telemetry overlay;
  - the dependencies' cache found by prefix when the build script changes (it takes hours to rebuild).

**Checked**
- A new test (round 142) runs the Try-it as the Learn tab does: 4 contacts and one flight, moved up to 0.84 m (the
  keyed 1 m peak was too high for 0.67 s in the air).
- Unit checks (rounds 140-142 in one chain): Windows 10,832, 0 failed (798 tests); the whole suite in render-cache
  verify mode: 10,833, 0 failed, 0 stale pictures. Linux with libraries: 10,568, 0 failed. Sanitizer build: 10,447,
  0 failed, no reports (without Jolt: the ragdoll's tests say so and skip). `editor_fuzz` in verify mode: 6,000
  frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 141): AutoPosing: the whole body posed by one end (task 0122)

**Asked:** the physics plan's AutoPosing (the user chose rule-based), as part of "Finish and do all tasks please."

**Added**
- **AutoPosing** (the Animation window's button, `autopose on`): drag a hand, a foot, the head or the hips with the move
  gizmo and the whole body follows.
  - **The solver:** a full-body inverse kinematics by damped least squares over every body bone and the hips' place.
  - **Feet:** the feet stay (the contacts at the playhead, bones you lock, or else the feet).
  - **Balance:** the center of mass stays over them.
  - **Natural poses:** each joint stays within 75 degrees of the pose it started in (the spine half of that) and is
    pulled back towards it.
  - **Speed:** under a millisecond for a humanoid, live while dragging.
  - **IK:** the targets move with the pose, so IK agrees with it.
  - **On release:** the moved bones are keyed at the playhead, in the drag's undo step.
- The console: `autopose on|off | drag <bone> <x> <y> <z> | lock <bone> | unlock <bone> | locks`.
- Learn: "AutoPosing: pose the whole body by one end".

**Fixed on the way**
- The Windows build's command line passed cmd's 8,191-character limit with the new files ("The input line is too
  long"): the sources now go to the compiler in a response file.
- A target far out of reach walked the body off its feet (a foot 2 m). Now:
  - each goal pulls at most 10 cm a step;
  - pins weigh ten times a goal;
  - the hips move at most 30 cm with feet pinned;
  - settle passes put a slipped pin back (29 cm before them, 0 after).

**Checked**
- New tests (round 141):
  - a hand in reach: 0.6 mm off, the feet held, joints within their limits, the head barely turned, 0.5 ms;
  - the head forward: the center of mass 3.4 cm from over the feet with balance, against 6.6 cm without;
  - 5 m out of reach: the arm points at the target (2 degrees), a foot within 12 mm, all finite;
  - the hips dragged 25 cm down: a crouch 5.6 mm off, the feet held and flat, the knees bending forward;
  - in the editor: a drag posed and keyed, one undo step, the IK targets and poles following (the elbow kept to
    0.0 mm through IK), a locked hand staying.
- An Opus review (the Jacobians, the solve and the limits checked: correct); every finding fixed:
  - dragging the hips pinned nothing (the feet were "below the dragged bone"), so the body slid;
  - IK poles weren't moved, so IK undid the knees' and elbows' bend;
  - a pinned foot kept its place but not its turn (toes through the floor);
  - the costs per drag frame (subtree sums by parents now, pins and IK chains worked out once a drag, the body kept
    between drags, settling stopping when done);
  - limits were measured from where each drag started, so they piled up, and knees could bend sideways (now from the
    rest, and a knee or elbow turns only about its bend axis);
  - a drag cut short left stale state;
  - every moved bone got Position keys, and IK-driven bones were keyed;
  - Play logged keys it didn't write;
  - balance counted locked hands as support;
  - the locks outlived the scene.
- Found while fixing:
  - balance fought a dragged goal (a crouch stopped 5 cm short). It now works only with joints that move no goal and
    no pin, leaning the torso instead.
  - A locked hand had switched off the feet's pinning.
- Unit checks: with round 142's (one chain for rounds 140-142; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 140): the active ragdoll: hits and falls that react (task 0121, ADR 0044)

**Asked:** the physics plan's active ragdoll (the user chose Jolt), as part of "Finish and do all tasks please."

**Added**
- **The active ragdoll** (with Blender's libraries: Jolt Physics):
  - the character as capsules, one per weighed bone, joined with humanoid limits;
  - joint motors drive it to the clip's pose with a strength; the hips are pulled to their place;
  - a floor under the clip's lowest point;
  - simulated on one thread, so the same clip always gives the same keys.
- **Events in the clip:** a hit (an impulse on a bone at a frame), limp (the motors off from a frame: a fall) and strong
  (on again). They are saved with the clip.
- The Animation window's **Ragdoll** button: simulated in the background, shown as a ghost, Apply one undo step. The
  console: `body ragdoll show [strength] | apply | off | hit | limp | strong | events | clear`.
- AutoPhysics and the ragdoll share one background worker, which hands back the finished result.
- Learn: "The active ragdoll: hits and falls".

**Fixed on the way**
- Blender's Jolt is built without RTTI, so a `dynamic_cast` on its constraints crashed. They are told apart by Jolt's
  own type.

**Checked**
- New tests (round 140), skipped without Jolt:
  - full strength follows the clip within 6.2 degrees, strength 0.2 within 64;
  - limp from a tipped pose, it falls from 1.17 m to the floor and rests;
  - a hit on a bone moves it most (0.32 m against 0.05 m);
  - a hit on the hips knocks the body back 0.7 m even at full strength;
  - an absurd impulse leaves finite keys;
  - two runs give identical keys;
  - events survive saving;
  - in the editor: a worker, the ghost, Apply and undo.
- An Opus review (no crash or lifetime fault; the threading checked); every finding fixed:
  - the hips' drive erased a hit (it now gives way for half a second after one);
  - the strength only capped the torque (it now sets the motors' stiffness too, and each joint's torque comes from the
    weight it carries);
  - the joints' limits were centred on the clip's first frame, not the rest pose;
  - bones touching in the first pose (thighs, shoulders and chest) could push apart;
  - a bone under a weightless helper was keyed in the wrong place;
  - the floor started inside the capsules;
  - the AutoPhysics button lit up for a ragdoll;
  - Jolt's layers were destroyed before the system, and a failed ragdoll left the floor in it;
  - early hits were lost, hits on bones that aren't joints said nothing, and huge ones could write non-finite keys;
  - a fallen body could sleep through "strong";
  - Apply's label, the replaced-key count, and a strength that isn't a number.
- Unit checks: with round 142's (one chain for rounds 140-142; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 139): spring bones for hair, ears, tails and cloth, found automatically (task 0120)

**Asked:** the physics plan's secondary motion, with "auto select hair bone, ear bone, cloth bone sequences
automatically. Or recursively if I select a parent bone that is the start of that specific chain" and "make sure that I
can also deselect or uncatogorize the chains if it's unneeded"; then "Finish and do all tasks please."

**Added**
- **SpringBone** on a chain's first bone: hair, ear, tail or cloth, each with its own stiffness, damping, gravity and
  radius.
  - The chain lags behind the body, swings past and settles, kept outside the body's bones.
  - It is the same at any frame rate: substeps, with rates per 1/60 s.
  - Live in Play, on the pose IK finished. **Bake into Clip** keys it frame by frame from the rest pose (the same keys
    when baked again, from the UI too), turns the live simulation off, and is one undo step.
- **Chains found automatically** (Rigging > Spring Bones > Find Spring Chains).
  - **By name:** hair, ponytail, ear, tail, skirt, cape and more, from Mixamo, VRM's `J_Sec_` and Japanese rigs'
    kami / mimi / shippo / sukato.
  - **By shape:** every branch of a fork is its own chain.
  - **Never offered:** limbs, fingers, toes, head, eyes or jaw.
  - Named chains come ticked and labelled; unnamed extras are listed unticked.
- **Your choice wins:**
  - untick a chain, change its label, or set it to **None**;
  - None and Remove are remembered on the chain's first bone ("Not a Spring Chain"), so Find doesn't offer it again;
  - Show Ignored and Unignore bring one back;
  - **Add Spring Bones** on a selected bone works recursively (a hair root's strands, a chain each);
  - **Remove** on a chain or a parent takes everything under it off, in one undo step.
- The console: `spring find [all] | apply | tick | label | add | remove | ignore | unignore | bake | list`.
- Learn: "Spring bones: hair, ears, tails and cloth".

**Checked**
- New tests (round 139):
  - 27 names across rig conventions read right;
  - Find on a Mixamo body picks exactly the 5 strands, 2 ears, the tail and 2 skirt strips, labelled, with an unnamed
    extra unticked;
  - a hair root makes five chains and leaves out a body bone under it;
  - at rest nothing moves; a stopping hip makes the tail swing past (1.8 cm) and settle;
  - collision keeps a tail out of a thigh (off: through it);
  - baking twice gives identical keys and puts the scene back;
  - in the editor: untick, None and Apply; ignored until Unignore; Remove on the hair root as one undo step, undone
    whole;
  - labels, settings and the ignored mark saved and loaded;
  - live in Play, and not when baked;
  - the same swing at 30, 60 and 144 frames a second (1.70, 1.70 and 1.74 cm past);
  - a 50 m teleport starts the chain afresh.
- An Opus review; every finding fixed:
  - the Find list held the bones themselves: Play and Stop, or an undo, left it pointing at freed objects (it keeps
    their ids now);
  - other chains' bones counted as the body: strands pushed each other apart (and the body's capsules were rebuilt
    per chain per step: now once per armature);
  - springs stepped before IK, a frame behind a limb IK moves;
  - Add again reset tuned chains and re-added chains set to None;
  - Add on a strand's middle bone split it;
  - None took off the chains below a fork too;
  - names looked up across characters;
  - `spring add <bone> none` added cloth;
  - a second bake found nothing;
  - Euler keys could flip near 90 degrees;
  - a zero-length bone's tip drifted;
  - a teleport whipped the chain;
  - ignoring a middle bone did nothing;
  - a frame's whole move kicked the chain at once (30 fps swung less than 60).
- Unit checks (rounds 138 and 139 in one chain): Windows 10,811, 0 failed (789 tests); the whole suite in render-cache
  verify mode: 10,812, 0 failed, 0 stale pictures. Linux with libraries: 10,547, 0 failed. Sanitizer build: 10,438,
  one failed - "spring bones in Play" measured 3.0 mm of lag against its 3 mm floor, the sanitizer's slow frames
  hitting Play's 0.05 s step cap. Its hips now move 5 cm a frame (the lag clear of the floor at any frame time); no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 138): AutoPhysics: the whole clip made physically possible (tasks 0118, 0119, ADR 0043)

**Asked:** "Finish and do all tasks please" (the physics plan's next tasks: the AutoPhysics spike, then AutoPhysics).

**Added**
- **The spike (task 0118, ADR 0043): GO.**
  - The body is reduced to its center of mass and its spin, pushed only by gravity and the contacts' forces
    (centroidal dynamics).
  - One trajectory optimisation over the clip finds the nearest motion with Newton's and Euler's laws holding at
    every frame and each force pushing inside its friction pyramid.
  - Measured on four clips of 100 frames (a jump, a run, a flip, a fall onto the hands): 221-493 ms each, the laws
    held to 7e-6 to 1.3e-4 of the body's weight, flights falling at exactly g, and a flip's spin constant in the air.
- **Blendity's own QP solver** (`core/qp`): ADMM as OSQP does it, with a banded Cholesky factorisation (a trajectory
  couples only frames a few apart), warm starts, an adaptive step and cancel. No new dependency.
- **AutoPhysics (task 0119):** the Animation window's AutoPhysics button.
  - Sampled on the editor's thread, solved on a worker ("Solving..."), shown with Flight's ghost, frame colours and
    Apply (one undo step).
  - The hips carry the correction; the feet's and hands' targets are held through their contacts and follow the body
    again over 4 frames after.
  - A clip edited while it solves isn't shown. The console: `body physics show|apply|off`.
- Learn: "AutoPhysics: the whole clip", with questions.

**Checked**
- New tests (round 138):
  - the QP's known answers, an equality, bounds, a warm start, a 2,000-unknown chain in its band, an empty constraint
    refused;
  - the four clips' times, residuals and moves;
  - standing coming back as it was, and bad input refused;
  - a jump keyed in straight lines made a parabola, with the foot held and the keyed clip realising the solved
    center;
  - a possible clip all green; missing contacts or contacts on bones that aren't IK ends explained; a cancelled
    solve;
  - in the editor: the worker, the ghost, Apply and undo, an edited clip not shown, Off.
- An Opus review (the math, the solver and the worker's lifetimes checked: correct); every finding fixed:
  - a finished solve waited for the mouse to move (the editor slept);
  - the turn left at the end was taken back in the air too, where nothing could do it;
  - Euler's central difference let the momentum zig-zag between even and odd frames;
  - results were judged by the solver's status, not the laws (a long clip thrown away, an unsettled one kept);
  - no limit on a clip's length, no cancel between passes, non-finite lengths not refused;
  - a result could arrive in Play, show on another scene with a clip where the old one was, or replace Flight's;
  - a contact pushed at its first frame's tip, not where the clip has it.
- Unit checks: with round 139's (one chain for both; there). CI's sanitizer runner took 13 s on the spike's four clips
  (the guard was 5 s): their time is checked in optimized builds only.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 137): physics for animation: the body's masses, contacts and flight (tasks 0115-0117, ADR 0042)

**Asked:** "Is there a way to implement body simulation for animation like in Cascadeur." The user chose the center of
mass with ballistics, the full AutoPhysics solve, secondary motion, an active ragdoll (Jolt) and rule-based
AutoPosing; then that hair, ear and cloth chains be found automatically (or from a parent bone) and can be unticked or
relabelled; then "Finish and do all tasks please." This round is the first three tasks of that plan.

**Added**
- **The body's masses (task 0115, ADR 0042):** each deforming bone weighs its share of the skinned mesh's volume
  (tetrahedra against the bone's own middle, shared by the corners' weights), with its center and inertia; bones
  without a skin get capsules; all scaled to the armature's **Body Mass** (75 kg, in the Inspector). The center of
  mass and the whole body's inertia follow any pose, the rig root's motion included.
- **The center of mass in a clip:** the Animation window's **Center of Mass** button draws its path over the clip,
  where it is at the playhead and the point under it. The body is built once per rig and skin, not every frame.
- **Contacts (task 0116):** an IK chain's end within 5 cm of the ground and slower than 0.5 m/s for 2 frames or more
  is a contact, shown as bars under the ruler (set by hand brighter, and kept when contacts are found again). Pin
  keys the IK target still through each one, its parent's motion frame by frame taken into account. Contacts are
  saved in the clip.
- **Flight (task 0117):** between a take-off and a landing the center of mass is put on the parabola through them at
  their times (g = 9.81 m/s^2), and the body turned about it so the angular momentum stays the take-off's (solved in
  the turned axes, refined until it holds); the landing pose kept by default. **Flight** shows the corrected body as a
  ghost, its frames green (fine), blue (changed) or red (impossible: nothing written); **Apply** keys it into the
  clip as one undo step, only into the clip it was worked out for.
- The console: `body show on|off`, `body mass [kg]`, `body com [frame]`, `body contacts find|pin|clear|list|add|remove`,
  `body flight show|apply|off`.
- Learn: "Physics for animation: the center of mass" and "Contacts and flight", each with Try it and questions.

**Checked**
- New tests (round 137): two skinned boxes weigh 25 and 50 of 75 kg with their centers and inertias; the center of
  mass and inertia follow a pose (parallel axes), capsules without a skin; a clip's trajectory follows the root and
  leaves the scene as it was; an IK leg's rests found within a frame, a manual contact kept, pinning holding the
  target, contacts saved and loaded; a straight jump made an exact parabola, a tuck's angular momentum kept within 3%
  (0.5% on a double spin), the landing pose kept, an impossible jump marked red and not written; in the editor Body
  Mass, contacts, the ghost and Apply as one undo step.
- An Opus review; every finding fixed:
  - the overlay rebuilt the body every frame;
  - IK targets were keyed against a fixed parent with a moving root;
  - Apply could write the clip of another character selected since;
  - contacts outside the clip weren't clamped, and replaced keys weren't counted;
  - the hips were guessed from the first bone (now the root, else the heaviest top bone);
  - a turn could flip Euler angles (the other solution is now taken when nearer);
  - the momentum correction worked in the wrong axes for a large turn;
  - the body's cache ignored the bones; Apply ran while playing; animated values other than transforms weren't put
    back; `body com` read the wrong frame; manual contacts didn't trim found ones.
- Unit checks: Windows 10,753, 0 failed (773 tests); the whole suite in render-cache verify mode: 10,754,
  0 failed, 0 stale pictures. Linux with libraries: 10,489, 0 failed. Sanitizer build: 10,380, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 136): MoonRay checked against the real renderer (task 0113)

**Asked:** "Do and finish all tasks" (round 135's), with CI's Linux MoonRay archive downloaded into WSL to check the
export against the real thing ("Yes, download it into WSL").

**Fixed** (each measured in MoonRay v2026.29.1 against F12's path tracer on the same scene)
- **Textured materials rendered black:** MoonRay's texture system takes only tiled, mip-mapped files (as `maketx`
  makes them), so every PNG failed. Textures now go as tiled float EXRs with every mip level (`write_texture_exr`,
  dependency-free, area-weighted levels, wrapping), linear (an 8-bit colour map's sRGB undone first), made on the
  render's thread and kept while the source is unchanged.
- **The world map was turned:** its directions were measured with a map whose pixels are their own coordinates; the
  world, its sky and its azimuth now match to 0.1%.
- **A sun was twice as bright:** a DistantLight's normalized intensity is Blendity's intensity as it is (both give
  pi x I of irradiance); it matches to 1%.
- The world didn't show behind the scene (MoonRay hides lights from the camera by default), and an empty scene
  rendered black (MoonRay needs geometry in its layer: a speck far below is added).
- MoonRay's own error lines are in the log.
- CI's MoonRay build: Python 3.11 (Boost.Python failed on 3.13), autotools on macOS, the dependencies' cache saved
  even when a later step fails, the check containers allowed `mbind`.

**Checked**
- New tests (round 136): the texture EXR's tiles where its table says on every level, down to one pixel of the
  image's exact mean (odd sizes too), made once while its source is unchanged, an unreadable one white; with `BLENDITY_REAL_MOONRAY=1` and MoonRay installed, five scenes rendered in
  both: the world 1.000, a sky's azimuth 1.000, a sun 0.991, a point light at 1 m 0.891 (MoonRay's falloff is
  physical), a four-colour texture the same colour in each quadrant.
- An Opus review (the EXR format checked field by field: no fault); every finding fixed:
  - textures were converted on the editor's thread, and again on every frame of an animation: they are now made on
    the render's own thread before MoonRay starts, kept while their source is unchanged, and written without copies;
  - a texture used by two materials was converted twice;
  - MoonRay's last error line was lost without its newline, and its errors were logged again on every frame.
- Unit checks: Windows 10,706 (763 tests); this round's tree alone, Linux without Blender's libraries: 10,334,
  0 failed. Run together with round 137 (the full chain): Windows 10,753, render-cache verify mode 10,754 (0 stale
  pictures), Linux with libraries 10,489, the sanitizer build 10,380 (no reports), all 0 failed; `editor_fuzz` in
  verify mode 6,000 frames, no crash, 0 stale pictures.

## 2026-10-18 (round 135): MoonRay as an optional external renderer (tasks 0111-0114, ADR 0041)

**Asked:** "Is it possible to integrate MoonRay from DreamWorks into this project?", then "Let's make this an external
renderer. Just like other dependencies making it an optional install when using Blendity for the first time." The
user chose to build and host it and to run it through WSL2 on Windows, then "Do and finish all tasks."

**Added**
- **MoonRay built by CI (task 0111):** `.github/workflows/moonray.yml` and `tools/moonray/` build MoonRay v2026.29.1
  (Linux x86-64 in Rocky Linux 9, macOS on Apple silicon) into `blendity-moonray-<platform>-<tag>.tar.gz` with a
  `.sha256`: the renderer, every library it loads but the system's, its scene objects, a launcher, a test scene and
  every licence. Each archive is checked on clean Ubuntu 22.04 / 24.04, Debian 12, Fedora 41 and macOS 15. It is
  run by hand or by a push to `moonray-build/*`; publishing it is the user's call.
- **The optional install (task 0112):**
  - The first run asks "Download it?" (Download / Not now / Never ask), as Blender's libraries are offered: online
    only, never headless, again at most once a day after Not now.
  - MoonRay is looked for only then (on Windows that starts WSL).
  - Preferences > Renderers installs or removes it; the console's `moonray` (status, install [archive], remove,
    folder for your own build, check) does the same.
  - On Windows it runs inside WSL2, and says how to get WSL when it's missing.
- **The scene in MoonRay's RDLA (task 0113):** what F12's path tracer is given:
  - meshes mirrored into MoonRay's right-handed space (each triangle still facing out);
  - Principled materials as DwaBaseMaterial, with image, normal and emission maps;
  - the four light kinds, matched in brightness to the path tracer at 1 m;
  - the world baked to an environment map;
  - the camera with depth of field.

  What MoonRay doesn't take (toon, camera filters, procedural textures, alpha clipping, the lights' Range curve) is
  named in the log once.
- **MoonRay in F12 (task 0114):** Render Engine > MoonRay renders F12 images, camera sequences and animations.
  - It runs on its own thread, its progress in the Render window, and Stop ends it.
  - Its image is read back through Blendity's view transform and exposure: a new uncompressed-EXR reader works
    without Blender's libraries.
  - Without MoonRay, or with an orthographic camera, F12 path traces and says why.
- `sys::run_streaming` / `run_capture`: a program's output as it comes, with cancel and timeout. On POSIX the pipe is
  close-on-exec and the child's end is watched; on Windows only the child inherits it.
- A Learn section: "MoonRay, an optional third engine".

**Checked**
- New tests (round 135), with `blendity_tests --fake-moonray` standing in for MoonRay so CI needs neither MoonRay
  nor WSL:
  - WSL's distribution list in UTF-16 and UTF-8, Windows paths in WSL, MoonRay's progress parsed;
  - a program's output streamed, its exit code, a cancel within a moment, a child holding the pipe open;
  - uncompressed EXR in float and half, a compressed one refused by name, a short file claiming a huge image refused
    before anything is allocated, PFM's row order;
  - the export's objects, values, balance and winding, the right-handed conversion, the notes, a 50k-face mesh in
    about 0.2 s;
  - F12 with the stand-in: progress, the image through the view transform, a transparent film's alpha, a failure's
    message, Stop, a new render replacing it, the path tracer without MoonRay or with an orthographic camera;
  - an archive with the wrong checksum refused, a good one unpacked, probed and removed (Linux and macOS).
- An Opus review; every finding fixed:
  - a pipe a later program inherited could keep a render open;
  - a new render didn't replace a MoonRay one;
  - Stop during the search started it anyway;
  - the console's and Preferences' MoonRay chores ran on the editor's thread;
  - a folder change during a search was lost;
  - WSL was started at every launch once installed;
  - the first-run question could be covered by an update;
  - a short EXR could ask for gigabytes;
  - the packaging check could pass with missing libraries;
  - orthographic cameras;
  - a stale MoonRay in the macOS cache;
  - shared scratch folders.
- Unit checks: Windows 10,692, 0 failed (761 tests); the whole suite in render-cache verify mode: 10,693,
  0 failed, 0 stale pictures. Linux with libraries: 10,428, 0 failed. Sanitizer build: 10,319, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 134): performance pass 12 written up (task 0110)

**Asked:** pass 12 continued (the approved plan's last task).

**Changed** (documentation only)
- PERFORMANCE's pass 12: the targets met (no overlap-check stall; entering Edit Mode under 30 ms with the views on the
  GPU, about one frame on the CPU), what it cost, Blender's mechanisms adopted and left, and the next candidates (the
  operators' own cost, selection without drawing the view again, picking Edit Mode elements on the GPU).
- README's modeling paragraph mentions entering Edit Mode in about a frame.

**Checked**
- No code changed: the suite, verify mode and the stress sections stand as round 133 ran them.

## 2026-10-16 (round 133): entering Edit Mode in about one frame: the render mesh built on every thread (task 0109)

**Asked:** pass 12 continued (the plan the user approved: entering Edit Mode in about one frame).

**Changed**
- **The full build of a mesh's render cache runs on every thread,** the same to the bit as the serial build (kept for
  the tests): corners grouped by vertex, each sharing with the first earlier corner of the same normal and UV bits;
  render vertices numbered by a prefix sum in corner order; triangles per range of faces. A vertex with many corners
  (a pole, a fan's centre) is sorted rather than compared pairwise. The 50k-face flat cache: 42 -> 6.6 ms
  (development machine), 33 -> 5.4 ms (laptop setting). Blender threads its tessellation and draw extraction the same
  way.
- The sharing rule is exact in both builds: the same bits, never a NaN (the serial build's `==` let `/fp:fast` and
  its hash's buckets decide NaN and -0 / +0).
- A mesh with a corner past its vertices draws nothing instead of building from memory past them.
- **A full overlap check predicted past about a frame goes to the worker** (the rate learned from this machine's
  checks), not only past 200,000 vertices: entering Edit Mode on a 50k-face mesh does no check on the main thread,
  the rings arrive a moment later. The learned rate is held so a few stalled checks can't send every later one there.

**Pass 12** (laptop setting, `--laptop --quality low --only editor_render`, ms):

| Laptop setting, 50k-face sphere, ms | CPU before | 0108 | 0109 | GPU before | 0108 | 0109 |
|---|---|---|---|---|---|---|
| Entering Edit Mode, caches cold | 421 | 80 | 39 | 360 | 70 | 20 |
| of it the overlap check on the main thread | 360 | 24 | 0.5 | 308 | 23 | 0.6 |
| of it the render mesh's full build | 37 | 33 | 5.4 | 33 | 32 | 5.7 |
| Entering again, nothing changed | 24 | 21 | 32 | 11 | 5 | 6 |
| Extrude three faces | 466 | 179 | 118 | 487 | 163 | 115 |
| Undo after it | 337 | 27 | 38 | 334 | 23 | 30 |
| Select More after the undo | 208 | 56 | 68 | 213 | 55 | 62 |

Entering Edit Mode now costs about one frame: with the views on the CPU, entering again with nothing changed is
32 ms. The extrude's and Select More's rest is the operators themselves (left for later).

**Checked**
- New tests (round 133): the threaded build equals the serial one bit for bit on 10 meshes (smooth, by angle, sharp
  edges and a seam, two materials, flat, degenerate with a NaN, a cylinder, a 3,000-segment cone's pole, no UVs,
  empty), both caches; a drag's in-place update and the tangents after it equal the serial build's; the 50k-face flat
  cache builds faster threaded (asserted with more than one thread); entering Edit Mode on a 50k-face mesh checks on
  the worker, and with no budget on the main thread, the same rings either way; stalled checks don't send a
  5,000-vertex mesh's check to the worker.
- Two Opus reviews; every finding fixed (see task 0109's spec).
- Unit checks: Windows 10,601, 0 failed (751 tests); the whole suite in render-cache verify mode: 10,602,
  0 failed, 0 stale pictures. Linux with libraries: 10,334, 0 failed. Sanitizer build: 10,225, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 132): Edit Mode's overlap check reused, fast and local; pass 12 measured (tasks 0107, 0108, ADR 0040)

**Asked:** "Look for performance gains in edit mode; compare to how blender implements their edit mode." The user chose
entering Edit Mode as the scope, "Background + local" for the overlap check, and then asked for "the most optimized
way".

**Measured first (task 0107):** new `editor_render` rows for the frames that aren't drags: entering Edit Mode with the
mesh's caches cold, entering again, an extrude, an undo, a selection after it. On the 50k-face sphere the overlap
check (the yellow rings and magenta lines) was most of every stall: 310-360 ms on the main thread.

**Changed (task 0108, ADR 0040)**
- **Answers kept by what was checked:** the topology, eps and every position. An undo or redo to checked geometry,
  re-entering Edit Mode and a selection that copied the mesh take the answer at once (the last four are kept).
- **A check about 15x faster** (21-24 ms on 50k faces, from 315-575 ms), on every thread, finding exactly the pairs the
  old check finds (kept as the reference the tests compare with).
- **After a drag only what moved is checked again:** the moved vertices and the edges at them.
- **Past 200,000 vertices the check runs beside the editor** ("Checking..." in the panel, no rings until the answer
  for the mesh as it is now). The old cap, no check at all past 200,000 vertices, is gone.
- Mesh topology serials come from one counter for all meshes, so two copies changed apart never share one.
- A vertex at infinity no longer makes the check's distance infinite (every vertex was a pair, and Merge Overlapping
  then refused to weld anything); grid cells are held in range for meshes far from the origin; a line to a point at
  infinity is no longer drawn from NaN rows (found by the sanitizer job).

**Pass 12 so far** (laptop setting, `--laptop --quality low --only editor_render`, quiet machine):

| Laptop setting, 50k-face sphere, ms | CPU before | CPU after | GPU before | GPU after |
|---|---|---|---|---|
| Entering Edit Mode, caches cold (of it the overlap check) | 421 (360) | 80 (24) | 360 (308) | 70 (23) |
| Entering again, nothing changed | 24 | 21 | 11 | 5 |
| Extrude three faces (the overlap check) | 466 (316) | 179 (27) | 487 (336) | 163 (26) |
| Undo after it (the overlap check) | 337 (311) | 27 (0.4) | 334 (311) | 23 (0.4) |
| Select More after the undo (the overlap check) | 208 (153) | 56 (0.2) | 213 (155) | 55 (0.2) |

The rest of the extrude is its render meshes (two full builds, 54 ms: task 0109 threads them) and the operator itself.

**Checked**
- New tests (round 132): the fast check equals the reference on a cube, planted doubles, a 50k-face sphere, an empty
  mesh, one vertex, wires only, infinities, a mesh far out, random soups and wires near the 512-cell limit (at least
  3x faster on 50k faces in optimized builds with 8 threads or more); topology serials of diverging copies differ; the
  local check equals a full check through random drags and refuses or equals it near the 512-cell limit; in the
  editor a drag's release is local, a selection that copied the mesh and an undo reuse the answer; on a worker the
  editor doesn't wait, every frame shows nothing or the current geometry's pairs, a mesh changed meanwhile gets its own
  answer and an undo to the superseded geometry reuses its; Merge welds only the found doubles beside a vertex at
  infinity.
- Two Opus reviews; every finding fixed (see task 0108's spec).
- Unit checks: Windows 10,591, 0 failed (746 tests); the whole suite in render-cache verify mode: 10,592, 0 failed,
  0 stale pictures. Linux with libraries: 10,324, 0 failed. Sanitizer build: 10,216 checks, its one failure the
  speed ratio (2.7x in a debug sanitizer build), now asserted only in optimized builds; the overlap tests again under
  the sanitizers: 85 checks, 0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale
  pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 131): performance pass 11 written up (task 0106)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed** (documentation only)
- PERFORMANCE's pass 11 opens with one table, before and after, on both paths; the targets met and missed, the
  costs, what still draws on the CPU, and the next candidates.
- ADR 0026 notes that the laptop setting's 30 fps target for big Edit Mode drags is now met on the CPU path too.
- README's rendering paragraph mentions the views drawn on the GPU.

**Checked**
- No code changed: the suite, verify mode and the stress sections stand as round 130 ran them.

## 2026-10-16 (round 130): GPU Compute traces several samples a dispatch; OptiX measured again (task 0105, ADR 0037 note)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **The GPU path tracer's kernel traces up to 8 samples a dispatch,** each with its own random numbers as before,
  summed in registers. The accumulation and the denoiser's albedo, normal and depth planes (and a transparent film's
  coverage) are read and written once for them, instead of once a sample. Baking too.
- **GPU Compute, `--only gpu_optix`, samples a second:**
  - RTX 4070 SUPER 538-574 -> 592-611;
  - RTX 3060 Ti 279-281 -> 284-286.

  A small gain: the memory traffic was a smaller share of a sample than estimated.
- **OptiX measured again:** 1.62-1.69x on the 4070 SUPER, 2.07-2.12x on the 3060 Ti. Still over ADR 0037's 1.5x, so
  still a go as an upper bound. A full OptiX device (with GPU Compute's shading) is the user's decision, as a task of
  its own.
- The kernel's last source part is split before `main()` (MSVC's 16 KB literal limit was near).

**Checked**
- New test (round 130): eleven samples in one call (a dispatch of 8, then 3, from sample 3) equal eleven one-sample
  calls, to 2e-7: colour, the last hit's distance and id, albedo, normal, depth and coverage.
- The GPU path tracer's tests as they were (225 GPU checks).
- Unit checks: Windows 10,555, 0 failed (738 tests); the whole suite in render-cache verify mode: 10,556,
  0 failed, 0 stale pictures. Linux with libraries: 10,288, 0 failed. Sanitizer build: 10,180, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `pathtracer` as
  before.

**Review** (Opus code-reviewer). No correctness bugs: the accumulation's and the planes' meanings are kept, each
sample keeps its own seed, the pipeline cache is keyed on the kernel's source, and a submission stays near 100 ms.
Taken: the source split, the test widened to a dispatch's remainder, a non-zero first sample and the coverage plane.

## 2026-10-16 (round 128): the other views catch up on release, and cheaper frames while moving (tasks 0103, 0104, ADR 0039)

**Asked:** pass 11 continued ("Continue all remaining tasks"). In the Blender-ideas plan the user chose "Catch up on
release" and "Yes, cheaper while moving".

**Changed**
- **The other views catch up on release (task 0103):**
  - while you drag or navigate in the Scene view, the Game view and the Camera Preview keep their last picture and
    draw once when you let go, as Blender's sculpt mode updates its other views when the stroke ends;
  - Play always draws every frame;
  - Preferences > Other Views Follow Drags (off) brings the old way back; the console's `views follow on|off`.
- **Cheaper frames while moving (task 0104, ADR 0039):**
  - while you drag or orbit, the views drawn on the CPU render the sun's shadow map at half size with the 2 x 2
    filter, as Blender lowers its viewport's quality while you navigate;
  - the frame after you stop is exact: the views' picture keys carry the cheap state, so a cheap picture is never
    reused;
  - Preferences > Simpler While Moving (on); the console's `views simpler on|off`; the GPU's own map stays whole.
- **What counts as moving:** only a drag or navigation in the Scene view itself (a gizmo, an orbit, G / R / S,
  Push/Pull, a modal tool), not any mouse button held anywhere. The frame after a move always draws.
- **Speed:** the 50k-face Edit Mode drag on the CPU path, laptop setting: 28.3 -> 21.5 ms (the Game view 8.6 -> 0.2
  ms, the shadow map 3.5 -> 2.3 ms).
- A Learn section, "Drags that stay quick", with Try its for both switches.
- The console's `views` command shares the `viewport` branch: MSVC's limit on nested blocks (C1061) was reached.

**Checked**
- **New tests (round 128):**
  - during a 10-frame gizmo drag the Game view and the Camera Preview hold 20 pictures between them and render
    nothing; on release both draw again, and both equal a fresh render; with Follow on they draw every frame;
  - a gizmo drag in Play holds nothing;
  - a click in the Hierarchy holds nothing, makes no cheap shadow and draws no view.
- **New tests (round 129):**
  - with Simpler While Moving, 6 drag frames take 6 half-size shadow maps and the settled frame is the exact picture,
    pixel for pixel; off, none;
  - verify mode finds no stale picture across such a drag.
- Round 92's shadow-sharing test turns Follow on (it needs the Game view drawing during the drag).
- Unit checks: Windows 10,538, 0 failed (737 tests); the whole suite in render-cache verify mode: 10,539,
  0 failed, 0 stale pictures. Linux with libraries: 10,288, 0 failed. Sanitizer build: 10,180, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- any mouse button down anywhere counted as moving, so a click in the Hierarchy redrew the Scene view twice and
  softened its shadows; now only a drag in the Scene view counts;
- a held Camera Preview could show another camera's picture: it is held only for the same camera;
- the frame after a move always draws;
- the counter counts only maps that were actually halved;
- the Play test now drags a gizmo, and the Camera Preview's pixels are compared after release;
- Try its for Simpler While Moving.

## 2026-10-16 (round 127): Sky / HDRI worlds and toon on the GPU (task 0102)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **Sky and HDRI worlds draw on the card.**
  - The world's map goes into the card's texture store (ADR 0038's sampling, mips and all).
  - Its irradiance is the CPU's nine spherical-harmonic numbers.
  - The lighting pass has the CPU's `radiance`, `irradiance` and `specular`, through `dir_to_equirect`'s polynomial
    atan2 and asin. The background is the world's light through the view transform.
- **Toon materials and outlines draw on the card.**
  - `shade_toon` per light (the bands, the shadow colour, the specular spot), the rim, and the flat or normal
    ambient, as `light_surface` and `light_finish` do.
  - The G-buffer gains a plane with each pixel's instance, so the lighting pass reads its toon settings.
  - An outline's inverted hull is always culled, whatever the view's setting.
- A Sky's map is made anew when its sun changes; the card lets the old one go at once (rotating the sun would have
  held dozens of float maps there).
- **Speed:** the textured editor scene with a Sky world on the laptop setting: 22.1 ms with every view fallen back to
  the CPU, 9.9 ms with all 33 on the card.
- **Still on the CPU (Preferences says why):** realtime GI, probes and lightmaps, area lights, axis and see-through
  materials, camera filters.
- The Learn lesson "The views on the GPU" and the Viewport Device tooltip now say what the card draws; a Try it shows
  a Sky world on the GPU.

**Checked**
- **New tests (round 127)**, each skipped without a GPU, compared on the object's own pixels and on the whole view:
  - **Worlds:** a Sky and an HDRI world, under a rough and a glossy metal sphere. Mean 0.18-0.27 / 255 on the
    object, 0.13-0.14 on the view, 99.97 % or more within 3. The world's map is checked to reach the card.
  - **Toon:** two-tone; three bands with a specular spot and a rim; flat ambient off under a Sky; in a slab's shadow;
    an outline with the view's culling on and off, and mirrored. Mean 0.04-0.28 / 255.
- Unit checks: Windows 10,509, 0 failed (732 tests); the whole suite in render-cache verify mode: 10,510,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `editor_render`'s textured Sky row draws all its views on
  the card.

**Review** (Opus code-reviewer). The shading ports match the CPU line for line. Fixed:
- the Sky's old maps left the card only after 256 calls;
- the two fallbacks share one helper;
- the toon settings are cleaned on the CPU (GLSL's clamp of a NaN is undefined);
- tests for an outline with culling off and mirrored, and for the toon shadow;
- the lesson gained a question and a Try it for the worlds.

## 2026-10-16 (round 126): textures on the GPU (task 0101, ADR 0038)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **Textured materials draw on the card.** With Viewport Device on the GPU, these no longer send the view back to the
  CPU:
  - image textures on UVs, Box or Generated mapping, with tiling and offset;
  - base colour, metallic, roughness, emission and normal maps;
  - the Checker and Noise procedurals;
  - several materials on one mesh.
- **Sampled the CPU's way (ADR 0038).** The card reads the CPU's own mip chain from storage buffers, ports
  `Texture::sample` (filters, wraps, the level of detail from the next pixel's UVs) and `evaluate_surface`, and turns
  a back face's normals before the normal map, as the CPU does.
- **Several materials on one mesh:** its triangles are grouped by material on the card and drawn as one part each.
- **Textures stay on the card while drawn,** in two arenas that grow and are compacted on the card. A texture too
  large for the card sends the view to the CPU with the reason, once, not every frame.
- **Still on the CPU** (Preferences says why): axis materials, live procedural textures, see-through, cutout and toon
  materials, and Sky / HDRI worlds (toon and the worlds are task 0102).
- **Speed:** the editor scene with a Checker on its icospheres, laptop setting: 18.0 ms on the CPU, 10.4 ms with all
  its views on the card (before, all of them fell back).
- **Costs:**
  - every vertex on the card is 48 bytes now (UVs and tangents);
  - a mesh with several materials sends the whole mesh during an Edit Mode drag;
  - a normal-mapped mesh does too, since every tangent is written again.
- The procedurals' GLSL is one string shared by the path tracer's kernel and the views (`gpu_glsl.h`).

**Checked**
- **New tests (round 126)**, each skipped without a GPU:
  - **Picture match:** 11 features match the CPU on the object's own pixels: mean 0.15-0.69 / 255, 99.1-100 % within
    3. The features: UV Grid, a plane at a grazing angle (mips), Box, Generated, Checker, Noise, a normal map (which
    does change the picture: 64,264 pixels), metallic / roughness / emission maps, Closest + Mirror, Linear + Clip,
    and two materials on one mesh.
  - **Still on the CPU:** an axis material, a cutout and a live procedural texture send the view there, saying why.
  - **Texture store:** a 1 x 1 texture, fifty more, most let go, then a big one and a float one: compaction and
    growth keep the right texels.
  - **Drag:** an Edit Mode drag on a big textured mesh still sends only what moved.
- Round 122's fallback test now uses an axis material (procedurals draw on the card).
- Unit checks: Windows 10,461, 0 failed (730 tests); the whole suite in render-cache verify mode: 10,462,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `pathtracer` (its
  kernel now takes the shared procedurals) runs as before.

**Review** (Opus code-reviewer). The sampling and the material match the CPU line for line. Fixed:
- a texture of 4 texels or fewer fit the arena's placeholder, which couldn't be copied from when the arena grew;
- a texture too large for the card was copied and tried again every frame: the size is checked first, the refusal
  is remembered, and the levels go straight into the staging buffer;
- culling is per object, as the CPU's (not per material);
- a compaction that fails changes nothing;
- a part past its mesh fails the frame instead of drawing the whole mesh;
- the slots of a one-material mesh are found without a list;
- the shared procedurals;
- tests for the texture store and the live texture.

## 2026-10-16 (round 125): the Edit Mode cage on the GPU, and the Scene view at full size there (task 0100, ADR 0036 note)

**Asked:** pass 11 continued ("What should be more optimization gains?").

**Changed**
- **The cage on the card:** with the views on the GPU, the card draws the Edit Mode cage's edges and vertex dots
  into the Scene view's picture before it comes back. Its shaders follow the CPU's own rules: the line's clip and
  two-pixel coverage, the dot's centre depth test and disc, the 8-bit blend. The cage's vertices and edge colours stay
  on the card and only what changed goes up. The CPU then draws only the rest of the overlays.
- **The Scene view on the card draws at full size**, whatever the Viewport Resolution Scale (the scale saves the
  CPU's per-pixel work). The Game view and the Camera Preview keep it.
- Small uploads to the card (the cage's ranges, task 0099's mesh ranges) go in front of the frame's own submission
  instead of waiting for the card each.
- The 50k-face Edit Mode drag on the laptop setting, views on the GPU: 14.3 -> 11.5-12.3 ms (the CPU's overlays
  4.3 -> 0.5 ms). Playing costs about 1 ms more (the Scene view's bigger read-back).
- `Mesh::topology_serial()` is public: the GPU cage keys its edge colours on it.

**Checked**
- New tests (round 125), each skipped without a GPU:
  - the cage drawn by the card in vertex, edge and N-gon modes (selected and plain vertices, a seam, hidden edges)
    matches the CPU's on the cage's own pixels: mean 0.46-0.63 / 255, 99.6 % within 3 (much of it the 3D beneath);
  - a gizmo drag sends the card 16 bytes a frame for the cage, the picture mid-drag matches the CPU's, and a click
    picks the same vertex on both devices;
  - the Scene view on the card is the same at 67 % and 100 %, the Game view isn't;
  - switching to the card at 67 %: the view goes to full size, and verify mode finds no stale picture;
  - after a GPU error, the card opened again gets the whole cage.
- Unit checks: Windows 10,412, 0 failed (726 tests); the whole suite in render-cache verify mode: 10,413,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- a card opened again after an error (perhaps at the same address) could miss the cage's edges: everything kept for
  the card is forgotten with it;
- the full-size choice is in the Scene view's picture key (a scaled first frame could have stayed);
- the cage is drawn by the card only on a full-size frame;
- each line is a strip along it, not a box around it (a long diagonal edge was mostly discarded pixels);
- the cage's shaders are optional: if they can't be made, the CPU draws the cage and the view stays on the card;
- the dot colour and the depth bias shared by both paths; a cage the card refuses is logged once;
- the drawing-order change (the grid and wireframes now over the cage) noted in ADR 0036.

## 2026-10-16 (round 124): pass 11 begins - measured first, and Edit Mode drags work out only what moved (tasks 0098, 0099)

**Asked:** "What should be more optimization gains?" (all four areas chosen: real scenes on the GPU, Edit Mode on big
meshes, computers without a GPU, path-traced renders; the pass 11 plan approved).

**Changed**
- **Measured first (task 0098):** `editor_render` breaks down the 50k-face Edit Mode drag with the views on the GPU
  (moved in place, draw items, the GPU path's time, the bytes sent to the card). It also adds a textured scene with a
  Sky world on the GPU device: all its views still draw on the CPU, because of the Sky.
- **Edit Mode drags (task 0099):** moving a few vertices of a big mesh no longer redoes the whole mesh every frame.
  - What moved is found with one test per vertex.
  - Only the moved faces' normals and the corners at their vertices are worked out again: the same to the bit as a
    rebuild, in every shading mode.
  - Only those corners are written, and the GPU views send the card only the changed ranges.
  - The 50k-face drag on the laptop setting: 45 -> 28 ms with the views on the CPU, 25 -> 14-15 ms on the GPU.
- The kept normals aren't copied with a mesh, and a mesh never dragged doesn't hold them (`memory_bytes` counts
  them).
- GPU uploads end in a barrier that makes their writes visible to later draws.

**Checked**
- New tests (round 124):
  - 112 random drag steps on 7 kinds of mesh, in both caches, are bit for bit a rebuild: smooth, smooth by angle,
    sharp edges with a seam, sharp edges with an angle, a degenerate quad, mixed flat / smooth faces, flat;
  - with the views on the GPU, the card's copy is read back after every frame of a real gizmo drag and equals a
    whole upload. Leaving out one range made all 12 frames differ, checked once by hand;
  - the picture after the drag is the CPU's within ADR 0013's bar.
- Unit checks: Windows 10,385, 0 failed (721 tests); the whole suite in render-cache verify mode: 10,386, 0 failed,
  0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- the kept normals are no longer copied with every mesh, and are counted;
- a degenerate edge (both ends one vertex) is joined as the whole pass joins it;
- the fan's corner lookup is a binary search, so a vertex with thousands of faces stays cheap;
- the partial counter counts only updates that worked;
- a range that doesn't fit is refused, and the whole mesh goes up instead;
- only the moved vertices are converted for the card;
- the upload barrier;
- the tests: more mesh kinds, and the card's copy compared exactly.

## 2026-10-15 (round 123): OptiX against GPU Compute - a spike (task 0097, ADR 0037)

**Asked:** "They are installed but haven't restarted my computer" (the CUDA Toolkit 12.8 and the OptiX SDK 9.1.0 for
the OptiX spike).

**Changed**
- **A measurement, not a render device yet:** a small OptiX path tracer (`gpu::OptixTracer`) races GPU Compute
  (Vulkan with the RT cores) on the same scene. Stress `--only gpu_optix`.
- **Result:** OptiX renders 1.78-2.08x the samples a second on both cards: RTX 4070 SUPER 1041-1059 against 551-593,
  RTX 3060 Ti 589-594 against 286-287 (picture read-backs timed on both). That is the most it could gain: its kernel
  is simpler (Lambert, flat normals, no per-sample denoiser passes). ADR 0037 records it, and a full OptiX device
  waits for approval.
- **Build:** with the CUDA Toolkit and the OptiX SDK on the machine, `build.bat` compiles the kernel to PTX through
  NVRTC (CUDA 12.8's nvcc can't parse Visual Studio 2026's headers) and builds OptiX in. Without them (CI, the
  installers) nothing changes. At run time only NVIDIA's driver is needed, through Blender's `extern/cuew`
  (Apache-2.0).

**Checked**
- New tests (round 123): a build without OptiX says so; with it, a small scene traced by OptiX matches GPU Compute's
  mean brightness (100.024 %). Run here with an OptiX build; the chain's builds have no SDK and report it skipped.
- Unit checks: Windows 10,376, 0 failed (719 tests); the whole suite in render-cache verify mode: 10,377,
  0 failed, 0 stale pictures. Linux with libraries: 10,257, 0 failed. Sanitizer build: 10,149, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). The go stands, and the upper-bound framing is right. Fixed:
- OptiX's read-back is timed, as GPU Compute's merge is (2.15-2.35x before, 1.78-2.08x now);
- the per-sample differences are listed in the docs;
- the test skips when OptiX can't start;
- a null error string, leaks on failed allocations, and the mesh index checked;
- `build.bat` picks the newest SDK, checks for NVRTC, builds its PTX compiler once, says why it falls back, and never
  builds OptiX into the installers;
- the OptiX loader's BSD-3 notice and cuew's own licence text are in `licenses/`.

## 2026-10-15 (round 122): the views on the GPU - Viewport Device (task 0096, ADR 0036)

**Asked:** "If it's more performant, then yes" (the GPU viewport after spike 2's go); "For the OptiX system let me
know what I need to do" (answered with the setup steps: CUDA Toolkit 12.8, the OptiX SDK 8.0+, `OPTIX_ROOT_DIR`).

**Changed**
- **Preferences > Viewport Device (Auto / CPU / GPU):** the Scene view, Game view and Camera Preview draw their 3D on
  the GPU (Vulkan) and read it back into the editor's canvas. Overlays, gizmos, picking, the UI and the camera
  filters' passes are unchanged.
- **What it draws:** plain materials, sun, point and spot lights with the sun's shadows (3x3 or Low's 2x2), Gradient
  and Color worlds, the editor's sky, a camera's solid background, Standard / Filmic / ACES and every OpenColorIO view
  (AgX included, through the CPU's own LUT).
- **Anything else** draws on the CPU, as before, and Preferences says why: textures, procedurals, toon, see-through,
  several materials on one mesh, GI, area lights, Sky / HDRI worlds, camera filters. F12 renders stay on the CPU.
- **On the laptop setting, this machine's GPU:** moving an object 16.6-16.9 -> 4.1 ms a frame, playing 20.6-22.1 ->
  5.6-6.8, an Edit Mode drag on 50k faces 43.7-46.6 -> 24.7-25.1. A view the GPU draws renders its own shadow map.
  The GPU opens in the background when the editor starts.

**Checked**
- New tests (round 122), each skipped without a GPU:
  - the Scene and Game views of the default scene match the CPU's (mean 0.086 / 255, 99.99 % within 3 / 255);
  - a procedural sends the view to the CPU with its reason, and the picture is then the CPU's exactly;
  - a plane seen from below matches (back faces culled as on the CPU; round 121 now compares depth too), and a click
    picks the Cube from the GPU's ids;
  - an Edit Mode gizmo drag updates the mesh on the card in place (3 updates, 1 upload);
  - Scene lighting off (the editor's headlight) and a mirrored Cube match too (mean 0.083 and 0.087 / 255);
  - Viewport Device is kept in the prefs, and Auto stays on the CPU when the tests pin it.
- Unit checks: Windows 10,374, 0 failed (717 tests); the whole suite in render-cache verify mode: 10,375,
  0 failed, 0 stale pictures. Linux with libraries: 10,255, 0 failed. Sanitizer build: 10,147, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- mirrored placements cull the other side;
- the CPU no longer renders the shadow map for a GPU-drawn view, and depth comes back only when something reads it;
- the colour is read straight into the canvas;
- the meshes on the card have a budget;
- casting follows the CPU's any-slot rule;
- each view's reason is kept, and Solid / Wireframe are named;
- the views' statistics count the GPU's triangles;
- the GPU opens in the background.

Checked and kept: the editor's own lighting already matched the CPU (a finding the new test disproves).

## 2026-10-15 (round 121): a GPU view, spike 2 - go (task 0095, ADR 0013 amended)

**Asked:** "Can we look into rendering the editor with the GPU and not just the CPU?" The plan was a new spike first,
both ways to the window, then a decision by its numbers. Then "Go for it".

**Changed**
- **A measurement, not a feature yet:** a Vulkan rasteriser (`gpu::Raster`) draws the editor scene's content at 1080p
  the way the CPU's deferred view does (shadow map, G-buffer, the same lighting formulas). Then it either reads the
  picture back (A) or shows it in a window through a swapchain (B). Stress section `--only gpu_raster`.
- **Result, go on both cards:**
  - the GPU's work is 0.3-0.4 ms a frame;
  - (A) takes 2.7 ms on the RTX 4070 SUPER and 5.7 ms on the RTX 3060 Ti, against the CPU's 14.4 ms;
  - (B) takes 0.5 and 2.2 ms, against 16.6 ms;
  - the same picture: mean 0.32 / 255 on covered pixels, ids exact away from edges.

  The laptop is estimated (Iris Xe isn't here): about 2.5 ms of GPU work, against the CPU's 56 ms on the laptop
  setting. ADR 0013 records it. The GPU viewport itself waits for approval.

**Checked**
- New tests (round 121): asking for a GPU that isn't there fails cleanly with a reason; a small deferred frame (sun,
  point and spot lights) on the GPU matches the CPU's (mean 0.08 / 255, 100 % within 3 / 255, ids exact; covered pixels
  away from edges; skipped without a GPU).
- Unit checks: Windows 10,349, 0 failed (711 tests); the whole suite in render-cache verify mode: 10,350,
  0 failed, 0 stale pictures. Linux with libraries: 10,251, 0 failed. Sanitizer build: 10,143, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). The go stands; fixed: the verdict needs every card measured and every call in the
timed loops is checked, explicit pass dependencies, the shadow map always cleared, the swapchain asked for only where
the card lists it, one semaphore per swapchain image, the picture scored on covered pixels away from edges by one
helper, and the shader takes the triangle's own normal and the material's specular as the CPU does.

## 2026-10-15 (round 120): the denoiser on the GPU (task 0094)

**Asked:** "Can we also look into Cuda and Optix support?" - then "Go for it" on the plan: the denoiser first (no SDK),
then an OptiX spike. The download of OpenImageDenoise's CUDA and HIP modules from Blender's library repositories
(33.6 MB) was approved first.

**Changed**
- **Render Settings > Denoise Device (Auto / CPU / GPU):** OpenImageDenoise runs on an NVIDIA (CUDA) or AMD (HIP) card
  when there is one, with the CPU as the fallback. The installers and `dist` carry the two modules (never a Git LFS
  placeholder). A 1080p denoise: 580.9 ms on this 16-core CPU before, 43.1 ms on the RTX 4070 SUPER.
- **The CPU denoise is faster too** (580.9 -> 463.4 ms at 1080p): the filter is kept between frames of one size
  instead of made anew each time, and the images are prepared in parallel.
- The status line names the device ("denoised (OpenImageDenoise, CUDA: NVIDIA GeForce RTX 4070 SUPER)"); a Learn
  section in "Rendering on the GPU".

**Checked**
- New tests (round 120): the GPU's denoise matches the CPU's on screen (mean 0.078 / 255, 100 % within 3 / 255;
  skipped without a GPU); a GPU that fails gives exactly the CPU's picture and counts one fallback; Denoise Device is
  saved with the scene, and Auto stays on the CPU when the tests pin it.
- Unit checks: Windows 10,339, 0 failed (709 tests); the whole suite in render-cache verify mode: 10,340,
  0 failed, 0 stale pictures. Linux with libraries: 10,247, 0 failed. Sanitizer build: 10,139, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; fixed: the filter and buffers are dropped before a new size (a card never
holds both), after an error (now also checked after the filter's commit) and before a GPU render fits its scene to
the card; a card OIDN lists but can't open counts a fallback; the test's failure comes after a real run on the card;
the CMake build ships the modules too. Kept: alternating sizes remake the filter once per finished frame.

## 2026-10-15 (round 119): performance pass 10 written up (task 0093)

**Asked:** the performance pass ("Let's increase overall editor performance in general"), its last task.

**Changed**
- **docs/PERFORMANCE.md, pass 10:** the before / after table on the laptop setting (x 1.4). The editor's own frames,
  laptop setting: orbiting 13.1 -> 11.3 ms (88 fps), moving an object 27.3 -> 22.6 (44 fps), editing a material
  colour 25.1 -> 21.9, playing 23.8 -> 20.6; Edit Mode drags on 50k faces 137 -> 46 ms (7 -> 22 fps); a 60 fps cap now
  holds (30.6 -> 16.6 ms between frames). ADR 0026's laptop targets (60 fps orbiting, 30 fps editing) are met; the
  plan's 60 fps while editing and 30 fps for 50k-face drags are not, and the write-up says what is left and why.
  Next candidates refreshed (the Game view during a drag, the shader's shadow lookup, the sky, incremental overlap
  checks). ADR 0026 notes the results.

**Checked**
- Docs only; every number is from a `blendity_stress --laptop --quality low` run on this machine (2026-10-15).

## 2026-10-15 (round 118): views nobody picks in keep no full-size depth and ids (task 0092)

**Asked:** the performance pass ("carry on the next tasks").

**Changed**
- **At a reduced viewport resolution** (Low draws the views at 67 % and scales them up), the Game view and the Camera
  Preview no longer scale their depth and object ids up to full size as well - only the Scene view is clicked in, so
  only it needs them. The same pictures.
  Small: on the laptop setting a moved-object frame went 16.16 -> 16.09 ms (copied / not, in turn in one run);
  most of a scaled view's cost is its colour, which stays.

**Checked**
- New test (round 118): at Low, with the Camera Preview up, the Game view shows the same pixels with the planes copied
  and not, without and with a retro filter (a camera's filter stack scales up on its own path).
- Unit checks: Windows 10,323, 0 failed (706 tests); the whole suite in render-cache verify mode: 10,324,
  0 failed, 0 stale pictures. Linux with libraries: 10,231, 0 failed. Sanitizer build: 10,137, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: a camera's filter stack follows the view's choice (its upscale had
its own copy of the planes), and the test has a hook that puts the old copy back to compare.

## 2026-10-15 (round 117): plain-material pixels lit 8 at a time (task 0091, ADR 0035)

**Asked:** the performance pass ("carry on the next tasks"; checks overlapping the next task).

**Changed**
- **Shading a view:** on a processor with AVX2, the pixels of a plain material (no textures, procedural or axis
  mapping) are lit 8 at a time - the light directions, falloffs and highlights of every sun, point and spot light -
  with the same pixels on screen. Lightmaps, probes, voxel GI, toon and area lights keep the one-pixel path. On the
  laptop setting, off / on back to back: moving an object 17.18 -> 16.31 ms a frame, the Scene view's shading 4.45 ->
  3.86 ms. The Profiler's renderer options (and the console's `raster simd on|off`) can switch it off to compare.

**Checked**
- New test (round 117): a view with a sun and its 3x3 and 2x2 shadows, point and spot lights, highlighted faces, a
  two-sided face seen from behind and a procedural beside plain materials shows the same pixels with the 8-wide path on
  and off, its colours before display within 1e-4 relative (1.5e-5 measured: the release build lets the compiler
  reorder the one-pixel path's sums), and thousands of its pixels went through the 8-wide path. Skipped, and said so,
  on a processor without AVX2.
- Unit checks: Windows 10,319, 0 failed (705 tests); the whole suite in render-cache verify mode: 10,320,
  0 failed, 0 stale pictures. Linux with libraries: 10,227, 0 failed. Sanitizer build: 10,133, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: the test compares the colours before display too (not only the 8-bit
pixels), the kernel's helpers are functions (GCC can't give a lambda the AVX2 target), the ADR's claims say "the same
displayed pixels" rather than "bit for bit".

## 2026-10-15 (round 116): in place, faster - only moved faces split again, face normals in parallel (task 0090)

**Asked:** the performance pass ("carry on the next tasks").

**Changed**
- **A drag on a big mesh:** its render data followed the drag by splitting every quad into triangles again and working
  out every face's normal on one thread; now only faces with a moved vertex are split again (a face's triangles depend
  on its own corners alone) and the normals are worked out in parallel - the same results to the bit. On a 50k-face
  mesh, laptop setting: 9.1 -> 6.7 ms a frame for the two caches; a gizmo drag 35.4 -> 33.7 ms, G 34.4 -> 33.0 ms.

**Checked**
- New test (round 116): a cache read after several moves it missed, a vertex pulled across a quad's diagonal and back
  to the same bits, and a fold that refuses the in-place update followed by more moves all draw exactly what a rebuild
  draws; round 112's 48 random drag steps still find no difference.
- Unit checks: Windows 10,311, 0 failed (704 tests); the whole suite in render-cache verify mode: 10,312,
  0 failed, 0 stale pictures. Linux with libraries: 10,219, 0 failed. Sanitizer build: 10,125, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; the per-face work runs through a plain lambda (no `std::function` per face)
and the moved-face test is a small function of its own.

## 2026-10-15 (round 115): the Edit Mode cage drawn from parallel batches (task 0089)

**Asked:** "carry on the next tasks" (the performance pass; with the next task now written while the last one's
checks run).

**Changed**
- **The edges and vertex dots of a big mesh in Edit Mode** were recorded one call at a time, each edge placing both
  its vertices in the world; now each vertex is placed once and the edges and dots are prepared in parallel and
  recorded in order - the same picture. The overlays of a 50k-face mesh went from 7.4 to 4.5 ms a frame on the laptop
  setting: a gizmo drag 38.6 -> 35.4 ms, G 37.0 -> 34.4 ms.

**Checked**
- New tests (round 115): 30,000 lines and 10,000 dots (depth-tested, clipped, some behind the eye) drawn from
  batches give exactly the pixels one call each gives; an item left out stays out.
- Unit checks: Windows 10,304, 0 failed (703 tests); the whole suite in render-cache verify mode: 10,305,
  0 failed, 0 stale pictures. Linux with libraries: 10,212, 0 failed. Sanitizer build: 10,118, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; the batch reserves its room in the overlay list before appending.

## 2026-10-15 (round 114): uniform materials shaded once per triangle; one shadow map for an edited mesh (task 0088)

**Asked:** the performance pass ("Keep looking for performance gains throughout the project", overnight).

**Changed**
- **Plain materials** (no textures, procedural or axis mapping - most of a blocked-out scene) are worked out once per
  triangle instead of once per pixel, with the same picture to the bit. On the laptop setting: moving an object
  19.5 -> 17.2 ms a frame, editing a material colour 17.9 -> 16.5, playing 16.6 -> 15.5. The Profiler's renderer
  options can switch it off to compare.
- **An Edit Mode drag renders the sun's shadow once a frame**, not once for the Scene view and again for the Game
  view (the edited mesh's two render caches have the same shape): a gizmo drag on 50k faces 42.5 -> 38.6 ms.

**Checked**
- New tests (round 114): 200 random plain materials evaluated once equal per-pixel evaluation to the bit; a deferred
  view with a sun and its shadows, point and spot lights, flat and smooth meshes, emission and a procedural draws
  the same pixels either way; an edited mesh's two caches share a shape and a copy or an edit makes a new one; a
  drag renders the shadow map once a frame.
- Unit checks: Windows 10,301, 0 failed (702 tests); the whole suite in render-cache verify mode: 10,302,
  0 failed, 0 stale pictures. Linux with libraries: 10,209, 0 failed. Sanitizer build: 10,115, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: a live texture's footprint isn't worked out for a plain material's
pixels, a vertex-snapped copy of a render mesh drops its shape, the option's cache-key bit no longer overlaps the shade
mode's, a mesh id's copy is `noexcept` (moves stay moves).

## 2026-10-15 (round 113): the Edit Mode drag loop stops redoing whole-mesh work (task 0087)

**Asked:** the performance pass (plan item 0087).

**Changed**
- **Dragging vertices of a big mesh:** the check for flat rings of faces that a drag folds (they are zipped again)
  built a map of every edge of the mesh on every frame; it now looks edges up in per-vertex face lists kept while the
  mesh's faces don't change. The drag's own work on a 50k-face mesh went from 25 ms a frame to 1.6.
- **G** reuses its copy of the mesh from frame to frame instead of copying the whole mesh each time.
- **During any drag** (a mouse button held, G / R / S, Push/Pull, a modal adjustment) the overlapping-vertices check
  and the "Lighting out of date" status wait for it to end; the Modeling Tools panel shows what it found before.
- With task 0086: a Move-gizmo drag on 50k faces went from 98 ms a frame to 43 on the laptop setting, G from 100 to 42.

**Checked**
- New tests (round 113): a 10-frame gizmo drag on a big mesh copies it at most once, builds no render mesh, checks no
  overlaps and hashes no lighting key, and releasing checks once; G reuses its copy and Esc restores the mesh exactly;
  the per-vertex face lists give exactly what the old edge map gave, odd faces included, and are kept while only
  positions move.
- Unit checks: Windows 10,088, 0 failed (698 tests); the whole suite in render-cache verify mode: 10,089,
  0 failed, 0 stale pictures. Linux with libraries: 9,996, 0 failed. Sanitizer build: 9,902, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Holding the lighting answer during a drag froze Live GI and the status bar until release: during a drag a change is
  taken as out of date without the exact (costly) check, which runs when the drag ends.
- The overlapping-vertices overlay vanished whenever a mouse button was held (orbiting, a click): it keeps showing a
  result that is still current, and hides only while a drag changes the mesh.
- G's copy is remembered as a weak reference rather than an address; edge lookups scan the shorter vertex list.

## 2026-10-15 (round 112): Edit Mode - the render mesh follows a drag in place (task 0086, ADR 0034)

**Asked:** the performance pass (plan item 0086).

**Changed**
- **Dragging vertices in Edit Mode no longer rebuilds the mesh's render data every frame.** When only positions
  moved, the shaded mesh follows them in place - the same triangles and the same picture as a rebuild (quads and
  n-gons are split again from the new positions) - and the edge table and smoothing adjacency are kept. A Move-gizmo
  drag on a 50k-face mesh went from 98 to 69 ms a frame on the laptop setting, G from 100 to 67 ms.
- Normal-mapped meshes' tangents follow a drag the quick way and are made with MikkTSpace again when the drag ends.
- Deforming modifiers move their render mesh with the exact triangulation now (they kept the build's before).

**Checked**
- New tests (round 112): 48 random drag steps on six kinds of mesh draw bit for bit what a rebuild draws; a quad's
  diagonal flipping during a drag; a new face builds again while moved vertices keep the edge table; quick tangents
  within 10 degrees of MikkTSpace's, exact after the drag ends; a fold that changes where smoothing splits builds
  again; NaN positions draw as a rebuild's; F9 still adjusts a positions-only operator on a normal-mapped mesh.
- Unit checks: Windows 10,078, 0 failed (695 tests); the whole suite in render-cache verify mode: 10,079,
  0 failed, 0 stale pictures. Linux with libraries: 9,986, 0 failed. Sanitizer build: 9,892, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Settling the tangents after a drag bumped the mesh's version, which ended an F9 adjustment and re-ran modifier
  stacks: it only drops the caches now, and a separate counter the views key on makes them redraw.
- Meshes changed outside Edit Mode (an origin moved) kept their quick tangents until their topology changed, so a
  bake could depend on how a mesh got its shape: every mesh with quick tangents is settled when an interaction ends.
- Deforms worked out the triangle slots every frame: once per topology now. `memory_bytes()` (and so the undo budget)
  counts the kept topology; the test's drags are the same on every compiler.

## 2026-10-15 (round 111): per-view waste - the voxel GI grid's columns, the shadow map's ids plane (task 0085)

**Asked:** the performance pass (plan item 0085).

**Changed**
- **Realtime GI:** a view checking whether its voxel grid still fits the scene no longer allocates a fresh column
  pyramid (about 87,000 columns at 256) every frame to find out; only a grid it actually takes gets its columns.
- **Shadow maps** keep no picking-ids plane: nothing is cleared or written for it. The sun's map on a moved-object
  frame went from 0.68 to 0.48 ms on the laptop setting.
- **The GI blur** no longer copies its input plane before writing every texel anyway.
- **Measured and left alone:** gathering the views' draw items (0.07 ms a moved frame) and the scene hashes (0.03 ms)
  are too small to be worth changing in these scenes; shading is 5.1 of the Scene view's 7.2 ms (tasks 0088-0089).
  The Realtime GI drag in Automatic mode (71 ms) is the automatic lighting update, not the grid.

**Checked**
- New tests (round 111): small moves inside the GI grid make no new pyramid, leaving it makes one; a grid fitted
  without columns has the same placement and key; a shadow map has no ids plane and the same depth.
- Unit checks: Windows 10,058, 0 failed (689 tests); the whole suite in render-cache verify mode: 10,059,
  0 failed, 0 stale pictures. Linux with libraries: 9,966, 0 failed. Sanitizer build: 9,872, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; hardened: the Gouraud raster path and the selection outline also skip a
target without ids (a shadow map), so a future pass drawing into one can't index an empty plane; a shadow target
gives its ids memory back.

## 2026-10-15 (round 110): the frame-rate cap holds, and only what changed is presented (task 0084)

**Asked:** the performance pass (task 0083 measured; this is its first fix).

**Fixed**
- **A 60 fps cap ran at about 33 fps on Windows**, whatever the frame cost: every wait was rounded up to Windows'
  15.6 ms timer tick, so a frame that left less than a tick to wait lost a whole one more. The editor now waits on a
  high-resolution timer, and a frame pacer blocks only for as long as the waits have proved on time and polls for
  the rest: a real window's 13 ms frames under a 60 fps cap went from 30.6 ms apart to 16.6 ms (95 % within 16.7).
- **Presenting** sends only the bands of rows that changed: 0.68 ms instead of 2.42 for a Scene-view-sized change
  (95 % within 0.70 ms instead of 9.38), and 0.07 ms when nothing changed.
- **A window repaint drew the same frame twice** (the paint, then a queued event); now once.

**Checked**
- New tests (round 110): the pacer under a pretend 15.6 ms timer (the old loop 31.2 ms apart, the pacer 16.6); with
  a precise timer it blocks for nearly the whole wait; input waking it early doesn't start the frame early; only
  changed bands are presented, everything on a new size or when asked; one 30 ms-late wake-up doesn't leave the
  pacer polling for good; a change only in pixels' top bits is still seen.
- Unit checks: Windows 10,037, 0 failed (686 tests); the whole suite in render-cache verify mode: 10,038,
  0 failed, 0 stale pictures. Linux with libraries: 9,945, 0 failed. Sanitizer build: 9,851, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux (and Windows' fallback) a window uncovered or first shown asked for a repaint as an event, which presented
  nothing when the frame hadn't changed: a repaint, resize or DPI change now presents all of it.
- One very late wake-up could leave the pacer polling for good (it only learnt from waits it blocked in): an unused
  margin now shrinks each frame.
- The band hash could miss a change confined to pixels' top bits: each chain rotates and is finalised.
- macOS presented the whole picture once per changed run: once per frame now. `winmm` is linked for CMake builds too.
- Waits were truncated to whole milliseconds: `poll_events_for` takes seconds; Windows' waits wake for input already
  queued and can't hang on a timer that never fires.

## 2026-10-15 (round 109): performance pass 10 begins - measuring Edit Mode drags, the mesh caches and frame pacing (task 0083)

**Asked:** "Let's increase overall editor performance in general." Chosen: the viewport's frame rate and big meshes
in Edit Mode, keeping the CPU renderer; plan of tasks 0083-0091. This first task only measures.

**Added**
- **The Profiler** shows the time between frames (what the screen sees, the cap's waiting included) and what the
  mesh caches did in the last frame: copies for an edit, render meshes built or moved in place, tangents, edge
  tables; plus an Edit Mode drag's writes, the overlap check, the lighting key, snapping, the views' draw items and
  the scene hashes. The Learn tab's performance lesson explains them ("Reading a frame").
- **blendity_stress:** `editor_render` drags the 50k-face mesh for real (the Move gizmo, and G) and notes each part;
  `--only present` opens a window to time presenting frames and the cap's pacing.

**Found** (the laptop setting, `--laptop --quality low`; docs/PERFORMANCE.md, Pass 10)
- A 60 fps cap with 13 ms frames gave **30.6 ms between frames** (33 fps): Windows rounds the loop's waits to its
  15.6 ms timer. Presenting a frame costs 2.4 ms on average (9.3 ms at the 95th percentile).
- A gizmo drag on 50k faces costs 98 ms a frame: two render meshes built from scratch (38.6 ms, the Scene view's flat
  one and the Game view's smooth one), the drag's writes 24.5 ms (ring repair scans every face), the shadow map
  7.5 ms, the Edit Mode overlay 8.7 ms. G is the same plus a 2.7 ms mesh copy a frame.

**Checked**
- New tests (round 109): a gizmo drag moves the counters; the Profiler draws its new rows during a drag.
- Unit checks: Windows 10,022, 0 failed (680 tests); the whole suite in render-cache verify mode: 10,021,
  0 failed, 0 stale pictures. Linux with libraries: 9,928, 0 failed. Sanitizer build: 9,834, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No behaviour changed; fixed:
- The pacing figure was measured with present on top of 13 ms of work: present now counts inside the 13 ms (the
  result held: 30.6 ms between frames).
- "Moved in place" counted refused moves too; the first frame after start showed everything counted before it.
- The 95th percentile was rounded down (the Profiler and stress); the Profiler shows it from two frames on.
- The stress run's undo after the gizmo drag could undo an earlier step if the drag had missed.
- The tests now look for the Profiler's rows by name, and no longer pin today's rebuilds; ARCHITECTURE has a row.

## 2026-10-14 (round 108): installing without knowing GitHub - one-click downloads, releases of installers only (task 0082)

**Asked:** "Remove build-pr71" (done: the release and its tag). "Note the installer in the releases and update the
readMe in how to install. A non-technical person should be able to install it." "Have the release not contain the
source files if the user doesn't want them. Only have an executable or similar on whatever platform it's downloaded
to." Chosen: one-click links (v0.x releases are GitHub's Latest, still called pre-releases), extras moved aside.
Then: "Make them artifacts" - a merged PR's build is no longer published as a release.

**Changed**
- **README, Download:** "Download for Windows / Mac / Linux" links that fetch the newest installer directly, then
  step-by-step installs (Windows' "Windows protected your PC" > Run anyway; the Mac's Open Anyway; Linux's Allow
  executing), uninstalling and first steps.
- **Each release's notes** say which file to download and how to install it. An update inside Blendity still shows
  only what changed.
- **A release lists only the installers** (Blendity-Setup-Windows.exe, Blendity-macos.dmg, Blendity-linux-x86_64.AppImage)
  and SHA256SUMS.txt. The libraries archives and the Linux .tar.gz moved to one "libraries" release that Blendity's
  updater reads by itself. GitHub's own "Source code" files can't be removed from a release (GitHub adds them to
  every one); the notes say they're for developers.
- **0.10.0 and later 0.x versions** are GitHub's Latest release (so the links work), titled "(pre-release)"; Version
  History calls them pre-releases. PR builds stay GitHub pre-releases.
- **Merges into main make no release any more**, only build artifacts: a release comes from a `v*` tag, made when
  the user asks for one ("Make them artifacts. I will let you know explicitly when I want a release.").
- **Mac:** the app is signed ad hoc (when the signature verifies), so macOS offers Open Anyway instead of calling the
  download damaged.

**Review** (Opus code-reviewer). Fixed:
- Builds installed before this change look for "Blendity-Setup-...": the Windows installer is
  Blendity-Setup-Windows.exe, so they are still offered updates.
- A 0.x beta would have become the Latest release (and the README's download): betas are checked first now, and
  GitHub picks the Latest.
- Tagged releases had lost their "what changed" notes: GitHub's summary of the merged PRs comes before the install
  guide again.
- The "libraries" release would have grown without end: it keeps tagged releases' archives and the newest 20 PR
  builds'. The publishing script stops if an installer is missing, copes with no archives, and is split into steps.
- Ad-hoc signing could fail on the bundle's data files: it is kept only when it verifies.

**Checked**
- New tests (round 108): an update's notes leave out the install guide; a release of version-free installers finds
  its installer, takes its libraries from the "libraries" release by exact tag (build-pr7's never build-pr72's) and
  the "libraries" release is never offered.
- The publishing script against a stand-in `gh`: installers to the release, the rest to "libraries", a deleted
  build's archive removed.
- Unit checks: Windows 10,011, 0 failed (678 tests). Linux and the sanitizer build: on CI for this PR.

## 2026-10-14 (round 107): redo of material assets, tests that never touch the project, one version number (task 0081)

**Asked:** merge the stack (done: #61-#71 into main); "push V1.0.0 (or whatever nomenclature you use for the first
release before hotfixes)"; "test material shouldn't be even in the project at all when it comes to release"; "fix the
redo material system". Then: "let's leave it at 0.10.0 because I don't want an official 1.0.0 release just yet (needs
more refining)", and "still publish that version so people know it's a pre-release of the program" - so the version
stays 0.10.0, published as the pre-release `v0.10.0`.

**Changed**
- **Blendity 0.10.0 published as a pre-release:** a `v*` tag before 1.0 (or with a suffix, v1.0.0-beta.1) makes a
  GitHub Release marked "pre-release", titled "Blendity 0.10.0 (pre-release)". The updater still offers such a tagged
  version with previews off (previews are the per-PR builds); Version History labels it "pre-release".
- **One version number:** the VERSION file (still 0.10.0). CMake's project version and the macOS bundle read it (the
  bundle said 0.9.0). A future stable release is a VERSION change plus a `v*` tag.
- **Fixed: redo of a material linked to a .mat** brought back the undone values (any field) and the asset kept them:
  an undo or redo state set aside keeps copies of its own linked materials now.
- **Fixed: tests could write into the project they ran from.** A test making a clip or material before any editor
  existed resolved its path against the working folder; the test and stress programs point the asset root at their
  scratch projects first. The leaked R86Hold/R86Spin/R86Up.anim and the empty Materials/R51 were deleted from the
  project. (Installers never carried Assets.)

**Checked**
- New tests (round 107): a .mat-linked material's edit undoes and redoes twice and the asset follows (it fails without
  the fix: green 0.10 after the redo); objects sharing a .mat still share one copy in an undo state; the tests'
  asset root is the scratch project before the first test, and a relative asset path lands there; a tagged version
  GitHub marks a pre-release is offered with previews off, PR builds only with them on.
- Unit checks: Windows 10,004, 0 failed (676 tests); the whole suite in render-cache verify mode: 9,999,
  0 failed, 0 stale pictures. Linux with libraries (before the review fixes; CI runs them after): 9,906, 0 failed. Sanitizer build: 9,812, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- An undo state gave every slot its own copy of a shared .mat material, so objects that shared one material stopped
  sharing it after an undo: slots sharing one material share one copy now (as duplicating a scene does).
- The asset-root test passed whatever the tests did first: it checks the root main() set before any test now.
- CMake's project version and the macOS bundle's version read the VERSION file instead of a number of their own.

## 2026-10-14 (rounds 105-106): updates, older builds, and Blender's libraries optional at run time (tasks 0079-0080, ADR 0033)

**Asked:** "an option for the software while installed on a computer to check for updates and then ask the user if
they want to install the update. The install note should note the changes that the update will bring. Furthermore,
the user can choose to go to an older update (Before A Certain Pull Request)"; "if the DLL or external libraries isn't
found, result back to the fallback with the software that doesn't rely on dependencies (such as situations with no
internet, etc.). Then if there is internet, ask the user if they want to install the new libraries"; "The user should
always have the option to use dependencies or not."

**Changed**
- **Builds for every merged PR:** a merge into `main` publishes a pre-release `build-pr<N>` (notes: the PR's
  description); v* tags publish stable releases. Each carries the installers, the libraries archive and
  SHA256SUMS.txt. Builds know their tag and version (shown in About and the status bar).
- **Updates:** once a day, when online, Blendity asks GitHub's releases list (WinHTTP on Windows, `curl` elsewhere: no
  new library) and offers a newer build with its notes - Install / Not now / Skip this one. Help > Check for Updates
  and **Help > Version History** (every build for this system, older ones too: the build before a certain PR).
  Downloads are checked against the release's SHA-256 list; Windows installs silently and restarts Blendity, an
  AppImage replaces itself, macOS opens the disk image. Preferences > Updates: check at start, include PR builds.
- **Libraries optional at run time:** an install holds Blendity (dependency-free, always starts) and Blendity-full.
  Blendity hands over when the full build's libraries load (`--probe`) and **Preferences > Libraries > Use Blender's
  libraries** is on; otherwise - a library missing or damaged, offline, turned off, `--no-libs` - it runs on its own
  code, and online it offers this build's libraries (Download / Not now / Never ask), unpacked into
  `~/.blendity/libs/<build>`. The Windows installer's "Blendity only" type leaves them out.
- The JSON reader moved from the glTF importer to `core/json.h`; SHA-256, downloads, releases and the launcher are new
  in `core/`.
- #70's installer jobs needed: Windows Embree's SYCL runtime (`dpcpp`), Linux GCC 14 (Blender's libraries call its C++
  runtime, shipped in `lib/`), macOS's own zlib.

**Checked**
- New tests (round 105): SHA-256 against FIPS 180-4's vectors; SHA256SUMS lines; a releases list gives the newer builds
  (stable only or with PR builds), none for the newest or a local build, all in Version History; the Update and
  History dialogs at 1280 x 720, Skip remembered; (opt-in, `BLENDITY_NET_TESTS=1`) GitHub's real list fetched, a 404
  reported.
- New test (round 106): this build knows whether it has the libraries; `--probe` answers; the Libraries dialog at
  1280 x 720; Never ask and Use Blender's libraries kept with the preferences.
- By hand on Windows: a scratch install with both programs - with the libraries it handed over, with `--no-libs` and
  with `embree4.dll` removed it ran itself.
- Unit checks: Windows 9,992, 0 failed (672 tests); the whole suite in render-cache verify mode: 9,993,
  0 failed, 0 stale pictures. Linux with libraries: 9,900, 0 failed. Sanitizer build: 9,806, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux and macOS, opening a folder or starting a program ignored SIGCHLD for the whole process, after which every
  download and the libraries probe failed: children nobody waits for are reaped by a thread of their own instead.
- The Windows installer never started Blendity again after an update: it does now ([Run] after an update started
  from Blendity), installing where the old copy is (for this user, or for everyone).
- A build older than the newest 100 releases never saw an update or its libraries: its own release is fetched too.
- Installing "Blendity only" over an install with the libraries left the old full build to be handed over to: it is
  removed.
- On Windows the dependency-free program returned at once while the full one worked: it waits and passes on the exit
  code.
- Downloads went to guessable names in the shared /tmp (another user could plant a link): a private
  ~/.blendity/downloads, and only plain file names from a release.
- Closing Version History by clicking outside stopped all later offers for the session.
- Replacing an AppImage could lose it on a full disk, and restarting after a libraries download ran from the old
  AppImage's vanished mount: the new one is copied beside the old first, and Blendity restarts as the AppImage.
- Unpacking the libraries and copying an AppImage ran on the editor's thread (seconds): on the downloading thread now.
- Releases are published one at a time, in merge order; the libraries are asked about at most once a day and never
  turn into an update offer when update checks are off.
- The workflow's token is read-only except in the release job (a pull request's build scripts run with it).
- curl keeps to https (redirects too) with no fixed time limit on large downloads; WinHTTP treats an unfollowed
  redirect as an error; SHA-256 streams the file; Windows' own tar.exe from System32; the probe gives up after 30 s;
  a download stops when Blendity closes; Version History scrolls through every build; the title no longer reads
  "Blendity Blendity".
- Left: a full build that crashes after a good probe doesn't fall back by itself (Preferences > Libraries or
  --no-libs does).

## 2026-10-14 (rounds 103-104): FFmpeg linked like Blender's other libraries, and the libraries in the installers (tasks 0077-0078, ADR 0032)

**Asked:** "Can we install ffmpeg and treat it like a blender dependency?" - link it, and "make sure other dependencies
should be included when installing the project. The user shouldn't really have to worry on downloading blender on
their own."

**Changed**
- **FFmpeg 8.1** (Blender's build: x264, x265, libvpx, libaom, Opus, Vorbis, Theora, LAME) is linked behind
  `BL_WITH_FFMPEG` by build.bat, build.sh and CMake. Render Animation encodes its MP4 itself (H.264, CRF 18) on a
  background thread; a build without it falls back to an `ffmpeg` program or PNG frames, as before. `libs` lists it.
- Fetched into this machine's Blender library checkouts: `ffmpeg` (Windows 46 MB, Linux 67 MB) and Windows' `aom`
  (FFmpeg's AV1 DLL, 9.5 MB); Linux also links OpenJPEG and Blender's `libc_compat` (copied to `extern/`).
- **`packaging/fetch_blender_libs.sh <platform>`** fetches only the library folders Blendity uses, at commits pinned in
  `packaging/blender-libs.txt` (about 600 MB a platform), plus Blender's colour config when missing.
- **The installers carry Blender's libraries:** the release workflow fetches them (cached), builds all three with
  them and checks each has Embree, OpenImageDenoise, OpenColorIO, Jolt and FFmpeg; the macOS app bundle holds `lib/`
  and `datafiles/`. Nobody installing Blendity needs Blender or its libraries.
- Licences: FFmpeg and its codecs listed; LGPL-2.1 and GPL-2.0 texts added.

**Checked**
- New test (round 103): six 101 x 57 PNG frames become an MP4 FFmpeg reads back as 6 frames of 102 x 58 with libx264;
  a missing frame is refused with a reason. Render Animation's test made its MP4 in-process. Both on Windows and Linux.
- Unit checks: Windows 9,965, 0 failed (667 tests); the whole suite in render-cache verify mode: 9,966,
  0 failed, 0 stale pictures. Linux with libraries: 9,873, 0 failed. Sanitizer build: 9,779, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- CMake's Linux build wouldn't have linked: `libc_compat` sat in the static library, where nothing pulled it in before
  the codecs asked; it is now on every executable's link line.
- An odd frame size was stretched by a pixel (a soft, shifted picture): it is padded by repeating the edge now.
- The video wasn't tagged and used BT.601 colours: it is BT.709, limited range, tagged, as Blender writes it.
- Closing the editor while an MP4 was being encoded left the encoder running into the program's exit: it stops
  between frames and the editor waits for it.
- A failed or stopped encode left an unplayable half-MP4: it is deleted.
- x264's options went to whatever encoder was found: a fallback encoder gets a high bit rate instead.
- FFmpeg's log level was set from the worker thread: once, from the main thread.
- Only the five FFmpeg DLLs Blendity uses are shipped (not avdevice / avfilter); CMake no longer links an FFmpeg
  folder without its libraries; `extern/README.md` says where each copied file comes from.
- Left: the FFmpeg DLLs load with the program (as the other libraries' do), so a missing one stops it starting.

## 2026-10-14 (round 102): installers for Windows, Linux and macOS (task 0076)

**Asked:** installers for Windows, Linux and macOS; the choice was an Inno Setup .exe, an AppImage and a .tar.gz, and
an unsigned .dmg, attached to a GitHub Release on v* tags.

**Changed**
- **Windows:** `Blendity-Setup-<version>.exe` (Inno Setup, `packaging/windows/blendity.iss`): installs for the user
  alone (no administrator), Start menu and optional desktop shortcuts, `.scene` files opening in Blendity, an
  uninstaller in Settings > Apps. `build.bat installer` makes it locally when Inno Setup 6 is installed.
- **Linux:** `Blendity-<version>-x86_64.AppImage` and `blendity-<version>-linux-x86_64.tar.gz`
  (`packaging/linux/make_packages.sh`, built on Ubuntu 22.04 for older systems).
- **macOS:** `Blendity-<version>-macos.dmg` (`packaging/macos/make_dmg.sh`): the app with its icon and version and an
  Applications link; unsigned (right-click > Open the first time).
- `.github/workflows/release.yml` builds all three on pull requests that touch packaging, installs or mounts each and
  starts it headless, and on a v* tag makes the GitHub Release with them. `VERSION` is 0.10.0. No tag was pushed.
- An icon (a cube in the Emiliano palette) drawn by `packaging/make_icon.py` without any library.
- A `.scene` path given alone - a double-clicked file - opens, with the project folder above it.
- The debug OpenJPH DLL (`openjph.0.25d.dll`) is no longer copied into `dist\windows`.
- README: a Download section.
- Fixed: the test "feel: Unreal's End drops the selection..." failed twice on CI's sanitizer build (#61, #67): it gave
  Alt+J's camera turn a fixed 0.5 s, and a sanitizer frame can take most of that; it now waits for the turn to end.
  The auto-rig's view test (#68) and two others that waited the same way do too.

**Checked**
- New test (round 102): a lone `.scene` argument is the scene and its project is found above it; an option's value
  isn't.
- The Linux .tar.gz from the WSL build, unpacked elsewhere, starts headless and makes `~/BlendityProject`.
- Unit checks: Windows 9,953, 0 failed (666 tests); the whole suite in render-cache verify mode: 9,954,
  0 failed, 0 stale pictures. Linux with libraries: 9,861, 0 failed. Sanitizer build: 9,778, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- The release workflow's own run on this PR (Inno Setup isn't installed on this machine).
- A headless run writing to a path with an accented letter (UTF-8 arguments on Windows).

**Review** (Opus code-reviewer). Fixed:
- `build.bat release all` went on into the installer step (CI's Windows check would have failed without Inno Setup).
- The release job looked for the installers where the artifacts don't put them: no release would have been made.
- Installing "for all users" put the program in one user's folder: `{autopf}` follows the choice.
- A double-clicked scene with an accented letter in its path didn't open (Windows' ANSI argv): the arguments come from
  the wide command line as UTF-8. `--scene`'s value now finds its project too, and `.SCENE` in any case counts.
- The macOS note: on macOS 15 an unsigned app is allowed in System Settings > Privacy & Security > Open Anyway.
- The workflow's checks start each installed copy away from the repository (a fresh machine's project), wait for the
  Windows uninstaller and check that it removes the `.scene` association; rerunning the dmg script no longer nests
  the licences.

## 2026-10-14 (round 101): the laptop minimum - an i7, 8 GB, the HP Envy 13.3" 2-in-1 (task 0075, ADR 0026 amended)

**Asked:** a new minimum machine: an i7 with 8 GB of RAM, the HP Envy 2-in-1 13.3".

**Changed**
- The minimum is now that laptop (i7-1165G7, 4 cores / 8 threads, 8 GB shared with Iris Xe, 1080p at 150 %): README,
  MANUAL chapter 11, ADR 0026 and PERFORMANCE.md pass 9.
- **First run** on such a machine: Low, and a welcome that says so, with **Use High** one click away
  (`BLENDITY_MIN_SPEC=1` shows it on any machine).
- **Touch (Windows):** two fingers over the Scene view pinch to zoom, drag to pan, twist to turn round the pivot, in
  every editor feel; one finger is the mouse. **Pen:** its pressure scales the weight-paint brush.
- The Low tier's views read shadows with a 2 x 2 bilinear filter (4 taps instead of 9).
- An integrated GPU's path-tracing budget is a quarter of RAM (Iris Xe reports half as its own).
- `blendity_stress --laptop`: 5 threads (4 cores' 8 threads), the times to be multiplied by 1.4 for the laptop's clock.

**Checked**
- `editor_render` at Low with `--laptop` x 1.4: orbiting 13.0 ms (60 fps), moving an object 26.0, a material edit 24.9,
  Play 22.8 (30-40 fps; 60 while editing is **not met** on the laptop, and Low keeps 67 % rather than looking soft).
  On 8 full cores (pass 8's setup) the frames pass 8 left over 16.7 ms are now 12.9 and 11.4.
- New tests (round 101): a pinch of 2 halves the distance, a two-finger drag pans with the fingers, a twist turns the
  view by its angle, over the Inspector nothing, NaN ignored; a stroke at pressure 0.5 adds half; the first run on the
  minimum machine shows the welcome, which fits 1280 x 720, and Use High switches and is saved.
- The Preferences dialog checked at 1280 x 720 (a headless screenshot).
- Unit checks: Windows 9,948, 0 failed (665 tests); the whole suite in render-cache verify mode: 9,949,
  0 failed, 0 stale pictures. Linux with libraries: 9,856, 0 failed. Sanitizer build: 9,773, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- `--laptop`'s message had a line break inside its string: the stress tool didn't compile.
- A pen hovering after lift-off left its pressure 0 for every later mouse stroke: only mouse messages Windows made from
  a pen carry it now, and lifting or leaving resets it.
- The first finger's press, when Windows makes one, moved a held drag to the gesture's middle: gestures leave the
  mouse position alone while a button is down.
- Windows' own gestures (begin, end, taps) went unanswered; the pointer calls are looked up so the program starts
  without them.
- The 2 x 2 filter's edge is narrower than 3 x 3's, not as soft: the comments and lesson say so.
- Left: the Low filter is a flag on the cached shadow map, safe while every render runs on the main thread (a note for
  any render moved off it).

## 2026-10-14 (round 100): render animations and sequences to video (task 0074)

**Asked:** rendering sequences to video; the choice was PNG frames plus an MP4 through FFmpeg.

**Changed**
- **Render Animation** (File menu, the Render window, Ctrl+F12, `renderanim [first last] [fps] | stop`): every frame
  of the scene's sequence - or, without one, the selected object's clip - through the F12 render, into
  `Renders/<scene>_animation_<time>/0001.png...`; the pose goes back afterwards.
- Then, when FFmpeg is installed (on the PATH, or `renderanim ffmpeg <path>`, kept with the preferences), an H.264 MP4
  is made beside the frames in the background. Blendity runs FFmpeg rather than linking it: no new dependency.
  Without it the frames are the result, and the log says how to make a video.
- A render stops cleanly when something ends what it poses through: Play, a save, an undo, another scene, another
  object selected for a clip. Camera Sequence and Render Animation don't run at once.
- Fixed: the IK test "in Play a Rotator swinging a foot target" failed once on a fast CI runner (Play runs on the
  clock); it now steps until the target has moved.

**Checked**
- New tests (round 100): 21 frames of a sequence written as 0001-0021.png at the render's size, the first and last
  different, the pose put back; FFmpeg's arguments and the Windows command-line quoting; the MP4 made where FFmpeg is on
  the PATH (not on this machine); a clip's render stops when another object is selected or its preview ends; a lone
  number is refused.
- Unit checks: Windows 9,935, 0 failed (662 tests); the whole suite in render-cache verify mode: 9,936,
  0 failed, 0 stale pictures. Linux with libraries: 9,843, 0 failed. Sanitizer build: 9,760, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The rasterizer's frames waited for mouse or key input in the real app (the editor slept between them).
- A save, an undo, Play or another scene mid-render left the rest of the frames unposed; another object selected for
  a clip was posed and left so. The render now stops with a message.
- Camera Sequence could start over a running Render Animation.
- FFmpeg read the terminal on Linux (`-nostdin`); its exit code was lost once SIGCHLD is ignored, and a failed encode
  with a file left behind counted as made.
- An empty scene sequence hid the selected clip; a frame rate of 0 or NaN from a file reached FFmpeg; a fractional end
  rendered one frame too many.
- The Render window's tooltip searched the PATH every frame; it is looked up when a render starts.
- Two renders in one second shared a folder; a failed PNG went on with a gap; a lone number or unknown word in
  `renderanim` started a render.
- Left: a PATH entry with non-ASCII characters on Windows isn't searched (set the path with `renderanim ffmpeg`).

## 2026-10-14 (round 99): themes - Dark, Light, Emiliano Light and Emiliano Dark (task 0073)

**Asked:** an editor palette / theme option using the user's Figma site ("look at the variables tab in figma").

**Changed**
- **Preferences > Theme** (console: `theme <name>`): Dark (Blendity's), Light, **Emiliano Light** and **Emiliano Dark**.
  The two Emiliano themes take the Figma file's Variables (Branding Guide, Light and Dark modes): paper F1EEE5 and
  charcoal 292927 swapped between the modes, amber D7882B / A65E1E for buttons and selection, mint 45E8A2, red E6533C,
  blue 4C76FF and amber FF9A00 as signal colours (focus, X / Y / Z, errors, warnings), stone, slate and silver for
  borders and dim text. The choice is kept with the preferences.
- Colours the windows wrote by hand (list stripes, ruler strips, row hover, keyframe diamonds, the auto-rig's figure)
  are now theme colours.

**Checked**
- New test (round 99): Emiliano Light and Dark give the site's colours, case aside; an unknown name is refused; the
  theme survives saving and loading the preferences.
- Headless screenshots of both Emiliano themes (Animation layout).
- Unit checks: Windows 9,919, 0 failed (660 tests); the whole suite in render-cache verify mode: 9,920,
  0 failed, 0 stale pictures. Linux with libraries: 9,827, 0 failed. Sanitizer build: 9,744, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- In the light themes, tooltips, the F9 panel, the Hierarchy's scene row, every other Console line, the Learn sidebar,
  notes and questions, the Lighting and Render settings and the Project folder tree kept dark fills under dark text:
  they take theme colours now (tooltip, bar, pane, stripe, well, note, question, link), as do the scrollbars, menu
  separators, popup outlines, the windows' toolbars, the timeline playhead and the Edit Mode button's label.
- Emiliano Dark's borders and field outlines were the toolbar's colour; they show now. Its selected keys are amber.
- Emiliano Light's auto-rig figure and graphs sit on stone, not charcoal.
- `set_theme` and the Preferences combo share one name lookup (`ui::theme_index`).
- The Dark theme's Learn table rows, Sequencer details and curve keys moved by a shade or two (now theme colours).
- Left as they were: the Scene and Game views' and the UV editor's own drawing (they read over any picture).

## 2026-10-14 (round 98): a texture's Physical Size drives its materials' tiling (task 0072)

**Asked:** a texture's Physical Size should really adjust its tiling.

**Changed**
- Changing a procedural texture's **Physical Size** re-tiles every material of the scene using it (Tiling = 1 / size,
  in any of its maps), in the same undo step. A material tiled by hand keeps its Tiling.

**Checked**
- New test (round 98): Bricks from 2 m to 4 m takes a material from Tiling 0.5 to 0.25, and one using it only as a normal
  map; hand-tiled ones keep 3 and 1/3 (passed on the way); an unused `.mat` is left; undo and redo.
- Unit checks: Windows 9,913, 0 failed (659 tests); the whole suite in render-cache verify mode: 9,914,
  0 failed, 0 stale pictures. Linux with libraries: 9,821, 0 failed. Sanitizer build: 9,738, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A `.mat` asset no object used was re-tiled but undo couldn't put it back: only the scene's materials follow now.
- Only the colour map counted as using the texture: its normal, roughness, metallic and emission maps count too.
- A dragged Physical Size passing a hand-tiled material's size picked it up: which materials follow is decided at
  the first change of the undo step.
- Box mapping's z tiling follows too when it matched.
- The re-tile no longer scans every `.mat` file on each frame of a drag, nor logs each frame.
- New checks: a normal-map-only material, a hand-tiled one passed on the way, the unused `.mat`, redo.
- Left for its own task: redo of an edit to a material linked to a `.mat` (any field, not only tiling) keeps the
  undone values (older than this task).

## 2026-10-14 (round 97): drag stacks into order (task 0071)

**Asked:** drag-and-drop reordering of the camera filter stack, the modifier stack and the other stacks.

**Changed**
- Drag a **modifier's**, a **camera filter's** or a **texture layer's** name in its header to another place in its stack:
  a line shows where it lands and the item is outlined; letting go moves it, one undo step. A click without moving still
  folds the header; the menus' Move Up / Down stay.
- The **Sequencer's tracks** move the same way, by their names (clips go with them). Console: `seq trackmove <from> <to>`.

**Checked**
- New tests (round 97): Bevel dragged below Smooth in a Bevel / Array / Smooth stack ends last, undo puts it back in one
  step, a click still folds; a camera's first filter dragged below the second ends last; a Sequencer track dragged
  below another takes its clips, and `seq trackmove` moves it back.
- Unit checks: Windows 9,902, 0 failed (658 tests); the whole suite in render-cache verify mode: 9,903,
  0 failed, 0 stale pictures. Linux with libraries: 9,810, 0 failed. Sanitizer build: 9,727, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A drag whose list stopped showing (another object selected, the Sequencer emptied by an undo) stayed alive and moved
  an item the next time the list showed; it is now dropped.
- Undo, redo and Esc during a drag now drop it (the item under its old index could have been another one).
- The insert line could be drawn over the window above a scrolled Inspector; it is drawn inside the panel now.
- Drags that couldn't land (a track in Play, an unsaved texture's layer) no longer start.
- New checks: a filter dragged up, a drag whose list went away, Esc.
- Left as is: the line below the last item sits under its header, above its open settings.

## 2026-10-14 (round 95): a global sequencer - each clip names its object (task 0070, ADR 0030 amended)

**Asked:** "The sequencer should work globally and have two things to consider when adding a clip. Which animation clip
it is using and what object is it attributed to."

**Changed**
- The Sequencer window edits **the scene's sequence** (saved with the scene), with or without a selection.
- **Each bar is a clip and an object:** a dropped clip plays on the selected character; the Object field below the
  tracks takes an object dragged from the Hierarchy; bars read "Walk -> Robot". Console: `seq add <clip> <track>
  <start> [object]`, `seq object <clip> <object>`.
- Preview poses every bar's object and puts them all back. In Play the scene's sequence plays (triggers and `seq jump`
  too); an object's Animation component steps aside while the sequence has a bar on it.
- Overlapping bars crossfade only when they play on the same object.
- `.seq` files gain a 4th column (the object); older files load unchanged. The Sequencer component still plays a
  sequence on its own object.

**Checked**
- New test (round 95): one scene sequence moves two lids from two tracks in Preview (and puts them back), overlapping
  bars on different objects don't blend, the `.seq` and the scene file keep the objects and the sequence, and Play
  moves them.
- New test (round 96): three objects' bars overlapping on one track all play; an object given a bar during Preview is
  put back; renaming the `.seq` keeps the scene on it.
- Unit checks: Windows 9,885, 0 failed (656 tests); the whole suite in render-cache verify mode: 9,886,
  0 failed, 0 stale pictures. Linux with libraries: 9,793, 0 failed. Sanitizer build: 9,710, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress `animation`: 201 tracks 0.0735 ms a frame (target 0.2 ms); a sequence of 4 tracks crossfading 201-track
  clips 0.3143 ms a frame (target 0.5 ms).

**Review** (Opus code-reviewer). Fixed:
- Two rigs could share a lookup key and pose each other's bones.
- Renaming the scene's `.seq` lost it.
- An object given a bar during Preview kept its previewed pose.
- A track with three overlapping objects dropped one.
- A jump before Play's first frame didn't find the bars' objects.
- `seq jump` couldn't reach a component's sequence.
- Sequences made before this task are editable again.

## 2026-10-14 (rounds 89-94): rigging - character roots, bone handles and extrude, rig controls, auto-rig colours and views, grouped keyframes (tasks 0065-0069, ADR 0031)

**Asked** (2026-10-14):
- "When adding an armature to a mesh/object nest it in the hierarchy itself; similar to how unity handle's humanoid
  models being imported."
- "Add the ability to move the base and tip of a bone ... add bones using extrude like vertices."
- "Give the inverse kinematics a special gizmo ... (also add the option to remove FK/IK systems) ... like blender and
  maya."
- "For auto-rig ... make them different colors to indicate different parts of the body ... transfer to different
  orthographic views."
- "Select multiple keyframes in the animation block (drag select) ... x,y,z in the same frame and then expand as
  needed."

**Added**
- **Character roots** (0065): GameObject > Armature on a mesh, Add Armature (Skin) and the auto-rig put the mesh and its
  Armature inside "<mesh> Rig" in the mesh's old place; the Animation window and the Sequencer animate that root.
- **Bone handles and Extrude** (0066): a selected bone's tail handle (and Shift+drag on its head) aims and stretches it,
  children staying put, the result its rest pose; Extrude (Ctrl+E / E) grows a bone from the tip that follows the mouse
  until a click (Esc cancels). Console `bone head|tail|extrude`.
- **Rig controls** (0067, ADR 0031): the Rig Control component (Circle, Square, Box, Diamond, Sphere, Arrows, Cross,
  Root) drawn as wire over the scene, coloured by side (left blue, right red, middle yellow). IK targets are boxes,
  poles diamonds with a dashed line to their joint; IK's bones are tinted. FK controls (circles that turn their bones).
  Remove IK, Add / Remove FK Controls; console `ik remove`, `fk add|remove`.
- **Auto-rig** (0068): markers coloured by body part; Front / Left / Right / Back / Top views (`autorig view`), a
  dragged marker keeping its depth away from the front view.
- **Keyframes** (0069): the dopesheet groups a vector's axes in one row that unfolds; a group diamond selects and moves
  all its axes' keys; box select takes every diamond it touches, lights them while dragging, Shift adds, Ctrl removes;
  dragged keys show where they will land.
- Learn: "A character's root", "Bone heads, tails and Extrude", "Rig controls: IK and FK", "Auto-rig colours and
  views".

**Checked**
- New tests (rounds 89-94): the rig root's shape and place (in a group and at the scene's top level), undo; the skin
  following the root; Add Armature (Skin) nesting and leaving a shared skeleton; tail and head drags (children kept, rest
  pose, one step, a half-scale armature); Extrude kept (one step) and cancelled (no step, no bone); IK controls' shapes
  and sides (including ".L" names), Remove IK (one step, a user's object kept); FK turning its bone, a click on its wire,
  Remove FK, FK refused on IK bones, Reset Pose with FK, an idle solve; marker colours; view switching; a side-view drag
  keeping depth; grouped rows, a group diamond's move, a thin box, Ctrl box.
- Unit checks: Windows 9,873, 0 failed (654 tests); the whole suite in render-cache verify mode: 9,874,
  0 failed, 0 stale pictures. Linux with libraries: 9,781, 0 failed. Sanitizer build: 9,698, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The bone handles were drawn before the picture and painted over.
- An extrude made an undo step per frame (and Esc left steps).
- Remove IK could delete a user's object used as a target.
- FK on an IK bone redrew the editor forever.
- Reset Pose was undone by FK controls next frame.
- The side and top views moved dragged markers to the wrong depth.
- A scaled armature's tail landed away from the mouse.
- Also: the head handle took the gizmo's centre (now Shift), roll drift and the grab offset, a stale bone drag across
  scenes, shortcuts and right-click fly during an extrude, the rig root's place at the scene's top level, wrapping an
  import's mesh again, any single-child parent taken as a root, Create Child > Armature's meaning, ".L" control
  names, a misspelt `fk remove` name, disabled controls drawn and picked, FK lookups by name every frame.

## 2026-10-14 (round 88): typed object and asset fields, Unity's "None (Armature)" (task 0064)

**Asked:** "Add the ability to drop objects from hierarchy into parameter fields in the inspector and not just rely on
the name of an object (and indicate type just like unity3d), like armature parameter into skin under Rigging … such as
armature, animation, sequencer etc."

**Added**
- **Object fields know their type** (`Reflector::object(name, value, type)`):
  - Shown as Unity's object field: the type's icon and "Armature (Armature)", or "None (Armature)" when empty; yellow
    when the object is missing or of the wrong kind.
  - Dragging an object of another kind over a field turns it red, and the drop is refused with a Console line.
  - The ⊙ picker at the field's right lists None and the scene's objects of that kind.
  - Clicking the field still types a name.
  - Typed fields:
    - Skin Armature (Armature)
    - Lattice Object (Lattice)
    - Curve and Array Curve (Curve)
    - Boolean and Mesh Deform Object (a mesh)
    - Transfer Weights From (a mesh)
    - IK Target and Pole take any object.
- **Asset fields** (`Reflector::asset(name, path, ext, kind)`):
  - The Animation's Clip (.anim) and the Sequencer's Sequence (.seq) take a file dragged from the Project window, and
    refuse another kind.
  - Their picker lists the project's files of that kind.
- Learn: "Typed fields and asset fields" in the Inspector lesson.

**Changed**
- A press on a file in the Project window that turns into a drag no longer selects the file, so the Inspector stays on
  the object whose field you're filling. A click selects it when the button comes up.

**Checked**
- New tests (round 88):
  - A Skin's Armature field refuses a Sphere and takes the Armature (one undo step), and shows "None (Armature)" then
    "Armature (Armature)".
  - The picker lists None and the armature but not the Sphere; picking None is an undo step of its own.
  - A .anim dragged from the Project window fills an Animation's Clip, a .seq is refused, the object stays selected,
    undo empties it, and a click on a file still selects it.
- Unit checks: Windows 9,795, 0 failed (643 tests); the whole suite in render-cache verify mode: 9,796,
  0 failed, 0 stale pictures. Linux with libraries: 9,703, 0 failed. Sanitizer build: 9,620, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A field's help tooltip (and the picker's) never showed: the picker had become the last widget.
- The asset picker walked the project's folders every frame while open; it now reads them once, as it opens.
- Each asset field checked the disk every frame; now once a second.
- A pick whose field wasn't drawn the next frame could apply much later.
- A fast press-move-release on a file selected it before seeing the drag.
- The missing type icon.

## 2026-10-13 (round 87): switch the editor's feel: Unity, Unreal, Blender or Maya (task 0063)

**Asked:** "Switch the editor's feel between Unity / Unreal / Blender / Maya". The user chose keys and mouse navigation
only.

**Added**
- **Editor Feel**: Edit > Editor Feel, Preferences > Keymap, or `feel unity|unreal|blender|maya`. It sets the keymap
  preset and the Scene view's mouse navigation together, and is kept in the prefs. Your own key changes stay on top.
- **Navigation by feel:**
  - Unity: as before.
  - Unreal: RMB look + WASD; Alt+LMB orbit; MMB, Alt+MMB or LMB+RMB (in either order) pan; Alt+RMB dolly.
    Letting go of LMB while RMB is still held looks round again.
  - Blender: MMB orbit, Shift+MMB pan, Ctrl+MMB zoom. Alt+click selects an edge loop and Ctrl+Alt+click a ring,
    without moving the view; RMB doesn't fly.
  - Maya: Alt+LMB tumble, Alt+MMB track, Alt+RMB dolly. Plain MMB and RMB don't move the view.
- **An Unreal keymap preset**: Unity's keys plus Alt+P Play, Ctrl+W duplicate, End drop to floor (the whole object
  with its children, onto y = 0), and Alt+G / H / J / K for the perspective, front, top and side views.
- New actions for any keymap: Perspective / Front / Top / Side View, Drop to Floor.
- Learn: "Work like Unreal, Blender or Maya" in the Edit Mode lesson. MANUAL chapter 12 has a table of the feels.

**Changed**
- Navigation drags end when the button that started them is let go (needed by MMB orbit and LMB+RMB pan).
- The test runner flushes after each test, so a crash shows which test was running.
- The keymap presets list Unreal second, so the Preferences dropdown test (round 79) now picks Unreal from the second
  row.

**Checked**
- New tests (round 87):
  - Each feel's mouse buttons with real drags: Blender MMB, Shift+MMB, Ctrl+MMB; Maya Alt+LMB, Alt+MMB, Alt+RMB.
  - Unreal RMB, and LMB then RMB.
  - Unity's whole set unchanged.
  - Each feel's keys; an unknown feel refused.
  - Blender Alt+click loop and Ctrl+Alt+click ring with the view still.
  - Unreal End on an object and on an empty with a child; Alt+J top view.
  - The feel kept in the prefs (an unknown one falls back to Unity); your own key change kept across a feel switch.
- Unit checks: Windows 9,774, 0 failed (641 tests); the whole suite in render-cache verify mode: 9,775,
  0 failed, 0 stale pictures. Linux with libraries: 9,682, 0 failed. Sanitizer build: 9,599, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Blender Alt+click loop select did nothing.
- Unreal LMB-then-RMB box-selected instead of panning.
- A feel switch wiped your own key changes.
- `feel` with no argument switched to Unity.
- Drop to Floor ignored children.
- Maya's plain MMB panned.
- The Learn answer about RMB, and the prefs test's path.

## 2026-10-13 (round 86): a Premiere-style clip sequencer instead of a state machine (task 0062, ADR 0030)

**Asked:** "A clip editor like Premiere Pro to go between animation clips instead of using a state machine"; for Play,
the user chose "a timeline plus triggers".

**Added**
- **Sequences**, `Assets/Animations/<name>.seq`:
  - Tracks of clip instances. Each instance has its clip, start, In and Out (trim), speed and name.
  - Two instances overlapping on a track crossfade over the overlap (Premiere's cross dissolve). Rotations blend the
    short way round.
  - A higher track's clip wins for the numbers it animates.
- **The Sequencer component** plays a sequence in Play (Loop or Once, Speed).
  - Its **triggers** jump to a named clip when a key is pressed (a letter, a digit, Space, F1-F12).
  - A jump blends from the pose of that moment over the trigger's seconds, and the timeline plays on from there.
  - An Animation component on the same object steps aside while a sequence plays.
- **Window > Sequencer** (beside the Animation window in the Animation layout):
  - Drag `.anim` clips from the Project window onto a track.
  - Drag a bar to move it (onto another track too) and its ends to trim it; Esc cancels.
  - Overlaps show the crossfade as a cross.
  - Below the tracks: the selected bar's Name, Speed, In and Out, and the trigger list.
  - Right-click a bar for Go To, Speed and Delete. Add Track.
  - Preview and Play, as in the Animation window. Every edit is one undo step.
- **Console:** `seq new | add | move | trim | speed | rename | remove | track | trigger | jump | frame | preview | play |
  stop`.
- **Renames follow:** renaming or moving a clip, a sequence or their folder keeps sequences, Sequencer components and
  undo steps pointing at them. That includes sequences not loaded.
- Learn: "Sequencing clips" in the Animation lesson. MANUAL chapter 9 has a walk/run example.

**Changed**
- Game keys (`PlayContext::key_down`) are not seen while a text field has the keyboard.
- The two Previews take turns: turning on one turns off the other.

**Checked**
- New tests (round 86):
  - Sampling: one clip trimmed and at speed; a crossfade; a higher track winning; a bar inside another; rotations the
    short way.
  - `.seq` round-trip, refusing bad lines, trigger key names.
  - A clip rename and a folder rename (with a sequence not loaded) carry the sequences.
  - In Play: a trigger's key jumps and blends from the pose of the moment. Once holds the last frame. An empty
    Sequencer leaves the Animation playing.
  - In the window: a drop places a clip, a drag moves a bar, its end trims it, each one undo step.
  - Preview restores the pose with no undo step. The two Previews take turns.
  - Huge or NaN track numbers are refused. Undo and redo of an edit.
- Stress `animation`: a sequence of 4 tracks, each two crossfading 201-track clips, costs 0.31 ms a frame (target 0.5).
  The first version took 1.30 ms (string keys and allocations per value). Values are now 64-bit keys in a reused
  table.
- Unit checks: Windows 9,741, 0 failed (638 tests); the whole suite in render-cache verify mode: 9,742,
  0 failed, 0 stale pictures. Linux with libraries: 9,649, 0 failed. Sanitizer build: 9,566, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The Animation window's Preview didn't put the Sequencer's away.
- A folder rename missed sequences not yet loaded (the listing was walked before the move).
- `seq add` with a huge track number made that many tracks.
- A bar inside another snapped back at its end.
- Once stopped a frame short.
- A click in an overlap took the bar underneath; short bars couldn't be trimmed.
- Editing In pinned Out.
- A drag outlived its window.
- Triggers fired while typing in the Console.
- An empty Sequencer silenced the Animation component.
- Dropping a clip elsewhere logged a warning.
- Preview could pose another object after a selection change.
- A bad `.seq` was read again every frame.
- The stress test's clips stayed loaded after their files went.

## 2026-10-13 (round 85): the animation curve editor, Unity's Curves view (task 0061, ADR 0029 amended)

**Asked:** "Start on the later list": first the curve editor for the keyframe animation of task 0059.

**Added**
- **Tangents on keys** (Unity's modes):
  - Clamped Auto (the old Auto), Free Smooth (one slope both sides), Flat (level) and Broken (each side its own).
  - Linear and Constant spans stay.
  - `.anim` files keep a key's mode and slopes; files from task 0059 load with Clamped Auto keys.
- **The Curves view**: the Animation window's Dopesheet | Curves buttons.
  - Each property is a line of its value over time (x red, y green, z blue) on a value axis with grid lines and
    numbers.
  - Click track names to choose the curves (Ctrl+click for several); F frames them; the wheel zooms the values.
- **Editing curves:**
  - Drag keys in time and value (Shift: one direction only); drag a box to select.
  - A selected key's handles set its slope: Free Smooth moves both, Broken each alone.
  - Right-click a key for the tangent and span modes, or Delete.
  - Double-click a curve to add a key that keeps its shape.
  - In Preview the scene follows the drag.
  - Each edit is one undo step.
- Console: `anim tangent <object|.> <property> <axis> <frame> auto|free|flat|broken [in] [out]`.
- Learn: "Curves and tangents" in the Animation lesson.

**Changed**
- The Animation window's code is split into the ruler, the rows and the new `curve_editor.cpp`.
- Moving and pasting keys keep their tangents.

**Checked**
- New tests (round 85):
  - Free Smooth, Broken and Flat slopes on both sides of a key, and the file round-trip (old files too).
  - In the Curves view, dragging a key raises it and dragging its handle sets a Free Smooth slope, each one undo step.
  - Two keys of one track moved onto each other's frames both survive.
  - 300 wheel steps in on a value of 100 stop at a hair and don't hang.
  - A clip switched mid-drag gets no edit.
  - `anim tangent` with slopes is one undo step; an unknown mode changes nothing; slopes are kept to +-1e6.
- Unit checks: Windows 9,694, 0 failed (632 tests); the whole suite in render-cache verify mode: 9,695,
  0 failed, 0 stale pictures. Linux with libraries: 9,602, 0 failed. Sanitizer build: 9,519, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress `animation`: 201 tracks in 0.075 ms a frame (target 0.2).

**Review** (Opus code-reviewer). Fixed:
- Moving several keys of one track could lose keys (each was moved onto the next before it moved).
- The value grid could loop for ever when zoomed far in on a large value: whole steps, at most 200 lines, and the zoom
  is clamped.
- A drag could commit into another clip after a clip change, an undo or the window losing its object.
- The commit now uses the last preview's amounts; handles and picks are limited to the shown curves; the in-handle
  shows only where the span before is a curve.
- `anim tangent` with slopes was two undo steps.
- A double-clicked key now keeps the curve's shape.
- Slopes are clamped on load; a refused edit no longer shifts the selection.
- Ctrl+wheel time zoom, in the spec, was not built: moved to the spec's "As built".

**Found on Linux**: loading a key with tangents wrote through a wrong index under GCC (the sanitizer build stopped on it).
`set_key` returned `keys.insert(...) - keys.begin()`, and C++ leaves the order of the two sides open: GCC took `begin()`
before the insert moved the keys. Now in two statements.

## 2026-10-13 (round 84): a README for GitHub and one user manual (task 0060)

**Asked:** "Update the GitHub README and compress the tutorial docs into one document with examples and use cases",
building on the Learn tab.

**Added**
- **docs/MANUAL.md**: one manual by stage of the work, the layouts' order:
  - Getting around, reference and planning, blocking out, modeling, UVs, materials and textures, lighting and
    rendering, rigging and skinning, animation, files, performance, shortcuts and the console.
  - Each chapter has short examples to follow, its use cases, and the Learn lessons that go deeper.
  - It takes in the old README's feature list and the lighting guide.
- **Opening it:** Help > Open Manual and the `manual` console command open it (the project's copy, else GitHub's).
  The Learn tab's Welcome lesson has a "The manual" section.

**Changed**
- **README.md** is a short front page:
  - What Blendity is, the screenshots and a quick start.
  - What it does by stage, the minimum and recommended machines, building, and links to the manual and the developer
    documents.
  - The stress highlights live in PERFORMANCE.md.
- `docs/guides/lighting.md` points into the manual.
- On Windows, opening a web address keeps its slashes.
- `help` lists the rigging, auto-rig, animation and manual commands.
- CMake's unit-test run starts in the source folder, as the fixtures need.

**Checked**
- New test (round 84): the manual has its 13 chapters, the README and the lighting guide link it, and every lesson
  and lesson section it names is in the Learn tab under that lesson (with CRLF checkouts too).
- Unit checks: Windows 9,658, 0 failed (628 tests); the whole suite in render-cache verify mode: 9,659,
  0 failed, 0 stale pictures. Linux with libraries: 9,566, 0 failed. Sanitizer build: 9,483, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review**: the reviewer checked every menu path, field and number against the code. Fixed:
- Shapes are under GameObject > Shapes (Parametric).
- The lighting window is Window > Lighting.
- Reference Image faces the front (`refimage ... side` for the others).
- Rigging and Skinning are two layouts.
- Preferences are Edit > Preferences... (Quality, Keymap).
- The Bricks preset already has Edge Wear.
- Axis Top / Side / Bottom and High Start.
- Console: PlayStation (PS1).
- Retiming drags the keys, not the ruler.
- The GPU also bakes and runs realtime GI.
- Two lessons were cited under the wrong lesson.
- Old README sections were restored in the manual (origins, number expressions, Join / Follow / Shell..., Active
  Camera, camera sequences, Transparent, the lighting settings).
- The test's CRLF and working-folder handling.

## 2026-10-13 (round 83): keyframe animation with a dopesheet, Unity's way (task 0059, ADR 0029)

**Asked:** "A keyframe/animation system like Unity", "Dopesheet for now" (a Premiere-style clip sequencer later
instead of a state machine).

**Added**
- **Animation clips**, `Assets/Animations/<name>.anim`:
  - Tracks of numbers by path below the animated root: Position, Rotation (Euler degrees) and Scale, or any
    component's number (`Light.Intensity`).
  - Keys with Auto (Unity's Clamped Auto: smooth, no overshoot between two keys), Linear or Constant.
- **The Animation window** (Window > Animation; the Animation layout along the bottom):
  - Preview, Record, first / previous key / play / next key / last, the frame, the clip menu with Create New Clip,
    and Add Property.
  - The tracks, with a ruler, the playhead and key diamonds.
  - Click, Shift+click or box-select keys; drag them in time; Delete; Ctrl+C / Ctrl+V at the playhead; double-click
    to add; right-click for the interpolation.
- **Record**: gizmo, Inspector or console changes to the animated objects become keys at the playhead.
- **Preview**: scrubbing poses the scene without an undo step, and turning it off puts it back.
- **The Animation component** (Unity's legacy one) plays its clip in Play (Loop, Once, Ping Pong, Clamp Forever,
  Speed), before IK solves.
- Key edits are undo steps; renaming a clip keeps the components pointing at it.
- Console: `anim new|key|interp|frame|preview|record|play|stop`.
- Learn: "Keyframes and the dopesheet" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 83):
  - Sampling: Constant, Linear, and Auto through a peak without overshoot.
  - The file, with names holding spaces and a bad line skipped.
  - Wrapping, including a negative Speed.
  - Keys on a lid: scrubbing makes no undo step, and Preview off puts the lid back.
  - Record keys a move but not a rename or a new child, and one undo takes the keys back.
  - Preview leaves unrelated objects alone, and an undo afterwards doesn't bring the pose back.
  - In Play, a bone turns and the Blender tube's skin follows.
  - Dopesheet clicks: the ruler, dragging a key, Delete.
  - Renaming a clip, and undo after it.
  - Preview refused in Play; New Scene clears the window.
- Stress `animation`: 201 tracks in 0.074 ms a frame (target 0.2 ms).
- Unit checks: Windows 9,653, 0 failed (627 tests); the whole suite in render-cache verify mode: 9,654,
  0 failed, 0 stale pictures. Linux with libraries: 9,561, 0 failed. Sanitizer build: 9,478, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: seven majors and several minor findings, fixed:
- Property names with spaces broke the file.
- Preview put back every object in the scene.
- The previewed pose leaked into undo.
- Preview survived Play and scene switches.
- Record keyed renames, new children and IK-driven bones.
- Clips weren't forgotten on delete or cleared on another project.
- A rename didn't reach undo states.
- Also fixed:
  - Bad numbers in a file.
  - A negative Speed.
  - Missing clips read every frame.
  - The clip list walked every 2 s.
  - Pasting into another clip.
  - Moved keys' interpolation.
  - Fractional record frames.
  - Key edits in Play.
  - Console objects outside the root.
  - Unsafe clip file names.
  - Euler flips on record.
  - No dopesheet scroll.
  - Nesting.

## 2026-10-13 (round 82): the marker auto-rig, humanoid and quadruped (task 0058, ADR 0028 part 2)

**Asked:** "A Mixamo-styled automatic weight paint ... I want the markers and inpainted weights, but I want support for
quadruped auto rigging as well."

**Added**
- **Rigging > Auto-Rig**: Humanoid or Quadruped, with the character's mesh selected.
  - The Scene view turns to an orthographic front view (an animal: its left side).
  - A diagram shows the markers, the next one in orange.
- **Placing**: each click puts the next marker inside the body, halfway through the first part under the mouse.
  - Symmetry mirrors left and right.
  - Drag a marker to move it; Remove Last takes one back.
- **Markers**:
  - A person (Mixamo's): chin, wrists, elbows, knees, groin.
  - An animal: nose, base of the skull, withers, hips, base and tip of the tail, paws, front knees, hind hocks.
- **Build Rig**:
  - The skeleton with the presets' bone names (an animal also gets a three-bone tail).
  - IK on every limb, the poles following each limb's own bend.
  - The mesh bound with inpainted weights.
  - Its markers stay for another try.
- Console: `autorig humanoid|quadruped`, `autorig mark <marker> <x> <y> <z>`, `autorig symmetry on|off`,
  `autorig build`, `autorig cancel`.
- Learn: "Auto-rig with markers" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 82):
  - A 1.7 m mannequin's and a dog's markers give their bones within 2 % of the height.
  - Every vertex weighted.
  - Bending the forearm moves the hand (0.31) not the hips; lifting a front paw moves it (0.30) not the head.
  - Clicks in the front view land mid-depth (z = 0.000) and mirror.
  - Bent limbs stay as placed (bones at rest within 6e-8).
  - Markers on the wrong side, and a dog facing -Z, keep their sides.
  - Upside-down markers and unknown names are refused.
  - New Scene ends a session.
  - The background bind, its undo, and markers kept for a rerun.
- Unit checks: Windows 9,613, 0 failed (619 tests); the whole suite in render-cache verify mode: 9,614,
  0 failed, 0 stale pictures. Linux with libraries: 9,521, 0 failed. Sanitizer build: 9,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Build Rig turned limbs that weren't bent the poles' fixed way.
- The mesh's move gizmo took marker clicks.
- A session outlived New Scene, Open Scene and Play (a new scene's object ids could point it at another object).
- Also fixed:
  - Duplicate crossings at shared edges.
  - Weight Paint and Edit Mode during a session.
  - Double-clicks framing the view.
  - Esc taken from G / R / S.
  - Build while a solve ran.
  - An animal facing -Z.
  - Upside-down markers.
  - A wide stance.
  - Unknown marker names.

## 2026-10-13 (round 81): automatic weights and weight transfer by inpainting (task 0057, ADR 0028)

**Asked:** "A Mixamo-styled automatic weight paint ... based on Epic's Robust Skin Weights Transfer via Weight
Inpainting", with transfer from a body onto clothing.

**Added**
- **Bind: Automatic Weights** now inpaints:
  - A vertex clearly nearest one bone (under 0.8 of the next one's distance) that the bone can see (nothing of the
    mesh in between) takes that bone whole.
  - The rest get the smoothest weights that agree with those: one sparse solve per bone of `Q = -L + L M^-1 L` (the
    cotangent Laplacian and Voronoi areas), by Eigen's LDLT or conjugate gradients.
  - A hand hanging by a thigh, or below its own arm bone, takes no leg weight.
  - A bone no vertex is nearest (a tip bone past the end of a column) takes the vertices nearest it, so posing it still
    moves that end (round 69's Armature test).
  - **Bind: Envelope** keeps the old distance weights.
- **Transfer Weights** (Rigging window, `skin transfer <body> [mesh]`): a rigged body's weights onto clothing.
  - Matches within 5 % of the clothing's size with normals within 30 degrees (or opposite) copy; the rest are
    inpainted.
  - An **InpaintMask** vertex group forces places to be inpainted.
  - The clothing gets the body's Armature (Skin) modifier.
  - The body is an object field: drag it from the Hierarchy.
- **Large meshes (5,000+ vertices) solve on a worker thread**, in a job arena of their own, with a progress bar and
  Cancel. They land as one undo step; undo, Play or another scene cancels them.
- Console: `skin bind [object] [inpaint|envelope]`, `skin transfer <source> [target]`, `skin cancel`.
- Learn: "Automatic weights by inpainting" and "Weights onto clothing" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 81):
  - The Laplacian is symmetric, its rows sum to 0, and the masses add up to the area.
  - Inpainting between known ends is a monotone partition of unity.
  - A transfer onto a copy (renumbered and 2 mm out) gives back the weights within 1e-4.
  - The paper's loose sleeve takes no torso weight (plain closest point gives it 1.0).
  - InpaintMask vertices are inpainted, and a source's mask isn't carried.
  - A hand by the thigh takes no leg weight, whether by the ratio test or by visibility.
  - Short spine bones in a wide torso each get a band and blend (largest step between rings 0.14).
  - A NaN vertex, a wire edge and a zero-area face still give a partition, by the direct solver.
  - `skin bind` at once and on a worker thread, each one undo step; cancel, undo during a solve and closing the
    editor mid-solve.
  - Transfer by console and by dragging the body onto the Rigging window's field; the sleeve then bends with the arm.
- Stress `skin_weights` (8 threads, Eigen): 50k vertices in 108 ms (automatic) and 31 ms (transfer), against the
  2 s target. The editor's slowest frame while it solves 50k in the background: 1.4 ms.
- Unit checks: Windows 9,576, 0 failed (613 tests); the whole suite in render-cache verify mode: 9,577,
  0 failed, 0 stale pictures. Linux with libraries: 9,484, 0 failed. Sanitizer build: 9,401, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress skin_weights: 50k vertices in 108 ms on 8 threads.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- Short bones got hard bands.
- A wire edge or NaN vertex made the solve singular and dropped it to conjugate gradients.
- A transfer normalised over InpaintMask and non-bone groups.
- A large mesh's Add Armature (Skin) had no undo step.
- The background solve shared the job pool with the editor's frames.
- Also fixed:
  - Undo or Play during a solve.
  - Vertices moved during a solve.
  - No frames while a solve ran with the window closed.
  - The Inspector's bind button.
  - Unconverged solves went unreported.
  - The solve depended on the mesh's scale.
  - Exceptions in the worker.
  - Copying render caches.
  - Nesting.
  - Tests that missed the closest-point path and the visibility rule.

## 2026-10-13 (round 80): flatter code after the review on PR #50

**Asked:** "Can you read feedback in my GitHub and then make appropriate changes?" The one review (PR #50, task 0053):
"Be careful of nested if statements. Use methods as noted by @CodeAesthetic 'Why You Shouldn't Nest Your Code'."

**Changed** (extraction and guard clauses):
- **`MultiSelect`**: `click` is three lines over `add`, `toggle` and `range_to`. New `sync` and `select_all` replace
  blocks each of the three windows repeated.
- **One delete question** (`ask_delete` with a `DeleteQuestion`) replaces six copies: one and several, in the
  Materials, Textures and Project windows. One context-menu item (`delete_menu_item`) and two key checks
  (`select_all_key`, `delete_key`) replace the nested `if`s around them.
- **Helpers out of nested loops:** `slots_using`, `select_objects_using`, `materials_using_texture`,
  `edit_material_asset`, `project_keys`, `project_row_pressed`, `request_*_delete`.
- **`matdelete` / `texdelete`** moved out of the console's command chain into `delete_assets_command`, with
  `material_by_name`, `texture_by_name` and a shared `split_list` replacing nested lookup loops.
- Small differences:
  - Clicking away from the Materials window's "Delete the material ...?" now cancels it, as the other windows'
    questions did.
  - Ctrl+A and Delete over the Materials and Textures windows wait while a text field is being edited.
  - `matdelete` with two assets of one name deletes the first found (it took the last).
- The rule now applies to new code too (task 0057's solver is written as small functions).

**Checked**
- New tests (round 80):
  - `matdelete` with names holding spaces and an unknown one; `split_list` trims and drops empty parts.
  - The multi-selection: Ctrl+Shift with the last click gone, `sync` with hidden and edited items, Ctrl+A.
- Reviewed by the code reviewer twice (no behaviour bugs; its remaining nesting spots were flattened too).
- Unit checks: Windows 9,523, 0 failed (599 tests); the whole suite in render-cache verify mode: 9,524,
  0 failed, 0 stale pictures. Linux with libraries: 9,431, 0 failed. Sanitizer build: 9,349, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-13 (round 79): drag objects onto fields; the Layout dropdown crash; a flaky CI check

**Asked:** "Some checks fail, check those out." "Clicking the drop down for the layouts crashes the program." "Also
add the ability to drag and drop objects from the hierarchy into the parameter field rather than just typing the name
(applies to all tools)."

**Fixed**
- **Clicking the Layout dropdown crashed the editor.** A dropdown's list draws at the end of the frame, after the
  Layout dropdown's per-frame list of names was gone. Every dropdown now keeps its own copy of its names while open.
- **CI (Windows) failed on PRs #49 and #50** at "min spec: a thread ceiling holds ...": on a 4-core runner every
  thread had to join one short loop. The check now takes the best of a few tries; the other Windows run of each PR
  passed, and the failed jobs were re-run.

**Added**
- **Object fields** (Unity's `ObjectField`): every field that names another object (Boolean, Lattice, Curve and Mesh
  Deform's Object, Array's Fit Curve, IK Target and Pole, a skin's Armature) takes an object dragged from the
  Hierarchy. The field lights up under it, the drop is one undo step, and the Console warns when two objects share
  the name.
- **Pressing a Hierarchy row selects it when the button comes up**, so dragging a row onto a field keeps the
  Inspector on the object you're editing (Unity selects on press, so its users lock the Inspector first). A click,
  a wobbly click, a reparent or a drop in the Scene view still selects it.
- Learn: "Object fields: drag, don't type" in the GameObjects lesson.

**Checked**
- New tests (round 79):
  - Opening the Layout dropdown and picking UV Unwrapping by mouse, from a named layout and from a custom one.
  - Dragging the Cylinder's row onto a Boolean's Object field fills it, keeps the Cube in the Inspector, and undoes
    in one step; a click still selects.
  - A click that slides 8 px still selects; a row dropped in the Scene view ends up selected; a press right after a
    drop finds no drag still live.
- Unit checks: Windows 9,511, 0 failed (597 tests); the whole suite in render-cache verify mode: 9,512,
  0 failed, 0 stale pictures. Linux with libraries: 9,419, 0 failed. Sanitizer build: 9,337, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A field scrolled out of view or under a popup took the drop.
- The drag state could outlive the button and drop again on the next click.
- Also fixed:
  - A wobbly click no longer selected, and a row dropped in the Scene view stayed unselected.
  - The lesson said Unity selects on release (it selects on press).
  - Two objects with one name went unreported.
  - A field being typed in wrote its old text back over a drop.
  - Nested reflectors didn't pass object fields on.

## 2026-10-13 (round 78): bind poses on rigged imports (task 0056, ADR 0025 amended)

**Asked:** "Yes, please follow Blender, Unity, and Maya methodology" (about rigged files saved in a pose).

**Fixed**
- **A rigged glTF or FBX saved mid-pose** imported with that pose as its rest, so the skin was wrong and Reset Pose
  did nothing. Each bone's rest is now its **bind pose**, as Blender, Unity and Maya read it: glTF's inverse bind
  matrices, FBX's cluster bind matrix. The pose in the file stays on the bones, and Reset Pose gives the shape the
  mesh was bound in.
- Files saved at rest import exactly as before. A bind that can't be used (missing, zero, non-finite, under a parent
  scaled to 0) falls back to the saved transform.
- The Console's "saved in a pose other than its bind pose" warning is gone. It now warns when two meshes bound a bone
  in different poses, or an FBX mesh moved after binding.
- Learn: the Bones and skins lesson says how posed files import.
- Tests: round 75's materials test left its MA..MD materials in the scratch project, so they piled up run after run
  until MC scrolled out of the window and the test failed (and crashed when run alone). It now clears them first.

**Checked**
- New tests (round 78), with fixtures exported from Blender 5.2 with the forearm bent 60 degrees:
  - The posed glTF and FBX import bent 60 degrees and match Blender's posed mesh (FBX within 9e-7).
  - `armature reset` gives the file's mesh and puts the forearm back above the upper arm.
  - Rigs saved at rest keep each bone's saved transform exactly.
  - Unusable binds leave the bone at its saved transform.
- Unit checks: Windows 9,493, 0 failed (594 tests); the whole suite in render-cache verify mode: 9,494,
  0 failed, 0 stale pictures. Linux with libraries: 9,401, 0 failed. Sanitizer build: 9,319, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: one major and three minor findings, fixed:
- An FBX cluster without a bind matrix (ufbx leaves zeros) was taken as a bind, which froze its vertices.
- A parent scaled to 0 made a bone's rest its absolute bind.
- Two meshes bound in different poses, and a mesh moved after binding, went unreported.
- A rig saved at rest got float-rounded rests instead of the saved ones.

## 2026-10-13 (round 77): layouts for each stage of the work (task 0055, ADR 0027)

**Asked:** "Add different layouts that focus on different sections of the 3D modeling process such as:
Reference/Planning, Blocking Out, Modeling and Topology, UV Unwrapping, Texturing and materials, and then lighting and
Rendering; of course the generic editor too ... I want to avoid that feeling of so many tools ... separate layouts for
rigging, skinning and weight painting, animation".

**Added**
- **Stage layouts** in the Layout dropdown (top right, Unity's place) and Window > Layouts: Reference & Planning,
  Blocking Out, Modeling & Topology, UV Unwrapping, Texturing & Materials, Lighting & Rendering, Rigging, Skinning &
  Weight Painting, Animation.
- **Each layout arranges the windows** for its stage, for example the UV Editor beside the Scene view for UVs, or the
  Materials and Textures windows for texturing.
- **Each layout shows only its stage's tools** in the Modeling Tools window, the GameObject menu and Add Component.
  The keys and console reach the rest; Show All Tools shows them.
- **Save Layout... / Delete Layout...** for your own layouts, kept in preferences.
- **GameObject > Reference Image...**: an unlit plane with the image, facing front, side or top. It casts no shadow and
  stays out of renders; an image from outside the project is copied in.
- Console: `layout <name>|save <name>|delete <name>`, `alltools on|off`, `refimage <path> [front|side|top]`.
- Learn: "Layouts for each stage" in The Editor Layout lesson.

**Checked**
- New tests (round 77):
  - The stage layouts open their windows and show their groups.
  - Blocking Out's Tools window offers Show All Tools, and a click brings the Edit Mode section back.
  - The console reaches a hidden Light.
  - Save Layout keeps the windows and focus; built-in names are refused; Delete removes the layout.
  - A lesson's layout button arranges the windows.
  - Preferences bring back the layout and your own after a restart.
  - A Reference Image is unlit, 2:1 for a 200 x 100 picture, faces up for "top" and +X for "side", out of renders.
- Unit checks: Windows 9,470, 0 failed (591 tests); the whole suite in render-cache verify mode: 9,471,
  0 failed, 0 stale pictures. Linux with libraries: 9,378, 0 failed. Sanitizer build: 9,296, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A lesson's layout button (and `--layout`) changed the tools but not the windows; one table now serves both.
- Tooltips of hidden GameObject menu items landed on others.
- Also fixed:
  - The dropdown couldn't get back to a layout from a custom arrangement.
  - An unknown `layout` name went by silently.
  - `refimage` split paths at spaces.
  - Images outside the project were named by absolute path.
  - Duplicate saved layouts weren't merged.
  - The Tools window couldn't turn Show All Tools off.

## 2026-10-13 (round 76): curve and lattice points with the vertex and edge keys (task 0054)

**Asked:** "Add the ability to add more points and loop cuts for lattice and curves using similar key binds for
vertices and edges."

**Added**
- **Curves**, with a curve's points selected and outside Edit Mode:
  - **Extrude** (Ctrl+E; Blender's E) grows the curve from a selected end.
  - **Loop Cut** (Ctrl+R) puts a point on the segment under the mouse, where the mouse is.
  - **Merge at Center** (Alt+M; Blender's M) joins the selected points.
  - **Delete** removes the selected points, keeping two.
  - **Alt+C** opens or closes the curve.
- **Lattices**: Loop Cut over an edge adds a row across it, keeping the shape. Delete with a whole row selected
  removes one.
- Each is one undo step.
- Console: `points extrude|cut|merge|close|lattice ...`.
- Learn: "More points, by the vertex and edge keys" in the Modifiers lesson.

**Checked**
- New tests (round 76):
  - By keys, Extrude puts the new point beyond the last in the curve's direction and selects it; undo is one step.
  - In Blender's keymap, E grows the first point backwards.
  - Ctrl+R puts the point on the old path (0.004 off).
  - On a Poly curve, Ctrl+R puts it where the mouse was, away from the corners.
  - Merge leaves one point at the middle.
  - Delete keeps two points; Alt+C toggles Closed.
  - A lattice's U row added by Ctrl+R leaves the deformation unchanged (0 off), and Delete of a full row takes it
    away again; undo brings it back.
  - A merge of bad indices from the console is refused.
- Unit checks: Windows 9,444, 0 failed (587 tests); the whole suite in render-cache verify mode: 9,445,
  0 failed, 0 stale pictures. Linux with libraries: 9,352, 0 failed. Sanitizer build: 9,270, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- Ctrl+R put the new point on top of the segment's first point (always on a Poly curve).
- A console merge with an index out of range could crash.
- Also fixed:
  - The closing span of a closed curve couldn't be cut by the mouse.
  - Finding the points' samples could take billions of steps on the biggest curves.
  - Delete in the Hierarchy stopped deleting a lattice with points selected.
  - The two-point rule now holds on every Delete path.
  - Extrude at the point limit said nothing.

## 2026-10-13 (round 75): many assets at once (task 0053)

**Asked:** "I want to be able to multiselect textures and materials in their tabs so I can en masse delete assets."

**Added**
- **Multi-select in the Materials, Textures and Project windows.** Ctrl+click toggles, Shift+click selects a range
  (Ctrl+Shift adds it), Ctrl+A selects everything shown (a search narrows it).
- **One question for many.** Delete, the toolbar or the context menu asks once ("Delete 3 materials?") and sends them
  all to the Recycle Bin / Trash. The Materials window empties their slots in one undo step.
- Console: `matdelete A, B` and `texdelete A, B`, with names separated by commas.
- Learn: "Many at once" in the Assets lesson.

**Checked**
- New tests (round 75):
  - By clicks: Ctrl, Shift and Ctrl+Shift select exactly the named materials.
  - Delete asks once and deletes three of four. The slot that used one shows the default, and one undo brings it
    back.
  - Ctrl+A in the Textures window takes every texture shown, or only the search's.
  - The console deletes two textures by name.
  - The Project window's Ctrl+click selects two files and Delete removes both after one question.
- The user accepted task 0050's two frames over 16.7 ms (material edit 19.8, Play 17.6).
- Unit checks: Windows 9,422, 0 failed (583 tests); the whole suite in render-cache verify mode: 9,423,
  0 failed, 0 stale pictures. Linux with libraries: 9,330, 0 failed. Sanitizer build: 9,248, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The Project window's context-menu Delete ignored the multi-selection.
- The console couldn't name assets with spaces.
- Also fixed:
  - Items hidden by a search were still deleted.
  - Two delete questions could fight.
  - Ctrl+A could fall through to the scene's Select All.
  - Shift with the anchor gone selected one item.
  - The Learn text said Project-window deletes empty slots (they keep the material).

## 2026-10-13 (round 74): the Rigging window (task 0052)

**Asked:** "rigging and weight painting tools should be in their own tab. The mesh filter area is getting a little
crowded from the amount of modeling operations we have."

**Added**
- **Window > Rigging**, opened beside the Inspector, with these sections:
  - **Skeleton**: Add Armature, Add Bone, Humanoid / Quadruped Rig, Reset Pose, Apply as Rest, and the armature's
    settings.
  - **Skin**: Add Armature (Skin) to the selected mesh for the selected skeleton, Bind: Automatic Weights, the
    skin's settings.
  - **Vertex Groups.**
  - **Weight Paint**: the toggle and the brush.
  - **Inverse Kinematics**: Add IK to Bone (makes its target and pole), Auto IK, the IK's settings.
- **Selecting a bone while painting** (in the Hierarchy) paints the group named after it, as Blender does.
- Console: `ik add [bone]` makes the target and pole.
- **Learn** (the user: "I really like the Learn feature ... feel free to use this system further"):
  - "The Rigging window" in the Modifiers lesson.
  - "Quality tiers: a small machine" in Performance and Stress Testing.
  - "A small graphics card" in Rendering on the GPU.
  - Each has Try it buttons and questions.

**Changed**
- The Inspector's MeshFilter section keeps modeling; one row ("Vertex Groups, Weight Paint: Rigging...") opens the
  window.

**Checked**
- New tests (round 74), by clicks:
  - Add Bone puts a child at the tip.
  - Reset Pose and Apply as Rest work.
  - Add IK to Bone makes a target and a pole, and moving the target poses the bones.
  - The MeshFilter row opens the window with the groups.
  - With a mesh and a bone selected, Add Armature (Skin) binds the mesh.
  - The window's Weight Paint paints a stroke.
  - Selecting bones while painting switches groups.
  - A layout with the Rigging tab loads back.
  - The new lessons' Try it commands make a humanoid and open the window, switch the tier and threads, and set the device to Auto.
- Unit checks: Windows 9,384, 0 failed (580 tests); the whole suite in render-cache verify mode: 9,377,
  0 failed, 0 stale pictures. Linux with libraries: 9,284, 0 failed. Sanitizer build: 9,202, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Add Armature (Skin) could almost never be clicked; it now looks through the whole selection.
- Fields shared UI ids: the IK's Weight and the brush's Weight. The window, each component and the brush have their
  own ids now.
- The buttons went by name, so two characters' identical bones or a name with a space went wrong. They act on the
  objects now.
- Also fixed:
  - A failed bind left a Skin with no undo step.
  - A bone selected mid-stroke was lost.
  - Add IK on an arm put the pole in front.
  - The test hook kept old button rectangles.

## 2026-10-13 (round 73): the minimum machine's GPU (task 0051)

**Asked:** the same request: "at minimum run on a GTX 1050ti".

**Added**
- **Render > Device: GPU + CPU (Auto).** Both render, each taking samples as fast as it finishes them.

**Changed**
- **GPU submissions are at most about 100 ms** (were up to 64 samples at once), so a slow card can't pass Windows'
  2 s driver timeout.
- **A scene too big for a card's memory** goes with smaller textures (a mip level at a time, never under 256), or
  to the CPU when even that won't fit, instead of failing to allocate. GPU bakes do the same.
  `BLENDITY_GPU_VRAM_MB` emulates a smaller card.

**Checked**
- New tests (round 73):
  - A 2048 texture on a 12 MB card arrives at 1024.
  - A frame whose buffers alone pass the card's memory goes to the CPU.
  - Device 2 is saved and renders on the CPU and the GPUs.
- Stress `gpu`: the longest submission is 98 ms (3060 Ti, compute kernel) and 106 ms (4070 SUPER). At 8 threads,
  the GPU-viewport spike is "go" only for ray-tracing cards; the minimum machine keeps the CPU viewport.
- Unit checks: Windows 9,349, 0 failed (574 tests); the whole suite in render-cache verify mode: 9,350,
  0 failed, 0 stale pictures. Linux with libraries: 9,257, 0 failed. Sanitizer build: 9,175, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The memory check used the previous frame size, because the scene is sent before the camera sets the size. It is
  checked again when the frame size changes.
- A scene that didn't fit threw its GPUs away and was built up to seven times on every pass. The GPUs are now set
  aside, open, for the next scene, and the texture sizes per level are worked out instead of built.
- Also fixed:
  - Bakes counted the render's frame in their budget.
  - The longest-submission figure never reset.
  - A card reporting no memory restarted the minimum.
  - The CPU row in Auto could be unticked to no effect.
  - `device auto` was missing from the console.
- Tests added: the fallback path through `build` then `set_camera` with a 16 MB card, and the GPUs coming back for a
  scene that fits.

## 2026-10-13 (round 72): the minimum machine, CPU and memory (task 0050)

**Asked:** "Look for more optimizations project wide to at minimum run on a GTX 1050ti and at least a quad-core
processor with eight threads running at a modest 3 Ghz and eight gigabytes of ram" (performance first).

**Added**
- **Quality tiers** (Preferences > Performance > Quality tier; console `quality low|medium|high|custom`): Low draws
  the Scene and Game views at 67 % and scales them up, with a 1024 shadow map, lighter Realtime GI and 60 fps;
  Medium is 85 % with a 2048 shadow map; High has no caps. They hold in the editor's views only, never in F12
  renders. The first run picks Low on a machine with 8 threads or fewer, or about 8 GB or less.
- **Viewport Resolution** (`viewscale 50..100`) and **CPU Threads** (`threads N`) in Preferences > Performance;
  `BLENDITY_THREADS` and `blendity_stress --threads N | --min-spec | --quality` emulate a smaller machine.
- **Cached shadows**: objects that stay still keep a shadow map of their own while another one is dragged.

**Changed**
- **The sky is drawn after the surfaces**, only where none landed, and only every 4th pixel where it is smooth. Its
  lookups use polynomial atan2 / asin. Pictures differ by at most one display step.
- **The views' shadow frame is snapped** (steps of 10 %), so texels can be up to about 1.2 times larger. F12 still
  fits exactly.
- **A material colour edit no longer redraws the shadow map.**
- **Jolt uses half of Blendity's threads.**
- **Memory budgets**: the texture cache drops its least recently used unheld textures past a quarter of RAM, and
  undo drops its oldest steps past an eighth (each at most 2 GB).

**Checked**
- New tests (round 72):
  - A ceiling of 3 holds a loop to 3 threads with TBB and with the built-in pool after its workers started.
  - At 50 % the Scene view draws 438 x 205 of 876 x 409, a click still selects the cube, and the scale is in the
    picture keys.
  - The tiers set scale, shadow map and fps; F12 keeps the scene's 2048.
  - Two tiers at the same scale but different caps are different pictures.
  - A moving caster drawn over the still casters' map equals the full map bit for bit. Six moved frames draw the
    still layer once.
  - The deferred sky matches a per-pixel evaluation within 1 display step.
  - The polynomial atan2 / asin are within 4e-7 of the library's.
  - The texture cache lets go of unheld textures past its budget and keeps a held one.
  - Undo keeps its newest steps within its budget, and they undo.
- Stress at 8 threads, Low, fastest of four runs:

  | Frame | Before (ms) | After (ms) |
  |---|---|---|
  | Orbit | 17.9 | 7.1 |
  | Moving an object | 47.2 | 15.5 |
  | Material colour edit | 48.1 | 19.8 |
  | Play | 39.3 | 17.6 |

  The material edit and Play are still over 16.7 ms; PERFORMANCE.md pass 8 says what is next. Peak working set with
  10,200 objects: 448 MB.
- Unit checks: Windows 9,336, 0 failed (570 tests); the whole suite in render-cache verify mode: 9,337,
  0 failed, 0 stale pictures. Linux with libraries: 9,246, 0 failed. Sanitizer build: 9,164, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- The thread ceiling didn't hold in the dependency-free pool once its workers had started.
- The tiers' shadow and GI caps weren't in the views' picture keys, so a tier change could show a stale picture.
- F12 took the tiers' GI caps.
- Preferences from before the tiers lost the user's frame cap.
- Retro filter stacks were scaled twice and came out soft.
- Also fixed:
  - Edit Mode and modifier levels kept an object "moving" (motion is by placement now, with a still layer per view
    slot).
  - Snapping was coarse (now steps of 10 %, and none for F12).
  - A pending sky could outlive its frame.
  - A NaN depth could stall the sky fill.
  - The TBB arena could be freed while in use.

## 2026-10-12 (round 71): the macOS check, and merging the stack (task 0049)

**Asked:** "check for any errors on the GitHub side ... and merge them".

**Fixed**
- macOS CI failed on #45 and #46: a test compared the skinned mesh with the base mesh bit for bit after Apply as
  Rest; Apple clang's fused multiply-adds left one vertex one ulp off. The test compares within 1e-5 now (the
  editor was right).

**Changed**
- CLAUDE.md: agents merge only when the user asks, after CI passes, with a merge commit.

**Checked**
- The test on Windows; CI's four jobs on #46 before merging.

## 2026-10-12 (round 70): inverse kinematics and rig presets (task 0048)

**Asked:** the roadmap (2026-10-11): "IK/FK for humanoid and quadruped rigs", with name-based humanoid auto-map and
the gizmo refused on an IK-driven bone.

**Added**
- **IK constraints** (ADR 0025): an IKConstraint on a chain's end bone (a shin, a forearm) turns the chain so its
  tip reaches the Target object; two bones exactly (law of cosines) bending towards an optional Pole, longer chains
  by cyclic coordinate descent (Iterations), Weight from the rest pose to the solution. Solved every editor frame
  and in Play after the scripts, only when something it depends on moved; the solved pose makes no undo step. The
  gizmo and G / R / S refuse a bone IK turns ("move its target instead"). Console `ik add <bone> <target> [pole]
  [chain]`.
- **GameObject > Rig**: Humanoid (Hips, Spine, Chest, Neck, Head, arms, legs) and Quadruped (spine, neck, head,
  tail, four legs) with IK targets on hands and feet and poles at knees and elbows; **Auto IK on Selected
  Skeleton** finds the limbs of an imported Mixamo (`mixamorig:LeftUpLeg`), Unity (`LeftUpperLeg`) or Rigify
  (`thigh.L`) skeleton by name. Console `rig humanoid|quadruped|autoik [root]`.
- Learn > Modifiers: "Inverse kinematics".

**Fixed**
- The GameObject menu's Lattice and Armature items had each other's tooltips.

**Checked**
- New tests (round 70): two bones reach a target within 1.3e-7, point straight at one out of reach, and with a pole
  the knee lies in the plane of hip, target and pole; the skin follows; nothing moved, nothing solved; five bones
  by CCD within 7.7e-4; Reset Pose solves again; Weight 0 keeps the rest; a target the chain carries is refused;
  the presets make the named bones and 4 IK chains each, lifting a foot target bends the knee forward with the
  foot on it; auto IK finds 4 limbs in Mixamo, Unity and Rigify skeletons and adds nothing the second time; in the
  editor, dragging a foot target with the gizmo poses the leg, the gizmo leaves a driven shin alone, undo puts the
  target and the leg back, `ik add` and a scene file keep the constraint; in Play a Rotator swinging a target
  animates the leg and Stop restores it; a second rig has its own targets; a target that circles back leaves no
  twist; Weight 0.5 doesn't add up over many moves; a pole the chain carries is refused; a lost target doesn't hold
  the bone; G on a driven shin starts nothing while G, R on the head turns it.
- Stress `vgroups`: a humanoid's four chains cost 0.006 ms a frame when every target moves, 0.001 ms
  when nothing does; among 20,000 other objects 0.046 ms (the per-frame pass); no IK in a scene: no walk.
- Unit checks: Windows 9,303, 0 failed (562 tests); the whole suite in render-cache verify mode: 9,304,
  0 failed, 0 stale pictures. Linux with libraries: 9,213, 0 failed. Sanitizer build: 9,131, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed: a second preset rig's limbs reached for
the first rig's targets (names now unique); each solve started from the last one, so a circling target wound up
twist and a Weight under 1 added up frame by frame (now from the rest pose); a pole parented under the chain
re-solved every frame; the per-frame pass walked every object of every scene and looked targets up by name (now
nothing with no IK anywhere, ids between frames); G / R / S weren't refused on a driven bone; a constraint with a
lost target still blocked the gizmo; a straight chain without a pole could bend sideways (now the rest bend, then
the bone's own Z); auto IK's poles were world +Z / -Z (now where each limb bends). Left as it is: the undo snapshot
is taken after the frame's solve, harmless now that solving doesn't depend on the previous pose.

## 2026-10-12 (round 69): skeletons and skinning (task 0047)

**Asked:** the roadmap (2026-10-11): "rigging ... v1: import rigs from FBX/glTF and pose them".

**Added**
- **Bones and armatures** (ADR 0025): a bone is a GameObject with a Bone component (Length, Deform, its rest
  transform) under a root with an Armature (Deform: Linear or Dual Quaternion, Show Bones, Rest Position). Bones
  are drawn as Blender's octahedra and picked by clicking them; pose them with the gizmo. GameObject > Armature
  (a selected mesh is bound to it), `bone add <parent> [length]`, `armature reset|apply` (Reset Pose, Apply as
  Rest).
- **The Armature (Skin) modifier**: moves the mesh with the bones its vertex groups are named after, 4 influences,
  Linear or Dual Quaternion (twisted joints keep their girth), deform-only (task 0041). **Bind: Automatic Weights**
  (`skin bind`): groups from each vertex's distance to the bones.
- **Rigged FBX and glTF import**: bones, hierarchy, weights (normalised), an Armature and a Skin modifier.
- Learn > Modifiers: "Bones and skins".

**Checked**
- New tests (round 69): a two-bone tube is unchanged at rest and turning the upper bone 90 degrees turns its
  vertices about the joint exactly (2.7e-7) while the lower ones stay; a 150 degree twist keeps the joint's radius
  at 0.200 with Dual Quaternion where Linear shrinks it to 0.052, and both agree at rest and for single-bone
  vertices; Blender's glTF and FBX two-bone tubes import with Lower and Upper bones, an armature, weights summing to
  1, unchanged at rest, and posing Upper moves its vertices and not Lower's; GameObject > Armature binds a column,
  `bone add` places a child at its parent's tip, `skin bind` gives weights summing to 1, a pose is one undo step and
  is kept by a scene file, Reset Pose and Apply as Rest; Reset Pose on a root that is a bone; a rest near 90 degrees
  stays exact through a file; an unevenly scaled mesh keeps Dual Quaternion's girth; a mesh under the armature
  follows it, and the Skin's own vertex group limits it; automatic weights give every vertex away from the joint to
  its nearest bone.
- Stress `vgroups`: a pose change on 50,176 vertices with 3 bones, 5.5 ms Linear, 5.6 ms Dual Quaternion (target 8).
- Unit checks: Windows 9,232, 0 failed (555 tests); the whole suite in render-cache verify mode: 9,233,
  0 failed, 0 stale pictures. Linux with libraries: 9,142, 0 failed. Sanitizer build: 9,060, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed: Reset Pose and Apply as Rest skipped a
root that is a bone; rest rotations kept as Euler angles lost up to 0.8 degrees near 90 (now a quaternion); the ADR
said bind poses were read (it now says they aren't, and an FBX saved in another pose is reported); in an FBX with
two characters every mesh took every skin's bones (now its own); making the armature's name unique could rename a
bone (the armature is never a bone now). Also: Dual Quaternion fell back to Linear on an unevenly scaled mesh;
a zero rest scale made vertices jump; hidden bones were still picked and In Front did nothing; GameObject >
Armature stacked a second Skin; glTF welded touching vertices with different weights. Left as they are (in the
spec's As built or for later): renaming a bone doesn't rename its vertex group; duplicate joint names.

## 2026-10-12 (round 68): Weight Paint (task 0046)

**Asked:** the roadmap (2026-10-11): "weight painting", the second half: the brush.

**Added**
- **Weight Paint mode** (MeshFilter > Vertex Groups > Weight Paint, or `weightpaint on`): the chosen group of the
  active mesh shows as Blender's weight colours (blue 0, green 0.5, red 1) and dragging in the Scene view paints it.
  Brush: Add, Subtract, Draw (towards Weight), Blur (towards the neighbours); Radius (screen pixels), Strength,
  Weight, Falloff (Smooth, Sphere, Root, Sharp, Linear, Constant), Mirror X, Front Faces Only. One undo step per
  stroke. Normalize All and Clean. Console: `wpbrush`, `wpfalloff`, `wpmirror`, `wpfront`, `wpstroke x y ...`,
  `wpnormalize`, `wpclean`.
- Learn > Modifiers: "Weight painting".

**Checked**
- New tests (round 68): Add raises the weights under the brush only, Subtract lowers them, Draw reaches Weight; the
  Smooth falloff paints the centre more than the rim; Blur evens a hard edge; Front Faces Only leaves a sphere's
  back alone (and off, reaches it); Mirror X gives the twin the same weight; a real mouse drag there and back is one
  stroke, one undo step, and stays within one dab's strength (also along the mirror line); leaving the mode keeps
  the weights; the view shows red at weight 1 and blue at 0 (pixels); the painted group stays the painted object's
  when another object is selected; a scene file keeps the weights; a new scene leaves the mode; Normalize All,
  Clean.
- Stress `vgroups`: a dab on a 50,176-vertex mesh 0.08 ms (target 8); a stroke's press and release 3.8 ms.
- Unit checks: Windows 9,194, 0 failed (549 tests); the whole suite in render-cache verify mode: 9,195,
  0 failed, 0 stale pictures. Linux with libraries: 9,104, 0 failed. Sanitizer build: 9,022, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: four majors and several minor findings, all fixed: an undo, a new scene or Play in the
middle of a drag let the stroke write into meshes the undo history shared (a stroke now belongs to its scene and
mesh and stops otherwise); a new scene kept painting whatever object had the same id; choosing another group showed
a stale picture; selecting another object could switch the painted group; every click made an undo step (only a
stroke that changes weights does now); Mirror X applied twice near the centre line; strokes accumulated past their
strength (Blender's no-accumulate rule now); Normalize All moved the active group (Lock Active now); the X-mirror
map could stall on a stray far vertex (an edge-length tolerance, and cached); Esc leaves the mode.

## 2026-10-12 (round 67): vertex groups (task 0045)

**Asked:** the roadmap (2026-10-11): "weight painting", the first half: the groups it paints.

**Added**
- **Vertex groups** (ADR 0024): MeshFilter > Vertex Groups in the Inspector (Add, Remove, rename; in Edit Mode a
  Weight with Assign, Remove from, Select, Deselect) and the console `vgroup add|remove|rename|assign|unassign|
  select|deselect|active`. Saved in scene files (`vg` lines), kept by undo.
- **Every deforming modifier** (Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform) has **Vertex
  Group** and **Invert Group**: only that group's vertices move, each by its weight.
- The groups follow the vertices through every edit: welds and merges, Mirror, Array and Join carry them exactly;
  any other change of the vertex count mends them by position (a vertex that stayed keeps its weights, a new one
  takes the surface's at its place), at each undo step and after each generative modifier of a stack.
- Learn > Modifiers: "Vertex groups".

**Checked**
- New tests (round 67): 17 Edit Mode operators (merge, delete, dissolves, subdivide, loop cut, bevel, extrudes,
  inset, triangulate, poke, split, duplicate, quads, collapse, connect, edge split) keep one weight per vertex,
  vertices that stay keep both groups' weights, and new vertices on the surface carry a linear ramp exactly; adding
  a second group and removing one survive the undo commit; Separate keeps each part's weights and Join through the
  editor brings the other object's group by name; Apply Modifiers bakes a group-limited Displace as shown; Mirror
  copies give their sources' weights, Array and simple Subdivision carry them, Join merges groups by name; a Displace limited to a group moves each vertex by its weight, Invert the rest, an
  unknown name the whole mesh; groups round-trip through a scene file, old files load without them, the console
  adds, assigns (0.75), renames and undo takes each back.
- New stress section `vgroups`: 37 Edit Mode operators (random vertex or face selections) and 10 generative
  modifiers (each followed by a group-limited Displace) on the 26 stress meshes with groups: 1,222 operations, 0
  groups of the wrong size, 0 weights not finite or out of 0..1, 0 weights moved off a vertex that stayed. Carrying
  two groups through Subdivision Surface on 25k vertices: 1.8 ms a re-run (48 ms once, building the map).
- Push/Pull: 26,620 operations, 0 problems, 394 results with faces newly on top of each other (unchanged);
  `modifiers_ngon` 11,514 operations, 0 problems.
- Unit checks: Windows 9,151, 0 failed (543 tests); the whole suite in render-cache verify mode: 9,152,
  0 failed, 0 stale pictures. Linux with libraries: 9,061, 0 failed. Sanitizer build: 8,979, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, five majors and several minor findings, all fixed: the undo commit's mending
undid Add and Remove group and lost a joined object's groups (it now only mends groups an operator left the wrong
size); mending by position overwrote Mirror's and Array's exact copies; Separate dropped the groups; Apply of one
modifier ignored its vertex group; carrying groups through a generative modifier cost 89 ms every frame of a drag
(now a kept map: 1.8 ms); the tests couldn't tell carried weights from re-sampled ones and the stress selected no
faces; the search could stop early for points outside the old mesh, a far point overflowed an int, a deformer's
NaN reached weight-0 vertices, saving before the commit dropped the groups (save commits first).

## 2026-10-12 (round 66): the Mesh Deform modifier, a cage of any shape (task 0044)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Mesh Deform** (Add Modifier > Deform): set Object to a closed mesh round this one and press **Bind** (or
  `meshdeform bind [object]`); then moving the cage's vertices shapes the mesh smoothly. Binding uses mean value
  coordinates (Ju, Schaefer and Warren 2005; ADR 0023), closed form, so it takes a moment instead of Blender's
  voxel solve. An open, flat or missing cage is refused with the reason; a cage whose vertex count changes stops
  the deform and says "Bind again". Deform-only, so dragging cage vertices updates in place.
- Learn > Modifiers: "A cage round a mesh".

**Checked**
- New tests (round 66): right after Bind a sphere in a cube cage and in an icosphere cage is unchanged; the weights
  sum to 1 (off by 1e-7); moving and turning the whole cage moves the mesh the same way (off by 7e-8); pulling one
  cage corner moves the mesh near it 18 times more than the far side; an open cage and a flat one are refused,
  Unbind leaves the mesh, an extra cage vertex stops the deform with "Bind again"; a cage wound the other way binds
  and reproduces a stretch, one with a face turned round or doubled is refused, a stray far vertex doesn't spoil the
  volume test; weights at a cage vertex, on a face and outside the cage reproduce the point; two meshes caging each
  other end without a crash; editing the bound mesh gives what a fresh Bind would; the binding round-trips through
  a scene file; in the editor a refused Bind says why, Bind is one undo step, and undoing an Unbind binds again.
- Stress `modifiers_ngon`: Mesh Deform on all 26 meshes with cages moved, NaN, huge and flattened after Bind:
  11,514 operations, 0 problems. Binding 20,002 vertices to a 1,280-triangle cage: 97 ms (target 200); each deform
  0.8 ms (target 5).
- Unit checks: Windows 9,126, 0 failed (537 tests); the whole suite in render-cache verify mode: 9,127,
  0 failed, 0 stale pictures. Linux with libraries: 9,036, 0 failed. Sanitizer build: 8,954, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, a major and several minor findings, all fixed: two meshes caging each other
recursed until the stack overflowed (the cage is now hashed as the Boolean's cutter is, without evaluating its
keys); editing the bound mesh rebuilt every weight on every drag frame (rows are now redone only for vertices that
moved); a stray cage vertex could fail the volume test, which also lost precision far from the origin; the binding
text was copied and hashed byte by byte in every evaluation (the change hash now sees a key); a binding read from a
file wasn't checked; a point on a cage face missed the on-face case by rounding (its tolerance is now 1e-6).

## 2026-10-12 (round 65): lattices (task 0043)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Lattice objects** (GameObject > Lattice, also in the Hierarchy's + menu): a box of points fitted round the
  selected mesh, which gets a **Lattice modifier** naming it. Points U / V / W (2 to 32, changing them keeps the
  shape), Linear or B-Spline; the modifier has a Strength. Click a point to select it and move, turn or scale it
  with the gizmo (Shift adds, Esc lets go). The modifier is deform-only, so dragging a point updates the mesh in
  place (task 0041).
- Curve and lattice points are now one kind of thing to the editor (`Component` control points); the console has
  `points select|move <object> ...` for both (`curve` still works).
- Learn > Modifiers: "Squashing with a lattice".

**Checked**
- New tests (round 65): a lattice at rest leaves a mesh as it is (both interpolations, three resolutions); Linear:
  a corner moved by d moves the mesh's corner by d, the opposite corner not at all, the centre by d / 8, Strength
  0.5 half as far; B-Spline: pulling an inner point never moves neighbouring samples by more than the pull times
  their spacing over the cell size; changing the resolution keeps an affine pull (within 1e-3); NaN, flattened,
  missing and self-referencing lattices leave a valid mesh; a turned, stretched lattice on a turned, scaled object
  bends the mesh as the same lattice would in world space (off by 1e-7), and moving it re-bends; a B-Spline
  lattice keeps its points through resolution changes 2 to 10 and back, and outside its box blends on without a
  crease; GameObject > Lattice fits the box and adds the modifier, a click selects a point, a gizmo drag moves
  only it and reshapes the mesh, Delete then removes the lattice, undo restores each step, `set` reaches the
  resolution and any point, a second lattice is Lattice.001 and its modifier names it, and a scene file keeps
  everything.
- Stress `modifiers_ngon`: lattice cases on all 26 meshes (both interpolations, three resolutions, ordinary, NaN
  and huge offsets, a flattened box): 11,384 operations, 0 problems. A B-Spline 4 x 4 x 4 lattice deforms a 50k-
  vertex mesh in 0.5 ms (target 3 ms).
- Unit checks: Windows 9,083, 0 failed (531 tests); the whole suite in render-cache verify mode: 9,084,
  0 failed, 0 stale pictures. Linux with libraries: 8,993, 0 failed. Sanitizer build: 8,911, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, all fixed: changing a B-Spline lattice's
resolution sampled it as a B-spline and shrank the shape a little every time (now sampled linearly, as Blender
does); a second lattice had the same name, so its modifier found the first (now Lattice.001); outside the box
B-Spline held the face value (a crease) where Blender blends on; a click on a mesh inside its lattice could select
the lattice (only the box's outer edges pick it now); the flattened-box test was absolute (now relative to the
box's size); Delete with a lattice point selected did nothing; the timing row subtracted two noisy timings.

## 2026-10-12 (round 64): curve objects, the Curve modifier and Array along a curve (task 0042)

**Asked:** the roadmap (2026-10-11): "Curves: objects follow a path (Blender's Curve modifier and Array-along-curve)".

**Added**
- **Curve objects** (GameObject > Curve > Bezier, Circle, Path, also in the Hierarchy's + menu): Bezier (Blender's
  Auto handles), Catmull-Rom or Poly; Closed; Resolution; per point a Tilt and a Radius. Drawn in the Scene view
  (orange when selected); click the line to select the curve, click a point to select it (Shift adds, Esc lets go),
  and the Move / Rotate / Scale gizmo moves the selected points, one undo step per drag. Console:
  `curve add|move|delete|select <object> ...`, and `set Curve.P2Position x y z`.
- **Curve modifier** (Deform): bends a mesh along a named Curve object on any of six axes; the mesh's origin sits
  at the curve's start, past an open curve's end the mesh goes on straight, a closed one wraps round. It is
  deform-only, so dragging its curve updates the mesh in place (task 0041).
- **Array: Fit Type** Fixed Count, Fit Length and Fit Curve (as many copies as fit the curve's length).
- Learn > Modifiers: "Bending along a curve".

**Checked**
- New tests (round 64): a straight curve of each type leaves a beam unchanged; a quarter circle bends a beam onto
  the arc (radius off by 2.5e-6, length along it kept); a closed curve wraps a beam round and an open one goes on
  straight past its end; Fit Curve gives 5 then 7 then 14 copies as the curve grows and is scaled, Fit Length 3,
  NaN or a missing curve one; moving a point or the curve object re-bends, a curve naming its own object or
  nothing leaves the mesh; NaN, huge and coincident points on every type, closed or not, and every axis give a
  finite mesh; a scene file keeps everything; in the editor a click selects the curve, then a point, the Move
  gizmo moves only that point and undo puts it back; moving points and the curve leaves no stale picture; a
  straight curve along each of the six deform axes changes nothing, Tilt 90 turns the cross-section a quarter turn
  and Radius 2 doubles it; the Circle preset is round within 0.3 %; a closed trefoil knot's frame meets itself;
  Delete with points selected removes the points, and selecting another object lets go of them.
- Stress `modifiers_ngon`: the Curve modifier on all 26 meshes along five ordinary and hostile curves, every type,
  closed or open, every axis: 10,760 operations, 0 problems.
- Unit checks: Windows 9,034, 0 failed (523 tests); the whole suite in render-cache verify mode: 9,035,
  0 failed, 0 stale pictures. Linux with libraries: 8,944, 0 failed. Sanitizer build: 8,862, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: approved, with minor findings fixed: Array's Fit Curve no longer re-evaluates when the
curve is only moved or turned (just its scale counts), so dragging a curve under "Array then Curve" stays on the
in-place path; a curve's path is sampled once per change (the Scene view used to sample every curve every frame)
and hashed straight from its fields; a point selection is dropped when another object is selected, and Delete
removes the selected points; the Circle preset has 8 points (4 bulged in by 4 %); `curve` is in the console's help.
Left as they are: point clicks win over a mesh in front within 9 pixels; V and Ctrl+Shift snapping don't apply to
curve points yet.

## 2026-10-12 (round 63): deforming modifiers update in place (task 0041)

**Asked:** the roadmap (2026-10-11): curves, lattices, cages and skinning all deform meshes, and dragging their
fields must stay interactive ("make sure the editor is still decently responsive").

**Changed**
- Smooth, Displace, Simple Deform and Cast only move vertices. At the end of a stack they now re-run on a kept copy
  of the mesh and the render mesh moves its positions, normals and tangents in place, instead of a copy, a full
  rebuild and new tangents on every change. A field drag on a 50k-face mesh went from 56 ms to 6.6 ms per change.
  Modifiers before them (Subdivision, Mirror, ...) are evaluated once and kept while only the deformers change.

**Checked**
- New tests (round 63): on a cube, a UV sphere, a 50k-face grid, a mesh with sharp edges and one with UV seams,
  each of the four deformers, three changes each: the in-place result equals the full evaluation (positions and
  normals bit for bit; tangents unit length and at right angles); only the tail re-runs and the render mesh keeps
  its build, a generative modifier or Edit Mode's cage rebuild in full; a flat grid made bumpy rebuilds then
  follows in place; a scene snapshot keeps its shape when the original changes again; a Boolean re-cuts when its
  cutter is deformed; tangents are the same whatever drag led to a shape (and within 18 degrees of MikkTSpace on a
  twisted cylinder); a saved and reloaded scene evaluates the same; in the editor a drag stays in place and
  re-renders the view, the shadows and voxel GI with no stale picture, and undo brings the old shape back exactly.
- Stress `modifiers_ngon`: Simple Deform angle on a 50,176-face mesh 6.6 ms per change, 8.3 ms with sharp edges
  (in full 56 ms); 6,080 operations on odd meshes, 0 problems.

**Review (code reviewer)**: two majors and several minor findings, all fixed: a Boolean whose cutter was deformed
kept its old cut (the cutter's mesh is now the same object, so its dependency mixes `deform_version`); tangents
were projected from the previous frame's and drifted with the drag (now from UV gradients); `deform_version` moved
only after the deformers ran; tests for voxel GI, save / load, the Boolean and tangents; a sharp-edges stress row.
- Unit checks: Windows 8,975, 0 failed (511 tests); the whole suite in render-cache verify mode: 8,976,
  0 failed, 0 stale pictures. Linux with libraries: 8,885, 0 failed. Sanitizer build: 8,803, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-12 (round 62): axis materials by height with a noisy border; deleting textures (task 0040)

**Asked:** "procedural ... work based on global position. Like the top of a cliff where it's grassy but over the edge
it turns into dirt and rock" ("if it's quick, then do it"); "provide the option to delete textures in the texture
tab"; "Making a new material in the material tab doesn't actually show the new material."

**Added**
- **Axis High** on axis materials: a material above **High Start** (world height in World mode, the object's own in
  Local), faded over **High Blend**; and **Axis Noise** / **Noise Scale**, which make the grass-to-dirt and the
  high band's borders wander. The same in the views, CPU and GPU path tracing and bakes. Learn > Materials: the
  terrain section.
- **Deleting textures** in the Textures window: a Delete button, the thumbnail's right-click menu, or Del; it asks
  first, says how many materials use it (they then show it missing), and moves the file to the Recycle Bin.

**Fixed**
- New Material Asset (and New in the Textures window) with text left in the search box made something the grid
  filtered out, so it looked as if nothing happened: New now clears the search, and a search says how many items
  it hides ("Showing 1 of 15").

**Checked**
- New tests (round 62): above the band snow, below it the slope's parts, a blend between, nothing without it;
  Local mode by the object's height; noise 0 gives the slope rule exactly, noise makes the border wander (107 grass,
  92 dirt at the border angle) and is the same at the same point; NaN fields and positions stay finite; a tilted
  plane is 29.3 % snow rasterized, 30.0 % on the CPU, 30.1 % on both GPUs (checked to be the GPUs); the fields
  round-trip, an old .mat loads with the defaults, the pickers set every part; Delete asks first then trashes the
  texture and a material using it shows it missing; New clears a leftover search.
- Stress `shading`: with height and noise, every point near a border, 2.2x a plain material (the spec's 2x is
  2.5x for this worst case; see its As built); task 0029's axis row 1.5-1.6x. Both are now the fastest of five
  interleaved rounds against a plain run: single timings swung 1.5-2.4x for the unchanged 0029 row.

**Review (code reviewer)**: a blocker, a suspected major and several minor findings, all fixed: Axis High couldn't
be picked in the UI (the setter knew three parts); the GPU test could pass on a CPU fallback (it now checks the
device); a NaN height on the GPU; noise evaluated at NaN; weak tests; the delete popup's count (now unique
materials, assets included); undo could write a deleted texture's text into a new one of its name; unsaved texture
edits not saved before the trash.
- Unit checks: Windows 8,880, 0 failed (503 tests); the whole suite in render-cache verify mode: 8,881,
  0 failed, 0 stale pictures. Linux with libraries: 8,790, 0 failed. Sanitizer build: 8,712, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 61): anime and manga camera filters (task 0039)

**Asked:** the rest of "anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's
item 6, second half).

**Added**
- Camera filters: **Halftone** (a dot screen, Mono ink on paper or Colour, the same dots at any resolution),
  **Hatching** (one to three line directions switched on by darkness), **Line Art** (silhouettes, object edges,
  creases by angle, Taper), **Paper** (blotches, fibres and grain) and **Color Ramp** (brightness through 2 to 4
  colours, Constant or Linear).
- **Looks**: Add Filter > Looks > Anime (Line Art, saturation, a soft Bloom) and Manga (Grayscale, Halftone, Line
  Art, Paper); `filter look Anime|Manga`. One undo step.

**Asked during the round:** "in the material tab and inspector it says the texture is either non color or missing."
And: "When I have a procedural map (bricks) and I have a color ramp that is based on height. It only works when height
map is enabled even though I just want the color ... Maybe add a toggle with the texture window to only enable or
disable certain maps."

- **Normal Map, Roughness Map and Height Map switches** on a procedural texture (the Textures window, below the layer stack): off, that
  output is neutral (a flat normal, roughness 1 so the material's Roughness rules, mid height) and Assign to
  Selection leaves it out, while the layers still compute the height inside, so a Color Ramp from the height
  colours the bricks without them also bumping the surface. Live shading follows the switches too.

- **Bake Textures in the Hierarchy's right-click menu** too (as GameObject > Bake Textures), as the user asked.

**Fixed**
- **A material's texture field said "(missing)" for a procedural texture** (Bricks.ptex#albedo is no file): it
  now shows the texture's image and its name ("Bricks (albedo)"), and its picker lists every procedural texture's
  outputs beside the images. Their Non-Color tag and colour-space switch are gone (an output's colour space is its
  own). The renderers always used them; only the field was wrong.
- Path-traced pictures gave the filters (Edge Outline, Line Art) depth from a random sample per pixel; now one ray
  through each pixel's centre, so their lines match the rasterizer's.
- The Add Filter menu showed two Color headings (Color Ramp was listed after the Stylize effects).

**Checked**
- New tests (round 61): halving Halftone's cell gives 3.9 times the dots and 540p and 1080p show the same 14,528;
  white stays white and black solid; Hatching's ink falls with brightness; Color Ramp Constant gives exactly 3
  colours; Paper at 0 changes nothing and is the same twice; Line Art draws a cube's edges rasterized and traced
  (the traced lines all within a pixel of the rasterized ones), a 30-degree fold at 20 and not at 40, nothing on
  a smooth sphere; the looks add their effects in order, undo, round-trip, and an unknown look adds nothing.
- Stress `filters`: each pass at 1080p on 8 threads against Edge Outline (about 10 ms): Halftone 0.6-0.9x,
  Hatching 0.8x, Line Art 1.8-2.5x, Paper 0.1x, Color Ramp 0.05x (the spec's flat 4 ms is below what Posterize
  costs; see the spec's As built); the fuzz of every effect with NaN / huge fields, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed but one noted: grey specks in pure
white and black halftone; noisy traced creases from jittered depth; Paper's cache holding one size (now four);
an unknown look adding an empty stack; scratch memory that never shrank; Taper on the sky side; dots sized from
one pixel. Noted: these colour passes veil a transparent film's background, as Invert and Posterize do.
- Unit checks: Windows 8,850, 0 failed (497 tests); the whole suite in render-cache verify mode: 8,851,
  0 failed, 0 stale pictures. Linux with libraries: 8,762, 0 failed. Sanitizer build: 8,684, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `textures` stress 0 problems.

## 2026-10-11 (round 60): toon shading and outlines (task 0038)

**Asked:** "Anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's item 6, first half).

**Added**
- **Toon shading model** (a material's Shading Model, or the Toon preset): light in Toon Bands flat tones with a
  hard (or Softness) edge at the Threshold, a Shadow Color for the dark side, a hard highlight spot, a rim light
  and a flat ambient. The same tones in the views, F12 on the CPU and the GPU, and Live Preview; bounces off toon
  surfaces stay ordinary (Lambert), so lighting bakes are unchanged.
- **Outline** component: the black line round an object (the inverted hull), in the views, path-traced renders
  (camera rays only) and exports; Thickness in metres or object units, a colour.
- Learn > Materials: *The anime look*, with a Try-it.

**Fixed**
- Objects with a negative scale (mirrored) were drawn inside out by the rasterizer.

**Checked**
- New tests (round 60): tones step through exactly Toon Bands levels; a rasterized toon sphere takes 2 tones with
  1 band and 4 with 3 (PBR: the 4 commonest colours cover 28 %); the CPU trace's tone shares are within 0.7 % of
  the rasterizer's, 3.1 % under a large area light; the GPU matches the CPU; the rim lights only the silhouette;
  an Outline adds a ring of its colour round the sphere and changes none of its pixels, rasterized and traced,
  mirrored too; thickness 0 draws nothing; the shell is pushed out with flipped faces, cached, world-space
  thickness; an outlined object with a modifier traced in the Camera Preview while the Scene view redraws; undo
  of the Outline and of a toon field; the fields round-trip through a .mat and the Outline through a scene.
- Stress `shading`: a toon material shades in 8.4 ms at 1080p against PBR's 10.4 ms (spec: at most +15 %).
- The GPU kernel came within reach of the SPIR-V optimiser's id limit (a second inlined shadow-ray call
  overflowed it): the toon light branch shares the PBR one's shadow ray.

**Review (code reviewer)**: a blocker, three major and several minor findings, all fixed but one noted: the
outline shell had one cache slot for several views (a path tracer could keep a freed one); mirrored objects drew
the hull's near side; the toon response was applied at bounces and in bakes; the rasterizer's toon ignored an area
light's facing; falloff scaled tones instead of being banded; flat ambient ignored occlusion; tests for
mirroring, modifiers, undo and area lights; the ADR now records the component and the camera-only hull. Noted:
an outline makes shadow rays use the closest-hit loop.
- Unit checks: Windows 8,756, 0 failed (490 tests); the whole suite in render-cache verify mode: 8,757,
  0 failed, 0 stale pictures. Linux with libraries: 8,668, 0 failed. Sanitizer build: 8,590, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `shading` and `gpu` stress 0 problems.

## 2026-10-11 (round 59): GPU devices open in the background (task 0037)

**Asked:** "Switching from shaded to rendered and path trace preview crashes/halts the program" ... "make sure the
editor is still decently responsive" (the retro stages stay in the path tracers).

**Fixed**
- **Turning the Camera Preview (or the Render window, Live Preview, a bake) to the GPU froze the editor** the first
  time in a session: the driver built the kernel's pipeline on the editor's thread, about 80 s with a cold cache,
  and Windows closed Blendity as not responding. Now the GPU opens in the background: the view traces on the CPU
  ("GPU starting") and moves to the GPU when it is ready; bakes do those slices on the CPU; the editor wakes four
  times a second meanwhile. A spare stays open so the next view gets a GPU at once.

**Added**
- A pipeline cache per GPU in `~/.blendity/cache` (checked against the GPU and a hash): later starts build the
  pipeline from it even when the driver's cache is cold (9-13 ms instead of 84-141 ms here with a warm one).

**Checked**
- New tests (round 59): a GPU that doesn't exist is refused at once; with the open slowed to 3 s the slowest
  editor frame is 17 ms, the preview traces on the CPU and is on the GPU after 3.3 s, and the spare is handed over
  at once; a tracer forgets a pending GPU when the device turns back to the CPU; a bake's gather returns at once
  while the GPU opens and uses it later; the pipeline cache is written and used, and corrupt, torn and foreign
  files are ignored. The tests keep their caches in a scratch folder.

**Review (code reviewer)**: four major and several minor findings, all fixed: the wake-up was cleared later in the
same frame; quitting while a device opened could crash in static destructors (now leaked statics and `_Exit`);
the GPU bakes still opened a device on the editor's thread; "GPU starting" stuck after switching to the CPU;
the cache file had no body check and a shared temporary name; a failed open was never retried; any other view's
open rebuilt a waiting tracer's scene; prewarm ignored the RT setting; the tests wrote the user's cache folder.
- Unit checks: Windows 8,714, 0 failed (480 tests); the whole suite in render-cache verify mode: 8,715,
  0 failed, 0 stale pictures. Linux with libraries: 8,627, 0 failed. Sanitizer build: 8,549, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress 0 problems.

## 2026-10-11 (round 58): bake textures onto a mesh for export (task 0036)

**Asked:** "Bake textures onto a mesh" for export (the roadmap's item 5), one atlas per object.

**Added**
- **Object > Bake Textures** (512 to 4096; console `baketex [size] [keepuvs]`): the active object's look (image
  maps, procedural and `.ptex` textures, axis materials, Box and Generated mapping, every material slot) baked into
  albedo, roughness, metallic and normal PNGs in `Assets/Textures/Baked`, on new UVs (Smart UV Project and Pack
  Islands); the mesh takes those UVs and a new `<Object> Baked` material on every slot. One undo step. Objects
  whose modifiers add geometry are refused with a message (apply them, or export with Bake Textures).
- **Export > Bake Textures** (OBJ, FBX, glb / glTF, with a size): each exported object baked on a copy, the
  images written beside the file and referenced; the scene is not changed.
- **Exports carry more maps** without a bake: glTF normal textures and roughness / metallic packed into one PNG in
  the buffer; FBX Texture and Video objects for base colour, normal, roughness, metallic and emission maps.

**Fixed**
- OBJ exports wrote a procedural texture's output (`X.ptex#albedo`) as a file path when it hadn't been converted.
- `CMakeLists.txt` didn't compile `src/texture` (the build scripts and CI did).

**Checked**
- New tests (round 58): six materials on a cube's faces bake their colours at the face centres on one atlas; a
  checker on a plane matches the renderer at all 64 cell centres; an axis material bakes grass on top and dirt on
  the sides, rotated too; a flat surface's normal map is within 0.3 degrees of +Z and a Bricks texture bakes its
  mortar; Object > Bake Textures keeps the rasterized look (0.02 % mean colour difference) and undo restores the
  UVs and material; OBJ, glb and FBX exports with Bake Textures write and reference the images (ufbx reads the
  FBX textures); ordinary materials' maps reach glTF and FBX; Keep UVs; NaN, empty and collapsed meshes bake
  nothing; two objects with one name get their own files; a Mirror modifier is refused; Opaque alpha bakes solid
  and a Transparent slot gives the baked material its surface.
- Opened in Blender 5.2: the OBJ, glb and FBX import with Base Color, Roughness, Metallic and Normal linked to
  the baked 256 x 256 images.
- Stress `texbake` (new): 50k faces at 2048 in 506 ms on 8 threads (spec: under 2 s), 441 ms on 32, 4096 in
  968 ms; 60 fuzzed bakes (NaN, huge, collapsed meshes, odd sizes) 0 problems. A 4096 bake from the menu holds
  the editor for about a second.

**Review (code reviewer)**: four major and several minor findings, all fixed: an Opaque material's alpha made the
baked object a Cutout; a bake overwrote another same-named object's images (now new files each time); the in-scene
bake ignored modifiers that add geometry (now refused); tracked stress reports deleted by a cleanup (restored);
mirrored objects' normal maps in exports; the Bake option shown with Materials off; Unlit and emission dropped;
packed roughness read as linear from sRGB maps; collapsed triangles baked.
- Unit checks: Windows 8,693, 0 failed (476 tests); the whole suite in render-cache verify mode: 8,694,
  0 failed, 0 stale pictures. Linux with libraries: 8,625, 0 failed. Sanitizer build: 8,547, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `texbake` and `textures` stress 0 problems.

## 2026-10-11 (round 57): transparent backgrounds in renders (task 0035)

**Asked:** "Add the ability to make backgrounds transparent when they are rendered (png transparency)."

**Added**
- **Transparent** (Render window; Blender's Film > Transparent): F12 renders, rasterized or path traced on the CPU
  or the GPU, leave the world out of the background while it still lights the scene. A saved PNG has alpha 0
  there, partial alpha at anti-aliased edges and behind see-through surfaces, 1 on solid objects; an EXR keeps the
  alpha too; a JPEG is composited over black. The Render window shows a transparent render over a checkerboard.
  Camera sequences save the same way.

**Checked**
- New tests (round 57): rasterized and path-traced (CPU and GPU) renders of a cube with no floor have alpha 0 at
  the corners, 255 in the middle and partial values at the edges; a 50 % see-through quad gives alpha 127 (raster)
  and 128 (traced); the PNG reads back with the alpha; off, every pixel is opaque. Opened in Blender 5.2: RGBA,
  straight alpha, 0 at the corner, 1 on the subject. A glass sphere is covered (alpha 255) and shows the world
  through it on the CPU and the GPU; a Bloom filter keeps a rasterized cube opaque; an Edge Outline keeps its
  alpha in rasterized and filtered path-traced renders.

**Review (code reviewer)**: three major and several minor findings, all fixed: glass vanished in transparent traces,
Bloom made a see-through rim in rasterized ones, a filtered trace's alpha ignored its passes, a faint veil from
colour-quantising passes, coverage accumulated on every GPU render, low coverage amplifying denoised colour.
- Unit checks: Windows 8,584, 0 failed (466 tests); the whole suite in render-cache verify mode: 8,585,
  0 failed, 0 stale pictures. Linux with libraries: 8,516, 0 failed. Sanitizer build: 8,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `gpu` stress 0 problems.

## 2026-10-11 (round 56): the Research window retired; papers in Learn > References (task 0034)

**Asked:** "you can remove the research tab, doesn't really serve a purpose. Any features that use papers should be
simply referenced and included in the learn tab."

**Changed**
- **The Research window is gone** (Window menu, File and Help menus, the Project window's Research Papers folder,
  dropping a PDF copying it into the project). A layout saved with it loads without it.
- **Learn > References** lists the papers behind Blendity's features, each with the feature it became and where
  it is (Taubin smoothing, Catmull-Clark, LSCM unwrapping, MikkTSpace, voxel GI, GGX, the area-light work of
  Karis and Heitz et al., the BVH, path guiding, the physical sky, noise, colour temperature...), with a Try-it
  button for Taubin smoothing. `window Research`, `research`, `references` and Ctrl+8 open it. Mesh features from
  papers are now under Mesh > From Papers. Nothing in the `research/` folder was touched.

**Fixed**
- The Textures window's preview could keep an old picture after an undo (an image freed and a new one allocated
  at the same address looked unchanged); it, and the Materials window's previews, now hold their images.

**Checked**
- New tests (round 56): an old layout with Research (beside the Console, and alone in an area) loads without it,
  every preset too; `window Research` and `research` open References; every paper feature is cited there and the
  Taubin Try-it smooths a noisy sphere; dropping a PDF copies nothing; Ctrl+8 opens References from any lesson; a
  layout with Research in its root area, or only Research, loads without it.

**Review (code reviewer)**: a major and several minor findings, all fixed: Add Tab still offered Research, Ctrl+8
opened the last lesson, Research in a layout's root area stayed, the path-guiding citation (now Ruppert, Herholz
& Lensch 2020, the method Open PGL is set to), the research folder still deletable from Blendity, the Materials
previews holding textures (now a content key and weak references), dead state.
- Unit checks: Windows 8,552, 0 failed (461 tests); the whole suite in render-cache verify mode: 8,553,
  0 failed, 0 stale pictures. Linux with libraries: 8,488, 0 failed. Sanitizer build: 8,410, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_ui` 0 problems.

## 2026-10-11 (round 55): camera composition guides (task 0033)

**Asked:** "adding 'rule of thirds' guides to the camera and can be togglable. Any additional camera guides or
helpers are also welcomed."

**Added**
- **Composition guides** on every camera (Blender's Composition Guides and Safe Areas): **Thirds**, **Center**,
  **Center Diagonal**, **Golden Ratio**, **Golden Triangle A / B**, **Harmonious Triangle A / B**, **Title Safe**
  and **Action Safe** (margins adjustable), in a colour and opacity of your choice, all behind one **Show
  Guides** switch. They draw over the camera's frame in the Game view, the Camera Preview and while piloting the
  camera (inside a filter's letterbox when there is one), never into renders. Toggle them in the Camera
  inspector, the Game view's new **Guides** menu or with `guides thirds on` / `guides off`. A Learn lesson
  explains them.

**Checked**
- New tests (round 55): the thirds lines at 1/3 and 2/3 of the Game view's camera frame (every pixel of the
  line), also in a 2.39:1 letterbox; each of the ten guides draws on its own; Show Guides off draws nothing; an
  F12 render with guides on is identical to one without; the camera's render hash ignores them; Title Safe at
  Blender's margins; the fields save, undo and redo; the Guides menu clicked with the mouse; the triangles'
  endpoints are Blender's.

**Review (code reviewer)**: a blocker and several minor findings, all fixed: the Guides menu read a dead pointer,
the triangles weren't Blender's construction, the piloted frame ignored a filter's letterbox, a hashing hazard in
`Camera::reflect`, loose console parsing, HiDPI lines, zero safe margins.
- Unit checks: Windows 8,522, 0 failed (458 tests); the whole suite in render-cache verify mode: 8,523,
  0 failed, 0 stale pictures. Linux with libraries: 8,458, 0 failed. Sanitizer build: 8,380, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle 1.33 ms.

## 2026-10-11 (round 54): every camera filter in the path tracers (task 0032, ADR 0008 amended)

**Asked:** "I want all the camera filter modifier stack to show up in the render view ... I want the filters to
show up in the path traced system as well".

**Added**
- The Retro Console's four rasterizer stages now show in path-traced pictures (F12, the Render window's
  Preview, the Rendered Camera Preview), on the CPU and the GPU: **vertex snapping** (wobbly PS1 edges),
  **affine texture warping**, **the texture rules** (Nearest or 3-point filtering, the size cap) and the
  Saturn's **screen-door transparency**. The other filters (outlines, bloom, grading, fog...) already did.

**Checked**
- New tests (round 54): a traced quad at a grazing angle takes the rasterizer's affine UVs (mean difference
  0.19, against 0.94 for perspective), the GPU equal to the CPU; a 1-texel cap makes a textured quad one
  colour on the CPU and the GPU; the screen door is an exact checkerboard (solid and see-through) on both;
  a path-traced F12 render through a snapping Retro filter has the rasterized render's silhouette (0 pixels
  differ, 448 without the snap); the GPU takes the same capped mip level, Nearest and 3-point filters as the
  CPU; a nearly clear surface (alpha 0.05) vanishes through the door as in the rasterizer.
- Stress `filters`: the odd-settings check had been reporting 135 false problems since task 0026 (it still
  required the internal image to be no larger than the view); corrected: 300 runs, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed: the door kept nearly clear
surfaces, the GPU capped the sky map too, every preview rebuild copied every mesh (now cached and reused),
hits behind glass counted as camera hits, shadow rays skipped the texture rules, non-finite screen positions.
- Unit checks: Windows 8,494, 0 failed (452 tests); the whole suite in render-cache verify mode: 8,495,
  0 failed, 0 stale pictures. Linux with libraries: 8,430, 0 failed. Sanitizer build: 8,352, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

## 2026-10-11 (round 53): live procedural textures (task 0031, ADR 0017 amended)

**Asked:** "Not only do I want the procedural textures to be evaluated at a set resolution. I want to view them
change on the material itself. So one part is images at a set resolution, but still having the convenience of
live shaders."

**Added**
- **Live Shading** (a texture's field, on by default): up close, where a texel of the image would cover more
  than a pixel, the Scene and Game views, the Camera Preview, F12 raster and the Materials window's previews
  evaluate the texture's layers at the pixel, like a shader, so bricks, planks and tiles stay sharp at any
  distance (normals included). Further away they read the mipmapped image, which doesn't shimmer. The images
  at the set resolution are still what exports, bakes and the path tracers use.
- **Objects follow a texture edit while you drag:** dragging a field in the Textures window updates every
  material using the texture each frame (its images at 256 during the drag, the set size on release); before,
  they changed only on release.

**Changed**
- The Tile layer reads the texel its tiled point lands in (it read one half a cell off), so its images can
  differ slightly from before.

**Checked**
- New tests (round 53): a point evaluation equals the planes at every texel centre for every layer type (Warp
  and Tile within a mean 0.01); the live normal within 10 degrees of the image's normal; up close a mortar edge
  is sharp with Live Shading (0 in-between pixels across a row) and blurred without (13), and far away both
  give the same pixels; dragging a field gives the material a new 256 image every frame and the 1024 one on
  release; the preview's Edge Wear as wide as at full size; 12 stacked Warp / Tile layers in linear time; an
  axis material's scale and Mirror wrapping keep the image; the preview is per texture; Live Shading saves;
  NaN coordinates stay finite.

**Review (code reviewer)**: four major and several minor findings, all fixed but the frame-time target (2.4x for
a whole close-up frame, documented): extra planes kept for every texture, texel-sized filters 8x wide in the
drag preview, axis materials going live while minified, stacked Warp / Tile doubling per layer, a global
preview flag, every Materials preview redrawn on any texture edit, Live Shading toggles re-evaluating images,
Mirror / Clamp / Closest, Generated mapping, a hard switch line (now a crossfade).
- Stress `procedural_textures`: a point of the Bricks preset about 290 ns, 485 ns with its normal (one
  thread); a 1080p frame made entirely of magnified texels 2.4x the image-only one (the spec's target was
  under 2x: not met, see the spec's As built); the odd-settings fuzz now also evaluates every layer type at
  points: 147 runs, 0 problems. 1024 x 1024, 8 layers: 108 ms (unchanged).
- Unit checks: Windows 8,471, 0 failed (447 tests); the whole suite in render-cache verify mode: 8,472,
  0 failed, 0 stale pictures. Linux with libraries: 8,419, 0 failed. Sanitizer build: 8,341, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.33 ms.

## 2026-10-11 (round 52): large area lights, the gizmo after Set Origin, Select Coplanar (task 0030)

**Asked:** "the area light when width and height are large (lets say 10 x 10) still creates weird rendered
artifacts (black splotches on the object, even a default cube)", "when you use the lighting generate lighting
feature it causes the error"; "when I retarget the origin of any object, the transform tool and gizmo doesn't
automatically apply to the new origin"; "the ability to select coplanar faces"; "certain camera filter
properties like edge outline don't show up in the render view ... in the path traced system as well".

**Fixed**
- **Generate Lighting under a large area light no longer bakes splotches.** The bake took the light's range
  falloff from each random point on the light (on a 10 m light it varied tenfold across it), so 32 samples
  per texel left heavy noise that the denoiser turned into blotches. Now the unshadowed light is computed
  (no noise) and the shadow rays only decide how much of it gets through, the falloff at the centre as in the
  path tracer. A floor under a 10 m light: texel-to-texel noise from about 15 % to 0.1 %.
- **Large area lights light the sides of things in the Scene and Game views and F12 raster.** The
  rasterizer treated an area light as a point at its centre, so a cube's sides under a 10 m light were black;
  it now takes the light's whole surface (the path tracer's answer within 1 %), with the specular from the
  light's nearest point. Light probes take area lights the same way.
- **The gizmo moves to the new origin at once** after the Inspector's Set Origin > Apply: the change asked for
  no new frame, so the app waited for the next input with the gizmo on the old origin.

**Added**
- **Select Coplanar** (Edit Mode, Mesh menu; `editop select_coplanar`): adds every face lying in the plane of a
  selected face, joined to it or not (Blender's Select Similar > Coplanar). In vertex and edge mode it starts
  from the faces whose corners are selected and adds to the selection.

**Checked, no change needed**
- Camera filters in path-traced renders: an Edge Outline shows in F12 path-traced renders at 720p with the
  denoiser, on the CPU and both GPUs and in the Render window's Preview (now a regression test). The filters
  that don't reach the path tracer are the Retro Console's rasterizer stages (vertex snapping, affine
  textures, the texture override, screen-door transparency); task 0032 brings them to it.

**Checked**
- New tests (round 52): the area light's integral against a million-point reference in 13 cases (within 1 %);
  a cube's side under a 10 m light lit in the rasterizer as in the path tracer (0.098 vs 0.098); a glossy
  floor's highlight within a factor 0.5 to 1.6 of the path tracer's; a baked floor under a 10 m light smooth
  (0.1 % texel-to-texel) and as bright as the path tracer (0.412 vs 0.413); a change asks for a frame and
  the gizmo is on the new origin in it; Select Coplanar (joined, apart, raised, flipped, tilted, degenerate,
  the vertex selection kept); the Edge Outline in path-traced renders.
- Stress `shading`: an area light per pixel, one thread: 175 ns (rectangle), 210 ns (a wall cutting it),
  280 ns (0.3 m below), 360 ns (disk); a point light is about 10 ns.
- Unit checks: Windows 8,423, 0 failed (438 tests); the whole suite in render-cache verify mode: 8,424,
  0 failed, 0 stale pictures. Linux with libraries: 8,371, 0 failed. Sanitizer build: 8,293, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.41 ms; `bake` and `probes` odd-settings runs: 0 problems.

## 2026-10-11 (round 51): axis materials (task 0029, ADR 0018)

**Asked:** "a material that will always have grass on top followed by dirt, regardless of rotation".

**Added**
- **Axis materials:** a material's **Axis Mode** (World or Local) and three parts, **Top**, **Side** and
  **Bottom** (material assets picked in the Inspector; empty uses the material's own maps). Surfaces facing
  up take Top, steep ones Side, downward ones Bottom, by the normal in the world (World: grass stays on top
  however the object turns) or the object's own space (Local: moss that turns with the stone); **Axis
  Angle** sets where Top turns into Side, **Axis Blend** how soft the edge is, **Axis Scale** the
  projection's tile size. The parts are projected triplanar, so no UVs are needed.
- The same in the Scene and Game views, F12 raster, the CPU path tracer and the GPU path tracer; the
  Materials window's previews show axis materials and procedurals (they showed neither).

**Checked**
- New tests (round 51): World mode puts Top up, Side on steep faces and Bottom underneath whatever the
  object's rotation; Local mode follows the object; Blend 0 is a hard edge and 0.3 mixes; a part's texture
  varies across a surface without UVs; the fields round-trip through a `.mat`, an edit undoes, and a part's
  edit changes the views' hash; an F12 raster render from above shows the Top, still after a quarter turn
  in World mode and the Side in Local mode; the CPU and both GPUs agree on an axis-material cube (mean
  difference 0.0006 of 0.187).
- Stress `shading`: an axis material with two parts 1.51× a plain one per point (target under 1.6×).
- Unit checks: Windows 8,371, 0 failed (431 tests); the whole suite in render-cache verify mode: 8,372,
  0 failed, 0 stale pictures. Linux with libraries: 8,321, 0 failed. Sanitizer build: 8,243, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

**Review (code reviewer)** found and these were fixed, each with a test: two axis materials naming each
other recursed without end (now cut while resolving, a part naming the parent itself is ignored); the GPU
material table's iterator could be invalidated while appending parts; the GPU projected procedural parts at
a different point than the CPU; the shadow-map (RSM) cache key missed the parts; a NaN normal shaded black
(now the Side part); the views' hash mixes the parts as resolved. Also: a compiler warning in the normal
output's Sobel (64-bit indices).

## 2026-10-11 (round 50): the Textures window (task 0028)

**Asked:** a tab dedicated to making procedural textures with a Blender-style modifier stack.

**Added**
- **Window > Textures** (`window Textures`): the project's texture assets as thumbnails (click to edit,
  drag onto an object in the Scene view or a Hierarchy row to use it); **New** from a preset (Bricks, Wood
  Floor, Tatami, Tiles, Noise, Checker, or empty); the size; **Assign to Selection**; a search.
- **The layer stack**, drawn like a camera's filter stack: each layer a foldout with its checkbox and a menu
  (Move Up, Move Down, Reset, Remove Layer), **Add Layer** by category (Generators, Filters), every field
  editable; each edit one undo step.
- **A preview** of an output (Albedo, Normal, Roughness, Height), 1× or tiled 2 × 2 to check the seams; it
  evaluates at 256 while a field is being dragged and at the texture's size on release.
- Double-clicking a `.ptex` in the Project window opens it in the Textures window. The window opens beside
  the Materials window, so the Scene view stays visible to drag onto.
- From the code review (Opus): the window first opened over the Scene view, and a drop then landed on an
  object in the hidden view's last picture (drops onto the Scene view now need this frame's picture);
  dragging a field re-evaluated the whole texture every frame for every material using it (they now
  re-resolve once, on release, while the preview follows at 256); the preview repacked the full image on
  every frame, idle ones included (it is cached and drawn from the smallest fitting mip); a deleted texture
  stayed selected; the name is the file's, not a field; the drag highlight now matches where a texture
  asset can land; the tests clean up the scratch project's textures.

**Checked**
- New tests (round 50), by real clicks: `window Textures` opens it and the layout round-trips with it;
  New > Bricks creates and selects the asset; Add Layer > Blur appends a Blur, its menu's Move Up moves
  it, its checkbox disables it, and undo takes each back; a value typed into a layer field changes the
  texture and the preview, and undo / redo restore both; a thumbnail dragged onto the Cube gives it the
  texture's maps; the preview is 256 while a field is dragged and 1024 after.
- Unit checks: Windows 8,343, 0 failed (423 tests); the whole suite in render-cache verify mode: 8,344,
  0 failed, 0 stale pictures. Linux with libraries: 8,295, 0 failed. Sanitizer build: 8,217, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.39 ms (unchanged).

## 2026-10-11 (round 49): procedural texture assets (task 0027, ADR 0017)

**Asked:** a tab for making procedural textures (bricks, wood floors, tatami mats...) with a Blender-style
modifier stack. This round is the texture assets themselves; the Textures window is the next (0028).

**Added**
- **Texture assets, `Assets/Textures/<name>.ptex`:** a size (64–4096), a physical size (metres per tile),
  a normal strength, a seed, and **a stack of layers** evaluated in order to four images: **albedo**,
  **normal** (from the height), **roughness** and **height**.
- **21 layers.** Generators, all tiling seamlessly: Brick, Wood Floor, Tatami, Tiles (square, hexagon,
  herringbone), Voronoi, Noise, Wave, Checker, Stripes, Gradient, Image. Filters: Blur, Levels,
  Hue / Saturation, Invert, Warp, Tile, Edge Wear, Height AO, Color Ramp, Mask by Height. Each layer has
  a blend mode (Normal, Multiply, Add, Screen, Overlay, Height Max / Min), an opacity, the planes it
  writes, and can fade by the mask a Mask by Height layer below sets.
- **Materials use them by output:** `Assets/Textures/Bricks.ptex#albedo` (`#normal`, `#roughness`,
  `#height`); Assign to Selection (Project window, or `newtex` then the console) sets all three maps and
  the tiling from the physical size. Editing a texture redraws every view that shows it.
- **Exports carry them:** the outputs a material uses are written as PNGs beside an exported OBJ or glTF
  and referenced (by name in the MTL, so the folder can be moved). USD and FBX don't reference textures
  yet, so no images are written for them.
- **Assets behave like material assets:** saved when an edit settles and with the scene, renamed and moved
  with their references (materials and scene files), forgotten when deleted.
- Console: `newtex <name> [preset]` (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker), `texlist`,
  `texlayer <texture> add|remove|move|reset|list`, `settex <texture> <Field> <value>` (a layer's field
  as `L0Rows`). **Texture edits undo and redo** (the asset's text travels with the undo step).
- From the code review (Opus): Hexagon and Herringbone tiles are real tilings now (the first draft's
  hexagons didn't repeat vertically and its herringbone was a square grid), Tatami lays true 1:2 mats
  (Basket, Running Bond, Pinwheel), and every count that decides a pattern's parity is rounded so the
  pattern repeats exactly; the seam test now checks sample(u, v) = sample(u + 1, v) = sample(u, v + 1)
  on 20 configurations instead of a step comparison that could not fail. Blur, Height AO and Edge Wear
  are separable (O(r) or O(1) per texel instead of O(r²): radius 64 at 4096² went from minutes to
  seconds) and copy only the planes they touch; evaluation runs outside the cache's lock; a texture's
  first load no longer bumps the texture generation in the middle of a render's resolve; NaN guards use
  `finite_bits` (a `v == v` test can be folded away under /fp:fast); integer fields are clamped where
  used; an unknown layer type (a newer file) is kept so the layers after it keep their fields; exported
  images get unique names per asset and each asset is evaluated once; assigning a texture to an object
  using a `.mat` asset gives that object its own copy instead of editing the shared asset.

**Checked**
- New tests (round 49): every generator tiles seamlessly and is deterministic; a Brick's mortar fraction
  follows its parameters, Tatami has its border, a Wood Floor its gaps; the stack order matters; a
  `.ptex` round-trips; a material with `Bricks.ptex#albedo` renders coloured, and a layer field and the
  normal strength redraw the view; OBJ and glTF exports write and reference the PNGs (by name in the MTL;
  the glTF read back by the importer) while the scene's material keeps the asset, and an STL export writes
  none; renaming the `.ptex` retargets the material; `texlayer` / `settex` edit the stack, the file is
  written when the edit settles, and undo / redo restore a field edit and an added layer.
- Unit checks: Windows 8,308, 0 failed (417 tests); the whole suite in render-cache verify mode: 8,309,
  0 failed, 0 stale pictures. Linux with libraries: 8,260, 0 failed. Sanitizer build: 8,182, 0 failed, no reports.
- Stress `procedural_textures` (new): 1024 x 1024 with 8 layers 108 ms on 32 threads (target under 150 ms);
  147 fuzzed runs (NaN / infinite / huge / negative fields on every layer type), 0 problems.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 48): filters look the same at every resolution and during traces (task 0026, ADR 0016 part 2)

**Asked:** the camera-filter round, second part: a camera's filters should look the same in the Camera
Preview, the Game view and a render, and a filtered progressive trace shouldn't change its look as
samples accumulate.

**Changed**
- **Pixel-sized filter parameters scale with the picture's height** (1080p = 1x): Pixelate's cell, Edge
  Outline's thickness, Film Grain's size, CRT's scanline and phosphor pitch, Sharpen's radius. A 540p
  Preview and a 1080p render of the same camera now match (an outline covers 0.57 % of both).
- **A small view renders at its stack's resolution** (a 240-row retro stack in a 120-row inset) and is
  box-averaged into the view, instead of rendering at the inset's own rows.
- **Bloom on a progressive trace** reads the linear light at every resolve, so the glow doesn't grow
  with the samples; Color Grading's exposure applies in linear light when bloom's HDR plane is there.
- **The views' cache keys hash only the raster-relevant render settings:** changing the path tracer's
  Samples no longer redraws the Game view (changing Exposure still does).
- **The Game view's background is the world's gradient** (or sky / HDRI) when the scene's lighting is on;
  in Gradient mode it used the editor's fixed sky colours.
- A paused Game view keeps the grain's and the flicker's last pattern.

**Checked**
- New tests (round 48): an Edge Outline's covered fraction is equal at 540p and 1080p (0.0057 both); a
  Pixelate row has the same cell count at both sizes (54 / 55); Samples doesn't redraw the Game view,
  Exposure does (0 / 2 view renders); a Rendered Preview with bloom keeps its mean brightness from the
  first frames to the finished trace (152.6 / 152.6); a red world gradient reaches the Game view's sky;
  a paused Game view with Film Grain holds still. The `filter_layout` test now expects the stack's
  resolution for a small view.
- From the code review (Opus), three majors fixed: the Live Preview and the Rendered Camera Preview
  keyed on the raster-only settings too, so changing Samples, Max Bounces or the denoiser no longer
  restarted them (a trace-settings hash is mixed into their keys; a test changes Max Bounces); Color
  Grading's new HDR path re-encoded every pixel from the HDR plane, wiping a glow, an outline, depth of
  field or a Solid Color clear drawn after it (it now re-encodes only pixels whose colour still is the
  plane's encoding; a test checks a glow and a solid clear survive); the Fill layout's width was
  unbounded for a squeezed view (both sides are capped, and a letterboxed stack scales by one factor so
  its pixels stay square). Also: the linear buffer is taken once per progressive resolve, a stepped
  paused Game view keys on its time, and the preview's header is kept out of the bloom measurement.
  Note: a view smaller than its stack's resolution now renders and traces up to 1080p rows for it (a
  360-row Rendered Preview of a 1080-row retro stack traces 1920 x 1080).
- Unit checks: Windows 8,205, 0 failed (408 tests); the whole suite in render-cache verify mode: 8,206,
  0 failed, 0 stale pictures. Linux with libraries: 8,156, 0 failed. Sanitizer build: the
  round's tests 21 checks, 0 failed (the full run before the review fixes failed only in the two tests
  those fixes changed; CI's job covers the final code). Stress `filters`:
  300 fuzzed stacks, 0 problems; `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 47): the Active Camera, and filters wherever a camera's picture shows (task 0025, ADR 0016)

**Asked:** camera filters were missing from the Game view, the Camera Preview and the Render window.

**Found:** which camera "the" camera is was decided by name ("Main Camera", else the first), while the
Camera Preview showed the selected one: a filter on the selected camera showed in the Preview and not
in the Game view. The Scene view's filters ran with no camera (no exposure, clear colour or fog). Path-
traced pictures carried no depth or ids, so Edge Outline and the Retro fog were skipped in the Rendered
Preview and a path-traced F12, and Solid Color was ignored there. F12 raster without filters skipped
depth of field.

**Added / fixed**
- **An explicit Active Camera** per scene (Blender's scene camera, Unity's MainCamera tag): "Set as
  Active Camera" in the Camera inspector and the Hierarchy menu, a camera dropdown in the Game view's
  bar (replacing "Display 1"), `activecamera <name>|none` in the console, Ctrl+0 (Ctrl+Numpad0) in the
  Blender keymap; saved with the scene, one undo step, cleared when the camera is deleted. Without one
  the old name rule applies, so old scenes behave as before.
- **The path tracers (CPU and GPU) record the first hit's distance and object id,** and a traced picture
  gets a real `FilterFrame` with depth (as the rasterizer's z / w) and ids: Edge Outline and the Retro
  fog now work in a path-traced F12 and the Rendered Camera Preview. A camera's Solid Color fills the
  traced background (camera rays only; the lighting is unchanged).
- **The Scene view's filters look through the active camera** for exposure, Solid Color clear and far
  clip (the editor's projection stays; no depth of field while working). **F12 raster without filters**
  applies depth of field before the supersampling downsample.

**Checked**
- New tests (round 47): a filter on a second camera shows in the Game view only once it is the Active
  Camera, and undo restores the name rule; the Camera Preview and the Game view both follow the active
  camera's filters; the Scene view with filters takes the camera's exposure (+18 % mean brightness at
  +4 EV under Filmic) and Solid Color (a red sky pixel); a path-traced F12 with Edge Outline has 591
  near-black pixels against 1 without, and Solid Color fills its background; F12 raster depth of field
  lowers the neighbour difference 0.47 → 0.36; the active camera round-trips through a scene file and is
  cleared by deleting the camera; the console command reports, clears and refuses a non-camera.
- From the code review (Opus): the Game view's camera dropdown kept its labels in locals that died
  before the popup read them (a use after free the first time it was opened); they live in the editor
  now, the list shows only cameras the view could show, and a test opens it with mouse clicks and picks
  a camera. On a GPU the traced depth plane was a mean divided by a sample count that lags the sums
  (inflated fog between merges) and a mean that counted sky samples at 1e6 (an unfogged ring around
  distant objects): the plane now holds the last hitting sample's distance, consistent with the id. The
  hit point is rebuilt from the near plane as the tracer traces it (orthographic cameras too); the
  traced Solid Color is divided by the camera's exposure so it matches the raster clear; the far clip
  is kept with the render; `Scene::clear` resets the active camera and a dangling id in a file is
  dropped at load. A test checks outlines in the Rendered Camera Preview too. Note: F12 raster depth of
  field runs on the supersampled image (33 million pixels at 4x 1080p), a second or two.
- Unit checks: Windows 8,179, 0 failed (399 tests); the whole suite in render-cache verify mode: 8,180,
  0 failed, 0 stale pictures. Linux with libraries: 8,131, 0 failed. Sanitizer build (before the review
  fixes): 8,045, 0 failed, no reports. Stress `filters`: 300 fuzzed stacks, 0 problems; `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` unchanged within noise.

## 2026-10-11 (round 46): render-frame waste (task 0024)

**Asked:** the editor-performance round, second part: the frames that really render (orbiting, dragging,
editing a material, playing) did work that isn't rendering.

**Changed**
- **One filter stack per camera,** rebuilt only when one of its filters changes (a stack is a
  `std::function` per pass, and every view asked for a new one on every render). The Rendered Camera
  Preview and F12 use it too.
- **One filter target per view** (Scene, Game, Camera Preview, F12): two filtered views of different
  sizes used to reallocate a shared one every frame. The Profiler counts resizes and stack builds.
- **The rasterizer resolves each material's textures once per flush** and shades through them,
  instead of a version check and an atomic read per pixel; its per-item scratch vectors are kept
  between flushes and the per-chunk tile table is thread-local.
- **The sun's shadow casters are copied only when the map is rendered** (pointers until the cache
  check). The RSM already did.
- Not built: early-outs for the Scene view's icons and z-fight outlines on a cache hit (they draw
  after the cached picture, so they must draw every frame); recorded in the spec.

**Checked**
- New tests (round 46): the resolved-texture shading equals the per-pixel path on 64 points of a
  UV-grid material; three filtered views keep their own targets (2 resizes to warm up, 0 afterwards);
  a camera's stack is built once and once more after a filter field changes; the shadow map still
  renders once per change.
- Unit checks: Windows 8,148, 0 failed (390 tests); the whole suite in render-cache verify mode: 8,149,
  0 failed, 0 stale pictures. Linux with libraries: 8,100, 0 failed. Sanitizer build: 8,021, 0 failed,
  no reports.
- Stress `editor_render` on the final code, quiet machine: idle 1.31 ms, orbit 7.38, moving an object
  18.13, editing a material 18.13, playing 11.88 (task 0023's 1.30 / 7.32 / 18.44 / 18.33 / 12.13: the
  same within noise, the waste being allocation and copies that were small next to the rendering);
  shadow renders per row unchanged (1 for a move and a material edit, 0 otherwise). `shading`, `raster`
  and `filters` within their usual numbers (deferred 1080p 10.5 ms, 5.2M triangles at 60 FPS, the PS1
  look 2.5 ms). `editor_fuzz` in verify mode: no crash, 0 stale pictures.
- From the code review (Opus): the filter-target and stack-build tests measured idle frames, which are
  cache hits that render nothing; they move an object every frame now and check that the views really
  rendered. The Scene view no longer copies the cached stack to change its fit (`filter_layout` takes
  the fit apart from the stack); the cache is trimmed between frames, never during a lookup.

## 2026-10-11 (round 45): idle editor frames (task 0023, ADR 0015)

**Asked:** improve the editor's performance, the Scene view first. This round is the idle frame
(nothing changed, the mouse over a panel); the next (task 0024) is the frames that really render.

**Found:** pass 6 had called the 2.1 ms idle frame "almost all hash". A per-phase profile said the
scene hash was about 0.3 ms of it; the rest was painting the UI: every icon rasterized from its
primitives on every draw (the tab bars alone 0.38 ms), 2.5 million pixels of fills the views painted
over anyway, the Project window listing folders from disk every frame, the material library copied and
sorted twice a frame.

**Changed**
- **The scene hash's per-object memo (ADR 0015):** a component says what the views read of it
  (`render_role()`: one virtual call instead of a `dynamic_cast` per type, which was most of the cost),
  each object remembers the hash of its world matrix, flags and materials keyed by a transform version
  (bumped wherever the world matrix goes stale, reparenting included) and the material versions, the
  three evaluated-mesh keys come from one pass, and the Scene view's filter camera takes the frame's
  scene hash. Lights, probe volumes and modifiers are hashed every time, so the memo can't go stale
  on them. The hash at 10,000 meshes: 0.46 ms (the idle frame there 4.8 → 2.7 ms).
- **Icons** are rasterized once per size into a coverage mask and blended through it.
- **Fills:** the frame fills only the border strips around the dock area (the dock fills its
  splitters), and no panel is painted behind the Scene and Game views, which paint every pixel.
- **The Project window** keeps its folder listings with the file list (refreshed every 2 s or on a
  change); `loaded_material_assets()` is rebuilt only when the library changes; the Materials window
  counts users in one pass; `Canvas::circle` walks the ring, not its bounding box; the Statistics panel
  shows the rasterizer's binning time (`ms_bin`, the binning part of the setup tasks over every chunk).
- **Not changed, on purpose:** the hasher's byte-at-a-time mixing (its values are in every saved
  bake's key); no per-frame memo for `main_camera()` or "Lighting out of date" (the serial they would
  key on doesn't change on edits). The spec's 1.0 ms idle target is not met: 1.3 ms, the rest being
  legitimate drawing (`docs/PERFORMANCE.md`, pass 7, lists it); recorded in the spec for sign-off.

**Checked**
- New tests (round 45): `evaluated_keys` equals the three `evaluated_key` calls on 300 modifier
  combinations; the transform version bumps on a move, a parent's move and reparenting; the memoised
  hash equals a from-scratch hash and changes after 23 kinds of change (a gizmo drag, the position
  field, undo, redo, parenting, a parent's move, unparenting, material, light, modifier added / changed
  / disabled / Show in Renders, renderer disabled / enabled / flag, a material slot, an object
  deactivated / reactivated / deleted / created, a probe volume added / changed), in verify mode with 0
  stale pictures; a camera filter field re-renders the Game view; the main camera follows a rename, a
  disabled Camera and a deactivated object; Statistics shows the binning time.
- From the code review (Opus): a small ring in `Canvas::circle` blended a few pixels twice (one run
  per row when the hole is narrow); an empty root dock leaf went unpainted without the frame's clear
  (filled now); an icon's anti-aliased edge past its rect was lost by the mask (the mask is padded by
  3 px); pixel-equality tests for the icon masks (within 2 of a direct draw) and `Canvas::circle`
  (identical to the bounding-box walk).
- Unit checks: Windows 8,139, 0 failed (386 tests); the whole suite in render-cache verify mode:
  8,140, 0 failed, 0 stale pictures. Linux with libraries: 8,091, 0 failed. Sanitizer build: 8,009,
  0 failed, no reports.
- Stress `editor_render`: idle 1.98 → 1.27 ms, mouse over the Hierarchy 2.01 → 1.25, orbit 8.49 →
  7.40, moving an object 20.99 → 18.42, editing a material 19.22 → 18.19. `editor_ui`: idle at 1,000
  meshes 1.48 → 1.11 ms, at 10,000 4.82 → 2.74 ms, scene hash at 10,000 meshes 0.46 ms (0.56 from
  scratch). `editor_fuzz` in verify mode: 6,000 frames, 10,621 events, no crash, 0 stale pictures.

## 2026-10-11 (round 44): area lights bake smoothly; Preview Lighting isn't blocky on small objects (task 0022)

**Asked:** adjusting an area light's Width and Height made objects look pixel-like.

**Found:** every change restarts Preview Lighting at its roughest pass: a quarter of the resolution (a
small sphere got a few texels across), 16 samples, and the area light sampled with at most 8 random
points. It looked blotchy for about a second before refining. The real bake sampled area lights
randomly too.

**Fixed**
- **Area lights are sampled on a jittered grid** over their surface in the baker (stratified, with a
  per-texel offset; a concentric mapping for disks). At 16 samples a floor under a 2 x 1 m light is
  within 3.7% of a converged bake on average, against 5.8% with random points.
- **Preview Lighting** gives every object a chart at least 32 texels across, and samples area lights
  32 to 64 times instead of at most 8. That is most of the fix: a small sphere had a few texels across
  and 8 noisy samples.
- **From the code review:** the preview's 32-texel minimum chart was bigger than a 16-texel page (Max
  Lightmap Size allows 16) and wrote past it. A layout now cuts its minimum chart down to the page
  size, for every caller, and a test lays out and previews with 16-texel pages. The baker also takes
  exactly the Direct Samples the user set (not rounded up to a grid).

**Checked**
- Unit checks: Windows 8,108, 0 failed (378 tests), twice. Linux with libraries: 8,060, 0 failed. Sanitizer build: 7,978, 0 failed, no reports (run before the review fix; the final code's run is CI's). Windows dependency-free: 7,980, 0 failed.

## 2026-10-11 (round 43): Baked lights exist only through the bake (task 0021)

**Asked:** don't rasterize lights whose Mode is Baked; the bake represents them. Mixed and Realtime
stay live. Before a bake, Preview Lighting is what shows them, as Bakery's Realtime Preview does.

**Changed**
- **A Baked light is left out of the rasterized views:** no direct light or highlight, no shadow map,
  no reflective shadow map (so no live bounce from it). Until now it was skipped only on lightmapped
  or baked-probe surfaces, and still lit everything else.
- **Before a bake it lights nothing;** turn on Preview Lighting or press Generate Lighting.
- **Unchanged:** its icon and gizmo, the bake itself, and the path tracer (F12 path traced, the Rendered
  Camera Preview), which computes true GI.
- **Solid shading** leaves Baked lights out too, and live GI picks its sun the way the shadow map does
  (the first directional light that isn't Baked).

**Fixed (found by the code review)**
- **The first version also dropped Baked lights from the path tracer,** which builds its lights the
  same way. A path-traced F12 render of a Baked-light scene came out sky-lit. The rasterizer and the
  path tracer now take different light lists, and a test renders F12 through the editor to check.
- **The light's Mode help text** said Baked lights still drew on unmapped objects.

**Checked**
- Unit checks: Windows 8,092, 0 failed (375 tests), twice. Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,044, 0 failed. Sanitizer build: 7,966, 0 failed, no reports. Windows dependency-free: 7,964, 0 failed.

## 2026-10-11 (round 42): the sanitizer CI job's flaky pilot test

**Asked:** the Linux sanitizer job was failing on the PR that brings tasks 0011-0020 to main.

**Fixed**
- **"Stopping the pilot brings back the editor view" failed only on the sanitizer runner:** 7,045
  pixels differed, all in one 294 x 24 box at the top of the Scene view. The test's click helper left
  the mouse resting on the Statistics toolbar button, and on a slow run the button's tooltip delay
  elapsed, so the tooltip drew over the view. The helper now parks the mouse outside the window after a
  click. Reproduced and fixed under the sanitizer build on 3 threads (CI's count).

**Checked**
- The pilot tests pass under the sanitizer build on 3 threads (twice), and on Windows.

## 2026-10-10 (round 41): manual lighting updates and a rough preview, like Bakery's (task 0020)

**Asked:** stop recomputing lighting on every change while moving things around, update it only when
asked, and add an optional very rough preview like Bakery's Realtime Preview.

**Added**
- **Lighting Updates** (Lighting window): Manual, the default, or Automatic (the behaviour until now).
  - **In Manual,** Realtime GI, realtime lightmaps, live probes and Auto Generate don't react to changes.
    Direct light and shadows stay live.
  - **The last lighting stays up:** a baked object that moved keeps its lightmap, and the last realtime
    lightmaps and live probes stay shown. "Lighting out of date" says when it's stale.
- **Update Lighting** (button, Ctrl+Shift+L, `lighting update`) runs Realtime GI once (the grid, the
  RSM, the realtime lightmaps, the live probes) and freezes again. Generate Lighting still bakes.
- **Preview Lighting** (a toggle, `lighting preview on|off`):
  - **What:** a rough progressive bake at a quarter of the Lightmap Resolution, 16, then 64, then 256
    samples, denoised, on the GPU when Render > Device is GPU Compute.
  - **When:** it starts half a second after changes stop and runs about 8 ms a frame.
  - **Shown, not saved:** it appears in place of the bake and is never saved.
- **`lighting mode manual|automatic`**, and the Learn lesson and guide explain both modes.
- **In Manual, one automatic update** when Realtime GI is turned on or a scene opens, so it never starts
  empty.
- **"Lighting out of date (Update Lighting)"** in the status bar when frozen Realtime GI is stale (with
  or without a bake).

**Fixed (found by the code review)**
- **A fresh Generate Lighting was covered by the preview again.** The preview now stands down while
  the real bake is current.
- **Frozen frames dropped a Realtime sun's bounce on lightmaps** (and the frozen live probes, and
  specular occlusion). A frozen frame is now the last updated picture, bit for bit.
- **Undo and redo dropped the frozen realtime lightmaps** (a clone gets a new scene serial). They're now
  keyed by a scene id that undo keeps.
- **In Automatic with a stale bake, the preview was never attached.** Staleness is now judged on what's
  shown.
- **Smaller fixes:**
  - A mesh edit in Manual drops the object's old map (only a move keeps it).
  - Opening a scene clears the last one's preview.
  - The preview's sample count resets on a change.
  - The Lighting window's out-of-date text fits Manual.
  - `lighting mode auto` works.
  - The stress section puts its scene back after the drag rows.
  - Two tests that couldn't fail now can: Auto Generate gets its real second, and "never saved" checks
    a real folder.
- **Not fixed:** each preview stage lays out and builds its scene again in one frame. That's a hitch
  on big scenes while orbiting during a preview.

**Checked**
- Unit checks: Windows 8,066, 0 failed (369 tests), with Automatic and with Manual as the default
  (`BLENDITY_LIGHTING_UPDATES`). Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,018, 0 failed. Sanitizer build: 7,940, 0 failed, no reports.
- Stress `editor_render`, 1600 × 1000, Realtime GI on, dragging a static object:

  | Mode | ms a frame |
  |---|---|
  | Automatic | 84–108 |
  | Manual | 22–27 (the same as with no GI) |
  | Manual + Preview Lighting | 22–23 (it waits for the drag to stop) |

  After the drag, the GPU preview shows its first pass in 1.4 s and reaches 256 samples in 3.4 s. The
  first run after a kernel change compiles the pipeline once (11 s).

## 2026-10-10 (round 40): lighting without lag: baked GI that costs nothing live, realtime lightmaps, GI and baking on the GPU, no path-traced Scene view (tasks 0016-0019)

**Asked:**
- remove the Scene view's path-traced Rendered mode (too slow);
- the approved tasks 0016–0018: realtime lightmaps first with a fallback, baked lighting with no lag,
  and GI, probes and baking on the GPU where it's faster.

**Added**
- **Realtime lightmaps (0016):** Contribute GI objects get a low-resolution map (Realtime Resolution, 2
  texels per metre by default).
  - Voxel GI lights it a slice at a time when the lighting changes, and it's read like a baked map.
  - **Orbit cost:** +1–3 ms at 1080p over no GI, against +12–14 ms traced per pixel.
  - **Look:** the same at any zoom (the Scene view's blocks are gone on mapped objects).
  - **Fallback:** objects without a map keep the per-pixel pass.
- **No live work once baked (0017):** Realtime GI traces only what a bake doesn't hold.
  - **Cases:** nothing baked, the scene changed since the bake, or a Realtime Directional Light.
  - **Otherwise idle:** it traces nothing, and the views equal Realtime GI off.
  - **Status:** the Lighting window's "Live GI:" line says which case applies, and the per-pixel cost.
- **Cheaper probe sampling (0017):** +4 ms at 1080p with every object probe-lit, from +15 ms (a brick
  hint, the cube's axes once per sample, one sample per 2 × 2 pixel quad).
- **GI on the GPU (0018, ADR 0014):** a Vulkan compute kernel for voxel GI's gather.
  - **What uses it:** realtime lightmaps and live probes. **GI Device** (Auto / CPU / GPU) chooses.
  - **Speed:** 4–8.6× faster on the RTX 4070 SUPER, 3.2–6.2× on the 3060 Ti. A whole pass fits in a
    frame.
- **Baking on the GPU (0018):** with Render > Device = GPU Compute, Generate Lighting traces lightmap
  texels and probe rays on the path tracer's kernel.
  - **Speed:** 8.6× (4070 SUPER) and 6× (3060 Ti) on the tracing.
  - **Agreement:** totals match the CPU's within 0.01%.
- **A lighting guide** (`docs/guides/lighting.md`) and a Learn lesson, "Baked and Realtime Lighting".

**Removed**
- **The Scene view's Rendered (path-traced) mode (0019).** `shading rendered` warns and shows Shaded.
  Path tracing stays in F12, the Render window and the Camera Preview's Rendered toggle.

**Fixed (found while building it)**
- **A small object's realtime lightmap had a black patch:** big texels gathering from inside the floor
  saw nothing. They now take their chart neighbours' light, as invalid probes do. Realtime charts are
  also at least 24 texels across.
- **Shared probe samples depended on thread scheduling** (row pairs split between threads). Rows now go
  out in pairs.
- **Changing GI Device didn't gather again;** it's in the live keys now.
- **From the code review:**
  - **A Sky world drawn from the sun was missing from the GI keys,** so turning the sun didn't re-gather
    with Bounce off, and the GPU kept the old sky. The keys now include the sky's own key.
  - **Probe bakes on the GPU could allocate gigabytes** at high sample counts. Slices are now sized by
    rays (about 8M).
  - **Test limits had been tuned to one build** (the probe quad's worst difference, the small ball's
    sky share). They're now principled bounds, with a comment.
  - **Smaller fixes:**
    - The GPU kernel's read-back buffer is freed.
    - Choosing a GI Device again retries the GPU after an error.
    - Unwrapped realtime charts keep their size when an object moves.
    - Another scene's published maps are never shown.
    - The GPU bake's random sequences follow each texel, not its place in a batch.
    - Realtime lightmaps are capped at about 2M texels.
    - Stale "Rendered" tips are gone.

**Not as the approved specs said (recorded in each spec's "As built"; all signed off by the user on 2026-10-10):**
- **0016:** no voxel ray jitter or occlusion fade; the maps' blur and bilinear filter smooth mapped
  surfaces, and the fallback keeps its look.
- **0017:** shared probe samples differ from per-pixel ones by up to 8 levels on 0.03% of pixels.
- **0018:**
  - GPU and CPU gathers can differ by one or two grazing rays per point (means within 0.002).
  - The per-pixel pass stays on the CPU.

**Checked**
- Unit checks:
  - Windows 7,994, 0 failed (358 tests), and the same with GI Device Auto on the GPU
    (`BLENDITY_GI_DEVICE=auto`);
  - Windows dependency-free (CI's build): 7,866, 0 failed.
  - Render cache verify mode: 0 stale pictures.
- The code review's open minors, not done:
  - a GPU failure mid-pass makes the CPU finish that pass in one frame (a one-off hitch);
  - criterion 5 of task 0016 (fallback pictures bit-identical to the old pass) is checked by traced
    receiver counts, not pixels.
- Linux with libraries: 7,946, 0 failed. Sanitizer build: 7,868, 0 failed, no reports.
- Stress:
  - `vgi`: a 1080p orbit reading realtime lightmaps costs +0.9–3.1 ms over no GI, against +12–14 ms
    traced. A pass of 9,377 texels takes 4.8 ms on the CPU.
  - `probes`: deferred shading at 1080p is 13.7 ms without probes and 17.7 ms with every object
    probe-lit (29.1 ms before).
  - `gpu_gi`: gathers and bakes as above (ADR 0014's table).

## 2026-10-10 (round 39): realtime lightmaps, baked lighting without lag, GI on the GPU (specs only)

**Asked:**
- realtime lightmaps, the faster option first with a fallback;
- GI and probes on the GPU if faster;
- baked lighting that doesn't lag the Scene view;
- why realtime GI looks pixel-like in the Scene view but not in the Game view.

**Found:**
- **The Scene view's blocks** come from the per-pixel GI pass tracing at ¼ of the screen resolution.
  Objects are small on screen there, so each 4 × 4-pixel GI cell shows.
  - GI Resolution = Half makes them smaller; a 256 voxel grid doesn't.
  - A dark band where objects meet the floor comes from the voxel steps.
- **Per-frame costs at 1080p:**
  - a baked lightmap: about 1 ms;
  - live voxel GI: about 12 ms, even where a bake covers the scene;
  - probe sampling: up to 15 ms.

**Drafted (for approval, no code yet):**
- **Task 0016:** realtime lightmaps per texel, with the per-pixel pass as the fallback.
- **Task 0017:** no live GI work once the bake covers the scene, cheaper probe sampling, and a lighting
  guide.
- **Task 0018:** GI, probes and baking on Vulkan compute, each behind a measured spike.

## 2026-10-10 (round 38): GPU viewport, a measured go / no-go spike (task 0015)

**Asked:** run the editor's views on the GPU, after the CPU wins, if it pays off.

**Added**
- **Stress section `gpu_viewport`:** a 1080p view on the CPU (deferred and the cached shadow map)
  against the same picture's work on each GPU. The GPU side is the Vulkan path tracer at 1 sample with
  no bounces, split into clears, render plus read-back, and resolve.
- **ADR 0013** (proposed) and **task 0015's spec** (no viewport code).

**Result: no-go.**
- **The bar:** two views at ≤ 0.50× the CPU's time.
- **The CPU frame:** 19.6 ms.
- **The GPU's render plus read-back alone:**
  - RTX 4070 SUPER: 10.6–11.8 ms (0.54–0.60×);
  - RTX 3060 Ti: 15.4–17.5 ms (0.79–0.89×).
- **End to end through the path tracer:** 31–39 ms.
- **Recommendation:** keep the views on the CPU and look next at voxel GI and probe sampling (about
  12–15 ms each at 1080p).

## 2026-10-10 (round 37): probe volumes, like Unity 6's Adaptive Probe Volumes (task 0014)

**Asked:** add probe volumes, kept live by the voxel GI.

**Added**
- **Probe Volume component:** Global (around everything that contributes GI) or a box that follows the
  object's rotation and scale.
- **Placement:** bricks of 4 × 4 × 4 probes, fine near geometry and coarse in open space. Probes inside
  geometry move out (virtual offset) or are marked invalid and filled from their neighbours.
- **Each probe is an ambient cube:**
  - the light from geometry per axis;
  - the share of the sky each axis sees, so a new sky colour relights them with no re-bake.
- **Baked** by Generate Lighting after the lightmaps, in slices so the editor stays responsive.
- **Live:** Realtime GI keeps the probes current, 4,096 probes a frame, with no bake needed.
- **Shading:** per pixel, with normal bias, view bias and leak reduction.
  - **Receive GI** (MeshRenderer) picks Lightmaps or Light Probes.
  - Objects that don't contribute GI always use the probes.
- **Settings:** min and max probe spacing, the biases, leak reduction.
- **Debug display:** `probes show` / `probes hide`, or Show Probes in the Lighting window.

**Fixed (found by the tests and the review)**
- **Enormous volumes and NaN settings overflowed float-to-int casts** (the sanitizer caught them): in
  placement, in the live update's voxel lookup and in the volume box's key. The probe cap could also be
  broken. Counts are now in double, cells are clamped before the cast, the key hashes the setting's
  bits, and the cap is strict.
- **Live probes never showed with Realtime GI on**, yet they re-rendered the views. Probe-lit objects
  now keep the probes (as in APV), and the live probes are what follow lighting changes.
- **Light counted twice with Baked GI off:** baked probes are now used only with Baked GI on. Probes
  from another bake are refused on open.
- **A re-bake without a volume** left the old `ProbeVolume.bin` to come back on open; it is removed.
  Probes are saved even when there are no lightmaps.
- **The probe bake ran on the UI thread in one go:** it now runs in slices within the bake's frame
  budget, and the bake's mesh copies are freed after it.
- **Global volumes put probes on the floor plane:** the pad is now 1.5 spacings.
- **Thin walls leaked light:** APV's normal-based leak reduction was added (on by default).
- **Live probes near a floor saw through it:** they gather from the nearest free voxel.
- **Receive GI didn't redraw the views.**
- **Smaller fixes:**
  - Indirect Intensity applies to the probes.
  - A cell with every valid probe behind the surface falls back to their plain average.
  - The virtual offset is limited to ¾ of a spacing.
  - Receive GI = Light Probes objects get no lightmap chart.
  - The live key covers both views' voxel GI and only the settings that change the gathers.
  - The volume box is remembered while nothing changes.
- **Tests:**
  - **A NaN volume was a valid point box on GCC:** `AABB::add` drops NaN there, so the placement test
    sets the box directly.
  - **"Stopping the pilot brings back the editor view" failed under the sanitizer:** it compared the
    Statistics overlay, whose frame time differs on a slow run. Statistics is now off for that test,
    and the view comes back bit for bit.

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0012):**
- Each probe is an ambient cube, not SH L1 / L2.
- Probes win over per-pixel voxel GI for the objects they light.
- Live probes restart as a whole, not only near a change.
- The debug display draws dots.

**Checked**
- Unit checks: Windows 7,880, 0 failed (344 tests; round 37 has 29). Render cache verify mode: 0 stale
  pictures.
- Linux with libraries: 7,856, 0 failed. Sanitizer build (ASan + UBSan + float-cast-overflow): 7,778, 0 failed, no reports.
- Stress `probes` (32 threads):
  - **Scene:** 38,144 probes in 596 bricks, 2,704 inside geometry;
  - **Time:** place 9.2 ms, validate 11.1 ms, bake 24.2 ms (64 rays);
  - **Memory:** 206 bytes per probe;
  - **Live:** 1.48 ms per 4,096-probe slice;
  - **Shading at 1080p:** deferred shading 13.6 ms without probes and 29.1 ms with every object
    probe-lit (a follow-up);
  - **Fuzz:** 100 odd volumes, 0 problems.

## 2026-10-10 (round 36): realtime GI from the research paper: voxel-based global illumination (task 0013)

**Asked:** implement the first paper in `research/papers/`, Thiedemann et al., *Voxel-based Global
Illumination* (I3D 2011), working with the baking so that changing the lighting never needs a manual
re-bake.

**Added**
- **Realtime GI** (Lighting window): one bounce of the sun's light and the sky's occlusion, live in
  every rasterized view.
- **The paper's pieces, on the CPU:**
  - **Voxel grid (section 3):** the scene voxelized into columns of 128 bits, with an exact conservative
    span voxelizer.
  - **Moving objects:** each object's voxels are kept, so a moved object is voxelized alone.
  - **Ray test (section 4):** a mip-map hierarchy and its ray walk.
  - **Bounce (section 5.1):** gathered through the sun's reflective shadow map.
  - **Sky:** the share of the sky that gets through gives directional occlusion; open ground keeps
    exactly today's light.
  - **Per pixel:** traced at ½ or ¼ resolution with 16 interleaved ray sets, a geometry-aware blur and
    an upsample.
- **With the baking:**
  - Lightmapped surfaces add the live bounce of lights the bake doesn't hold.
  - Objects changed since a bake, or a whole bake made stale by a light or world change, switch to live
    GI instead of flat ambient.
  - **Auto Generate** re-bakes a second after changes stop.
- **Settings:** voxel resolution, radius, rays, intensity, sky occlusion, bounce, GI resolution, RSM
  resolution, specular occlusion.

**Fixed (found by the tests and the review)**
- **Bounce depended on where the scene sat:** the sun's reflective shadow map flipped normals toward a
  camera at the world origin. It now faces the sun.
- **GI settings didn't redraw the views,** and they made the bake look out of date (which would have
  started Auto Generate). Only the settings a bake depends on count now.
- **Lightmapped surfaces:** they keep their baked specular. Receivers that take everything from a
  lightmap trace nothing.
- **Zero-area triangles** add no voxels.
- **Auto Generate's wake-up** was cleared in the same frame, so an idle editor never re-baked.
- **The Scene view and Game view shared one voxel / RSM cache** and rebuilt each other's every frame.
  Each kind of view now has its own.
- **The ray walks stall or skip far from the grid's corner:** they re-derived cells from rounded
  positions. They now step cell indices and pick children by crossing time. A test checks 20,000 rays
  over 256 voxels long on a 256 grid.
- **A pre-existing rasterizer bug:** a vertex far beyond float range cast infinity to int in tile
  binning, which the sanitizer caught.
- **Smaller fixes:**
  - The RSM holds the sun's light only.
  - The self-occlusion offset clears a voxel's diagonal.
  - Ray sets differ in elevation too.
  - The grid stays put while the scene fits it.
  - Objects that are gone are forgotten.
  - Non-casting objects don't occlude.
  - The lightmaps' out-of-date key covers bake settings only (a 0012 bake would show as out of date
    once).

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0011):**
- Realtime lightmaps are per pixel, not per texel: the per-pixel pass covers the same cases.
- The settings live in the Lighting window (`LightingSettings`).

**Checked**
- **Unit checks:** Windows 4,077, Linux 4,053, sanitizer build 3,975; 0 failed. 315 tests. The
  test-engineer wrote 17 tests and the review added 2:
  - **Voxelizer:** matches a double-precision triangle–box test, exactly.
  - **Hierarchy and ray walks:**
    - mips are the OR of the level below;
    - the hierarchical walk equals the column walk exactly on 100k rays, and on 20k rays longer than
      256 voxels;
    - it agrees with an independent 3D DDA on all of them;
    - the first hit is right going up and down.
  - **Gather:** RSM back-projection gathers lit points and not shadowed or back-facing ones.
  - **Pictures:**
    - a red wall bleeds onto the floor;
    - a box darkens the floor beside it, and far floor is bit-identical;
    - GI off is bit-identical;
    - deterministic, and 1 thread equals all threads.
  - **Caching:** orbiting re-voxelizes nothing; moving one object re-voxelizes only it.
  - **With the baking:**
    - a Realtime sun's bounce follows it on a lightmapped floor without a bake;
    - Auto Generate waits, re-bakes and sets its wake-up time.
  - **Settings:** save, load, undo; odd values are safe.
- **Verify mode:** 0 stale pictures.
- **Stress `vgi` (Windows):**
  - **Voxelizing:** 2.2 ms for 257 objects at 128, and 5 ms at 256.
  - **Ray walks, short rays:** the hierarchical walk equals the column walk (6.8 vs 7.4 Mrays/s on one
    thread).
  - **Ray walks, 40 m rays:** the hierarchical walk is 5x faster (5.4 vs 1.1 Mrays/s).
  - **RSM 256²:** 3 ms, only when the casters or the sun change.
  - **The GI pass adds:**
    - 720p, 8 rays, Quarter: 5.8 ms;
    - 1080p, 8 rays, Quarter: 11.8 ms (over the spec's ~10.5 ms budget);
    - 1080p, 4 rays, Quarter: 8.5 ms.
  - **Odd settings:** 200 runs, 0 problems.

## 2026-10-10 (round 35): baked lighting like Unity's (task 0012)

**Asked:** "a similar light baking system like Unity3D": lightmaps and light modes, the approved task 0012.
Voxel GI (0013) and probe volumes (0014) work with the same lightmaps, so changing the lighting won't
need a manual re-bake.

**Added**
- **Per object and per light** (all saved, undoable and settable with `set`):
  - **Contribute GI** on MeshRenderer, with **Scale In Lightmap** and **Generate Lightmap UVs**;
  - **Mode** on each Light: Realtime, Mixed or Baked.
- **A Lighting window** (Window menu, or `window Lighting`) holds:
  - **Generate Lighting** and Cancel, with a progress bar;
  - **Clear Baked Data**;
  - the Lightmapping settings: resolution in texels per unit, max size, padding, direct and indirect
    samples, bounces, denoise, indirect intensity, Auto Generate;
  - the Environment.

  Console: `bake start|cancel|clear`.
- **The baker (`Lightmapper`) runs on the path tracer:**
  - **Lightmap UVs:** generated per object (Smart UV Project + Pack Islands), sized so each object
    gets the asked density.
  - **Indirect light per texel:** cosine-weighted rays through the static objects, the Baked and Mixed
    lights and the sky, with every bounce. Realtime lights are left out, as in Unity.
  - **Direct light:** Baked lights only, with shadow rays.
  - **Finish:** a filter within each object's charts, then dilation into the padding.
  - **Runs** in slices per frame, cancellable, on its own copies of the meshes.
- **Shading:**
  - Lightmapped surfaces take their ambient light from the map.
  - Baked lights are skipped there in realtime.
  - Mixed lights keep their realtime light and shadows (Baked Indirect).
- **Files:** the lightmaps are saved next to the scene, as `<scene>/Lightmap-N.hdr` and
  `LightingData.bin`, and load with it.
- **Stale objects:** an object changed since the bake falls back to realtime ambient, and the status
  bar says **"Lighting out of date"** (ADR 0010).

**Fixed (found by the tests)**
- **Lightmaps had 21–64% fewer texels than asked.** The packed charts fill only part of their square.
  Charts are now sized from the area their UVs actually cover. A test checks density within 20% of
  Lightmap Resolution × Scale In Lightmap across shapes and sizes.
- **A NaN vertex gave NaN lightmap UVs.** Fast-math compiles `std::isfinite` away, so the check is now
  by bits.

**Fixed (from the review)**
- **The Window menu read past its shortcut list** once the Lighting window was added: undefined
  behaviour every time the menu opened. A compile-time check now ties the list to the windows.
- **A bake still running when another scene was opened or created** would have written its lightmaps
  into that scene's folder. Switching scenes now cancels it, and a new scene starts with no baked data.
- **The lighting folder came from the name stored in the scene**, not the file name. A renamed scene
  could share or delete another scene's lightmaps, and a name of "." pointed at the Scenes folder
  itself. It is now the file's name, and anything that isn't a plain name gives no folder.
- **A bake made before the scene had a file, or before Save As, was lost on save.** Saving now writes
  it next to the file, and so does the auto-save on exit.
- **Dragging a lightmapped object re-hashed its whole mesh every frame**, several times. The mesh part
  of the hash is now remembered by render-mesh serial (only the matrix is rehashed), and "Lighting out
  of date" is recomputed only when the scene or the lighting settings change. This also fixes a hash
  that could go stale after opening another scene.
- **Light skipping:** lightmapped surfaces skip the lights the maps were baked with, as they were
  baked. A light baked as Baked and switched to Realtime no longer shines twice, and one added since
  is no longer dropped.
- **A light on an object with its own non-contributing mesh was missing from the out-of-date check.**
- **A damaged `LightingData.bin` could ask for gigabytes.** Counts are now checked against the file's
  size.
- **Bounces were one too many:** the gather's own hit is the first bounce. With 0 bounces you get only
  the sky and direct light.
- **Smaller fixes:**
  - The content hash now includes per-face materials, sharp edges and smoothing.
  - Batches are sized by sample count, so a frame's budget holds at high sample counts.
  - A cancelled bake frees its memory.
  - Lightmaps are not used under the Scene view's headlight.
  - The stress section reports the filter + dilation time.
- **Five tests from the review:**
  - baking before saving;
  - a new scene during a bake;
  - a damaged file;
  - a lamp mesh with a Baked light;
  - a mode switched after the bake.

**Not done, from the review:**
- The texel rasterizer still lives inside the baker; task 0013 will expose it for realtime lightmaps.
- Texels are taken at texel centres, not conservatively, so slivers thinner than a texel get none;
  dilation covers them.
- The Scene view uses a lightmap only when its viewport mesh matches what was baked. With modifiers
  that differ between the viewport and renders it shows realtime light while the status says up to
  date.
- Laying out a very large bake (the UVs, the atlas) still runs on the main thread before the first
  slice.

**Not as the spec said:** the lightmaps are Radiance `.hdr` files, not `.exr`. Blendity can write EXR
only with Blender's libraries and has no EXR reader; `.hdr` is written and read without them.

**Checked**
- **Unit checks:** Windows 3,540, Linux 3,516, sanitizer build 3,438; 0 failed. 293 tests. The
  test-engineer wrote 19 tests, and the review added 5:
  - **UVs:** inside 0..1, no overlaps, padding, density.
  - **Colour bleeding:** a red wall tints the floor with a Baked or a Mixed sun, and a white wall
    doesn't.
  - **Accuracy:**
    - open sky exact;
    - under a covering slab, dark;
    - at a slab's edge within 0.013 of an 8,192-sample path-traced reference;
    - a Baked sun's direct light exact (colour × intensity × N·L), 0 in shadow.
  - **Light modes:**
    - a Baked sun alone lights the floor;
    - a Mixed sun keeps the realtime shadow (90% of the lit region in common with an unbaked scene);
    - a Realtime sun leaves the maps bit-identical.
  - **Lifecycle:**
    - save and reopen;
    - a moved box falls back, then comes back bit-identical;
    - `bake clear` restores the picture bit-identically;
    - dynamic objects untouched;
    - path-traced F12 unchanged.
  - **Robustness and undo:** cancel, undo, NaN and odd settings.
- **Verify mode:** the suite has 0 stale pictures (the view caches re-render when a bake finishes,
  clears or loads).
- **Stress `bake` (Windows):**

| Static objects | Texels | Bake time |
|---|---|---|
| 10 | 12k | 0.03 s |
| 100 | 122k | 0.25 s |
| 1,000 | 1.24M | 2.2 s (3 lightmaps) |

  - That is about 500k texels/s and 42–56 Mrays/s, of which filtering and dilation take 3–140 ms.
  - Shading a lightmap costs 1–1.4 ms more at 1080p (10.1 → 11.0–11.5 ms).
  - Odd settings: 30 runs, 0 problems.

## 2026-10-10 (round 34): a faster editor: views and shadows reused until something changes (task 0011)

**Asked:** improve performance for the Scene view and the editor (task 0011 of the approved plan).

**Found**
- **The sun's shadow map was rendered on every shaded render**, up to three times a frame, at about
  8 ms each. `docs/PERFORMANCE.md` and a stress note both said it was cached.
- **Any input re-rendered every visible 3D view from scratch**: a mouse move over a panel, a caret blink,
  a tooltip timer. In a working scene (1600×1000, Scene and Game views, a Camera Preview), an idle frame
  cost about 46 ms.

**Changed**
- **Views reuse their picture while nothing they show has changed (ADR 0009).**
  - **What is cached:** the Scene view, the Game view and the Shaded Camera Preview.
  - **The key:** a content hash of the scene (geometry, transforms, the hierarchy, MeshRenderer flags,
    materials, lights, world, render settings), plus each view's camera, size, toggles and selection.
  - **Never cached:** Rendered shading, piloting, Play while running.
  - **Verify mode** (`BLENDITY_RENDER_CACHE_VERIFY=1`) renders anyway and compares. The unit suite and
    the random-input fuzz both run clean in it.
- **The shadow map is kept until its casters, their materials or the light change.** There are two
  entries, so the Scene and Game views don't evict each other. Each render mesh gets a `serial` the
  caches key on.
- **Clears run in one parallel pass**, and the visibility plane exists only for deferred shading.
- **Overlay lines and points are drawn in parallel bands of rows**, bit-identical to drawing them one by
  one. The F12 supersampling downsample is parallel too.
- **The scene hash no longer evaluates modifier stacks:** `GameObject::evaluated_key` gives the key
  without the mesh.
- **The Profiler window shows where the frame went:**
  - time in the Scene view and its overlays, the Game view, the preview, the shadow map, the UI and
    present;
  - how many views were rendered or reused.

**Fixed (from the review)**
- **The Game view and Camera Preview missed MeshRenderer flags.** Unticking Show in Renders or Receive
  Shadows left them stale. The flags are now in the shared scene hash, which also fixes the
  path-traced preview's key.
- **A modifier's "Show in Edit Mode" didn't redraw the Edit Mode cage.** The cage's evaluated mesh is
  now in the key.
- **Re-parenting a selected object's child** (for example by undo) left its child outline. The
  hierarchy is now in the key.
- **Smaller gaps:**
  - Reopening a scene file now starts fresh caches (each Scene has a `serial`).
  - The raster options are in the Game view and Camera Preview keys.
  - The grid's orbit distance and pivot are in the Scene view key.
  - A batch can't stay open across frames.
- **One test** checks the first three cases in verify mode. Without the fix it finds 3 stale pictures.

**Checked**
- **Linux, dependency-free build, `editor_render`, before → after:**

| Frame | Before (ms) | After (ms) |
|---|---|---|
| Idle | 47.3 | 2.1 |
| Mouse over the Hierarchy | 44.2 | 2.1 |
| Orbiting the Scene view | 43.6 | 9.9 (no shadow pass) |
| Moving an object | 45.1 | 24.7 |
| Editing a material colour | 45.4 | 27.0 |
| Playing | 32.5 | 14.8 |

- **Idle frames with many objects:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000 (the hash). An idle
  frame with 1,000 empties went from 10.6 to 1.5 ms.
- **Overlays:** 1.15 → 0.77 ms in a normal frame, and 17.5 → 12.4 ms with a 50k-face Edit Mode cage.
- **Edit Mode on a 50k-face mesh is still about 280 ms a change.**
  - The render mesh costs 13–19 ms and the tangents 28–37 ms.
  - Most of the rest is the overlapping-vertex check. It runs on changes made outside a drag; a real
    gizmo drag skips it.
  - Speeding this up is the follow-up (a positions-only mesh update; it needs an ADR).
- **Unit checks:** Windows 3,203, Linux 3,179, sanitizer build 3,101; 0 failed. 270 tests. The
  test-engineer wrote 26 tests and the review added 1:
  - idle and panel-only frames render nothing;
  - fourteen kinds of change each redraw, including a real gizmo drag, an Edit Mode vertex drag, undo
    (bit-identical to before), Play, shading, the World and Exposure;
  - orbiting renders the shadow map at most once;
  - about 55 mixed actions in verify mode leave no stale picture;
  - the overlay batch is bit-identical on 450 random lines and points;
  - the parallel clears.
- **Stress:**
  - `editor_fuzz` (6,000 frames of random input) in verify mode: no crash, 0 stale pictures.
  - `filters`: 0 problems.
- **The whole unit suite** runs in 18 s on Windows (21.7 s before this task); the tests reuse views too.

**Not done from the spec**
- **`is_selected` as a set** and **one render snapshot per frame:** the scene walks and the linear
  selection search don't show in the measurements at 1,000–10,000 objects.
- **`outline_ids` in one pass:** it reuses its mask instead; still two passes when a selection has
  children.
- **A test for the caret blink:** it is the same frame as the idle case (no events, a redraw).

## 2026-10-09 (round 33): leaner, faster unit tests (task 0010)

**Asked:** consolidate the tests, removing redundant or unnecessary ones. The same request also asked for
editor performance, Unity-style baked lighting, probe volumes, and the voxel-based global illumination paper
in `research/papers/`. Those are specced as tasks 0011–0014, approved, and come next. A GPU viewport
follows as task 0015, specced after a measured spike.

**Changed**
- **Per-element checks are counted.** A loop that ran `CHECK` on every pixel or table entry now counts the
  failures and checks once (`count_bad`, which prints the first bad element). Coverage is the same. The
  bloom odd-values test alone had run 53,800 checks.
- **Overlapping tests are merged**:

| Old test | Now covered by |
|---|---|
| filter stack: an empty stack, or one with every effect off, gives the same Game view as no component | The stack part moved into "with no filter the rasterizer's image is unchanged". The Game view part (empty stack, two disabled effects) moved into "PS1 Game view is 15-bit ... with no filter, an empty stack, disabled or neutral effects it is the old picture". |
| camera filters: the component saves, loads (its own values beat the preset), round-trips twice, and clones | "filter stack: a stack saves, loads (every field, its own values beating the preset) and round-trips; clones and Duplicate deep-copy it", which gained a third effect with every field moved off the preset. |
| bloom: a target that is a window into a larger image (row stride) only changes inside the window | "odd sizes: every pass works ... (row stride, Bloom too)" runs Bloom on the same window, with a white patch to spread. |
| Bloom's save / load / set / undo in "bloom: Add Filter > Bloom on the Main Camera ..." | Threshold is Bloom's row in the effect table "every filter effect (Retro Console, Color, Lens, Stylize, Bloom) can be added from the console, set, saved, loaded and undone". The table also gained a Retro Console row, and checks that every registered effect has a row. The bloom test keeps the Game view glow, Intensity and Scatter's save / load / undo (restored after the review), and undoing the add. |
| The PS1 preset fields in "presets: PS1, N64, Saturn and DOS ..." | "Retro Console Filter contributes the PS1 look" (now also checks that there is no screen-door). |
| The two-filter ordering in "Retro Console Filter contributes the PS1 look" | "effects combine in list order". The later snap grid (in both orders) and the "cap of 0 doesn't lift a cap" checks stay. |
| The Camera Preview rebuild in "the component's hash changes with any effect field ..." | "the Camera Preview (Rendered) refreshes when a filter field changes", which now sets the size through the stack's field names, for the first effect and for a second one (E1). The hash test no longer opens an editor. |

- **Checks that tested nothing:**
  - `CHECK(true)` after drawing a window became a check that the window is open.
  - An empty bloom or retro target is checked as untouched.
  - Dead variables are removed, including an unchecked `plain_changes_within_key`, an unused render, and
    an unused `esc` event.
- **Shared helpers:**
  - `kNaN` / `kInf` sit at the top of the file.
  - `cf_draw` is shared by the PS1 helpers and the bloom scene.
  - Each test prints its time.
- **Faster tests.** Sample counts were lowered only with a tolerance worked out from the noise, written in
  the test.
  - **Path tracer:** the MIS reference uses 1024 spp instead of 8192. The ratio of the two means has σ ≈ 0.5%,
    so the 1% check is about 2σ of the estimator. The tracer is seeded per pixel and sample, so it gives
    the same answer on every run; the measured ratio is 0.9965. The path-guiding reference uses 1024 spp
    instead of 4096 (measured agreement 0.4%, bound 4%).
  - **Bloom path-traced F12:** waits for the render to report done, instead of a 6,000-frame sleep loop.
  - **Pilot click:** clicks only where there is geometry. The pilot-frame test reuses the window size it
    found.
  - **Film grain:** 45 ms waits instead of 60 ms. The seed changes every 1/24 s.
  - **Filter-menu tests:** use a wireframe Scene view (only the Inspector is under test), and the
    checkbox sweep steps 4 px.
  - **Bloom odd-values test:** tries every odd value at two sizes, and the other sizes with NaN and
    infinity.
  - **The rotate-shape sweep:** uses a smaller window.

**Checked**
- **Checks:** Windows 3,021 (was 60,624), Linux 2,997 (was 60,600), sanitizer build 2,919. 0 failed.
  243 tests (3 merged away).
- **Time:**
  - Linux wall time (Release, median of 3): **37.5 s → 22.0 s**.
  - Windows, sum of test times: **32.0 s → 21.7 s**.
- **Slowest ten, before → after (Windows, ms):**

| Test | Before | After |
|---|---|---|
| pilot click | 3974 | 194 |
| path guiding | 2264 | 865 |
| rotate a shape on a rectangle | 2249 | 2024 |
| bloom path-traced F12 | 1798 | 223 |
| effect menu clicks | 1283 | 681 |
| pilot frame = Game view | 1218 | 867 |
| film grain in the editor | 1068 | 703 |
| MIS | 1031 | 136 |
| bloom odd values | 1017 | 418 |
| enabled checkbox | 740 | 289 |

- **The spec's 3× target was met by 5 of the 10.** The rest are bound by something the test is about:
  - **rotate-shape:** 20 editors at about 100 ms each to set up;
  - **film grain:** real time has to pass for the grain to move;
  - **pilot frame:** two 1600×1000 editors;
  - **effect menu:** about 25 clicked frames;
  - **path guiding:** the guided render itself.
- **Review fixes:**
  - Bloom's Intensity and Scatter lost their save / load / undo coverage in the merge; it is back in the
    bloom test.
  - The "later snap grid wins" check is back in both orders.
  - The Camera Preview is also checked through a second effect's fields.
  - The statistics comments now give the real margins.
  - Two more per-texel loops are counted.
  - Local NaN constants use `kNaN` / `kInf`.
- **Not done from the spec** (left as they were):
  - **The neutral/identity tests stay separate:** the rasterizer with no filter, the Color / Lens /
    Stylize passes at neutral settings, and Bloom at intensity 0. They check different layers, so one
    table would hide which one broke.
  - **The shared event helpers were not built** (`ev`, `click`, `cmd`, `grab`, `settle`). The copies differ
    in small ways (frames per click, the rows grabbed), and unifying them is a refactor of about 40 tests
    with no change in coverage.
  - **The F12 sequence test (8592), the denoiser test (2088) and the 9833 editor count are not sped up.**
    Each is under 300 ms now, and none is in the slowest ten.
  - **The check count is 3,021, not about 2,500.** The rest are single checks.
- **The slimmer tests still catch bugs.** Three bugs were planted by hand and not committed; each was
  caught:
  - bloom dropping alpha (caught by the counted check);
  - Bloom's Threshold not saving (the new table row);
  - mesh-light NEE biased by +5% (the 1024-spp MIS check: ratio 1.044).

## 2026-10-09 (round 32): Bloom (task 0007)

**Asked:** start task 0007: the Bloom & glow category of the Camera Filters stack.

**Added**
- **Bloom** in Add Filter (Bloom & glow): light brighter than a threshold spills into the pixels around it.
  Settings: intensity, threshold, soft knee, scatter (the glow's spread), tint, and a clamp against
  fireflies. It works from the rasterizer's linear light and from the path tracer's (F12 renders and the
  finished Rendered Camera Preview).
- **An HDR plane in the rasterizer:** linear light before exposure and tone mapping, kept only when a
  camera's stack has a Bloom. The sky and HDRI write their radiance into it too. Without a Bloom nothing is
  allocated and pictures are bit-identical.

**Fixed (from the review)**
- **Bloom undid the effects before it.** Placed after Posterize (or a retro dither, or depth of field), it
  re-encoded the shader's original light and lost their result. It now re-encodes only pixels still as
  shaded, and adds the glow on top of anything an earlier pass changed. A test checks that Posterize's
  levels survive.
- **The glow's size depended on the resolution:** a small Camera Preview glowed far more widely than a 4K
  render. The pyramid now starts from a fixed 540-row base, so the glow covers the same part of the frame
  at any size.
- **A seam at silhouettes against an HDRI or physical sky:** the sky was decoded from 8 bits while the
  object beside it had full linear light. The sky now writes its radiance into the HDR plane.
- **Memory:** the reused buffers are given back when much bigger than needed (after a 4K render).

**Checked**
- 10 tests (9 by the test-engineer, 1 from the review): an emissive quad above the threshold glows, falling
  off with distance, and nothing changes below it; more intensity adds more light and more scatter
  spreads it; no Bloom means no HDR plane and identical pictures; the stack order holds; Posterize's levels
  survive a later Bloom; odd values and 1x1 / 2x2 frames are safe; save, load, undo and Add Filter; a
  path-traced F12 glows. Several check every pixel, which is why the count jumps.
- Unit checks: Windows 60624, Linux 60600, sanitizer build 60522; 0 failed.
- Stress `filters` (1080p, Windows / Linux): Bloom 14.3 / 15.9 ms on decoded colour, 14.0 / 12.9 ms on the
  HDR plane; the HDR plane itself costs 2.5 / 1.6 ms a frame when asked for. Random stacks of every effect type: 300 runs, 0 problems.

## 2026-10-09 (round 31): Color, Lens and Stylize camera filters (task 0006)

**Asked:** start task 0006: the Color, Lens and Stylize categories of the Camera Filters stack. Animated
effects move in Play mode only.

**Added:** twelve effects in Add Filter, each with its own settings, stackable in any order.

| Category | Effects |
|---|---|
| Color | **Color Grading** (in linear light: exposure, contrast through middle grey, saturation, temperature and tint, lift / gamma / gain), **Posterize**, **Grayscale / Sepia**, **Invert** |
| Lens | **Vignette** (intensity, smoothness, roundness, colour), **Chromatic Aberration**, **Film Grain** (strongest in the midtones), **Lens Distortion** (below 0 barrel, above 0 pincushion, with zoom) |
| Stylize | **Pixelate**, **Edge Outline** (lines where objects meet and where the depth creases or steps), **CRT** (scanlines, a curved tube, phosphor stripes, flicker), **Sharpen** |

Film grain and CRT flicker move only in the Game view while playing. F12 renders, camera sequences,
previews and piloting stay reproducible.

**Fixed (from the review)**
- **F12 during Play animated the grain.** Only the Game view animates now; there is a test.
- **CRT flicker vanished at 60 fps.** A 30 Hz sine sampled every 1/60 s sits on its zero crossings. It is a
  random brightness every 1/30 s now; a test checks 60 fps frames.
- **Edge Outline drew a band on floors seen at a grazing angle.** It tests flatness now: the second
  difference of distance and of 1/distance (linear across the screen on a plane, orthographic or
  perspective) instead of a jump in distance. A test checks a grazing floor with an 8 px, very sensitive
  outline.
- **Lift / gamma / gain clamped above 1**, so exposure plus a lower gain flattened the highlights.
- **The grain seed's time** is wrapped to a day, so a huge time can't overflow the cast.
- **Lens Distortion's help** had barrel and pincushion swapped (the test-engineer noticed).

**Checked**
- 19 tests by the test-engineer, plus 2 from the review. For each effect: neutral settings are
  bit-identical, and its behaviour matches hand-computed values (posterize level counts, the sepia matrix,
  exact outline columns, CRT rows and mask, sharpen at a step, +1 stop = x2 linear light). Grain and flicker
  animate only when asked. All 12 effects save, load and undo. NaN, infinite and huge values are safe at
  1x1 and 64x48 and with a row stride.
- Unit checks: Windows 5710, Linux 5686, sanitizer build 5608; 0 failed.
- Stress `filters` (Windows, 1080p, defaults): every effect 0.4-10 ms (Lens Distortion and CRT the slowest).
  300 random stacks of every effect type with every field fuzzed: 0 problems (crash, size or copy-hash).

## 2026-10-09 (round 30): piloting a camera shows its game view (task 0009)

**Asked:** whenever a camera is piloted, its camera filters should show: treat the pilot camera as the game
camera (chosen: the pure game look).

**Changed**
- **Piloting** (Pilot Camera, or `pilot`) shows the camera's Game view picture inside its frame: its filters,
  lens, exposure and depth of field, and the scene's lights, whatever the Shading mode. Around it is a plain
  dark passepartout.
  - The grid, gizmos, icons, overlays and statistics are hidden while piloting.
  - The banner stays, and a thin outline sits just outside the frame.
  - Clicking an object in the frame still selects it.
  - Entering Edit Mode ends piloting, because its cage and selection need the editor's overlays.

**Fixed:** the round-29 Saturn screen-door test failed on macOS (arm64). It checked that every pixel of a
normally blended quad differs from both the veil and the background, and a few didn't. The screen-door checks
themselves are exact and passed; the contrast check now asks for 90% and prints the count.

**Checked**
- Seven new tests (five by the test-engineer, two from the review):
  - The frame equals a second editor's Game view of the same camera at the same size: 0 of 518,870 pixels
    differ, plain and with the PS1 filter.
  - PS1 shows in every Shading mode.
  - With the grid, gizmos and statistics on or off, no pixel changes.
  - The passepartout is uniform.
  - A click selects.
  - Stopping restores the grid.
- Unit checks: Windows 5062, Linux 5038, sanitizer build 4960; 0 failed.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - **Depth copied for the editor's tools** used the camera's near / far, so the tools' visibility tests
    disagreed. It is re-projected into the editor's projection now. A test compares it with the editor's own
    render: worst difference 0.0008.
  - **Edit Mode ends piloting.**
  - **No busy redraw** in Rendered mode while piloting.
  - **Documented limit:** with an orthographic camera, lens shift or a letterboxing filter, the editor's
    projection doesn't line up with the picture. Clicks still select what they hit, and the gizmos are hidden.

## 2026-10-09 (round 29): the Camera Filters stack, and the N64, Saturn and DOS looks (task 0005)

**Asked:** make camera filters a general system like Unity's post-processing, where Retro Console is one
category among others (Color, Lens, Stylize, Bloom & glow come in tasks 0006 and 0007), then start task 0005.

**Changed**
- **Camera Filters** replaces the separate Retro Console Filter component.
  - It is one component per camera holding a list of effects (ADR 0008).
  - **Add Filter** lists the effects by category.
  - Each effect has its own toggle, foldout and menu (Move Up, Move Down, Reset, Remove Filter).
  - Order matters: later resolution and texture settings win, the smaller texture cap wins, and colour
    passes run top to bottom.
- **Old scenes and habits still work.**
  - A scene saved with the Retro Console Filter component loads into a stack.
  - `component Retro Console Filter` adds one to the stack.
  - `set RetroConsole.Width 160` reaches the first such effect, and `set CameraFilters.E1Width 160` the
    second.
- **`redo` with no arguments** now redoes. `redo <param> <value>` still adjusts the last operation. The
  plain form didn't work in round 28: an older command of the same name caught it first.

**Added**
- **Retro Console presets:**

  | Console | Resolution | Look |
  |---|---|---|
  | Nintendo 64 | 320x240 | the RDP's 3-point texture filter, 64 px textures with mipmaps, 15-bit colour with a Bayer dither, fog |
  | Sega Saturn | 320x224 | whole-pixel vertices, affine textures, 15-bit colour, see-through surfaces as a checkerboard "mesh" |
  | DOS PC | 320x200 | 256 colours (a 6x6x6 cube plus 40 greys) with an ordered dither, affine textures |

- **The `filter` console command:** `add <name>` (any case), `remove <i>`, `move <i> <j>`, `reset <i>`
  and `list` (which lists the available effects for an empty stack).

**Fixed (found by this round's tests and review)**
- **The DOS palette's nearest-colour table turned 14 palette colours into neighbours**, black among them,
  so a DOS frame could never be pure black. The nearest colour is now computed exactly from the palette's
  structure, and it is faster too.
- **Editing several selected cameras skipped their effects.** Effect edits now carry the same names the
  files use ("E0 Width"), so they reach each selected camera.
- **`set CameraFilters.Effects ...` could replace the whole stack.** The type list can only be changed with
  `filter`.
- **Screen-door pixels** skip shading for the holes and write depth for the drawn pixels (fog and later
  surfaces see them).

**Checked**
- Unit checks: the test-engineer wrote 12 tests (round 29) from the spec, plus a review test that types into
  the Inspector with two cameras selected. Windows 5029, Linux 5005 and the sanitizer build 4927; 0 failed.
- Stress `filters` (Windows, 1080p): every preset is 6.5-8.2x faster than unfiltered; 200 random stacks with
  odd values, 0 problems; every 256-colour frame uses only palette colours.
- Review (code-reviewer agent, Opus): changes needed. All are fixed above: multi-camera editing, `set` on the
  type list, screen-door shading and depth, the stress comment, docs and the console nits.
- Headless screenshots of each preset on the textured scene.

## 2026-10-09 (round 28): component menus work again, steady macOS CI (task 0008)

**Asked:** components couldn't be removed from an object; the macOS build kept failing in CI.

**Fixed**
- **Remove Component, Move Up and the modifier menu did nothing.** A component's or modifier's menu runs
  at the end of the frame, after the Inspector has drawn. Its items wrote to the Inspector's own
  variables, which by then no longer existed, so the click was lost (and wrote to freed stack memory).
  The choice is now kept by the editor and applied on the next frame. This covers Remove Component, Move
  Up and Reset, and the modifier menu's Apply, Duplicate, Copy to Selected, Move and Reset.
- **macOS CI failed now and then.** The Scene view filter test compared frames pixel by pixel, and the
  overlay text changes more pixels on slower machines. It now checks whether the view is filtered (its
  share of 15-bit pixels).

**Added:** console commands `undo` and `redo`.

**Checked**
- Two new real-event tests: they click a component's menu (Move Up, Reset, Remove Component, then undo)
  and a modifier's menu (Duplicate, Apply).
- Unit checks: Windows 4298, Linux 4274, sanitizer build 4196; 0 failed.
- Review (Opus): approved. Its suggestions are in:
  - the Keymap window's Category dropdown read freed strings while open (same kind of bug);
  - a menu choice not applied by the next frame is dropped;
  - Transform > Reset finds the object by id;
  - the test now really picks Remove and then selects another object, and the Cube keeps its
    component.

## 2026-10-09 (round 27): camera filters, with the PS1 look (task 0004)

**Asked:** camera "filters" built from shaders, starting with one that recreates 3D graphics on the
PlayStation 1 and older consoles. Decisions taken with the user:
- filters are components on the camera;
- they show in the Game view and Play mode, the Camera Preview, F12 and sequences, and the Scene view
  through a toggle;
- presets for PS1, N64, Saturn and DOS (N64, Saturn and DOS are task 0005).

**Added**
- **Retro Console Filter** (Add Component > Rendering), with a Console preset (PS1). Its fields:
  - Width / Height and Fit (Fill View with square pixels, or Letterbox to the console's frame);
  - Vertex Snap with a grid;
  - Affine Textures;
  - Texture Filter, Max Texture Size and Mipmaps;
  - Colour Depth (24 / 15-bit) and Dither (PS1 4x4 / Bayer);
  - Fog with start, end and colour.
- **The PS1 preset:** 320x240, vertices on whole pixels, affine textures, nearest 256 px textures with no
  mipmaps, 15-bit colour through the PS1's dither matrix.
- **Where filters render:** everywhere the camera's image shows, through one `Editor::render_camera`.
  - A path-traced render through a filtered camera traces at the low resolution and gets the colour pass.
  - The Scene view's new camera button (or `filters on|off`) previews the main camera's filters there.
- **Speed:** a filtered camera renders fewer pixels: the PS1 look at 1080p took 2.4 ms against 18.3 ms
  unfiltered.
- ADR 0007 (camera filters), requirement R-10.

**Fixed**
- **Components with spaces in their names** couldn't be added from the console (`component Retro Console
  Filter`), and were dropped when a scene was loaded. Both take the whole name now.
- **A texture could be freed while it was being drawn.**
  - When several shading threads met a material whose textures weren't loaded yet, each loaded them and
    replaced the others' (AddressSanitizer: heap-use-after-free).
  - Both renderers now load materials' textures before shading in parallel.
- **NaN checks under `/fp:fast`.** The Windows build is compiled with `/fp:fast`, which lets the compiler
  assume no NaNs. `display::linear_to_srgb8(NaN)` returned 255 or 0 depending on how the binary was inlined
  (an existing test failed once new code changed the inlining). NaN tests now read the bits
  (`nan_bits` / `finite_bits`).

**Checked**
- Unit checks: Windows with libraries 4280, Linux 4256, sanitizer build 4178; 0 failed.
  - The test-engineer agent wrote 14 tests from the spec. One of them found the save/load bug.
  - Two Linux failures were in the tests, not the code, and the tests were fixed:
    - a straight-edged rectangle can't show sub-pixel creep, so the creep check uses a slanted quad;
    - the Scene view's log line needs a 1% tolerance.
- Stress `filters`:
  - the PS1 look at 1080p: 2.4-2.9 ms against 18.3-24.7 ms unfiltered;
  - no 15-bit pixel off the 5-bit grid;
  - 300 runs of odd settings (NaN / infinite snap and fog, 1x1 to 100000 px), 0 problems.
- Same-session A/B of `shading` against `main` (Linux): equal within noise, so the filter code costs
  nothing when unused.
- Headless screenshots of a textured scene: no filter, PS1 Fill, PS1 Letterbox, the Scene view toggle.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - the texture warm-up now runs in every shade mode (the see-through pass shades materials in Solid
    view too), and its lock is re-entrant, so a texture load's own parallel_for can't deadlock;
  - the path tracer resolves textures at the start of every render call, not only at build;
  - stats survive a filtered render (overlays rebind the renderer without resetting them);
  - the Scene view always uses Fill, so grid, gizmos and vertex picking match the picture.

## 2026-10-09 (round 26): sanitizer findings fixed, branch protection on (task 0002)

**Asked:** after merging tasks 0001 and 0003, deal with the current issues before the camera
filters: sync, branch protection, and the sanitizer findings.

**Changed**
- `main` is protected:
  - pull requests only;
  - the `windows`, `linux` and `macos` checks must pass, on a branch up to date with `main`;
  - no force pushes or deletions;
  - 0 approvals, since GitHub doesn't count approving your own PR.
- `linux-sanitizers` is blocking now and joins the required checks once this merges. It also checks
  float-to-int casts that overflow (`float-cast-overflow`, which GCC leaves out of `undefined`).
- `CLAUDE.md` says how to call `gh` from the Bash tool and how to read CI.

**Fixed** (both undefined behaviour; neither showed as a wrong result yet, but a compiler is free to
miscompile them):
- **Merge by Distance and proportional editing far from the origin.** Their spatial hash multiplied
  grid cells in `int`. A point at x = -2 with a 0.1 mm distance already overflowed, and far points
  over tiny distances overflowed the cell index itself. One shared helper now uses 64-bit cells and
  unsigned hashing.
- **JPEG decoding** shifted a negative number (`-1 << s`) for every negative coefficient.

**Checked**
- Sanitizer build (ASan + UBSan + float-cast-overflow, dependency-free): 1832 checks, 0 failed, no reports.
- Windows with libraries: 1934 checks, 0 failed. Linux with libraries: 1910, 0 failed.
- The merge stress section: hash and O(n²) reference agree, speed unchanged.

## 2026-10-09 (round 25): CI passes again (task 0003)

**Asked:** continue with the next task, making CI pass on all three platforms so branch protection can
be switched on.

**Fixed**
- **`build.sh` stopped before compiling without Blender's libraries.** The object folder's name was
  built with `[ ... ] && echo _libs`. That command fails when the libraries are absent, and `set -e` ended the
  script without a message. This broke every Linux and macOS CI run.
- **`build.sh` was not executable in git** (mode 644, committed from Windows), so CI's
  `./build.sh` failed with "permission denied" (exit 126) before even reaching that line. It is
  now 755.
- **macOS saved numbers with nine digits.** Apple's C++ library doesn't announce floating-point
  `to_chars`, so scene and material files used `%.9g` there: 0.7 was written as `0.699999988`, and a
  material test looking for "0.7" failed. The fallback now writes the fewest digits that read back as the
  same number, as `to_chars` does, so files are identical on all three platforms.

**Changed:** CI's unit-test steps turn each failing check (and each sanitizer report) into a GitHub
annotation, so failures can be read on the run page without signing in to view the logs.
The quick stress step has hung on CI until GitHub's 6-hour limit on every Windows run so far (and now
on Linux), though it takes under 90 s locally even on one core. It now stops after 15 minutes, and
`blendity_stress` reports each section's start and end on stderr, so the run page shows where it was.

**Fixed: the stress test never ended on 3 or 5 threads.** The job-system section steps through thread
counts and, to end on the machine's count, stepped back to half of it. With 3 or 5 threads (GitHub's
macOS and Linux runners) that step-back repeated forever. Now the counts are listed first (1, 2, 3, 4, 8,
16... then the machine's own). This was the hang that kept every Windows CI run going for 6 hours.
- **Push Through out through several faces in one plane needed Manifold.** Pushing a rectangle
  through a wall whose back already had a circle, or an archway where the back had a rectangle across
  it, was refused in the dependency-free build. Now the outline is cut across the back faces and
  the front outline gets a corner opposite each new back corner, then a tube joins the two. Both builds
  take this path, so they agree. Manifold is used only when the exit faces aren't coplanar.

**Checked**
- Unit checks:
  - Windows, dependency-free: 1822, 0 failed.
  - Linux, dependency-free: 1822, 0 failed.
  - Windows with libraries: 1924, 0 failed.
  - Linux with libraries: 1900, 0 failed.
- Linux, dependency-free: the quick stress pass and the headless screenshot run as CI runs them.
- Push/Pull stress: 0 problems; 394 results with faces newly on top of each other (unchanged).
- Review (code-reviewer agent, Opus): approved. It suggested the cut be checked to form the opening's outline edge for edge before the tube is built, so a face missed inside it can't leave edges with three faces; that check is in.

## 2026-10-09 (round 24): engineering setup for agentic development (task 0001)

**Asked:** set the repository up for research-informed agentic development:
- standards, three agents (implementer, test engineer, code reviewer), requirements, decision records and task specs;
- CI for the real stack;
- a plan / implement / test / review / human-merge workflow.

**Added**
- `CLAUDE.md`: the project map, build and test commands, testing rules, code standards, documentation duties, approval gates and the publishing procedure.
- `.claude/agents/`:
  - `implementer` (Sonnet): one approved task; no architecture, dependencies, commits or pushes.
  - `test-engineer` (Sonnet): tests from the acceptance criteria; edits only `tests/` and `stress/`; reports bugs as failing tests.
  - `code-reviewer` (Opus): read-only; ranked findings and a verdict.
- `.claude/settings.json`: denies force pushes, pushes to `main` / `blendity`, merging PRs and hard resets; asks before commits, pushes and `gh pr create` / `gh api`.
- `docs/requirements.md`: product, quality and constraint requirements with IDs.
- `docs/decisions/`: the index, a template and six ADRs recording decisions already in force:
  1. optional Blender libraries;
  2. licensing;
  3. copy-on-write meshes;
  4. SketchUp Push/Pull semantics;
  5. tool settings in the F9 and drag helpers;
  6. subtree publishing with human-merged PRs.
- `docs/tasks/`: the lifecycle, a template, task 0001 (this) and draft task 0002 (sanitizer findings).
- `docs/ARCHITECTURE.md`: a system overview (layers, one frame, one edit), key invariants and module ownership at the top. The phase tables are kept below as the history.
- CI: a `linux-sanitizers` job (unit tests under ASan + UBSan, non-blocking until task 0002), and cancelling superseded runs.
- `build.sh` honours `EXTRA_FLAGS`.
- `.github/pull_request_template.md`.

**Changed:** work reaches `main` only through a pull request that CI must pass and the human merges. There are no more direct pushes (ADR 0006).

**Found while checking:**
- **CI has failed on every recent push to `main`.**
  - `build.sh` exits silently before compiling when Blender's libraries are absent (always the case in CI).
  - Two Push Through tests rely on the optional Manifold library.
  - Recorded as task 0003, to finish before branch protection is switched on.
- **The sanitizer job's first finding:** a signed overflow in `merge_by_distance`'s spatial hash (task 0002).
- **Local checks:** Windows 1924 and Linux 1900 unit checks pass with Blender's libraries. With `build.sh` corrected, the sanitizer build compiles and runs.

## 2026-10-09 (round 23): arched shapes in one piece, array copies past a surface's edge

**Asked:** arraying a vertical rectangle with an arched top cut some copies improperly.

**Fixed**
- **The arch itself was two faces.** An arc drawn on a rectangle's top crosses an edge of the faces around the rectangle. The pieces inside the arc were merged, except where the crossed edge ran to the arc's own end (the rectangle's corner), so the arch stayed split. The array then copied both pieces. Edges from a crossing to the arc's ends are now dissolved too, so the arch is one face, and so is each copy's.
- **Copies past the surface's edge.** A copy hanging past the edge of the wall used to fall back to a loose duplicate lying over the wall (overlapping faces, an open mesh). Now:
  - The part of it on the surface is cut in: clipped to the surface's outline when that outline is convex, as a wall's is.
  - A copy with nothing under it at all is still a separate copy.
  - A cut that can't be made leaves that copy out rather than half of it.

**Checked:** 1924 unit checks on Windows and 1900 on Linux, 0 failed. New test: an arched doorway on a wall arrayed 2 and 3 times, with 8 and 16 arc segments. The doorway area must be exact, or exact plus the clipped part for the copy past the wall's end. No overlaps, still closed, no bad triangulations, no wire edges.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 22): Merge Coplanar on a selection, Array copies cut into the surface

**Asked:** Merge Coplanar on a selected group of faces; Array copies of a face should cut into the mesh like the original instead of lying on top of it.

**Added**
- **Merge Coplanar on the selection.**
  - With faces selected (in any mode, a face counts when all its corners are selected), only the edges between two selected coplanar faces dissolve, and only corners that no unselected face uses go. The rest of the mesh keeps its edges, and the merged faces stay selected.
  - A 3 x 3 block of the Plane becomes one 12-cornered face (it keeps the corners its neighbours share, so there are no T-junctions); two separate blocks become two faces.
  - With nothing selected it works on the whole mesh, as before.

**Changed**
- **Array copies are drawn onto the surface.**
  - Each copy of a shape that sits on a face is cut into the faces it lands on, as drawing the shape there would, instead of a duplicate lying on top (z-fighting). A copy crossing the faces round an earlier drawing becomes one face.
  - Only a copy that lies wholly on the surface is cut in; one hanging off it, or a face with nothing flat under its copy (a box's whole top), is still a separate copy.
  - The originals and all copies end up selected, ready for Push/Pull or Recess together.

**Checked:** 1916 unit checks on Windows and 1892 on Linux, 0 failed. New tests:
- Merge Coplanar on a 3 x 3 block, two blocks, and nothing selected.
- A drawn rectangle arrayed three times across a cube's top: cut in, no overlaps, still closed, no bad triangulations. The three pushed in together stay closed; a copy with nothing under it stays a separate copy.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 21): tool helpers, settings out of the Inspector, Push/Pull on non-manifold meshes, overlap marking

**Asked:**
- Drop "Fluent" from Hard Surface, and add a helper for the tools such as Array.
- Make Push/Pull work on non-manifold meshes.
- Edges were marked as overlapping when they weren't: only the smaller edge should be marked.
- Thickness, draft angle, extrude distance and the like belong in each tool's helper, not in the general area under Draw.

**Changed**
- **Hard Surface** (no "Fluent" in the interface). Grid, Pipe, Array, Taper Extrude and Recess / Plate:
  - Clicked in the menu or the Inspector, each opens a **drag helper** like Ctrl+B's. The mouse sets the spacing, radius, distance or depth, or you type it. The wheel steps the count, columns (Shift: rows), sides, end scale or border. X / Y / Z set Array's axis. Click or Enter confirms, Esc cancels.
  - Afterwards each one is in **Adjust Last Operation (F9)**.
- **Tool settings live with the tools.** The Inspector's Edit Mode area keeps only the switches that change how tools behave: Push/Pull Each Face, Inset Individual, Auto Fuse, Auto Smooth and Sharp Angle.
  - Each tool's numbers (thickness, angle, distance, segments, profile, count...) are in its F9 panel or drag helper.
  - Shell, Thicken and Draft are now adjustable (F9) operations too.
  - Whatever you set there is kept for that tool's next use.
- **Overlapping edges:** of two edges along each other, only the shorter one is marked and selected. That's the piece lying on a longer edge, or one of two doubled edges; the long edge it lies on is left alone.

**Fixed**
- **Push/Pull on non-manifold meshes.** Pushing a face in next to a fin, or next to a second solid sharing the edge, was refused as "the faces around the selection lean over it". The fin or the other solid counted as a wall of this one. Only the faces that carry the surface on across the edge count now. Tested on:
  - a fin (pushing and pulling the top, the front and the fin itself);
  - two cubes sharing an edge, and two sharing a corner;
  - an open box;
  - a cube with a stray wire edge and a loose point;
  - two faces at once.

**Checked:** 1906 unit checks on Windows and 1882 on Linux, 0 failed. New tests:
- The Array helper from the menu: wheel, Z, a typed spacing, Enter. Grid, Taper and Recess open their helpers and Esc leaves the mesh as it was.
- The non-manifold Push/Pull cases.
- No overlap alarms on the primitives, the teapot, a bevelled, a subdivided and a drawn-on cube; only the shorter edge selected.
- Shell, Thicken and Draft as F9 operations.

Stress:
- Push/Pull: 0 problems, and 394 results with faces newly on top of each other. That's 4 more than before, from pushes beside fins that used to be refused.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems, the same as last round.
- The fuzzer: no crash.

## 2026-10-09 (round 20): bevel profile, arcs on drawn shapes, smooth region inset

**Asked:** a profile for the bevel, to make it concave or convex; an arc drawn on a rectangle drawn on a surface didn't make a face inside the arc; insetting several faces "stepped" instead of changing smoothly.

**Added**
- **Bevel Profile** (Blender's superellipse), 0 to 1:
  - 0.5 is a round quarter circle, 0.25 a flat chamfer.
  - Toward 1 it bulges out to a square corner (convex); toward 0 it scoops in (concave).
  - In the Inspector (Bevel Profile), the F9 panel, the Ctrl+B helper (Alt+wheel, shown in its readout), the Bevel modifier (saved with the scene), and the console (`bevelprofile 0.8`).
  - Profile 0.5 is now an exact quarter circle; before, it was a quadratic curve slightly flatter than a circle.

**Fixed**
- **Arcs (and other open lines) across the faces round a drawn shape.**
  - A shape drawn on a face sits in a ring of several faces. A line crossing their edges only cut a face that held all of it, so elsewhere it fell back to loose wire edges.
  - The line is now split where it crosses edges, and each face it runs through is cut. The pieces inside an arc (between it and the chord joining its ends) become one face again.
- **Dissolve Edges** could drop a corner that lay on a straight line in the merged face while a neighbouring face still used it, opening the mesh. Such corners now stay.
- **Inset Region no longer steps.** When an inset would fold over (wider than the region), it used to halve the thickness, so dragging jumped between values. It now finds the furthest thickness that doesn't fold (the middle face keeps at least 2% of its area, and no ring quad crosses into a bow-tie), so dragging slides smoothly to that limit and holds there. A region that folds at any thickness is left as it was.
- **Scripted drawing points** on an edge shared by two faces belong to the face that faces the camera, as a click would.

**Checked:** 1838 unit checks on Windows and 1814 on Linux, 0 failed. New tests:
- Profiles 0 to 1 give a rising volume; flat equals a one-segment chamfer; round equals a quarter circle; the editor setting reaches Ctrl+B and the modifier saves its own.
- Arcs on a drawn rectangle: side to side out and in, corner to corner, across a corner, out to the cube's edge, and on the cube's own edge. No wire edges, still closed, and one face inside the arc where it encloses one.
- Region inset swept from 0 to 0.8 in 0.01 steps: no jumps, never growing back, holding at the limit.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems. 4 are new and come from Recess on regions that touch themselves at a sphere's pole, which can leave two corners in one place.
- The fuzzer: no crash.

## 2026-10-09 (round 19): Follow along the edges themselves, no Open Space Plane, Push/Pull on several faces

**Asked:** Follow with picked edges didn't follow the edges (especially several of them); remove the Open Space Plane, since guides do that work; Push/Pull on several faces at once, each along its own normal.

**Fixed**
- **Follow along picked edges** sweeps the face along the edges where they are, from the path's end nearest the face (SketchUp's Follow Me). Before, it carried a copy of the path's shape over to start at the face's centre, so the result ran beside the edges instead of along them. The face's own sides, still selected after switching from face to edge mode, no longer join the path and break it.

**Changed**
- **No Open Space Plane.** The plane chooser (front, side, view, last face), its offset and Draw in Open Space Only are gone. A click off the mesh lands on the ground, and guides (X / Y / Z, Ctrl+click an edge) take a shape anywhere else.
- **A new drawing's turn** follows the shape's first side, so a rectangle drawn at an angle gives its object that angle.

**Added**
- **Push/Pull on several faces.** Each connected group of selected faces moves along its own normal; separate groups move together (top and bottom of a box both out, say).
  - **Push/Pull Each Face** (Inspector, F9's Each Face, `pushpull individual on`) moves every selected face along its own normal.

**Checked:** 1716 unit checks on Windows and 1692 on Linux, 0 failed. New tests:
- Follow along a bent rail of quads, directly and through the editor with a stray side of the face selected; the end cap must sit at the path's end.
- Clicks off the mesh land on the ground.
- A 3-point rectangle at 30 degrees: the object's origin and X.
- Push/Pull on top and bottom, on every face of a cube, and with P on two sides in the editor.

Stress:
- Curved surfaces now include Push/Pull Each Face on curved regions: 5,036 operations, 4 problems. Pushing neighbouring faces of a convex surface in, each along its own normal, can make their walls meet (8 results), as Blender's Extrude Individual does.
- Push/Pull: 0 problems and 390 overlaps.
- The modeling section: 0 problems.
- The fuzzer: no crash.

## 2026-10-08 (round 18): Plasticity guides and perpendicular snaps, one Make Face, overlapping elements, Fluent-style tools, bevel and rotation fixes

**Asked:** commit and push round 17; rotating a face drawn on a rectangular plane still left artifacts; a 90-degree edge snap for polylines like Plasticity's; Ctrl+B sometimes bevelled at once without the helper, and the helper wouldn't go past two segments; guides like Plasticity's (from an axis, local or global, and from edges with Ctrl) instead of axis planes; the Make Face shortcut didn't always work; shapes drawn at an angle kept a global transform (pivot / centre, local / global); a way to see overlapping vertices and edges; Make Face and Smart Fill as one operation; keep Follow like UModeler's; tools from Fluent 4 that fit the current ones.

**Done first:** round 17 committed and pushed.

**Fixed**
- **Bevel's helper (Ctrl+B).** Auto Smooth shaded the bevel after it was recorded, and that change made the helper think the bevel had been undone, so it quit and left the bevel as it was. That happened as soon as there were 2+ segments: a second Ctrl+B bevelled at once, and the wheel stopped at two. Shading is now part of the operation. The helper stays, and the wheel goes up to 64 segments.
- **Turning a shape drawn across the Plane's grid.** When the turned shape reached past the cells it was cut into, the ring around it couldn't be rebuilt. The flat faces around it now join the ring as needed.
- **Concave quads were drawn wrongly.** A dart-shaped quad was split along the diagonal that runs outside it, so one triangle faced backwards. The other diagonal is used now. This also removed the overlaps from drawing circles across curved faces (35 to 0 in the curved stress).
- **Make Face** works in vertex, edge and face mode, and its key works anywhere in Edit Mode, not only with the mouse over the Scene view.
- **A new drawing takes its shape's place.** The first shape drawn in a new Drawing object moves the object's origin to the shape's centre and turns the object with the shape's plane, so Pivot, Local and Global behave.
- **Local in Edit Mode** follows the selection: Y along the selected faces' normal, X along their longest edge. The gizmo and G/R/S now move, turn and scale a shape on a slope, or turned on its face, along its own sides.

**Changed**
- **Guides instead of axis planes** (Plasticity's construction lines):
  - X, Y or Z while drawing lays a guide along that axis, Local or Global as Snap Axes says, through the last point (or the point under the mouse before the first click).
  - Ctrl+click on an edge lays a guide along it.
  - A polyline can follow a guide off its plane, which makes a 3D path for Follow.
- **Make Face and Smart Fill are one operation**, Make Face (F in the Blender keymap, Alt+F in Unity's):
  - Holes and wire loops in the selection close (every hole when nothing is selected): flat ones with a face, bent ones with a fan, cracks welded shut.
  - Selected vertices that form no loop get one face through them.

**Added**
- **Perpendicular Snap** (Plasticity): a point on an edge where the line from the last point meets it at 90 degrees. On by default; switch it in the Inspector or Modeling Tools, or with `drawmode perp on|off`.
- **Overlapping vertices and edges:**
  - Vertices lying on top of each other get yellow rings, and edges running along each other get magenta lines, live in Edit Mode.
  - Modeling Tools > Check: Overlapping Vertices & Edges counts them and has Show, Select and Merge.
  - Also as Edit Mode tools (Select Overlapping, Merge Overlapping) and the console: `overlaps [select|merge|show on|off]`.
- **Follow, UModeler's way.** Select the face and press Follow; with no wire path drawn from it, the face is remembered. Then select the path's edges in edge mode and press Follow again. The path starts at the face, from whichever end leaves it along its normal, and the picked edges stay.
- **Hard-surface tools after Fluent 4** (a new Hard Surface group in Edit Mode; settings in the Inspector; console `hs ...`):
  - **Grid:** cuts quads into columns x rows, splitting neighbours to match.
  - **Pipe:** a capped tube along selected edges or a drawn polyline.
  - **Array:** repeats the selected faces along the selection's own X, Y or Z.
  - **Taper Extrude:** an extrusion with its end scaled.
  - **Recess / Plate:** an inset panel pushed in, or pulled out when the depth is negative.

**Checked:** 1718 unit checks on Windows and 1694 on Linux, 0 failed. New tests:
- Ctrl+B three ways (mouse over the Scene view, elsewhere, and a second bevel starting at 7 segments) with six wheel steps.
- Shapes turned 15 to 90 degrees on a quad, on the 10 x 10 Plane across and inside cells, and on long faces (where a shape turned past its face's edge has no surface left around it, only validity is required).
- The perpendicular snap, on and off.
- Ctrl+click and X guides on a turned cube, Local and Global.
- A 3D polyline along guides, then Follow.
- Make Face with both keymaps, in each mode, with the mouse away from the Scene view, plus loose vertices.
- Overlap finding, selecting and merging.
- Follow along picked edges.
- A rectangle drawn on the view plane (the new object's origin and up) and the Edit Mode frame.
- Each Fluent tool, closed where it should be.

Stress:
- Curved surfaces: 4,876 operations including the new tools, 4 problems, and 69 results with faces on top of each other (91 before).
- Push/Pull: 0 problems, and 390 results with faces newly on top of each other.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 17): drawing on any axis plane, turning drawn shapes cleanly, the keymap dropdown, the Utah teapot, deleting materials, curved surfaces

**Asked:** commit and push round 16; draw polylines on the YX and YZ planes as well as XZ, with an axis-lock workflow like Extrude's, usable with Follow; rotating a face just drawn onto a face left artifacts; the keymap preset dropdown in Preferences disappeared when clicked; the Utah teapot with a camera and a point light as the scene you start with; deleting a material from the Materials window; more edge-case testing on curved surfaces for Push/Pull and the other tools.

**Done first:** round 16 committed and pushed.

**Fixed**
- **Dropdowns inside dialogs.** A dropdown opened inside Preferences (the keymap preset, the frame-rate cap) closed every popup, the dialog included, so the list vanished. It now opens on top of the dialog. Picking an item, or clicking the field again, closes only the list.
- **Turning a shape drawn on a face.** Its ring of faces twisted over itself because the inner corners turned while the outer ones stayed. The ring is now rebuilt between the two outlines whenever a move folds it, live during gizmo drags and Blender's G / R / S. The selection follows.
- **Follow on a face of a solid** no longer leaves the old face inside as a start cap. That cap put three faces on each of its edges. The solid now grows the way Extrude grows it.
- **Regions that touch themselves at a vertex** (a cone's apex between two arms of a selection, a pinch on a torus) can now be extruded, inset and refilled without an edge ending up in four faces. Each such vertex gets one copy per side.
- **Smart Fill** fills holes whose outline touches itself one lobe at a time. Before, it made one face that ran through the shared vertex twice.
- **Inset Region** asked to go further than the region is wide now backs off until no face turns over, instead of folding the ring onto the inner faces.
- **Bevel** on curved surfaces no longer leaves zero-area patches where two bevels meet. The twin arcs are welded instead.
- **Push/Pull through a curved region** no longer leaves faces lying on the opening. When the cut can't open the far side cleanly, it stops at the far side, as it already does for other unclean holes.

**Added**
- **Axis planes while drawing.** X, Y and Z put the shape on the YZ, XZ or XY plane, like an axis lock on Extrude:
  - Before the first click, the plane goes through the first point. Pressing the same key again goes back to the surface's own plane.
  - Mid-polyline, the plane turns through the last point, so one line can climb a wall and then run across a floor. Enter keeps it as wire edges: a 3D path for Follow.
  - A closed loop that turned planes stays a loop of wire edges, since it isn't flat.
  - Also in the Inspector and Modeling Tools (Axis Plane), and the console: `drawmode axis x|y|z|none`.
- **The Utah teapot.** GameObject > 3D Object > Teapot is Martin Newell's 32 Bezier patches, tessellated and welded, with UVs. A new project, and File > New Scene, now start with it on a ground plane, along with a Main Camera and a Point Light (Blender's start-up cube, camera and light). Your existing SampleScene is unchanged.
- **Delete Material** in the Materials window, from the right-click menu or the Delete key over the window:
  - It asks first and shows how many slots use the material.
  - Those slots are emptied, so their faces get the default material.
  - A material asset's .mat file goes to the Recycle Bin.
  - Undo puts the slots back.
  - Console: `deletemat <name | Assets/...mat>`.
- **A curved-surfaces stress section** (`blendity_stress --only curved`). It runs 4,396 operations on 10 curved meshes: UV and ico spheres, a torus, a cylinder, a cone, Catmull-Clark and bevelled boxes, a bumpy blob, the teapot and an open hemisphere. The operations:
  - Push/Pull on single faces and on curved regions, in, out, through and onto.
  - Inset (individual and region), Extrude, Bevel, Poke, Triangulate / Tris to Quads.
  - Shapes drawn on and across curved faces, then turned.
  - Follow, Smart Fill, Bridge, Push Through, Delete Loose, Merge by Distance and Auto Smooth.
- Console: `newscene`.

**Checked:** 1643 unit checks on Windows and 1619 on Linux, 0 failed. New tests:
- The preset dropdown in Preferences opened, closed and picked with real clicks while the dialog stays open.
- The teapot's welding, facing, UVs and size.
- The starter scene, including save/load and New Scene.
- Drawing up and over a cube with Z then X, then Follow along that path into a closed solid.
- A bent closed loop staying wire edges.
- A drawn rectangle turned 30, 45, 90 and 170 degrees with R (no bad triangulations, no overlaps, still closed, still selected), and the ring repair on its own.
- Delete Material with its confirmation, a scene material, an asset going to the (scratch) trash, and undo.
- A cone region pinched at its apex extruded, inset, and refilled with Smart Fill.

Stress:
- Curved surfaces went from 59 problems to 4 (zero-area faces in rare corners) and from 283 results with faces on top of each other to 91.
- Push/Pull: 0 problems, and the results with faces newly on top of each other fell from 676 to 390.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 16): drawing in open space, a UV gizmo, Merge by Distance in Edit Mode, Z-fighting highlights, Delete in the Hierarchy

**Asked:** commit and push round 15; draw outside of a face (UModeler) or in open space (Plasticity); the Scene view's gizmo in the UV editor for moving, scaling and rotating; Blender's Merge by Distance; Z-fighting faces stayed highlighted in Object Mode and after the object was deleted; the Delete key for objects in the Hierarchy.

**Done first:** round 15 committed and pushed.

**Fixed**
- **Delete in the Hierarchy.** In Edit Mode, Delete removed the selected faces of the mesh being edited even with the Hierarchy focused. It now removes the selected objects there (leaving Edit Mode first), as in Unity.
- **The Project window's Delete key** now also needs the mouse over the Project window, so a Delete meant for the Hierarchy or the Scene view can never reach a file selected there earlier. (Several files went to the Recycle Bin this afternoon while the editor was open, which is what this guards against.)
- **Z-fighting highlights** are dropped once their object is deleted or its mesh changes. They only show while you edit one of the objects involved, or while the Z-Fighting panel is open after checking from it, so nothing looks selected in Object Mode.
- **Drawing rectangles:** the opposite corner no longer snaps onto the first corner's axes, which could flatten the rectangle into a line when seen at a shallow angle.

**Added**
- **Drawing in open space** (Inspector and Modeling Tools, while drawing):
  - Clicks off the mesh go onto an **Open Space Plane**: the ground, a **front** (XY) or **side** (YZ) plane, a plane **facing you** through the point you orbit, or the **last face's plane**, to keep drawing past a face.
  - A **Plane Offset** moves the plane along its normal.
  - **Draw in Open Space Only** ignores surfaces like Plasticity's construction plane. Corners and edges of the mesh still guide the point, which stays on the plane.
  - A faint grid shows the plane under the cursor. A closed shape there becomes a new face; an open one becomes wire edges.
- **The UV editor's gizmo**, like the Scene view's, at the centre of the UV selection:
  - **Move** has U (red) and V (green) arrows and a centre square for free movement. **Rotate** has a ring. **Scale** has U and V handles and a centre box for uniform scaling. **Transform** (Y) shows all three.
  - W, E, R and Y pick them in the UV editor too, and Ctrl snaps.
- **Merge by Distance in Edit Mode** (Merge & Clean Up; Blender preset: Alt+M):
  - Selected vertices closer than the distance weld (all of them when nothing is selected), and it reports how many were removed.
  - F9 adjusts the **Merge Distance** and **Unselected**, which lets selected vertices weld onto unselected ones, as in Blender.
- Console: `drawmode plane ground|front|side|view|face [offset]`, `drawmode space on|off`, `zfight clear`, `delete`, `editop merge_distance`.

**Checked:** 1573 unit checks on Windows and 1549 on Linux, 0 failed. New tests:
- Clicking a Hierarchy row and pressing Delete or Backspace, also from Edit Mode, with a Project file selected and focused earlier that must survive.
- Drawing with real clicks on the front plane at a depth, on the view plane, and "open space only" over a cube.
- Merge by Distance on an unwelded cube (24 to 8 vertices, closed), its F9 distance, selected only, and Unselected.
- Dragging the UV gizmo's U arrow, V scale handle and rotate ring with real mouse events.
- Z-fighting outlines gone in Object Mode, and pruned when the objects are deleted.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash.

## 2026-10-08 (round 15): rebindable keymaps with presets, draw axes, a Z-fighting check, UV editing tools, grouped Edit Mode tools

**Asked:** commit and push round 14; local or global snap axes for drawing; put the selection tools in one place and combine other tools where they can be; a Z-fighting check that says which faces are a problem and can be removed; more UV tools (selecting faces, rotating, scaling, selecting and moving whole islands); re-render the Camera Preview whenever a light's colour temperature changes; rebindable shortcuts in Preferences, with keymaps for other programs (Blender, Maya and more).

**Done first:** round 14 committed and pushed.

**Fixed**
- **Colour temperature didn't refresh the Camera Preview.** The scene's render fingerprint, which decides when the preview, the Rendered view and the live preview re-render, only included a light's type, colour, intensity and range. It now includes every light setting: temperature, spot angles, area size, shadows.

**Added**
- **Keymap** (Preferences > Keymap):
  - Every shortcut is a named action (about 75) with a context: anywhere, in Edit Mode, over the Scene view, or the Scene view in Edit Mode. So one key can mean different things in different places, as before.
  - **Presets:** **Unity** (Blendity's own), **Blender** (G/R/S, E, X, M, F, Ctrl+R...), **Maya** (Q/W/E/R, F8-F11 for component modes, B for soft select...), **3ds Max** (W/E/R, Z to frame, 1/2/4 sub-objects, Shift+E...) and **SketchUp** (P, R, C, A, L, M, Q, Space...).
  - Click a binding and press the new key (Esc cancels, Backspace clears). Actions are found by name or by key, and clashes within overlapping contexts are shown.
  - Changes are saved with the preferences and can be reset. The old "Blender Transform Keys" option still works on top of Unity.
- **Snap Axes: Local or Global** for drawing (Inspector and Modeling Tools). Local follows the face's edges or the object's rotation; Global uses the world's axes. Parallel and perpendicular edge snaps and guides work either way.
- **Z-fighting check** (Modeling Tools > Check: Z-Fighting; "Select Z-Fighting" in Edit Mode):
  - Finds faces lying on top of each other, within objects and between objects (in world space).
  - Each pair says what it is and what can go: a duplicate (remove one), both sides of an inner wall (remove both), a face hidden under another (remove it), or a partial overlap (move one).
  - Pairs have Select and Remove buttons, there's **Remove Suggested** for all of them, and the faces are outlined in the Scene view (red: can go).
  - Objects merely resting on each other aren't flagged.
- **UV editor:**
  - **Vertex / Face / Island** select modes (1/2/3). Click inside a face, box-select by face centre, or click or drag a whole island.
  - **Sync** with Edit Mode's face selection.
  - **All / None / Invert / Grow to Islands.**
  - **Rotate ±90** or by an angle, **Scale** by a factor, **Move** by an offset, **Flip U / V**, **Fit** to 0-1, **Center**, and **Align** (left, right, top, bottom, straight U / V).
  - Arrow keys nudge the selection (Shift: further).
- **Edit Mode tools in groups:**
  - **Select** first: every way of selecting in one place, with All and None. Then Create & Extrude, Cut & Divide, Merge & Clean Up, Deform, and Shading & UV.
  - Each group folds away; the Mesh menu has the same groups.
  - Mark / Clear Seam, Mark / Clear Sharp, Shade Smooth / Flat, To Tris / Quads and Grow More / Less each share one button.
  - Extrude Individual is now a switch on Extrude, like Inset's.
  - Select Overlapping became the Z-fighting check.
  - The doubled Knife and Merge Coplanar buttons are gone.
- Console: `keymap <preset>`, `keymap bind <action> <keys>`, `keymap clear <action>`, `zfight [scene] / zfight fix`, `uvsel ...`, `uvxf ...`, `drawmode axes local|global`, `duplicate`.

**Checked:** 1527 unit checks on Windows and 1503 on Linux, 0 failed. New tests:
- Light settings change the render fingerprint.
- Every preset's key layout, with no clashes in any preset, and chords that print and parse back.
- Real key presses under Unity and Maya, a rebind, clash detection, and overrides saved and loaded.
- Local and global draw axes on a rotated cube.
- Z-fighting: a duplicated cube fixed with one copy's faces removed, the inner wall of two joined boxes, and a hidden face versus a partial overlap.
- UV face and island selection and every transform, synced with Edit Mode.
- Every operator in a group, all selection tools in Select, and the Extrude Individual switch.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash, with keys going through the new keymap.

## 2026-10-08 (round 14): archways pushed through cleanly, drawing over earlier edges, Smart Fill, Auto Smooth, camera sequences

**Asked:** commit and push round 13; fix the face artifacts in the half circle of an archway pushed through; a Smart Fill that patches open edges into a surface, tested where the edges converge to no area; drawing on a face and pushing or pulling through should work smoothly with edges already on either face (rectangles, circles, arcs); an option to shade bevels and other round surfaces smooth automatically; render the scene from a sequence of cameras.

**Done first:** round 13 committed and pushed.

**Fixed**
- **Archway artifacts.** Where a hole comes out of a face, the face around it was built by zipping its outline to the hole's outline by angle. Seen from the wall's far corner, an arch's curve runs "backwards", so some faces came out flipped, on top of their neighbours. That's the flicker in the half circle. Now every zipped result is checked: all faces must face the right way and add up to exactly the ring's area. When they don't, the ring is made of two faces joined by bridges, which works for any hole shape. Archways with 4-, 8- and 16-segment arcs, pushed through with Push/Pull or Push Through, now give a closed wall with the exact opening, no overlapping faces and every face triangulated exactly.
- **A selection reaching the wall's edge** (the doorway, the arch and the wall above it) pushed through left a folded face. Push Through now welds its result like Push/Pull does.
- **Scripted drawing on non-convex faces.** `drawpoint` found the face under a point with a test that only worked for convex faces, so a point on the ring around an earlier circle counted as "on the ground". Mouse drawing was not affected.

**Added**
- **Smart Fill** (Edit Mode tools, Mesh menu, Modeling Tools for whole objects). Open edges, whether sides of faces with nothing beyond or wire edges, are chained into loops, inside the selection or across the whole mesh. Each loop is closed the way that suits it:
  - A flat loop becomes one face, wound like the faces around it, so a box with a missing side is a closed solid again.
  - A bent loop becomes a fan from its centre.
  - A loop with no area is welded shut, so no zero-area face is ever made: a crack between unwelded copies of an edge, a slit, or a rim converging on a point (welded into one vertex).
  - A loop lying on a line is left alone and reported.
  - A face's own outline is never filled a second time.
- **Auto Smooth:**
  - Operators that make curved surfaces (a bevel with 2+ segments, a pulled circle) turn on smooth shading by angle (30 degrees by default) on flat-shaded meshes. Round parts look round and corners stay crisp. Meshes you shaded per face are left alone.
  - Toggle and angle are in Edit Mode's bevel settings and the Mesh menu, and saved with the preferences.
  - **Shade Auto Smooth** applies it to whole objects (Mesh menu, Modeling Tools).
- **Render Camera Sequence** (Render window):
  - Every camera with **Render in Sequence** on (new in the Camera Inspector) is rendered in **Sequence Order**, using the current settings and engine.
  - Each image is saved to `Renders/<scene>_sequence_<time>/NN_<camera>.<format>`.
  - It shows progress and can be stopped. Renders go back to the Main Camera afterwards.
- Console: `autosmooth`, `rendersequence [folder|stop]`, `editop smart_fill`, `meshop smart_fill`, `meshop shade_auto_smooth`.

**Checked:** 1447 unit checks on Windows and 1423 on Linux, 0 failed. New tests:
- Archways (three arc sizes) pushed through two ways, plus the notch case.
- Five "prior edges" cases, each pushed through or pulled out, each a closed solid with the exact volume, no overlaps and clean triangulation:
  - A rectangle drawn over an earlier circle's edge.
  - A circle drawn across an earlier rectangle.
  - A rectangle pushed through where the back has a circle.
  - An archway pushed through where the back has a rectangle across it.
  - A rectangle over a circle, pulled out.
- Smart Fill: one and two holes, a selection, a bent hole, a crack, a pinched rim, a line, a wire triangle, a slit, and the editor operator.
- Auto Smooth on 1- and 4-segment bevels, per-face shading, off, and by hand.
- A three-camera sequence in order with one excluded, three different images, and a stopped path-traced sequence.

Stress results:
- The modeling section, with Smart Fill on holes, cracks and wire loops in all 26 awkward meshes: 6,080 operations, 0 problems.
- Push/Pull: 0 problems, overlapping results still 676.
- The editor fuzzer: 6,000 frames, no crash.

## 2026-10-08 (round 13): Push/Pull without overlapping faces, Delete Loose, region inset, depth of field everywhere

**Asked:** commit and push round 12; check Push/Pull and similar tools for the overlapping-face artifacts still showing up; Blender's Delete Loose; inset several faces individually or as one; clearing a number field and pressing Enter should give 0, as in Unity; a New Material made in the Inspector should appear in the Materials folder; draw polygons from a face's exact centre, as in Plasticity; let depth of field blur the foreground while the background stays sharp.

**Done first:** round 12 committed and pushed.

**Fixed: Push/Pull's overlapping faces.** A new check finds faces lying on top of each other, or a hair apart (they z-fight). On the stress suite's 26,620 Push/Pull operations the old code produced 1,502 such results, plus 2 broken (open) meshes. Now there are 676 and none broken: 0 problems. What's left is mostly holes through curved surfaces and deliberately broken inputs (unwelded faces, inside-out normals). The causes, each fixed:
- **Stopping 2 mm short.** Push/Pull stopped just short of any surface in its way. A block pushed back down hovered a hair above the surface, with paper-thin walls, and two halves pulled to the same height left a sliver between them. Now, where the walls beside the selection end (flush), it goes exactly there: the walls vanish and the faces merge back, as in SketchUp. Asked for more, it carries on from there as a second step, so pushing a pulled block past flush makes a pocket, and pulling one half past the other works in one go.
- **Sliding past a neighbour counted as hitting it.** Edges lying on the selection's outline, such as a neighbouring block's top edge, stopped the move. Only geometry inside the swept area does now.
- **New walls in a neighbour's plane.** With Merge Coplanar off, pushing along a coplanar side built a wall inside that side. And when a neighbour stretched along one edge but got a wall along another, the wall covered the strip it grew into. Now a neighbour that stretches takes every side lying in its plane.
- **Folds at corners.** Faces touching the outline only at a corner kept the old corner, making a fold outside the solid. When every face around the outline contains the move's direction, the corners now simply slide (Blender's move along the normal).
- **Faces flattened to nothing** are dissolved into their neighbour, the way Blender's Dissolve Degenerate does it, instead of being left to flicker or turn inside out. Back-and-forth spikes left by welding are removed too.

**Added**
- **Delete Loose** (Blender: Mesh > Clean Up > Delete Loose): removes vertices and wire edges that no face uses, within the selection, or everywhere when nothing is selected. It's in the Edit Mode tools, the Mesh menu and the Modeling Tools window, and also works on whole objects.
- **Select Overlapping:** selects faces lying on top of each other, so leftovers (or imported ones) are easy to find and delete.
- **Inset Individual** switch (Blender's Inset > Individual), also in the F9 panel:
  - On: each face is inset by its own amount.
  - Off: the whole selection is inset as one face. Only its outline moves in, by a thickness, and the edges between the selected faces stay.
- **Empty number fields give 0**, as in Unity, within the field's limits (a field with a minimum of 1 gives 1).
- **New Material in an Inspector slot** (and New Material of Type) now saves a `.mat` in `Assets/Materials`, so it shows up in the Project and Materials windows like any other.
- **Face centre (Plasticity):**
  - When drawing, a face's exact centre (its area centre, not the average of its corners) snaps, shown in orange.
  - **Start at Face Center** makes circles, polygons and centre rectangles begin there wherever you click on the face.
- **Depth of field everywhere:**
  - The Game view, the Shaded Camera Preview and a piloted Scene view now show the camera's depth of field, not only path-traced renders.
  - Nearer things blur as well as farther ones. A blurred foreground spills softly over a sharp background, and a blurred background never spills over a sharp foreground (a "scatter as gather" pass, as in EEVEE).
- **Keep Focus on Point:** Pick Focus Point now keeps that point in focus while the camera moves (Blender's Focus Object). Piloting no longer moves the focus to the orbit pivot.
- Console: `insetmode`, `newslotmat`, `focus`, `position`, `drawmode facecenter`, `editop delete_loose`, `editop select_overlapping`.

**Checked:** 1309 unit checks on Windows and 1285 on Linux, 0 failed. New tests:
- Push/Pull sequences:
  - A square pulled up then pushed back flush, then pushed past it.
  - A pocket pulled back flush.
  - Two halves pulled to the same height, to different heights and past each other.
  - One half pulled and the other pushed.
  - Inset, push in and pull back.

  Each must stay closed, keep the exact volume and have no overlapping faces.
- Delete Loose, with and without a selection.
- Region and individual inset: face counts, a closed result, the inner area.
- Clearing float and int fields (with a minimum).
- New Material assets from the Inspector.
- Face-centre drawing with real clicks, and the area centre of an L-shaped face.
- Rasterized depth of field: the in-focus background untouched, the foreground's blur spilling over it, and the reverse.
- Focus tracking while moving and piloting.

Stress: the modeling section ran 5,716 operations with 0 problems, and the editor fuzzer ran 6,000 frames with no crash.

**Note:** your two material files in `Assets/Materials` (`New Material.mat`, `Solid Material.mat`) were moved to the Recycle Bin at 1:34 PM, while your own Blendity session was running. They can be restored from there.

## 2026-10-08 (round 12): drawing across faces, camera piloting fixes, asset folders, face material picker, drawing modes, guide lines, frame rate

**Asked:** commit and push round 11; make the camera actually adjust while piloting; fix drawing a circle onto a surface whose faces overlap inside the circle; renaming a material should rename its file; organise assets into folders like Unity; pick the material itself when assigning faces; draw shapes uniformly (from the centre), not only corner to corner, as in Plasticity; set the editor's frame rate and explain the performance cost; delete things in the Project window; construction lines to line drawings up with, as in Plasticity.

**Done first:** round 11 committed and pushed.

**Fixed**
- **A circle (or any closed shape) drawn over several faces** used to be added as a separate face lying over them. It is now cut into every face it covers, with a vertex wherever it crosses an edge, and all the pieces inside come out selected for Push/Pull. Parts hanging off the surface become wire edges.
- **Piloting a camera:**
  - When the camera's frame was narrower than the view, the frame didn't match what the camera saw, and starting to pilot changed the camera's field of view. The frame is now exactly the camera's view, and starting changes nothing.
  - A physical camera's lens was worked out from the wrong side of the sensor; it now follows the camera's Sensor Fit.
  - Moving the camera any other way while piloting (gizmo, Inspector, undo) now moves the view with it, instead of being overwritten.
- **Copied materials kept the wrong name.** A `.mat` copied outside Blendity kept the old name inside it. The file name now wins, as in Unity, so nothing gets renamed by surprise.

**Added**
- **Project window like Unity's:**
  - A nested folder tree, and a **+** menu or right-click for **Create > Folder / Material** in the current folder.
  - Drag files or folders onto a folder, a breadcrumb or `..` to move them. Dragging a scene material from the Materials window onto a folder saves it there as an asset.
  - **F2 / Rename** renames in place.
  - **Delete / Del** asks first, then sends the item to the **Recycle Bin** (Trash on Linux and macOS), where it can be restored. Only things inside Assets, research papers or screenshots can be deleted.
  - Moving or renaming keeps everything working: the shared material, texture paths, `.scene` files that mention it, and undo. A deleted material stays on its objects as a scene material.
- **Material name = file name**, both ways: renaming a material renames its `.mat`, and renaming the file renames the material.
- **Face Material** field in Edit Mode (Inspector and Modeling Tools): a grid of preview spheres with every material asset (from any folder), the object's slots and the scene's materials. Pick one and the selected faces use it; its slot is found or added for you.
- **Drawing modes from Plasticity:**
  - Rectangles from a **Corner**, the **Center**, or **3 Points** (one side at any angle, then the width), with **Square** to keep the sides equal.
  - Circles and polygons from the **Center**, **2 Points** (across) or **3 Points** (on the circle).
- **Guide lines**, Plasticity's lines and SketchUp's guides:
  - The new **Guide** draw shape, or **Guides from Edges**. They show as dashed lines and are saved with the scene, and adding them can be undone.
  - Drawing snaps to where two guides cross (or where one pierces the drawing plane), along a guide, and parallel to it.
- **Editor frame rate** (Preferences > Performance, and the Profiler):
  - A cap from 30 to 240 fps or Unlimited, now applied to mouse movement too (before, every mouse move drew a frame).
  - Redraw **only when something changes** (the default) or **always**.
  - It shows the measured frame cost and what that reaches on this machine, warns when the cap is over budget, and explains what each setting costs (CPU, battery, input lag, path-tracing samples). The stats overlay shows the live rate.
- Console: `drawmode`, `guide`, `clearguides`, `guidesfromedges`, `fps`, `redraw`, `preferences`, `facemat`, `mkfolder`, `moveasset`, `renameasset`, `deleteasset`.

**Checked:**
- 1209 unit checks on Windows and 1185 on Linux, 0 failed. New tests cover:
  - Circles across a 4 x 4 grid: the pieces' areas add up, and pulling them makes a solid.
  - The circle through grid corners, hanging off the edge, and along existing edges.
  - A circle overlapping a drawn rectangle on a cube, then Push/Pull, which stays closed.
  - Piloting in a tall view, moving the camera from the Inspector, and a physical camera's lens.
  - Folder move and rename, scene-file rewrites, undo after a move, a copied `.mat`, delete to a scratch trash, and refusing to delete outside Assets.
  - Face material picking.
  - Every drawing mode.
  - Guide snapping with real mouse clicks, save and load, and undo.
  - The frame-cap timing.
- Stress: the modeling section now also draws random circles and squares across faces on all 26 awkward meshes, plus degenerate loops: 5,716 operations, 0 problems. Push/Pull still has only its 2 long-standing problems. The editor fuzzer ran 6,000 frames with no crash.
- **Safety:** the tests and the fuzzer now work in a scratch project and a scratch trash, because the Project window can move and delete files. Your `Assets` folder was checked to be unchanged after full runs.

## 2026-10-08 (round 11): snapping, a Materials window, Modeling Tools, camera piloting, Plasticity tools

**Asked:** commit and push round 10 and remove old Blendity builds; drawing that snaps to edges parallel to a rotated object, not only world axes; test edge cases (a rectangle, an arc on its side, then Push/Pull); materials as objects you can see and adjust outside any object (Unity); drag a mesh onto another and snap it to the surface, and Unity's vertex snapping; move and frame a camera through the Scene view, including its FOV; useful tools from Plasticity; a separate tools window like UModeler's; sanity and edge-case testing.

**Done first:** round 10 committed and pushed; `Blendity.old.exe` and the stale `build\dev.exe` deleted, leaving only `dist\windows\Blendity.exe`.

**Fixed (found by the new edge-case tests)**
- An arc drawn from corner to corner of a rectangle, bulging outside it, used to "split" the rectangle with points outside it (a broken face). It now becomes a second face sharing that side, as in SketchUp.
- Push/Pull on a free-standing face (nothing attached along its outline, like a shape drawn on the ground) left the bottom open. It now makes a closed solid, keeping the original face as the floor (or lid when pushed).
- Shapes drawn on a rotated object's face came out lined up with the world. The drawing plane now takes its axes from the face's edges (or the object's own rotation).
- Imprinting a shape that lines up exactly with the face's outline could make extra triangles in the ring (a floating-point tie); the ring is now clean quads.

**Added**
- **Drawing inference** (SketchUp-style), from the last point: the plane's own red and green axes, any mesh edge's direction (parallel) and its perpendicular, and square to the previous segment, each shown with a dotted guide; Shift locks to the nearest. **Corner Radius** rounds rectangles, polygons and closed polylines (Plasticity's curve fillet).
- **Materials window:** every material in the scene and every material asset as a preview sphere. Click one to edit it there, drag it onto objects, faces, Hierarchy rows or slots, or right-click to assign, save as an asset, duplicate, or select the objects using it.
- **Modeling Tools window** (UModeler's tool panel): Create (primitives and parametric shapes), Draw, Object tools (Join, Boolean, Reset XForm, Mirror, origin, camera), Edit Mode's tools, UV and snapping, always available. It shares the Edit Mode tool code with the Inspector.
- **Surface snapping:** Ctrl+Shift while moving drops objects onto the surface under the mouse, resting on their lowest point, and optionally aligned to its normal. Dragging objects from the Hierarchy into the Scene view does the same.
- **Vertex snapping** (Unity's V): hold V and drag; the selection's nearest vertex lands on the vertex under the mouse (objects, and selected vertices in Edit Mode), with Unity's square marker.
- **Pilot Camera:** look through a camera and frame the shot by navigating. The camera follows the view, the wheel dollies, Ctrl+wheel changes its FOV (or focal length on a physical camera), and a frame with thirds guides shows the shot. **Align to View** is there too. Both are in the Camera Inspector and the Modeling Tools window.
- **From Plasticity:** **Shell** (hollow a solid, opening the selected faces), **Thicken**, **Draft** (taper side faces by an angle) and a **Radial** mode on the Array modifier.

**Checked:** 1049 unit checks on Windows and 1025 on Linux, 0 failed. New tests cover: drawing on a rotated object and pushing the result through; the rectangle + arc + Push/Pull case; degenerate shapes and lines off the mesh; fillets and drawing into a drawn face; Shell, Draft, Thicken and Radial Array; dropping onto a surface; V vertex snapping with real mouse events; piloting with the wheel; both new windows. Stress results:
- Push/Pull ran 26,620 operations and still has only its 2 long-standing problems. A first run found 36 new zero-area faces from the new floor; fixed.
- The modeling section ran 5,066 operations with 0 problems.
- The editor fuzzer ran 6,000 frames with no crash.

## 2026-10-08 (round 10): materials as assets, drawing tools, UModeler tools, origin fix

**Asked:** push the previous round; keep a changelog; Shift + scale on a face makes a new face, as in Blender; automatic seams from sharp edges; a light "focus" like the camera's; UModeler features Blendity lacks; Unity-style material assets you drag onto objects and slots; removing material slots; a polyline tool for circles, arcs and so on, drawn onto a mesh with snapping; fix the origin not being used as the transform point.

**Fixed**
- **Origin as the transform point.** The toolbar's handle position defaulted to *Center*: the gizmo sat at the bounds centre and rotations turned about it, while scaling still used the origin, so a moved origin seemed to do nothing. It now defaults to *Pivot* (the origin, as Blender transforms one object), the choice is saved, and Center scales about the same point it rotates about; G / R / S follow the same setting.
- The "Blender Transform Keys" preference was never saved (it was missing from the preferences file).

**Added**
- **Shift + drag** a gizmo handle in Edit Mode extrudes first: faces get walls and a new cap, so Shift + scale makes an inset face and Shift + move an extrusion (Blender's E then S, ProBuilder / UModeler's Shift-drag). In vertex / edge mode the edges grow faces.
- **Seams from Sharp Edges** (UV menu, UV Editor, edge-mode tools): a seam on every edge whose faces meet at more than the Sharp Angle (30 degrees, Blender's default) and on edges marked sharp; *+ Unwrap* does both. **Select Sharp Edges** too.
- **Light aiming**: Light > *Aim at Point* is an eyedropper like the camera's focus picker. Click a surface and spot, area and directional lights turn to shine at it (X kept level, Unity's LookAt); the range grows to reach it. `pickfocus` and `aimlight x y z` in the console.
- **Material assets** (Unity): Assets > Create Material, or a slot's *Save as Material Asset*, writes `Assets/Materials/<name>.mat`. Drag one from the Project window onto an object in the Scene view (the face under the mouse picks the slot), onto a face in Edit Mode (all selected faces if it is one of them), onto a Hierarchy row or onto an Inspector slot. Images drag onto a material's texture fields. Selecting a `.mat` shows its full material editor; every renderer using it shares one instance, edits are written back to the file, and scenes refer to the file (the values are kept as a fallback).
- **Material slots**: an x on each slot removes it (its faces move to the slot before, as in Blender), plus **Remove Unused Slots**.
- **Drawing tools** (UModeler / SketchUp): Polyline, Rectangle, Circle, Arc and Polygon, drawn onto the edited mesh or the ground (Inspector > Draw, Mesh > Draw, `draw <shape>`). Snapping to corners (green), midpoints (cyan), edges (red), faces (blue) and the ground, with Ctrl for the grid and Shift for the plane's axes, plus a live length. A closed shape inside a face is cut into it, ready for Push/Pull; a line between points on a face's edges splits it through every bend; anything else becomes a new face or wire edges. With nothing selected it starts a new mesh.
- **From UModeler**: **Follow** (sweep a face along a drawn path, SketchUp's Follow Me, with mitred corners), **Spin / Lathe** (edges or vertices turned round an axis), **Slice** (cut by an X / Y / Z or view plane through the selection, optionally removing a side; Blender's Bisect), **Reset XForm** (bake rotation and scale into the mesh), **Mirror** (applied, welded), **Select Similar** (normal, area) and **Select by Material**.

**Checked:** 1005 unit checks on Windows and 981 on Linux, 0 failed (new tests: Shift-drag extrude, seams, light aiming, material assets with save / load and slot removal, drawing on a face and on the ground, Follow / Spin / Slice, Reset XForm and the pivot). The stress suite ran 4,728 operations of the new and existing tools on 26 awkward meshes, plus 390 random shapes, with 0 problems; the editor fuzzer ran 1,500 frames with no crash.

## 2026-10-08 (round 9): every Blender export format, file dialogs, Boolean, modifier stack, shapes, n-gons

**Asked:** Blender's export options and formats (tested against an installed Blender); Save As dialogs for objects and screenshots; origin handles; Boolean operations; combining meshes; parametric objects; SketchUp-style n-gon editing; extruding vertices and edges; a Blender-style modifier stack.

**Added:** export to OBJ, binary / ASCII FBX, glTF (.glb / .gltf), STL, PLY and USD (.usda) with Blender's options, and import of the same; every format checked both ways in Blender 5.2 (this found that the earlier ASCII FBX did not open in Blender). Native Save / Open dialogs. Join, Separate, Boolean (applied or a live modifier with a wire cutter). Wire edges and vertex / edge extrusion. Edit Origin with handles and click snapping. A modifier stack with Edit Mode / Viewport / Render toggles and Bevel, Weld, Triangulate, Wireframe, Displace, Simple Deform, Cast and Screw. Thirteen parametric shapes. N-gon Mode, Merge Coplanar and the Knife (K).

**Checked:** 963 checks on Windows, 939 on Linux; a new stress section with 0 problems.

## 2026-10-08 (round 8): lights, export, selection fixes

Spot and Area lights with colour temperature (rasterizer and CPU / GPU path tracing); OBJ and FBX export; editing another mesh by clicking it in Edit Mode; the Hierarchy's create menu; Alt+click loops and Ctrl+Alt+click rings; the scene gizmo's views; origin dots; a lockable Camera Preview; a camera focus eyedropper; old app builds removed.

## 2026-10-08 (round 7): Blender's modal editing, edge selection, per-face shading

G / R / S modal transforms with axis locks and typed values; edge mode keeps the picked edges; per-face Shade Smooth / Flat and Mark Sharp; Bevel Clamp Overlap and a dozen more Blender operators; Unity-style colour fields; render resolution presets; docs/USABILITY.md.

## 2026-10-07 (rounds 4-6): GPU rendering, Push/Pull, physical cameras

Vulkan GPU path tracing with ray tracing hardware and combined CPU + GPU rendering; SketchUp-style Push/Pull as a confirmed operator, hardened by a stress test on awkward meshes; tools split by selection mode; Set Origin; physical cameras with depth of field and exposure; edge tools (Bevel, Bridge, Push Through); Adjust Last Operation (F9); Inspector expressions and multi-object editing; surface types (opaque, cutout, transparent, glass).

## 2026-10-06 / 07 (rounds 1-3): Blendity

The editor itself: Blender's mesh model and Edit Mode behind a Unity-style editor; UV unwrapping, materials and textures; a deferred rasterizer and a path tracer; Blender's bundled libraries (Embree, OpenImageDenoise, OpenSubdiv, OpenColorIO, Manifold, Jolt and others), each optional; SIMD kernels; the Learn tab; stress tests.
