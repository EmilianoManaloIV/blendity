# 0511: Importing Gaussian splats - PLY, SPZ and glTF (round 531)

- **Status:** done (round 531)
- **Requirements:** R-10 (import), Q-01 (robust data), C-02 (dependency-free)
- **Decisions:** ADR 0147 (the layout); none new
- **Model:** main session (Opus) for the formats; test-engineer and code-reviewer agents
- **PR:** (link when opened; stacked on round 530's)
- **Approval:** covered by the user's approval of Pass 27's plan (2026-11-15, relayed by the hub).

## Goal
Blender 5.3 imports Gaussian splats from PLY (auto-detected), SPZ (v2-v4) and glTF's `KHR_gaussian_splatting`.
Blendity refuses a PLY without faces (`import_formats.cpp`, "point clouds are not supported"). Afterwards each of
these becomes a Point Cloud object (round 530): a 3DGS PLY or an SPZ a Gaussian Splats cloud, a plain point PLY a
Points cloud, with the same count, attributes and SH as Blender 5.3 imports from the same file, in Blendity's axes.

## Scope
- In:
  - `src/scene/import_splat.h/.cpp`: the PLY splat / point reader (detected as Blender does: `f_dc_0..2`,
    `opacity`, `scale_0..2`, `rot_0..3`, `f_rest_*` on a face-less vertex element; colours from `red/green/blue`
    for Points), SPZ v1 / v2 / v3 (gzip, through `unity_package`'s `gunzip` over `image.cpp`'s inflate; v1's float16
    positions from Niantic's format notes, which Blender's reader refuses) and v4 (NGSP,
    zstd streams - with Blender's libraries only; the dependency-free build refuses v4 with a message), and the glTF
    extension's attributes. Decoding as Blender's (`OriginalActivationFunctions`, SPZ's fixed point, its 8-bit
    colour / alpha / log scale, v2 and v3 rotations, its SH quantisation).
  - Axes: a PLY is Z-up right-handed like the meshes Blendity reads from PLY (`from_z_up`); SPZ (RUB) and glTF are
    Y-up right-handed (`mirror_x`, the same result as Blender's +90 degrees about X followed by `from_z_up`); positions, rotations and SH coefficients are carried into Blendity's space (SH by
    an exact per-band linear map for the orthogonal change of axes).
  - SH bands: degree 4 in a file keeps bands 1-3 (warning); an incomplete band keeps the complete ones (warning).
  - Hooks: `ImportedNode::points`, `import.cpp` (a node's cloud becomes a `PointCloud` component; `.spz` in the
    import list), the PLY importer (a face-less file goes to the point reader), the glTF importer (a primitive with
    the extension), the editor's import dialog filter.
- Out: USD particle fields, export, SH degree 4, per-point radius for Points.

## Acceptance criteria
1. A 3DGS PLY (binary little / big endian, ASCII) becomes one Gaussian Splats object with every point, the opacity
   and scale decoded, rotations normalised, SH degree as the file's `f_rest` count says.
2. A PLY with only `x y z [red green blue]` becomes a Points cloud (colours from 0-255 to linear), not an error.
3. SPZ v2 and v3 (gzip) decode as Blender's reader does; v4 decodes with Blender's libraries and is refused
   cleanly without them.
4. glTF `KHR_gaussian_splatting` primitives become Gaussian Splats objects.
5. Truncated files, NaN values, a zero count and a huge count refuse cleanly or give a valid cloud; never a crash or
   an allocation the size of a bad header.
6. For the same files, count, attributes and SH (after the axis change) match what Blender 5.3 imports - checked
   against reference numbers written by Blender 5.3 headless where it runs, else against hand-made files whose
   values are known.
7. The unit suite passes on Windows and Linux; a stress section fuzzes the readers.

## Documentation to update
- `changelog.d/531-splat-import.md`, `licenses/README.md` ("Ported, not copied": `io/spz/`, `ply_import_gsplat.cc`,
  the glTF add-on's SH conversion, Apache-2.0).
