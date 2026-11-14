## 2026-11-13 (round 502): modifiers live on a Blockout (task 0486, ADR 0129 amended)

**Asked** (the owner, 2026-11-11, of Pass 23b's proposal): "Yes, modifiers should work on the blockout."

**What was there:** a modifier added to a Blockout object was already evaluated over the mesh its pieces build (the
build lands on the MeshFilter; the stack runs over it as over any mesh), and Bake to Mesh and Unbake already kept the
stack - but none of it was specified or tested, a small stack ran on the main thread in the frame that adopted each
landing of a drag, a click on a mirrored or bevelled Blockout named a piece from faces that were not the drawn ones,
and Apply was refused without a way on.

**Changed**
- **The stack follows the pieces on a worker.** Once a Blockout shows a result of its stack, every later evaluation -
  after each landing of a build, after a modifier's number changed - goes to the evaluation worker whatever the stack's
  size (`blockout_stack_follows` in `src/scene/scene.cpp`, through ADR 0107's hook); the last result stays up until
  the new one is adopted, so the bare mesh is never shown in between. The first evaluation (a modifier just added) and
  a stack that reads another object (Boolean, Curve) follow the old rule. The worker's copy of the object leaves the
  Blockout component out (its face origins were copied with it). In Play a Blockout's stack is evaluated on the spot,
  so colliders read the exact mesh.
- **What reads the modified mesh:** the Scene and Game views, Generate Lighting, colliders and the scene's ray casts,
  the OBJ / FBX / GLB exports. **What reads the pieces:** the handles, the outlines, the Draw Piece tool, the faces'
  origins.
- **Picking:** a click or a box on a Blockout whose shown modifiers change the faces (anything that is no deformer)
  selects the Blockout and the status says "modifiers change the faces: click selects the Blockout" (once while it
  stays selected). While the worker is still making the stack over a new landing, no piece is named either. With deformers only, the piece is named from the DRAWN face - the ray is cast at the
  deformed mesh, whose faces are the base's in the base's order, and the hit is taken back to the base's flat face
  to name the piece among several that made one face (`blockout_pick_piece_shown`). With no modifier it is as before
  (`src/editor/blockout_modifiers.cpp`, the two call sites in `scene_pick.cpp`).
- **A drag whose landings come faster than its stack evaluates** still shows results: a finished evaluation of a
  Blockout is adopted although the pieces moved on (it is of a later landing than what is shown), and the newest is
  asked for at the next draw (`eval_jobs_poll`).
- **`blockout wait`** also brings the stacks to the meshes that landed (`Editor::blockout_stacks_settle`; one line
  in the command, cleared by the hub), so a script's next command reads the modified mesh of the pieces as they are.
- **The stack's cache key holds the base mesh's identity** (`uid`), not only its address and version: a Blockout's
  base is a new mesh at every landing, and a new mesh standing at the address of one that went, with the same version,
  passed for it - the result of an earlier landing stayed up as if it were current (seen once in the drag test).
- **Limits, said plainly:** between a landing and the adoption of its stack's result, ray casts, bounds and the
  views read the result before it (a frame or a few); export, Generate Lighting, Apply and Play's start wait and
  evaluate exactly. More than four evaluations at once, a stack that reads another object, and any Blockout stack
  in Play run on the main thread.
- **Apply and Apply All on a Blockout** are refused with a "Bake to Mesh" button on the message (`blockout bake`); the
  baked object keeps the stack and Apply works there. The Inspector's modifier section on a Blockout says "Live over
  the Blockout's result: runs again when a piece moves."
- Objects that are not Blockouts evaluate their stacks as before.

**Fixed (older than this round, found by its tests):** Apply All in the Inspector on any mesh whose last component
was a modifier crashed - the stack was applied while the modifier rows were still being drawn from the old list. It
now runs after the rows.

**Not changed, seen by the tests:** an Array with a NaN or infinite offset gives a structurally valid mesh with
non-finite points, on any object (a follow-up for the modifiers, not this round's).

**Added:** the Learn lesson "Modifiers on a Blockout" (Bevel the whole building, Mirror half a plan, Array a bay); the
test group `blockout_modifiers` (`tests/test_blockout_modifiers.cpp`); the stress section `--only blockout_modifiers`
(`stress/stress_blockout_modifiers.inc`).

**Numbers** (`blendity_stress --only blockout_modifiers`, a timed window on a quiet machine, with Blender's
libraries; `interior 100`, a door moved every frame for 120 frames, one every 16.7 ms, workers on):

| stack on the Blockout | frame median / worst ms | move to landing median ms | move to evaluated mesh median / worst ms | results adopted in the drag | wait after the last move ms | main-thread evaluations | frames of the bare mesh | faces base / shown |
|---|---|---|---|---|---|---|---|---|
| Bevel (defaults) | 5.31 / 8.75 | 5.58 | 5.61 / 57.54 | 95 | 9.79 | 0 | 0 | 393 / 1693 |
| Mirror (X) | 5.04 / 7.02 | 5.17 | 5.26 / 56.73 | 98 | 11.86 | 0 | 0 | 393 / 786 |
| Subdivision Surface (1 level) | 4.83 / 6.33 | 5.02 | 5.03 / 56.59 | 97 | 17.89 | 0 | 0 | 393 / 1790 |

After each drag the evaluated mesh is the stack run by hand over the pieces' mesh, 0 problems. `--only blockout`:
every sweep row's problems 0 as in round 497 and no count of faces on top of each other above round 497's (the kernel
is not touched). `--only modifiers_ngon`: 11,514 operations on odd meshes, 0 problems; 510 shapes built, 0 problems.

**Checked:** the groups `blockout_modifiers` (40 tests), `blockout`, `modeling` and `editor` - with Blender's
libraries on Windows 16,460 checks, 0 failed; dependency-free 14,611 (Windows) and 14,611 (Linux), 0 failed. `tools/test_coverage_map.py --check`: 378 entries, 0
empty. On the dependency-free builds Decimate at its default ratio leaves the test's grid-path mesh at 658 triangles (the test
asks for fewer only where the exact path builds the base; a question for the modifiers, not this round). Not covered by a test: the cache key's identity fix directly (no test could force a mesh to land at a freed
mesh's address), the Inspector's dim line, the lesson's text.

<!-- docs/ARCHITECTURE.md -->
after: | `src/editor/blockout_editor.cpp`
| `src/editor/blockout_modifiers.cpp` | Round 502: a Blockout that carries a modifier stack - `blockout_stack_changes_faces`, `blockout_pick_takes_whole` (picking names a piece only while the stack keeps the faces), `blockout_pick_piece_shown` / `blockout_box_select_shown` (the piece of a drawn face through deformers), `blockout_stacks_settle` (the stacks brought to the landed meshes now: `blockout wait`). The stack itself is `GameObject::evaluated_mesh`'s; `blockout_stack_follows` (scene.cpp) sends it to the evaluation worker after every landing. |

<!-- docs/USABILITY.md -->
after: | A Blockout
| A Blockout with a Mirror or a Bevel: a click named a piece from faces that were not the drawn ones; Apply refused with no way on | fixed (round 502): the click selects the Blockout and says why; Apply offers Bake to Mesh |
| Apply All on a mesh whose last component is a modifier crashed the editor | fixed (round 502): it runs after the Inspector's rows are drawn |
