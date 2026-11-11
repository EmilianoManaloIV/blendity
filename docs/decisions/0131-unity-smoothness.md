# ADR 0131: A Unity material's smoothness - one number a material now, a texel later

- **Status:** accepted with Pass 24's specs (the owner's words of 2026-11-10, relayed by the hub session: "I approve
  all specs from all passes") and built in round 485 (task 0461); the owner reviews it with the round's PR
- **Builds on:** round 371 (a Unity `.mat` loads converted), task 0143 (`derive_channel`: a packed map split into files
  for a model's import), round 447 (textures stream in after an import), ADR 0105 (responsiveness), round 484's
  measurements (docs/tasks/0460, "Results")
- **Requirements:** R-04 (materials), Q-06 (responsiveness)

## Context
Unity's Standard shader, Silent's Filamented and URP Lit keep a material's gloss in a texture: under the
`_METALLICGLOSSMAP` keyword metal is the map's red and smoothness its alpha times `_GlossMapScale`; under
`_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A` smoothness is the albedo's alpha. Blendity's material has one number and one
map for roughness (`roughness` x the roughness map's red) and a texture slot is a path and a colour-space flag
(`TextureRef`). Since round 371 a Unity material with a metallic-gloss map comes in at Metallic 1 x the map and
Roughness `1 - _GlossMapScale` - 0 for nearly every material - with the comment "the alpha's smoothness isn't carried".

Round 484 measured it on the owner's world (57 materials, 38 worn): 12 materials have their smoothness in a map (10
worn, on 181 renderer slots); Unity shades them at a mean roughness of 0.24 to 0.76, Blendity at 0. The maps are 2048
px (12) and 4096 px (2). With each of the ten set to Unity's mean values by hand, F12's picture at the owner's two
stations moves from 3.75 / 0.898 and 3.67 / 0.902 (MAE / SSIM against Unity) to 3.74 / 0.905 and 3.58 / 0.912: right
in direction, small in size - the picture's large faults were elsewhere (materials read without their maps: 13.71 /
0.702). Decoding the 14 mask maps takes 1.7 s with Blender's libraries and 4.1 s without, on one thread; the world's
textures hold 1.32 GB in the cache.

## Options
| | what | memory on this world | what changes | what it gives |
|---|---|---|---|---|
| A | **One number a material**: roughness = 1 - (the map's mean alpha) x scale, the mean kept in a table beside the package's GUID table | none | the Unity material reader; a table written off the main thread | every material right on average; no variation across a surface |
| B | **A derived roughness map** a mask map (`derive_channel`, as a model's import does today): a grey PNG in the import cache, the Roughness Map slot pointing at it | 21 MB a 2048 px map, 85 MB a 4096 px one with their mip levels: about 0.42 GB for the 14, on 1.32 GB | the reader; files written (seconds a package, off the main thread); 14 more textures to stream at open | right per texel in every renderer and exporter, as they are |
| C | **A channel on the texture slot** (`TextureRef`: which channel, inverted, scaled) | none (the mask is in the cache for its metal already) | every place that samples roughness: the CPU shading, the views on the card, the path tracer and its kernels, the bake's frozen materials, the exporters (which must write a roughness texture), the material file, the Inspector | right per texel, and the derived files of B and of today's model imports could go |

## Decision
1. **Round 485 builds A.** It removes the fault round 484 named ("smoothness dropped": 10 worn materials) for every
   Unity import at no cost in memory or in any renderer, and what it leaves - no variation within a material - is not
   what the measured pictures lack.
2. **The mean lives in `.unity-stats`** beside `.unity-guids`: one line a texture a material reads its gloss from -
   GUID, the file's bytes when it was measured, width, height, mean R G B A. Written by the unpack's worker, before the
   GUID table; for a package unpacked earlier, by one background job when a material first asks for a line that is
   not there. A material reads the table through the GUID index; a missing line, or one for a file of another size,
   leaves the alpha at 1 - what Unity shows for a texture it cannot find, and for most materials the value they had
   before - until the job lands and the material is read again. A texture of no package folder (a Unity project's
   Assets copied in) is measured the same way and its mean kept for the session only: Blendity writes its own files
   into the folders it made, not beside a project's assets. A mean is held at the table's six decimals wherever it is
   used, so the session that measured a map and the next one read the same number.
3. **Reading a material again is not an edit.** The asset is re-read in place and the copies the undo and redo
   states keep (and the scene Stop brings back) take the new reading with it; an edit made by hand to a Unity material
   in the session goes, as it does at a restart (a Unity material's file is never written back). **Only what the
   occasion names is read again**: the folder of the package an import just ended, the materials whose maps a job
   measured, or every Unity material when the owner asks (`unity rematerials`). A job that happens to measure one
   package's maps never reads another material again (the round's review: it did, and took typed values with it).
4. **Per texel is the next step, and C is its shape** - not B: B's 0.4 GB on this world is a third more texture memory
   for a difference no station shows yet, and C also serves glTF's and Unreal's packed maps. It is a change through
   every renderer and exporter, so it gets its own task and its own measurement (a close view of worn metal against
   Unity, and the path tracer's picture), when the owner asks for it or a comparison shows the need.
5. **The keyword decides which rule applies**, as in Unity: a texture in the slot without its keyword is ignored, and
   a slot a keyword names but nothing fills is Unity's white texture (all metal, alpha 1).

## Consequences
- A Unity material's roughness in Blendity is no longer 0 when Unity's is not; `tools/unity_materials.py`'s table reads
  `smoothness dropped` 0 on the measured world.
- One more hidden file a package (`.unity-stats`, a few kilobytes); the unpack takes the maps' decode longer on its
  worker (1.7 s on the measured package with Blender's libraries).
- A material whose gloss varies strongly across its map (worn metal, wet patches) reads flat until step 4.
- Round 484's tests that pin roughness 0 flip to Unity's numbers; the committed set gains its table.
