# Round 506: the Scene view's kept per-frame work - the timed A / B

One timed run on a quiet machine (2026-11-14 07:55, 32 threads, the views on the card, Windows with Blender's libraries),
`blendity_stress --only scene_views`: every row with round 506's keeps off, on, and off again in one process. The keeps
that are in this table's last part and showed no gain (the shadow fit, the staggered walks) were taken out after this run.

## The windowed app (the row of `docs/perf/sweep-504.md`, line 1)

`Blendity.exe` windowed on a scratch project, the camera orbiting, `BLENDITY_VIEW_KEEPS=0` against the default; two runs each.

| row | keeps off: frame ms | keeps on: frame ms | off: Scene view ms | on: Scene view ms |
|---|---|---|---|---|
| 50,000 cubes, Scene view, run 1 | 39.81 | 9.63 | 32.82 | 5.33 |
| 50,000 cubes, Scene view, run 2 | 32.6 | 10.16 | 25.14 | 5.51 |
| 10,000 cubes, Scene view, run 1 | 8.54 | 3.65 | 5.65 | 1.5 |
| 10,000 cubes, Scene view, run 2 | 7.42 | 3.61 | 4.9 | 1.47 |
| an empty scene, Scene view, run 1 | 2.34 | 2.36 | 0.82 | 0.81 |
| an empty scene, Scene view, run 2 | 2.35 | 2.34 | 0.82 | 0.81 |

## Every row, keeps off / on / off again

| row | size | median ms | p95 ms | longest ms | frames | budget |
|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | 31.4 | 43.0 | 44.2 | 60 | OVER |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | 13.3 | 24.7 | 26.4 | 60 | ok |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | 35.7 | 46.9 | 48.9 | 60 | OVER |
| the camera orbiting, Solid, keeps off | 50,000 cubes | 53.6 | 65.6 | 68.4 | 60 | OVER |
| the camera orbiting, Solid, keeps on | 50,000 cubes | 18.0 | 29.6 | 35.0 | 60 | OVER |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | 52.7 | 64.2 | 70.9 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | 121.4 | 128.8 | 134.2 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | 77.2 | 84.8 | 89.2 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | 121.3 | 127.9 | 130.6 | 60 | OVER |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | 243.3 | 253.9 | 276.5 | 60 | OVER |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | 206.1 | 227.5 | 248.2 | 60 | OVER |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | 242.6 | 265.0 | 276.0 | 60 | OVER |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | 5.4 | 22.9 | 25.6 | 60 | ok |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | 7.6 | 10.1 | 12.6 | 60 | ok |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | 7.7 | 10.6 | 11.7 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | 4.5 | 6.3 | 7.1 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | 3.5 | 5.5 | 6.5 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | 4.5 | 6.3 | 6.6 | 60 | ok |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | 6.9 | 9.1 | 9.3 | 60 | ok |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | 3.7 | 6.0 | 6.8 | 60 | ok |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | 7.0 | 9.1 | 11.6 | 60 | ok |
| the camera orbiting, Solid, keeps off | 10,000 cubes | 9.0 | 11.3 | 11.8 | 60 | ok |
| the camera orbiting, Solid, keeps on | 10,000 cubes | 3.4 | 6.0 | 6.4 | 60 | ok |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | 8.8 | 11.4 | 14.2 | 60 | ok |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | 25.1 | 27.8 | 28.7 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | 18.4 | 22.1 | 23.1 | 60 | OVER |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | 28.0 | 30.1 | 31.4 | 60 | OVER |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | 46.2 | 49.6 | 52.9 | 60 | OVER |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | 39.6 | 43.6 | 48.5 | 60 | OVER |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | 45.9 | 51.5 | 55.1 | 60 | OVER |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | 1.6 | 5.2 | 5.7 | 60 | ok |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | 2.2 | 2.6 | 3.3 | 60 | ok |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | 2.2 | 2.7 | 2.8 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | 1.7 | 1.9 | 2.2 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | 1.5 | 1.8 | 2.0 | 60 | ok |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | 1.6 | 1.9 | 2.1 | 60 | ok |

## Off against on

| row | size | median ms, keeps off | median ms, keeps on | median ms, keeps off (again) | off / on (the dearer off) | longest ms, off | longest ms, on |
|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded | 50,000 cubes | 31.4 | 13.3 | 35.7 | 2.68x | 48.9 | 26.4 |
| the camera orbiting, Solid | 50,000 cubes | 53.6 | 18.0 | 52.7 | 2.98x | 70.9 | 35.0 |
| Scene and Game view, the camera orbiting, Shaded | 50,000 cubes | 121.4 | 77.2 | 121.3 | 1.57x | 134.2 | 89.2 |
| one object moved every frame, Shaded | 50,000 cubes | 243.3 | 206.1 | 242.6 | 1.18x | 276.5 | 248.2 |
| the pointer over a panel, the view still | 50,000 cubes | 5.4 | 7.6 | 7.7 | 1.00x | 25.6 | 12.6 |
| the Hierarchy shown, the pointer moving over it | 50,000 cubes | 4.5 | 3.5 | 4.5 | 1.29x | 7.1 | 6.5 |
| the camera orbiting, Shaded | 10,000 cubes | 6.9 | 3.7 | 7.0 | 1.87x | 11.6 | 6.8 |
| the camera orbiting, Solid | 10,000 cubes | 9.0 | 3.4 | 8.8 | 2.61x | 14.2 | 6.4 |
| Scene and Game view, the camera orbiting, Shaded | 10,000 cubes | 25.1 | 18.4 | 28.0 | 1.52x | 31.4 | 23.1 |
| one object moved every frame, Shaded | 10,000 cubes | 46.2 | 39.6 | 45.9 | 1.17x | 55.1 | 48.5 |
| the pointer over a panel, the view still | 10,000 cubes | 1.6 | 2.2 | 2.2 | 1.00x | 5.7 | 3.3 |
| the Hierarchy shown, the pointer moving over it | 10,000 cubes | 1.7 | 1.5 | 1.6 | 1.10x | 2.2 | 2.0 |

## Where a frame went (ms a frame)

| row | size | keeps | Scene view ms | UI ms | card total ms | instances ms | card render ms | collect ms | scene hash ms | overlays ms | lighting ms | items copy ms | instance key ms | shadow fit ms | unsupported ms | card writes ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 24.03 | 8.54 | 8.72 | 0.97 | 7.52 | 0.00 | 0.54 | 1.80 | 8.57 | 0.81 | 0.96 | 0.00 | 3.25 | 4.51 |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 7.71 | 7.47 | 3.81 | 0.00 | 3.61 | 0.00 | 0.54 | 2.00 | 1.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 28.20 | 8.51 | 12.42 | 0.96 | 11.21 | 0.00 | 0.64 | 2.04 | 8.69 | 0.82 | 0.96 | 0.00 | 3.25 | 4.61 |
| the camera orbiting, Solid, keeps off | 50,000 cubes | off | 45.88 | 8.92 | 15.25 | 1.17 | 13.84 | 0.00 | 0.56 | 2.01 | 0.00 | 0.00 | 1.17 | 0.00 | 3.57 | 4.56 |
| the camera orbiting, Solid, keeps on | 50,000 cubes | on | 11.47 | 7.98 | 8.54 | 0.00 | 8.34 | 0.00 | 0.55 | 2.03 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | off | 45.36 | 8.80 | 15.17 | 1.14 | 13.79 | 0.00 | 0.64 | 2.14 | 0.00 | 0.00 | 1.14 | 0.00 | 3.44 | 4.57 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 48.11 | 22.55 | 41.66 | 2.97 | 31.57 | 0.00 | 4.34 | 14.06 | 26.37 | 2.45 | 2.97 | 0.00 | 9.98 | 13.95 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 34.76 | 22.35 | 35.63 | 0.00 | 28.38 | 0.00 | 4.33 | 13.89 | 0.95 | 0.00 | 0.00 | 0.00 | 0.00 | 11.12 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 47.90 | 22.25 | 41.36 | 2.93 | 31.42 | 0.00 | 4.36 | 14.05 | 26.05 | 2.43 | 2.93 | 0.00 | 10.03 | 13.84 |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | off | 86.44 | 22.66 | 81.75 | 42.47 | 31.81 | 79.52 | 4.46 | 13.99 | 26.62 | 3.01 | 3.04 | 0.00 | 10.25 | 14.12 |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | on | 75.78 | 23.02 | 79.50 | 42.70 | 29.36 | 80.34 | 4.43 | 14.41 | 0.96 | 0.00 | 3.03 | 0.00 | 3.59 | 11.64 |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | off | 86.40 | 22.91 | 82.05 | 42.74 | 31.81 | 79.47 | 4.37 | 14.07 | 26.78 | 3.00 | 3.07 | 0.00 | 10.30 | 14.11 |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | off | 0.68 | 7.02 | 0.00 | 0.00 | 0.00 | 0.00 | 0.59 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | on | 0.63 | 7.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.52 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | off | 0.71 | 6.99 | 0.00 | 0.00 | 0.00 | 0.00 | 0.60 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | off | 0.61 | 3.65 | 0.00 | 0.00 | 0.00 | 0.00 | 0.50 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | on | 0.63 | 2.78 | 0.00 | 0.00 | 0.00 | 0.00 | 0.53 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | off | 0.69 | 3.53 | 0.00 | 0.00 | 0.00 | 0.00 | 0.58 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 5.07 | 1.94 | 2.11 | 0.19 | 1.75 | 0.00 | 0.12 | 0.38 | 1.52 | 0.13 | 0.19 | 0.00 | 0.67 | 0.61 |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 2.11 | 1.87 | 1.22 | 0.00 | 1.05 | 0.00 | 0.12 | 0.45 | 0.19 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 5.20 | 1.94 | 2.15 | 0.19 | 1.79 | 0.00 | 0.14 | 0.44 | 1.53 | 0.14 | 0.19 | 0.00 | 0.67 | 0.63 |
| the camera orbiting, Solid, keeps off | 10,000 cubes | off | 7.13 | 2.05 | 2.44 | 0.20 | 2.04 | 0.00 | 0.12 | 0.37 | 0.00 | 0.00 | 0.20 | 0.00 | 0.67 | 0.80 |
| the camera orbiting, Solid, keeps on | 10,000 cubes | on | 1.85 | 1.86 | 1.20 | 0.00 | 1.05 | 0.00 | 0.11 | 0.42 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | off | 7.15 | 1.98 | 2.40 | 0.20 | 2.01 | 0.00 | 0.14 | 0.42 | 0.00 | 0.00 | 0.20 | 0.00 | 0.68 | 0.79 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 10.41 | 4.54 | 9.65 | 0.60 | 7.32 | 0.00 | 0.83 | 2.70 | 4.51 | 0.49 | 0.60 | 0.00 | 2.10 | 2.81 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 8.22 | 4.38 | 10.54 | 0.00 | 8.89 | 0.00 | 0.73 | 2.45 | 0.17 | 0.00 | 0.00 | 0.00 | 0.00 | 2.20 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 12.01 | 4.40 | 13.48 | 0.55 | 11.27 | 0.00 | 0.75 | 2.45 | 4.27 | 0.46 | 0.55 | 0.00 | 1.92 | 2.69 |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | off | 17.70 | 5.17 | 19.88 | 6.69 | 11.65 | 11.08 | 0.91 | 2.53 | 4.44 | 0.51 | 0.52 | 0.00 | 1.90 | 2.74 |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | on | 15.63 | 5.14 | 19.38 | 6.81 | 11.05 | 11.35 | 0.86 | 2.47 | 0.16 | 0.00 | 0.53 | 0.00 | 0.57 | 2.20 |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | off | 17.91 | 5.14 | 19.90 | 6.78 | 11.59 | 11.08 | 0.91 | 2.53 | 4.37 | 0.51 | 0.53 | 0.00 | 1.93 | 2.72 |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | off | 0.17 | 1.90 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | on | 0.19 | 1.89 | 0.00 | 0.00 | 0.00 | 0.00 | 0.11 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | off | 0.21 | 1.88 | 0.00 | 0.00 | 0.00 | 0.00 | 0.14 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | off | 0.18 | 1.37 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | on | 0.18 | 1.26 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | off | 0.19 | 1.35 | 0.00 | 0.00 | 0.00 | 0.00 | 0.09 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

## What a frame did (counts a frame; kept / missed)

| row | size | keeps | light walks | item copies | item hashes | shadow fits | unsupported walks | rows written | table writes | validations | solid lists | hierarchy rebuilds | kept/missed: card buffers | lighting | instance key | items copy | shadow fit | unsupported | solid items | hierarchy rows | interval walks | most in a frame |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 1.0 | 1.0 | 1.0 | 0.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 0 | 0 / 0 | 0 | 0 |
| the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 53 / 7 | 60 / 0 | 60 / 0 | 0 / 0 | 120 / 0 | 0 / 0 | 0 / 0 | 60 | 1 |
| the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 1.0 | 1.0 | 1.0 | 0.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 0 | 0 / 0 | 0 | 0 |
| the camera orbiting, Solid, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 1.0 | 0.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 | 0 | 0 |
| the camera orbiting, Solid, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 0 / 0 | 60 / 0 | 0 / 0 | 0 / 0 | 120 / 0 | 60 / 0 | 0 / 0 | 53 | 1 |
| the camera orbiting, Solid, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 1.0 | 0.0 | 2.0 | 50000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 | 0 | 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 150000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 180 / 0 | 180 / 0 | 0 / 0 | 360 / 0 | 0 / 0 | 0 / 0 | 7 | 1 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| one object moved every frame, Shaded, keeps off | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| one object moved every frame, Shaded, keeps on | 50,000 cubes | on | 0.1 | 0.0 | 3.0 | 0.0 | 3.0 | 150000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 0 / 180 | 180 / 0 | 0 / 0 | 180 / 180 | 0 / 0 | 0 / 0 | 7 | 1 |
| one object moved every frame, Shaded, keeps off (again) | 50,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 150000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| the pointer over a panel, the view still, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| the pointer over a panel, the view still, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 47 | 3 |
| the pointer over a panel, the view still, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| the Hierarchy shown, the pointer moving over it, keeps off | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 | 0 | 0 |
| the Hierarchy shown, the pointer moving over it, keeps on | 50,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 53 / 7 | 45 | 1 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 50,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 | 0 | 0 |
| the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 1.0 | 1.0 | 1.0 | 0.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 0 | 0 / 0 | 0 | 0 |
| the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 53 / 7 | 60 / 0 | 60 / 0 | 0 / 0 | 120 / 0 | 0 / 0 | 0 / 0 | 60 | 1 |
| the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 1.0 | 1.0 | 1.0 | 0.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 0.0 | 0.0 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 60 | 0 / 0 | 0 / 120 | 0 / 0 | 0 / 0 | 0 | 0 |
| the camera orbiting, Solid, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 1.0 | 0.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 | 0 | 0 |
| the camera orbiting, Solid, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 60 / 0 | 0 / 0 | 60 / 0 | 0 / 0 | 0 / 0 | 120 / 0 | 60 / 0 | 0 / 0 | 53 | 1 |
| the camera orbiting, Solid, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 1.0 | 0.0 | 2.0 | 10000.0 | 3.0 | 1.0 | 1.0 | 0.0 | 0 / 60 | 0 / 0 | 0 / 60 | 0 / 0 | 0 / 0 | 0 / 120 | 0 / 60 | 0 / 0 | 0 | 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps off | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| Scene and Game view, the camera orbiting, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 0.0 | 0.0 | 0.0 | 30000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 180 / 0 | 180 / 0 | 0 / 0 | 360 / 0 | 0 / 0 | 0 / 0 | 7 | 1 |
| Scene and Game view, the camera orbiting, Shaded, keeps off (again) | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| one object moved every frame, Shaded, keeps off | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| one object moved every frame, Shaded, keeps on | 10,000 cubes | on | 0.1 | 0.0 | 3.0 | 0.0 | 3.0 | 30000.0 | 6.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 173 / 7 | 0 / 180 | 180 / 0 | 0 / 0 | 180 / 180 | 0 / 0 | 0 / 0 | 7 | 1 |
| one object moved every frame, Shaded, keeps off (again) | 10,000 cubes | off | 3.0 | 3.0 | 3.0 | 0.0 | 6.0 | 30000.0 | 9.0 | 3.0 | 0.0 | 0.0 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 180 | 0 / 0 | 0 / 360 | 0 / 0 | 0 / 0 | 0 | 0 |
| the pointer over a panel, the view still, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| the pointer over a panel, the view still, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 47 | 3 |
| the pointer over a panel, the view still, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 | 0 |
| the Hierarchy shown, the pointer moving over it, keeps off | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 | 0 | 0 |
| the Hierarchy shown, the pointer moving over it, keeps on | 10,000 cubes | on | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 53 / 7 | 45 | 1 |
| the Hierarchy shown, the pointer moving over it, keeps off (again) | 10,000 cubes | off | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 1.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 60 | 0 | 0 |

## Each keep turned off alone (the Shaded orbit)

| keep turned off alone (orbit, Shaded) | size | median ms | p95 ms | longest ms | frames | this keep is worth ms (median) |
|---|---|---|---|---|---|---|
| all keeps on | 50,000 cubes | 18.8 | 28.9 | 29.9 | 60 | - |
| card buffers alone off | 50,000 cubes | 24.3 | 35.7 | 38.6 | 60 | 5.6 |
| lighting alone off | 50,000 cubes | 26.3 | 38.2 | 41.2 | 60 | 7.6 |
| instance key alone off | 50,000 cubes | 19.7 | 30.4 | 32.8 | 60 | 1.0 |
| items copy alone off | 50,000 cubes | 19.9 | 30.4 | 30.9 | 60 | 1.2 |
| shadow fit alone off | 50,000 cubes | 18.6 | 28.6 | 31.1 | 60 | -0.1 |
| unsupported alone off | 50,000 cubes | 22.1 | 32.8 | 34.0 | 60 | 3.4 |
| walks alone off | 50,000 cubes | 18.4 | 29.7 | 31.0 | 60 | -0.4 |
| solid items alone off | 50,000 cubes | 18.7 | 29.6 | 29.8 | 60 | -0.0 |
| hierarchy rows alone off | 50,000 cubes | 18.6 | 28.8 | 31.9 | 60 | -0.2 |
| all keeps on | 10,000 cubes | 7.5 | 9.9 | 10.1 | 60 | - |
| card buffers alone off | 10,000 cubes | 8.3 | 10.5 | 10.8 | 60 | 0.8 |
| lighting alone off | 10,000 cubes | 9.0 | 11.4 | 11.9 | 60 | 1.5 |
| instance key alone off | 10,000 cubes | 7.7 | 9.8 | 10.2 | 60 | 0.1 |
| items copy alone off | 10,000 cubes | 7.8 | 10.0 | 10.6 | 60 | 0.2 |
| shadow fit alone off | 10,000 cubes | 7.5 | 9.4 | 10.7 | 60 | -0.0 |
| unsupported alone off | 10,000 cubes | 8.3 | 10.4 | 11.7 | 60 | 0.8 |
| walks alone off | 10,000 cubes | 7.6 | 9.8 | 9.9 | 60 | 0.0 |
| solid items alone off | 10,000 cubes | 7.5 | 10.2 | 10.7 | 60 | -0.0 |
| hierarchy rows alone off | 10,000 cubes | 7.4 | 9.6 | 10.0 | 60 | -0.1 |
