# 0595. Size automatic CPU work to the available processor set

Round 613. Approved by the owner on November 19 under the application-wide performance and hardware compatibility goal.

## Evidence and scope

Linux on the development host reports 32 online logical CPUs. On the merged dependency-free build, launching the animation thread-ceiling test with `taskset -c 0,2` still reported 32 available threads and ran three distinct threads under a ceiling of three. The OS allowed the process only two CPUs. This concerns inherited CPU affinity, including restricted sessions on ARM64 Linux. It does not measure Pi performance or fractional container CPU quotas.

## Change

Use the Linux affinity mask for automatic CPU scheduling in the built-in job system and the existing shared hardware-thread query. Align automatic texture readers, UV workers and optional inference thread defaults with that query. Preserve explicit job-system worker requests, TBB scheduling and processor detection on Windows and macOS. Handle affinity-query failure with the current portable fallback. Avoid a syscall on each parallel loop. On all platforms the built-in background caller runs serially when the effective budget is three lanes or fewer, and a single available CPU limits automatic texture reading to one reader.

Ownership: core system and job sizing, automatic scene worker sizing, focused scheduling tests, this spec and the round 613 changelog fragment. Separate from GPU rendering and Preferences input handling.

## Verification

On Linux launch fresh processes unrestricted, with one CPU, and with a noncontiguous pair drawn from the process's allowed mask. Compare the selected count and real worker usage, exercise small parallel loops and explicit limits, and verify exact output. Measure matched workload latency or CPU/task counters before making a speed claim. Run existing job, animation and editor tests, compile both Windows dependency modes and verify the native ARM64 build where available. Record any hardware limits and link independently reviewed evidence before publication.

Reference: [Linux affinity semantics](https://man7.org/linux/man-pages/man2/sched_getaffinity.2.html) and ADR 0106 for background jobs.
