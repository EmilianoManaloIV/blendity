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
{"protocol":"blendity-livelink/1","port":40100,"pid":1234,"app":"Blendity","version":"0.11.0","scene":"SampleScene","inbox":"C:/Projects/Game/Incoming","started":1793000000}
```

and removes it when Live Link stops or Blendity ends (only its own: another Blendity started since owns the file). A
bridge with Auto Connect on reads it every 2 seconds while not connected and connects to `port`; a refused connection
(a stale file after a crash) is just a failed try. Port 40100 taken: Blendity uses the next free of 40100-40109.
Decisions: [ADR 0086](decisions/0086-livelink-discovery.md). `tools/livelink_fake_server.py` is a stand-in that
serves the recorded session and writes the file, for a bridge's tests.

A bridge may send one line first, its **hello**: `{"t":"client","app":"Unity 6000.0","bridge":"1.1","caps":["skins"]}`
(`caps` since round 378: "skins" - it skins a rig's mesh itself, so Blendity sends it no deforms of one). Blendity's
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
- `static`: present (true) on a mesh whose renderer has Contribute GI on (round 381).
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

**A rig's mesh (round 378).** A mesh posed only by its Skin (normal-only modifiers aside), or carrying shape keys, goes
at rest - before the Skin, its keys unapplied. Its Skin must deform as the engines' skins do: a linear blend (not
Preserve Volume), posed (not Rest Position), at most four bones a vertex; otherwise its vertices go as any mesh's.
- **The binary** carries its skin (`JOINTS_0`, `WEIGHTS_0`, a `skins` entry whose inverse bind matrices are in the
  mesh's own space) and its keys as morph targets.
- **The message** adds `"skin":{"armature":5,"joints":[5,6,7]}`, where each joint is the object of that index (the
  bones Live Link already sends), and `"shapes":["Smile",...]`, the keys' names in the targets' order.
- **Then** it moves by its bones' `pose` messages and a `weights` message (a mesh with keys only). The rest mesh goes
  again only when it changes, or its rig does: the armature, a joint's rest, name or parent, which bones deform, the
  Skin's settings, the mesh's place from the armature. A rename or a material doesn't send it.
- **A rig no more** (its armature deleted, Preserve Volume on, another modifier added) goes as a plain `mesh` with no
  `skin` or `shapes`: a client takes its skin off then.
- **A client that didn't say `"skins"`** still gets the posed vertices as deforms, which map onto the same vertices.

### weights (round 378)
`{"t":"weights","id":7,"weights":[0.25,...]}`: a keyed mesh's shape weights (0 to 1), in its `mesh` message's `shapes`
order. Sent at a sync and whenever they change. It can come before the mesh it weights: keep it, and set it when the
mesh comes.

### clip (round 379)
`{"t":"clip","id":5,"name":"Wave","asset":"Assets/Animations/Wave.anim","fps":60,"frames":61,"loop":true,"tracks":[6,7],"bin":N}`:
the clip of object `id`'s Animation component, baked.
- **The binary:** for each object in `tracks`, for each frame, its local position (3), rotation (4) and scale (3) as
  float32 - 40 bytes, in the axes a `transform` carries. Frame i is at i / fps seconds.
- **Baked from** the clip's Position, Rotation and Scale tracks; an axis the clip doesn't key keeps the object's own
  value. Other tracks (shape keys, components, materials) aren't in it.
- **Sent** at a sync (after its `sync_end`: baked on a worker, after every object it names) and when it changes - a
  key set, moved or deleted, another clip chosen, an object it names renamed or made, an axis it doesn't key moved - at
  most 4 times a second while it keeps changing, its last state after. A bake over 32 MB isn't sent (Blendity's log
  and the Live Link window say so).
- **The Animation component's** Speed, Wrap (beyond `loop`) and Hold (on 2s) aren't in it.
- **A client keeps it** as its engine's clip, one per `asset` (found again after a reconnect); Live Link goes on posing
  the objects itself.

### collider (round 380)
`{"t":"collider","id":7,"shape":"box","center":[-0.1,0,0],"size":[1,2,1],"radius":0.5,"height":2}`: the object's
Collider - `box`, `sphere`, `capsule` (up Y, `height` end to end), `convex` or `mesh` (the object's own mesh, which the
client has; build it again when the mesh comes, and when its deforms have stopped for half a second) - its centre in
glTF's axes, sizes in metres; `"shape":"none"` when it is taken off or turned off. Sent at a sync, when it changes, and
again when the object's kind changes (a client makes the object again then).

### lod and lodmesh (round 380)
`{"t":"lod","id":7,"screen":[0.5,0.25,0.1],"ratio":[1,0.5,0.25],"cull":false}`: the object's LOD Group. Level i shows
while the object stands at least `screen[i]` of the view's height (the first that fits); below the last, the last
level, or nothing when `cull`. `"screen":[]` when the group goes. A mesh that deforms every frame (a skin, shape
weights) has none.

`{"t":"lodmesh","id":7,"level":1,"topology":42,"verts":120,"bin":N}`: level 1 or more, a glTF binary like `mesh`'s, the
mesh decimated to its ratio on Blendity's mesh worker. Sent at a sync, when the level count or a ratio changes (a
screen size or Cull alone sends only `lod`), and when the mesh has changed and then stood still for half a second (a
stroke doesn't decimate every tick). A level the latest `lod` doesn't have is ignored: one built before the group
shrank or went may still arrive.

### avatar and springs (round 381)
```
{"t":"avatar","id":5,"root":3,"view":[0,1.5,0.05],"visemes":{"mesh":9,"shapes":["vrc.v_sil",...]},
 "blink":{"mesh":9,"shapes":["vrc.blink_left","vrc.blink_right","vrc.lowerlid_left","vrc.lowerlid_right"]},
 "eyes":{"left":12,"right":13}}
