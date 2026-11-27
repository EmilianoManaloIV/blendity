## 2026-11-27 (round 659): Profile constrained Linux imports

**Asked:** Measure splat and Unity import on low-end ARM64 and x86-64 CPU profiles.

**Added (task 0641):**
- A repeatable million-record PLY and generated Unity-scene workload with kernel-enforced limits, live CPU affinity, process peak RSS and separate read, decode and serial validation timings.

**Checked:** See the [constrained import measurements](docs/perf/0641-constrained-imports.md).

<!-- docs/PERFORMANCE.md -->
append:
## Constrained splat and Unity imports (round 659)

The [import profile report](perf/0641-constrained-imports.md) records stage timings and memory peaks for a streamed PLY and a Unity scene under native Linux VM limits.
