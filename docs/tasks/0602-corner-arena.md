# 0602: Bound first-use corner storage and preserve live blocks during reuse

- **Status:** in review
- **Authorization:** Owner performance and hardware-resource assignment under parent task 0600, approved for dispatch as round 620.
- **Requirements:** R-05, R-11, Q-03, Q-04, Q-06.
- **Decisions:** [ADR 0109](../decisions/0109-the-cpus-share-of-a-card-frame.md) establishes stable triangle bases and the existing CPU fallback. The growth heuristic and vector split fix fit that decision.
- **Implementation:** Project `implementer` for the allocator and policy, followed by independent test and review.
- **PR:** Pending Hub publication.

## Goal

Keep baked lighting on the GPU with less reserved corner storage on integrated and low-memory cards. Preserve each live mesh's baked data when reusing freed space, including the non-tail split that previously let a later upload overwrite a live block.

## Scope

- In: Classify raster devices using existing device information, adjust growth and allocation retry, correct free-block splitting, verify the GPU rendering path and capture reservation and frame evidence.
- Out (not in this task): Baked corner representation and precision, Editor mesh and UV ownership, and changes to other renderer storage policies.

## Likely files

- [`src/render/gpu_raster.cpp`](../../src/render/gpu_raster.cpp), including `VulkanRaster::init`, `add_corners`, `remove_corners`, and `make_buffer`.
- [`src/render/gpu_corner_arena.h`](../../src/render/gpu_corner_arena.h), the private device classification and growth sizes.
- [`tests/test_gpu.cpp`](../../tests/test_gpu.cpp), the compact policy and Vulkan split and retirement fixtures.
- [`stress/stress_main.cpp`](../../stress/stress_main.cpp), the existing editor UI, render and fuzz sections for verification.

## Acceptance criteria

1. A 12-triangle baked cube that fits the initial 4 KiB buffer triggers zero growth. Four cubes totaling 48 triangles establish real before and after Vulkan reservation at first growth under the same device and a controlled low-memory profile.
2. Integrated devices and devices with known low memory use amortized growth within device limits. The standard profile retains the successful 64 MiB growth and 16 MiB fallback targets from [round 458](0434-kept-instances-and-the-ui.md). Retry can fit the exact request after larger attempts fail, and exhausting retries leaves the held data and descriptors intact.
3. Split, reuse, coalesce, retire, grow, reject and reset with stable live bases and exact independent data. Exercise policy and allocation-failure behavior without Vulkan where needed.
4. Compare display, depth and ID bytes and frame-time distributions for progressive and large lightmapped growth. Record growth allocations and copied bytes. Distinguish the physical device from the emulated policy inputs. Reject a measured large-scene growth stall beyond 100 ms in this fixture.
5. Run GPU and editor test groups and the relevant stress sections on Windows and Linux. Obtain independent test and review receipts and verify the combined candidate.

## Tests and checks to run

- Unit: `BLENDITY_TEST_FILTER="round 620"` for the focused GPU group, then the full `gpu` and `editor` groups on Windows and Linux. Run the allocation policy assertion even when a Vulkan device is unavailable.
- Stress: `blendity_stress --quick --only editor` with isolated project, preferences, cache and trash. Record crashes, stale pictures and missed changes.
- Evidence: Extract the production add and remove methods for a controlled allocation probe. Run native before and after Vulkan fixtures in the Hub's timed window. Check `git diff --check` and `py -3 tools/dev/changelog.py check`.

## Risks and edge cases

- Growing more often can raise upload-frame tail times on the compact profile. Record the allocation, copy and timing costs for the growing scene.
- A non-tail split can move the block vector. Retired entries can coalesce, and a retired tail can be reused immediately. Live triangle bases must continue to address the same data.
- Reject requests beyond `maxStorageBufferRange` or 2 GiB before computing the byte count. Recover from create, allocate, bind and map failure. Keep a valid old mapping until a replacement can publish.
- Test image and payload fidelity at the existing 24 floats per triangle. Keep the policy assertions buildable in a dependency-free configuration.

## Documentation to update

