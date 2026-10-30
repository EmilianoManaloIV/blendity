# 0222: fingers and a pen on a 2-in-1 - the write-up and a check list

- **Status:** done (round 242)
- **Requirements:** R-14
- **Decisions:** ADR 0062
- **Model:** main session
- **PR:** #188

The user asked to "integrate better support for pen mode. So that if I'm in a 2 in 1 laptop I can navigate more fluidly
and manipulate the environment", then to "include finger and pen support".

Their choices:
- in Tablet Mode, fingers navigate and the pen manipulates;
- a pen quick menu;
- Tablet Mode follows the fold, with an override.

Tasks 0213-0221 did it, one round each (233-241). This is the summary, and a check list for the one test these rounds
couldn't do: a real touch screen and pen.

## The input model (0213, ADR 0062)
- **Every pointer event says what made it:** the mouse, a pen or a finger. A pen's side button is read, and a finger's
  press can be taken back.
- **One recognizer** (`bl::TouchRecognizer`, no OS calls) turns fingers into:
  - one-finger presses, moves and lifts;
  - two-finger pinch, pan and twist.

  Windows and Linux feed it.
- **A finger's press waits** up to 0.12 s, or 10 px, or its lift. A pinch's second finger usually lands in that time,
  so the first never pressed anything. A later second finger takes the press back: nothing clicks, selects, drops or
  docks.

## What each task added
| Task (round) | What | PR |
|---|---|---|
| 0213 (233) | Pointer kinds, the recognizer, Windows' fingers read from its pointer messages | #179 |
| 0214 (234) | Tablet Mode: one finger orbits, a tap selects, a double tap frames, a flick glides; the editor's test clock | #180 |
| 0215 (235) | Tablet Mode: a finger scrolls every list and panel, sliders still edit sideways | #181 |
| 0216 (236) | The pen alone navigates: the Scene Gizmo dragged, a hand and a magnifier, the side button held | #182 |
| 0217 (237) | The pen quick menu: the side button clicked or the pen held still, then a flick or a tap | #183 |
| 0218 (238) | Tablet Mode follows the fold (Auto, On, Off), acting only on a change | #184 |
| 0219 (239) | Linux: fingers and pens through XInput 2 (libXi opened at run time) | #185 |
| 0220 (240) | macOS: the trackpad's pinch and twist; a tablet's pressure, tilt and eraser | #186 |
| 0221 (241) | The manual's section, a Learn section, the status bar's hints in Tablet Mode | #187 |

What the reviews caught: each round had an Opus review, and each found something real. The ones that mattered most:
- Auto Tablet Mode would have switched on for ordinary desktops (0218);
- a cancelled press could still move a file (0213);
- a finger scroll could collapse a multi-selection or change a slider (0215);
- Linux's tablets would have been missed whenever their device appeared after the start, which libinput does at a
  pen's first use (0219).

## What was and wasn't tried
- **Tried:** every gesture and pen action is a scripted unit test. The `input` group holds the recognizer's rules, the
  editor's behaviour for each pointer, the quick menu, the posture's preference and the platforms' pure conversions.
  The Windows, Linux and sanitizer suites run them all.
- **Not tried on hardware:**
  - The machine these rounds were written on has no touch screen or pen in use: the Windows touch and pen paths ran
    only as compiled code.
  - **Linux:** the XInput 2 code needs `libxi-dev` to compile. No machine here has it, so CI compiles it for the first
    time (`libxi-dev` was added to CI's packages).
  - **macOS:** the trackpad and tablet code can't be built here at all; CI's macOS job compiles it.
- **The check list below** is the hardware test.

## Check list for the 2-in-1 (about ten minutes)
Open Blendity with a scene that has a few objects.

1. **Fold the laptop** into a tablet. The status bar says "Tablet Mode on: the keyboard folded away" and the panels
   grow. Unfold it: Tablet Mode goes off. (If nothing happens: Preferences > Pen and touch > Tablet Mode shows Auto?
   Then Windows doesn't report the fold on this machine. Tablet Mode is still in any tab's right-click menu.)
2. **One finger in the Scene view:** drag to orbit; tap an object to select it; double-tap to frame it; flick and watch
   the view glide, then touch to stop it.
3. **Two fingers:** pinch to zoom, drag together to pan, twist to turn. Put one finger down, then a second a moment
   later: no box select appears.
4. **Lists:** drag a finger up and down the Hierarchy and the Inspector: they scroll and glide. Drag a slider sideways
   with a finger: it moves. Tap a Hierarchy row: it's selected.
5. **The pen:** select and move an object with the gizmo. Rest your palm on the screen while drawing: nothing happens
   under the palm.
6. **The pen alone:** drag the Scene Gizmo (top right) to orbit; drag the hand under it to pan and the magnifier to
   zoom. Hold the pen's side button and drag: it orbits (Shift pans, Ctrl zooms, if a keyboard is there).
7. **The quick menu:** click the side button over the Scene view, or hold the pen still half a second: the ring opens.
   Flick through Undo and lift. Open it again and tap Delete with an object selected.
8. **Weight Paint or the Editing tab's mask brush:** press lightly and hard, tilt the pen, turn it over to erase.

Anything that doesn't behave as written goes into the next round.
