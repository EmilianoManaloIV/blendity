# Usability review: Blender's editing inside Unity's editor

Blendity has two kinds of users. Unity users expect Unity's navigation, its W / E / R tools and an Inspector. Blender users expect to model with the keyboard: G, R and S, axis keys, typed values and the mouse. This review covers where those two clash, what Blendity does about it now, and what is still left.

## The recommended split

The two programs rarely want the same thing from the same input, so the rule below avoids most clashes.

| Job | Follow | Why |
|---|---|---|
| Moving the view | **Unity**: hold RMB + WASD to fly, Alt+LMB orbit, MMB pan, wheel zoom, F frame | This is muscle memory for most people who open a "Unity-style" editor. Blender's MMB orbit stays available as a Preference later; it needs no other key. |
| Picking a tool | **Unity**: Q / W / E / R / T / Y and gizmos | Clicking and dragging gizmos is discoverable, and it suits the mouse-first Unity user. |
| Changing geometry | **Blender**: a verb key, then the mouse or a typed number, then click to confirm or Esc to cancel, then F9 | Blender's real strength is modeling fast without leaving the keyboard. Every edit operator now follows this one pattern. |
| Settings | **Unity**: the Inspector, with expressions in fields | Easier to find than Blender's N panel and redo panel, and multi-object editing works. |

### One pattern for every edit

Every modal operator now behaves the same way. This is the most important consistency rule: users learn it once and it carries over to every tool.

1. **Start** with a key: G move, R rotate, S scale, E extrude, I inset, Ctrl+B bevel, P Push/Pull. Loop Cut (Ctrl+R) cuts at once and is adjusted in F9.
2. **Shape it**:
   - Move the mouse.
   - **X / Y / Z** lock to an axis. Press the same key again for the object's local axis, and a third time to unlock.
   - **Shift+X / Y / Z** lock to the plane without that axis.
   - **Type a number**: expressions work, Backspace edits.
   - **Ctrl** snaps and **Shift** gives fine control. During a bevel, the wheel changes the segments.
   - During a G, pressing **R / S / G** switches between move, rotate and scale.
3. **Finish**: a click or **Enter** confirms, and **Esc** or a **right-click** cancels and leaves no undo step.
4. **Adjust afterwards** in the F9 panel: changing a value re-runs the operator as the same undo step.

A hint bar at the bottom of the Scene view lists the keys while any of these operations is running.

### Where the keys clash, and how it is resolved

| Key | Unity | Blender | Blendity |
|---|---|---|---|
| G | (unused) | Move | **Move, as in Blender** |
| R | Scale tool | Rotate | Scale tool by default. With **Edit > Blender Transform Keys**, R rotates and the Scale tool moves to **T**. During a G, R always switches to rotate. |
| S | (unused) | Scale | Scale with Blender Transform Keys on. During a G, it always switches to scale. |
| E | Rotate tool | Extrude | Rotate tool; **Ctrl+E** extrudes Blender-style (extrude, then move along the normal) |
| I | (unused) | Inset | **Inset**, drag to adjust (Ctrl+I too) |
| X | (unused) | Delete menu | An axis lock during a transform. Ctrl+X dissolves whatever the current selection mode selects; Del deletes. |
| Y | Transform tool | Split | Transform tool. Split is in the Mesh menu and the Inspector. |
| P | (unused) | Separate | **Push/Pull** (SketchUp) |
| Ctrl+D | Duplicate | (Shift+D) | Duplicate: objects in Object Mode, the selected faces in Edit Mode (Shift+D as well) |
| Ctrl+Z / Y | Undo / Redo | Undo / Shift+Ctrl+Z | Both work |
| RMB | Fly / context menu | Cancel, or a context menu | **Fly**, except while an operator runs, when it cancels |
| MMB | Pan | Orbit | Pan, with orbit on Alt+LMB |
| Alt+LMB | Orbit | (emulated 3-button) | Orbit |

**Why this is the best compromise:**
- Nothing Unity users already know changes by default.
- Everything Blender users reach for while modeling works: G, the axis keys, typed values, E / I / Ctrl+B with the mouse, F9.
- The one real clash, R, is a single tick box.

## What was fixed in this pass

| Problem found | Fix |
|---|---|
| Selecting two opposite edges selected all four edges of the quad. Mark Seam, Bevel and the edge highlight all saw four. | Edge mode keeps edges themselves, as Blender does (`edge_sel_`). The edge tools read exactly those edges (`meshops::EdgeSelectionScope`). |
| Shade Smooth on some faces smoothed the whole object. | Shading is per face (`Mesh::face_smooth`). Smoothing stops at flat faces, at edges marked sharp (Mark Sharp / Clear Sharp), at the Smooth by Angle limit, and optionally at UV seams ("Seams Are Hard Edges"). |
| The Camera Preview's Shaded / Rendered button did nothing. | The click fell through to the Scene view and deselected the camera, and Rendered showed the rasterizer when the engine was Rasterized. The inset now keeps its clicks, and Rendered always path-traces. |
| Inset and Bevel ran at a fixed amount. | Both follow the mouse, as in Blender. Bevel takes the wheel for segments and has Clamp Overlap. |
| Colours were RGB only, with a bare popup. | Colours work like Unity's: a swatch with an alpha bar, and a Color window with a saturation / value square, hue strip, RGB 0-255 / 0-1 / HSV modes, alpha, hex RRGGBBAA, and preset and recent swatches. Base Color carries the material's alpha. |
| High render resolutions took typing. | Resolution presets from 640 x 360 to 8K, plus print sizes, each with its megapixels. The output size shows in MP and MB. |

## Still worth doing

1. **A Blender navigation preference**: MMB orbit, Shift+MMB pan and numpad views, for users who want Blender navigation too. It shares no keys with modeling, so it is independent of everything above.
2. **A context menu on right-click without dragging** (Unity's Scene view menu, Blender's W menu): the current mode's operators under the cursor. Right-click is already split between fly (drag) and cancel (during operators), so a click without movement is free.
3. **Pie menus or a search** (Blender's F3): with about 50 operators now, a type-to-find box is the fastest way to reach a rare one.
4. **Snapping during G** to vertices, edges and faces (Blender's snap targets), not just the grid increment.
5. **Proportional editing during G / R / S** (it already works with the gizmos), with the wheel changing its radius.
6. **Knife (K) and Spin / Screw**: the main Blender tools still missing.
7. **The Inspector's operator grids are long.** Collapsible groups (Select, Transform, Topology), or showing only the operators the current selection can use, would help.
