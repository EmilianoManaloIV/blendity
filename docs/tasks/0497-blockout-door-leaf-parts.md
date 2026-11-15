# 0497: A door's leaf as a part that swings (round 515, Pass 23b)

- **Status:** done 2026-11-15, with deviations: export writes each leaf as its own node beside the Blockout's (the
  exporters keep no parents; ADR 0136 decision 8 rewritten); the Unity bridge has no test; a leaf's collider is kinematic
  through a Rigidbody (decision 7); round 513 is the HCI worker's, so this PR is a stack of its own on `main`; the tests
  are in `tests/test_blockout_leaf.cpp` (test_blockout.cpp reached MSVC's object-section limit). (Implemented 2026-11-14; approved 2026-11-14 with ADR 0136 - the owner, through the hub: "Approve 514 and 515"; asked as "Doors
  should also come in parts, so I can "swing" the door open without modifying the mesh so much."). The three choices
  below were taken as recommended: the leaf is dynamic (the lightmap baked with the doorway open), revolving wings only
  with Leaf on (off by default), turning a leaf by hand writes Open Amount back.
- **Requirements:** R-02 (modeling), U-01, C-02, R-09 (export to engines)
- **Decisions:** ADR 0136 (new: a leaf is its own object); ADR 0129 amended by it
- **Model:** main session or implementer for the leaf objects and their sync; test-engineer; code-reviewer; the HCI
  worker reviews the Inspector rows (the hub)
- **PR:** its own, base `main`, stacked on round 513's; after round 513, before round 503

## Goal
Opening a door is turning its leaf, not rebuilding the wall: a styled door's leaf (Single), leaves (Double), sliding leaf
or revolving wings are separate objects with their pivot at the hinge, which Open Amount - or the Rotate tool, or an
animation - turns, live. The wall keeps a clean doorway and nothing in front of it is cut.

## Scope
- In:
  1. ADR 0136 decisions 1-10: leaf objects (`BlockoutLeaf`), the doorway without the leaf, no swing cut, pivot at the
     hinge, Open Amount as a transform write, both-ways sync, the door owning its leaves, Bake / Unbake, dynamic lighting
     and a kinematic Box collider, export with the pivot, windows unchanged, old scenes converted on open.
  2. **Styles:** Single = one leaf hinged at the chosen jamb; Double = two leaves hinged at both jambs, each half the
     opening; Sliding = one leaf that slides along the wall's face (Slide Direction); Arched = one leaf, its top
     square at the springing line (the arch above stays open - say so) or closed-only as now; Revolving = one "Wings"
     object (four wings in a cross) turning about the drum's axis, made only when Leaf is on (default **off** for
     Revolving, so the drum stays the "space of a revolving door" the owner asked for in round 499); Opening Only = no
     leaf.
  3. **Inspector (HCI):** the door's own rows keep Open Amount, Hinge Side, Slide Direction and Leaf; a selected leaf
     object shows its door's Open Amount and Hinge Side (editing them edits the door), plus a line "Made by Door 'X' -
     its shape follows the door's numbers". No advice that only points at the Console; every row reachable by keyboard
     (ADR 0145).
  4. **Console:** `blockout set open|hinge|leaf` as now; `blockout leaf select` selects the selected door's leaves (so
     scripts can turn them); tests drive the Rotate tool with real events.
  5. **Learn:** "Doors with a style" gains the leaf: Try-it - swing a leaf with Open Amount, then with the Rotate tool, and
     see the Inspector follow.
- Out: casement windows as parts; door hardware; animations made for the user (an Animator / clip is theirs); a leaf
  that collides dynamically by itself.

## Likely files
`src/scene/blockout_object.h/.cpp` (BlockoutLeaf, the doorway without the leaf, removing the swing cut, leaf meshes),
`src/editor/blockout_editor.cpp` (keeping leaves in step, the both-ways sync, Bake / Unbake), the export path that skips
pieces (the leaf under the Blockout's object), `adjust_last.cpp`, `lessons.cpp`, `panels.cpp` (the leaf's Inspector line,
a few lines), `tests/test_blockout.cpp`, `stress/stress_blockout.inc`, ADR 0136, ADR 0129 (a pointer to 0136).

## Acceptance criteria
1. A new Single / Double / Sliding door (button with real events, console) has 1 / 2 / 1 leaf objects under its piece,
   each with its origin on its hinge (Sliding: closed place), upright; Opening Only and a Revolving door with Leaf off
   have none; Leaf off removes them, Leaf on makes them.
2. Changing Open Amount 0 -> 1 in twenty steps asks for **no** Blockout build and leaves the Blockout's mesh bytes the
   same; the leaf turns 90 degrees (Sliding: moves its width) about its hinge; a live drag of Open Amount keeps the
   frame within budget (a stress row, untimed in the suite).
3. Turning a leaf with the Rotate tool about its Y by 45 degrees (real events) sets its door's Open Amount to 0.5 in the
   same undo step; one undo returns both.
4. The doorway: an open door cuts nothing in front of the wall - a column standing in the swing keeps all its faces; the
   status says nothing about cuts.
5. Moving the door along its wall, moving the wall's room, turning the Blockout, duplicating, deleting and undoing the
   door: the leaves follow / are duplicated / go / come back; deleting a leaf by hand turns its door's Leaf off (one undo).
6. Bake to Mesh keeps the leaves as separate enabled objects at the same world place; Unbake returns them under the door.
7. A leaf is not Contribute GI, has a Box collider fitted to it, kinematic in Play mode; the lightmap bake of a room with a
   closed door bakes the wall with its doorway (the leaf not in the bake).
8. Export (OBJ / FBX / GLB, and the Unity bridge where the suite covers it): the leaf is its own node under the Blockout's
   object, its pivot at the hinge, its rotation the current Open Amount.
9. An old scene with a round-499 styled door opens with leaf objects made and the door looking the same; a scene from
   before round 499 opens to the same mesh bytes.
10. The round's groups (blockout, editor, hci, physics, lighting) on the three builds; the coverage map; the stress
   section's rows other than the replaced door rows unchanged.

## Risks and edge cases
- A leaf object renamed or re-parented by the user: it stays the door's only while it is the door's child; moved out, it
  becomes an ordinary object (BlockoutLeaf dropped) and the door makes a new one - or Leaf off; say which.
- Scale on the Blockout or the door (ADR 0135: scale goes into numbers): the leaf's mesh follows the numbers, its object
  scale stays 1.
- A Double door where one leaf is turned by hand and the other is not: Open Amount follows the last one turned.
- Play mode: the leaves are scene objects and move by scripts; leaving Play mode restores them (as any object).
- Undo of a Style change that removes or adds leaves; the hash mark must not loop (writing Open Amount from a leaf must
  not ask for a build).

## Documentation to update
`changelog.d/515-blockout-door-leaf-parts.md` (README line, USABILITY row: "an open door cut what stood in its swing" ->
fixed; ARCHITECTURE: BlockoutLeaf); ADR 0136 accepted; ADR 0129 pointer; this spec marked done.

## For the owner to decide (with this spec)
1. **Baked light through a closed door:** the leaf is dynamic, so the wall's lightmap is baked with the doorway open
   (light passes as if the door were open; the leaf itself is lit by probes) - the usual engine way. Or bake the leaf in at
   its saved place as a static occluder (then it cannot move without a re-bake). Recommended: dynamic.
2. **Revolving wings:** made only when Leaf is on, default off (the space only, as round 499's answer) - or on by default.
3. **Turning a leaf by hand writes Open Amount back** (recommended) - or Open Amount only, and a hand-turned leaf is just
   an object turned.
