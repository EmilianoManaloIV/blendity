# 0566: Preserve painted face-corner colours through mesh edits

- **Status:** done
- **Pass / round:** 29 / 584 (handoff item 14)
- **Requirements:** R-02, painting on meshes under ADR 0152
- **Decisions:** amendment to ADR 0152, operator rules below
- **Model:** Codex planning task for cross-cutting geometry, followed by test and review roles
- **PR:** pending

## Goal

An artist can edit, evaluate, apply, join and separate a painted mesh and keep distinct RGBA values on either side of a shared vertex. The release discards valid Face Corner layers when the corner list changes.

## Scope and ownership

| Piece | Owned files and responsibilities |
|---|---|
| Corner construction and editing | Corner-related code in `src/scene/mesh{.h,.cpp,_internal.h,_cleanup.cpp,_tools*.cpp}` and `mesh_tools_loop.*`. Extend the existing FaceBuilder and direct topology writers. |
| Evaluated and applied meshes | Colour handling in `src/scene/modifiers.cpp`, `scene.cpp`, `vertex_groups.cpp`, `boolean.cpp`, `subdiv_osd.cpp`, `subdiv_chain.*`, `dyntopo.cpp` and its stroke-state header `dyntopo.h`, `dynamesh.cpp`, `quad_remesh.cpp`, `decimate.cpp`, and the corresponding editor operation entry points including `src/editor/object_ops.cpp` and `vertex_paint.cpp`. |
| Verification and docs | `tests/test_vertex_paint.cpp`, the matching geometry stress section in `stress/stress_main.cpp`, ADR 0152, the existing Vertex Paint lesson in `src/editor/lessons.cpp`, this spec and `changelog.d/584-corner-colours.md`. |

Start from v1.0.0. The coordinator grants any additional file and gives the 14.52 investigation first use of any shared mesh source, test or stress file. Continue on disjoint files until its fix is integrated. Leave renderer/export sampling, scene format, Point transfer conventions and parked branch 0562's lesson-string changes outside the round.

## Transfer rules

Use the old face and corner indices and construction weights already known where each output polygon is built. Express the transfer once per output corner and reuse it for every valid Corner layer. Map directly from topology even when UVs are missing, identical across a seam, or packed differently. Keep the existing corner-signature validity check. Set the new signature only after writing the mapped values. A mesh without valid Corner layers takes the existing geometry path without allocating transfer tables.

| Operator family | Result |
|---|---|
| Retained, deleted, reordered or copied faces | Copy each surviving corner exactly, including alpha. Duplicate, split, mirror, array, decimate and winding changes follow each face's permutation. For dissolve/merge, each surviving boundary corner follows the input face on its outgoing boundary edge. Keep adjacent faces independent. |
| Cuts, subdivisions, inset, extrusion, bevel and other generated geometry | For points along an input edge, linearly interpolate the two corners on that face. Face centres use face-corner weights. Catmull-Clark interpolates colour within each parent face, with its corner, edge-midpoint and face-centre samples. Caps copy their contributing face. Side strips, bridges and bevels use their own generating edge loops, sampling corresponding ends at the construction fraction. For fills whose topology supplies only a boundary, use the adjacent face at each boundary edge. With no painted source for a generated corner use opaque white. |
| Boolean, remeshing, Dyntopo and modifier application | Boolean boundaries sample the contributing operand's source face, including reversed cutter faces of a difference. For remeshing without exact ancestry, select a source surface triangle for each output face by nearest point to its centre, resolve equal distances by best aligned face normal then source index, and sample its corners using clamped triangle barycentrics. Use existing spatial acceleration or equivalent bounded lookup. Prefer explicit edge/face ancestry for Dyntopo splits and collapses. Keep these rules through evaluated meshes, caches and Apply. |
| Join and Separate | Join unions valid layers by source name and domain. A missing side supplies opaque white. Existing attribute lookup requires unique names across domains. Keep the destination name and give the incoming conflicting domain a deterministic suffix, starting with `.001` and skipping occupied names. Keep this mapping across all operands. Keep destination active/render choices, including unset. Separate carries each selected face's corners by index and preserves both layer selections on each output. For Boolean operands use the same layer matching and destination selection rules. |

## Acceptance criteria

