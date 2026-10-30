# 0264: the stylized tools in the panels - Scatter's source and mask by name, Strokes' alpha and Fill Canopy

- **Status:** done (round 284)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the follow-ups of round 282's walkthroughs)
- **Model:** main session
- **PR:** #227

## Asked
The user (2026-10-28): start and finish all the follow-up work. Round 282's stylized walkthrough kept three failing
steps: the leaf alpha (`strokeobj alpha`), the canopy fill (`strokeobj canopyfill`) and the scatter mask (a layer index
typed into a field) were console-only or numbers.

## What changed
- **Scatter's Source** is a dropdown of the scene's meshes (not the one painted on, not scattered instances).
- **Scatter's Mask Layer** is a dropdown of the surface's layers by place and name (an unnamed layer by its type).
- **Strokes' Alpha** is a dropdown of the project's images (None turns it off).
- **Fill Canopy**: a button with Fill Radius and Fill Count fields, running `strokeobj canopyfill`.
- `tool_combo_row`: a labelled dropdown row for the tool sections, its rectangle recorded for tests.

## Tests (`tests_round284`)
Real clicks in the Stylized layout (the Tools pane scrolled until the row is inside it): the Source dropdown picks
Sphere, the Mask dropdown its first layer, the Alpha dropdown a PNG written into the project, Fill Canopy adds 200 cards.

## Not done
- The scatter source is still one object (no list of sources).
