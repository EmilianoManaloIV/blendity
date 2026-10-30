# tools/

What is here, and what is local-only.

| Tool | What it does | Run |
|---|---|---|
| `test_coverage_map.py` | Every console command, keymap action and window against the tests that exercise it (task 0177, ADR 0056); `--check` fails on an empty entry; `--widgets` adds the `u.button` labels and Inspector fields with the tests that drive each by label and counts them toward `--check` (task 0251) | `python tools/test_coverage_map.py [--widgets]` from `blendity/` |
| `hci_fitts_audit.py` | Reads the Fitts dump `tests_round271` writes (`fitts-1.0.json`, `fitts-1.5.json`: every window's and tool mode's widget rectangles) and reports the targets under 7 mm at 96 DPI and the index of difficulty from the Scene view's centre, per context and distinct (ADR 0085, task 0251) | `BLENDITY_HCI_DUMP_DIR=<dir> BLENDITY_TEST_FILTER=hci blendity_tests --group editor`, then `python tools/hci_fitts_audit.py <dir>` |
| `hci_klm.py` | Recounts every row of the keystroke-level model table (`docs/hci/klm.md`) from its operators (K 0.2, P 1.1, B 0.1, H 0.4, M 1.35 s) and prints the before and after totals; `--check` fails on a row whose seconds disagree with its operators (ADR 0085, task 0262) | `python tools/hci_klm.py [--check]` from `blendity/` |
| `hci_label_audit.py` | Lists the string literals under `src/editor` that carry a shortcut the keymap does not produce (other tools' keys and mouse gestures skipped); `--check` fails when any remain, for the chain once round 274 moves them to `shortcut_text` (ADR 0085, task 0251) | `python tools/hci_label_audit.py [--check]` from `blendity/` |
| `bench.bat`, `bench_blender.py`, `bench_table.py` | Pass 13's table (task 0178): Blendity's stress rows beside Blender 5.2's, run headless on the same scenes, written into `docs/PERFORMANCE.md`; `BLENDER=path` overrides where Blender is, `BENCH_GPU=name part` which card | `tools\bench.bat [--quick]` on a quiet machine |
| `clean.bat` | Removes `build\`, the stress reports a run wrote (restoring `latest.md`), the inbetweening model's `.venv`; `--all` removes `dist\` and the model's training runs too | `tools\clean.bat` |
| `ptx_compile.cpp` | NVRTC driver that compiles the OptiX kernel to PTX (task 0097); with `--header=` it writes the committed `src/render/optix/optix_kernel_ptx.h` with the sources' hash and the OptiX version (task 0169) | by `optix_ptx_embed.bat` (or `build.bat` with CUDA and no committed header) |
| `optix_ptx_embed.bat` | Writes the committed OptiX PTX header after the kernel changes; needs the CUDA Toolkit (`CUDA_PATH`) and the OptiX SDK (task 0169, ADR 0054) | `tools\optix_ptx_embed.bat` |
| `inbetween/` | The AI inbetweening model's training and export (task 0128): `train.py`, `export_onnx.py`; `.venv/` and `runs/` are local-only (in `.gitignore`) | see `inbetween/README.md` |
| `moonray/` | `clone.sh` and `linux_build.sh`: fetch and build MoonRay on Linux for the bridge (tasks 0112-0114) | by hand, see docs/tasks/0112 |

Local-only, never committed: `inbetween/.venv/`, `inbetween/runs/`, `__pycache__/`, `build/`, `dist/`, the stress
reports beside `stress/results/latest.md`. `tools\clean.bat` removes them.
