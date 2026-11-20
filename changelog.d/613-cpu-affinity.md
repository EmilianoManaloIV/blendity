## 2026-11-19 (round 613): Size automatic CPU work to Linux processor affinity

**Asked:** Improve performance on machines and sessions with fewer processor resources.

**Changed (task 0595):** Linux uses the main thread's inherited CPU mask when sizing the built-in worker pool, texture readers, UV workers and optional inference sessions. Preferences reports the same available count. Built-in background jobs use the background caller when their budget leaves one lane.

**Checked:** On the previous Linux build a process restricted to one or two CPUs created 32 threads in the scheduling test. The regression launches fresh processes with a single CPU, a noncontiguous pair and the full allowed mask, and checks foreground and background scheduling and exact results.
