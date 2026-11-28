# Articulated animation and skin weight transfer

Transfer Weights copies each matched source weight to the target mesh. When every target vertex has a trusted match, no unknown weight remains for inpainting. The editor formerly built the cotangent Laplacian, computed vertex masses and traversed connectivity before discovering the empty solve. It now checks the mask after validating weight-array sizes, preserves the matched weights and counts active groups with the existing threshold. Partially trusted meshes still use the general solver.

## Fixtures and observed outputs

The runner imports a generated closed mesh, places the markers, builds the skeleton and binds it, moves a selected IK target, paints and normalizes weights, copies and pastes a pose, keys a clip, previews fixed frames and plays the clip. An identical shell receives the source weights on each complex rig. The two-bone column provides a small control. The [action inventory](0628-animation-inventory.md) records 190 actions and distinguishes timed workflows, supplemental functional checks and prerequisite-limited routes.

| Fixture | Vertices / quads | Bones / IK limbs | Clip tracks | Brush weight values changed | Maximum transferred-shell vertex displacement |
| --- | ---: | ---: | ---: | ---: | ---: |
| Column | 338 / 336 | 2 / 0 | 7 | 330 | Outside control workload |
| Marker humanoid | 10,242 / 10,240 | 17 / 4 | 64 | 9,670 | 0.48 m |
| Marker quadruped | 15,874 / 15,872 | 20 / 4 | 73 | 14,920 | 0.37 m |

Shell displacement is the largest Euclidean distance between corresponding vertices in the two posed-shell OBJ exports. An independent audit used the native scene hierarchy, clip files, editor logs and OBJ vertex arrays. At frame 120 each skin returned to its initial shape translated 2.4 m. The midpoint deformed, and twelve frames of live playback advanced each body. The rotated bone’s pasted quaternion was approximately (0.21644, 0, 0, 0.97630). Humanoid retargeting carried 17 parts over 121 frames onto a different rig, with 54 tracks and 4,187 retained keys. The creature played its native clip. Humanoid retargeting needs a mapped human skeleton.

A supplemental replay used the saved complex fixtures to exercise previously uncovered routes. Moving Head's head preserved its tail, moving its tail preserved its head, and the extruded child started at that tip and disappeared on Undo. Parent and Position followers moved with a source translated by (2, 3, 4) m on both rigs. Parent and Rotation followers reproduced its 45° Y turn. Scale, LookAt and Aim preserved their serialized settings. The current evaluator solves only Parent, Position and Rotation.

The walk replay preserved ten footprints. Changing one right-foot print by 0.10 m and running Update changed its IK animation tracks. Sequence Jump changed the selected object's exported X from −1 to −8, accounting for the OBJ coordinate convention. Ripple moved the following bar from frame 80 to frame 20. The original combined checker exited 1 after expecting the opposite OBJ X sign and expecting a headless external-encoder preference write. An independent read of the retained meshes and logs verified the jump and the missing-custom-encoder fallback. External encoding and positive model inference require the respective installed tools. These supplemental runs establish functional behavior. The rig/constraint stages exited 0, and the original and corrected walk/sequence receipts are retained in `0628-walk-seq-output1/checks{,-verified}.json`.

The full-size avatar added fingers with three joints, toes, eyes, jaw, followers and springs. The editor generated 292 bones, 139,912 triangles, 15 skinned meshes, one plain mesh, 60 spring chains, 28 constraints and 70 shape keys. The clip keyed all 292 bones and four existing IK targets. Auto IK found zero additional limbs. A gizmo drag moved the selected left-hand IK target. The first of 6,010 exported hand vertices moved from (0.1818009, 1.22191, −0.008426939) m at frame 0 to (0.2115267, 1.16344, −0.04140931) m at frame 30. The entire hand position array matched frame 0 at frame 60, and advancing live playback produced a distinct array. The clean CPU-only run averaged 8.00 ms over seven drag frames and took 172.4 ms for twelve playback frames. This single run describes scale and behavior.

