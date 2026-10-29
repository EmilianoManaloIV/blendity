# Task 0295 - The bridges tested in the engines (rounds 315-318) - done

The user installed Unity, Unreal and Godot so the Live Link bridges (tasks 0274-0278) could be run in them (2026-10-29:
"Feel free to do testing as needed with these applications when they are fully installed").

## Unity 6000.6.7f1 - run, two faults fixed
- A scratch project, the package added from the repository as a local `file:` package and marked testable, the
  EditMode tests in batch mode:
  - `RecordedSessionLeavesTheScene` passes: the recorded session replays through the package and leaves the scene
    it should.
  - `ConnectsByItselfAndAppliesTheSession` (new) passes: with `tools/livelink_fake_server.py` serving and the
    discovery file under `BLENDITY_PREFS_DIR`, the package connects by itself, says hello (`{"t":"client","app":"Unity
    6000.6.7f1","bridge":"1.1"}`) and builds the session's objects.
  - The package imports with no warning, and its `.meta` files check clean.
- **The faults the run found:**
  - `TryAutoConnect()` called while the link was connected or connecting dropped it and started over (`Connect`
    disconnects first); it now returns at once when busy.
  - Opening another scene took the linked objects with the old scene while the client kept applying changes to
    destroyed objects. The link now starts again on a scene opened or created, so Blendity's first sync builds them
    in the new scene.

## Unreal 5.8.4 - built and run (rounds 315 and 317)
- **Round 315:** `RunUAT BuildPlugin` reached the plugin's rules and stopped. Unreal's build tool needs the
  **.NET Framework SDK 4.6 or newer**, which this machine lacked. It also showed that **Unreal 5.8 refuses C++17**:
  the plugin's `Build.cs` asked for it, and now takes the engine's own standard.
- **Round 317:** the user installed the .NET Framework 4.8.1 SDK and its targeting pack.
  - BuildPlugin succeeds into a short package path: Unreal refuses paths over 260 characters.
  - `Blendity.LiveLink.Replay` passes with no warning, in a scratch project with the plugin and the recorded session
    in its `Resources`.
  - The editor, run headless on that project, connected by itself to `tools/livelink_fake_server.py` through the
    discovery file within 12 s, saying hello as "Unreal 5.8.4".
- **The faults the real build found:**
  - `B[0]` on the protocol's `Json` was ambiguous to MSVC, since a literal 0 is also a null `const char *`. Indexing
    is now one template over integer types, so `int64`, `uint32` and the rest are unambiguous too, and a negative
    index is past the end. Blendity's suite pins it: it compiles only the protocol layer, which never indexed by a
    literal.
  - Every linked mesh warned that collision couldn't read its triangles. The meshes now keep their CPU-side data
    (`bAllowCpuAccess`), so traces and clicks in Play work on them, and they no longer mark a package dirty.
  - The replay test's editor world had no world context, so deleting an actor warned. It now has one.
- **Known cost:** a deform message rebuilds the whole mesh. With CPU access, each rebuild also keeps a CPU copy and
  cooks collision on the game thread. A big deforming mesh (a skinned character of 100k triangles streaming its pose)
  may slow the Unreal editor. This is not measured yet; the follow-up is to measure one deform of a large mesh and,
  if it is costly, leave collision off while deforms stream.

## Godot 4.7.2 - run (round 316)
- `smoke_test.gd` and `auto_connect_test.gd` pass headless. The editor loads the enabled add-on with no error.
- **Live:** the add-on connected by itself to a running Blendity (a windowed check with scratch preferences) and built
  its sample scene.
- **The test faults the run found:**
  - The smoke test read the camera's `global_transform` before the scene entered the tree; it now builds the
    transform from the parents.
  - The auto-connect test stopped with a script error when no stand-in started, which happened with the Microsoft
    Store's `python` stub. It now fails with its checks.
- Godot 4.4+ writes `.uid` files: they are committed and packaged, and `.godot/` is ignored.

## Blender 5.2.2 - run again (round 316)
Both tests pass. The add-on connected live to a running Blendity in the same way. The Install a Bridge test, which
skips without a Blender, passes against it in a scratch user folder.

## Round 318 - live sessions, Install a Bridge, Unreal's deforms
- **Live:** each bridge followed a running Blendity through a new test pair: `tests_round318` in Blendity's suite
  (`BLENDITY_TEST_BRIDGE`) and a live-session test per engine. It covers a Box moved, recolored and deformed, a bone
  turned, the Sphere deleted, an object made, and a 32,514-vertex sphere deformed ten times. All four were LIVE OK.
  The longest frame or tick while the sphere deformed:
  - Blender 4.3 ms;
  - Godot 42.6 ms;
  - Unity 33.3 ms;
  - Unreal 145 ms (259 before).
- **Unreal faults found and fixed:**
  - A quiet link was taken for a closed one, which reconnected and re-synced every 2 seconds.
  - A deform rebuilt the whole mesh description and cooked collision. It now changes positions in the kept
    description, with collision back once the deforms stop.
  - A deforming mesh's collision setup asked for triangles it hadn't kept.
- **Install a Bridge:** a fresh Godot, Unity and Unreal project, each set up by Blendity's installer, opened with no
  error (the Unreal one built first).
- **Left:** Unreal's own static-mesh build is about 75 ms of a deform of a 32,000-vertex mesh. A dynamic mesh component
  would remove it.
