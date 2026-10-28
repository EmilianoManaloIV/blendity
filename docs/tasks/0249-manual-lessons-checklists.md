# 0249: the manual, the Learn lessons, the check lists

- **Status:** done (round 269)
- **Requirements:** R-14
- **Decisions:** none (documentation of ADRs 0068-0084)
- **Model:** main session
- **PR:** #215

## Asked
The user (2026-10-27): after the customize, sculpt, paint and stylize rounds, the manual, the Learn lessons and the
check lists that make them usable.

## What changed
- **The manual** (`docs/MANUAL.md`): chapter 1 gains *Your editor, your way* (the Wheel, panels anywhere, a shortcut
  for anything, UI files, the module registry, the stages as one pipeline); chapter 3 *Stylized environments*
  (Strokes, canopy and grass, Scatter, the World's disc, band and fog, the Stylized layout); chapter 4 *Sculpting*
  (brushes, masks, polygroups, hiding, the four ways to add resolution, and the pipeline order *sculpt > remesh >
  unwrap > bake mesh maps > paint* with why); chapter 6 *Painting the surface* (the Substance pipeline in five
  numbered steps: mesh maps, the stack with mask stacks and smart masks, Paint Mode with 2D and stencils, smart
  materials, export) and *The hand-painted look* (baked light, the painterly brush, the palette, the filters, the
  Hand-painted model); chapter 13's console table gains the sculpt, paint and stylize rows. The chapter count stays 13
  (the manual test).
- **The features tracker** (`research/FEATURES.md`): nine rows for the rounds' methods with their papers where there
  is one (Jacobson 2013 winding numbers, Gibson 1998 surface nets, Huang 2018 QuadriFlow, Baxter 2004 IMPaSTo marked
  simplified) and "no paper" where the method is a tool's (Dyntopo, mesh maps, the stack, Project Paint, canopy
  normals), each pointing at its source file and test round.
- **Five Learn lessons** with Try-it buttons (`src/editor/lessons.cpp`): *Customizing the Editor* (the Wheel, two
  layouts, the shortcut list), *Sculpting* (Sculpt Mode on the Sphere, the Clay brush, Mirror X, a mask clear, DynaMesh
  64), *Painting the Surface* (mesh maps, a surface, a Fill with an edge-wear mask, Worn Steel, Paint Mode, a glTF
  export), *The Hand-painted Look* (baked light, Paint Mode with a bristle tip and wet mixing), *Stylized Environments*
  (the Stylized layout, Strokes, a canopy, grass, Scatter, fog) - each citing the reference books and Blender's source
  folder, with a question. The Learn tab's Try-it test runs every `! cmd:` of the new sections.
- **Check lists:** the pipeline orders in chapters 4 and 6 (sculpt > remesh > unwrap > bake > paint; bake > stack >
  paint > smart materials > export) are the check lists the manual carries; the task specs' *Not done* sections list
  what each round left open.

## Tests
No new code: the existing manual test (13 chapters, the README's link, the Learn notes) and the Learn tab's Try-it
test (every new section's commands run in a headless editor) cover the additions; the editor, assets and animation
groups pass with them.
