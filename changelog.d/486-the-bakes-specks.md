## 2026-11-10 (round 486): the bake's specks

**Asked:** "Baked lighting looks really weird here, its like it has a mirror finish and there is splotches still." -
Pass 24's third round: the splotches. Round 484 measured them as specks (small round spots one bake has and the next
has not) and named a lead; the owner approved the pass's specs ("Continue and finish all tasks, I approve all specs
from all passes.").

**Fixed (task 0462, ADR 0122 amended)**
- **A bake leaves far fewer specks.** A texel lit by two or three of its thousand gather rays - the ones that met a
  small bright patch - is luck, and the bake's filter should spread it into its neighbours. It did not: since round 465
  the filter took a Sobol-sampled texel's measured noise for an eighth of itself (at 1024 samples), which is right for
  light that fills the hemisphere and wrong for light a few rays carry, so the lucky texel passed for steady light.
  The filter (and the firefly clamp) now read each texel's noise by how many of its samples carry its light - its
  light squared over its samples' variance: the samples' own noise up to 16 such samples, round 465's scale from the
  sample count on, a smooth curve between. On SideRail (lights linear, round 485's materials, two seeds) the specks at
  the owner's two views fall from **90 and 71 to 11 and 39**, the grain from 0.190 to 0.181, the blotch from 0.055 to
  0.053; the bake takes what it took (38.8 s against 38.9 s, like for like in one queue).
- What is left - 11 and 39 - is about what round 484 measured for the direct light alone: the lights a texel keeps
  by chance. That is the next round's (the picking spread evenly), not this filter's.

**Bake again.** A saved bake is not marked out of date by this change: Generate Lighting once more to get it.

**Changed**
- Preview Lighting's filter reads the noise the same way.
- The change is in Blendity's own filter. Where OIDN's lightmap filter runs instead (not today: it has no lightmap
  weights), the specks must be measured again before it becomes a bake's own.

**Added**
- `lighting variancescale auto|on|off` (console; as `lighting pick`: the next Generate Lighting, not saved, not in the
  scene's key): `on` is rounds 465 to 485's reading - round 485's SideRail bake byte for byte, both seeds - `off` the
  samples' own noise everywhere (it blurs detail in dim places: the tunnel's error 7 % up); `clamp`, `snr` and `step`
  are the round's measured candidates.
- `gather_variance_read` (`render/lightmapper.h`): the rule, on its own for tests.
- Stress `lightmap_quality`: a table of the old and the new reading on every scene.
- Learn, "A bake's noise, measured": what a speck is, why the filter let it through, two questions, two Try-it steps.

**Not built, and said in the task:** the largest-sample sums and batch means (the texel's own signal to noise does
it, with nothing new from the card); a lower gate for the firefly clamp (the clamp changes no texel under any
reading); the lights kept by chance spread evenly (a round of its own, `lamp_hall` its mark).

**Marks missed, and said in the task:** at 2048 samples the tunnel view counts 47 specks against 39 at 1024 - one
past "within a fifth" (grain and blotch fall: 0.146 / 0.047); against Unity with Blendity's own bake the dark station
`spawn_back` reads SSIM 0.798 against 0.804, 0.001 past the mark, its grid correlation up from 0.953 to 0.959 - the
other five stations are within it and the owner's two views a hair better (6.80 / 0.879 and 6.15 / 0.899 against 6.81
/ 0.877 and 6.15 / 0.898). On the CPU path, where the sky lights a texel, the rule leans towards the old reading (the
review's finding; a follow-up).

**Checked**
- Unit: 11 tests for the round (`tests_round486`, `tests_round486_independent`: two seeds' bakes of a small lamp only
  the gather finds differ by less than 0.6 of what they did, more samples make fewer specks, a steady glow keeps its
  peak, the rule on hostile numbers, few-sample and plain-random bakes bit for bit as before, the verb's words). Full
  suite: Windows dependency-free 24,430 checks, 0 failed; Windows with Blender's libraries 25,344 checks in 1,918
  tests, 4 failed - the four GPU picture comparisons that fail on this machine's card on `main` too; Linux 24,436, 0
  failed. `tools/test_coverage_map.py --check`: nothing untested.
- Stress `lightmap_quality` (relative RMSE against the reference, old reading -> new): room 0.0133 -> 0.0133 (1024
  samples: 0.0119 -> 0.0119), corridor 0.0039 -> 0.0039, tunnel 0.1033 -> 0.1061 (1024: 0.0581 -> 0.0569), courtyard
  0.0071 -> 0.0071, hall 0.0433 -> 0.0433; bias unchanged to 0.0001; the tunnel's grain 0.062 -> 0.052. No problem
  counts in this section.
- test-engineer: five tests, one finding (a few-sample bake one float step off the old one) - fixed, the test made
  bit for bit. code-reviewer: approved; its five minor findings and three nits fixed or written into the task and
  the ADR.

<!-- README -->
after: a Unity scene's lights take their intensity
a bake's filter reads each texel's noise by the samples that carry its light, so a texel lit by a few lucky rays is smoothed away and a bake leaves far fewer specks (round 486);
