## 2026-11-16 (round 543): Shadows traced by the graphics card in the Scene view

**Asked:** Port Blender 5.3's Workbench ray-traced shadows to the Scene view, using Vulkan ray queries, as a Preferences quality option that is off and greyed out with a tooltip on cards without ray queries (Pass 27, worker M2).

**Added (task 0523, ADR 0151)**
- **Preferences > Ray-Traced Shadows** (off by default, saved; `views rtshadows on|off`). With it on, the views drawn on the card trace each pixel's shadows instead of looking them up in the sun's shadow map.
  - Every light with Shadows on casts them, including point and spot lights; the map has the sun alone. The first 32 lights take part.
  - The shadows are exact at any distance, with hard edges.
  - Area lights cast no realtime shadow, as with the map.
  - See-through, baked and Solid views keep the map.
- **Greyed out where it can't run:** on a card without ray queries, or with the views on the CPU, the option is greyed out and its tooltip says why. If a build fails (for example, out of card memory), it says so once and the views use the map, still on the card.
- **On the card:**
  - The rasterizer's device opens with ray queries when the card has them.
  - Its own structures (`src/render/gpu_raster_rt.*`) are built in the frame's own command buffer: one bottom level per drawn part of a casting mesh, rebuilt when its vertices change, and a top level of the casting instances, rebuilt only when one moves or the list changes.
  - The lighting passes' shadow lookups go through one `shadow_vis()`: the map's lookup as before with the option off, a first-hit ray with it on.
  - The traced variants are compiled with the other shaders at start.
- **Skinned characters** cast from their rest pose until Pass 26 poses them on the card.
- **The windowed check** prints a "traced shadows" line: the builds so far, and the host's time to record the last frame's. `tools/dev/rt_shadows_row.py` runs the SideRail row with the option off and on.
- **Learn:** a "Shadows traced by the card" step.

**Checked:**
- Both suites: with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux). Linux (dependency-free): `gpu` 129, `editor` 3205, `render` 2550, 0 failed (no card in WSL: the card tests skip).
- The round's tests are the `round 543:` rows at the end of `tests/test_gpu.cpp`, run on an RTX 4070 SUPER: with the option off the picture is bit for bit what it was.
- `gpu` group locally: 4 checks fail, the same 4 as on main locally (test_gpu.cpp:889 x2, :2228, :4888; queued as round 550), so they are not this round's.
- Timed row (SideRail, `tools/dev/rt_shadows_row.py`): TIMED_ROW - it runs in the hub's batched timed window and is filled in then.

<!-- README -->
replace: toon, the Edit Mode cage); camera filters from colour
with: toon, the Edit Mode cage, shadows traced by the card's ray tracing where it has it - round 543); camera filters from colour

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z, any other edit
| The card's views shadowed only from the sun, through a shadow map that blurs far away. (round 543) | Preferences > Ray-Traced Shadows traces every light's shadows on cards with ray tracing; greyed out with the reason elsewhere. |

<!-- docs/ARCHITECTURE.md -->
after: | `gpu::Raster` (`render/gpu_raster.*`), `render/gpu_vk.h`
| `src/render/gpu_raster_rt.cpp` | The views' traced shadows (round 543, ADR 0151): the rasterizer's own acceleration structures (`rt::Shadows::record` builds them in the frame's command buffer) and `shadow_vis`, the lighting passes' one shadow test. |
