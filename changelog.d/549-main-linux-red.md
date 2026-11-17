## 2026-11-16 (round 549): main green on Linux again - three faults the new CI image showed

**Asked** (the hub, for the owner's main): main went red on Linux at #419's merge (068207d9) - `blendity_stress --quick`
crashed in `curved`, and "voxel GI: turning Realtime GI or its settings on redraws the views" and round 513's "draw piece
window: a 1 x 1.5 m drag ..." failed - though #419's own run was green on the same tree; every PR's `linux` check was
blocked. Find the cause and fix it in the code.

**Found (task 0529):** GitHub's `ubuntu-latest` moved from ubuntu-24.04 (g++ 13.2) to ubuntu-26.04 (g++ 15.2) between the two
runs. Three diagnostic runs there (a branch of their own, no PR; Intel Xeon 6 and AMD EPYC runners) failed the same way every
time, alone and on one worker thread; a WSL machine with the same g++ 15.2 passed every time, and valgrind found no read of
memory never written. Each failure was code that a difference of a unit in the last place tips over:

**Fixed**
- **A crash drawing a shape on a curved face (undefined behaviour).** The ring around a shape drawn on a face (its two bridges,
  `bridged_ring`) sorted the outer loop by lengths worked out again at every comparison - no consistent ordering when the
  compiler rounds its two inlined copies apart or a value is NaN, and `std::sort` then read before its array (valgrind).
  Its keys are now worked out once, NaN last, equal keys in index order.
- **Realtime GI with Sky Occlusion and Bounce both off changed a pixel.** It still traced: the blur's and the upsampling's means
  of a sky of 1 came out a rounding off 1, and a traced pass shades pixel by pixel instead of eight at a time. Nothing is traced
  now when GI adds nothing - the same picture as GI off, and the gather's time saved. And the views no longer hand the objects
  the realtime lightmaps a pass published while Sky Occlusion or Bounce was still on: until the next pass landed they shaded
  the picture (a slow machine took it before then - #431's first CI run). A new test waits for a published pass, turns both
  off and compares at once: 0 pixels differ, 43,839 with the second fix taken out.
- **A click or ray exactly on the edge two triangles share fell through.** `scene_raycast` and `ground_at` use the tolerant
  ray-triangle test task 0437 made for this (the window test's ray met the far wall's face exactly on its diagonal).
- Round 513's window test waits for the Blockout's build after a drag instead of four frames.

**Checked:** the round's groups (`editor`, `modeling`, `lighting`, `hci`, `blockout`), not the full suite: Windows with Blender's
libraries 49,681 checks, dependency-free Windows 42,288 and Linux (WSL, g++ 15.2) 42,294, 0 failed - the two tests that were red
among them. New: a ray through a quad's diagonal at 1,080 turns and offsets hits it every time (the plain ray-triangle test lets
1 of the 2,160 through on Windows, none in WSL); the curved section's octagons drawn on a 48-sided cylinder, turned and the rings
repaired, all valid; rings repaired around a NaN or infinite corner, every index in range. Stress on Windows (full),
`curved` 5,032 operations / 6 problems / 67 with faces newly on top of each other, `pushpull` 26,620 / 0 / 394,
`modifiers_ngon` 11,514 / 0 - each the same as main 068207d9 on the same machine; `curved --quick` in WSL 1,934 / 1 / 24, as
main. The proof of the crash fixed is CI's `linux-stress` on ubuntu-26.04. Code review: approve, its five minors fixed (a
realtime lightmap's bounce is still added while GI adds nothing; the test mirrors the stress's steps).

<!-- docs/USABILITY.md -->
after: | Adding a door or window meant the Create section's buttons
| A click exactly on the edge between two triangles of a face could select nothing behind it, or the object behind. (round 549) | A ray on a shared edge lands on the face. |
