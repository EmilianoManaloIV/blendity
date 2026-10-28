# 0250: the write-up - customize, sculpt, paint, stylize (rounds 250-269)

- **Status:** done (round 270)
- **Requirements:** R-14
- **Decisions:** ADRs 0068-0084 (summarized here)
- **Model:** main session
- **PR:** #216

## What the pass set out to do
The user asked (2026-10-27) for ZBrush's and Substance Painter's pillars with real R&D behind each, then the stylized
side - the hand-painted look and Ghibli-styled environments - on top of the drawing-tablet editor of the rounds before.
Twenty rounds, one PR each, stacked.

## What landed, in order
| Rounds | Tasks | What | ADR |
|---|---|---|---|
| 250-255 | 0230-0235 | The Wheel; custom panels, a shortcut for anything, a UI file; windows for a drawing tablet; the Draw tab; the Layers panel; the HCI and modularity pass (a module registry, tabs that fit) | 0068-0073 |
| 256-258 | 0236-0238 | Sculpt Mode: the foundation (brushes that touch only what is under the pen), the brush set with symmetry, the lazy mouse, alphas and stroke types, masks / polygroups / hiding | 0074 |
| 259-262 | 0239-0242 | Dynamic Subdivision; DynaMesh (winding-number inside test, surface nets); the ZRemesher spike (QuadriFlow on a worker, GO); Sculptris Pro (Dyntopo) | 0075-0078 |
| 263-264 | 0243-0244 | Mesh maps (normal, position, curvature, occlusion, thickness); the layer stack on a surface with mask stacks, smart masks, groups, the rect engine, async evaluation | 0079-0080 |
| 265-266 | 0245-0246 | Painting on the model by projection with tile undo; smart materials, texture sets, 2D painting, stencils | 0081-0082 |
| 267-268 | 0247-0248 | The hand-painted look (tips, wet mixing, jitter, grain, Smudge / Blend, baked light, the Hand-painted model, filters); stylized environments (Stroke objects, foliage, Scatter, a stylized sky and fog) | 0083-0084 |
| 269-270 | 0249-0250 | The manual, the Learn lessons, the features tracker; this write-up | - |

## What held up
- **One brush architecture carried five rounds.** The sculpt dab (0236) and the paint dab (0245) share the hit, the
  disc, the falloff, the symmetry instances and the undo model; the painterly tips, wet mixing and jitter (0247), the 2D
  canvas (0246), the Strokes and Scatter tools (0248) all hang off it. Each later round was a weight function or a
  colour rule, not a new tool.
- **In-place undo deltas.** The sculpt delta (0236) and the paint tile step (0245) made strokes cheap to undo: a stroke
  on a 1k surface stores 639 KB against a 13.6 MB snapshot; a 4k image would have ended the budget in thirty strokes.
- **The rect engine** (0244): a layer edit redoes only the layers above it inside the edited rectangle grown by the
  filters' halo, bit for bit the full evaluation - the stack stayed interactive as it grew to groups, masks and smart
  materials.
- **Workers everywhere a frame would stall:** QuadriFlow, the mesh maps, the surface's evaluation, the baked light,
  with the result landing only when the mesh or surface is still the one it started from.
- **Honest numbers in the tests** (printed, never asserted as timings): the 6k sphere to 2,000 quads in about 2.5 s;
  the 24k sphere's DynaMesh volume 0.1 % off; a 40 px dab painting 7,119 texels and none of 372,096 far-side texels;
  Worn Steel's edge band 0.706 against the faces' 0.565; a scatter dab of density 4 placing 13 instances (4 pi wanted).

## What the reviews caught (and the pass kept)
Every round had an Opus review before landing; the blockers and majors were taken the same round. The patterns that
came back: a frame-per-step UI loop where a stroke was meant (the 2D canvas); timing-dependent test checks (the worker
landing); a direction read in the wrong space (screen against texel: Smudge); files overwritten under undo's feet (the
baked light); undefined behaviour on NaN input (stencil offsets); documentation duties left for the landing script.
The fixes are listed in each spec's *Review* section and each CHANGELOG entry's *Review (Opus) taken* bullet.

## What is still open
- Sculpt: Dyntopo with Dynamic Subdivision (refused together); QuadriFlow's own guides and symmetry (the spike's list).
- Paint: the Hand-painted rim on the card and in the path tracer (unlit there); the pen's tilt turning the Flat tip;
  a Paint Strokes re-render; export of a surface evaluated on the main thread (the command's cost); the 2D brush radius
  in texels.
- Stylize: a canopy fitted to a sketch outline; sway through spring bones; scatter across several objects in one dab;
  the GPU sky and fog pixels compared against the CPU's (the gpu leg compiles the shaders).
- Process: the sanitizer leg's WSL instability in the last rounds (rounds 266, 268 and 269 shipped without it; the
  Windows and Linux legs passed, and round 267's sanitizer run covered the shared code).

## Where to read more
`docs/MANUAL.md` chapters 1, 3, 4 and 6; the Learn tab's five new lessons; `research/FEATURES.md`; the ADRs 0068-0084;
each round's spec under `docs/tasks/0230-0249` with its *Not done* and *Review* sections; `docs/PERFORMANCE.md`'s
sections for each round.