```
An armature's VRChat setup:
- **When an armature has one:** when it has LeftEye and RightEye bones, a mesh skinned to it with vrc.v_* keys, or a
  humanoid Head.
- **`root`:** the avatar root - the nearest object above the armature and every mesh skinned to it (VRChat's descriptor
  goes there, the meshes in its hierarchy); the armature itself when there is none (Blendity's log says to parent them
  under one object).
- **`view`:** where VRChat's view sits, in the root's space and glTF's axes - between the eyes at rest, else a little
  above the Head.
- **`visemes` and `blink`:** the mesh skinned to the armature carrying the most of those keys (`mesh` 0: none), with
  VRChat's names in its order.
- **`eyes`:** the eye bones' ids (0: none).
- **No longer an avatar:** `{"t":"avatar","id":5,"none":true}`.

`{"t":"springs","id":5,"chains":[{"root":7,"label":0,"stiffness":0.4,"damping":0.15,"gravity":1,"radius":0.02,"bend":0}]}`:
- each spring chain (a SpringBone on its first bone) under the armature, not under an armature inside it (a hair rig
  has its own);
- `"chains":[]` when the last goes.

Both are sent at a sync (after every object) and when they change; an armature that stops being one (its Armature taken
off) sends `none` and `"chains":[]`. An armature is looked at once a second, a few a frame.

### What it costs the editor (round 382)
- **Messages built on Live Link's workers:** meshes, level meshes, textures and clips. The main thread copies what
  changed and sends the small messages.
- **Measured** on a Windows release build with four clients:
  - with 200 objects, a 100,000-vertex mesh deformed, a 50-bone rig posed and a clip keyed every frame, the tick is
    1.1 to 1.9 ms on average and at most 3.2 ms;
  - a topology change of that mesh costs about 4 ms;
  - painting a surface dab after dab costs 0.1 ms on average and 2.8 ms at most, its texture sent about 4 times a
    second.
- **Jobs on workers** (a skin bind, a bake, a render) leave the tick going every frame (`tests_round382`). Work done
  on the main thread, such as an import's busy step, holds it like everything else.

### deform
`{"t":"deform","id":7,"topology":42,"verts":24,"bin":N}` + N = 12 x verts bytes: float32 x, y, z per mesh vertex
(little-endian, in the object's space and glTF's axes). Sent instead of a whole mesh when only vertices moved (a drag
in Edit Mode, skinning, a deformer, physics). Apply it only when `topology` and `verts` match the last `mesh`; move
each of the binary's vertices to the position of its `_VERT`, then recompute normals. A deform whose topology doesn't
match is dropped (a newer `mesh` is on its way, and a `deform` follows it when the positions moved meanwhile).

### material
```
{"t":"material","key":1,"name":"Skin","gltf":{...a glTF material object...},
 "maps":{"Base Map":{"path":"C:/project/Assets/Textures/skin.png","non_color":false,"texture":3},...}}
