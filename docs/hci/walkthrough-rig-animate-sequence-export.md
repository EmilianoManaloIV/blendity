# Cognitive walkthrough: rig > skin > key > sequence > export (baseline, round 271)

Same conditions and columns. The user: an animator from Unity's Animation window / Unreal's Sequencer / After Effects, on
a keyboard and a pen.

| # | Step | Try | Notice | Associate | Progress | Finding | Fix |
|---|---|---|---|---|---|---|---|
| 1 | Auto-Rig the character | Y | Y | Y | Y | The best in-context feedback in the editor: "click the <marker>" with a diagram (`auto_rig_tool.cpp:262`); the "Alt+drag to orbit" hint is wrong under the Blender feel | 0254 (F4) |
| 2 | Bind the skin | Y | ? | N | ? | Needs the mesh and a bone selected with Ctrl+click in the Hierarchy (`rigging_window.cpp:163`): a two-object selection foreign to Unity and hard on a pen; the solve's progress bar lives only in the Rigging window; Play cancels it silently | 0259, 0253 (F2) |
| 3 | Fix weights in Weight Paint | Y | Y | ? | Y | The pen's eraser end does nothing (`weight_paint.cpp:286`); no Ctrl-subtract; refusals in the console | 0259 |
| 4 | Open the Animation window, new clip | Y | Y | Y | Y | The empty state explains itself | - |
| 5 | Turn Record on, pose the arm | Y | Y | Y | N | Record shows only a dot in its button and a red playhead; with the window behind a tab every gizmo edit becomes a key and nothing says so (`editor.cpp:1853`) | 0258, 0261 (F7) |
| 6 | Step frames, set a key by hand | N | N | N | - | No keymap action for play, step or key in any preset (`keymap.cpp:106-209`); keying needs a double-click on a row; Play and Next-key share the ">" glyph | 0258 |
| 7 | Edit a long clip's keys | Y | Y | N | ? | No time zoom or pan: a 1,000-frame clip is under 1 px a frame; key hits are ±5 px; tangents by right-click only | 0258 |
| 8 | Preview during Play | Y | - | - | N | Refused with a console warning; key edits during Play return silently (`animation_window.cpp:299`) | 0252 (F1) |
| 9 | Lay clips in the Sequencer | Y | Y | Y | Y | Premiere-style bars; trim grips can be 2 px (`sequencer_window.cpp:319`) | 0258 |
| 10 | Open the clip from the Project window | Y | N | - | - | Double-clicking a .anim opens the OS text editor (`project_window.cpp:592`) | 0260 |
| 11 | Export to Unity with the rig and animation | Y | Y | - | N | The exporter has no skeleton, skin or animation options (`export.h:38-59`) and the manual does not say so | out of this pass: its own round; the manual says so from 0260 |
| 12 | Save before closing | Y | - | - | N | Exit auto-saves without asking; closing while playing skips it (`editor.cpp:388`); New / Open replace the scene without a prompt | 0254 (F5) |

**Baseline score** (an N in any of the four columns): 12 steps, **8 failing** (2, 3, 5, 6, 7, 8, 10, 11; 12 fails on
Progress too: 9 counting it). The step most likely abandoned: 6 (no key to set a key); the most dangerous: 12 (data loss).

## After (round 282)

Walked again on the round-281 build with the same profile, by reading each step against what landed; a step is fixed when every column it failed now reads Y. Partly: one of its problems is gone, another is not.

| # | Baseline | After | What changed | What is left |
|---|---|---|---|---|
| 1 | Y (a wrong hint) | Y | The hints take their keys from the keymap (0254) | - |
| 2 | fail | fixed | A Skeleton dropdown in the Skin section, no Ctrl+click (0259); the skin solve in the tray (0253) | - |
| 3 | fail | fixed | Ctrl and the pen's eraser subtract, decided once a stroke (0259) | - |
| 5 | fail | fixed | A RECORD badge in the Scene view's bar and a red frame round the view (0258) | - |
| 6 | fail | fixed | Keymap actions for Set Key, Play, previous and next key, Record in every preset (0258) | stepping one frame still needs the frame field |
| 7 | fail | partly | Key hits are 9 px either side; the tangent menu by Alt+click (0258) | no time zoom or pan in the dopesheet |
| 8 | fail | fixed | Set Key and Record refuse in Play with a toast and a Fix (0258) | - |
| 10 | fail | fixed | A .anim's double-click opens the Animation window (0260) | - |
| 11 | fail | fail | - | the exporter still has no skeleton, skin or animation (out of this pass) |
| 12 | fail | fixed | "Save changes?" before New, Open and Exit; leaving Play before the auto-save (0254) | - |

**After:** 12 steps, **2 not fully fixed** (11 failing, 7 partly), against 8 failing at the baseline.
