# Constrained Linux import measurements

Task 0641 extends the [four Linux VM profiles](0622-low-end-vm.md) to cover binary Gaussian-splat PLY and Unity scene imports. These are native headless CPU measurements on resource-limited VMs. Physical device storage, desktop presentation, thermal behavior and GPU performance require separate measurements.

## Reproduce

In a native Linux checkout with the existing release build toolchain, X11 development headers, systemd and cgroup v2, build outside the measurement unit. Generate the PLY once in scratch. Each profile writes its evidence into a new directory.

```bash
set -eu
scratch=$(mktemp -d)
BLENDITY_NO_LIBS=1 BLENDITY_NO_OPTIX=1 bash build.sh release app
bash tools/dev/ply_import_probe.sh "$scratch/ply-probe"
"$scratch/ply-probe" generate "$scratch/million.ply" 1000000
for profile in school-laptop thin-client; do
    python3 tools/dev/low_end_vm.py --user "$profile" "$PWD/dist/linux/Blendity" \
        "$scratch/$profile" --imports "$scratch/ply-probe" "$scratch/million.ply"
done
```

Use `pi-sbc` and `arm-notebook` on ARM64. The [hosted ARM workflow](../../.github/workflows/low-end-vm.yml) accepts the manual `imports` workload and also runs it on the dedicated `diagnostics/0641-constrained-imports` branch. It spends at most 60 runner-minutes on one native ARM VM and runs the two profiles serially. The local user manager must delegate the memory controller. A disposable runner with passwordless sudo can use the default system-service launcher. In either mode the caller’s UID runs the child. The runner records the actual CPU affinity and hard memory/swap limits, checks inherited throttles and samples each workload process.

## What the timers cover

The PLY probe reads the production importer’s streamed binary vertex path. The little-endian fixture carries degree-3 spherical harmonics, with three invalid positions to exercise stable compaction. The input SHA-256 is computed immediately before import. Read time therefore measures buffered vertex reads after a full input pass. It excludes the header probe and file open. The 236,001,478-byte file has a 1,478-byte header and a 236,000,000-byte vertex payload across 29 bounded reads. The invalid rows are at the start, middle and end. Dropping the first row makes serial validation move every retained record, so its cost reflects compaction as well as checking. Five consecutive calls run in one process. Each call verifies all 999,997 retained positions. A digest covers the decoded attribute arrays and must match across calls. Timing stops before these checks. Stage timings and whole-process RSS are available in `result.json`.

The Unity fixture contains 256 named GameObjects, each with a Transform, MeshFilter and MeshRenderer referencing Unity’s built-in cube. Three fresh CPU editor processes open it and save their own result. Checks read the serialized scene and verify each cube’s unique saved ID, root parent, full transform, enabled renderer, eight-vertex, six-face cube mesh and original Unity object and transform IDs. The open mark includes the console open and wait. Process RSS also includes saving and the final 1280 × 720 editor screenshot.

## Results

Each PLY stage is the median of five imports in milliseconds. Stage medians are calculated independently. Peak RSS covers the whole probe process. The Unity column lists all three fresh-process open marks.

| Profile (CPUs / GiB) | Background lanes | Total | Setup | Vertex read | Decode | Change axes | Serial validate | PLY peak RSS (MiB) | Unity open (ms) | Unity peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|
| ARM SBC (4 / 8) | 2 | 238.51 | 68.84 | 23.59 | 22.20 | 83.41 | 37.46 | 236.60 | 6.9, 6.8, 7.0 | 42.47 |
| ARM notebook (2 / 4) | 1 | 334.72 | 66.26 | 22.82 | 39.64 | 166.55 | 36.72 | 236.61 | 6.9, 6.9, 6.9 | 42.45 |
| x86 school laptop (2 / 4) | 1 | 1162.74 | 90.95 | 891.58 | 27.34 | 124.52 | 25.51 | 237.53 | 58.7, 62.5, 64.1 | 42.05 |
| x86 thin client (2 / 2) | 1 | 1162.65 | 104.02 | 848.54 | 26.99 | 125.13 | 26.05 | 237.27 | 66.1, 66.3, 63.5 | 41.91 |

