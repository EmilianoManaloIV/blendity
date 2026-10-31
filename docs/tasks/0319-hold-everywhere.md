# Task 0319: animating on 2s and 3s - held in the preview, the camera smooth

- **Status:** done (round 340, 2026-10-31)
- **Asked:** the 2D-look methods ("Add those components onto the plan."): animating on 2s and 3s, the camera kept smooth.

## Today (before)
The Animation component's Hold (task 0149: on 1s, 2s, 3s, 4s) held the pose in Play, sequences and renders. The
Animation window's Preview and scrubbing showed every frame, and a camera the same clip animated was held with the
characters - anime holds the characters, not the camera's pan.

## What
- **Preview holds as Play does:** the Animation window's Preview (scrubbing, playback) applies the root's Hold.
- **The camera moves every frame:** an object with a Camera that the clip animates is sampled at the frame itself;
  **Hold the Camera** (on the Animation component) holds it with the rest.
- **The ruler marks the drawn frames:** on 2s or 3s, a mark at each frame a new pose is drawn on.
- `apply_clip(clip, root, frame, hold, hold_cameras)`: one place for the hold, Play and the Preview through it.

## The review's fixes
- Render Animation held twice: the camera was held in the video and a range starting between drawn frames posed a frame
  early. The render passes the raw frame and the hold counts from its first frame (`apply_clip`'s `hold_from`), held
  once.
- Set Key, Add Key and Record on a frame between drawn poses wrote the held pose there and bent the curve: they key the
  drawn frame the playhead shows (a camera's at the playhead).
- A camera on an animated rig's empty was held: the rig moves every frame too (`camera_rig`).
- The curve drag and the Zoetrope hold as the Preview does; the ruler walks only the frames in view; the lesson says a
  Sequencer holds everything; Play is tested through the component.

## Not done
- The Sequencer's own camera keeps its hold (a sequence holds everything it plays).
- A scene-wide Hold default in the Render settings (the plan's idea): each Animation already has its Hold, and a second
  place to set it would leave which one wins to guesswork.

## Acceptance (`tests_round340`)
- Render Animation of 3-6 on 2s poses 3, 3, 5, 5 while the camera moves every frame; Set Key at 5 keys frame 4; a
  rig's empty moves every frame (Hold the Camera holds it).
- On 2s, the Preview at frame 5 shows frame 4's pose; the camera frame 5's; Hold the Camera holds it to frame 4's.
- `apply_clip` with the hold: frame 6 moves on; the ruler's marks at every second frame; Hold the Camera saves.
