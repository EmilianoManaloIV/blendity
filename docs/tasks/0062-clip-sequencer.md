# 0062: A Premiere-style clip sequencer, instead of a state machine

- **Status:** done
- **Requirements:** R-06
- **Decisions:** ADR 0030
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/59

## Goal
The user (2026-10-12): "I want a way to integrate a clip editor like Premiere Pro to go between animation clips
instead of using a state machine." Asked how Play should choose clips (2026-10-13), they picked **a timeline plus
triggers**: clips laid out on tracks with crossfades and played in order, and in Play a key (or a command) jumps to a
named clip with a blend.

## Scope
- In:
  - **Sequences** as assets, `Assets/Animations/<name>.seq` (text): tracks of **clip instances**, each a clip
    (`.anim`), where it starts on the timeline, its trimmed in and out frames, a speed and a name. Two instances on one
    track that overlap **crossfade** over the overlap (Premiere's cross dissolve); a higher track's clip wins for the
    numbers it animates (as video layers).
  - **Triggers** in the sequence: a key (or a name a command uses) and the instance to jump to, with a blend time.
  - **The Sequencer component** (Unity's PlayableDirector, roughly) on the animated root: its sequence, Play
    Automatically, Wrap (Loop, Once). In Play it plays the timeline; a trigger's key jumps to its instance, blending
    from the pose of the moment over the blend time, and the timeline carries on from there.
  - **The Sequencer window** (Window > Sequencer; the Animation layout gets it beside the Animation window): a ruler,
    the tracks, each instance a bar with its name; drag a `.anim` from the Project window onto a track to place it;
    drag a bar to move it (onto another track too), its ends to trim it; overlaps show the crossfade; Delete; a bar's
    right-click: Speed, Rename, Go To; Add Track; the playhead with Preview (as the Animation window's: scrubbing
    shows the pose, turning it off puts the scene back); the Triggers list (key, instance, blend).
  - Edits are undo steps; renaming a `.anim` keeps the sequences pointing at it.
  - Console: `seq new [name]`, `seq add <clip> <track> <start>`, `seq trigger <key> <instance> [blend]`,
    `seq jump <instance>` (in Play), `seq frame <n>`, `seq preview on|off`.
  - A Learn section.
- Out: audio, blend shapes, events that call code.

## Acceptance criteria
1. Sampling: inside one instance the sequence gives the clip's own values (trimmed, at its speed); across an overlap
   the values blend linearly from one to the other; a higher track's instance wins.
2. In Play, a trigger's key jumps to its instance, blending from the current pose over its blend time.
3. A `.seq` file round-trips; renaming a clip keeps the instance pointing at it.
4. In the window, dragging a bar moves it (one undo step) and dragging its end trims it.
5. Sampling a sequence of 4 tracks of 200-track clips costs at most 0.5 ms a frame (stress).

## Tests and checks to run
- Unit tests (round 86); full suite, verify mode, Linux, sanitizer; stress `animation`.

## As built
- **Clips play frame for frame** at the sequence's rate (60 by default, as clips are). A clip saved at another rate
  isn't converted; no clip editor makes one yet.
- **Two overlapping bars per track crossfade.** Where three overlap, the two that started last blend.
- **Rename** is the Name field below the tracks (and `seq rename`), not the right-click menu. The menu has Go To, Speed
  0.5x / 1x / 2x and Delete. Triggers follow a renamed clip.
- **Speeds** are 0.05 to 10, forwards only (a file's negative speed plays forwards).
- **Not built yet:** vertical scroll and zoom of the timeline, snapping, renaming or removing tracks, editing the
  sequence's frame rate. A trigger key typed in the window isn't checked (the console checks it).
- **Game keys** (every component's `key_down`) are not seen while a text field has the keyboard, so typing in the
  Console doesn't fire triggers.
- **Performance:** values are 64-bit keys of (path, property, axis) in a reused open-addressing table. A sequence of 4
  tracks of crossfading 201-track clips costs 0.31 ms a frame (target 0.5).
- Tests: round 86 (six tests).
