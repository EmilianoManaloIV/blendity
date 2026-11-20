## 2026-11-18 (round 584): Carry face-corner paint through mesh edits

**Asked:** Keep face-corner colours when editing, applying modifiers, joining or separating painted meshes.

**Changed (task 0566, ADR 0152)**
- Mesh edits carry each surviving face corner and interpolate linear RGBA within the contributing face. Artists can keep different colours and alpha on either side of the same vertex.
- Join matches layers by name and domain, uses white for a missing side, and assigns distinct names when domains conflict. Separate keeps each piece's painted faces and layer selections.

**Checked:** All 25 groups passed in three configurations. Including the final modeling-group rerun, Windows passed 134,275 checks with Blender libraries and 125,174 dependency-free. Linux passed 125,199 dependency-free. Focused checks included all twelve outer and cutter walls of a Boolean difference, clamped remesh gradients, a three-way Join with occupied names, editor Apply All and Undo. Against the release baseline both Windows configurations kept zero problems in 26,620 Push/Pull operations and all 394 existing overlapping results. All 11,540 odd-mesh operations passed, including 26 new painted operations. All 5,032 curved operations retained the six existing problems and overlap counts of 67 with libraries and 66 without.

<!-- README -->
replace: model, texture baking.
with: model, face-corner paint that survives mesh editing, and texture baking.

<!-- docs/USABILITY.md -->
after: | A mesh had no vertex colours:
| A mesh edit that rebuilt faces discarded their painted corners. (round 584) | Edit Mode tools, modifiers, Join and Separate carry colours with their source faces. |

<!-- docs/ARCHITECTURE.md -->
replace: Corner layers only while their corner list matches, a dropped one reported
with: Corner layers carried by face and corner provenance or sampled on rebuilt surfaces, with a corner-list validity check
