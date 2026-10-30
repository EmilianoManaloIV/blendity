# 0269: accessibility - Okabe-Ito polygroups, the eraser by shape, the CPU badge, selected keys by shape

- **Status:** done (round 289)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (F7)
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups of round 281's *Not done*: polygroup hues that neighbour each other under deuteranopia, the eraser said
by colour alone, the GPU view's CPU fallback said once in the log, selected keys by colour alone (High Contrast's
selected key the warning's yellow).

## What changed
- **Polygroups** (`sculpt_group_color`): Okabe and Ito's eight colours, then the same eight lighter and darker.
- **The brush ring** (`draw_brush_ring`, Paint, Scatter, the Textures canvas, Weight Paint): erasing or subtracting, it
  is dashed with an E beside it. Paint's ring now also follows the brush's Erase setting (it followed only the state
  set when a stroke begins).
- **A CPU badge** in the Scene view's bar while the views wanted the card and draw on the CPU, with the reason.
- **Selected keys** outlined in the dopesheet and the curves; High Contrast's selected key cyan.

## Tests (`tests_round289`)
Group 0 is #E69F00, the eight differ, the tiers change the lightness; Paint's ring erasing only with Erase on; the CPU
badge absent when the CPU is chosen, present with Auto and no card.

## Not done
- Hatching on polygroups (a GPU shader change).
