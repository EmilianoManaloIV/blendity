# 0545: A kept draw list stamps its card stand-ins as seen

- **Status:** done (round 563)
- **Requirements:** Q-performance, Q-robustness
- **Decisions:** none needed (ADR 0146 1(c) and ADR 0115 as they stand; this closes the follow-up of round 520's review)
- **Model:** main session (worker W2 of Pass 28)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/461

## Goal
Round 520's review (PR #457) found that Solid view's kept draw list could point at a freed card stand-in after a prune;
that was fixed by dropping every kept list whenever a stand-in is let go (`skin_forget_kept_lists`). Its second pass left
a follow-up: a stand-in used only by a kept list is never stamped as seen (`SkinStand::seen` is written by a gather, and
a kept list is used in place of one), so while another view gathers every frame - a Game view with an LOD Group or a
Camera Flatten, beside a Scene view that keeps its list - the stand-in ages out after 600 gathers. The kept list is then
dropped, gathered again, and the skin is sent to the card again: safe, but a hitch about every ten seconds.

Afterwards: a view that draws from a kept list holds its stand-ins as a view that gathers does. A stand-in is let go by
age only when no list in use points at it, and a kept list in use is never dropped for age.

## Scope
- In: `Editor::ItemsCache` notes the stand-ins its kept list's items point at; `collect_items_ref` and `solid_items_ref`
  stamp them (`seen = skin_builds_`) each time the kept list is returned in place of a gather.
- Out: how stand-ins are made, replaced or pruned (`gpu_skin.cpp` is unchanged: every stand-in let go still clears the
  kept lists first, which is what makes the noted pointers safe to read); the 600-gather age itself.

## Likely files
- `src/editor/render_view.cpp` (`skin_list_stands`, `skin_stamp_kept`, `collect_items_ref`, `solid_items_ref`)
- `src/editor/editor.h` (`ItemsCache::stands`, two test hooks)
- `tests/test_gpu.cpp` (group `tests_round563_gpu`)

## Acceptance criteria
1. Solid shading of a posed skin with its list kept, drawn every frame while 700 gathers of another view count: the list
   is not gathered again, no stand-in is let go (before: let go after 600, the list made again). Without a card.
2. The same on the card: no skin is sent again (`FrameProfile::skin_uploads` holds).
3. A kept list that is not drawn does not hold its stand-ins: after 700 gathers elsewhere they are let go and the list
   with them; the next frame gathers before anything is read.
4. A stand-in dropped between two frames (every one, as age does) on the card: the next frame gathers, sends the skins
   again and shows the same picture, pixel for pixel.
5. Round 520's "a kept Solid list is made again when its stand-ins are let go" still passes; gpu, editor, lighting and
   animation groups show no regressions on Windows and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 563"`, groups gpu, animation, editor, lighting (Windows with the card, Linux).
- Stress: none (no geometry, no timed row: the counters say what was sent).

## Risks and edge cases
- The noted pointers are raw: they are read only while the list is valid, and `skin_forget_kept_lists` clears that on
  every path that lets a stand-in go or replaces one in its slot (age, the six-a-object limit). A list that is not kept
  (a settling rest, an LOD level, a Camera Flatten) notes none.
- Verify mode gathers on a hit, which stamps by itself.
- A stand-in shared by two views is stamped by either.

## Documentation to update
- `changelog.d/563-kept-list-seen-stamp.md`.
