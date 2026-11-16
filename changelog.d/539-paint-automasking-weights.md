## 2026-11-15 (round 539): Weight Paint automasking, Add / Set Weight (Blender 5.3)

**Asked:** Pass 27 (Blender 5.3's new features), worker P1: weight and vertex paint as 5.3 has them - automasking
(Topology, Mesh Boundary, Cavity, Inverted Cavity, View Normal, Area Normal), Add Weight as the default weight brush
with Set Weight beside it, and a Blur that accumulates at low strength.

**Added (task 0519)**
- Weight Paint > Automasking (Rigging window): Topology (only the connected piece the stroke starts on, and its
  Mirror X twin's), Mesh Boundary with Propagation Steps, Cavity (Off / Cavity / Inverted, Factor, Blur), View Normal
  and Area Normal with Limit and Falloff - Blender's options with Blender's defaults, ported from
  `sculpt_automasking.cc` into `src/editor/weight_paint_automask.cpp`. The options multiply; the weight blend takes the
  automask on the target too, as Blender's does, so Set Weight leaves a half-masked vertex at half the Weight.
- Console: `wpautomask topology|boundary|cavity|inverted|view|area [on|off]`, `wpautomask none`, and its numbers
  (`boundarysteps`, `cavityfactor`, `cavityblur`, `viewlimit`, `viewfalloff`, `arealimit`, `areafalloff`).
- A Learn lesson section, "Automasking the weight brush", with a Try-it and a question.

**Changed**
- The weight brushes carry 5.3's names: Add Weight (already the brush Weight Paint starts with), Subtract, Set Weight
  (was Draw; `wpbrush set`, `draw` still works) and Blur. Ctrl with Set Weight paints towards 1 - Weight, as Blender's
  flip does.

**Checked against 5.3**
- Blur already read the weights as they are, dab after dab, so it accumulated at low strength before this round; a
  test now pins it.
- Blender 5.3 also turns Multi-Paint, Auto-Normalize and Mirror Vertex Groups on by default. Blendity has none of the
  three (only Mirror X symmetry and the Normalize All button), so there were no defaults to match; they are not added.
- Vertex Paint: Blendity has no Vertex Paint mode (no colour attribute on meshes). Its automasking waits for that mode,
  which needs a data-model change and an ADR of its own.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux).

<!-- README -->
replace: skins, weight paint and IK in a Rigging window; **automatic
with: skins, weight paint (with Blender 5.3's automasking) and IK in a Rigging window; **automatic

<!-- docs/USABILITY.md -->
after: | Vertex groups could only be set a selection at a time with one weight.
| Weight painting a sleeve or a collar spilled onto the arm or past the rim; the brush had no automasking. (round 539) | Weight Paint > Automasking: Topology, Mesh Boundary, Cavity, View Normal and Area Normal, as Blender 5.3. |
