# 0035: Transparent backgrounds in renders (PNG alpha)

- **Status:** done (as built below)
- **Requirements:** R-03, R-06
- **Decisions:** none expected (Blender's Film > Transparent; an image's alpha already exists in `Image`)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Blender's Render Properties > Film > Transparent: the world still lights the scene but isn't drawn behind it,
so a saved PNG (or EXR) has alpha 0 where only the sky was, partial alpha at anti-aliased edges and behind
see-through surfaces, and 1 on solid objects.

## Scope
- In:
  - **`RenderSettings::transparent`** (Render window, Render Properties, `set Render.Transparent true`; saved).
  - **Rasterized F12:** alpha from coverage after the SSAA downsample (sky samples count 0); transparent and
    glass surfaces their alpha.
  - **Path traced F12 (CPU and GPU):** alpha = the share of camera samples that hit something (a glass
    surface counts as Cycles' Transparent Glass option off: opaque), averaged like the colour; the denoiser runs
    on the colour, the alpha kept.
  - **Camera filters:** passes run on the colour; alpha follows (Pixelate / scaling with it; outlines and
    effects drawn where alpha is 0 make it 1 there).
  - **Display and saving:** the Render window draws a transparent render over a checkerboard; PNG saves RGBA,
    EXR saves its alpha channel; JPEG and HDR (no alpha) are composited over the world colour with a note.
  - Camera sequences and `saverender` the same.
- Out: Cycles' "Transparent Glass" (refraction showing the background as alpha).

## Acceptance criteria
1. A cube on no floor, rasterized and path traced: corners of the PNG alpha 0, the cube's middle 255,
   its edge pixels in between.
2. A 50 % transparent quad over nothing gives alpha about 128.
3. Off: identical files to before.
4. The saved PNG opens in Blender with the alpha (checked: Image Editor shows the checkerboard).

## Tests and checks to run
- Unit (round 57), full suite, verify mode, Linux, sanitizer; open a saved file in the installed Blender.

## Documentation to update
- CHANGELOG, README (rendering paragraph), USABILITY row.

## As built
Built as specified: `RenderSettings::transparent` (Render window, `set Render.Transparent`), the path tracers'
camera rays that miss adding nothing (the Solid Color mechanism, black) and their coverage summed per pixel
(`PathTracer::coverage`; the GPU's auxiliary buffer gained a third region), the rasterizer's two passes, RGBA PNG
and EXR, the Render window's checkerboard. The saved PNG opens in Blender 5.2 as RGBA, straight alpha, 0 at the
corner and 1 on the subject.

**Deviations:**
1. **The rasterizer's alpha comes from two renders** (over black, then white; alpha = 1 - the difference, colour =
   the black render / alpha): exact for the forward pass's blending of see-through surfaces, at twice the cost, and
   only when Transparent is on.
2. **A filtered path-traced render runs its passes twice**, on the picture over black and over white (straight
   colour by the coverage, composited in display values), and takes alpha from the difference as the rasterizer
   does, so outlines drawn beside an object and pixels a pass moves carry their alpha. An unfiltered one is
   un-premultiplied before the view transform, in linear light, dividing by at least 5 % (a silhouette's few
   samples would otherwise amplify the denoiser's bleed).
3. **A JPEG is composited over black**, not the world's colour (simpler, and what a straight-alpha PNG over black
   shows); Radiance HDR keeps the premultiplied colour without alpha; the EXR alpha is written for unfiltered
   path-traced renders (the only ones with float pixels).
4. **Glass counts as covered** (Cycles' Transparent Glass off), as the spec said: only the camera ray itself sees the
   empty background; rays out of glass see the world.
5. **Under 4 % alpha from the black / white difference is clear:** passes that quantise colours (15-bit, posterize)
   or add grain leave the background a few levels apart in the two pictures, not a real veil.

**Review:** the code reviewer found three major issues and several minor ones, all fixed with tests: glass vanished in
transparent path-traced renders (alpha 0, black inside: the rays out of it saw the empty background and cleared its
coverage); a Bloom filter on a rasterized transparent render bloomed the white pass's sky into objects (a see-through
rim); a filtered path-traced render's alpha ignored what the passes drew or moved (outlines erased, pixelation and
distortion fringes); colour-quantising passes left a faint veil on the sky; the GPU accumulated coverage on every
render (now only for a transparent film); low coverage amplified the denoised colour; no note when JPEG or HDR
drop the alpha.

