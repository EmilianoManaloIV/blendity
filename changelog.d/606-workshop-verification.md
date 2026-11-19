## 2026-11-19 (round 606): Workshop fixtures, rig controls and compatibility guide

**Asked:** Verify classic Source Filmmaker, Half-Life: Alyx and Counter-Strike 2 character and map imports through posing, rendering and reopening with the source detached.

**Changed (task 0588, ADR 0154)**

- Auto IK recognizes Scout's side-specific hips and the exact arm and leg naming patterns in Alyx and CS2. An alarm prop armature keeps its existing joints without gaining humanoid controls.
- Added original loose and packed Workshop layouts and checks for weighted movement, marker rigs, editable project materials and cold reopen. Added the Workshop guide and updated both book chapters.

**Checked:** Focused Windows fixture runs passed 772 initial checks and 224 in a separate process with source and cache detached, including the alarm-armature regression. Independent Linux and Windows generation produced identical bytes for all 15 fixture files. Rebuilt both manuals and inspected Workshop sections on user guide PDF page 42 and architecture PDF page 28. All three real character-and-map scenes matched their detached-source renders. The downloaded CS2 map contributed 61 mesh objects and the combined scene resolved base textures on all 75 material slots. The Windows verification also passed 905 checks across five editor processes for same-name assets, two reimports and byte-for-byte retention of edited and untouched project PNGs. The task records material coverage and outstanding checks.

<!-- docs/ARCHITECTURE.md -->
append:
## Steam Workshop verification

The Workshop fixtures exercise the Source import picker, native material and texture snapshots, skinning,
humanoid naming and source-detached rendering. The generator and evidence are recorded in
[the fixture directory](../tests/fixtures/source_workshop/README.md).
