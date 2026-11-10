## 2026-11-10 (round 484): SideRail's mirror finish and splotches, reproduced and measured

**Asked:** "Baked lighting looks really weird here, its like it has a mirror finish and there is splotches still." (the
owner's imported VRChat world SideRail, against its reference). Pass 24's first round: name each cause with evidence
before anything is fixed. **This round changes no behaviour** - one log line apart (below).

**Found (task 0460; the tables are in `docs/tasks/0460-siderail-look-measured.md`, "Results")**
- **The mirror finish in the owner's screenshots is materials read without their textures.** A Unity `.mat` read before
  its package's GUID table exists gets Metallic 1, Roughness 0 and no map; the table is written last while a package
  unpacks, the Materials window loads every `.mat` it sees meanwhile, and the asset library keeps what it read until
  Blendity restarts. Reproduced on main in the real window: `ConcretePanel` without its maps in 6 of 7 runs, 17 to 37 of
  the scene's 38 Unity materials without any (10 is right). At the owner's two stations the picture goes from 3.75 /
  0.898 and 3.67 / 0.902 (MAE / SSIM against Unity) to 13.71 / 0.702 and 12.50 / 0.697. **Until round 485: restart
  Blendity after importing a package** (or keep the Materials window closed while it unpacks).
- **Deleting a package's folder and importing it again in one session** leaves a Unity scene with none of its Unity
  materials (the models' own instead), until a restart.
- **Smoothness is dropped** where Unity keeps it in the metallic map's alpha: 10 of the 38 Unity materials the scene
  wears (181 renderer slots) come in at roughness 0 where Unity shades roughness 0.24 to 0.76. Set right by hand, F12's
  picture moves little (3.74 / 0.905 and 3.58 / 0.912).
- **16-bit PNGs are read brighter by the build with Blender's libraries** (libpng's simplified interface takes them as
  linear light: one half reads 186 / 255, not 128): 14 of SideRail's textures, 7 of them on materials - the plank
  floor's colour, mask and normal map among them. The dependency-free build reads them as Unity does.
- **The lights' intensity:** a project made from a package has no Unity `ProjectSettings`, so its lights are taken in
  gamma, silently; SideRail's own project is linear. With Blendity's own bake the owner's two stations then read 17.76
  / 0.734 and 16.40 / 0.761 against Unity where the linear bake reads 6.64 / 0.863 and 5.97 / 0.885, and are 2.9 and
  2.6 times as bright - the largest single fault there. Whether such a project should take its lights linear is the
  owner's decision.
- **The splotches are mostly not the lights kept by chance** (the plan's cause, and Pass 21's note): with every light
  kept at every texel SideRail's bake has the same grain and blotch (0.384 / 0.251 against 0.382 / 0.251). They are
  **specks** - small round spots in one seed's bake and not the other's, about 140 a view at the owner's stations (800
  unfiltered, 330 in the owner's own session with no maps and the lights in gamma). About 100 are in the gathered
  bounce and about 20 are lights kept by chance; the emitters and the texels filled from their neighbours are not it.
  **More samples make them worse** (271 and 210 at 2048 samples, 59 and 78 at 256): the lead for round 486 is that the
  bake scales the noise it measures for Sobol samples (to an eighth at 1024), so its firefly clamp and its filter take
  a texel's few rare bright samples for steady light.

**Added (measuring only)**
- **`unity materials [all] [to "<file>"]`**: what Blendity made of every material the scene wears, as text;
  **`unity mapstats [decoders] "<image>"...`**: an image's channel means as it is decoded, and with `decoders` as
  Blendity's own decoder reads it (a script's verb: refused in a window).
- **`lighting pick on|off|<rays>`**: how many Baked point and spot lights a texel keeps by chance in the next Generate
  Lighting (8: the bake as before; `off`: every light); a bake that picks says how many lights a texel kept and
  dropped - the one line a default bake's log gains.
- **`tools/unity_materials.py`**: every Unity `.mat` of a project beside what Blendity made of it - Unity's metallic
  and roughness by the shader's own rule, Blendity's, a fault class per material, the images two decoders read
  differently; `state` puts a scratch project into a test state (no maps; Unity's values by hand; 16-bit PNGs as
  8-bit).
- **`tests/fixtures/unity_gloss/`**: twelve small Unity materials of our own, one per fault, with Unity's values worked
  out by hand; the folder is checked out byte for byte (`.gitattributes`).
- **`tools/unity_compare.py`**: `--bake-cmd`, `--lights-as-imported`, `--reuse-unity-lightmaps`; `noise`, a bake's
  noise at seeds 0 and 1 without Unity; and at each station the two bakes' own grain, blotch and **specks** (spots
  where the two pictures differ by over half the picture's mean light).
- **Stress `lightmap_quality`: `lamp_hall`** - a hall lit by 72 Baked lights whose reference keeps every light, with the
  bake at `pick` off / 8 / 16 / 32: there the picking is the noise (4.8 times the blotch of keeping every light), which
  on SideRail it is not. The four older scenes' rows are unchanged to the last digit.
- **`decode_image_own`** (`image.h`): Blendity's own decoders whatever the process's codec switch says.
- **Learn:** "A Unity material beside Unity's own values" (a Try-it, two questions); "Many lamps" in "A bake's noise,
  measured" (two questions, two Try-its).

**Kept exactly:** a bake at the defaults saves the same pages, byte for byte, from main's build and from this round's
(a room with twelve Baked lights; four files). It did not at first: with the rays a run-time number MSVC ordered one
expression another way and two bytes of one page differed, so the default path keeps the constant and the measuring
path is a function of its own, never inlined.

**Not done (by design):** nothing is fixed; rounds 485 and 486 are specified from these tables, and 486 is cut again
(the plan's stratified light picking would not touch most of SideRail's specks). Not taken as timings: the bake at
`pick off` and 16 (another worker's build ran beside them) and the stress section's times. Not run: the reproduction
with the Project window in place of the Materials window. The lights' intensity in a project made from a package is put
to the owner, not changed.

**Checked:** both Windows suites - dependency-free 23,199 checks, 0 failed; with Blender's libraries 24,114 (the four worktree `gpu` picture comparisons, as rounds 463-466, and in one of two runs
the slow-open test while another worker used the card);
Linux (WSL) 23,205 checks with one failure while the machine was baking - "Live Link never pauses the editor", round
382's tick-time bound - which passes alone; the audits; both tools' self-tests; the code review (changes needed, all
applied: the committed materials checked out byte for byte, a decoder entry that leaves the process's switch alone, the
tools refusing a project that is not a scratch copy); stress `lightmap_quality` - `lamp_hall` 0.043 / 0.0327 / 0.0096
(relative RMSE / grain / blotch), 4.8 times the blotch of every light kept, the four older scenes as Pass 21 left them;
SideRail in scratch copies - the tables of the task's results; the owner's folders untouched.
