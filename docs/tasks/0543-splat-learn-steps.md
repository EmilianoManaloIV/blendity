# 0543: The splat Learn section walks through the renderers (round 561)

- **Status:** done (round 561)
- **Requirements:** U-xx (Learn: a lesson for every feature)
- **Decisions:** none needed
- **Model:** main session (Opus); Learn text only
- **PR:** (link when opened; the new top of Pass 27's stack, on #443)
- **Asked by:** the hub, from M3's Learn walk (verify-544, "Learn check list"), 2026-11-17

## Goal
"Point clouds and Gaussian splats" had one Try-it step: it made test splats and stopped. Afterwards the section's steps
show rounds 532-534 at work: the splats orbited in the Scene view next to a Points cloud, seen in the Game view, and
rendered with F12 on the CPU path tracer, with the card's device one step away.

## Scope
- In: `src/editor/lessons.cpp`, the section's text, five more Try-it steps and four more questions; a test that walks
  the section in a fresh editor.
- Out: a step that renders on the graphics card. The card step sets Device to GPU Compute and opens the Render window, and the
  learner presses F12. A walk that rendered on the card would need a card window from the hub, and CI has no card.

## Acceptance criteria
1. The section has at least six Try-it steps and six questions.
2. Run in order in a fresh headless editor, as the Learn tab runs them, no step logs a warning or an error.
3. Afterwards the scene holds exactly two clouds - 10,000 splats and 10,000 Points at (2.5, 0.5, 0) - the engine is the path tracer and
   the device is GPU Compute (which falls back to the CPU without a card; the walk does not render on it).
4. M3's "learn (Pass 27's write-up)" walk finds the section clean.

## Tests and checks to run
- `BLENDITY_TEST_FILTER="learn: 'Point clouds"` in group `splats`, then the `splats` and `editor` groups.

## Documentation to update
- `changelog.d/561-splat-learn-steps.md`.
