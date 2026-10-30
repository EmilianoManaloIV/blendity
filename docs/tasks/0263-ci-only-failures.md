# 0263: the CI-only and non-LTCG test failures

- **Status:** done (round 283)
- **Requirements:** R-14
- **Decisions:** ADR 0056 (tests pin behaviour, never times)
- **Model:** main session
- **PR:** #227

## Asked
The user (2026-10-28): review the test issues and check the non-passing cases on GitHub. Every PR from #218 on failed
CI on all four jobs, though the local chain (Blender's libraries, LTCG, 32 threads) passed; #189's sanitizer job failed
too; one modeling test failed in any build without LTCG.

## What was wrong
Reproduced locally with the build CI makes (`BLENDITY_NO_LIBS=1`) and without LTCG:
- **The job tray test** ("a partial value seen"): the Mesh Maps baker reported progress when a block of rows ended; with
  CI's two-core runners the blocks were large and ended together, so the bar went from 0 to done in one step.
- **The Boolean-by-name test**: the dependency-free build has no Manifold, so the Boolean refuses; the test expected
  a cut.
- **The toast test**: in a full run, a modeling test had written a missing normal map into the shared Default-Material,
  so every later test in the project raised "cannot read none.png", which the toast test counted as a leftover warning;
  on a slow runner an Info toast ("done in ...") can also follow the Fix.
- **The Edit Mode drag test** (non-LTCG only): the full and the in-place corner-normal paths inlined `normalize`
  differently under `/fp:fast` and disagreed in the last bit of one component.
- **#189's sanitizer failures**: the autopose test asserted a time (under 16 ms), and two Auto Generate tests measured
  the one-second settle with the wall clock, which a sanitizer build's frames overran.
- **A sequencer Play test** (full run, fast build): 31 frames were taken as half a second of Play.

## What changed
- `Mesh` corner normals go through one out-of-line `corner_normalize` in every path (`src/scene/mesh.cpp`).
- The baker stores its progress after each row (`src/render/mesh_maps.cpp`).
- Auto Generate's settle uses `clock_now()`, so tests set the clock (`src/editor/lighting.cpp`).
- Tests: the toast test tolerates Info toasts and prints what is left; the Boolean test accepts the dependency-free
  build's refusal; the autopose time is printed, not asserted; the voxel GI tests set the clock; the modeling test
  gives the cube its own material copy; the sequencer test runs half a second on the editor's clock.

## Tests
The whole suite in a `BLENDITY_NO_LIBS=1` non-LTCG build, and the chain's builds.

## Not done
- The PRs below #227 keep their red CI: their branches don't have these fixes; merging the stack's top merges them.
