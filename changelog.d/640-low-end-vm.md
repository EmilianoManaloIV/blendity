## 2026-11-26 (round 640): Measure four low-end Linux VM profiles

**Asked:** Exercise the editor under several low-end device budgets, including a Pi-class SBC.

**Added (task 0622):**
- Four native Linux CPU and RAM profiles with scratch-only editor checks, hosted ARM64 automation and a repeatable x86-64 VM procedure.

**Checked:** On final joined revision `6ed52dc0`, four profiles reopened the edited scene and rendered it through the CPU viewport and F12 camera. The hosted ARM profiles recorded 3.36–5.09 ms steady orbit means across six samples. Two joined WSL profiles recorded 3.72–3.79 ms. See the [VM report](docs/perf/0622-low-end-vm.md) for caps, image checks and memory peaks.

<!-- docs/PERFORMANCE.md -->
append:
## Low-end Linux VM profiles (round 640)

The [four-profile report](perf/0622-low-end-vm.md) records native Linux CPU editor measurements under enforced CPU and RAM budgets and the limits of the device comparisons.
