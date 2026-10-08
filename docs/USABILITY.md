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
| E | Rotate tool | Extrude | Rotate tool; **Ctrl+E** extrudes Blender-style: faces move along the normal; in vertex / edge mode edges grow faces and lone vertices grow edges, then move freely |
| I | (unused) | Inset | **Inset**, drag to adjust (Ctrl+I too) |
| K | (unused) | Knife | **Knife / Line**: click two points on a face's edges to split it (SketchUp's Line tool, with endpoint / midpoint / on-edge snapping) |
| Ctrl+J | (unused) | Join | **Join** the selected meshes into the active one (Object Mode) |
| X | (unused) | Delete menu | An axis lock during a transform. Ctrl+X dissolves whatever the current selection mode selects; Del deletes. |
| Y | Transform tool | Split | Transform tool. Split is in the Mesh menu and the Inspector. |
| P | (unused) | Separate | **Push/Pull** (SketchUp) |
| Ctrl+D | Duplicate | (Shift+D) | Duplicate: objects in Object Mode, the selected faces in Edit Mode (Shift+D as well) |
| Ctrl+Z / Y | Undo / Redo | Undo / Shift+Ctrl+Z | Both work |
| RMB | Fly / context menu | Cancel, or a context menu | **Fly**, except while an operator runs, when it cancels |
| MMB | Pan | Orbit | Pan, with orbit on Alt+LMB |
| Alt+LMB | Orbit | Loop select (click) | **Orbit when dragged, loop select when clicked** in Edit Mode; Ctrl+Alt+click selects a ring |

**Why this is the best compromise:**
- Nothing Unity users already know changes by default.
- Everything Blender users reach for while modeling works: G, the axis keys, typed values, E / I / Ctrl+B with the mouse, F9.
- The one real clash, R, is a single tick box.

## What was fixed in this pass

| Problem found | Fix |
|---|---|
| Selecting two opposite edges selected all four edges of the quad. Mark Seam, Bevel and the edge highlight all saw four. | Edge mode keeps edges themselves, as Blender does (`edge_sel_`). The edge tools read exactly those edges (`meshops::EdgeSelectionScope`). |
| Shade Smooth on some faces smoothed the whole object. | Shading is per face (`Mesh::face_smooth`). Smoothing stops at flat faces, at edges marked sharp (Mark Sharp / Clear Sharp), at the Smooth by Angle limit, and optionally at UV seams ("Seams Are Hard Edges"). |
| In Edit Mode, clicking another mesh did nothing. | It now becomes the mesh being edited (as in Unity, where a click always selects); picking another object in the Hierarchy does the same. |
| The scene gizmo's axes and label ignored clicks (the click started a box select first). | The gizmo keeps its clicks: named views, and Persp / Iso on the label. |
| Right-clicking empty Hierarchy space opened nothing (the menu closed in the same frame). | Popups opened after their owner declared its menus survive their first frame. |
| The Adjust Last Operation panel (e.g. after Push/Pull) never went away. | An x button, Esc and the next selection click put it away; F9 brings it back. |
| The Camera Preview's Shaded / Rendered button did nothing. | The click fell through to the Scene view and deselected the camera, and Rendered showed the rasterizer when the engine was Rasterized. The inset now keeps its clicks, and Rendered always path-traces. |
| Inset and Bevel ran at a fixed amount. | Both follow the mouse, as in Blender. Bevel takes the wheel for segments and has Clamp Overlap. |
| Colours were RGB only, with a bare popup. | Colours work like Unity's: a swatch with an alpha bar, and a Color window with a saturation / value square, hue strip, RGB 0-255 / 0-1 / HSV modes, alpha, hex RRGGBBAA, and preset and recent swatches. Base Color carries the material's alpha. |
| High render resolutions took typing. | Resolution presets from 640 x 360 to 8K, plus print sizes, each with its megapixels. The output size shows in MP and MB. |
| Exported FBX files did not open in Blender ("ASCII FBX files are not supported"). | FBX is written in binary like Blender's own exporter (ASCII stays as an option for Unity). Every export format is now checked by importing it into Blender 5.2 itself. |
| Exports, screenshots and renders always went to fixed project folders. | Each one asks where to save with the system's Save As dialog (after Blender's export options); File > Import Model and Open Scene use the Open dialog. |
| An OBJ object could pick up an extra, unused material slot from the object before it. | The carried-over material is only added when a face uses it before any `usemtl`. |
| Set Origin could only use fixed points (or Edit Mode's selection, hidden in a menu). | Edit Origin with Handles: the gizmo moves the origin alone, and a click snaps it to a vertex, edge midpoint or face centre. Edit Mode has Origin to Selection as a button. |
| Modifiers were scattered among the components, with only an on/off switch. | One Blender-style modifier stack with Edit Mode / Viewport / Render toggles, Apply per modifier, Duplicate, Copy to Selected and reordering. |
| A shape could only start from a primitive: there was no way to extrude a vertex or an edge. | E / Ctrl+E in vertex and edge mode, with wire edges as in Blender. |
| A new origin didn't act as the transform point: the gizmo sat at the bounds centre (the toolbar defaulted to Center) and scaling used the origin anyway. | Pivot (the origin) is the default and remembered; Center uses the bounds for the gizmo, rotation and scale alike, and so do G / R / S. |
| Materials lived only inside objects; reusing one meant picking it from a list per slot. | Material assets (.mat) dragged from the Project window onto objects, faces, Hierarchy rows or slots, as in Unity. |
| Only the last material slot could be removed. | An x on every slot, and Remove Unused Slots. |
| Shapes could only come from primitives or extrusion. | Draw polylines, rectangles, circles, arcs and polygons onto faces (cut into them) or the ground (new faces), with SketchUp-style snapping. |

## Still worth doing

1. **A Blender navigation preference**: MMB orbit, Shift+MMB pan and numpad views, for users who want Blender navigation too. It shares no keys with modeling, so it is independent of everything above.
2. **A context menu on right-click without dragging** (Unity's Scene view menu, Blender's W menu): the current mode's operators under the cursor. Right-click is already split between fly (drag) and cancel (during operators), so a click without movement is free.
3. **Pie menus or a search** (Blender's F3): with about 50 operators now, a type-to-find box is the fastest way to reach a rare one.
4. **Snapping during G** to vertices, edges and faces (Blender's snap targets), not just the grid increment.
5. **Proportional editing during G / R / S** (it already works with the gizmos), with the wheel changing its radius.
6. **Spin** (an interactive Screw on the selection) and **Knife cuts through several faces** in one stroke: the Knife and the Screw modifier are in; these are their next steps.
7. **The Inspector's operator grids are long.** Collapsible groups (Select, Transform, Topology), or showing only the operators the current selection can use, would help.
