## 2026-11-15 (round 540): Size, strength and colour kept per brush; pressure curves for Paint's Hardness and Spacing

**Asked:** Blender 5.3's brushes: each keeps its own size, strength and colour, with a Unified switch to share one value,
and Paint's Hardness and Spacing follow the pen's pressure through a curve.

**Added (task 0520)**
- Sculpt, Weight Paint and Paint (Paint / Smudge / Blend) keep size, strength (Paint: opacity) and colour (Paint) for each
  brush: Draw, then Smooth with another size, then Draw again gives Draw its own size back. A brush used for the first time
  starts with the size you had. "Unified Size", "Unified Strength" (Paint: "Unified Opacity") and, in Paint, "Unified Colour"
  in each Inspector section share one value again, as Blender's: while it is on, the value you set is the shared one and
  no brush's own is touched; turning it off gives the active brush its own value back at once.
- Paint: "Hardness Pressure" and "Spacing Pressure" with a curve each. On, a dab's hardness is `hardness * curve(pressure)`
  and the step between dabs `spacing * (1.5 - curve(pressure))` (at least 0.05 of the radius). Off at first.
- Saved: `brushes.txt` beside `prefs.txt` (slots, active brush, switches and shared values, curves); malformed lines are
  skipped and NaN / out-of-range numbers clamped. A headless run with no scratch preferences folder refuses `brush save` /
  `brush load`.
- Console `brush unified sculpt|weight|paint size|strength|colour on|off`, `brush curve hardness|spacing on|off [x y ...]`,
  `brush save|load|list`.
- New `src/editor/brush_slots.h/.cpp` (a bank per mode keyed by the brush's index; a brush added later needs no change).

**Changed:** a brush no longer inherits the size of the one before it once it has been used, so one old Sculpt test (the Drag
Dot mask stroke) needed its size and strength set again after it switched to Draw.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux).

<!-- README -->
after: the pen's eraser end, tilt and a pressure curve in sculpting (round 368);
a brush keeps its own size, strength and colour, with Unified switches, and Paint's hardness and spacing follow the pen's pressure curve (round 540);

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z
| Switching from Draw to Smooth and back kept one size for every brush. (round 540) | Each brush keeps its own size, strength and colour (Unified switches share them); Paint's Hardness and Spacing can follow the pen's pressure. |

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::button_grid`, `ui::Context::button_cells`
| `BrushBank`, `brush_bank_switch`, `brush_bank_flips`, `Editor::brush_banks_sync`, `brushes.txt` (`editor/brush_slots.h`) | Size, strength and colour kept per brush in Sculpt, Weight Paint and Paint, keyed by the brush's index; Unified switches share one value (round 540). |
