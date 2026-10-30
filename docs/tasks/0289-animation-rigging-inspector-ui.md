# Task 0289 - Animation, rigging and Inspector verbs without the console (round 309)

Part of the console-only-to-UI plan (tasks 0271-0273, 0286-0290). Every control calls the code the console calls.

## What
| Verb | Now reached by |
| --- | --- |
| `autopose lock / unlock <bone>` | Animation > Puppet > Lock the Selected Bone (checked while locked) |
| `body contacts find / pin / clear` | Puppet > Contacts > Find, Pin, Clear |
| `body contacts add <bone> <from> <to>` | Puppet > Contacts > Add One Here (the selected bone, the playhead and 9 frames after) |
| `body contacts remove <bone> <frame>` | Puppet > Contacts > Remove the One Here |
| `body ragdoll hit / limp / strong / clear` | Puppet > Ragdoll Events (where Jolt is built in): a 30 N s hit on the selected bone from five sides, Go Limp Here, Get Strong Here, Clear Events |
| `body ragdoll show <strength>` | Puppet > Ragdoll Events > Simulate, the Motors at 25 / 50 / 75 / 100 % |
| `walk save / load <file>` | the Rigging window's Walk Cycle: Save Style (Assets/Gaits/<name>.gait, a free name) and Load Style... (the project's .gait files) |
| a Vertex Group / Thickness Group typed | the Inspector: a dropdown of the mesh's own groups, "(the whole mesh)" first; a mesh without groups keeps the typed field |
| `shape add / remove <name>` | the Inspector, MeshRenderer: Add Shape Key (Key 1, Key 2...) and Remove... (the mesh's keys) |

Tooltips that sent the artist to `autopose lock`, `body contacts add` and the ragdoll's console events now name the
Puppet menu.

Left for round 310 or later, noted in its write-up: the tangent menu's slope fields, Hair Rig's locks and bones fields,
the Anchor layer dropdown, the Sequencer trigger key capture and the Wheel editor's Add through the palette.

## Acceptance
- `tests_round309` (tests/test_animation.cpp) by real clicks on a Mixamo-named synthetic person:
  - a foot locked and unlocked;
  - a contact added at frame 12 to 21 on the foot, removed, and another cleared;
  - a limp event where Jolt is built in;
  - a style saved and loaded back from the list;
  - a Smooth modifier's Vertex Group picked from the cube's groups;
  - a shape key added and removed.
- The whole suite in the dependency-free non-LTCG build; the chain on main.
