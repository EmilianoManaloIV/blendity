# 0614: Recalculate Normals on large meshes

- **Status:** in review
- **Authorization:** Owner's program-wide low-core optimization assignment via the pass 33 Hub.
- **Requirements:** Q-04 and Q-06, editor responsiveness.
- **Decisions:** ADR 0056 (measurement outside unit assertions), 0105 (latency budgets), 0123 (fragments), 0134 (local groups).
- **Implementation:** independent implementer, test engineer, and reviewer in sequence.
- **PR:** [#502](https://github.com/EmilianoManaloIV/blendity/pull/502) against main. First published head 7d64985e4af84e8ac535bdb01c205c453e48aedc.

## Goal

Reduce the wait for Edit Mode Recalculate Normals on a large solidified mesh. In pass 32 the full 2.10M-face operator took 434.2, 435.7 and 435.2 ms on the joined build (modeling-1/2/3-final reports in C:/blab/pass32-survey/joined-pair-68el3lha). This measures the synchronous mesh operation used by scene_view.cpp, on the ordinary desktop configuration. The matched two-core results appear in the Outcome below.

## Scope

Compute each component's closedness during the existing face traversal that already queries incidence for every edge. Skip the farthest-face fallback for components whose signed volume already decides winding, if byte-equivalent. Leave topology construction, public APIs, traversal order, floating-point reductions governing orientation and command behavior intact. The camera and Vulkan matched-control results in task 0600 and the rejected Edit Mode first-frame candidate in 0571 have separate triggers.

## Likely files

| Owner | Files |
| --- | --- |
| Implementer | src/scene/mesh.cpp (meshops::recalc_normals_outside), docs/tasks/0614-recalculate-normals.md, changelog.d/632-recalculate-normals.md |
| Implementer then independent test engineer | tests/test_modeling.cpp |
| Lead | scratch probes, reports and Hub receipt in C:/blab |

Other pass 33 tasks own splats, texture previews and shadow caches. The existing modeling stress fixture in stress/stress_main.cpp supplies the measured input.

## Acceptance criteria

1. Match baseline winding, corner order and per-corner attributes on closed and open components, inconsistent winding, non-manifold edges, near-zero-volume shells, disconnected mixed meshes and empty input. Preserve the fallback's order and tie behavior.
2. In an exclusive same-path release comparison with one logical processor on each of two physical cores, record the actual mask/topology, worker configuration, hashes and at least three alternating pairs plus identical-baseline control. Retain only a repeatable full-operation improvement larger than baseline variation with a median saving of at least 10% on the 2.10M-face row. Record absolute ms, small mesh controls and parity. Check editor command behavior and Undo. Distinguish the timed operator from dispatch and redraw.
3. Affected unit groups and editor pass on Windows and Linux, or document a reproduced baseline failure. Existing modeling stress geometry and overlap counts agree.

## Tests and checks to run

Use baseline and candidate packages of identical build settings and runtime. Run the modeling workload or faithful standalone release fixture through the real operator with fresh inputs. Verify output separately from timing. Use scratch project and trash, project build locks and the Hub's quiet window after current requests. Run focused orientation tests followed by modeling and editor, stress modeling, and changelog validation. Record exact source and test identities in the receipt. Full CI is the Hub's publication gate.

## Risks and edge cases

An incidence other than two marks the current component open even when all neighbors were visited. Face reversal preserves edge membership. For negligible or nonfinite volume preserve the existing absolute-volume comparison including its NaN behavior. Preserve corner attributes through existing reverse_face. Keep memory growth bounded on meshes with many components.

## Documentation to update

Record verified scope, measured savings and controls in the fragment and its PERFORMANCE section; update this spec and the scratch receipt. Hardware claims apply only to the tested machine and affinity.

## Outcome

Baseline integration/4 at 9246bcb1c0f5f547c12439a8a11a6849bfabe361 has identical src and stress
trees to the pass 32 final product bce0c11.

The candidate records each component's edge incidence in `meshops::recalc_normals_outside`
while orienting neighbors. It computes winding from the same signed-volume sum for eligible shells
and evaluates the farthest-face fallback in the original face order for the remaining components.
Round 632's modeling tests cover exact corner order and attributes, an open fan with opposing
fallback signs at equal distance, disconnected non-manifold and negligible-volume components,
empty input, and the Edit Mode command with Undo. Dynamic verification is recorded below.

### Matched Windows measurements

The pass 32 release archive (bce0c11) supplied the baseline stress executable, unchanged release objects, and runtime. Integration/4 has the same tracked src and stress inputs. The candidate relink replaced mesh.obj from source blob 91db3b569a59b23a924dbb4680e4289b88d12779. The compiler was MSVC 19.51.36260 with Blender libraries and the TBB allocator. Each fresh process ran the complete modeling selector at the same executable path. test_modeling_tools times meshops::recalc_normals_outside after the grid loop cut and solidify. The measurements exclude command dispatch, Undo bookkeeping and redraw.

| Ryzen 9 5900XT configuration | Solidified faces | Baseline median (ms) | Candidate median (ms) | Median paired saving (ms) |
| --- | ---: | ---: | ---: | ---: |
| Two physical cores, two workers | 33.5k | 5.5 | 3.3 | 2.2 |
| Two physical cores, two workers | 527.4k | 97.4 | 59.2 | 41.6 |
| Two physical cores, two workers | 2.10M | 436.1 | 290.2 | 145.9 |
| Full affinity, 32 workers | 33.5k | 5.5 | 3.2 | 2.3 |
| Full affinity, 32 workers | 527.4k | 90.7 | 68.9 | 22.7 |
| Full affinity, 32 workers | 2.10M | 450.7 | 309.9 | 141.8 |

The process mask was checked after each launch. Mask 0x5 selects one logical processor on each of two physical cores, and 0xffffffff uses all 32 logical processors. The large two-core baseline/candidate pairs, in AB, BA, AB launch order, were 433.6/301.3, 436.1/290.2 and 442.1/288.8 ms. The identical-baseline pair was 432.6/430.1 ms. Default-affinity pairs were 441.4/309.9, 450.7/308.9 and 474.6/321.1 ms, with an identical-baseline pair of 444.6/441.1 ms. Median paired relative savings were 33.46% and 31.46%, respectively. The largest fixture remains over the modeling workload's 100 ms target. Desktop affinity does not establish physical Pi latency.

The independent tester rehashed executables and DLLs and checked all 16 geometry rows in all runs. The sphere overlap control reported zero doubled vertices and zero doubled edges throughout. See C:/blab/0632-normals/independent-timing.txt, pair-l_3ps7ws/manifest.json (two cores), pair-3ajuj710/manifest.json (default), and C:/blab/texture630-profile-normals632.log (both slots, exit 0, zero taint).

### Correctness and verification

The independent test engineer specified fixtures before reading the diff and added NaN-shell and keymap checks. Actual Shift+N and Ctrl+Z corrected and restored winding for both Unity and Blender shortcuts. The focused Windows run passed 322 assertions. Full Windows libraries groups passed 34,892 modeling and 3,914 editor checks. Linux x86-64 dependency-free groups passed 32,569 modeling and 3,866 editor checks. Windows tests SHA-256 f9049333f26d1c1c37f19a8a54b6e50e491051850e9aa469e5e7b2396ebd290d; Linux f1ad8dfeda42d2a5b5bdbb76b6b6d4046ded7dd854d68195c672884356d08244. The final test blob is 86d8ea442bd6726518865075f1d5343ae36f9622. Logs in C:/blab/0632-normals: focused-bm02ov2h/output.log, modeling-gt97zkc8/output.log, editor-5px1npcy/output.log, linux-modeling.log and linux-editor.log.

A separate native Linux probe rebuilt the 1024 grid, edge loop, loop cut and solidify fixture before running the baseline and candidate operators. The two outputs matched byte-for-byte across all 134,611,148 serialized bytes of positions, face offsets, corner vertices, UVs, custom normals and face materials (SHA-256 dd0578d47a073cec09c887f9f5514120cd22464e2692063323c99489745cb6e4). Smaller fixtures checked per-corner colors and UVs by original corner identity. See C:/blab/0632-normals/parity.cpp, linux.sh, linux-parity.sha256 and linux-parity-bytes.txt. Windows stress geometry and overlap counts matched in all 16 process runs. The Linux preflight initially stopped on Windows line endings in the archived source; the final run compared normalized source before building.

The independent reviewer approved source blob 91db3b56 and tests 86d8ea44, and the lead corrected the stale discovery text in the Goal. The additional dependency-free Windows release passed 32,569 modeling and 3,888 editor checks with zero failures. See C:/blab/0632-normals/win-lite/modeling-zwjr5pe4/receipt.json and win-lite/editor-4i9g1qrl/receipt.json. The Hub owns publication and verification of the combined revision. The Linux comparison command exited successfully and its digests and byte count remain in the scratch receipt. The temporary native output files were unavailable for a subsequent independent reread.