1. Exercise each operator family from the ADR 0152 inventory, including each Separate mode and both evaluated and applied topology-changing modifiers. Use adjacent faces with a common vertex, contrasting colours, different alpha, two Corner layers and a Point layer. Assert result values by face and corner, layer names/domains, valid signatures and active/render identity.
2. Verify exact, bit-preserving copy on reordered equal-size topology. For generated samples assert independently calculated RGBA values within floating-point tolerance. Test an unpainted source, cross-domain name collision, and a painted Boolean cutter. Remesh a two-sided painted fixture and verify the face-side selection rule including ties. Cover the library and dependency-free implementations where they differ.
3. Drive Join, Separate and a redoable Edit Mode operation through the editor, including undo and F9. Re-read the copy-on-write mesh after each edit. Undo restores colours and selections exactly. Successful carries produce no dropped-layer warning. Keep warning coverage with a genuinely invalid layer, replacing the release's expected-drop assertions for supported edits.
4. Unpainted meshes keep identical topology, positions, UVs and material assignments. Existing Point tests pass. Degenerate, empty and non-manifold cases yield a valid result or a clean refusal. Stress problem and newly overlapping face counts do not rise.

## Tests and checks to run

Run new filtered cases first, then the full local chain on Windows and Linux because topology writers cross many test groups. Include the Windows libraries and dependency-free builds. Exercise the geometry stress sections `pushpull`, `modifiers_ngon`, `curved` and `editor` against the release baseline. Use only scratch projects, caches, output and trash. Coordinate build slots and timed windows. Record check totals, stress counts and CI results.

## Documentation to update

Amend ADR 0152 with the verified operator-by-operator transfer table and keep its existing file-format and Point rules. Update the Vertex Paint lesson to demonstrate a seam after editing. Put the user-visible change, README line, architecture and usability updates, and measured stress results in the round's changelog fragment.

## Verification record

The reviewed source was built on the command-sweep top at `4dbfc99f`. Its fast-forward to `e0f0ed86` changed the engineering guide, workflow handoff, task 0568 and its changelog fragment. The subsequent fast-forward to `3f05393a` adds project role definitions, workflow handoff, task 0569 and its changelog. The compiler inputs, generated sweep, dependencies, build scripts and runtime assets are identical across all three stack revisions.

The full Windows stress comparison completed with exit 0. In the following table each pair is v1.0.0 before and this round after.

| Full section | With Blender libraries | Dependency-free Windows |
|---|---|---|
| Push/Pull | 26,620 operations both times, zero problems, 394 results with overlaps before and after | Same |
| Odd meshes | 11,514 to 11,540 operations, 510 shapes, zero problems | 11,514 to 11,540 operations, 450 shapes, zero problems |
| Curved meshes | 5,032 operations, six problems, 67 overlapping results before and after | 5,032 operations, six problems, 66 overlapping results before and after |
| Projected draws in Curved | 236 draws, 82 refused, zero problems and zero overlapping results before and after | Same |
| Editor | Each run finished 6,000 frames and 10,621 input events with zero crashes, stale pictures or missed changes | Same |

The six release-baseline curved problems are zero-area faces after Bevel (one), Merge by Distance (one) and Recess (four). Odd meshes now checks one painted subdivision on each of the 26 mesh fixtures.

The frozen test file has SHA-256 `FF81BE19CB2DB83E395F23D7E588B633C15D15F7F2DD4B5D53D7B5DB3DEB7241`. The independent test engineer checked all twelve Boolean walls by signed axis and extent, clamped barycentric samples, source-order ties, a three-operand name collision, and unchanged geometry on unpainted input. Independent review approved this exact revision. Full suites ran across 25 groups in each configuration. A final test-only edit affected modeling, so its entire group was rebuilt and rerun. The other 24 groups used identical code and inputs.

| Build | Full run before final test-only edit | Previous modeling | Frozen modeling | Frozen focused | Verified checks across 25 groups |
|---|---:|---:|---:|---:|---:|
| Windows, Blender libraries | 134,051 | 33,954 | 34,178 | 27,428 | 134,275 |
| Windows, dependency-free | 125,020 | 31,701 | 31,855 | 25,163 | 125,174 |
| Linux, dependency-free | 125,045 | 31,701 | 31,855 | 25,163 | 125,199 |

All groups and focused checks passed. Source coverage reported 398 entries and zero empty entries. Changelog application, the command inventory, the HCI and component audits, and all 182 handbook citations passed.
