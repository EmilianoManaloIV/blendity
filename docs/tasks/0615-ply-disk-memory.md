# 0615: Reduce peak memory when importing binary point-cloud PLY files from disk

- **Status:** in review
- **Authorization:** Owner's low-end optimization assignment, delegated by the Hub as a separately reviewable follow-up to #502.
- **Requirements:** Q-06 and existing PLY import behavior.
- **Decisions:** ADR 0147 governs point-cloud attributes. No new architecture planned.
- **Implementation:** Separate project implementer, test engineer and reviewer in sequence.
- **PR:** [#503](https://github.com/EmilianoManaloIV/blendity/pull/503) against main. First published head 62a0b1f083b6e8be73276c65db31cd637cce68b0.

## Goal

Import large binary point clouds on low-memory machines without retaining a second, file-sized copy while constructing the decoded cloud.

## Scope

Investigate bounded batches of fixed-width binary vertex records at the production disk entry point. Preserve the borrowed-buffer API, existing reader and editor behavior. ASCII, mesh and unsupported layouts may use the existing fallback. Use only generated scratch inputs.

## Likely files

- src/scene/import_formats.cpp (import_ply_file), src/scene/import_splat.cpp and its header.
- Point-cloud and editor import tests, stress disk measurements and fragment 633.

## Acceptance criteria

1. Compare success, diagnostics, name, mode, SH degree, array contents and ordering with the existing importer across endianness, custom property order, scalar widths, padded strides, preceding elements, high SH degrees and batch boundaries.
2. Preserve ASCII and mesh dispatch, malformed and truncated input handling, notes and errors. Verify the real editor command imports a generated disk file.
3. Use fresh alternating baseline/candidate processes at two and four physical cores on identical generated files. Record layout, bytes, point count, elapsed time, peak process memory, affinity, repetitions and spread. Include small files, million-point degree 3, and peak cases. Promote only with genuine peak savings and acceptable time and small-file overhead measured against run variation.
4. Independent testing and review must support the decision. If the trial fails, retain measured findings and remove the production experiment.

## Tests and checks to run

Use the affected point-cloud/import and editor groups on Windows and Linux with scratch project/trash and the verification chain. Measure import_ply_file directly and check a connected editor import. Use the Hub's exclusive timed slot and shared build locks. Run fragment and diff checks.

## Risks and edge cases

File-size checks before allocating, integer overflow, reads cut short after header validation, a record wider than the batch, header termination, nonvertex elements, and binary/ASCII fallback. Historical stress borrowed an in-memory input and cannot establish disk-import peak.

## Documentation to update

Add changelog.d/633-ply-disk-memory.md with the performance evidence and disposition, following the fragment format.

## Outcome

The trial streams eligible disk files above 8 MiB in batches of up to 8 MiB of complete vertex records. It checks the declared vertex range against the file length before allocating the cloud. The disk and borrowed readers share the PLY decoder and validation. The original byte dispatch handles small files, ASCII, meshes and unsupported headers. After independent review, the file probe also requires self-consistent fixed-width declarations. The whole-file reader preserves defined results for incomplete binary declarations and checks that every property it will use fits in the supplied bytes before allocating the cloud.

Candidate checks cover both byte orders for padded, mixed-width points with a preceding element, degree-4 splats and their warning, truncated records, invalid counts, ASCII and mesh dispatch, and the editor import command.

### Final production import measurements

On Windows x64, the dependency-free release build was measured against `0667198b11aac1c1ba9d25e329836548f7ba37dd`. Both binaries used the same scratch probe in the app entry point and the same MSVC 14.51 release build configuration. A Windows topology query selected one logical processor per physical core (masks 0x5 and 0x55). Each cell gives the median and range across five fresh processes per revision. Pairs alternated baseline-first and candidate-first. The generated files were read into the OS cache before each case. These timings describe warm filesystem cache on this desktop.

The direct probe times `import_ply_file` and compares hashes of all six decoded attribute arrays. The editor probe runs the normal headless startup, issues `import <file>`, waits for its async file job to land, steps one more frame and verifies exactly one attached million-point degree-3 cloud. Its time includes editor startup through that frame. Both probes read the Windows process lifetime peak working set. Each editor process used a fresh scratch project, preferences and cache.

The small file contains 16 degree-3 records (5,495 bytes). Small-cold measures one import in a newly started process with a warm file cache. Small reports the mean of 100 imports in each fresh process. Points uses a million XYZ records (12,000,121 bytes). Million and editor use 1,000,000 degree-3 vertices with normals and a 248-byte stride (248,001,532 bytes). Wide uses 1,000,000 degree-4 vertices with 32 additional unused floats and a 484-byte stride (484,002,965 bytes). All four files are little-endian. Each point's attributes vary within a repeated 1,024-record block.

| Cores | Workload | Baseline ms | Candidate ms | Baseline peak MiB | Candidate peak MiB |
| ---: | --- | ---: | ---: | ---: | ---: |
| 2 | small-cold | 0.414 (0.402–0.437) | 0.419 (0.406–0.435) | 9.0 (9.0–9.0) | 9.0 (9.0–9.0) |
| 2 | small | 0.197 (0.195–0.199) | 0.197 (0.194–0.204) | 9.1 (9.1–9.1) | 9.1 (9.1–9.1) |
| 2 | points | 17.1 (16.9–17.7) | 16.3 (16.3–24.6) | 43.3 (43.3–43.3) | 39.8 (39.8–39.8) |
| 2 | million | 325.8 (325.7–341.5) | 293.5 (287.4–308.1) | 470.6 (470.6–470.6) | 242.0 (242.0–242.0) |
| 2 | wide | 511.2 (502.2–561.1) | 432.2 (426.6–441.6) | 695.7 (695.7–695.7) | 242.0 (242.0–242.1) |
| 2 | editor | 750.8 (746.7–762.4) | 708.1 (695.4–728.8) | 491.8 (491.8–491.9) | 322.9 (322.4–322.9) |
| 4 | small-cold | 0.469 (0.444–0.520) | 0.468 (0.458–0.518) | 9.1 (9.1–9.1) | 9.1 (9.1–9.1) |
| 4 | small | 0.202 (0.201–0.211) | 0.204 (0.198–0.206) | 9.2 (9.1–9.2) | 9.1 (9.1–9.2) |
| 4 | points | 16.4 (16.3–17.2) | 15.5 (15.3–17.2) | 43.4 (43.4–43.4) | 39.9 (39.9–39.9) |
| 4 | million | 301.6 (293.7–317.5) | 250.6 (249.7–251.9) | 470.7 (470.7–470.8) | 242.1 (242.1–242.2) |
| 4 | wide | 476.0 (470.6–484.8) | 391.5 (387.4–410.0) | 695.8 (695.8–695.8) | 242.1 (242.1–242.1) |
| 4 | editor | 621.3 (611.1–632.2) | 571.9 (562.2–597.1) | 492.0 (491.9–492.0) | 322.6 (322.6–322.7) |

Paired median savings for the million-point direct import were 228.59 MiB on two cores and 228.60 MiB on four. The editor saved 168.97 and 169.35 MiB. The degree-3, wide-record and editor trials were faster in every pair. Small-file time ranges overlapped on both core counts. The two-core XYZ file had one slower candidate run (24.6 ms against its paired baseline of about 17 ms), while its five-run median improved from 17.1 to 16.3 ms. Cold physical storage, other operating systems and full-library builds were outside the paired Windows performance measurement.

The retained evidence is `C:/blab/ply-disk-633/` with `generate.py`, `run-final.py`, `rows-final.jsonl`, `binaries-final.json` and the probes under `base/src/app/`. The exclusive window log `C:/blab/ply-633-final.log` records the timings and lock state. The first reviewed candidate and its superseded measurements remain in scratch.

### Verification and review

The Linux dependency-free release build passed 706 splat, 3,348 asset and 3,860 editor checks. All 2,100 damaged inputs in quick splat stress completed with zero reported problems. One unlocked run under concurrent load recorded a 204.4 ms frame when a 100,000-point cloud landed. Three fresh alternating pairs of the published and corrected Linux stress executables repeated this case under the exclusive lock on four cores: baseline landing frames were 0.5, 0.5 and 0.6 ms, and candidate frames were 0.6, 0.6 and 0.5 ms. All six runs attached all 100,000 points within the existing budget. See `test-receipt-final.md`, `test-linux-final-build.log`, `check-landing-final.hub.out` and `landing-final-*-*.md` in the scratch evidence directory.

The independent reviewer identified a possible disagreement between declared stride and the properties read from an incomplete declaration. The corrected source routes such records to the whole-file path and checks the last required read before allocating. The test engineer verified defined whole-file outputs on safely padded data in both byte orders, plus fallback selection and declared-length rejection. The reviewer inspected the final-record width guard and approved the corrected source. The final Windows benchmarks used these corrected production files.

| Final build | Splats | Assets | Editor | Failures |
| --- | ---: | ---: | ---: | ---: |
| Windows, dependency-free | 706 | 3,343 | 3,860 | 0 |
| Windows, Blender libraries | 719 | 3,386 | 3,886 | 0 |
| Linux, dependency-free | 706 | 3,348 | 3,860 | 0 |

Windows used the stock release test target with the verification chain's no-LTCG setting and MSVC toolset 14.51. The frozen production and test source digests are in `final-source-sha256.json`; Windows group logs are in `final-{lite,full}-{splats,assets,editor}/out.log`. The matched Windows benchmark used release app binaries with LTCG. The two benchmark executables and the Linux stress pair have their SHA-256 digests in `binaries-final.json` and `check-landing-final.hub.out`. Independent roles verified the corrected implementation and its output. Retain the optimization for Hub publication and the owner's final review.