## Reproduction

Run from the checkout with a verified package, using a new scratch directory for each execution.

```text
python tools/bench_animation_rigs.py --exe <editor> --out <fresh-directory> --repeat 3
python tools/bench_animation_rigs.py --exe <editor> --out <other-directory> --cpus 0,1 --repeat 3
```

The runner records native scenes, clips, evaluated mesh samples, commands, stage timings and the editor process affinity. Fixed-frame samples compare directly across executions. Live playback uses wall time and is checked for progression. Transfer timings cover the complete action including completion of its background job. The numerical solver time in the editor log measures only its inpainting phase.

## Baseline controls

Product and build inputs at `90136ffc` match the verified Pass 35 package. All 67 Windows full runtime files matched its manifest. The full baseline editor SHA-256 was `6b0f57419ebf92fb8ad0c8702579cff00418dcc17ea2617d0486a5ac24af9d4e`, and the candidate editor SHA-256 was `c02787fe240b91dd4e7907d0bed6f59b9fc3f8790fdf5bfecc8b7b220c95bd53`. The twelve baseline stress runs completed with zero taint. Each median summarizes three runs. The job-system worker cap in this table limits the pool. The editor comparisons below constrain the whole process to two logical CPUs.

| Baseline workload | Ordinary | Two workers | Output check |
| --- | ---: | ---: | --- |
| 201 tracks, 2,613 keys | 0.0133 ms/frame | 0.0130 ms/frame | Both below 0.2 ms/frame |
| Four pairs of overlapping 201-track clips | 0.3191 ms/frame | 0.3333 ms/frame | Both below 0.5 ms/frame |
| Humanoid locomotion, 180 frames | 0.016 ms/frame | 0.016 ms/frame | 3.97 m traveled on every run |
| Automatic weights, 12-bone tube | 16.8 ms | 16.4 ms | About 4,000 unknown vertices solved |
| Weight transfer onto a 5% wider tube | 3.5 ms | 3.1 ms | Maximum partition error 6e-8 |

A separate clean four-pair comparison on the tube measured complete transfer at 3.4 → 0.4 ms with ordinary workers and 2.9 → 0.5 ms with two workers. All eight pairs improved while maximum partition error stayed at 6e-8. The stress row’s unknown count and solver label describe the automatic bind.

### Editor actions on the three fixtures

Each cell gives the baseline median in milliseconds at ordinary affinity / two logical CPUs. All 36 full-workflow executions completed: three repetitions of both executables, at both affinities, on all three fixtures. Each replay used a fresh project. Fixed preview includes two drawn frames. Playback covers 24 frames. The longer two-CPU playback total reflects the CPU renderer as well as the animation system.

| Stage (ms) | Column | Humanoid | Quadruped |
| --- | ---: | ---: | ---: |
| import | 21.5 / 17.9 | 16.2 / 14.7 | 18.3 / 16.5 |
| rig creation | 0.2 / 0.2 | — | — |
| bind | 0.9 / 0.7 | — | — |
| Place markers | — | 1.9 / 2.3 | 2.5 / 3.2 |
| Build and bind | — | 43.9 / 56.6 | 71.7 / 96.4 |
| ik pose | — | 5.9 / 13.6 | 7.4 / 16.9 |
| weight paint | 0.2 / 0.1 | 1.1 / 1.0 | 1.9 / 1.9 |
| Clean and normalize weights | 0.1 / 0.1 | 1.2 / 1.2 | 1.7 / 1.5 |
| pose paste | 3.3 / 6.3 | 3.5 / 7.3 | 3.9 / 7.9 |
| Create clip and keys | 1.9 / 1.7 | 2.0 / 1.8 | 1.8 / 1.8 |
| fixed preview | 4.2 / 6.2 | 4.9 / 8.6 | 5.1 / 9.4 |
| playback | 62.4 / 117.0 | 71.6 / 165.0 | 75.7 / 180.8 |
| Shell import | — | 43.6 / 53.3 | 24.4 / 24.4 |
| Transfer Weights | — | 15.3 / 14.9 | 23.8 / 24.6 |
| retarget rig | — | 0.1 / 0.1 | — |
| retarget | — | 3.0 / 2.6 | — |

