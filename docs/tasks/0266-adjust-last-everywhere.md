# 0266: Adjust Last outside Edit Mode - primitives, UV operators, Draw shapes

- **Status:** done (round 286)
- **Requirements:** R-14
- **Decisions:** ADR 0085
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups: Adjust Last (F9) covered Edit Mode's operators only; an added primitive, a UV operator and a drawn
shape could not be adjusted after the fact (Blender's redo panel can).

## What changed
- `src/editor/adjust_last.cpp`: a panel at the bottom left of the Scene view after
  - **an added primitive**: Cube's size; Sphere's radius, segments and rings; Icosphere's radius and subdivisions;
    Cylinder's and Cone's radius, height and segments; Torus's radii and segments; Plane's size and subdivisions;
  - **a UV operator with numbers**: Unwrap's and Pack's margin (Pack's rotation), Smart UV's angle limit and margin;
  - **a Draw shape made by clicks**: a circle's or an arc's segments, a polygon's sides, a rectangle's fillet.
- A change runs the operation again from the state before it and **amends the same undo step** (the step holds the
  scene before; only the editor's after-snapshot is taken again): one Ctrl+Z still goes back to before the operation.
- The panel lasts while that step is the newest; Edit Mode's own panel gives way to it; F9 works in the Scene view.

## Tests (`tests_round286`)
Typed into the panel: an added Cylinder's 8 segments (26 faces become 10, the undo steps unchanged, one undo removes
it); an Unwrap's margin 0.2 (the UVs move, the steps unchanged); a drawn circle's 6 segments (24 vertices become 6);
another step ends the panel.

## Not done
- **Knife**: it has no numbers to adjust (Blender's Knife has no redo panel either).
- A polyline or a shape finished with Enter / double-click (only the click-made shapes are recorded).
