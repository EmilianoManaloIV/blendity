# Task 0295 - The bridges tested in the engines (round 315)

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

## Unreal 5.8 - waiting on the machine
- `RunUAT BuildPlugin` reached the plugin's rules and stopped: Unreal's build tool needs the **.NET Framework SDK 4.6
  or newer** (Visual Studio Installer > Build Tools > Individual components > ".NET Framework 4.8.1 SDK"), which this
  machine lacks.
- It also showed that **Unreal 5.8 refuses C++17**. The plugin's `Build.cs` asked for it, so it would not have
  compiled; it now takes the engine's own standard. The protocol layer is standard C++ and still builds in
  Blendity's suite.
- Once the SDK is installed, the plugin build, the automation test `Blendity.LiveLink.Replay` and a live connect are
  run.

## Godot - not installed yet
`bridges/godot/smoke_test.gd` and `auto_connect_test.gd` (`godot --headless --script`) run once it is.
