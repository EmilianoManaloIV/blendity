# 0136: MMD models (PMX and PMD)

- **Status:** done
- **Requirements:** R-6
- **Decisions:** ADR 0047
- **Model:** main session (Opus)
- **PR:** (stacked on tasks 0134-0135)

## Goal
MikuMikuDance characters open in Blendity ready to pose and animate: skinned, faced the same way as every other
character, toon-shaded with their outline, their faces as shape keys, their legs with IK, and their hair and skirts
as spring chains.

## Scope
- In: own readers for PMX 2.0 / 2.1 (UTF-16 or UTF-8 text, 1/2/4-byte indices) and PMD (Shift-JIS). No library.
- In: Shift-JIS through the platform (Windows' code page 932, iconv elsewhere): `core/text_encoding`.
- In: space: MMD's 8 cm unit, turned half a turn about Y so the character faces +Z (a rotation: faces keep their
  order; the file's own normals decide when a file is mirrored).
- In: the skin:
  - BDEF1/2/4 weights; SDEF read as BDEF2, QDEF as BDEF4;
  - bones as nodes, each turned so +Y points at its tail (the IK chain's first link at its effector);
  - the standard bone names in the words Blendity's rig tools read (`左ひざ` -> `LeftLeg`), others kept.
- In: vertex morphs as shape keys; group morphs as the weighted sum of their members.
- In: materials:
  - Toon shading, lit colour diffuse + ambient;
  - the texture, found as any import's;
  - Cutout when the image has alpha;
  - the toon ramp's bottom as the shadow colour;
  - double-sided by its flag.
- In: an Outline from the widest edge the materials ask for (MMD's edge is per material).
- In: IK on the chain's first link, as long as the chain, with a pole in front of a knee.
- In: rigid bodies MMD simulates: their bones as Spring Bones chains (label by name, radius from the body's size).
- Out: bone, UV, material, flip and impulse morphs (reported); inherited rotation (twist, eyes: reported); MMD's joints
  and rigid-body collisions (the springs' own collisions stand in); sphere maps.

## Acceptance criteria
1. A synthetic PMX (UTF-16 and UTF-8) imports placed exactly, the thigh's +Y down its length, the knee's IK on
   `LeftFootIK` with a pole in front, the hair a spring chain of the body's radius.
2. Its front face faces +Z; turning the knee moves the vertices it carries.
3. Its morphs lift 8 cm and half that; its material is Toon with MMD's lit colour, its texture, its ramp's shadow; its
   outline is the edge size's thickness.
4. The same character as PMD (Shift-JIS) imports the same.
5. 240 damaged files import or refuse without harm (sanitizers clean).

## As built
- `scene/import_mmd.cpp` (`scene/mmd_binary.h`: the bounded reader, shared with VMD); `core/text_encoding.{h,cpp}`;
  `ImportedIK`, `ImportedSpring`, `ImportedOutline` added to `ImportResult` and made by `instantiate_import`.
- Tests: round 156 (the PMX and PMD writers are in the tests: no third-party model).
- After the review:
  - IK targets and poles are looked for inside the constraint's own character first (`ik_target_of`; two copies of a
    character share names);
  - "D bones" (`足D`, `ひざD`, `足首D`: bones that copy another's whole turn from the same place, which the skin is
    weighted to) hand their weights to the bone they copy, so IK and walking move the skin;
  - bounded: at most 4,096 shape keys within 512 MB, bones at most 65,535 and nested at most 512 deep (a loop is
    broken), positions past a million units refused; vertex groups only for bones that carry vertices; groups and
    keys moved (not copied) through the weld;
  - hard edges kept: vertices weld only with the same normal;
  - a fork in dynamic bones starts a chain per branch;
  - a pole only at knees (`ひざ`, or mapped to a leg);
  - MMD's shared toon ramps (`toon01-10.bmp`) are looked for beside the model and reported when absent;
  - image headers read in 32 bytes, once per path; toon ramps decoded once;
  - names made unique within the import; relative texture paths resolved beside the model; SDEF/QDEF, sphere maps,
    joints and flip/impulse morphs reported;
  - English physics names by whole words (`ponytail` is hair).
  - Still out: the inherited partial turns (twist helpers), MMD's joints and rigid-body collisions.
