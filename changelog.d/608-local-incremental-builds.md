## 2026-11-19 (round 608): Incremental local Windows builds

**Asked:** Shorten the Hub's local editor refresh by reusing verified MSVC objects between published revisions.

**Added (task 0590, ADR 0155)**
- `BLENDITY_INCREMENTAL=1` opts the Windows release editor and unit tests into a separate scratch cache. It uses the clean build's resolved source lists, library selection and compiler flags, including LTCG.
- The worker checks transitive include contents and search candidates, relinks each requested target as needed and validates the staged runtime. It publishes one complete package at a time and keeps earlier packages available while their executables are in use.

**Checked**: Clean and incremental camera renders matched at both tested scene geometries, and all 101 runtime, license and shader files matched by SHA-256. Eight independent native build scenarios passed. On the owner machine, the clean editor took 234.4 s, an empty incremental cache took 303.4 s, an unchanged request took 19.4 s, and a documentation-only request took 18.3 s. The Hub now uses the incremental path for routine local editor refreshes. Task 0590 records timings, conditions and full-suite results.
