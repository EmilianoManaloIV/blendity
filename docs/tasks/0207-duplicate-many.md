# 0207: duplicating many objects

- **Status:** done (round 227)
- **Requirements:** R-01, R-07; task 0203's rank 4
- **Decisions:** none
- **Model:** main session
- **PR:** #172 (stacked on #171)

## What was there
Duplicate on 10,000 selected cubes under one parent took 648 seconds against Blender's 1.5. `Scene::duplicate`
handled one object at a time:
- **The name:** it tried "Cube (1)", "Cube (2)" ... against every sibling. The k-th copy tried about k names over 10,000
  siblings, which is on the order of n³ string compares in all.
- **The place:** it erased the copy from the end of the list and inserted it after its original.

## What changed
- `Scene::duplicate_many` takes the whole selection.
  - The siblings' names are gathered once per parent, and each base name keeps where its count got to.
  - Each parent's list is rebuilt once, every copy right after its original.
  - The names and places come out as one at a time gave them: the smallest free number, and the latest copy of an
    object right after it.
- `Scene::duplicate` is `duplicate_many` of one. `Editor::duplicate_selected` uses it.
- `Editor::selected_objects` looks each ancestor up in a set rather than searching the selection for it, which mattered
  with a selection of thousands.

## Measured (stress `blender_parity`, Pass 15)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Duplicate 10,000 objects | 647,887 ms | 70 ms | 1,525 ms |

Of the 70 ms, 15 is the operator; the rest is the frames that show 20,000 objects and the undo step.

The same run, measured after tasks 0204-0206, also shows:
- a frame of 1,000 keyframed objects 1.7 -> 0.40 ms (Blender 3.8), from task 0206;
- select all 10,000 objects 8.1 -> 3.4 ms (Blender 32.7).

## Checked
- tests/test_editor.cpp `tests_round227`:
  - "Cube", "Cube (2)", "Cube" give "Cube (1)", "Cube (3)", "Cube (4)", each right after its original;
  - 2,000 selected come out with 2,000 new names, each copy after its original, the copies selected;
  - the same object twice puts the later copy right after it.
- The editor group passes.
- An Opus review found no fault on the editor's path. It found three orders that differed from one at a time for
  lists the editor never passes (the same object twice, a child before its parent, an original missing from its
  list), all fixed, and the selection's ancestors searched for one by one (now looked up).
