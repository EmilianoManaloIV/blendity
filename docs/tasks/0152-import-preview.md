# 0152: The import preview with switchable shading

- **Status:** done (round 170)
- **Requirements:** R-03, R-05
- **Decisions:** ADR 0047 (imported characters), ADR 0048 (toon styles); no new decision
- **Model:** main session
- **PR:** #115 (stacked on #114)

## Goal
The user: a preview when importing a model, with the shading switchable before anything lands in the project - the
model as the file made it, PBR, or one of the toon styles - so an anime character arrives looking like one.

## Scope
- In: `Editor::begin_import_preview` (the model read into an `ImportResult`, instantiated into the preview's own
  `Scene`, nothing written under Assets), the Import Preview dialog (`Dialog::ImportPreview`: the model rendered by
  the CPU rasterizer under the editor's headlight, turning slowly, dragged to orbit; the import's summary and notes;
  a Shading row - As Imported, PBR, Classic, Guilty Gear, Soft Anime, Painterly, Manga - applied to every material
  of the preview through the file's materials kept as text; an Outline switch with a width; Import and Cancel),
  `finish_import` (the rest of the old `import_model_file`: the copies into Assets, the `.mat` files from the chosen
  materials, the objects, the clips, an Outline on each mesh when asked), `cancel` discarding the preview scene.
  Preferences > Import: "Preview before importing" (on by default; off, or headless, imports as before) and the
  default shading. Console `import <file> [asis|pbr|classic|guiltygear|soft|painterly|manga]`: a shading word
  imports at once with it (scripts and tests); without one, the dialog when the preference is on. The "Importing:
  preview the look" lesson section.
- Out: the GPU viewport for the preview (the CPU rasterizer draws every material, painted maps included); a
  path-traced preview; a per-material shading choice (every material takes the same, as a character's do; the
  Materials window edits them after); keeping the file's materials AND a toon style at once (a toon style is applied
  over the file's materials, so an MToon shade map stays with Soft Anime - that is the file's maps under a style).

## Acceptance criteria
1. `import <glb> guiltygear` makes `.mat` files under Assets/Materials/<model> whose fields are Guilty Gear's
   (toon_style 1, shading_model 1); `import <glb> pbr` PBR ones; `import <glb> asis` the file's.
2. The dialog (headless UI): opening the preview writes nothing under Assets; the picture has non-black pixels; the
   Shading row's buttons (`button_rects` "ipv:Soft Anime") switch the preview materials (toon_style 2); Outline on
   adds Outline components to the preview's meshes; Cancel closes the dialog, leaves Assets untouched and no objects
   in the scene; Import makes the objects, the `.mat` files with the chosen fields and the Outlines.
3. The preference off: `import <glb>` imports at once. The default shading preference is applied when the dialog
   opens.
4. (The MMD and VRM readers share the path: the GLB stands for every format in the tests.)
5. A look chosen at import rewrites a model's existing `.mat` files (As Imported keeps them); the console's short
   and two-word forms (`soft`, `guilty gear`); the preference's round trip and clamp; the plain `import` opening the
   dialog when the preference is on; a second file waiting its turn; the popup clicked away as a Cancel; the Outline
   checkbox by a click; the picture's centre pixel not the sky's.

## After the review
- A model imported again kept its existing `.mat` files whatever the dialog said: a look other than As Imported
  rewrites them now (the tooltip says so).
- Several files dropped at once lost all but the last: they queue, one preview after another; a drop while a preview
  is open waits its turn.
- The Outline switch didn't reach a file's own outlines (an MMD's) on import: `finish_import` takes Keep / Off / Set.
- The turntable re-rendered every frame and spun by the frame rate: it turns by the clock (30 degrees a second) and
  renders in 2-degree steps; the headlight follows the preview's camera (half the turn was back-lit); plain tone
  mapping; the picture's height bounded by the window; the blit guarded; the key a compared struct.
- The console's `soft` and the two-word names didn't match; a motion file with a shading word went to the model
  reader; a file with no mesh opened an empty dialog (the plain import now); meshes without a material get one in the
  import's list so the shading reaches them.
- The draw items are built by `mesh_draw_item` / `outline_draw_item`, shared with the views; the shading names come
  from `toon_style_names`; the dialog's rows are their own functions; the orphaned comments moved.
- Out, noted: a PMX / VRM preview test (the GLB stands for the shared path); the preview ignores the GPU viewport.
