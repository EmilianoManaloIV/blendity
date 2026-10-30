# Cognitive walkthrough: a prop modelled, unwrapped, textured, lit and rendered (baseline, round 271)

A fresh profile, Tablet Mode on, UI scale 1.5, a 13-inch 2-in-1 with a pen. The user: an artist from Unity (U), Unreal
(R) or the Adobe suite (A); pen-primary. The four questions per step (Polson): **Try** - will they try to achieve the
right effect; **Notice** - will they notice the control; **Associate** - will they connect the control with the effect;
**Progress** - will they see progress. Y / N / ? per question; the finding; the task that fixes it.

| # | Step | Try | Notice | Associate | Progress | Finding | Fix |
|---|---|---|---|---|---|---|---|
| 1 | Add a cube (GameObject > 3D Object) | Y | N in some layouts | Y | Y | "3D Object" is hidden unless the layout shows it (`editor.cpp:921`); the Hierarchy's right-click Create needs a right-click | 0255 |
| 2 | Move it with the gizmo, snap to the grid | Y | Y | ? | ? | Ctrl snaps but nothing says so while dragging (no hint branch); V vertex snap and typed values are hidden; the snap step is in Preferences | 0254 (F3), 0255 |
| 3 | Enter Edit Mode, select faces | Y (Tab) | Y | ? | Y | "1 2 3" pick modes: the hint says so; a Unity user expects a mode dropdown | 0255 |
| 4 | Extrude the top | Y | N | N | Y | Two "Extrude"s in the Mesh menu; the edge one is labelled "E", which is the Rotate tool in Unity's preset (`scene_view.cpp:2257`) | 0254 (F4), 0255 |
| 5 | Bevel the edges with 3 segments | Y | Y | N | ? | Segments only by the mouse wheel while the tool runs (`scene_view.cpp:3302`): a pen has no wheel | 0255 |
| 6 | Adjust the bevel afterwards (F9) | ? | N | Y | Y | F9 works in Edit Mode only; tooltips promise it broadly | 0255 |
| 7 | Mark seams, unwrap | Y | N | N | Y | The menu says "Unwrap U" but U works only with the UV editor focused (`uv_editor.cpp:577`); the UV toolbar drops its seam buttons when narrow (`:167`); panning is middle-drag only | 0254 (F4), 0255 |
| 8 | Make a material, assign it | Y | N | ? | Y | Assign to Selection is right-click only in the Materials window (`material_window.cpp:232`); drag onto the object is the other way - both pen-hostile | 0257 |
| 9 | Add a light | Y | N | - | Y | Light is hidden from Add Component outside Light / Material layouts (`editor.cpp:1034`) | 0256 |
| 10 | Set the sky | Y | ? | N | Y | The Environment is edited in two windows (Lighting and Render); the lesson names the wrong menu | 0256 |
| 11 | Generate lighting | Y | Y | Y | ? | Progress with elapsed time, no time left; refused in Play with a console warning only; the bake's GPU choice lives in the Render window | 0252 (F1), 0253 (F2), 0256 |
| 12 | Render, save | Y | Y | Y | ? | No time-to-finish; the render image pans with middle-drag only; Save with nothing rendered warns after the click | 0253 (F2), 0257 |

**Baseline score** (a failing step has an N in Try, Notice, Associate or Progress; a ? is a doubt, not a fail): 12
steps, **8 failing** (1, 4, 5, 6, 7, 8, 9, 10). The step a newcomer is most likely to abandon: 7 (UV), where the label,
the focus rule and the toolbar all mislead at once.

## After (round 282)

Walked again on the round-281 build with the same profile, by reading each step against what landed; a step is fixed when every column it failed now reads Y. Partly: one of its problems is gone, another is not.

| # | Baseline | After | What changed | What is left |
|---|---|---|---|---|
| 1 | fail | fail | - | "3D Object" still shows only in the Create, Draw, Edit and Rig layouts (Show All Tools reaches it); the Hierarchy's Create still needs a right-click |
| 4 | fail | fixed | The edge extrude is "Extrude Edges"; menu labels come from the keymap (0254, 0255) | - |
| 5 | fail | partly | `]` and `[` set the segments while Bevel runs (0255) | a pen with no keyboard still has no way: no on-screen field |
| 6 | fail | fail | - | Adjust Last (F9) still covers Edit Mode's operators only |
| 7 | fail | partly | Labels from the keymap; the UV toolbar folds into a More menu; W/E/R/Y follow the preset (0254, 0255) | panning is middle-drag only |
| 8 | fail | fixed | Assign to Selection is a Materials toolbar button (0257) | - |
| 9 | fail | fixed | The Rendering components are in Add Component in every layout (0256) | - |
| 10 | fail | fixed | One Environment, in the Lighting window; the Render window points at it (0256) | - |
| 2, 11, 12 | doubts (?) | Y | The gizmo drag's hint (0254), the tray's time left (0253), disabled buttons with reasons (0252) | - |

**After:** 12 steps, **4 not fully fixed** (1 and 6 failing, 5 and 7 partly), against 8 failing at the baseline.
