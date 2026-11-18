## 2026-11-18 (round 563): A kept draw list holds its card stand-ins

**Asked:** Pass 28, the follow-up of round 520's review (PR #457): a `seen` stamp, so a kept draw list can never be left
with a card stand-in that was let go.

**Fixed (task 0545)**
- A skinned mesh the card poses is drawn from a stand-in that is let go after 600 gathers without it. A view that kept
  its draw list (nothing in the scene changed) used the list in place of a gather, so its stand-ins were never stamped:
  with another view gathering every frame (a Game view with an LOD Group or a Camera Flatten) they aged out, the kept
  list was dropped and the skins went to the card again - a hitch about every ten seconds. A kept list now notes the
  stand-ins its items point at and stamps them each time it is used (`skin_list_stands`, `skin_stamp_kept` in
  `render_view.cpp`), so a list in use is never dropped for age. A list that is not drawn still lets them go, and is
  gathered again before it is read.

**Checked:** the groups the change touches - with Blender's libraries on Windows: gpu on the card 1,530 checks (its only failure round 508's
bake test, by a toast: round 564), editor 3,862 and lighting 9,926 with 0 failed, round 520's stand-in tests in animation;
dependency-free on Linux: gpu 138, editor 3,843, lighting 9,872, 0 failed. Review: approve.