- Update the round fragment [`changelog.d/620-corner-arena.md`](../../changelog.d/620-corner-arena.md), with README and usability notes about stable baked lighting when storage is reused. Place shared documentation edits in fragments as required by [ADR 0123](../decisions/0123-changelog-fragments.md).
- Record the actual device, emulated profile, figures, acceptance outcomes and outstanding gates here. ADR 0109 continues to cover the arena contract.

## Outcome

### Implementation

- Vulkan raster devices reported as integrated, or with known device-local memory of at most 4 GiB, use a 1 MiB minimum when the baked corners outgrow the initial 4 KiB buffer. The small-scene phase doubles capacity or fits the request up to 4 MiB of baked data. A request exceeding that span selects the standard 64 MiB growth and 16 MiB fallback floors. That limits the extra data copied to promote out of the small-scene phase to at most 4 MiB. Other devices use the standard targets from first growth. All profiles respect the storage buffer range and the existing 2 GiB arena limit, try the exact size before reporting an allocation failure, and skip duplicate attempts.
- `VulkanRaster::add_corners` checks the limit before converting floats to bytes. Failed replacement buffers are discarded before touching the old buffer or its descriptors. A successful replacement copies the occupied span at its original byte offsets, as required by ADR 0109. Free-block splitting updates the original entry by index after appending a remainder, which can move the vector storage.
- `VulkanRaster::make_buffer` reports bind and map failures and clears the unsuccessful allocation. This lets the corner arena retry with a valid mapping.

### Production-method probe

The scratch probe in `C:/blab/0620` extracts `add_corners` and `remove_corners` from baseline `66feaeb3` or the working source. Run `py -3 extract.py 66feaeb3` or `py -3 extract.py working`, then `probe.bat` from that directory. MSVC 14.51 builds the extracted code with a host allocator standing in for Vulkan. It starts with the same 4,096-byte buffer as `VulkanRaster::init`. Growth allocation counts exclude that initial buffer. The table records the original compact candidate measured in window W3. Evidence is in `C:/blab/0620/baseline.log` and `C:/blab/0620/candidate-unbounded.log`.

| Fixture | Before | After |
|---|---|---|
| Baked cube payload, 12 triangles (1,152 bytes) | 4,096 bytes reserved, no growth | Same |
| Four baked cubes, 48 triangles (4,608 bytes) | 67,108,864 bytes reserved at first growth | 1,048,576 bytes reserved under the compact policy |
| Cube plus eight 80,000-triangle uploads, 128 MiB cap | 67,108,864 bytes, 1 growth allocation, 1,152 bytes copied | 61,449,216 bytes, 4 growth allocations, 53,764,608 bytes copied under the compact policy |
| Standard policy, cube plus ten 80,000-triangle uploads | 134,217,728 bytes, 2 growth allocations, 61,442,304 bytes copied | Same |
| Non-tail split after reserving 3 + 1 triangles | Bases 0, 1, 2, 3, 1. The last upload overwrites a live block. | Bases 0, 1, 2, 3, 4. Coalescing holes 1 and 2 reuses base 1 and keeps neighboring markers. |

The probe simulates a 2 MiB primary allocation and a 1,252,608-byte exact retry failing below that size. With the exact size permitted, the next call succeeds at base 48 while retaining the old data at base 0. Tail retirement returns base 0 for reuse. The independent test engineer's host probe reported 44 checks passed for the candidate and six failures for the baseline (`C:/blab/0620-test/working.log`, `C:/blab/0620-test/66feaeb3.log`).

The adaptive replay kept the tiny and four-cube results. It used one growth allocation and copied 1,152 bytes in the large fixture, matching baseline. For twelve 5,000-triangle uploads it reserved 1, 2, 4 and 64 MiB and copied 6,724,608 bytes. The same split, coalescing and exact-fit retry checks passed. See `C:/blab/0620/candidate.log` for this replay.

### Build and runtime checks

The Windows release build passed with MSVC 14.51.36231 and Blender's libraries (`C:/blab/0620/windows-build.log`). The quick editor stress run exercised editor UI, rendering and fuzzing. Its eight Vulkan render rows drew 131 views on an NVIDIA GeForce RTX 4070 SUPER with zero CPU fallbacks. The fuzzer ran 1,500 frames and 2,674 events with zero crashes, stale pictures or missed changes (`C:/blab/0620/windows-stress/reports/report_20261120_082645.md`).

