# Task 0411: The scene's change counter (round 435)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0115 (the scene's change counter), ADR 0009 / 0015 (render hashes), ADR 0105 (the suite)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #332 (round 434)

## Asked
Pass 18's plan, round 435: a scene-wide change counter bumped by every edit path - transforms, objects made /
destroyed / reparented, components, meshes and materials touched, modifier results adopted, Inspector and `set`
edits, the undo swap; the scene render hash kept per frame on it and shared by every caller; a validator that walks
anyway every few frames so a missed bump is a counted miss, not a stale picture. Target: Shinano's hashing 0.98 ms ->
~0, 50,000 cubes' near 0.

## Built
- **src/core/edit_epoch.h:** the counter (relaxed atomic, process-wide).
- **Bumps:** `GameObject::mark_dirty`, `Scene::adopt`, both `Scene::destroy`s, `add_component` / `remove_component`,
  a modifier result adopted (`eval_generation_`), the modifier tails' and sculpt's `deform_version`, `Mesh::touch`,
  `Mesh::settle`, `Material::touch`, every animated value written (`write_anim_value`: clips, the Sequencer, the
  Curve editor's key drags, Preview's restore); in the editor `mark_changed`, the Inspector's reflector when it changed
  a field, every field set by name (`set`), constraints that moved something. `mark_dirty` bumps once for its subtree
  (its recursion is `mark_dirty_tree`).
- **`Editor::scene_render_hash`:** kept while the counter, the scene (pointer, serial) and the tail (environment,
  bake / texture / probe generations) hold still and nothing changes behind the counter (Play, a clip or the
  Sequencer playing, a stroke, Live Link clients); walked outside a frame; checked by a walk on the first call of every 8th frame - a
  disagreement counted (`Scene hashes kept / missed` in the Profiler), warned once, the walked hash used.

## Measured
`views435.py` (bench_world's `--windowed-check`), the views on the card, before / after in one session:

| view | before: frame (hashing) | after: frame (hashing) |
|---|---|---|
| Shinano, Scene view | 8.44 ms (0.98) | 6.52 ms (**0.12**) |
| Shinano, Scene + Game view | 6.17 ms (0.96) | 5.09 ms (**0.12**) |
| 50,000 cubes, Scene view | 96.4 ms (4.38) | 92.1 ms (**0.55**) |

The hashing is what this round changes (what is left is the every-8th-frame check and the tail). The frames' other
differences are the card's passes and presenting, which vary run to run (Shinano's card passes 2.70 / 1.56 ms in the
two runs). Gathering (1.86 / 21.9 ms) and the UI are round 436's.

**The fuzz with the check on every frame:** 6,000 frames, 0 changes the counter missed, 0 stale pictures; a five-minute
soak (`BLENDITY_FUZZ_MINUTES=5`): 116,532 frames, no crash, 0 stale pictures, 0 changes the counter missed.

**Found on the way:** the Inspector clamps a field written outside its range (a light's Intensity above 8) on its next
draw without recording an edit - a second write past the counter (one miss, then kept); left as it is.

## Tests (tests_round435)
- Each funnel moves the counter; nothing done leaves it.
- Idle frames keep the hash (walked at most once in eight frames); a `set` on a light walks it at once and changes it;
  Undo walks it again (the restored scene is a clone: a new hash); no miss.
- A field written past every funnel is caught by the next check, counted as one miss, the walked hash used.
- Play makes every call walk; Stop ends it.

## Review (Opus)
Changes needed, fixed: the Sequencer's playback and the Curve editor's key drags wrote animated fields past the counter
(`write_anim_value` now bumps; the Sequencer playing is volatile); the UV clustering's heap stamp had been given a bump
(not scene state - removed); `mark_dirty` bumped once per descendant (now once per call); `set` bumped when it set
nothing; the comment on writes between frames said more than the code does (reworded).
