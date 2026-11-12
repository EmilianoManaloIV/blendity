## 2026-11-11 (round 487): Generate Lighting's gather on every ticked graphics card, in batches that keep a card working

**Asked:** the owner, after baking SideRail on a machine with two cards: "when baking lights it uses all the GPU's
available. I only saw the 4070ti working." Of the options put to them - the gather on every ticked card, the bake's
CPU half moved to the card, or only saying which card bakes - they chose "Yes, both cards". (Round 487 was first light
cookies: SideRail has none. Then two faults of their bake, which they dropped after the diagnosis: "2 and 3 can be
ignored".)

**Found while measuring:** the first build of this shared each batch between the two cards and SideRail's bake came
out about 3 s *slower* on both cards than on one (41.1 / 40.9 / 39.7 s against 37.7 / 39.1 / 36.6 s). The cards'
own clocks said why: a batch was 8192 texels, and a card's time for so small a batch is mostly its wait for the
longest paths of each dispatch, not its work - the 4070 SUPER took almost as long for three quarters of a batch as for
a whole one. That was true of every bake on one card too.

**Changed (task 0463)**
- **A bake's batches are as large as keep the cards working**: Generate Lighting asks the cards how many texels they
  take next - what each returned in about three quarters of a second, from about 8 million paths up, at most 131,072
  texels a card. **SideRail's lightmaps bake in 17.7 s on one card where they took 37.7 s** (the gather itself: 28.4 s
  to 10.2 s by the card's clock), with the same samples.
- **The gather runs on every graphics card ticked in Preferences**, as an F12 render does; until now only the first
  ticked card gathered. Each call is shared between the cards by the speed each showed; the cards are made ready
  together (the scene is built once for every card it fits, and sent to all of them at once). On the owner's two cards
  (an RTX 4070 SUPER and an RTX 3060 Ti) SideRail's lightmaps bake in 16.2 s on both, 17.7 s on the first alone.
- **A texel's random directions are its place in the bake**, not its place in a batch and not the card that gathers
  it. So the lightmap does not depend on the batches or on the split: SideRail's saved lightmap pages are byte for
  byte the same baked on both cards and on one, and a gather split between the two cards equals the first card's own
  at all 20,000 points of the test, bit for bit.
- A card that is still opening, cannot hold the scene, cannot be sent it or fails leaves its share to the others; a
  card that cannot hold the scene, or holds it with smaller textures, says so in the Console. With no card left the
  CPU gathers, as before.
- **The bake says which cards gathered**: a Console line with each card's share ("the gather ran on NVIDIA GeForce
  RTX 4070 SUPER 71 %, NVIDIA GeForce RTX 3060 Ti 29 %") and one with where the gather's time went (the batches, each
  card's time by the wall and by its own clock), and the shares beside the bake's time in the Lighting window when
  more than one card gathered. A bake on one card reads as it always did.
- Preview Lighting keeps its small batches (it steps on the editor's thread) and one card; the light probes keep to
  one card.

**Added**
- `lighting cards all|one` (console; a measuring setting like `lighting pick`: the next Generate Lighting, not saved,
  not in the scene's key): `one` keeps the gather to the first ticked card.
- Learn, "Rendering on the GPU": the section "Baking light on every card", two questions, two Try-it lines.

**Measured (SideRail in a scratch copy, 820,031 texels, 1024 gather samples; the build with Blender's libraries; a
quiet machine, three runs each after a warm-up that is not counted; the lightmaps' seconds)**
- Before this round (one card, batches of 8192 texels): 37.7 / 39.1 / 36.6 s.
- Now, one card: 17.7 / 17.5 / 17.8 s. Now, both cards: 16.2 / 16.2 / 16.5 s, the shares 71 % and 29 %.
- The pages of the six runs after are one file, byte for byte.

**Said plainly**
- **The second card adds little on this pair**: 1.4 s of 17.7. The 3060 Ti gathers at well under half the 4070 SUPER's
  speed here, the gather is now about 10 s of a 17 s bake, and making a second card ready costs about half a second.
  Two like cards, or a heavier world, gain more. The larger part of this round's gain is the batches, and a machine
  with one card has it too.
- **A bake of more texels than one old batch (8192 at 1024 samples) has other noise than before this round** - the
  same samples a texel, other random directions, since a texel's sequence is now its place in the bake. A saved bake
  is not touched; a new bake of the same scene differs from the old one as two seeds do. Smaller bakes, and every
  `gpu_gather` call that is not a bake's (the light probes'), are bit for bit what they were.
- A step of a bake is now up to about a second on the cards and the direct light of up to 131,072 texels a card after
  it: on two cards Cancel and the progress line answer within about two seconds, where they answered in a quarter of
  one.
- "Does not depend on the batches" holds for bakes of up to 8,388,608 texels (a texel's place wraps there, and a batch
  that crosses the wrap does not wrap with it): a larger bake's noise can differ from run to run with the cards'
  speeds. SideRail has 820,031.
- When a card fails mid-bake the Console's "takes its share" line is written before the other card has taken it, and
  the failed attempt's time is counted to the card that took over (the time line only).
- A submission of more samples than fit one pass is cut at a multiple of 8 samples; that changes a one-card gather's
  last bits only above 16,384 gather samples, which no bake setting reaches.
- Moving the CPU half (the lamps' direct light, the emitters) onto the card is not built.
- Four checks of three Scene-view tests fail in the gpu group of the build with Blender's libraries on this machine
  (`tests/test_gpu.cpp:888`, `:2227`, `:4887` - the card's picture against the CPU's, 1.025 where 1.0 is allowed):
  they fail the same way, with the same numbers, on `main` and in rounds 485 and 486's runs; not this round's.

**Checked:** the round's groups and `editor` (ADR 0134; the whole suite is CI's) - the build with Blender's libraries:
gpu 784 checks with the 4 of `main` failing and no other, lighting 9805, editor 2252; dependency-free on Windows: gpu
112, lighting 9749, editor 2242; on Linux: gpu 113, lighting 9751, editor 2240; stress `lightmap_quality` ran to its end with no problem row (the dependency-free build:
the CPU's bake, which this round does not change); `test_coverage_map.py --check` and `changelog.py check` pass. The two-card
tests run where a machine has two cards (this one); CI has none and skips them.
Review: three passes of the code-reviewer - the first on the split (all findings applied), the second on the batches
(two majors: Preview Lighting would have had the large batches on the editor's thread, and Cancel would have waited
for a whole large batch's direct light - both fixed, with five minors), the third approved the fixes (its minor is the
8,388,608 line above). The tests are the main session's with the reviewer's gaps added; no separate test-engineer
pass ran (the card tests need the machine's one card window).

<!-- README -->
after: a bake's filter reads each texel's noise by the samples that carry its light
Generate Lighting's gather runs on every ticked graphics card, as F12 does, in batches sized by the cards' speed, and says which cards gathered (round 487: SideRail's lightmaps 37.7 s to 16.2 s);

<!-- docs/USABILITY.md -->
after: | A baked world had "splotches"
| A light bake used one graphics card however many were ticked, and kept even that one waiting: it sent the card 8192 texels at a time. (round 487) | Generate Lighting's gather runs on every ticked card, in batches of what the cards return in about three quarters of a second, with the same lightmap whichever card gathers a texel and however the bake is cut; the Console and the Lighting window name the cards and their shares. SideRail's lightmaps: 37.7 s to 17.7 s on one card, 16.2 s on two. |

<!-- docs/ARCHITECTURE.md -->
after: | `gather_variance_read`, `unscale_gather_variance`
| `PathTracer::gpu_gather_on(devices, ..., first_index)`, `gpu_gather_batch`, `gpu_gather_shares`, `gpu_gather_cards`, `gpu_gather_times` (`render/pathtracer.*`); `BakeSettings::gpu_devices`, `gpu_batch_by_speed`, `Lightmapper::gpu_cards` (`render/lightmapper.*`); `Editor::choose_bake_gpus`, `lighting_cards_command` (`editor/lighting.cpp`) | Round 487: the bake's gather on several cards at once - one `BakeCard` (renderer, scene build, measured rate, batch) a device; `bake_cards_ready` builds the scene once and sends it to the cards together; contiguous ranges of each call shared by rate in whole 256s, one thread a card; a failed card's range is retried on the others. A point's random sequence is `first_index` plus its place in the call (a bake passes the texel's index), so neither the split nor the batch size changes a result; `gpu_gather_batch` is the texels the cards take next (what each returned in about 750 ms, at most 131,072 a card). `gpu_gather(device, ...)` is the list of one. Preview Lighting keeps batches of about 8M paths (`gpu_batch_by_speed` false). |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 24: a bake on every ticked card, in batches that keep a card working (round 487)

Generate Lighting's gather on the owner's two cards, an RTX 4070 SUPER (12 GB) and an RTX 3060 Ti (8 GB). SideRail in
a scratch copy: 820,031 texels, 1024 gather samples, the build with Blender's libraries, a quiet machine, three runs
each after a warm-up that is not counted.

| SideRail's lightmaps | Run 1 | Run 2 | Run 3 | The gather (wall) | The cards' own clocks |
|---|---|---|---|---|---|
| Before: one card, batches of 8192 texels | 37.7 s | 39.1 s | 36.6 s | 30.2 s in 101 batches | 4070 SUPER 28.4 s |
| First build: both cards, batches of 8192 texels shared | 41.1 s | 40.9 s | 39.7 s | 32.3 s in 101 batches | 4070 SUPER 25.4 s (76 %), 3060 Ti 24.7 s (24 %) |
| Now: one card, batches by its speed | 17.7 s | 17.5 s | 17.8 s | 11.7 s in 15 batches | 4070 SUPER 10.2 s |
| Now: both cards, batches by their speeds | 16.2 s | 16.2 s | 16.5 s | 10.2 s in 12 batches | 4070 SUPER 7.4 s (71 %), 3060 Ti 7.7 s (29 %) |

(The gather columns of the first two rows are from single runs made while finding the cause, not from the timed
three.)

What the second row showed: with 8192 texels a batch a card's time hardly depends on how many of them it is given
(three quarters of the batches' texels took the 4070 SUPER 25.4 s, all of them 28.4 s) - it waits for the longest
paths of each small dispatch. Batches of fixed sizes, one card, the same bake, single runs: 8192 texels 28.4 s on the
card's clock, 32,768 10.4 s, 65,536 9.2 s, 131,072 8.3 s, 262,144 8.0 s, the whole bake in one 7.7 s. So a card's
batch grows from about 8 million paths by what it returned in the last call, to at most 131,072 texels (a bake's
step, and so Cancel, stays under about two seconds).

Making the cards ready (opening them, building the scene, sending it) is 1.4 s for one card and 1.9 s for two, the
scene built once and sent to both at once.

The lightmap pages of every run of the last two rows are byte for byte the same file: a texel's random sequence is its
place in the bake, whichever card gathers it and however the bake is cut into batches. They are not the pages of the
first two rows (there a texel's sequence was its place in its batch): the same samples, other directions.
