# Sculpt pointer workload and action inventory

Round 643 covers the production Sculpt actions under [task 0625](../tasks/0625-sculpt.md) and the responsiveness requirement [Q-06](../requirements.md). The inventory is for the pass-wide survey in task 0630.

## Reproduce

Compile `tools/perf_sculpt_0625.cpp` with the release editor settings and link it as the sole entry point against the candidate's production editor objects and platform libraries. These are the `LIB_SRC` objects in `build.sh` or `LIBSRC` objects in `build.bat`. From the repository root, run the following in the Hub's exclusive timing window. Pass a new scratch output directory outside the checkout.

```text
python tools/perf_sculpt_0625.py <perf_sculpt_0625 executable> <scratch-directory> --pairs 3
```

To rebuild the Linux dependency-free probe, use a fresh checkout with an empty object directory. The release build records the actual compile flags in `flags.txt`. The probe links the same production objects as the editor with its own `main`.

```bash
BLENDITY_NO_LIBS=1 BLENDITY_NO_OPTIX=1 ./build.sh release app
obj=build/obj_release_linux
read -ra flags <<< "$(cut -d' ' -f3- "$obj/flags.txt")"
product=()
for f in "$obj"/src_*.o "$obj"/extern_*.o; do
    [[ $f == */src_app_main.cpp.o ]] || product+=("$f")
done
c++ "${flags[@]}" tools/perf_sculpt_0625.cpp "${product[@]}" \
    -lX11 -ldl -static-libstdc++ -static-libgcc -o /tmp/perf_sculpt_0625
```

The driver alternates normal process affinity and a single permitted logical CPU. Pass `--cpu NUMBER` to choose a specific CPU. The child editor uses the CPU viewport and gets a fresh scratch project, trash and preferences directory for each run. Each run's `sculpt.log` contains per-stroke press time, total move time, release time, move p95 and output checks. The 32 individual move timings are discarded after the percentile is calculated. The driver writes `summary.json` with the executable's SHA-256, CPU sets and timing medians after the test checks and the per-row output comparisons pass. Windows and Linux are supported. Logical-CPU affinity limits the scheduler threads can run on and leaves the host clock and memory configuration in place.

To inspect a single case, run `perf_sculpt_0625 <new-scratch-directory> <case>`, for example `drag-rect`. The Sculpt unit group includes a shorter Draw and Mask check using the shared pointer fixture. Run the existing `blendity_stress --only sculpt_paint` separately for its 100k- and 500k-face workloads. That stress section's Mask output column counts moved positions, so a Mask row's zero movement is expected. The round 643 benchmark checks actual mask changes.

## Workload and checks

Each case begins on a roughened sphere with a quarter mask over most of the surface, a fully held patch, two face groups and corner UVs. Idle and Draw controls include a lower subdivision level. A stroke uses a real Scene view press, 32 move events along a short zigzag across the front of the sphere and a release through `step_frame_headless`. The viewport's object ID must be present under the initial pointer. The second stroke starts on restored geometry in the same editor, with a four-pixel path shift. The fixture and output checks run outside the measured event frames. A fresh case still includes scene setup and a viewport warm-up before its first measured press.

The test checks actual position changes for geometry brushes and actual mask changes for Mask, finite vertices, retained active selection and Sculpt mode, and the expected undo delta count. It compares face offsets, corner indices, group IDs, face materials, face smoothing and UVs before and after the stroke, then checks exact position and mask restoration on undo. Each row records its dab count and before/after digest, and the driver compares those outputs across runs of the same binary. Timing columns cover the press frame, all move frames, release frame and the per-stroke move p95. The move interval includes per-frame measurement bookkeeping. The timings cover headless input and frame rendering. Tablet drivers, display presentation and GPU input-to-display latency require desktop measurements.

## Production action inventory

The action sources are [Modeling Tools → Sculpt](../../src/editor/panels.cpp), the [Sculpt Wheel](../../src/editor/quick_menu.cpp), [key actions](../../src/editor/keymap.cpp) and the [console and pointer dispatcher](../../src/editor/sculpt.cpp). A numbered test group below refers to the corresponding `tests_roundNNN` function in [the Sculpt suite](../../tests/test_sculpt.cpp).

