# 0588 Workshop fixtures, guides and end-to-end verification

- **Status:** reviewed, awaiting integration
- **Requirements:** R-6, [pass 0582](0582-workshop-import.md)
- **Decisions:** [ADR 0154](../decisions/0154-steam-workshop-import.md)
- **Implementation:** dedicated workstream, then independent testing and review
- **PR:** pending

## Goal and ownership
Own redistributable cross-title fixtures, provenance, compatibility matrix, user guide and compiled book verification.

Owned paths or surfaces: `tests/fixtures/source_workshop/`, `tests/test_source_workshop.cpp`,
`src/scene/ik.cpp` for imported skeleton name matching, user and architecture book chapters,
and `docs/guides/workshop.md`. Coordinate registration and shared interface changes through the mounting round.

## Acceptance and verification
- Give classic Source Filmmaker, Half-Life: Alyx and Counter-Strike 2 separate rows and item layouts. In each row,
  select a character and a map from the downloaded layout and import both into one scratch project. Check the rest
  mesh and bind pose, orientation and scale, and the placements of repeated map props.
- Check character, world and prop material slots, project-owned files, UV orientation, base color, cutout alpha,
  and the supported additional channels against the fixture. Select the same asset again and check that an
  edited material is preserved. Check that distinct source textures with the same basename land as distinct
  project assets and that missing or unsupported dependencies are reported by virtual path.
- Pose the imported skeleton by moving its bones and verify weighted deformation and undo.
- Add controls with the editor's **Auto IK on Selected Skeleton** command using the imported bone spellings.
  Move the target on each arm and leg independently. Check the affected weighted vertices, a stationary reference,
  and undo. Save the posed scene, move its source copy away, clear source and image caches by reopening in a fresh
  editor, and check that the skin, controls, pose, map geometry, textures and render survive.
- Use the Humanoid marker Auto-Rig workflow on a fresh unskinned copy of the imported rest mesh. Check that it
  creates weights, a control moves weighted geometry, undo restores the pre-operation state, and reopening keeps
  the controls and deformation. Confirm that an ordinary prop and a prop with a two-bone armature acquire no
  humanoid controls.
- Render the posed character in the textured map before moving source material and the import cache. Reopen and
  render again in a separate editor process. Check the character and world are both present and the pictures agree.
- Record the installed game build, Workshop item ID and revision, chosen virtual paths, file or manifest hashes,
  renderer and results for real items. Keep redistributable fixture results separate. Mark every unavailable title
  or step pending.
- Rebuild both affected books and inspect the changed pages. Derive independent tests from these criteria. Use
  scratch projects, run the Windows and Linux suites and relevant import/editor stress checks, and record exact
  commands and results. Cite repository revisions, primary format references and fixture provenance.

## Documentation
Own `changelog.d/606-workshop-verification.md`. Include verified outcomes and applicable shared-document sections under ADR 0123. Supply user-facing changes to the Learn lesson and guide owners. Mark this spec done with test, review and PR evidence at closeout.

## Verification record

- Original fixture generation completed on Windows with
  `py tests/fixtures/source_workshop/generate.py <scratch-directory>`. The regenerated files match the tracked
  layout byte for byte by relative path and SHA-256. The generator fixes the embedded ZIP creator metadata
  to the Windows value for portable output. The task started on Blendity revision `f83be355`. Independent
  checks confirmed byte layouts and texture samples with a separate parser and ValveResourceFormat 21.1.7798.
  See [fixture provenance](../../tests/fixtures/source_workshop/README.md) for the results and sources.
