# Live Link: blendity-livelink/1

Live Link sends the scene open in Blendity to other programs on the same computer as it changes: a bridge in
Blender, Unity, Godot or Unreal connects, gets the whole scene, and then every change as it happens. It is one way:
Blendity is the source, and nothing a client sends is read but its hello. Start it with Window > Live Link or the console:

```
livelink start [port]    (the default port is 40100; 0 picks any free one)
livelink status
livelink stop
```

Decisions: [ADR 0055](decisions/0055-live-link.md). Code: `src/core/net_server.*` (the transport),
`src/editor/livelink.*` (what is sent), `tests/test_livelink.cpp`, the stress section `livelink`.

## Finding Blendity: the discovery file

Live Link starts with Blendity (Preferences > Live Link > Start with Blendity; on by default). While it listens,
Blendity writes `~/.blendity/livelink.json` (beside its preferences; `BLENDITY_PREFS_DIR` moves both):

```
{"protocol":"blendity-livelink/1","port":40100,"pid":1234,"app":"Blendity","version":"0.11.0","scene":"SampleScene","started":1793000000}
```

and removes it when Live Link stops or Blendity ends (only its own: another Blendity started since owns the file). A
bridge with Auto Connect on reads it every 2 seconds while not connected and connects to `port`; a refused connection
(a stale file after a crash) is just a failed try. Port 40100 taken: Blendity uses the next free of 40100-40109.
Decisions: [ADR 0086](decisions/0086-livelink-discovery.md). `tools/livelink_fake_server.py` is a stand-in that
serves the recorded session and writes the file, for a bridge's tests.

A bridge may send one line first, its **hello**: `{"t":"client","app":"Unity 6000.0","bridge":"1.1"}`. Blendity's
Live Link window lists the client by it. Nothing else a client sends is read.

## The connection

- TCP on 127.0.0.1 only: no other computer can connect, and there is no authentication.
- The stream is a sequence of messages. Each message is one line of UTF-8 JSON (one object) ended by `\n`.
- A message with an attachment has `"bin": N` as its last member; exactly N bytes follow the newline, then the next
  line. Read the line, look for `bin`, read N bytes.
- Unknown message types and unknown members are ignored by a client: later versions add, never change.

## Axes and units

Everything is in **glTF's axes**: right-handed, +Y up, metres. Blendity itself is Unity's left-handed Y up; its X
axis is mirrored on the way out, exactly as its glTF exporter writes files:

| | Blendity | sent |
|---|---|---|
| position | (x, y, z) | (-x, y, z) |
| rotation (quaternion x, y, z, w) | (x, y, z, w) | (x, -y, -z, w) |
| scale | (x, y, z) | (x, y, z) |
| mesh vertices | (x, y, z) | (-x, y, z), polygon winding reversed |

An object's forward is +Z in both (Unity's convention, kept by the mirror). glTF's own cameras and lights look down
-Z, so a bridge that builds them the glTF way turns them 180 degrees about Y. A Unity bridge mirrors X back and gets
Unity's values unchanged; a Blender bridge converts from glTF's axes as Blender's glTF importer does (Y up to Z up).

Transforms are **local** (relative to the parent named in the object's message).

## Messages

Every message has `t`, its type. Objects are named by `id`: a number that stays the same for the object's life,
through undo and Play (Blendity's stable object ids).

### hello
`{"t":"hello","protocol":"blendity-livelink/1","app":"Blendity","axes":"gltf","units":"m"}`. The first message.

### sync ... sync_end
`{"t":"sync","scene":"SampleScene","objects":12,"playing":false}`, then the whole scene - every `material` before the
objects that use it, every `object` with parents before children, each light's and camera's message after its object
- then `{"t":"sync_end"}`. The meshes follow as `mesh` messages, possibly after `sync_end` (they are built on a worker
thread). A client that connects later gets its own sync; the others are not sent it again.

### object
```
{"t":"object","id":7,"name":"Cube","parent":0,"kind":"mesh","active":true,
 "position":[-1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1],"materials":[1,2]}
```
- `parent`: the parent's id, 0 for a root.
- `kind`: `mesh`, `light`, `camera`, `armature` or `empty`.
- `bone_of`: present on a bone, the id of its armature's object.
- `materials`: on objects with a renderer, the material key of each slot, as the renderer draws it (an empty slot is
  the default material, sent like any other).
Sent when an object appears, and again whole when its name, parent, kind, active state or material slots change.

### transform
`{"t":"transform","id":7,"position":[...],"rotation":[...],"scale":[...]}`: a non-bone object's local transform.

### pose
`{"t":"pose","id":3,"bones":[[id,px,py,pz,qx,qy,qz,qw,sx,sy,sz],...]}`: every bone of the armature object `id`, with
its local transform, when any of them moved. The whole rig each time, so that a newer pose can replace an older one
still waiting to be sent.

### mesh
`{"t":"mesh","id":7,"topology":42,"verts":24,"bin":N}` + a glTF 2.0 binary (.glb) of the object's mesh in its own
space, modifiers applied (what the viewport shows). One glTF mesh with one primitive per material slot that has faces;
each primitive's `extras.slot` names the slot (index into the object's `materials`). Attributes `POSITION`, `NORMAL`,
`TEXCOORD_0` and `_VERT`: for each of the binary's vertices, the index (a float) of the mesh vertex it came from - the
binary splits a mesh vertex where its faces' normals or UVs differ. `topology` is an id of the mesh's faces, UVs and
materials; `verts` the mesh's vertex count. The binary has no materials: they come as `material` messages. A `mesh`
for an id the client doesn't know is ignored.

### deform
`{"t":"deform","id":7,"topology":42,"verts":24,"bin":N}` + N = 12 x verts bytes: float32 x, y, z per mesh vertex
(little-endian, in the object's space and glTF's axes). Sent instead of a whole mesh when only vertices moved (a drag
in Edit Mode, skinning, a deformer, physics). Apply it only when `topology` and `verts` match the last `mesh`; move
each of the binary's vertices to the position of its `_VERT`, then recompute normals. A deform whose topology doesn't
match is dropped (a newer `mesh` is on its way, and a `deform` follows it when the positions moved meanwhile).

### material
```
{"t":"material","key":1,"name":"Skin","gltf":{...a glTF material object...},
 "maps":{"Base Map":{"path":"C:/project/Assets/Textures/skin.png","non_color":false},...}}
```
`key` names the material in objects' `materials`; the same material shared by many objects is one key. `gltf` is
the material as Blendity's glTF exporter writes it (base colour and alpha, metallic, roughness, emission with
`KHR_materials_emissive_strength`, alpha mode, double-sided), without textures; a Toon material adds the extension
`BLENDITY_toon`: every field of the material by its Inspector name (Toon Bands, Shadow Color, ...). `maps` are the
material's texture slots by Inspector name as absolute paths on this computer. Sent when a material is first used and
whenever any of its fields changes.

