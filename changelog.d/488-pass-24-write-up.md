## 2026-11-10 (round 488): Pass 24's write-up

**Asked:** "Baked lighting looks really weird here, its like it has a mirror finish and there is splotches still." -
Pass 24's last round: what the pass did about both, in one table. The owner, through the hub: "Thank you, continue and
finish all tasks."

**Added (task 0464)**
- `docs/PERFORMANCE.md`, "Pass 24 in one table": the mirror finish (the owner's two views against Unity's own
  pictures: 13.71 / 0.702 and 12.50 / 0.697 in the owner's session, where the maps were lost, 3.71 / 0.905 and 3.57 /
  0.912 now, mean difference / SSIM; a fresh import that kept its maps was already 3.75 / 0.898 and 3.67 / 0.902) and
  the splotches (147 and 133 specks before, 11 and 39 now, the bake no longer by a like-for-like pair), what each round built, the
  marks missed, the open items ranked and each round's wall time.
- Before / after pictures of the two views, in the sessions' notes folder and not in the repository (the world is its
  author's).

**Not done**
- Round 487 (light cookies, the Light Volume mist, OIDN's lightmap weights) was not run: each waits for the owner to
  ask for it.
- The lights a texel keeps by chance spread evenly - what the 11 and 39 specks left are - is a round of its own.

**Checked**
- No source, test or stress file changes; the suites are round 486's. `tools/dev/changelog.py check` passes.
