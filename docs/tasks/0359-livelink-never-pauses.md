# Task 0359: Live Link never pauses, and a live test of everything that flows (round 382)

## Asked
Phase B's last round (plan cozy-fluttering-corbato, "never pausing, and a live test"):
- Live Link must not stall the editor while you light, model, paint, rig and animate with engines connected.
- It must keep going while long jobs run.
- Everything Phase B made flow must be shown to work with the installed engines, not only with the recorded session.

## Measured
`tests_round382`, on this machine (Windows, the release build), each test with four clients connected:

| What | The main thread's tick |
|---|---|
| 200 objects, a 100,000-vertex mesh deformed, a 50-bone rig posed and a clip keyed every frame, 120 frames | 1.1 to 1.9 ms on average, at most 1.7 to 3.2 ms |
| A topology change of the 100,000-vertex mesh (the copy for the mesh worker) | 3.5 to 4.3 ms |
| A surface painted with the paint tool, a dab every frame for 2 s (each dab a new 512 x 512 texture) | 0.1 ms on average, 2.8 ms at most, the texture sent 10 times |

- **What the tests check besides the times:**
  - everything went: a deform and a pose a frame, the clips;
  - the textures at most about 4 a second;
  - no frame over 25 ms (40 while painting).

- **Where the work goes:** meshes, level meshes, textures and clips are built on Live Link's workers.
- **The main thread's share:** copies of what changed, and the small messages.
- **The plan's copy-on-write share of the mesh, not done:** the copy costs 4 ms for 100,000 vertices on a topology change
  only. Sharing would let a worker read a mesh that a sculpt stroke writes in place: the stroke takes the mesh once at
  its start, not per dab.

A second test runs a skin bind of a 100,000-vertex mesh on a worker. A Box moved meanwhile reaches the client on a
frame when the bind is still running. Live Link ticks every frame while jobs run on workers; work the editor does on
its main thread (an import's busy step) still holds everything, Live Link included.

## The live test
The round-318 test (`BLENDITY_TEST_BRIDGE` naming a command that runs the engine's live-session test) now also, after
the Box, bone and dense-mesh steps:
- turns the fog on from 5 to 40 m;
- gives the Box a 4 x 4 checker as its Base Map;
- skins "Arm" to the rig with a Smile key at 0.5;
- puts the clip "Wave" on the rig;
- makes "Crate" with three levels and a 1 x 2 x 1 Box Collider.

Each engine checks these as it takes them:

| Engine | Fog | Checker | Arm | Clip | Crate |
|---|---|---|---|---|---|
| Blender 5.2 | the world's mist from 5 m | a 4 x 4 image | its Armature modifier, Smile 0.5 | the Wave Action | 2 level children, its collider property |
| Godot 4.7.2 | the environment's fog from 5 m | a 4 px albedo texture | its Skin, the blend shape at 0.5 | blendity/Wave | 2 levels, its collider |
| Unity 6000.6.7f1 | RenderSettings.fog from 5 m | a 4 px main texture | a SkinnedMeshRenderer, weight 50 | the Wave asset | an LOD Group of 3, the box 1 x 2 x 1 |
| Unreal 5.8 | the height fog from 500 cm | a 4 x 4 BaseMap | a mesh (Unreal takes the posed vertices) | - | 3 LODs, a box element |

All four report LIVE OK. The checker is checked by its size, not its texels.

**A Godot flake:** two Godot runs in eight failed their "followed until Blendity stopped" check while every scene check
passed. The check now reports why, and the six runs since passed.

## Found by the live test
- **Godot's and Unity's live tests** still expected the material colour linear; since round 377 the bridges convert it
  (Godot's albedo is sRGB, Unity's material colour gamma). Unity's test also read `_BaseColor`, which only URP has;
  it now reads the shader's main colour.
- **A short test folder (Unreal's) had no `Assets/Textures`,** so the checker was never written. The tests make the
  folder first, as round 381's recorded session does.

## The review's fixes (Opus, round 382)
- **The stress test's hand-written texels were almost never seen.** A painted texture is a new texture each stroke,
  so painting is now measured with the paint tool itself, in a test of its own.
- **The long-job test** credits a move only on a frame when the bind still runs.
- **The Try-it** opens the window by its title, and a test checks it.
- **The stress test now checks** that everything went, and its worst frame.
- **The docs** say what was measured and on what.

## Limits
- **The cause of the Godot flake** isn't known yet.
- **The times are this machine's.** The tests allow slower ones (8 ms average, 25 ms worst).
