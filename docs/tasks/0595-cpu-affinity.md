# 0595. Size automatic CPU work to the available processor set

Round 613. Implemented and verified on Windows x64, Linux x86-64 and Linux ARM64. Approved by the owner on November 19 under the application-wide performance and hardware compatibility goal.

## Evidence and scope

Linux on the development host reports 32 online logical CPUs. On the merged dependency-free build, launching the animation thread-ceiling test with `taskset -c 0,2` still reported 32 available threads and ran three distinct threads under a ceiling of three. The OS allowed the process only two CPUs. This concerns inherited CPU affinity, including restricted sessions on ARM64 Linux. It does not measure Pi performance or fractional container CPU quotas.

## Change

Use the Linux affinity mask for automatic CPU scheduling in the built-in job system and the existing shared hardware-thread query. Align automatic texture readers, UV workers and optional inference thread defaults with that query. Preserve explicit job-system worker requests, TBB scheduling and processor detection on Windows and macOS. Handle affinity-query failure with the current portable fallback. Avoid a syscall on each parallel loop. On all platforms the built-in background caller runs serially when the effective budget is three lanes or fewer, and a single available CPU limits automatic texture reading to one reader.

Ownership: core system and job sizing, automatic scene worker sizing, focused scheduling tests, this spec and the round 613 changelog fragment. Separate from GPU rendering and Preferences input handling.

## Verification

On Linux launch fresh processes unrestricted, with one CPU, and with a noncontiguous pair drawn from the process's allowed mask. Compare the selected count and real worker usage, exercise small parallel loops and explicit limits, and verify exact output. Measure matched workload latency or CPU/task counters before making a speed claim. Run existing job, animation and editor tests, compile both Windows dependency modes and verify the native ARM64 build where available. Record any hardware limits and link independently reviewed evidence before publication.

Reference: [Linux affinity semantics](https://man7.org/linux/man-pages/man2/sched_getaffinity.2.html) and ADR 0106 for background jobs.

## Verified hardware behavior

On the 32-CPU x86-64 WSL host the baseline process peaked at 32 threads under each of the one-CPU, noncontiguous two-CPU, and unrestricted masks. The candidate peaked at 1, 2 and 32 respectively across three repetitions per binary and mask, 18 fresh processes in total. CPU limits applied to a child process only. Raw counts and executable hashes are in `C:/blab/affinity613/independent-peaks.jsonl`.

The native four-vCPU Linux ARM64 runner passed foreground, background and explicit-worker checks under one, two and four allowed CPUs. The background loop used one, one and two participants respectively. The editor produced its 1280 × 720 CPU screenshot and passed 4,102 editor/input checks. These are scheduler and functional results. Physical Raspberry Pi frame rates remain unmeasured. [ARM64 build and artifacts](https://github.com/EmilianoManaloIV/blendity/actions/runs/44687303234)

The dependency-free x86-64 Linux build linked the editor, tests and stress executable. The animation group passed 5,082 checks and the editor group passed 3,819. Three documentation assertions initially failed in the scratch checkout because its archive omitted the root README and credits. Restoring both tracked files from the tested revision and rerunning the editor group resolved all three without changing the executable. Evidence: `C:/blab/affinity613/linux-animation.log` and `linux-editor-verified.log`.

On Windows x64, the MSVC 14.51 full-library build passed all five explicit-worker checks, four foreground-ceiling checks and four background checks at each configured budget of one, two and three lanes. The built-in background loop used one thread at each budget. At three lanes the baseline used two background participants, while both builds used three foreground participants. The full animation group passed 5,082 checks. Windows logs are in `C:/blab/affinity613/win-db69edc8`.

The combined dependency-free Windows build at `4fa79e8b` also passed the explicit-worker, ceiling and three low-lane checks. Its animation, editor and HCI groups passed 5,078, 3,823 and 2,110 checks respectively. The full-library editor at `db69edc8` passed 3,849. Both Windows applications produced a 128 × 96 PNG on startup.