The exact-key handoff in C:/blab/pass36-hub/0625-exact-key-test-map.tsv covers 136 routed keys. Of these, 52 have paired action measurements, 26 have focused checks and 58 have explicit entry-point gaps. For example, the brush wheel, breadcrumb and mode-button routes may reach a verified tool without a separately checked click on that control.

| Production action or control | Fixture, selection and check | Coverage gap |
| --- | --- | --- |
| Sculpt on/off, Escape, entering Edit or Weight Paint, Play refusal | Round 256 selects a sphere or switches modes and checks exclusivity, refusal and Escape with the pointer over the view. | Mode switches are untimed. |
| Draw, Clay Buildup, Inflate | `draw`, `clay`, `inflate` move dense rough-sphere vertices and undo. Rounds 256–257 check Draw's radial falloff and Clay's buildup on a plane. | Dense sweep checks an affected set and output identity. The directional checks use the unit fixtures. |
| Smooth, Flatten, Scrape | `smooth`, `flatten`, `scrape` move the rough surface and undo. Rounds 256–257 use a spike and check smoothing energy and the tip's flattening. | Dense sweep checks that Scrape actually moves geometry. Its one-sided plane behavior lacks a dedicated geometric oracle here. |
| Grab, Snake Hook | `grab`, `snake`, `grab-spray`, `snake-spray` exercise the nonlocal paths. Round 256 checks Grab's first-dab set. Round 257 checks Snake Hook along the drag. | The timing path stays inside the silhouette. |
| Pinch, Crease | `pinch`, `crease` move vertices and undo. Round 257 checks Pinch brings vertices closer to the brush center. | Crease's depth profile lacks a dedicated geometric oracle here. |
| Mask brush, Ctrl paint, Ctrl+Alt erase, Shift blur | `mask` and `mask-dot` write mask floats without moving positions. `pen-eraser` samples subtraction. Round 258 checks the modifiers, mask-only deltas and a held or partially held surface. | Modifier variants run in the unit fixtures. |
| Brush size, strength, size +/- and Unified Size / Strength | Sweep chooses a fixed moderate brush. Rounds 256 and 540 check falloff across the disc, per-brush values and actual Unified Size/Strength switching. [The editor suite's task 0261 test](../../tests/test_editor.cpp) selects a cube in Sculpt, runs the shared bracket-key actions and checks the radius changes. | The size +/- console aliases and the full range of dense radius and strength costs lack individual sweep cases. |
| Falloff, Carve, Front Faces Only | Smooth falloff and Both Sides establish sweep controls. `front-only` uses the same path. Round 256 checks inward motion with Carve/Alt and brute-force radius membership. Round 258 exercises Constant. | Sphere, Root, Sharp and Linear falloff lack individual sculpt-output measurements in this round. |
| Pressure Off/Size/Strength, Pressure Curve, Use Tilt/Tilt Strength, pen eraser and E | `pen-size`, `pen-strength`, `pen-tilt`, `pen-eraser` feed pen events. Round 368 checks half pressure and a bowed curve, tilt, the eraser and the E action. | Physical stylus input and the full Tilt Strength range are unmeasured. |
| Mirror X/Y/Z, Radial, symmetry off and Lazy Mouse | `mirror-x`, `mirror-y`, `mirror-z`, `radial-8`, `lazy` use the dense checked drag. Round 257 checks mirrored X deltas, radial 6 and the damped zigzag. | Y/Z, Radial 8 and interaction among all axes and the maximum radial count lack a geometric symmetry oracle in this round. |
| Alpha picker/Off, Alpha Turns | Round 257 loads a checker alpha and verifies fewer vertices move. Alpha is off in the timing rows. | Image picking and turning, and turning with pen tilt, have no paired timing or independent rendered-alpha comparison. |
| Dots, Drag Dot, Drag Rect, Freehand, Spray | `dots`, `drag-dot`, `drag-rect`, all Freehand brush rows and `spray` compare real event output. Round 257 checks one Rect stamp, Drag Dot's last footprint and one Freehand dab per move. | The matrix samples each method and two nonlocal Spray exceptions. |
| Clear, Fill, Invert, Blur, Sharpen, Grow, Shrink, Rect and off-mesh gestures | Round 258 checks whole-mask values and ring/rectangle results and verifies Ctrl-click inversion, Ctrl-drag rectangle and Escape cancellation. | Whole-mesh mask actions are untimed in the dense pointer sweep. |
| Groups from Islands, Mask, Materials, Merge All, Show Groups, Show Mask | Round 258 uses a joined two-island selection, checks group assignment, persistence, group colors and mask darkening. The dense sweep checks the fixture's groups survive brush deltas. | Group recomputation and overlay switching lack paired timings. |
| Hide Group, Masked, Unmasked, Invert, Hide All, Show All, Ctrl+Shift isolate/reveal | Round 258 checks face visibility and picking of hidden islands, shows all and exercises Ctrl+Shift isolation using the Scene pointer. | Mixed hidden-face density and isolate costs are untimed. |
| Dynamic Subdivision levels 0–3, +/- | Round 259 uses an active coarse sphere, checks modifier selection and undo, compares the evaluated positions against full Catmull-Clark or simple subdivision and checks in-place update. | The dense sweep uses applied subdivision, and added/subtracted levels have no paired timing. |
| Dyntopo on/off/toggle/detail | Round 262 selects a sphere and checks splits/collapses, a manifold result and snapshot undo/redo, mask and mirror behavior, and delta undo after turning Dyntopo off. | High-density persistent Dyntopo and the combination with all brushes and stroke methods are untimed. |
| DynaMesh resolution and smooth | Round 260 runs against a sphere and overlapping spheres, checks a closed quad shell, volume, transferred mask, a follow-up stroke and undo/redo. It prints resolution 64/128 timings. | The timings are outside the paired affinity driver. |
| Quad Remesh, target, preserve sharp/boundary, adaptive, seed and cancel | Round 261 waits for the job on a selected sphere and on joined spheres, checks closed quads, target count, mask and undo, then issues console cancel, waits again and checks the selected mesh still holds the previous result. Stroke and mask edits also reject a stale job result. In a build without QuadriFlow, the refusal is checked. | Sharp and boundary preservation, adaptive scale and seed have no per-option result oracle in this round. Job timing is outside the paired affinity driver. |
| Console `dab`, `stroke`, `isolate` | Rounds 256–258 exercise the console paths alongside real input. | Console `stroke` bypasses the production pointer's stroke spacing, so the affinity driver uses Scene pointer events. |

## Topology and mask controls

The joined Windows full-library baseline verified 78 named Sculpt tests with 551 assertions. In `tests_round260`, the sphere actually passed to DynaMesh had 5,954 vertices and 5,952 faces. Resolution 64 produced 18,908 vertices and 18,906 quad faces, with every edge incident to two faces. Its volume changed from 0.5027 to 0.5022. The subsequent stroke changed the new mesh and the two undos restored each previous state. The Quad Remesh worker in `tests_round261` returned 2,113 quad faces for a 2,000-face target with zero bad edges. Cancel and stale-result cases retain the earlier surface. The default Linux build exercises the unavailable-backend path for Quad Remesh.

For Dyntopo, the completed Draw stroke grew the 1,490-vertex input to 1,822 vertices with zero bad edges and zero degenerate faces. The coarse-detail test reduced 5,954 vertices to 3,704. The fixtures also cover a loose vertex, a shape key, split sharp edges, hidden faces and snapshot undo. In the two-island visibility fixture, a large dab moved 708 vertices on the shown island and zero on the hidden one. These are action-level observations from the exact product inputs. The platform logs and individual assertions are recorded in the [Pass 35 sculpt group](C:/blab/pass35-hub/6ed52dc02b8e0659d72b583661500ac425d7f428/full-suite/sculpt/stdout.log) and [sculpt fixtures](../../tests/test_sculpt.cpp).

## Dense mesh frame cost

This clean Linux CPU run used the unchanged production stress binary from the verified Pass 35 package. The selected spheres contained 99,458 and 500,000 faces. Each active brush row reported 41 working frames from the press and 40 spiral moves. Both profiles finished with zero structural mesh problems. The table gives median working-frame milliseconds. The normal profile had 32 logical CPUs available. The limited profile had affinity to CPUs 0 and 1 and two editor workers.

| Brush | 99k normal | 99k limited | 500k normal | 500k limited |
|---|---:|---:|---:|---:|
| Draw | 13.7 | 34.6 | 20.0 | 51.6 |
| Smooth | 12.3 | 34.1 | 19.6 | 50.9 |
| Grab | 12.1 | 33.7 | 18.2 | 50.4 |
| Clay Buildup | 12.4 | 35.5 | 20.3 | 51.2 |
| Inflate | 12.3 | 34.6 | 20.2 | 53.6 |
| Flatten | 12.6 | 34.7 | 21.1 | 52.2 |
| Pinch | 12.5 | 34.7 | 20.6 | 53.0 |
| Crease | 12.4 | 34.9 | 20.8 | 52.9 |
| Scrape | 11.9 | 34.3 | 20.2 | 51.3 |
| Snake Hook | 12.8 | 34.5 | 20.0 | 52.1 |
| Mask | 1.4 | 1.0 | 2.0 | 1.6 |

All ten geometry brushes exceeded the 16.7 ms median budget at 500k on both profiles. At 500k, normal Draw averaged 22.3 ms per frame, including 18.9 ms in Scene 3D and 2.6 ms in the dabs. On two CPUs those means were 54.6, 52.2 and 2.5 ms. The counters are nested and include 40 partial normal updates and 41 in-place updates on each profile. They identify the CPU viewport as the larger follow-up opportunity. The rendering lane received these measurements. Mask updates different data and its position-delta column is zero by design.

Source and run evidence are in `stress/stress_survey.inc` (`sculpt_rows`, `stroke_frames`, `parts_row`), `C:/blab/pass36-hub/0625-baseline.cSwIyY/{normal,limited}/latest.md` and `timing-queue/0625-dense-clean.result.json` (official child exit 0, zero taint). The earlier `0625-baseline.es1QII` batch had test-process taint and was excluded from the table.

## Where the two-CPU frame goes

A separate diagnostic used a roughened, masked sphere with 499,002 vertices, 500,000 faces and per-corner UVs. The 1600 × 900 headless editor framed it with the production camera and dispatched two Draw strokes of 32 real moves each. Each move recorded the complete frame time and the production `FrameProfile` and `RasterStats` stage counters. Outside that interval, it hashed 8,281 color pixels and 8,281 depth/object-ID samples in the same 181 × 181 viewport region. The changed geometry, retained attributes and exact undo were checked after each stroke. This fixture differs from the standard 500k spiral stress in camera coverage, stroke path and surface data.

Three alternating pairs varied the scene's shadow setting on CPUs 0 and 1. The following figures are the median of the three runs' mean move-frame timings. Stage timers overlap. In particular, binning runs during triangle setup.

| Milliseconds per move | Shadows enabled | Shadows disabled |
|---|---:|---:|
| Full frame | 76.998 | 61.911 |
| Scene 3D | 74.939 | 59.817 |
| Shadow map update | 13.752 | 0 |
| Vertex processing | 4.230 | 4.180 |
| Triangle setup | 10.291 | 11.228 |
| Tile rasterization | 9.933 | 10.035 |
| Pixel shading | 27.412 | 24.720 |
| Draw-item collection | 6.336 | 6.367 |

Pixel shading was the largest recorded raster stage. The baseline also regenerated one shadow map per move. With shadows disabled all 192 paired color-frame hashes changed, even though geometry, attributes, undo and depth/ID digests matched. Repeats within each setting agreed on all sampled colors and depth/IDs. Reusing the whole prior Scene frame would also serve stale picking data. The depth/ID digest changed on all 62 consecutive move pairs within the two strokes in every run.

The instrumented source and accepted window are in `C:/blab/pass36-hub/0625-stages/prepare.py`, `0625-stages/run.0M0me6/independent.txt` and `timing-queue/0625-500k-stages.result.json`. The source counters come from `Editor::update_shadow_map` and `Renderer3D::flush`.

### Rejected shadow-cache change

A scratch build changed one branch in `Editor::update_shadow_map`. After the normal cache lookup missed, an active CPU sculpt stroke reused its most recent valid shadow map. Both builds kept shadows enabled and rendered a fresh color view on every move. The only replaced production object was `render_view.cpp`. Three normal/candidate pairs alternated their run order. Both executed the same Draw fixture and sampled the same viewport coordinates.

The median of the runs' mean frame times was 74.886 ms with the production code and 61.971 ms with reuse. The shadow pass dropped from 13.253 ms to zero. The two strokes changed 4,960 and 4,906 vertex positions respectively on both builds. Geometry digests, attributes, undo and sampled depth/IDs matched. **All 192 paired move frames differed in sampled color.** The change failed visible output parity and was excluded from the round.

The exact scratch candidate is `C:/blab/pass36-hub/0625-stages/render_view_candidate.cpp`. The comparison logs and independent evidence are under `0625-stages/pair.6i7vcU`, with child exit 0 and zero taint in `timing-queue/0625-shadow-reuse.result.json`. Probe SHA-256 was `b6b30d65250026c33210d30090439a1c110ad9fafb2f41b1832e0062e2cde2a3` for production and `8b1ee34296d6a9c1989de7b2c8474755a2e97b26db843a9d0cca2a2429275ee4` for the rejected branch.

## Paired pointer results

The standalone probe ran 31 cases with two checked strokes per case and three alternating affinity pairs. All 62 per-stroke outputs matched across six fresh processes, including the input/output digests, touched positions, changed mask entries and dab counts. Each non-idle stroke restored the compared mesh data through one undo. The dense fixture contained 23,810 vertices and 23,808 faces. The small control had 1,490 vertices. Values in the table use the first stroke's 32 move frames. The p95 columns give the median of three per-stroke nearest-rank p95 values in milliseconds. The second stroke followed a path four pixels lower after undo.

| First stroke | Changed positions | Changed mask values | Normal p95 (ms) | One CPU p95 (ms) |
|---|---:|---:|---:|---:|
| idle-small | 0 | 0 | 1.192 | 0.760 |
| idle-dense | 0 | 0 | 1.551 | 0.763 |
| draw-small | 237 | 0 | 9.081 | 28.189 |
| draw | 3,830 | 0 | 12.049 | 35.249 |
| smooth | 4,775 | 0 | 11.903 | 35.753 |
| grab | 3,576 | 0 | 11.708 | 38.112 |
| clay | 4,697 | 0 | 12.243 | 36.116 |
| inflate | 3,830 | 0 | 11.967 | 35.354 |
| flatten | 5,257 | 0 | 13.067 | 38.687 |
| pinch | 5,065 | 0 | 13.553 | 35.969 |
| crease | 5,547 | 0 | 13.123 | 37.293 |
| scrape | 3,834 | 0 | 12.243 | 35.109 |
| snake | 5,234 | 0 | 13.099 | 35.996 |
| mask | 0 | 4,849 | 1.342 | 0.909 |
| dots | 4,594 | 0 | 11.511 | 34.206 |
| drag-dot | 1,721 | 0 | 12.278 | 35.620 |
| drag-rect | 5,341 | 0 | 1.675 | 0.797 |
| spray | 7,260 | 0 | 13.359 | 37.200 |
| grab-spray | 3,576 | 0 | 11.940 | 35.215 |
| snake-spray | 4,668 | 0 | 11.678 | 34.483 |
| mask-dot | 0 | 1,723 | 1.373 | 0.852 |
| mirror-x | 4,660 | 0 | 13.042 | 36.631 |
| mirror-y | 6,171 | 0 | 13.288 | 37.730 |
| mirror-z | 7,668 | 0 | 15.287 | 37.628 |
| radial-8 | 11,431 | 0 | 18.800 | 43.062 |
| lazy | 3,991 | 0 | 11.637 | 34.035 |
| front-only | 4,491 | 0 | 12.731 | 35.647 |
| pen-size | 2,523 | 0 | 11.888 | 34.435 |
| pen-strength | 4,554 | 0 | 12.726 | 35.579 |
| pen-tilt | 5,561 | 0 | 12.556 | 36.785 |
| pen-eraser | 0 | 7,296 | 1.389 | 0.916 |

The rough fixture's Draw stroke took 346.712 ms across 32 moves on normal affinity and 1,080.566 ms on one CPU. The matching idle path took 35.967 and 22.294 ms. Mask changed 4,849 values and held every position fixed. Grab and Snake Hook both moved real geometry under sustained input. The drivers measure CPU-affinity sensitivity on the same machine. No performance change to the sculpt kernel was adopted.

All 62 output rows and the six raw logs are in `C:/blab/pass36-hub/0625-v2/linux-measure.0gLGry/samples`. The release probe SHA-256 is `e3a8aa6f4e78d59c48cf13ad71a56746aaa08e941d4275c2fea4021f24c3e568`. The official `0625-probe-v2.result.json` records child exit 0 and zero taint. The first experimental fixture was rejected because the default camera projected its intended travel below the minimum. The verified fixture uses a wider path and nonuniform per-corner UVs.
