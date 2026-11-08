# 0436: UVs evaluated, not invented (round 460)

- **Status:** in review
- **Requirements:** R-02 (modeling), R-04 (UVs and materials)
- **Decisions:** ADR 0120 (the drawing solver), decision 1
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
Drawing a shape on a textured face, cutting a face with a line, or Push/Pulling a drawn region leaves the texture where
it was. Today a new corner's UV is (0,0) (`FaceBuilder::add` with no UVs, `src/scene/mesh_internal.h:29`) or a copy of a
neighbouring corner's (`split_face_path`, `src/scene/mesh_tools3.cpp:564`), so the checker breaks wherever you draw.
Afterwards a new corner's UV is read off the face it lies in, and Push/Pull walls get a strip with the face's texel
density, as Extrude's walls already do.

## Scope
- In:
  - **New** `meshops::face_uv_at(const Mesh &m, size_t face, Vec3 p) -> Vec2` in `mesh_tools3.cpp`, declared in
    `mesh.h` next to `split_edge`. Triangles and planar quads: barycentric / bilinear-inverse in the face's plane. N-gons:
    mean-value coordinates (Floater 2003) over the corners, projected into the face's plane (works for concave faces).
    A point outside the face (a ring vertex a hair outside after float rounding) is clamped to the outline first.
    A face whose corners' UVs belong to two charts (a seam runs inside it - corners with the same vertex but
    different UVs are not possible within one face, so "two charts" means the UV outline folds or is degenerate:
    zero UV area or a flipped triangle in its fan) falls back to the nearest corner's UV, so no UV is invented
    across a fold.
  - **Use it** where corners are created from a face today:
    - `split_face_path`'s interior points (copied corner UV now);
    - `imprint_loop`'s ring and inner face (`mesh_tools.cpp:2809`);
    - `imprint_loop_across` (`mesh_tools4.cpp:390`), through the same helpers;
    - `attach_face` (the new face lies outside `f`: UVs extrapolated from `f`'s plane mapping - the affine map fitted to
      `f`'s corners, least squares for n-gons);
    - `dissolve_line_faces`;
    - the free face Draw adds (`draw_tool.cpp:567`): a planar box projection at the mesh's average texel density, so a
      new face is textured, not all-zero.
  - **Push/Pull walls** (`push_pull` and its curved path, `mesh_tools.cpp` ~1890-2400): a wall quad's UVs continue the
    side face's edge UVs along its length and run the pulled distance across, scaled by the host's texel density - the
    same strip `extrude_faces` writes (`mesh.cpp:2259`); a stretched coplanar neighbour's moved corners get
    `face_uv_at`-style extrapolation from that neighbour's mapping instead of keeping their old UV.
- Out: projection onto curved surfaces (task 0437); welding and cleanup (task 0438); re-unwrapping; UDIM / second UV sets
  (only the active `uvs` layer exists).

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh_tools3.cpp` (`face_uv_at`, `split_face_path`, `attach_face`, `dissolve_line_faces`)
- `src/scene/mesh_tools.cpp` (`imprint_loop`, Push/Pull walls), `src/scene/mesh_tools4.cpp` (`imprint_loop_across`)
- `src/scene/mesh_internal.h` (`FaceBuilder`: no behaviour change; callers pass UVs)
- `src/editor/draw_tool.cpp` (`draw_commit`'s free face)
- `tests/test_modeling.cpp` (`tests_round460`), `tests/test_editor.cpp` (a real-event draw on a checker plane)

## Acceptance criteria
1. `face_uv_at` returns the corner UVs at the corners, the bilinear / barycentric value inside a triangle and a quad (to
   1e-5), and a value inside the UV hull for a point inside a concave n-gon.
2. After `imprint_loop`, `imprint_loop_across`, `split_face_path`, `attach_face` and Push/Pull on a UV-mapped plane grid
   and on the teapot (`primitives::teapot`): every face the operation did not touch keeps its UVs **bit for bit**.
3. On planar hosts (where the UV map is affine) the new faces' `face_area_stretch` (`uv.cpp:849`) is within 5 % of
   their host faces'. On curved hosts (the teapot's bent, trapezoidal quads) every new corner's UV equals the host's own
   bilinear mapping at that point, within 2e-3 of the host's UV extent (the shape is flattened into the host's best-fit plane, slightly off the bent patch). *Amended in testing (round 460):* a trapezoid's
   bilinear density varies across the face, so a part of it cannot match the whole face's average stretch (up to 1.31x
   on the teapot) even with exact UVs.
4. No new face has all its corner UVs equal to (0,0) when its host had UVs; a free drawn face gets non-degenerate UVs.
5. A mesh without UVs stays without UVs (no `uvs` layer created).
6. Existing unit tests and the stress sections `pushpull`, `modifiers_ngon`, `curved`, `editor` show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="uv"` / `--group modeling` and `--group editor` in both builds, then the full suite
  (Windows + Linux).
- Stress: `--only pushpull | modifiers_ngon | curved | editor` - problem and overlap counts must not rise.
- `py tools/test_coverage_map.py --check`.

## Risks and edge cases
- Degenerate host faces (zero area, collinear corners): fall back to the nearest corner's UV; never NaN.
- Faces with a UV fold or a seam-split vertex next to the new corner: the corner takes its own face's chart, so a seam
  stays a seam (`split_edge` already keeps per-face UVs at a split).
- Bit-for-bit on untouched faces: the operators rebuild faces through `FaceBuilder`; untouched faces must copy their UV
  arrays unchanged, not re-evaluate them.
- Push/Pull's F9 re-run must give the same UVs as the first run (LastOp starts from `before`).
- Non-uniform object scale: UVs are evaluated in mesh space, so scale does not matter.

## Documentation to update
- CHANGELOG (round 460), README count, ARCHITECTURE row (`face_uv_at`), USABILITY "fixed" row (texture breaks where you
  draw), ADR 0120 decision 1, Learn: a `?` question and a `!` Try-it (checker material, draw a rectangle, Push/Pull) in
  "Drawing onto faces".
