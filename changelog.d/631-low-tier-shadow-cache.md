## 2026-11-26 (round 631): Low quality releases oversized CPU shadow planes

**Asked:** Make memory available to the editor after switching a shaded CPU viewport to Low quality.

**Fixed (task 0613, ADR 0026)**

- Switching to Low releases obsolete completed shadows, static caster layers and the rasterizer's spare depth plane.
- Compatible shadow maps keep their depth data and cache keys. The shadow memory account includes the spare depth plane.

**Checked:** On the 1280 × 720 software viewport, matched single and split views fell from 20 MiB to 4 MiB of retained CPU shadow storage at settled Low. The first caster edit fell from 28 MiB to 12 MiB. Scene, Game and shadow-depth hashes matched, as did all six F12 images. Windows passed 45 focused, 40,585 render and 3,908 editor checks. Linux passed 40,479 render and 3,860 editor checks. Both editor stress runs completed 1,500 frames with zero stale pictures or missed changes.

<!-- README -->
after: The Lighting window shows the active update mode
Selecting Low releases oversized shadow planes in CPU viewports (round 631).

<!-- docs/USABILITY.md -->
after: | The editor felt sluggish: moving the mouse over a panel
| Switching a CPU viewport from High to Low could keep an obsolete shadow plane through redraws and edits. | Low releases oversized completed maps, static layers and spare depth when selected (round 631). |

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::render_scaled`, `set_quality_tier`, `shadow_resolution_for_view`
| `Editor::set_quality_tier`, `shadow_map_scratch_bytes`, `trim_shadow_map_scratch` | Entering Low releases oversized CPU viewport shadow depth, including the rasterizer's current-thread spare. Compatible cached depths keep their keys and contents. | ADR 0026, task 0613 |

<!-- docs/PERFORMANCE.md -->
append:
## Round 631: CPU shadow memory after lowering the quality tier

In a generated sun-lit scene at 1280 × 720, the baseline retained 20 MiB of CPU shadow depth after switching from High to Low. The candidate retained 4 MiB at the same checkpoint in single and split views. The first caster edit used 12 MiB versus 28 MiB. A Low launch kept the same cache build counts. Returning immediately to High rebuilt one completed map. Across six paired runs on two physical desktop cores, settled process working set fell 15.57–16.35 MiB. The immediate return to High took 3.68–7.99 ms longer. The 16-edit median changed by at most 0.19 ms, and lifetime peak memory showed no consistent change. The active depths and both view images matched. See [task 0613](tasks/0613-low-tier-shadow-cache.md) for the measurements and controls.
