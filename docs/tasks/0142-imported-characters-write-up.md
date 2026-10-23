# 0142: Imported characters written up

- **Status:** done
- **Requirements:** R-6, R-13
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (with tasks 0136-0141)

## Goal
The imported-characters pass (tasks 0134-0141) recorded where the next person looks: the ADR's conventions, a row per
subsystem in ARCHITECTURE, a "fixed" row per problem in USABILITY, a Learn lesson per format, and each task spec
marked done with what its review changed.

## Scope
- In: ADR 0047's Decision holds the shared conventions: one space (the X mirror, MMD's 8 cm and -Z, VRM 0.x's -Z,
  Collada's up axis and unit), materials as assets, shape keys on the mesh, clips from model files (FBX stacks, glTF
  samplers, BVH rows, Collada channels: Euler keys nearest the frame before, reduced to the frames that matter), VMD's
  conversion, and that what a format carries beyond Blendity is named in the import's notes.
- In: README's feature paragraph; ARCHITECTURE rows for `import_materials`, shape keys, `import_mmd`, `import_vmd`,
  `import_vrm`, `import_anim`, `import_bvh`, `xml.h` / `import_dae`, `ik_target_of`; USABILITY rows per format.
- In: Learn lessons: materials made for you, shape keys, MMD models, MMD motions, VRM, imported animations, BVH,
  Collada (each with a Try-it the tests run).
- In: the memory of the pass (what the user chose, the pitfalls) for the next session.

## Acceptance criteria
1. Every task 0134-0141 is marked done with its tests, fixtures and review fixes listed.
2. Every new reader has an ARCHITECTURE row, a USABILITY row, a lesson and a CHANGELOG round.

## As built
- Done as the tasks went (rounds 154-161); the CHANGELOG rounds carry each chain's numbers.