- `git diff --check`, `py tools/make_books.py --check-cited` and `py tools/dev/changelog.py check` passed
  on the implementation worktree. The full Windows and Linux suites and import/editor stress runs remain
  pending. Independent Linux regeneration matched all 15 fixture files, including the classic BSP SHA-256 `58fdb01f75c592f37db0f89f6638faacb546d244efbe756735107f5c5aa3bd53`. The native Linux focused compile stopped in the editor sources when WSL reported `Wsl/Service/E_UNEXPECTED`. The Hub received local verification branch `codex/0588-hosted-linux` at `9390f3010f9b0c77c71166e567f623d588b77a34`, its verified incremental Git bundle and the two-process invocation below. The [hosted build](https://github.com/EmilianoManaloIV/blendity/actions/runs/44599787546) stopped before test execution at `src/platform/platform_x11.cpp:10` because the runner lacked `X11/XKBlib.h`. A [second baseline dispatch](https://github.com/EmilianoManaloIV/blendity/actions/runs/44607511557) stopped at the test-shard self-check before compilation. The Hub retired that baseline. The identity-aware diagnostics install `libx11-dev` and the other Linux CI prerequisites before compilation.
- For the fixture cold-run, run `blendity_tests --group source_workshop` with a new `BLENDITY_TEST_DIR`.
  It creates the scratch projects, saves `before.png` for each title, moves the installed copy and the conversion
  cache aside, and performs an in-process sanity reopen. Run the same executable in a second process with the same
  `BLENDITY_TEST_DIR`, `BLENDITY_WORKSHOP_COLD_REOPEN=1` and
  `BLENDITY_TEST_FILTER=detached reopen`. The second process reopens each scene from its native files and compares
  its CPU raster render to the first process. The Windows release libraries snapshot passed 772 checks in the first process and 224 in the cold process on 2026-11-19. Scratch logs: `C:/blab/0588-verification/fixtures/fourlimbs/fixture-all.log` and `cold-all.log`. The earlier scratch tree also contained an obsolete single-file CS2 VPK. An exact clean fixture inventory removed that competing provider.

### Reimport identity verification

The joined product snapshot is `12b708b2a6e4b1c02104a31a2f91df62a99f7855`. The diagnostic
revision `26e642229b8b4f22079bce79cf31c7558a09e073` adds the test group, its shard registration,
two generated input revisions, and the clean Windows/Linux verification commands. Both the standalone product
and diagnostic group tables passed `py tools/dev/test_shards.py --self-test`. The Windows diagnostic uses MSVC
14.51 and the Blender libraries. The hosted Ubuntu 24.04 job builds the dependency-free editor and test binary.
Each platform starts from an empty object directory. The [final hosted verification run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44608047654) publishes individual group logs and the five-process record as a downloadable artifact.

The identity probe begins with a floor and two plinths whose materials and textures share the basename
`enamel` under different virtual paths. Their initial texture bytes are identical. The tester edits both
native material roughness values and the floor PNG. The second source revision reverses floor/plinth encounter
order and changes both source textures. Five editor processes check the initial import, offline reopen,
two explicit reimports after relocating the input and cache, and the final offline scene. The archive keeps
its basename during relocation. The test compares the complete bytes of both project PNGs (one edited, one
untouched), native material contents, renderer pixels and each old/new placement's assignments. It also checks
that the native asset inventory stays fixed. The importer may add provider and fingerprint records to its
existing metadata directory when a source moves.

Windows passed all 5,255 registered-group checks, including the separate cold process, and 905 identity
checks (114, 84, 238, 313 and 156). Each process exited zero. The initial six group totals were 899 for
`source_content`, 528 for `source1_model`, 1,846 for `source1_map`, 436 for `source2_model`, 550 for
`source2_map` and 772 for `source_workshop`, followed by 224 cold checks. The five-process probe preserved
every native PNG byte and both material edits. See `C:/blab/0588-verification/identity-verified-win/summary.json`
and `phases.log`. The same product sources also passed 224 cold checks against the earlier Windows fixture
projects, copied without their source or import cache.

A separate diagnostic renamed the anonymous VPK while moving it. The importer treated that renamed archive as
a different source and created a second snapshot. Both Windows and [hosted Ubuntu](https://github.com/EmilianoManaloIV/blendity/actions/runs/44607244854) detected this at the second reimport. All six registered import groups and the separate cold process passed on that earlier diagnostic revision. The standalone VPK matcher includes its provider basename.
The tested relocation guarantee applies when preserving the package name, or when a verified Workshop item
identity is available as covered by the content provider tests.

### Independent review

The first read-only review found that a prop with `Alarm_Upper_L` and `Alarm_Lower_L` could acquire unwanted
hand controls. The implementer narrowed the new Source patterns and added an editor-command regression
that checks serialized scene equality. The tester verified the regression and cross-platform fixture identity.
The second read-only review approved the 0588 candidate after the 712 initial and 224 cold checks passed. A subsequent independent review approved the four-target marker extension. Each hand and foot moved a vertex weighted more than 50% to the matching lower and tip joints by over 2.5 cm. Each move passed whole-mesh undo and redo comparisons, and the combined pose survived reopen (772 initial and 224 cold checks).
The Hub owns publication and full-stack verification.

### Compiled manuals

On Windows, `py tools/make_books.py --pdf --wsl` completed for both books. The user guide has 76 PDF
pages. Its new section 9.3 appears on PDF page 42 (printed page 36). The architecture manual has 69 PDF
pages. Its section 7.4 appears on PDF page 28 (printed page 22). Poppler rendered both pages at 1150 pixels
on the long edge; visual inspection confirmed readable headings, paths, menus, cross-references and
paragraphs within the margins.

### Real-source evidence

| Title | Source inspected | Evidence and outstanding verification |
| --- | --- | --- |
| Classic SFM | Scout, a raw MDL v49 from [the pinned reproducer repository](https://github.com/gkjohnson/source-engine-model-loader-models/tree/506af1ce5e6054dc70973e85af1b4f5c08add1c8). `maps/sfm_thewarehouse.bsp` from [SFM - The Warehouse, Workshop 791304326](https://steamcommunity.com/sharedfiles/filedetails/?id=791304326). | Scout MDL SHA-256 `fe6a8fb04e648b4d3e8515bef9ecffb17cb558f05a529aaeb3d59a24bb70b17a`. Its 78 bones include bilateral `bip_upperArm → bip_lowerArm → bip_hand` and `bip_hip → bip_knee → bip_foot` chains. Warehouse SHA-256 `f86d0596fe486e684fd12447800603d4ed8476aef181a206d6418eb9c3fa706a` with 47 static prop records and seven distinct referenced models. The real combined scene added 48 map mesh objects. With Scout it has 49 meshes, 45,190 faces and 80 project material slots, 45 with resolved base textures. Turning each hand and foot target independently moved 50, 50, 148 and 148 weighted vertices by over 1 mm. The final 50 checks and 10 cold-process checks passed, including separate character and map visibility and a matched CPU render after detaching source and cache. The raw Scout VVD spans 31.6 × 81.7 × 22.2 inches in Source XYZ. Its long axis is Y, so the default Source Z-up conversion leaves this model sideways in the reference render. Upright default placement for this sample needs separate reader verification. The SFM game installation was unavailable. |
| Half-Life: Alyx | `models/alyxmp/avatar.vmdl_c` from [AlyxMP revision 8468542](https://github.com/Requiem-Software/AlyxMP/blob/84685425f2c1771995c7f269fe1e1c0852a72d5f/mod/game/hlvr/models/alyxmp/avatar.vmdl_c) and the `bad-kitty.vpk` map from [revision b58c157](https://github.com/thenayr/bad-kitty-half-life-alyx/tree/b58c1570570a16a7469153ab09da1b972359471d). | Avatar SHA-256 `64f7c69ed8ca2f9c667786f2594936bd7146c7a3abd3fe287c25316122f29110`. ValveResourceFormat 21.1 reads 88 joints and both arm and leg chains. The real combined scene added 87 map mesh objects. With the avatar it has 88 meshes, 133,374 faces and 219 project material slots. The four targets moved 171, 162, 479 and 549 weighted vertices by over 1 mm. The final 50 checks and 10 cold-process checks passed, including character and map visibility and a matched CPU render. Zero slots resolved a base texture without the base game. These author-hosted samples establish geometry and posing. Steam delivery remains unverified. A separate real ApertureVR foliage model imported its 2048 × 2048 diffuse texture, alpha range 0–255 and 0.305 cutoff, and rendered identically on detached reopen. |
| Counter-Strike 2 | App 730 build 26445667, `agents/models/ctm_sas/ctm_sas.vmdl_c` from the installed game. Downloaded map [crashz' Crosshair Generator v4, Workshop 3070193546](https://steamcommunity.com/sharedfiles/filedetails/?id=3070193546). | SAS SHA-256 `7312ab88d05bea67b06f3a945d06dc1e5fc5511d4849bc05bc05aec25380eb22`. Its KV3 v5 DATA has all eight limb names. The four targets moved 124, 131, 453 and 449 weighted vertices by over 1 mm. All five native material slots resolved base textures after the compressed mip reader correction. The downloaded multipart VPK contains `maps/xhair_v4.vpk`. Steam manifest `8869814522091201787` records 75,388,535 bytes. Outer directory SHA-256 `0d2b75b6fe2f41c924bf533f5148525fb004dc0d8659fd6db8ca1addb9096de5`, chunk `e312d5d5fc6d6e651987f8e8eb575de1079b60f00ba636165c6c6da8cc23b040`, nested map package `86fe185e903d5b10589d13305779f54d67ce3a53c9e4be9e7ecf24629e7bf84c`. The first reader rejected full-width visibility masks. After correction, the importer followed `maps/xhair_v4.vmap_c` to `maps/xhair_v4/world.vwrld_c` and added 61 map mesh objects. The combined scene held 63 meshes, 20,585 faces, 94 bones and 75 project material slots with 75 resolved base textures. The separately hidden actor and map changed the image, and the cold reopen matched the reference render (51 initial and 10 cold checks). Four baked map lightmaps were reported as omitted. |

The final real-file Windows logs are under `C:/blab/0588-verification/real-identity/<title>/`. Each
`make-<title>.log` records limb motion, materials, object counts and CPU raster checks. Each
`reopen-<title>.log` records a separate process after relocating the game/Workshop inputs, import cache
and preferences. Capture resolution was 192 × 128 with 2× SSAA. The before and after PNGs were byte-identical for each scene (SHA-256 prefixes SFM `f6ffc76cb3ee`, Alyx `1b9bc2c19955` and CS2 `108ca0240998`). The initial failing CS2 probe remains
under `real/cs2/probe.log`.

For each real character, a fresh unskinned copy of the largest mesh received eight markers. Scout's copy
was placed upright before marking. Automatic Weights produced 17 groups for Scout (5,241 vertices),
AlyxMP (13,525) and SAS (9,899). On these meshes the background weight solver creates its own undo step.
Undoing weights and then rig creation restored the unskinned mesh. Redo restored binding. All four controls
moved evaluated geometry and the accumulated pose survived reopening each marker scene.

The standalone real cutout tests each passed 10 initial and 11 detached checks. The ApertureVR foliage
(`models/props_foliage/bush01.vmdl_c` at [revision 9d04243](https://github.com/krotter0/aperturevr-source/tree/9d04243bf584703147e9c4030f8e1bec751b2da5)) retained a 2048 × 2048 texture and 0.305 cutoff. CS2 SWAT hair
retained a 512 × 512 texture and 0.072 cutoff. Both project textures decoded with alpha values spanning
0–255. Logs are in `C:/blab/0588-verification/cutout-alyx-identity/` and `cutout-cs2-identity/`.