```
`key` names the material in objects' `materials`; the same material shared by many objects is one key. `gltf` is
the material as Blendity's glTF exporter writes it (base colour and alpha, metallic, roughness, emission with
`KHR_materials_emissive_strength`, alpha mode, double-sided), without textures; a Toon material adds the extension
`BLENDITY_toon`: every field of the material by its Inspector name (Toon Bands, Shadow Color, ...). `maps` are the
material's texture slots by Inspector name: an absolute path on this computer, and since round 377 `texture`, the key
of the `texture` message carrying its pixels (use those: a painted, generated or procedural texture has no file, and a
file's own may be changing). Sent when a material is first used and whenever any of its fields changes.

### texture (round 377)
`{"t":"texture","key":3,"name":"skin.png","width":1024,"height":1024,"non_color":false,"hash":"9f2c...","bin":N}`
followed by N bytes of a PNG.
- **What it carries:** the pixels of the texture a material's maps name by `key`, whatever made them: an image file,
  a generated image, a procedural texture's output, or a painted surface. A texture larger than 2048 on a side goes at
  2048, every n-th texel.
- **Keys:** the same path in the same colour space is one key across every material.
- **The PNG:** RGBA, 8 bits, sRGB for colour data (a float image's linear values made sRGB too) and the values as
  they are for non-colour data (`non_color`).
- **When it's sent:** at a sync, and whenever the pixels change.
  - A painted surface goes once each stroke ends, when its outputs are made again.
  - An image that keeps changing, such as a procedural texture's field being dragged, goes at most 4 times a second,
    with its last state after.
  - Pixels looked at again but unchanged aren't sent; `hash` is the content's.
  - Only the textures some material names now are watched and sent.
  - It's encoded off Blendity's main thread, so it may arrive after the material naming it. Apply the material then,
    and again when its texture arrives.

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

### world (round 376)
```
{"t":"world","mode":"gradient|sky|hdri|color","sky":[r,g,b],"equator":[r,g,b],"ground":[r,g,b],"color":[r,g,b],
 "hdri":"C:/project/Assets/Textures/sky.hdr","strength":1,"rotation":0,"turbidity":3,
 "fog":{"enabled":true,"start":10,"end":60,"color":[r,g,b],"color_far":[r,g,b]},"fields":{...}}
