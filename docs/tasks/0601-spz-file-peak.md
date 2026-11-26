# 0601: Lower peak memory when opening a legacy SPZ file

- **Status:** in review
- **Authorization:** The owner's engine performance assignment in the Hub authorizes this pass.
- **Requirements:** Q-06 and existing SPZ file and scene compatibility.
- **Decisions:** None needed. The importer keeps its public API and file format.
- **Implementation:** Project implementer with independent test engineering and code review.
- **PR:** [#501](https://github.com/EmilianoManaloIV/blendity/pull/501) against main. Initial published head `d6af4056d5b103d9a5aad648d8301217d394f21d`.

## Goal

Open a large legacy SPZ file using less peak memory while preserving decoded geometry and attributes.

## Scope

Task 0597; src/scene/import_splat.cpp (import_spz_file, spz_gzip, spz_decode); scratch C:/blab/0615-splat/measure.cpp. The decoded arrays, inflated bytes and file-owned compressed bytes have overlapping lifetimes. Test the effect at the production file import entry point.

## Likely files

The round owns src/scene/import_splat.cpp, tests/test_splats.cpp and changelog.d/619-spz-file-peak.md. The benchmark reuses the existing splats stress fixture.

## Acceptance criteria

1. Limit the lifetime of the disk importer's compressed file buffer to the stage that consumes it. Preserve the borrowed read_spz interface and its input on success and failure. Preserve gzip validation, reported errors and timings, v4 fallback and full-library support.
2. Compare full import of the task 0597 one-million-point fixture in five interleaved fresh-process pairs against 66feaeb3. Record peak RSS and elapsed time at four and two permitted CPUs, six output arrays and count. Target at least 45 MiB less peak RSS without a material time regression. Use the same external file and scratch directories. Report full-import numbers separately from inflate.
3. Verify valid v1–4 as applicable, truncated/corrupt input, byte-for-byte decoded attribute parity, and borrowed input lifetime. Run splats and editor groups on Windows and Linux and splats stress. Use independent testing and review.

## Tests and checks to run

Run the splats and editor groups, the damaged-input splats stress workload, Windows full-library Zstandard cases, and five alternating full-import pairs at each CPU affinity.

## Risks and edge cases

Borrowed compressed bytes must remain unchanged after successful and failed reads. Decode errors, malformed headers, gzip bounds and the v4 fallback must remain observable to the caller.

## Documentation to update

Record the end-to-end RSS, time, geometry parity, and qualifications in the round 619 fragment and this task.

## Outcome

### Implementation and measurement method

The legacy disk importer reads the compressed bytes into a scoped string, unpacks into the existing byte vector,
and releases the string when that scope ends. It then validates the unpacked layout and decodes the cloud using
the same functions as the borrowed reader. The borrowed input remains caller-owned. The disk fallback still calls
the existing reader for NGSP v4 and unrecognized file signatures. See `import_spz_file`, `spz_unpack_gzip`
and `spz_decode_gzip` in [the importer](../../src/scene/import_splat.cpp).

The comparison uses the task 0597 fixture unchanged: one million SPZ v3 points, SH degree 3, 60,773,370 bytes
compressed and 65,000,016 bytes inflated. Fixture SHA-256 is
`0b406c7222f94048f41a098ff210f64570baecaeb075aa81df1471a8ce119cd5`.
The harness uses dependency-free GCC 15.2 release objects at `-O2` on the AMD Ryzen 9 5900XT under WSL 2.
Normalized production source and external code match the archived baseline revision `66feaeb3`. Both executables
link the same harness and 304 common objects. The importer's source blob is `783ee286` at baseline and
`825c0dd9` for this candidate.

Each side of five interleaved pairs per CPU count ran in a fresh process (20 processes in total), on CPUs 0–3 with four permitted job threads
or CPUs 0–1 with two permitted job threads. Both revisions use the same cached input file and scratch directories.
The elapsed interval covers the production `import_spz_file` call, including disk read and decoded cloud validation.
Peak RSS is the process high-water mark from `getrusage(RUSAGE_SELF)` at the end of that call.
The harness checks the point count and SH degree and records six array element counts and raw-byte digests
after the measured interval. Task 0597 measured gzip inflate separately. This comparison measures full import.

### Paired full-import results

Each entry shows the median and range across five processes. The reduction column is the median of the five
paired differences in peak RSS.

| Permitted CPUs | Baseline peak RSS (MiB) | Candidate peak RSS (MiB) | Paired reduction (MiB) | Baseline elapsed (ms) | Candidate elapsed (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 | 350.46 (350.41–350.64) | 292.56 (292.04–292.58) | 58.05 | 473.42 (416.67–550.37) | 417.94 (414.18–422.37) |
| 2 | 350.38 (350.25–350.64) | 292.51 (292.35–292.59) | 57.90 | 468.15 (460.54–507.41) | 461.83 (460.52–497.85) |

The smallest individual reduction was 57.70 MiB. The candidate was faster in nine of the ten pairs.
The two-CPU candidate ran 37.30 ms slower in one pair, and the two-CPU elapsed ranges overlapped.
The median paired elapsed change was −10.78% on four CPUs and −0.64% on two CPUs.
These results measure one Linux fixture and machine.

All 20 imports produced 1,000,000 points at SH degree 3 with matching FNV-1a-64 digests of the six arrays.

| Array | Elements | Digest |
| --- | ---: | --- |
| Position | 1,000,000 | `e7fbd66d965e33b3` |
| Color | 1,000,000 | `da10788b285545dd` |
| Opacity | 1,000,000 | `cb11489c7772f6a4` |
| Scale | 1,000,000 | `cec6005b7cf55d32` |
| Rotation | 1,000,000 | `a455e169623b598c` |
| SH coefficients | 15,000,000 | `d2a54bfd80433e72` |

Build identity and raw rows are in `C:/blab/0619/prepare-bench.log` and `C:/blab/0619/run-bench.hub.out`.
The timed wrapper recorded a successful exit and zero taints in `C:/blab/pass32-spz01.log`. The independent
audit confirmed all 20 production imports and array digests in `C:/blab/0619-test/audit_bench.log`.

### Local verification

The dependency-free WSL release build used the candidate source above and independent test source blob
`fc00a24e`. The splats group passed 517 checks, including v1–v3 attribute equality across disk and borrowed
reads, continued use after the borrowed bytes leave scope, damaged inputs and the v4 diagnostic without
Zstandard. The editor group passed 3,819 checks. Quick splats stress found zero problems in 2,100 damaged
inputs. Test and stress runs used external scratch projects, trash, preferences and caches.

WSL evidence in `C:/blab/0619` includes `build-wsl.log`, `verify-wsl-tests.log` and `wsl-stress-latest.md`.
The same lab contains the benchmark source, build script, launch script and result summarizer. The coverage
map found 398 covered entries and zero empty entries.

Independent code review approved the source, ownership and measured evidence against 66feaeb3. The independent Windows full-libraries build passed 530 splat checks, including 76 focused legacy assertions and 17 actual Zstandard v4 assertions. Valid v4 attributes and malformed-table refusal matched. The tester checked 364 common object hashes against the 66feaeb3 build manifest and replaced only the importer and splats test objects (C:/blab/0619-test/win-libs-15ffjgp_/provenance.json). The Hub runs the joined local suites, publishes the cumulative PR, and verifies hosted CI on the published head before owner review.
