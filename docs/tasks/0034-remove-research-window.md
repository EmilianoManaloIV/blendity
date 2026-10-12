# 0034: Remove the Research window; papers cited in the Learn tab

- **Status:** done (as built below)
- **Requirements:** R-01
- **Decisions:** none (removes a window; no ADR covered the research folder)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The Research window (a paper drop folder, a list of papers and of "research features") goes. Every feature
built from a paper keeps working and is cited where users learn about it: a **References** lesson in the
Learn tab, and a citation line in the lesson of each topic it belongs to.

## Scope
- In:
  - **Removed from the UI:** `WindowKind::Research` (the value kept, unlisted, so saved layouts load: a layout
    holding it drops that tab), the Window menu and Ctrl+Tab cycle entries, File > Open Research Papers Folder,
    the Project window's "Research Papers" folder, dropping a PDF / TeX / BibTeX onto the editor copying it to
    `research/papers` (now: "not a file Blendity opens"), `window Research` / `research` (the console says where
    references went), the Learn lessons' `research` verb.
  - **Kept:** the features themselves (Taubin smoothing and Add Surface Noise in the Inspector, under a
    "From papers" heading that names the paper), `src/research/` as their source, and the `research/` folder
    on disk (the user's; nothing is deleted).
  - **Learn > References:** one entry per paper a feature uses, from the features' own citations and the papers
    cited in the code and ADRs: Taubin 1995 (smoothing), Catmull & Clark 1978 (subdivision), Thiedemann et al.
    2011 (voxel GI), Ju, Schaefer & Warren 2005 (cage, later), Heitz et al. 2018 (area-light bake), Karis 2013
    (area-light specular), Shirley & Chiu 1997 (disk sampling), Ward / Cook-Torrance / Walter et al. 2007 (GGX)
    and the others the code cites; each with the feature, where it is in the editor and a Try-it button when
    there is one. The topic lessons gain a "Based on" line where they lack one.
- Out: moving or deleting the user's `research/` files.

## Acceptance criteria
1. No Research window in the Window menu, the dock or Ctrl+Tab; an old layout with it loads without it.
2. Learn has a References lesson listing every paper a feature uses, each naming its feature; its Try-it
   buttons run (Taubin smoothing on a noisy sphere).
3. Taubin smoothing and Add Surface Noise still work from the Inspector and the console.
4. Dropping a PDF does nothing but a message.

## Tests and checks to run
- Unit (round 56), full suite, Linux, sanitizer.

## Documentation to update
- CHANGELOG, README (the Research paragraph becomes a sentence about References), ARCHITECTURE (the research
  row), USABILITY row.

## As built
Built as specified: the window's entries removed (Window menu, File and Help menus, the Project window's folder,
Ctrl+8 now opens References), `dock_reset` drops a saved layout's Research tab (an area holding only it closes),
`window Research` / `research` / `references` and the lessons' `research` verb open Learn > References, dropping a
document only logs where the references are, the Mesh menu's and Inspector's "From Papers". The References
lesson replaces "Your Research" in place (lessons are saved by position: progress keeps its meaning).

**Deviations:**
1. **`WindowKind::Research` stays in the enum, unlisted** (layouts are numeric), its draw call a no-op.
2. **No "Based on" lines added to each topic lesson:** the topic lessons already cite their papers where they
   lean on one (the lighting lesson cites Thiedemann et al.), and References gathers all of them.
3. **Found on the way:** the Textures window's preview kept its repacked picture by the image's address; an
   image freed and another allocated at the same address (after an undo) showed the old picture (the round 50
   undo test failed intermittently). The pane holds the image it repacked; the Materials window's previews are
   keyed by the material's content (not its address and version, which a clone allocated where a freed material
   was could repeat) and check their textures through weak references; entries not drawn for a while are evicted.
4. **A project is recognised by `Assets` plus `research` or `Assets/Scenes`** (it was `Assets` plus `research`,
   which new projects no longer get).

**Review:** the code reviewer found a major issue and several minor ones, all fixed: the tab menu's Add Tab still
offered Research (an empty tab); Ctrl+8 opened Learn at the last lesson instead of References; a layout with
Research in its root area kept it (and one that was only Research was empty: now the default layout); the
path-guiding citation named the SD-tree paper instead of the parallax-aware mixtures Open PGL is set to
(Ruppert, Herholz & Lensch 2020); the user's research folder could still be deleted from Blendity; the Materials
previews held textures indefinitely; dead state, help text, names' diacritics.
