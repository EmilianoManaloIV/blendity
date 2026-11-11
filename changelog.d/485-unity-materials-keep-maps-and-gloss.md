## 2026-11-10 (round 485): Unity materials keep their maps and their gloss

**Asked:** "Baked lighting looks really weird here, its like it has a mirror finish and there is splotches still." -
Pass 24's second round, cut from round 484's tables: the mirror finish. The owner approved the pass's specs ("Continue
and finish all tasks, I approve all specs from all passes.").

**Fixed (task 0461, ADR 0131)**
- **A world no longer comes in as mirrors after importing its package.** A Unity material read while its package was
  still unpacking found none of its textures (the package's GUID table is written last) and kept Metallic 1, Roughness
  0 and no maps until Blendity restarted - the Materials window, open, read every `.mat` it saw. Now the list of the
  project's materials leaves out a folder that is still unpacking, and where the import ends every Unity material
  already read under it is read again, in place: the objects wearing it, the Inspector and the Materials window follow
  with nothing to redo. In the real window, with the Materials window open as the owner has it, five imports of
  SideRail's package: no material read early, all 38 linked with their maps (round 484: maps lost in 6 of 7 runs).
- **Deleting an imported folder and importing the package again** in one session brings its materials back (the
  folder's "deleted this session" marks are cleared where the import ends). Before, none loaded until a restart.
- **The shader's keyword decides, as in Unity** (Standard, Filamented, URP Lit): a texture left in the Metallic slot
  without `_METALLICGLOSSMAP` (URP: `_METALLICSPECGLOSSMAP`) is ignored and the sliders count; a slot a keyword names
  and nothing fills is Unity's white texture.
- **A smoothness kept in a map comes in**, as one number a material: Roughness = 1 - the mean of the alpha Unity's
  shader samples (the metallic map's, or the albedo's under `_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A`) x its scale. A worn
  wall is as rough as Unity shades it on average; it is not yet glossier in its polished patches than in its dust
  (ADR 0131: a channel on the texture slot is the later step). On SideRail, of the 38 Unity materials the scene wears,
  "smoothness dropped" goes from 10 to 0, and the two views the owner looks from read 3.71 / 0.905 and 3.57 / 0.912
  against Unity with Unity's bake (3.75 / 0.898 and 3.67 / 0.902 before) - round 484's by-hand mark.
- **16-bit PNGs read as stored in the build with Blender's libraries** (libpng is asked for the file's own samples):
  one half reads 128 / 255 in both builds, not 186.
- **A Unity `.mat` is never written over** (found by the round's review, older than the round): a scene opened while
  its package's folder was missing kept its own copies of the materials, and once the folder was back the next edit
  wrote Blendity's text over each Unity file. Such a copy is now read from the Unity file where an import ends (or on
  Reimport Unity Materials), and is not written while a Unity file is at its path.

**Bake again.** Lightmaps baked while the materials had no maps, or with the brighter 16-bit textures, were lit by
those materials, and a bake is not marked out of date by a material's maps: Generate Lighting once more after this
round.

**Added**
- **`.unity-stats`** beside a package folder's `.unity-guids`: a line for each texture a material reads its gloss
  from - GUID, the file's bytes, width, height, mean R G B A. The unpack's worker writes it; for a package unpacked by
  an older Blendity the maps are measured by one background job when its scene opens (the job tray: "Reading Unity
  materials' gloss"; at once in a headless run) and the materials that asked follow - no others. No image is decoded
  on the main thread for a material's values. On SideRail the unpack's worker took 2.3 to 3.4 s for its 14 maps and
  the job 2.3 to 2.6 s for 13 (both with two other passes building beside them; the decode alone is 1.6 s), and the
  frame that takes the job's result cost 30 to 40 ms, once. Opening the scene takes what it took: 2.23 s against
  2.23 s for round 484's build over the undisturbed turns of eight.
- **Assets > Reimport Unity Materials** (`unity rematerials`): every Unity material read again from its file, at once;
  it says how many changed. Reading again is not an edit - no undo step, the scene not marked changed, and the copies
  the undo history and Play's Stop keep take the new reading too, so neither brings the early one back. A value typed
  on a Unity material in the session is replaced by the file's, as at a restart; the Materials window's tooltip on a
  Unity material says so.
- **Learn:** "A Unity material's gloss, and reading materials again" (a Try-it, two questions).

**Changed**
- Round 484's tests that pinned the faults now pin Unity's numbers, each saying what it pinned before; the committed
  set gains its `.unity-stats`, and `blendity_today.txt` / `expected.json` hold round 485's reading.
  `tools/unity_materials.py` counts a texture left in the slot as a fault only when Blendity went by it, checks the
  committed table against its own reading of the PNGs, and keeps round 484's reading as a case of its self-test.
- `unity materials all` in a headless run measures the gloss maps of the materials it has just loaded before it lists
  them.
- A scene that wears a material whose Metallic slot held a texture without its keyword asks for a new bake once: its
  lighting was baked with that surface as metal.

**Not done (by design):** smoothness per texel; reflections off, highlights off and occlusion maps (Blendity's material
has no property for them); the lamps', glass's and mirrors' own shaders; URP Lit's and Standard's specular setups; a
scene saved while its materials were the models' own (opening the `.unity` again is the repair); the lights' intensity
in a project made from a package (the owner's decision, as round 484 put it). Not reached by a test: Cancel in the job
tray itself (the landing of a cancelled job is tested).

**Checked:** both Windows suites - dependency-free 23,541 checks, 0 failed; with Blender's libraries 24,455 with the
four worktree `gpu` picture comparisons failing as in rounds 463 to 484 (the same four, not this round's); Linux (WSL,
GCC) 23,547 checks, 0 failed; the audits; both tools' self-tests; the test-engineer's ten independent tests (two
packages sharing a GUID, a package imported over itself, hostile tables, unreadable maps, 16-bit PNGs of every colour
type, the import settings' Alpha Source; no product fault found); the code review (changes needed, all applied: a
landing read every Unity material again, Stop undid a landing made in Play, a stand-in copy could be written over a
Unity `.mat`, a landing could write the table beside an unpack; and one fault the round's own tests found after it -
a mean held more digits in the session that measured it than in the table). SideRail in scratch copies: the five
windowed imports, delete-and-import-again (38 linked), a project unpacked by round 484's build (the job, 38 linked),
the materials table (smoothness dropped 12 -> 0, slot without keyword 1 -> 0, decoders differ 4 -> 0 of 57), the six
stations with Unity's bake (the owner's two at round 484's by-hand mark, the other four unchanged). The owner's four
folders have no file changed; the tests' own import cache left four small folders in `~/.blendity/cache/import`, as
earlier rounds' did (a follow-up). Not done on a quiet machine: every timing above.

<!-- README -->
after: with Unity's materials converted by shader
the materials keeping their maps whatever is open while a package unpacks, the shader's keyword deciding which sliders and maps count, and a smoothness kept in a map read as its mean (round 485),
