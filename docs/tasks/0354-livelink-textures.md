# Task 0354: textures by content in Live Link (round 377)

## Asked
Phase B's second round (plan cozy-fluttering-corbato, "textures by content"):
- Painted, procedural and generated textures, which have no file to open, reach the engines as you work.
- Every map, not only Base and Normal.
- Unity's texture cache by path never reloaded a changed file.

## Built
- **A `texture` message** (docs/LIVELINK.md) carries a PNG of a texture's pixels. Each material map names its key
  (`maps.<slot>.texture`); one key is one path in one colour space.
- **Blendity** (`editor/livelink.cpp`):
  - **A cheap stamp per comparison:** the textures some live material names are stamped with their image, size, date
    and 1,024 sampled texels.
  - **A changed stamp** copies the texels (at most 2048 a side) for a texture worker. The worker makes the PNG's pixels
    (a float colour image made sRGB), hashes them, drops a broadcast whose pixels are the same as the last, encodes
    the PNG and sends it.
  - **New clients get every live texture.** An image that keeps changing goes at most every quarter second, its last
    state after. A painted surface goes when its stroke ends.
  - **Stats:** `textures` and `textures_same`.
- **The bridges load textures from the bytes and apply every map they have:**
  - **Blender:** writes the PNG to its temporary folder and loads or reloads the image; colour space from
    `non_color`. Adds the Emission Map.
  - **Godot:** `Image.load_png_from_buffer` into an ImageTexture, updated in place. Adds metallic, roughness and
    emission maps.
  - **Unity:** `Texture2D.LoadImage` into a texture per key, so there's no stale copy cached by path. Adds the Metallic
    and Emission maps.
  - **Unreal:**
    - ImageWrapper decodes the PNG into a transient texture.
    - The materials' base is now made in code, a Color times a BaseMap parameter (the engine's BasicShapeMaterial has
      no texture), and textured materials take the texture.
  - **All of them** build a material again when its texture arrives.
- **Round 376's follow-up:** Godot's and Unity's material and light colours are converted from linear (Godot to
  sRGB, Unity to gamma), as their world colours were.
- **The recorded session** gives the Box a 4×4 checker as its Base Map, and every bridge's replay checks it.

## The Opus review's fixes
- **Majors:**
  - **Every texture was copied on the main thread at each generation bump** (a procedural drag, each stroke's end):
    about 150 ms four times a second in a scene of 2K and 4K textures. The global generation is out of the stamp, the
    main thread only copies the texels it must, and the worker converts them.
  - **The painting claim was untrue:** a painted surface's outputs are made again when the stroke ends, so it goes
    then. The docs, lesson and changelog say so, and the in-place test is named for what it does (the paint view's
    preview).
  - **A float (EXR, HDR) colour map went out linear, too dark.** It's now made sRGB by the slot's colour space, and an
    HDR test checks 0.2 grey arrives as 124.
  - **Emission maps didn't follow Blendity's rule** (colour × strength × map) in Godot (its operator added them) and
    Blender (the map replaced the colour; a map alone glowed). Both now multiply.
  - **Unreal made a new texture for every message.** It now updates one per key in place.
- **Minors:**
  - A new client's sync no longer takes a change owed to connected clients.
  - Textures no material names aren't watched or sent, and a new session starts clean.
  - Textures are capped at 2048 a side.
  - Unity's built-in metallic keyword is set, and maps taken off are cleared in Unity and Unreal.
  - Blender's textures are per session.
  - Unreal's base material is released at the module's shutdown.
  - Constants are named, a shared no-op reflector is used, and a NaN texel is black.

## Checked
- **`tests_round377`:**
  - the texture at the sync, its key named by the material, a 64×64 PNG;
  - a repainted file sent again once with a new hash;
  - the same pixels looked at again: nothing sent, counted as same;
  - another texture's change: this one not even copied;
  - an image changing in place over 6 frames: one during, one after;
  - an HDR colour map's 0.2 grey as byte 123 to 124 (sRGB);
  - a map taken off: its image's changes not sent.
- **The recording's test:** one texture message.

| Bridge | Test | Result |
|---|---|---|
| Blender 5.2.2 | smoke_test.py | SMOKE OK: the 4×4 checker from its bytes, 8 blue texels |
| Godot 4.7.2 | smoke_test.gd | SMOKE OK: the albedo texture 4×4, the colour sRGB |
| Unity 6000.6.7f1, built-in and URP | ReplayTests | Passed: _MainTex 4 wide, the colour gamma |
| Unreal 5.8 | Blendity.LiveLink.Replay | Success: the Box's BaseMap a 4×4 texture |

## Limits
- **Unreal** draws the Base Map only. Its normal, metallic and roughness maps are kept but not wired into its material.
- **Unity's Lit** has no roughness map of its own: smoothness lives in the metallic map's alpha, which Blendity's
  separate maps don't fill.
- **PNG size:** the dependency-free build's PNG compression is weaker than zlib's.
- **Float images** go as 8 bits: an HDR texture loses its range.
- **A painted surface** reaches the engines when each stroke ends, not during it.
- **Textures over 2048 a side** go at 2048.
