# 0060: A README for GitHub and one user manual

- **Status:** done (as built below)
- **Requirements:** R-01
- **Decisions:** none (documentation)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/57

## Goal
The user asked (2026-10-12) to "update the GitHub README and compress the tutorial docs into one document with
examples and use cases", building on the Learn tab ("gives an immediate documentation to not only learn but see").

## Scope
- In:
  - **README.md** as a short front page: what Blendity is, the screenshots, a quick start, what it does by stage (the
    layouts of task 0055), the minimum and recommended machine, building, links to the manual and the developer docs.
  - **docs/MANUAL.md**: one manual by stage of the work - getting around, reference and planning, blocking out,
    modeling, UVs, materials and textures, lighting and rendering, rigging and skinning, animation, files,
    performance, shortcuts, the console - each with short examples to follow, use cases, and the Learn lessons that go
    deeper. It takes in the README's long feature list, the lighting guide and the user-facing parts of the lessons.
  - `docs/guides/lighting.md` points into the manual.
  - The Learn tab's Welcome lesson has a "The manual" section with a Try-it; the `manual` console command opens it
    (the project's copy, else GitHub's).
- Out: new screenshots (the current ones still show the editor as it is).

## Acceptance criteria
1. The README fits on a screen or two and links the manual.
2. The manual's 13 chapters each have examples, and every lesson it cites exists in the Learn tab (a unit test checks).
3. Nothing in the manual describes a feature the editor doesn't have.

## As built
- The README's stress-test highlights and project layout moved out: the numbers are in `PERFORMANCE.md`, the layout in
  `ARCHITECTURE.md`. Its "not recreated" list no longer names rigging and animation.
- **Opening a web address** on Windows no longer turns its slashes into backslashes (`fs::open_external`), so
  `manual` can open GitHub.
- A unit test reads the manual: its 13 chapter headings, the README's and the lighting guide's links to it, and that
  every lesson or lesson section it names (in its "Learn:" notes) is in the Learn tab.

## Tests and checks to run
- Unit tests (round 84); full suite, verify mode, Linux, sanitizer.
