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
