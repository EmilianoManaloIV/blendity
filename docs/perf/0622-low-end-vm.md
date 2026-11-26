# Low-end Linux VM measurements

Run the four profiles against the same product revision to exercise Blendity under bounded CPU and RAM. A hosted ARM64 VM and an x86-64 Linux VM each run two profiles in sequence. Build one native release editor per architecture, with optional libraries and OptiX disabled, before taking measurements.

| Profile | Native architecture | Allowed logical CPUs | Memory ceiling | Swap ceiling |
| --- | --- | ---: | ---: | ---: |
| `pi-sbc` (Pi-class SBC) | ARM64 | 4 | 8 GiB | 0 |
| `arm-notebook` (modest Linux notebook) | ARM64 | 2 | 4 GiB | 0 |
| `school-laptop` (older school laptop) | x86-64 | 2 | 4 GiB | 0 |
| `thin-client` | x86-64 | 2 | 2 GiB | 0 |

## Reproduce

On a native Linux VM with systemd and cgroup v2, build the executable outside the measurement unit. From the repository root, choose a new output directory under scratch.

```bash
set -e
BLENDITY_NO_LIBS=1 BLENDITY_NO_OPTIX=1 bash build.sh release app
scratch=$(mktemp -d)
for profile in school-laptop thin-client; do
  python3 tools/dev/low_end_vm.py --user "$profile" "$PWD/dist/linux/Blendity" "$scratch/$profile"
done
```

The default launcher uses a transient system unit and runs the workload as the caller. Use `--user` with a user manager that can enforce the required limits. On the development machine the Hub assigns the exclusive timing window before any editor measurements.

The hosted ARM64 [diagnostic workflow](../../.github/workflows/low-end-vm.yml) runs manually once available on the default branch. The Hub can also push a reviewed commit to the dedicated `diagnostics/0622-low-end-vm` branch. The job measures both ARM profiles on the checked-out commit with a 60-minute limit. The x86 command above covers both local VM profiles. Hosted runner provenance and profile evidence are retained as Actions artifacts for seven days. The x86 command retains its evidence under the printed scratch paths.

## What the workload measures

Each profile starts four editor processes in the same fresh unit. The seed process creates a 64-cube grid and saves the scene. The edit process opens it, subdivides a sphere and saves a new scene. The reopen process verifies the edited mesh counts and records three 30-move scripted orbits, including the first move and the mean and longest of the remaining 29 moves. The final process reopens the edit, saves a 1280 × 720 rasterized camera render, removes the grid in memory, and saves a second camera render. Each image comparison counts RGB changes of at least eight levels in a 320 × 250 sample. The camera control holds the viewpoint and lighting constant.

In each profile directory, `constraints.json` records the kernel limits and affinity. `result.json` includes timings, kernel-reported process peak RSS, image hashes and scene counts. Stage logs, command lists, live process samples, scene files and PNGs accompany it. Compare process peak RSS with like processes. The cgroup peak also includes the sampling process and charged file cache. A missing `memory.peak` on an older kernel leaves the cgroup peak unavailable.

## Interpretation

The four labels describe resource budgets. The CPUs execute the host instruction set at host speeds. Measurements include the host's cache hierarchy, scheduler and storage. The RAM limit covers allocations charged to the workload cgroup. The guest kernel and desktop do not share a simulated device's entire physical memory budget with the editor. The headless CPU viewport exercises the editor framebuffer. Display latency, X11 presentation, actual low-end CPU performance, GPU drivers, shared GPU memory, thermal throttling and device storage speed require physical or desktop testing. The [Pi hardware acceptance procedure](0578-pi5-acceptance.md) covers that follow-up.

## Observations

All four profiles completed on joined product `1ba53c90103fe15beae6d65d895628b4f858d859`. The two ARM profiles ran on the same hosted Neoverse-N2 VM. The two x86 profiles ran on the joined Linux executable in the local Ryzen 9 5900XT WSL2 VM. A separate hosted x86 build stopped at a missing `X11/XKBlib.h` before starting either profile. [Hosted workflow logs and artifacts](https://github.com/EmilianoManaloIV/blendity/actions/runs/45887472189)

| Profile and VM | Orbit means, three samples (ms/frame) | Longest steady orbit frame (ms) | Open / subdivide / save / reopen (ms) | First F12 elapsed (ms) | Editor peak RSS / unit peak (MiB) |
| --- | --- | ---: | --- | ---: | ---: |
| Pi-class SBC, hosted ARM | 3.64, 3.76, 3.36 | 4.4 | 19.3 / 19.5 / 2.6 / 17.0 | 319.0 | 129.0 / 128.6 |
| ARM notebook, hosted ARM | 5.22, 5.31, 5.48 | 5.6 | 24.8 / 26.2 / 2.6 / 25.5 | 652.2 | 128.6 / 126.6 |
| School laptop, local WSL | 3.77, 3.82, 3.78 | 4.2 | 23.2 / 23.7 / 2.8 / 21.2 | 413.4 | 130.4 / 127.1 |
| Thin client, local WSL | 3.78, 3.76, 3.82 | 4.4 | 20.3 / 21.2 / 1.7 / 21.1 | 410.7 | 130.5 / 127.0 |

The three orbit means each cover the 29 steady move frames after the first move. The action columns are elapsed times between editor marks, including the scripted redraws. The first F12 mark includes completion of the camera raster, before its PNG save. Its longest individual frame was 11.6, 18.5, 19.7 and 15.3 ms respectively. All four runs used the Low tier at 1280 × 720. On the shared ARM host, moving from the four-core profile to the two-core profile roughly doubled this first F12 time. The x86 runs used the same two cores. The smaller RAM ceiling produced no observed memory pressure in this scene. None of the four cgroups recorded a max-limit hit or OOM. Peak editor RSS and unit-charged peak count memory differently and need not rank in the same order.

Every live editor sample showed the verified cgroup and requested CPU mask. The scene had 72 objects. Subdivision took it from 1,413 vertices and 1,284 faces to 2,541 vertices and 2,388 faces, and the new counts survived a fresh process reopening the save. Manual inspection of all four before/after viewport pairs showed the colored cube grid from two angles. The full camera image showed the grid in front of the camera. Removing it exposed the gray cube, yellow cylinder, blue sphere and floor. In the 80,000-pixel sample, orbit changed 24,209 pixels on ARM and 24,206 on WSL. Grid removal changed 75,360 and 75,388 respectively. The automated orbit check detects image change. The separate camera control tests whether scene geometry contributes to the rendered output.

The hosted ARM editor SHA-256 was `e18311fe2eea3592b24e12adb4048f67eb86a4e27eb59deeb6d0d59bd9503aa6`. The local Linux editor SHA-256 was `13297b71c07e02d5ddf39418ec2a54d8fadb8c203fc627afffadb670f65ef9e0`. The WSL profiles ran serially under the Hub's exclusive window on 2026-11-26 at 12:18 Pacific, with both Windows build slots and the WSL lock held. The window recorded zero overlaps.