### Native Vulkan reservations and frame times, rejected W3 candidate

The paired fixture renders a 12-triangle cube, a 48-triangle four-cube scene and one cube plus eight 80,000-triangle tiles. The physical card was an NVIDIA GeForce RTX 4070 SUPER reporting 11,999 MiB device-local memory and `maxStorageBufferRange` of 4,294,967,295 bytes. The emulated profile changes the allocation policy inputs to integrated and 2,048 MiB while using the same Vulkan device. Each of the three before and after runs per scene and profile used the same geometry, lightmap, 160 × 120 output and warmed shader package. The display, depth and ID byte hashes matched in all 18 pairs. The timed window reported zero taint lines.

| Fixture and profile | Vulkan allocation before → after | Growth allocations before → after | Copied bytes before → after |
|---|---:|---:|---:|
| Cube, both profiles | 4,096 → 4,096 B | 0 → 0 | 0 → 0 |
| Four cubes, emulated integrated | 67,108,864 → 1,048,576 B | 1 → 1 | 3,456 → 3,456 |
| Four cubes, native discrete | 67,108,864 → 67,108,864 B | 1 → 1 | 3,456 → 3,456 |
| 640,012 triangles, emulated integrated | 67,108,864 → 61,449,216 B | 1 → 4 | 1,152 → 53,764,608 |
| 640,012 triangles, native discrete | 67,108,864 → 67,108,864 B | 1 → 1 | 1,152 → 1,152 |

The measured Vulkan memory requirement matched the buffer size for each reservation. The growing scene includes 147 frames per version and profile, with 27 upload frames among them. Frame times include allocation, corner upload, draw and readback.

| Growing-scene profile and frames | p50 before → after | p95 before → after | Maximum before → after |
|---|---:|---:|---:|
| Emulated integrated, all | 0.273 → 0.281 ms | 0.634 → 29.793 ms | 5.126 → 121.406 ms |
| Emulated integrated, upload frames | 0.584 → 1.187 ms | 5.000 → 116.543 ms | 5.126 → 121.406 ms |
| Native discrete, all | 0.277 → 0.271 ms | 0.591 → 0.608 ms | 5.352 → 4.586 ms |
| Native discrete, upload frames | 0.563 → 0.564 ms | 4.843 → 4.272 ms | 5.352 → 4.586 ms |

The compact profile met the small-scene reservation target and exceeded 100 ms in all three large-scene runs on this card. The slowest sample in each run coincided with growth that copied 30,721,152 bytes. The Hub rejected this version and requested the adaptive promotion above. Physical integrated GPU timing remains unverified. See `C:/blab/0620/native-results/`, `C:/blab/0620/native-summary.log`, `C:/blab/0620/timed-native-v2.hub.out`, and `C:/blab/pass32-verify03.log`. The first native attempt verified the one-cube hash before stopping on a missing PowerShell hash cmdlet. Its planes are saved under `C:/blab/0620/first-window`. The successful runner uses .NET SHA-256.

### Native Vulkan reservations and frame times, adaptive W4 candidate

The same card, profiles and 160 × 120 output were used with the adaptive policy. The fixture added a progressive scene of one cube and twelve 5,000-triangle tiles, uploaded on separate frames. Each row used three before and after runs. Display, depth and ID bytes matched in all 24 pairs, and the window recorded zero taint lines.

| Fixture and profile | Final Vulkan allocation before → after | Growth allocations before → after | Copied bytes before → after |
|---|---:|---:|---:|
| Cube, both profiles | 4,096 → 4,096 B | 0 → 0 | 0 → 0 |
| Four cubes, emulated integrated | 67,108,864 → 1,048,576 B | 1 → 1 | 3,456 → 3,456 |
| Four cubes, native discrete | 67,108,864 → 67,108,864 B | 1 → 1 | 3,456 → 3,456 |
| 60,012 triangles, emulated integrated | 67,108,864 → 67,108,864 B | 1 → 4 | 1,152 → 6,724,608 |
| 60,012 triangles, native discrete | 67,108,864 → 67,108,864 B | 1 → 1 | 1,152 → 1,152 |
| 640,012 triangles, both profiles | 67,108,864 → 67,108,864 B | 1 → 1 | 1,152 → 1,152 |

