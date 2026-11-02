# Task 0356: clips in Live Link (round 379)

## Asked
Phase B's fourth round (plan cozy-fluttering-corbato, "clips go as AnimationClips (Unity) and Actions (Blender) when
saved or keyed"). The plan's round "clips, LODs, colliders and VRChat in Unity" is split in three: clips (this round),
LODs and colliders (round 380), the VRChat setup in Unity (round 381).

## Built
- **A `clip` message** for each object with an Animation component whose clip asset exists:
  `{"t":"clip","id":<the animated root>,"name":"Walk","asset":"Assets/Animations/Walk.anim","fps":60,"frames":61,
  "loop":true,"tracks":[5,6,7],"bin":N}`.
  - The binary: for each track's object, for each frame, its local position, rotation and scale (10 floats) in Live
    Link's axes, as a `transform` message carries them.
  - Baked from the clip's Position, Rotation and Scale tracks; an axis the clip doesn't key keeps the object's
    current value.
  - The tracks are copied on the main thread and baked on a clip worker (review 379: a minute of 100 keyed objects
    had cost the main thread about 60 ms a send; it now costs about 6 ms). A bake over 32 MB isn't sent: a warning in
    the log, a count in the Live Link window.
  - Also sent again when what the bake reads besides the clip changes: the objects its tracks name, their unkeyed
    axes, the root's kind.
  - Sent at a sync, after every object (a clip names its root's children), and when the clip changes (a key set,
    moved or deleted, another clip chosen), at most 4 times a second while it keeps changing, its last state after.
- **The bridges keep it:**
  - **Unity:** an AnimationClip asset (`Assets/Blendity Live Link/Clips/<the .anim's name>.anim`, found again after a
    reconnect or a domain reload), localPosition / localRotation / localScale curves on each object's path from the
    root, straight between keys (linear tangents), each rotation the short way from the last.
  - **Godot:** an Animation in a "blendity" library on an AnimationPlayer ("Blendity Animation") under the root, with
    position, rotation and scale tracks. A skinned mesh's Skeleton3D now has a modifier ("Blendity Follow") that poses
    its bones from the joint nodes each frame, so a clip played on the rig moves the mesh too.
  - **Blender:** an Action ("<name>", one per clip asset, found again after a reconnect) with a slot per object, kept
    with a fake user and not assigned (Live Link still poses the objects); its keys at the scene's frame rate, so it
    plays at Blendity's speed.
  - **Unreal:** not yet (a Level Sequence per clip is a later round); the poses keep streaming.

## Checked
- `tests_round379`: the sync sends the clip; frame 10's samples against `apply_clip` (exact); a material track left
  out; a key changed sends it again once; nothing while unchanged; changed twice at once, the second after a quarter
  second; the Try-it.
- The recorded session adds a clip on the rig ("Wave": Upper turned 60 degrees about Z and raised 0.5 m over 60
  frames), and at its end the pose Blendity gives at frame 30.
- Review 379's cases: another clip chosen and an unkeyed axis moved send it again, a keyed axis moved doesn't; a
  minute of 100 keyed objects at 60 fps costs the tick 5.7 ms; ten minutes (144 MB) is refused.
- Each bridge samples its own clip at frame 30 against that pose (Unity also between frames 30 and 31, straight; Unity
  and Blender replay the session twice and keep one clip; Godot's follower poses the Arm's bone from the clip):

| Bridge | How | Upper at frame 30 against Blendity's pose |
|---|---|---|
| Unity 6000.6.7f1, built-in and URP | `AnimationClip.SampleAnimation` on the asset | 0 m, 0 degrees |
| Godot 4.7.2 | `position_track_interpolate` / `rotation_track_interpolate` | 0 m, 0 degrees |
| Blender 5.2 | the Action's F-Curves evaluated at frame 30 | 0 m, 0 degrees |
| Unreal 5.8 | ignores `clip` (Replay passes) | - |

## Limits
- Only transform tracks (Position, Rotation, Scale); shape-key and component tracks are not in the clip yet.
- The Animation component's Speed, Wrap and Hold aren't carried; `loop` is the clip's own.
- A clip is kept in the engine when its Animation component goes.
- Two clip assets with the same file name in different folders share one engine clip.