```
- **The world** (Window > Lighting > Environment):
  - the Gradient's three colours (Unity's Trilight);
  - the Sky's turbidity;
  - an HDRI as a path on this computer, with its rotation in degrees about Y;
  - a plain colour;
  - its strength.
- **Fog:** distance fog from `start` to `end` metres, toward `color` (and `color_far`).
- **`fields`:** every field by its Inspector name.
- **When it's sent:** after `sync`, before the objects, and whenever any of it changes.

### render (round 376)
`{"t":"render","exposure":0,"view_transform":"Filmic"}`: the render's exposure in stops and its view transform by
name ("Standard", "Filmic", "ACES", or an OpenColorIO view's). Sent at the sync and when either changes.

### filters (round 376)
`{"t":"filters","id":6,"effects":[{"type":"Bloom","enabled":true,"fields":{"Intensity":0.6,...}},...]}`: the render
camera's Camera Filters, top to bottom, each with its fields by Inspector name.
- **The render camera** is the one the Game view and F12 use: the scene's Active Camera, else "Main Camera", else
  the first camera. `id` names it.
- **Only that camera's stack is the look,** since each engine has one post-processing of its own. Another camera's
  stack isn't sent.
- **When it's sent:**
  - at every sync;
  - when the stack changes;
  - when another camera becomes the render camera;
  - when the camera is deleted.
- **`effects` is empty** when the camera has no stack. `id` is 0 when there is no camera. A reconnecting engine
  therefore never keeps an old one.

**What each bridge does with the look:**

| | Blender | Unity | Godot | Unreal |
|---|---|---|---|---|
| Gradient | a ColorRamp on the world's Z | Trilight ambient | a procedural sky | a SkyLight captured from the level, tinted; the ground as its lower hemisphere |
| Sky, HDRI | the Hosek-Wilkie sky; an Environment Texture | the procedural skybox; a panoramic one (PNG/JPG) | a physical sky; a panorama | the SkyLight (no HDRI) |
| Fog | the mist pass mixed in by the compositor | linear fog | depth fog | ExponentialHeightFog (approximate) |
| Exposure, view transform | the scene's | a URP Volume (kept on Blendity Look without URP) | tonemap exposure and mode | the exposure bias (Unreal's tone mapper kept) |
| Bloom, Vignette, Color Grading | the compositor | the URP Volume | glow and adjustments (no vignette) | an unbound PostProcessVolume |
| The scene's own | its world and compositor kept (a fake user), back when the link stops | RenderSettings kept on Blendity Look: Restore Scene Lighting | a second WorldEnvironment is warned about | look actors adopted on reconnect |

**Colours:** Live Link's are linear. Godot's and Unity's RenderSettings colours are sRGB, so those bridges convert;
Blender's and Unreal's are linear.

## Timing and what can be skipped

The editor compares the scene with what it has sent at most 60 times a second, and only while a client is connected.
Each client has its own queue on its own thread, so a slow client never slows the editor. A `transform`, `pose`,
`deform`, `light`, `camera` or `material` waiting in a client's queue is replaced by a newer one about the same
thing, so a client that falls behind skips to the latest state instead of replaying every step; messages without such
a key (`object`, `mesh`, `delete`, `sync`, `play`) are never replaced, and nothing queued after one of them replaces anything
before it. A client whose queue grows past 256 MB is closed.

## Send to Blendity: snapshots (round 374)

The stream stays one-way. The other direction is a file: a bridge's **Send to Blendity** writes a snapshot of its scene
into Blendity's inbox, and Blendity imports it.

- **The inbox:** the discovery file names it, `"inbox": "<project>/Incoming"`. Blendity looks there about once a
  second while Live Link listens (`livelink inbox` looks now). An imported snapshot moves to `Incoming/Done`.
- **A snapshot** is a folder `<name>.blendity-snapshot` holding:
  - the model, by the program's own exporter: `<name>.glb` (Blender's glTF) or `<name>.fbx` (Unreal's level
    export);
  - `snapshot.json`, written last, so its presence means the folder is whole.

```
{"snapshot": 1, "source": "Unreal 5.8", "name": "Yard", "model": "Yard.fbx", "objects": [
 {"name": "Sun", "path": "Sun", "transform": {"position": [2, 3, 1], "rotation": [0, 0.707, 0, 0.707], "scale": [1, 1, 1]},
  "light": {"type": "directional", "color": [1, 1, 1], "intensity": 2}},
 {"name": "Door", "path": "Room/Door", "class": "/Game/BP_Door.BP_Door_C", "props": {"locked": true}}]}
```

- **Each object** is found in the model by `path` (names from the top, `/` between), else by `name`:
  - **`light` / `camera`:** Live Link's own fields (the `light` and `camera` messages above), in Blendity's units.
  - **`class`:** an engine's class (an Unreal Blueprint's path).
  - **`props`:** custom properties. These two become an Engine Component, kept for the way back.
  - **`transform`** (optional): world, in Blendity's axes (left-handed, Y up, +Z forward, metres). An object with
    one is placed by it, and made when the model doesn't hold it (Unreal's lights and cameras). A light or camera
    with none came in the model looking down -Z (glTF's) and is turned to Blendity's +Z.
- **`model`** is a file name only: a path leaving the folder is refused.
- **`model_rotation`** (optional, `[x, y, z, w]`): the turn from the model's import frame to the snapshot's, applied to
  the whole model about the pivot. Unreal's FBX (Z up, front -Y, front axis not forced) comes in as Unreal's
  (-x, z, y), a quarter turn about Y short of the transforms' (y, z, x), so its plugin writes `[0, 0.7071, 0, 0.7071]`.
- **Which objects take a `transform`:** an object the model doesn't hold, and a light or camera. A mesh the model
  holds keeps the model's placement, because its geometry is in that frame.
- **Who writes them:**
  - the Blender bridge's Send to Blendity (and Blendity's own `import <file>.blend`, through Blender in the
    background: `bridges/blender/blend_to_snapshot.py`);
  - the Unreal bridge's Send to Blendity (`BlendityLinkSnapshot.cpp`).

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
