# 0532: A reopened door's Leaf came back off now and then - the leaf sync told scenes apart by address (round 552, Pass 27)

- **Status:** done (round 552, 2026-11-16; asked by the hub: round 515's reopen test reddened unrelated PRs)
- **Requirements:** R-02 (modeling), Q-01 (tests are trustworthy)
- **Decisions:** none needed (ADR 0136 decision 5 unchanged: a leaf deleted by hand turns its door's Leaf off)
- **Model:** main session (a one-line cause), code-reviewer
- **PR:** its own, base `main`

## Goal
Round 515's test "a scene with a styled door and no leaf objects ... opens with the leaf objects made" failed now and then
on CI, on PRs that do not touch doors (#425 windows-unit, #426 and #431 macos-unit): after `newscene` and an open, the
door's Leaf was off and it had no leaf (`tests/test_blockout_leaf.cpp:876`, `:878`, `:889`). Main's own runs passed.

**Cause.** `Editor::blockout_sync_leaves` keeps, per door id, the ids of the leaves it saw at the last walk; a leaf that
was there and is gone, with its door still there, was deleted by hand, so the door's Leaf goes off (ADR 0136 decision 5).
Those ids belong to one scene, and the sync told scenes apart by the `Scene` object's address. `open_scene` makes the new
`Scene` while the old one still lives and frees the old one after; the empty scene of `newscene` is freed the same way. So
an open after `newscene` can be handed back the block of the scene before it - the same address - and the allocator
decides when (Windows' low-fragmentation heap and macOS' malloc vary from run to run). Then the remembered ids were kept,
the reopened door (the same saved id) lacked the leaf the test had removed before saving, and the sync read that as a
deletion by hand. ADR 0136's comment on the field already said "the address can repeat", but the door's id repeats too.

**For the user** it is not only a test's flake: object ids start again in every scene, so a door in the scene opened
can have the id of a door in the scene closed. When the address repeated, the opened door's Leaf was turned off and its
leaves removed (a leaf of the closed scene's id missing), or - when the opened scene had an object of that id - that
object lost its leaf mark and the door made a second leaf.

**Fix.** The sync tells scenes apart by `Scene::serial` (different for every Scene made in the process; the render caches
already key on it). Exit Play puts back the clone made at Play's start (a new serial): the ids start again, as they did
with its new address before - no deletion is read from a reset.

## Scope
- In: the sync's scene check; a test that makes the same address on purpose (the scene read into the very `Scene`
  object), so the case runs on every run and every platform.
- In (the review found them, the hub asked for them here): two more places that told scenes apart by address, each to
  `Scene::serial` with a same-address test:
  - the Render view's texture stream (`texture_stream_poll`, `render_view.cpp:282`; not reset by `open_scene`): a
    batch the closed scene started landed - its files counted, its failures published - for the scene opened where
    the closed one was;
  - the join tool's targets (`blockout_targets`, `blockout_join.cpp:211`): within a drag's frames, a scene opened where
    the closed one was with as many objects kept the closed scene's Blockout corners for the snaps.
- Out: Paint, Sculpt and Weight Paint keep their scene by address too (`paint_scene_`, `sculpt_scene_`, `wp_scene_`):
  an open ends those modes first, so no case is known; a follow-up if one is found. The round 515 test is not changed
  and not loosened.

## Likely files
- `src/editor/editor.h` (`blockout_leaves_scene_` becomes the serial), `src/editor/blockout_editor.cpp`
  (`blockout_sync_leaves`); `TextureStream::scene` and `render_view.cpp` (two lines), a test hook
  `texture_stream_start_for_test`; `BlockoutCross::scene` and `blockout_join.cpp` (one line)
- `tests/test_blockout_leaf.cpp` (`tests_round552`, at the end: the leaf and the texture stream);
  `tests/test_blockout_join.cpp` (`tests_round552_join`, at the end)

## Acceptance criteria
1. A scene whose door's leaf was left out of the saved text, read into the same `Scene` object as the scene before it,
   comes back with the door's Leaf on and one leaf object. (Fails before the fix on every run.)
2. A texture batch started for a scene is dropped, not landed, when another scene takes the same address.
3. Blockout snap corners asked for in the same frame after another scene took the same address (as many objects) are
   the new scene's.
4. Round 515's reopen test and the rest of the blockout, blockout_join, editor and physics groups pass on Windows (both
   builds) and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="(552)"` and `"door leaf (515)"`, then the blockout, blockout_join, editor and physics
  groups.
- Stress: none (no geometry changes).

## Risks and edge cases
- Undo restores a cloned scene (a new serial): the ids start again, as with a new address before.
- Exit Play puts back the clone made at Play's start (its own serial): the ids start again, as before the change.

## Documentation to update
`changelog.d/552-leaf-sync-scene-serial.md` with a USABILITY "fixed" row (no README line: no new feature).
