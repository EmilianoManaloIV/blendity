# Task 0368: After Effects and Premiere on the Sequencer (round 391)

## Asked
Phase D's fifth round, the After Effects and Premiere round (plan cozy-fluttering-corbato; round 387's findings 23, 24,
25 and 27, and a small one):
- **Footage:** video clips on the Sequencer, decoded through FFmpeg.
- **Over time:** keyframed opacity and transform on clips.
- **Edits:** a blade cut and ripple delete.
- **Export presets:** H.264, ProRes and the VRChat video player's MP4.
- **A paper card by its name.**

## Built
- **Footage** (`render/video_decode.*`, `editor/seq_footage.cpp`): a video file (.mp4, .mov, .mkv, .webm, .avi, .m4v)
  dropped onto a lane, or `seq video <file> <track> <start> [frames]`, is a card as long as the file (its frames at the
  sequence's rate). It is read in-process with FFmpeg's libraries, as Blender's movie reader does: the frame under the
  playhead is decoded on from the last one, or after a seek to the keyframe before it when the playhead goes back or
  more than two seconds on. It shows over the Game view and in Render Animation's frames like any card. The
  dependency-free build has no FFmpeg: footage is refused with Get Blender's Libraries... as the fix. A footage bar is
  saved as a `card 5` line; older builds skip it.
- **Keys on a card** (Premiere's Effect Controls): `seq key <bar> opacity|x|y|scale|rotation <frame> <value>` (frames
  from the bar's start), `seq unkey <bar> <channel> [frame]`:
  - **opacity** 0-1; **x** and **y** in fractions of the frame's width and height from its middle; **scale** (1: fitted);
    **rotation** in degrees, clockwise;
  - linear between keys, held before the first and after the last; each is one undo step;
  - an untouched card is drawn as before (fitted on black); a keyed one lies over the render or the Game view's
    picture at its opacity, moved, scaled and turned;
  - saved as `key <channel> <frame> <value>` lines after their bar's line; older builds skip them.
- **Add Edit** (Premiere's; Alt+B in the Sequencer window, the bar's menu, `seq blade <frame> [track]`): every bar
  across the frame (on one track, or all) is cut in two there. The right part starts at the frame with its trim moved
  on (a clip's by its speed); its keys after the cut move with it, and each keyed channel keeps its value at the cut on
  both sides. One undo step "Add Edit"; a frame with no bar across it is refused.
- **Ripple Delete** (Shift+Delete, the bar's menu, `seq ripple <bar>`): the bar goes and the later bars on its track
  move left by its length. One undo step.
- **Export presets** (the Render window's combo, `renderanim preset h264|prores|vrchat`, kept in the preferences):
  - **H.264 (MP4):** as before;
  - **ProRes 422 (MOV):** FFmpeg's prores_ks, Apple's standard profile, 10-bit 4:2:2 - an intermediate for After Effects
    or Premiere;
  - **VRChat (MP4, 1080p):** H.264 High at CRF 20, at most 1080 lines (the width in proportion, even), the index at the
    front (`+faststart`) so VRChat's video players stream it, AAC sound;
  - an ffmpeg program (Preferences) gets the same choice in its arguments.
- **A paper card by its name:** `seq paper <name> <track> <start>` - a word with no extension that names a paper in
  Assets/Papers is that paper.
- **Keys and the keymap:** Add Edit and Ripple Delete are keymap actions (`seq.add_edit` Alt+B, `seq.ripple`
  Shift+Delete) that act while the pointer is over the Sequencer; Ctrl+K stays the Command Palette.
- **The decoder** opens files only (no URL a scene names), refuses frames over 8192 x 8192, takes the frame rate as
  Blender does (`av_guess_frame_rate`) and says once when reaching a frame took over a quarter of a second.
- **Not built:** keys on clips (they animate objects already), effects other than the five channels (blur, color),
  transitions between cards, J/K/L playback, audio from footage files (WAV bars carry sound), and decoding on a worker
  thread (a frame is decoded on the main thread when the playhead moves).

## Checked
- **Keys:** half way between two keys, held after the last, the defaults, the clamps; saved and read back with a footage
  bar; the topmost card at a frame is the footage.
- **Add Edit:** a keyed card cut 10 frames in - the left's trim, the right's start and trim, both sides' opacity at the
  cut, a later key moved; a clip at speed 2 cut 10 frames in moves the right's trim 20 clip frames; in the editor the
  two bars, the undo step "Add Edit" and its undo; a frame with no bar refused.
- **Ripple Delete:** a bar at 60 moves to 12 when the 48-frame bar before it goes.
- **A keyed card on screen:** at half opacity the red card over blue is half of each in the middle and the letterbox is
  the render; moved half a frame right, the left of the frame is the render.
- **Footage** (the build with Blender's libraries): three colored frames at 3 fps encoded and read back in order (3 fps,
  1.00 s), a seek back; the bar 60 frames long at the sequence's 60; at frame 25 the second (green) frame over a
  render; the dependency-free build refuses it.
- **Presets:** ProRes's file probed as prores, H.264's as h264; VRChat's from an 8 x 2160 frame comes out 4 x 1080; the
  ffmpeg program's arguments for ProRes and VRChat; the sizes (3840 x 2160 to 1920 x 1080, odd sizes made even).
- **A paper by its name:** the card's media is the paper's file.
- **The review's fixes, checked:** past its last key, at full opacity, a keyed card still leaves the render in the
  letterbox; a ProRes file 32 x 15 reads back its padded row as the row above; a key at 9.6 lands on the one at 10 and an
  unkey of nothing takes no undo step; a card with no end of its own cut 20 frames in gives 20 and 40; key lines after a
  card line of an unknown kind are left out.