The progressive compact candidate reserved 1, 2 and 4 MiB before promoting to 64 MiB on the ninth tile. That growth copied 3,841,152 bytes. The straight-to-large upload used the standard growth target on its first tile.

| Profile and frames | p50 before → after | p95 before → after | Maximum before → after |
|---|---:|---:|---:|
| Emulated integrated, progressive, all 195 | 0.177 → 0.178 ms | 0.418 → 0.681 ms | 6.194 → 18.176 ms |
| Emulated integrated, progressive, 39 uploads | 0.208 → 0.240 ms | 4.820 → 17.166 ms | 6.194 → 18.176 ms |
| Emulated integrated, large, all 147 | 0.293 → 0.293 ms | 0.653 → 0.665 ms | 5.908 → 6.242 ms |
| Emulated integrated, large, 27 uploads | 0.609 → 0.623 ms | 5.839 → 6.137 ms | 5.908 → 6.242 ms |
| Native discrete, large, all 147 | 0.292 → 0.296 ms | 0.767 → 0.767 ms | 6.341 → 7.242 ms |
| Native discrete, large, 27 uploads | 0.623 → 0.683 ms | 5.755 → 6.667 ms | 6.341 → 7.242 ms |

The adaptive policy retained the small-scene memory reduction and passed the large-scene timing limit. Progressive upload p95 reached 17.166 ms and the slowest frame took 18.176 ms during promotion. The native discrete control kept its growth sizes and its large-scene all-frame p95 stayed at 0.767 ms. Physical integrated GPU timing remains unverified. Raw runs are in `C:/blab/0620/adaptive-results/`. See `C:/blab/0620/adaptive-summary.log`, `C:/blab/0620/timed-native-adaptive.hub.out`, and `C:/blab/pass32-verify04.log`.

### Independent and combined verification

- The independent tester passed six portable boundary checks and 51 production-method host checks on the adaptive revision, including whole-triangle requests around the 4 MiB threshold, exhausted retries, and exact float comparisons through promotion. Evidence is in `C:/blab/0620-test/boundary-v2/verified.log` and `hub-ready.txt` in the same folder.
- The Hub reported independent review approval of the frozen candidate. The policy header SHA-256 is `ea14ab7e05d0465f15492bf8e059ed84915d029dffde3c6567e0819e09d3d47b`. The standalone GPU test file SHA-256 is `2cef11f545a1ced3a603c2cfe34e063c03d725a4b62d1252615081ca80ca9248`. The combined sources preserve the round's renderer and GPU test sections. The joined-file identities are recorded in `C:/blab/pass32/final/joined-blobs.json`.
- The adaptive combined Windows full-library and dependency-free release builds passed (`C:/blab/pass32/final/win-full/hub_build.log`, `C:/blab/pass32/final/win-lite/hub_build.log`). The full-library Windows `gpu` group passed all 1,836 checks, including the round-620 Vulkan split and retirement fixture (`C:/blab/pass32/final-full-groups/gpu/run.log`). The combined dependency-free WSL release build passed, along with 187 `gpu` checks and 3,860 `editor` checks (`C:/blab/pass32/final/joined-linux-build.log`, `C:/blab/pass32/final/joined-linux-gpu.log`, `C:/blab/pass32/final/joined-linux-editor.log`).
- Independent focused checks passed all 68 assertions on the joined Windows Vulkan build. The dependency-free Windows and Linux runs each passed all 22 portable assertions, with the Vulkan pixel case skipped in those builds. See `C:/blab/0620-test/final/run-620-20261120-100245-433/tests.log`, `C:/blab/0620-test/final/run-portable-20261120-100117-758/tests.log`, and `C:/blab/0620-test/final/linux.log`.
- The round is ready for local integration. The Hub owns the joined Windows editor receipt, cumulative matched stress, publication and CI under parent task 0600. The native allocation profile was emulated on a discrete GPU, so its results leave physical integrated-device performance open.
