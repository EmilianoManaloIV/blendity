# 0275: the Blender and Unity bridges connect by themselves

- **Status:** done (round 295)
- **Requirements:** R-10
- **Decisions:** ADR 0086
- **Model:** main session
- **PR:** #229

## Asked
Task 0274's request, for the two bridges that existed: no port typed, no Connect pressed, and the link found again
after Blendity closes or restarts.

## What changed
- **Blender** (`bridges/blender/blendity_livelink`, 1.1.0): add-on preferences Auto Connect (on) and a port (0: the
  discovery file's); a persistent timer reads the file every 2 seconds while not connected and connects; after
  Blendity closes, "waiting for it again"; Disconnect holds it off until Connect. The status says "Connected to
  Blendity (SampleScene)". The hello names Blender's version. `discovery.py` has no bpy, for the tests.
- **Neither waits on its program's frame** (the review): a connection starts without blocking and later updates finish
  it (Blender: a non-blocking socket checked with `select`; Unity: `BeginConnect` finished on update), so a stale file
  after a crash costs nothing - the longest look at a dead port in Blender measured 0.6 ms, against 500 ms before.
- **Unity** (`com.blendity.livelink`, 1.1.0): the same on `EditorApplication.update` (EditorPrefs Auto Connect, a
  manual port; SessionState holds Disconnect through reloads); `LiveLinkDiscovery.cs` has no UnityEngine. The package
  has its `.meta` files now (`tools/unity_meta.py` writes them with stable GUIDs, `--check` in CI), so it imports from
  a .tgz, a git URL or a .unitypackage, where Unity refuses assets without them.

## Tests
- `bridges/blender/auto_connect_test.py` in Blender 5.2.2, headless, against the stand-in: waiting without a file;
  connected with no port typed; the session applied (32 messages, 9 objects); the drop seen; a stale file looked at for 3 s without waiting; found again on a new
  port after a restart; the objects adopted, not duplicated (9 objects, 9 ids, 9 followed); the hello "Blender 5.2.2
  LTS". The smoke test still passes (32 messages, 9 objects).
- `ReplayCheck.exe --live <python> <repo>` (the .NET Framework's csc, C# 5): no file and another protocol's file
  ignored; found through the file; the session followed (32 messages); found again after the restart on a new port; the
  hello right. The replay itself still passes.

## Not done
- Unity's own EditMode test and a live connect in the Unity Editor: after the user installs Unity.
