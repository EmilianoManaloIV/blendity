# Task 0330: textures and lightmap UVs in exports

- **Status:** done (round 353, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 2 of
  docs/hci/compare-2026-10.md: the exported avatar's .glb held none of its painted texture set.

## Cause
A relative export path (`export glb Exports/Avatar.glb`) was written relative to the working folder, so the baked PNGs
landed where the glTF writer didn't look (round 351 made the path the project's). This round finds a second cause: an
export into a folder that didn't exist yet wrote its PNGs before the file made the folder - they failed, and the
material kept its procedural reference, which no other program reads.

## What
- The export's folder is made before anything is written, so the texture set's PNGs are there to embed (glTF) or
  reference (FBX).
- **FBX gets the baked images too:** procedural and painted (.ptex) maps are written beside the file for FBX as they are
  for OBJ and glTF.
- **Generated images** (UV Grid, Color Grid) are written as PNGs and referenced (no program knows "generated:").
- **glTF writes the emission map** (`emissiveTexture`); it had written base colour, metal-roughness and normal only.
- **Lightmap UVs** in the export options (glTF and FBX; off by default): a second UV set laid out by the lightmapper's
  own unwrap (no overlaps, a margin between islands) - glTF `TEXCOORD_1`, FBX a second UV map named Lightmap - which
  Unity (UV2), Godot (UV2) and Unreal bake their lighting onto.

## The review's fixes (Opus code-reviewer: approved, minors fixed)
- Tests that could pass on an earlier run's files: the folders are removed first, the FBX goes to its own new folder and
  its own references are checked (base, normal and emission maps found); the Color Grid's written pixels are compared
  with the grid's; the emission lit (glTF multiplies it by the map); the FBX's Layer 1; a mesh without UVs (the lightmap
  set as TEXCOORD_0, said in the log); FBX with the option off writes one UV map.
- Generated images are written as `Blendity_UV_Grid.png` / `Blendity_Color_Grid.png`, so a user's own UV_Grid.png
  beside the file is never overwritten; a failed write is said.
- Outline shells get no lightmap UVs (no engine lightmaps them); the layout's numbers named (1024 texels, 4 between
  islands, Unity's margin at that size).
- `export <fmt> [all] [ascii] [lightmap] [path]` from the console; the Try-it and a question about lightmap UVs in the
  files lesson.

## Checked
- `tests_round353`: a relative path lands in the project; the .glb embeds the albedo, the normal and the Color Grid as
  the emission; the FBX references 3 PNGs beside it and its material's base map exists; TEXCOORD_1 inside 0..1, the FBX's
  second UV map; the option off writes none; the lesson's Try-it.
- Blender 5.2 imports the .glb and the FBX with all three images, and both with two UV maps when Lightmap UVs is on.