### light
`{"t":"light","id":5,"type":"directional|point|spot|area","color":[r,g,b],"intensity":1,"range":10,"spot_angle":30,
"inner_spot_angle":21.8,"area":[w,h],"disc":false,"fields":{...}}`. `color` includes the colour temperature; `fields`
are all of the Light component's fields by name. Units are Unity's (intensity is a multiplier).

### camera
`{"t":"camera","id":6,"fov":60,"near":0.3,"far":1000,"orthographic":false,"ortho_size":5,"physical":false,
"focal_length":50,"sensor":[36,24],"fields":{...}}`. `fov` is vertical, in degrees.

### delete
`{"t":"delete","id":7}`: the object is gone. Every deleted object gets its own message, in no particular order (a
parent may come before its children); a delete for an id the client no longer has is ignored.

### play
`{"t":"play","playing":true}`: Play mode started or stopped. In Play the objects move as the game runs; Stop puts the
scene back as it was, and the changes back go out as ordinary messages.

## Timing and what can be skipped

The editor compares the scene with what it has sent at most 60 times a second, and only while a client is connected.
Each client has its own queue on its own thread, so a slow client never slows the editor. A `transform`, `pose`,
`deform`, `light`, `camera` or `material` waiting in a client's queue is replaced by a newer one about the same
thing, so a client that falls behind skips to the latest state instead of replaying every step; messages without such
a key (`object`, `mesh`, `delete`, `sync`, `play`) are never replaced, and nothing queued after one of them replaces anything
before it. A client whose queue grows past 256 MB is closed.

## Testing a bridge live (round 318)

Each bridge has a live-session test that follows a running Blendity, not a recording. Blendity's own suite drives it:
`tests_round318` in `tests/test_livelink.cpp` runs the command `BLENDITY_TEST_BRIDGE` names (skipped without it).

1. Blendity starts Live Link with its discovery file in the test's own folder, never `~/.blendity`, and runs the command.
2. The bridge connects by itself. Once it has the Box, it writes `$BLENDITY_BRIDGE_DUMP.ready`.
3. Blendity then makes its edits as changes, not as a first sync:
   - it moves, recolors and deforms the Box;
   - it turns a bone and deletes the Sphere;
   - it makes "Late" at (0, 5, 0);
   - it deforms "Dense", a sphere of 32,514 vertices, ten times, the last to radius 1.2.
4. It stops Live Link. When the discovery file goes, the bridge checks its scene, writes its report to
   `$BLENDITY_BRIDGE_DUMP` ("LIVE OK" or "LIVE FAILED") and exits.

| Bridge | The command | Run (2026-10-29) | The longest frame or tick while Dense deformed |
|---|---|---|---|
| Blender 5.2.2 | `blender -b --factory-startup --python-exit-code 1 -P bridges/blender/live_session_test.py` | LIVE OK | 4.3 ms |
| Godot 4.7.2 | `godot --headless --path bridges/godot --script res://live_session_test.gd` | LIVE OK | 42.6 ms |
| Unity 6000.6.7f1 | `Unity -batchmode -projectPath <project> -runTests -testPlatform EditMode -testFilter Blendity.LiveLink.Tests.LiveSessionTests` | LIVE OK | 33.3 ms |
| Unreal 5.8.4 | `UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Blendity.LiveLink.LiveSession; Quit" -unattended -nullrhi` | LIVE OK | 145 ms (259 before round 318) |

**Unreal needs a short test folder.** The suite points each child's home at its test folder. Unreal stops when its
shared data cache's path is over 119 characters, so run it with a short `BLENDITY_TEST_DIR` (`C:\uebl\t`).

**Install a Bridge, then open (round 318).** Each engine opened a fresh project that Blendity's Install a Bridge had
set up, with no error:
- Godot: the add-on was enabled.
- Unity: the package compiled.
- Unreal: the plugin was built for the project and its replay test passed.
- Blender: covered by the suite's install test.

## A minimal client

`tools/livelink_dump.py` connects and prints every message (Python 3, no packages):

```
py tools/livelink_dump.py [port]
```
