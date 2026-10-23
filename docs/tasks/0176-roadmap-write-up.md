# 0176: the roadmap of tasks 0156-0191 written up

- **Status:** done (round 210)
- **Requirements:** the roadmap of 2026-10-21 (the plan's "Stack 0" to "Stack 12")
- **Decisions:** ADRs 0051-0060 (with 0031, 0034, 0048, 0049, 0053, 0054, 0057, 0058, 0059, 0060 amended)
- **Model:** main session
- **PR:** #155 (documentation only, stacked on #154)

What the roadmap asked for and what happened, task by task: the round, the PR, and the numbers before and after - against
Blender 5.2.2 where Pass 13's table has a row (docs/PERFORMANCE.md). Every PR is stacked on the one before (#119 on
#118, ... #155 on #154): merging the last merges them all. The measurements are from this PC (AMD 5900XT, RTX 4070
SUPER and RTX 3060 Ti) on a quiet machine unless a row says otherwise. Targets that were missed are marked as missed.

## Stack 0: the project's problems
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0156 IK handles | 174, #119 | handles sized in the rig's units (a centimetre rig's no longer 100x too big), a Rig Control Size preference, rotatable IK targets turning the hand / foot (Rotate End) | the drawn box 20 % of the shin at rig scales 0.01, 1 and 100 (ADR 0031 amended) |
| 0157 baked emission | 175, #120 | emissive meshes sampled directly in the bake, MIS with the hemisphere, a firefly clamp, Preview Lighting at half density | a 10 cm lamp over a floor: 1.4 % RMS from the analytic irradiance (was noise-limited), 274x smoother than the hemisphere alone, the GPU gather within 3.5 % of the CPU |

## Stack 1 / 1b: the Wind gizmo, compaction, the benchmark
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0158 Wind gizmo | 176, #121 | icons for Wind, Rotator, Oscillator; the Wind's arrow, streamlines and rings | - |
| 0177 compaction | 177, #122 | one test per behaviour, timings to stress (ADR 0056), the suite in files and groups, dead code out | the suite's test time 204.6 -> 117.4 s (target 160) |
| 0178 the Blender benchmark | 178, #123 | `tools/bench_blender.py` in Blender 5.2 headless, `tools/bench.bat`, Pass 13's table | Blendity ahead on path tracing (7.1x on RT hardware, 1.4x compute, 2.4x CPU), UV (1.2x, 1.6x), bake (17.5x), GLB export (2.3x); behind in Edit Mode (entry 1.4x, extrude 3.0x, Select More 3.3x, undo 1.5x) |

## Stack 2: the GPU pipeline and the distance artifacts
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0159 GPU time in the Profiler | 179, #124 | timestamp queries, GPU busy / wait | F12 from the editor: the card busy 21 % of the render's wall time; 99.8 % inside each submission |
| 0160 a view in one submission | 180, #125 (ADR 0051) | record per view, flush once | the GPU path a frame 4.09 -> 3.70 ms |
| 0161 the path tracer off the main thread | 181, #126 (ADR 0052) | persistent device workers, two submissions in flight | F12 with both cards busy 41-43 % -> 49 % of wall time; samples a second unchanged on the stress scene (122 compute, 800 ray-tracing hardware: a card was already 99 % busy inside a submission); every editor frame under 10 ms during F12 |
| 0162 incremental uploads | 182, #127 | one submission per upload, instances / materials / lights updated alone | a whole upload 11.4 ms in 1 submission (was 182-252 ms in 13+); one object moved 4.9 ms |
| 0163 fewer CPU fallbacks | 183, #128 | area lights and cutouts on the card | the GPU path 3.58 -> 3.50 ms |
| 0164 Toon at distance | 184, #129 (ADR 0048 amended) | painted maps filtered by footprint, cel edges one pixel wide, outlines at least a pixel | the far pictures' high-frequency energy within the target (task doc) |

## Stack 3: Edit Mode's costs (before the pass of 0185)
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0165 operators without the waste | 185, #130 (ADR 0053) | one mesh copy, Auto Smooth's count 23 -> 4 ms | extrude 130.6 -> 48.9 ms |
| 0166 selection, not the mesh | 186, #131 | a selection operator no longer copies the mesh | Select More 32.2 -> 15.4 ms (CPU views), 7.9 (GPU views); Blender 15.5 |
| 0167 undo measured | 187, #132 (ADR 0053 amended) | the undo row measured honestly; undo gives back Edit Mode's selection | undo of an extrude 27.7 ms (CPU views), 11.0 (GPU views) |

## Stack 4: hair
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0168 hair as XPBD rods | 188, #133 (ADR 0049 amended) | mass-weighted XPBD rods, compliance, damping and air drag | a 40 cm strand released horizontally settles within 1 cm in 0.75 s with no ring (was 14 wiggles over 2 mm); ten times the density sags further (0.386 vs 0.177 m) and settles later (0.98 s) |

