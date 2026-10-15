# 0089: The Edit Mode cage drawn from parallel batches

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/80

## Goal
The Edit Mode overlay of a 50k-face mesh took about 7.5 ms a frame on the laptop setting, and half of it was the
editor recording 100,000 edges and 50,000 vertices one call at a time: each edge placed both its vertices in the
world and projected them. Drawing them was already parallel (bands of rows).

## Scope
- In: `Renderer3D::lines` / `points`: many overlay items at once, prepared in parallel chunks and recorded in the
  order given - the same commands as one call each. The Edit Mode cage places each vertex in the world once and
  records its edges and vertex dots through them.
- Out: the overlay's drawing (already in parallel bands).

## Acceptance criteria
1. 30,000 lines and 10,000 points (depth-tested, clipped at the screen edge, some behind the eye) recorded in batches
   draw exactly the pixels one call each draws; an item left out is left out.

## As built
- Tests: round 115. Stress (laptop setting, 50k faces): the Scene view's overlays 7.4 -> 4.5 ms a frame; the gizmo
  drag 38.6 -> 35.4 ms, G 37.0 -> 34.4 ms.