On the same Neoverse-N2 VM, reducing the importer from two background lanes to one nearly doubled the time to change axes. This stage transforms positions, rotations and degree-3 spherical harmonics. Buffered reads and serial validation changed by less than 1 ms each. The whole-import ranges were 236.77–251.28 ms and 333.67–347.46 ms respectively. The cgroup peaks were 244.93 and 242.70 MiB, with zero memory limit or OOM events. Each saved Unity scene contained 256 objects, 2,048 vertices and 1,536 faces.

The [hosted ARM run](https://github.com/EmilianoManaloIV/blendity/actions/runs/46005173172) used diagnostic commit `33228827a2db01072ca522eb7e6db90cceb402c6`. Its artifact includes runner and binary provenance, result JSON, process samples, editor logs, screenshots and the saved scenes. The editor SHA-256 was `cae787cbd515b5f980c90ffa0fd810780c6bc601f8bb1e27b92dfebd6d20dca5` and probe SHA-256 was `b523ae7b2555395cf0923e96933df5516d5b37d70b10e02e435b879c8e3bbb0f`. The generated PLY SHA-256 was `afed13e32bf3e4ab6e29761beb1c6733264498eb0a315e25560483f7aa6f3131`. Both profiles produced attribute digest `eb96fdb6d69f3f7b` on every repeat.

On local x86-64 WSL2, the warmed vertex reads from the Windows-backed D: volume through DrvFS took 73–77% of median total time. Axis conversion was the largest CPU stage at about 125 ms. Serial validation took 26 ms on both profiles. The total ranges were 1,155.70–1,262.25 ms and 1,119.11–1,233.29 ms. Both units peaked at 244.03 MiB with zero memory limit and OOM events. The two-CPU profiles used the same CPU set with different hard memory limits. These measurements cannot isolate an architecture effect from the host and filesystem differences.

The x86 probe and importer used diagnostic source `33228827`. The editor was relinked using the candidate importer and unchanged objects from `a3d6cc8c`, whose product sources and build script matched the diagnostic base. The x86 editor SHA-256 was `7e627c6061fd4779e50e0b25928fb46813996232e7e4e68883aaeeac2cd1dfeb` and probe SHA-256 was `50054eac6950f6399ab32c6853cbcdcf1c8704bf7e3fe0fff31c0624d6463ce5`. All ten local imports produced attribute digest `404bc89d46b240ba`. Each architecture passed the same fixture checks and produced repeatable results within its build. The full floating-point output digests differed between architectures.

## Same x86 importer on ext4 and DrvFS

We copied the same 236,001,478-byte PLY to the WSL2 ext4 filesystem, verified both files by SHA-256, and ran the thin-client profile with the same editor and probe. Only the PLY input path changed. Outputs and executables stayed on D:. The order was DrvFS, ext4, ext4, DrvFS. Each fresh probe process performed five imports after a full SHA-256 read of its input. All 20 imports produced the x86 reference digest.

| Input location | First process median read / total (ms) | Second process median read / total (ms) |
|---|---:|---:|
| Windows D: via DrvFS (9p) | 875.88 / 1152.86 | 872.48 / 1143.09 |
| WSL2 ext4 | 13.68 / 284.77 | 16.43 / 286.22 |

On this machine, moving the input to ext4 reduced median PLY time by about 0.86 seconds. Axis conversion stayed between 121.92 and 126.13 ms across all four process medians. Native ext4 and the hosted ARM profiles therefore agree on axis conversion as the largest measured stage on their warm native filesystem. The Windows-mounted input accounts for most of the additional elapsed time on this WSL configuration.

We also requested eviction for each input separately with Linux `POSIX_FADV_DONTNEED`. We recorded guest page residency with `mincore`, then immediately ran two imports. On ext4, 16,658 of 57,618 pages remained resident after the first eviction request and zero after the second. The first / repeat reads took 82.30 / 14.01 ms and 36.57 / 14.49 ms. DrvFS reported zero guest-resident pages both before and after eviction. Its first / repeat reads took 838.22 / 865.87 ms and 859.20 / 879.06 ms. Windows host cache and physical storage caches were uncontrolled, so these trials measure the effect of a guest eviction request only.
