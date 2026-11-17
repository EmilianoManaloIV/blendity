## 2026-11-16 (round 552): A reopened door kept its Leaf - the leaf sync tells scenes apart by serial, not address (task 0532)

**Asked** (the hub, 2026-11-16): round 515's test "a scene with a styled door and no leaf objects ... opens with the leaf
objects made" reddened unrelated PRs now and then (#425 windows-unit, #426 and #431 macos-unit); find the cause, a small
PR on `main`, without loosening the check.

**Fixed (task 0532)**
- **The cause:** the leaf sync remembers, per door id, the leaves it saw at its last walk; one gone with its door still
  there was deleted by hand, so the door's Leaf goes off (ADR 0136 decision 5). It told one scene from the next by the
  `Scene` object's address, and an open after `newscene` can be handed back the closed scene's block - when, the
  allocator decides (Windows' and macOS' heaps vary from run to run). Then the closed scene's leaf ids were kept, and
  the reopened door (the same saved id) lacked a remembered leaf: Leaf off, no leaf. Ids start again in every scene, so
  a user opening another scene could meet it too - a door of a repeated id lost its Leaf, or an object of the closed
  leaf's id lost its leaf mark and the door made a second leaf.
- **The fix:** the sync keys on `Scene::serial` (different for every scene made in the process; the render caches
  already do). Exit Play puts back the clone made at Play's start (a new serial), so the ids start again there, as
  before the change.
- **Two more places told scenes apart by address** (the review found them; the hub asked for them here), now by serial
  too: the Render view's texture stream - a batch the closed scene started landed for the scene opened where it had been
  (its files counted, its failures published) - and the Blockout snaps' kept corners, which in the frames of a drag kept
  the closed scene's corners for a scene opened at its address with as many objects.

**Added:** tests that put another scene at the very address the editor's scene had - the same address on purpose - so
each case runs on every run: the door's leaf (fails before the fix on Linux, at the CI's checks), the texture batch
dropped, the snaps' corners collected again. Round 515's test is unchanged.

**Checked:** the leaf test fails without the fix (Linux: Leaf off, no leaf - the CI's checks), and the stream test
failed on a Linux build that still had the old stream check; all pass with the fixes. The round's groups, 0 failed on
each: with Blender's libraries (Windows) blockout 30,107 checks, blockout_join 512, editor 3,231, physics 208;
dependency-free Windows 22,857 / 511 / 3,205 / 190 and Linux 22,860 / 511 / 3,205 / 190. The coverage map is clean; no
stress (no geometry changes).

<!-- docs/USABILITY.md -->
after: | Opening a door rebuilt the wall at every angle, and an open leaf cut whatever stood in its swing. (round 515)
| Now and then, opening a scene turned a door's Leaf off and removed its leaf, or gave a door a second leaf - the door's id was a door's of the scene closed before. (round 552) | The leaf sync tells scenes apart by their serial, not their address in memory: a scene opened is never read as the one closed. |
