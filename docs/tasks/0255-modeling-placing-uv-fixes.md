# 0255: modeling, placing and UV - the walkthrough's fixes

- **Status:** in progress (round 275)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #221

## Asked
The HCI pass (ADR 0085), the first area round: what the modeling walkthrough (`docs/hci/walkthrough-model-uv-material-
light-render.md`) and the audits found in modeling, placing and the UV editor - one Ctrl with three meanings, tool
parameters only the wheel can reach (no pen, no finger), modals that need a hovering pointer, the UV editor panning
with the middle button only and dropping toolbar buttons when narrow, Hierarchy drop zones a quarter of a row, one
name for two operations and two names for one, a Boolean that needs "selected last", Adjust Last Operation missing for
the tools that need it most. Nielsen's H4 (consistency), H7 (flexibility and efficiency), Fitts (targets), the pen.

## What changed
- **Ctrl stays - said**: Push/Pull's "keep the original face" and Sketch's "freehand" stay on Ctrl (the plan said Alt;
  Alt is the orbit modifier in the Unity, Unreal and Maya feels, which is why task 0223 moved freehand off it), and the
  hints and tooltips now say what Ctrl does in each tool. The snap increment is a dropdown beside the magnet (0.1 /
  0.25 / 0.5 / 1 / 5 m, or Custom for a value set in Preferences), saved with the preferences, read by the gizmo's Ctrl
  drag and the modals; the turn step stays Preferences'.
- **Keys for the wheel-only parameters**: in a modal `]` / `[` do what the wheel does - Bevel's segments, Array's
  count, Grid's rows and columns (Shift), Pipe's sides, Taper's and Recess's second amount - so a pen or a finger can
  set them; the modal hint says so (`+` and `-` stay typed values).
- **The UV editor**: its toolbar folds the buttons it has no room for into a "More" menu instead of dropping them; W / E / R / Y come from the keymap's tool actions (`action_pressed`),
  and the hint says the preset's chords.
- **Hierarchy drop**: the "into" zone is the middle third of a row (a quarter before), "before" and "after" the outer
  thirds (`hier_drop_mode`); a "Move into..." submenu in the row's menu reparents to the scene's root or to any of
  the first forty objects without a drag (a finger's way).
- **Names**: the edges' "Extrude" is "Extrude Edges"; "Select Z-Fighting" is "Select Overlapping Faces" and "Select
  Overlapping" is "Select Doubled Vertices" (the operator ids are unchanged).
- **Boolean by name**: "Boolean..." in the Mesh menu opens a dialog naming the object kept and cut (a dropdown over
  the selected meshes, the active one first) and the operation, with Apply now / Live modifier; `boolean <op>
  [modifier] [kept <name>]` in the console. The result no longer depends on which object was selected last; the
  submenu's old way still works.

## Tests (`tests_round275`)
- `hier_drop_mode` over a 30 px row: the first third before, the middle into, the last third after.
- The three renamed operations' labels.
- A Bevel modal started by the keymap's chord over the view: `]` twice raises the segments by two, `[` lowers by one.
- Cube kept and cut by the Sphere: the same face count whichever of the two was selected last - once by the hook,
  once by `boolean difference kept Cube`.
- The UV editor's hint names the preset's tool chords (W E R under Unity, other letters under Blender).

## Not done
- The UV editor taking the Rotate tool from a plain E is not driven by a test: in a headless frame the letter does not
  reach the editor's keyboard block (the hard-coded W / E / R did not either); in the app the keys work as before.
- The Boolean dialog's buttons and "Move into..." are not driven by a test (the hook and the console form are).
- Typed lengths in Draw, Knife and Sketch (SketchUp's measurement box).
- Modals run from a pen held down (press, drag, lift) - today a pen hovers first, as a mouse does.
- Adjust Last Operation for Knife, the Draw shapes, Set Origin, Boolean, Join, Separate, the primitives and the UV
  operations, and in Object Mode; the Wheel's edit set additions (Push/Pull, Knife, Adjust Last).
- A pen's pan in the UV editor: Space+drag was tried and dropped (Space is the Wheel's chord in every preset but
  SketchUp's, where it is the View tool); two-finger panning; Build from Sketch's refusals still name the console syntax.
- The round 275 plan was two rounds' worth: the rest is round 276's first item or a round of its own.
