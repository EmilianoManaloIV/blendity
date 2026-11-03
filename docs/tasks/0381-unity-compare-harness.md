# Task 0381: the comparison - snapshots across the board, attributes, performance; the report (round 404)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: rounds 401-405), Q-tests, Q-docs
- **Decisions:** none needed (tools and a report; the acceptance target is the plan's)
- **Model:** main session (Fable)
- **PR:** stacked on #302 (round 403)

## Asked
"Compare snapshots across the board to get accurate readings using Unity VCC's Shinano and SideRail project." The
fourth round: the harness and the measurement.

## Built
- **`bridges/unity/SdkCheck/BlendityCompare.cs`** (Unity, batch): `World` opens a scene and renders each station of a
  JSON through its Main Camera with its post-processing (PNG, sRGB) and without (EXR, linear) at 1280 x 720; `Avatar`
  puts a prefab in the studio rig and renders 900 x 1200; both write scene_look.json and timings.json.
- **`tools/unity_compare.py`**: runs Unity's side on a project copy (a VCC-managed project is refused unless it carries a
  `BLENDITY_COPY` marker), then Blendity's headless mode per station (the imported scene opened in the Blendity project,
  `unity lightmaps wait`, the Main Camera moved, `render wait`, `saverender`), then `tools/image_compare.py` per station
  with a side by side; writes report.md / report.json; exits 1 when a station misses the target.
- **`render wait`** (console; the code went in with round 402's commit, documented and tested here): holds a headless
  script till the render is done (the path tracer renders over frames), so a `saverender` after it saves the whole
  picture; it stops at 100,000 frames; the MoonRay engine isn't waited for.
- **`tools/unity_attribute_table.py`**: every component class and script a project copy uses, how often, and Blendity's
  standing (native / data / drawn / written back, with the round); DLL classes named by Unity's fileID formula.
- **The report** `docs/hci/compare-unity-vrchat-2026-11.md`: the stations' numbers, the attributes, performance, the
  verdict per audience, the residuals ranked.

## Measured (local, the user's packages; the pictures in `comparison-screens/`, not in the repo)
Shinano - every station passes: front MAE 1.00 / SSIM 0.987 / corr 1.000, back 1.17 / 0.977 / 1.000, face 2.61 / 0.944 /
1.000, hands 0.91 / 0.989 / 1.000 (with Unity's own sun direction; the mirrored sun of the first run had given 1.52-3.21). SideRail - near, not on, the target: spawn 7.03 / 0.931 / 0.989, spawn turned round
2.40 / 0.928 / 0.751 (a near-uniform dark picture), floor 4.71 / 0.937 / 0.999, ahead 6.25 / 0.928 / 0.987.

## Acceptance criteria
1. `render wait` then `saverender` gives the whole picture (the PNG and, with the path tracer, a linear HDR) - `tests_round404`.
2. The harness runs end to end on both project copies (local) and scores each station; the attribute table names every
   class of both projects.
3. The suites, the audits.

## Review (Opus, 2026-11-03) - fixed
- The harness never cleared its output folders (an earlier run's pictures scored as this one's) and ignored Blendity's
  exit code: cleared per run, the exit code and a missing Main Camera stop it; paths made absolute (Unity and Blendity run
  in their projects); the window size as main.cpp parses it; options and the stations JSON checked; a crashed comparison
  said. The linear picture is an EXR when the build writes one, compared only where both sides have it (the Radiance HDR
  path couldn't be read by image_compare.py). Unity's side restores the camera's own FOV per station and destroys its
  textures. The attribute table's class ids as Unity's reference lists them, CRLF files read. The report's wording where
  it claimed more than the numbers (SSIM ≥ 0.928; the eye textures; the stations re-open the scene).
- The rig's sun: the harness had aimed Blendity's sun at (+0.262, ...) where Unity's Euler(40, 200, 0) points at
  (-0.262, ...); measured both ways - Unity's direction takes the front station from 1.52 to 1.00, the face from 3.21 to 2.61 - and set to Unity's.

## Not done
- The SDK's control-panel checks on the world (`vrchat_sdk_world.json`) and the HCI re-run: round 405, once the residuals
  are fixed. The private Artifact page with the pictures: the side by sides were handed over as files instead (the
  pictures are the user's packages'). Shinano placed in SideRail as a station. The EXR comparison needs Blendity's path
  tracer (`--engine Path`): the rasterizer keeps no linear picture.