## Stack 5: OptiX
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0169 shipping OptiX | 189, #134 (ADR 0054) | committed PTX, NVIDIA's headers fetched at build, the driver at run time | - |
| 0170 / 0171 the OptiX device, toon parity | 190, #135 (ADR 0054 amended) | an OptiX render device with GPU Compute's picture, toon fields included | the same picture (0.00000 apart); **the speed targets missed**: 0.32x GPU Compute on the 4070 SUPER, 0.20x on the 3060 Ti (target 1.4x / 1.8x). OptiX stays opt-in |

## Stack 6 / 7: Live Link and the bridges
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0172 Live Link | 191, #136 (ADR 0055) | a local server, line JSON + GLB, one-way | one client 1.00 ms a frame; 1,000 moved objects compared in 1.23 ms |
| 0173 the Blender bridge | 192, #137 | an add-on applying the stream in Blender | replayed fixture in the suite; run by hand in Blender 5.2 |
| 0174 the Unity bridge | 193, #138 | a Unity package | its C# not run here (no Unity on this PC): stated in the task doc |
| 0175 Godot and Unreal | 194, #139 | specified, not built | - |

## Stack 8 / 9b: UVs
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0179 Part Unwrap | 195, #140 (ADR 0057) | PartUV's search on a geometric part tree, ABF++ beside LSCM | the teapot 58 -> 11 charts (Smart UV Project -> Part Unwrap), seams 42.9 -> 15.7 m; the cube + boss + torus 22 -> 6 |
| 0180 PartField | 196, #141 (ADR 0057 amended) | the opt-in model path; the user exports the model | tested with a stand-in model; the real model's conversion is the user's |
| 0186 the UV editor by material | 197, #142 | one material's faces at a time | - |

## Stack 9: the Editing tab
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0181 the Editing window | 198, #143 (ADR 0058) | a linear-float adjustment stack, non-destructive | a 4K 11-step stack 683 ms in full (**target 150 missed**), a last-step slider 30.7 ms (target 40 met) |
| 0182 masks | 199, #144 | gradient, range, colour and painted masks | - |
| 0183 textures and the camera look | 200, #145 | Apply to Texture, Use as Camera Look | - |
| 0191 the Editing tab at 4K | 201, #146 | a preview at the window's size, tables for the display axis | the 4K stack while working 65.4 ms, a slider 2.4 ms (the 0181 targets met); full size for export 412 ms |
| 0184 room to grow | 202, #147 | specified, not built | - |

## Stack 11: windows and the tablet
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0187 secondary windows | 203, #148 (ADR 0059) | tabs in OS windows on any display, per-window DPI | checked by hand on three displays |
| 0188 render-only and tablet | 204, #149 (ADR 0059 amended) | full-screen picture window, pen tilt, eraser, palm rejection, Tablet mode | - |

## Stack 12: the interaction pass
| task | round, PR | what changed | measured |
|---|---|---|---|
| 0189 the palette | 205, #150 (ADR 0060) | the command palette, Quick Favorites, the status bar's hints | the audit's 12 tasks 28 -> 27 actions (4 %, **the 30 % target missed**: the paths were already short; the first count was wrong and corrected) |
| 0190 essentials and guides | 206, #151 (ADR 0060 amended) | essential fields first, a guide per layout, Open Recent, typed gizmo moves | not measurable by action counts (recall and choice): a newcomer study would measure it |

## Stack 10: the performance pass against Blender (0185)
| row | round, PR | before | after | Blender 5.2.2 | target (half the gap) |
|---|---|---|---|---|---|
| Undo of the extrude | 207, #152 | 25.7 ms | about 21.0 | 15.7 | missed by about 0.4 ms |
| Extrude three faces | 208, #153 (ADR 0053 / 0034 amended) | 48.2 ms | 40.1-41.5 | 31.7 | missed by up to 1.5 ms |
| Entering Edit Mode | 209, #154 | 24.5 ms | 18.0-18.4 | 18.1 | met (level) |

The views were on the CPU (the stress default). What is left in each frame is drawing the mesh, which headless Blender
doesn't do; with the views on the GPU every Edit Mode row is level with Blender or ahead (undo 13.0-14.5 ms, Select
More 8.6-11.0, Edit Mode entry 18.6-21.5, extrude 38.1-41.6).

## What stayed open
- OptiX is slower than GPU Compute on these cards (0170); it stays opt-in.
- Godot and Unreal bridges (0175) and the Editing tab's layers / healing (0184) are specified, not built.
- The Unity bridge's C# and PartField's model conversion haven't run on this PC (0174, 0180).
- With Realtime GI on, entering Edit Mode still redraws the Game view (0185, round 3's note).
- Two address-keyed caches (the RSM key, each object's `materials_key`) miss after an undo (0185, round 1's note).
- The interaction pass's value needs a study with newcomers (0189, 0190).

## Lessons (Learn tab)
Each feature round added or rewrote a Learn lesson with a Try-it and questions (named in its CHANGELOG entry): among
them the baked lamp, the cards working between frames, the Profiler's GPU block, developing a picture, masks, textures
and the camera, windows on other screens, find anything and essentials first. The performance rounds (0185) changed
no feature and added none.
