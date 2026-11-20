# Task 0597 — Reduce gzip SPZ import memory

Round 615. Approved for dispatch through the owner-assigned application sweep, OPEN-WORK item 5.
Base `22d735b3` (#498). Status in review.

## Scope and acceptance

- Measure the inflate stage and the complete import separately on the same synthetic valid SPZ v3, SH degree 3 file. Record point count, bytes, revision, compiler and whether samples are from the matched machine or Pi. Compare separate fresh processes for each revision, same thread count and input. Keep timing outside unit assertions (ADR 0056).
- Transfer the validated gzip output allocation to SPZ decoding. Retain the bounded inflate, error diagnostics, preexisting string API and unaffected NGSP v4 path. Full import must yield identical cloud attributes. Check failure leaves the destination unchanged, embedded zeros, valid empty gzip, a limit below actual size and a bad trailer.
- Run affected splats, assets and editor groups and splats stress. Coordinate build and timing with the Hub. Close with independent testing/review and a measured fragment. No claim about physical Pi performance.
- Ownership: src/scene/unity_package.{h,cpp}, src/scene/import_splat.cpp, tests/test_splats.cpp, this spec and changelog.d/615-splat-import-memory.md. No change to project assets, UI, file formats or dependencies.

## References

- [Round 567 stress fixture and measurements](../../stress/stress_splats.inc), [OPEN-WORK item 5](../handoff/OPEN-WORK.md), [ADR 0147](../decisions/0147-point-clouds-and-gaussian-splats.md).
- [Validated gzip unpack](../../src/scene/unity_package.cpp), [SPZ version dispatch](../../src/scene/import_splat.cpp), [gzip member format, RFC 1952 section 2.3](https://www.rfc-editor.org/rfc/rfc1952#section-2.3).

## Measurement method

Use round 567's deterministic `make_spz_scene` fixture with 1,000,000 points and SH degree 3. The gzip file is
60,773,370 bytes and expands to 65,000,016 bytes. Its SHA-256 is
`0b406c7222f94048f41a098ff210f64570baecaeb075aa81df1471a8ce119cd5`.

The scratch harness links release objects from the dependency-free Linux build. It substitutes the two base
translation units for the baseline and compiles the harness against the gzip output type used by each revision.
The candidate source patch has stable Git patch ID `9cb09dee020ab4bda3c94871406da39a832e97cd`.
The measurement machine is an x86-64 Ryzen 9 5900XT under WSL 2 with GCC 15.2.0 at `-O2`. The harness pins
CPUs 0–3 and sets the job-system limit to four threads. Five pairs per stage alternate the run order and launch
one fresh process for each sample. Both revisions read the same cached file.

- **Inflate** preloads the compressed bytes, then times `gunzip` with SPZ's output limit. The reported peak includes
  the preloaded input and uses `getrusage(RUSAGE_SELF).ru_maxrss` after the call.
- **Complete import** times `import_spz_file`, including reading the file, inflating, allocating, decoding,
  converting axes and validating the resulting cloud. Its peak uses the same Linux process high-water mark.

The scratch lab at `C:\blab\0615-splat` contains `measure.cpp`, `prepare-bench.sh` and `run-bench-wsl.sh`.
These runs measure the x86-64 development machine.

## Measurement results

Values are the median (minimum–maximum) across five fresh processes per revision and stage.

| Stage | Base peak RSS (MiB) | Candidate peak RSS (MiB) | Base time (ms) | Candidate time (ms) |
| --- | ---: | ---: | ---: | ---: |
| Gzip inflate | 187.06 (186.92–187.18) | 125.14 (125.00–125.27) | 266.02 (250.78–301.19) | 205.04 (203.65–214.16) |
| Complete import | 350.43 (350.32–350.55) | 350.40 (350.19–350.49) | 469.68 (461.23–505.98) | 430.81 (419.82–534.16) |

The inflate process lost 61.92 MiB from its peak, consistent with removing one copy of the 62 MiB output.
Relative to each process's high-water mark after preloading the gzip, median growth fell from 124.00 MiB to
62.00 MiB. The complete import still needs the decompressed bytes and decoded attribute arrays at the same
time. Its measured peak stayed near 350.4 MiB and its time ranges overlapped.

Raw rows are in `run-bench.hub.out`. The Hub's `C:\blab\splat615-window01.log` recorded eight quiet
preflight samples, exit code 0 and zero taint lines.

## Compatibility evidence

- `spz_gzip` decodes the inflater's byte vector directly. The `gunzip` string overload continues to provide
  string output for Unity packages. The overloads publish output only on success and share the bounded inflate
  and trailer length check. `read_spz` still dispatches NGSP to the existing v4 reader.
- `tests/test_splats.cpp` covers binary bytes, exact and exceeded limits, a forged trailer that reaches the inflater's
  cap, a trailer length mismatch, destination preservation and an empty member at a zero-byte limit.
- An independent headless run imported seven splats apiece from SPZ v1, v2 and v3. The six serialized attribute
  arrays (position, color, opacity, scale, rotation and SH) matched the baseline byte for byte for all three versions.
  The independent expected-value calculations agreed within `1.2e-7` at worst. See
  `independent/matched-base.json`, `independent/candidate.json` and `independent/verdict.txt` in the scratch lab.

## Local verification

Windows used MSVC 14.51 and Blender's libraries with shader warming and OptiX disabled. WSL used GCC 15.2
and the dependency-free build. Both suites used separate scratch project, trash, preferences and cache folders.
The splats group exercised v4 decode on Windows and the dependency-free refusal on WSL.
Both quick stress passes used `blendity_stress --quick --only splats --threads 4`.

| Platform | `splats` | `assets` | `editor` | Quick splats stress |
| --- | ---: | ---: | ---: | --- |
| Windows with libraries | 440 | 3,119 | 3,845 | 2,100 damaged inputs, zero problems |
| WSL dependency-free | 438 | 3,086 | 3,819 | 2,100 damaged inputs, zero problems |

The first WSL editor run put its test folder under the archived source tree. The installer test's project search
then found the source tree's `Assets` ancestor. With the test folder in a separate `/tmp` directory, that same
test passed all five checks and the full editor group passed. See `wsl-editor-focused-corrected.log`,
`verify-wsl-corrected.log` and `wsl-editor-corrected.log`.

Build, test and stress records are in `windows-build.log`, `linux-build.log`, `linux-final-tests-build.log`,
`verify-windows.log`, `verify-wsl.log`, `windows-stress-latest.md` and `wsl-stress-latest.md`.

`tools/test_coverage_map.py --check` found 398 covered entries and zero empty entries. The changelog applicability
check and `git diff --check` passed.
