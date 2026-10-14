# 0072: A texture's Physical Size drives its materials' tiling

- **Status:** done
- **Requirements:** R-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/65

## Goal
The user (2026-10-14): "A texture's Physical Size should really adjust its tiling."

## Scope
- In: changing a procedural texture's Physical Size (Inspector, Textures window, `settex <asset> PhysicalSize <m>`)
  sets Tiling = 1 / size on every material of the scene using it (in any map). One undo step with the edit. A
  material whose Tiling no longer matches the old size was tiled by hand and keeps it.
- Out: an override checkbox on the material (the "tiled by hand" test stands for it).

## Acceptance criteria
1. Physical Size 2 m to 4 m: a material using it goes from Tiling 0.5 to 0.25; undo puts 0.5 back.
2. A material tiled by hand (3) keeps 3.
3. A material using it only as its normal map follows too.

## As built
- `Editor::retile_texture_users` (editor.cpp), called where a texture's fields change (the Inspector and `settex`).
- Which materials follow is decided at the first change of an undo step (a dragged size passes other sizes).
- A `.mat` no object in the scene uses is left alone: undo couldn't put it back. One the scene uses is written back
  with the project's other changed assets.
- Tests: round 98.
