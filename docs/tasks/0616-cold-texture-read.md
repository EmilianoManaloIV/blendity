# 0616: Measure cold texture file reads

- **Status:** done, prototype rejected after independent review
- **Authorization:** Pass 33 hub assigned this bounded follow-up to task 0612 on published 0667198b.
- **Requirements:** R-04 and Q-06.
- **Decisions:** Existing ADRs 0056 and 0134 apply.
- **Implementation:** Project implementer, independent test engineer and reviewer.
- **PR:** [#503](https://github.com/EmilianoManaloIV/blendity/pull/503) against main. First published head 62a0b1f083b6e8be73276c65db31cd637cce68b0.

## Goal

Reduce the first complete Textures panel frame on two physical cores if ordinary small-file reads account for a repeatable share of that frame.

## Scope

Evaluate the texture asset read on a cache miss. Preserve decode, sorting, publication and interaction behavior. Keep generic file reading compatible.

## Likely files

The implementer owns src/core/core.cpp, its declaration in src/core/core.h and src/scene/scene_io.cpp. The test engineer owns relevant existing tests. The lead owns this spec, changelog fragment 634 and scratch tooling.

## Acceptance criteria

1. Read exact binary bytes for empty, small, threshold-adjacent and large ordinary files using one open per successful read. Reject directories and special paths without waiting for a producer.
2. Through the existing texture entry points retain cache miss, failure and repair semantics, ordered results, complete same-frame panel list and main-thread publication.
3. On the same two distinct physical cores, compare five fresh-process matched pairs in alternating order and three baseline/baseline pairs for each workload. Use the existing 2,000-file small fixture and a mixed large-file control with equivalent decoded textures. Require at least four faster small first frames and a median paired gain exceeding the largest absolute A/A difference. Reject for a large-workload median regression exceeding its A/A envelope. Record individual timings, actual process affinity and worker count. Measure warm frame and scroll as controls.

## Tests and checks to run

Predeclare exact-byte, mixed-size, missing, directory, malformed/repair and special-file cases. Exercise actual bulk and single texture reads and editor first-frame behavior. Verify Windows and Linux affected groups and editor for retained product code, dependency-free compatibility, plus applicable texture stress. Run measurements in a reserved timed window with separate scratch project and trash.

## Risks and edge cases

Exact threshold, embedded NULs, short reads and path types can change stream behavior. Limits on C++17 file opening and changing paths must be made explicit. Keep large reads single-open and avoid decoding or publication changes.

## Documentation to update

Record measured outcome and rejection or retained change here and in changelog.d/634-cold-texture-read.md.

## Outcome

The candidate failed criterion 3. The baseline used the published 0667198b product code. Both benchmark executables compiled the same scratch headless entry point to report affinity and worker count and inject a wheel event. The app link inputs were verified against the full-library object bank. The prototype routed only single and bulk texture misses through a regular-file check and a single binary stream, reading 4,096 bytes at a time until EOF. Both used the production decoder, cache publication and CPU Textures panel. The earlier 122–126 ms load profile included decoding and cache work as well as file I/O.

### Whole first-frame measurements

On Windows with MSVC 14.51, Blender libraries and oneTBB, each fresh process reported mask 0x5 and two workers. CPUs 0 and 2 belong to different physical cores. Each run opened the Textures window for one frame at 1600 × 900 with a new cache, preferences and trash. The harness verified file hashes immediately before each workload, warming the filesystem cache. Cold refers to the editor process and its asset cache. Each workload used five alternating matched pairs interspersed with three baseline/baseline pairs. The timed wrapper held both Windows slots and the WSL lock and recorded zero competing-process markers.

The small project contained 1,000 textures of 229 bytes and 1,000 of 1,435 bytes. For the mixed control, trailing whitespace brought the latter 1,000 to 65,536 bytes while preserving their decoded contents. The first-frame panel pixels matched across builds and workloads. The first-frame panel SHA-256 was bc6ae1e742cf567cc09de1787f1a16cf28d8ac7ff43500ca49f19d731d00526d. Every process listed the same 2,000 sorted, already-loaded paths immediately after the first frame.

| Workload | First frame, baseline / prototype (ms), five matched pairs | Baseline/baseline absolute differences (ms) | Median paired gain (ms) |
| --- | --- | --- | ---: |
| Small | 186.3 / 186.8, 189.5 / 183.6, 190.8 / 186.0, 189.8 / 186.5, 197.1 / 184.2 | 6.8, 6.8, 4.8 | +4.8 |
| Mixed | 221.3 / 234.8, 221.8 / 219.4, 222.7 / 223.9, 222.8 / 224.4, 219.9 / 256.2 | 2.0, 0.8, 2.1 | −1.6 |

Paired gain is the baseline frame time minus the prototype frame time, so positive values favor the prototype. The five small-file gains were −0.5, +5.9, +4.8, +3.3 and +12.9 ms. The mixed-file gains were −13.5, +2.4, −1.2, −1.6 and −36.3 ms. Four small pairs favored the prototype. The predeclared rule required its median gain to exceed the largest baseline-pair difference, which measured 6.8 ms. Four mixed pairs favored the baseline; two prototype frames were 13.5 and 36.3 ms slower. On the mixed workload the median loss stayed within its 2.1 ms control envelope. Median first-scroll times were 53.4 / 53.5 ms on small files and 54.9 / 54.7 ms on mixed files. Warm reopen took 1.8 / 2.0 ms and 2.1 / 1.9 ms respectively. Both builds remained above the 100 ms first-open target.

[Benchmark build driver](C:/blab/0634/build.py) · [All process records](C:/blab/0634/pairs-82u543md/rows.json) · [Paired decision](C:/blab/0634/pairs-82u543md/decision.json) · [Timing lock log](C:/blab/cold-634-timing.log) · [Executable hashes and core topology](C:/blab/0634/pairs-82u543md/identity.json)

### Correctness checks

The independent tests exercised exact bytes across the buffer boundary and repaired missing or malformed textures through the single and browser entry points. The Windows full-library focused asset checks passed all 26 assertions. A dependency-free Linux probe passed 28 checks, including a writerless FIFO under a watchdog. Linux file events showed one read-open per ordinary file at each of seven lengths through 65,539 bytes. The prototype checked regular-file status before opening; concurrent path replacement between those operations was outside the probe. The scratch patch preserves the discarded candidate and its tests. The failed timing gate ended product verification before full affected-group and stress runs. Independent review confirmed the paired calculations, all 32 process logs and the findings-only scope.

[Independent test report](C:/blab/0634/probe/test-report.md) · [Windows asset checks](C:/blab/0634/unit-focus-6nds6rbp/run.log) · [Scratch patch](C:/blab/0634/candidate-and-tests.patch) · [Linux one-open trace](C:/blab/0634/probe/linux-opens.log) · [Linux byte and FIFO checks](C:/blab/0634/probe/linux-verified.log)
