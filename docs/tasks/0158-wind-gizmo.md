# 0158: The Wind gizmo, and icons for the effectors

- **Status:** done (round 176)
- **Requirements:** R-06, R-08
- **Decisions:** none (task 0146's overlay layers; task 0153's Wind)
- **Model:** main session
- **PR:** #121 (stacked on #120)

## Goal
Task 0153 left out "a gizmo line in the Scene view's icons" for the Wind component; the user asked for it
(2026-10-21: "we should [have] a gizmo for the wind component as specified in this project").

## Scope
- In: `Editor::scene_icon_of` gives every light, camera, Wind, Rotator and Oscillator its Scene view icon (new
  `Icon::Wind`, `Spin`, `Wave`), drawn on the icons layer and picked within 14 px like a light's;
  `Editor::wind_gizmo_segments` draws a selected Wind's wire: an arrow along its direction (half a metre plus a
  quarter metre per m/s of Strength), two wavy streamlines beside it when Turbulence is above 0 (waves as deep
  as the turbulence), three rings instead of the arrow when Spherical; the direction comes from
  `spring::wind_direction`, which `winds_of` uses too, so the gizmo can't drift from the simulation; the icons
  layer is "Light, Camera & Effector Icons".
- Out: dragging the arrow's tip to set the strength; icons for the PlayerController and Locomotion.

## Acceptance criteria
1. A 3 m/s Wind turned 90 degrees about Y: the arrow's tip at origin + (1, 0, 0) x 1.25 within 1 mm, three
   segments; its direction equals `winds_of`'s.
2. Turbulence 0.5 adds two streamlines of twelve pieces; Spherical gives three rings of radius 1.25 and no arrow.
3. A Wind, a Rotator and an Oscillator have icons; a plain object has none.
4. A click within 14 px of the Wind's projected position selects it; with `overlay icons off` it selects nothing.

## Tests and checks
- `wind gizmo: a selected Wind draws an arrow ...` (round 176); the overlays and spring-bone groups unchanged.

## After the review
- The near-vertical fallback basis wasn't perpendicular (barbs lopsided, rings ellipses within 26 degrees of
  vertical): `basis_of` builds a true orthonormal pair; a test pitches the wind 80 degrees.
- `std::isfinite` is folded away under `/fp:fast`: the gizmo reads `spring::wind_strength` / `wind_turbulence`
  (finite_bits, clamped), which `winds_of` uses too.
- Three streamlines (the spec's count; the third above the shaft); the gizmo moved to `rig_controls.cpp` beside
  the shape helpers, its rings through `circle()`; `streamline` and `draw_wind_gizmo` extracted; the gizmo is
  drawn before the icon's projection, so an arrow shows with its origin off screen.
- Every object asked three `dynamic_cast`s a frame for its icon: a `RenderRole::Effector` on Wind, Rotator and
  Oscillator makes it one virtual call, the casts only on a hit; the light / camera lookups come from the icon.
- The test split into the gizmo (pure) and the icons (an Editor), the tip within 1e-4, the direction checked
  against `winds_of`.
