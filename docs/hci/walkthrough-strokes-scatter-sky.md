# Cognitive walkthrough: a stylized scene - strokes, scatter, a painted sky (baseline, round 271)

Same conditions and columns. The user: an environment artist from Unreal (foliage tool, post-process volume) on a pen.

| # | Step | Try | Notice | Associate | Progress | Finding | Fix |
|---|---|---|---|---|---|---|---|
| 1 | Open the Stylized layout | ? | Y | Y | Y | The Layout dropdown (Unity's); an Unreal user looks for a Mode | 0261 |
| 2 | Strokes: paint cards on a branch | Y | Y | Y | Y | Modeling Tools > Strokes; no status-bar hint for the tool (`command_palette.cpp:310`) | 0254 (F3) |
| 3 | Give the cards a leaf alpha | ? | N | - | Y | `strokeobj alpha <image>` is console only; the guide says so | 0261 |
| 4 | Make a canopy | Y | N | Y | Y | `strokeobj canopyfill` is console only; the section has Normals and a Canopy Centre button but no Fill button | 0261 |
| 5 | Scatter rocks along a path | Y | Y | ? | Y | `scatter <object>` names the source by text; an Unreal user expects to drop the asset into a slot; no hint | 0254 (F3), 0261 |
| 6 | Keep them off the path with a mask | ? | N | N | Y | The mask is a Paint layer index typed into a field; the Textures window's layer names are not offered | 0261 |
| 7 | Erase a few | Y | ? | Y | Y | The pen's eraser end works; a mouse user has `scatter erase` only | 0261 |
| 8 | A sun disc and horizon band | Y | ? | Y | Y | In the Lighting window's World, inside a folded "Stylized Sky" group; the Render window edits the same World | 0256 |
| 9 | Fog | Y | ? | Y | Y | A "Fog" group in both windows; the Retro filter has its own fog | 0256 |
| 10 | Check it in the GPU view | Y | Y | ? | ? | If the card is busy the view falls back to the CPU with one warning, then silently (`gpu_views.cpp:601`): no badge | 0253 (F2) |

**Baseline score** (an N in any of the four columns): 10 steps, **3 failing** (3, 4, 6); five more carry a ? . The step
most likely abandoned: 3 / 4 (console-only foliage features), fixed by surfacing them in the Strokes section.