The largest generation cost was the quadruped’s Build and bind action at 71.7 / 96.4 ms. It solves unknown weights across the mesh. The 12-bone automatic bind control likewise still solves roughly 4,000 unknown vertices. Those actions require the general solver. The isolated optimization targets the independently observed cost of constructing the solver topology even when every weight was already trusted.

| Complete Transfer Weights action | Ordinary before → after | Two CPUs before → after |
| --- | ---: | ---: |
| Humanoid shell, 10,242 vertices | 15.3 → 4.3 ms | 14.9 → 4.3 ms |
| Quadruped shell, 15,874 vertices | 23.8 → 7.2 ms | 24.6 → 5.9 ms |

All twelve within-pair comparisons favored the candidate. The order was baseline/candidate, candidate/baseline, baseline/candidate in each affinity. The paired solver and scene receipts record the native weight groups, sampled meshes, clips and poses.

The independent audit of all 18 before/after fixture pairs checked ordered vertex-group names and 2,949,564 stored float weights, 72 fixed-frame OBJ exports, 24 native clips including six retargeted clips, and 24 posed-shell OBJ exports. Every paired byte comparison matched. All 36 live samples progressed over 24 frames. Control travel ranged from 0.07 to 0.15 m, humanoid travel from 0.08 to 0.21 m and quadruped travel from 0.09 to 0.22 m. The clean timing receipt is `0628-rig-pairs.result.json` in the Hub timing queue. `0628-independent/rig-pairs-audit.json` records the comparisons. The candidate inpaint subtime rounds to zero at the logger's millisecond precision.

### Isolated checkpoint replay

Four alternating pairs per rig and CPU profile reopened the same saved pre-transfer scene, transferred the weights and saved the result. On the humanoid the median fell from 16.80 to 4.65 ms at ordinary affinity and from 16.15 to 4.10 ms on two CPUs. On the quadruped it fell from 25.70 to 7.15 ms and from 25.25 to 6.80 ms. The independent replay audit compared all 3,932,752 stored floats and ordered group names across the 16 baseline/candidate pairs. All matched. All 32 executions completed with zero taint. The first replay attempt used a bare scene filename, which Blendity resolved under Assets/Scenes. The accepted batch reopened the checkpoints using explicit Results paths. The failed attempt produced no transfer timing.

## Verification

The Windows full-library and WSL Linux dependency-free builds each passed 51 focused checks in two tests and 5,133 animation checks in 374 tests. Editor passed 4,036 Windows checks and 4,010 Linux checks in 346 tests on each host. The Linux editor group used the same production object before the test-only fixture correction. The original triangle projection check gave the target the same corner indices as the source, selecting Blendity's index-based correspondence path. Rotating the target face's corner order preserved winding and made the fixture exercise closest-point projection. Earlier failures and the corrected results are retained separately.

The final nine Python harness checks passed on both hosts, including bounded child cleanup on affinity-inspection failure. The timed matrix used the same generated geometry, editor command stream and success path as the final runner. The cleanup change affects the launch failure path.

Both hosts independently ran the generated control, humanoid and quadruped. The three Linux end poses also traveled 2.4 m and all three live playback samples advanced. These CPU headless runs do not measure GPU skinning, window presentation or an individual follower constraint's latency. The Hub scratch root is `C:/blab/pass36-hub`. It contains the candidate build identities, the independent byte comparisons and the final unit logs under `0628-independent`, and the shared timing receipts under `timing-queue`.
