# 0622: Low-end Linux VM profiles

- **Status:** in review
- **Authorization:** owner assignment via Pass 35 Hub, round 640, 2026-11-26.
- **Requirements:** Q-06 (responsiveness)
- **Decisions:** ADR 0026, 0105
- **Implementation:** planning lead owns diagnostic workflow and integration, implementer owns Linux measurement tooling.
- **PR:** [#504](https://github.com/EmilianoManaloIV/blendity/pull/504) against main. First published head 2caef8084bd9e48998bef2525796d79fafcf4068.

## Goal
Measure a reproducible CPU editor journey under four restricted Linux VM profiles.

## Scope
- ARM64 SBC 4 vCPU/8 GiB and notebook 2/4, x86-64 school laptop 2/4 and thin client 2/2.
- Native ARM64 on disposable hosted runners and native x86-64 in the local WSL2 VM, with scratch-only workloads and bounded diagnostic runs. Physical GPU, clock, thermals, storage and desktop presentation require separate hardware evidence.

## Likely files
- Linux runner under tools/dev, diagnostic workflow, performance report and round 640 changelog fragment.

## Acceptance criteria
1. Verify native ELF architecture, exact workload CPU affinity and hard cgroup memory/swap limits from inside each profile, accounting for inherited limits. Refuse an unavailable profile.
2. Validate the CPU editor at 720p on scratch assets, including open, edit, save, reload and rendered image content in both an orbiting viewport and a saved F12 raster render. Record per-profile interaction samples, memory peak and executable identity.
3. Keep normal CI triggers/checks intact, bound hosted cost, document exact reproduction and hardware coverage. Coordinate final combined rerun with Hub.

## Tests and checks to run
Exercise four real profiles and negative constraint/output checks. Independently inspect measurements, screenshots and artifacts. Product code is outside this round.

## Risks and edge cases
Shared runner variation, cgroup delegation, limited CPU count, and failures masked by successful editor exit.

## Documentation to update
Round 640 fragment and a measured report in docs/perf.

## Outcome
The implementer supplied the Linux runner. The task lead integrated the diagnostic workflow and camera control. The independent test engineer's 36 prelaunch fixtures passed, including refusal of wrong ELF, insufficient affinity, inherited throttling, corrupt or blank images, missing edits and unchanged camera output. In a real WSL user unit, the invalid editor fixture exited successfully but produced no editor evidence. The runner rejected it after observing CPUs 0–1, a 2 GiB leaf memory.max and zero swap. The independent reviewer approved the launcher on joined revision ef4d3110. The Hub launched byte-identical tooling on joined product 1ba53c90 in diagnostic commit 2f15bb63. Both hosted ARM profiles passed. The hosted x86 build failed before measurement because the runner lacked an X11 header. Both x86 profiles passed against the joined WSL build in an exclusive window. The [performance report](../perf/0622-low-end-vm.md) records all four sampled profiles, executable hashes, visual checks and coverage limits.

Independent verification reproduced the table from 1,258 live process samples and decoded all 28 PNGs. The final reviewer approved the four measured rows, hosted ARM automation and WSL x86 procedure. The documented shell sequence now propagates a first-profile failure. The hosted x86 recovery remains a separate CI package decision.

On the final joined product `6ed52dc0`, both hosted ARM64 profiles and both local WSL2 profiles passed again. The ARM run recorded the same complete Git tree as the joined build. The [final measurements](../perf/0622-low-end-vm.md#final-product-measurements) give the executable identities and per-profile results.
