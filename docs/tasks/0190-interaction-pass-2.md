# 0190: the interaction pass, round two

- **Status:** done (round 206)
- **Requirements:** R-01 (a Unity-style editor), R-07 (every tool reachable)
- **Decisions:** ADR 0060 amended
- **Model:** main session
- **PR:** #151 (stacked on #150)

## Choosing the changes
Task 0189's audit showed Blendity's paths to common commands are already short (28 actions for eleven tasks, two or
three each, like Unity's and Blender's), so counting actions can't pick this round's changes. They are chosen instead
by what the research says costs newcomers most where no click count shows it - recall and choice (Hick's law: fewer
choices on screen; the 2025 study: task-relevant tools, progressive disclosure, scaffolding) - and by the roadmap's
list (0190's candidates), each checked against what already exists:
- already there, kept: the Hierarchy's keyboard navigation (arrows select, left / right fold), materials dropped on
  Hierarchy rows, Edit Mode's typed values for Push/Pull, Edit > Undo naming the action;
- done here: the five below.

## The changes
1. **Inspector essentials** (progressive disclosure): each component and modifier shows its first seven fields; the
   rest wait under "Show N more" (per component type, "Show less" folds them again); the Inspector's All Fields switch
   (and `inspector essentials on|off`, kept in the preferences) shows every field. A group past the seventh field
   counts as hidden. Headless runs show every field, so the tests' field lookups are unchanged.
2. **A first-run guide per layout** (scaffolding): the first time a stage layout is used, a strip above the docks says
   what it is for and offers its next three steps as buttons (the palette's commands: UV Unwrapping offers Smart UV
   Project, Part Unwrap and Pack Islands); Got it hides it for good (kept in the preferences); Help > Reset Guides,
   `layoutguide reset|dismiss|on|off`.
3. **File > Open Recent**: the last eight scenes opened, Clear Recent; kept in the preferences.
4. **Edit > Redo names the action** ("Redo Create Sphere"), as Undo already did.
5. **A number typed while dragging a gizmo axis** moves exactly that far along it (an expression works: 2*1.25),
   shown beside the cursor; Backspace takes a digit back - Blender's numeric input, which Edit Mode's Push/Pull had.

## Acceptance criteria
1. A Camera shows Show more until opened; All Fields shows every field.
2. A layout's guide shows its purpose and three steps until Got it, then never; each guide's steps are palette commands.
3. An opened scene is first in Open Recent; Redo is labelled with the action.
4. "2*1.25" typed during an axis drag moves 2.5 along the axis.

## Results
All four hold (tests/test_editor.cpp). The audit's action counts don't change for these (they are about what is on
screen and what must be remembered, not path length); a newcomer study would measure them, as the 2025 study did.

## Limits found in review
The seven fields are the first seven in each component's reflection order, so a component whose early fields depend
on a switch can push an important one past the cut: an Area Light with Use Color Temperature on hides Intensity (a
Spot Light, Range) until Show more. An `essential` flag per field in `reflect` would fix it; it isn't done here. The
Camera Filters stack is never cut (its effects keep all their fields). The guide's steps are looked up in the palette
each frame while the strip shows.

## Tests
tests/test_editor.cpp: "Inspector essentials: ...", "a number typed while dragging a gizmo axis ...".
