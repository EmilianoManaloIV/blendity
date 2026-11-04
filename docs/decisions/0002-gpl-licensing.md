# 0002: GPL licensing (source GPL-2.0-or-later, binaries GPL-3.0-or-later)

- **Status:** accepted (recorded 2026-10-09; decided in phase 2, 2026-10-06)
- **Requirements:** C-03

## Context
Blendity follows Blender's design closely, and Blender is GPL-2.0-or-later. It compiles in code from
Blender's tree (MikkTSpace under Apache-2.0, ufbx and fast_float under MIT, the Hosek-Wilkie sky under
BSD-3-Clause) and optionally links Blender's prebuilt libraries (ADR 0001), several of them Apache-2.0.
Apache-2.0 is compatible with GPL-3 but not with GPL-2-only.

## Decision
- Blendity's own source is GPL-2.0-or-later, like Blender; every source file carries
  `// SPDX-License-Identifier: GPL-2.0-or-later`.
- The executables, which combine that source with Apache-2.0 code, are distributed under
  GPL-3.0-or-later.
- Third-party notices, versions and the reasoning live in `licenses/README.md`; any new copied code or
  library adds its notice there in the same change.

## Consequences
- Anything distributed with Blendity must be GPL-3-compatible; libraries with incompatible licenses are
  ruled out even when Blender could use them.
- Reviewers check new files for the SPDX header and that `licenses/` is updated with any new third-party
  code.

## Amendment (round 409, task 0386, ADR 0104)
- Code **ported** from an Apache-2.0 source - the Cycles sky radiance evaluation in `src/render/sky.cpp` - keeps the
  origin's copyright holder and notice word for word in its header, whose SPDX line names both licences
  (`GPL-2.0-or-later AND Apache-2.0`); the file is distributed, like every executable, under GPL-3.0-or-later through
  the "or later" clause. `licenses/README.md` lists it under "ported, not
  copied".
- Code **read, not copied** (lilToon's formulas, Poiyomi's and MToon's property names, the CATS plugin's tools,
  Cascadeur's ideas) is credited in `CREDITS.md` and `licenses/README.md` with the original's licence; no licence text
  ships, since no code is in the tree.
- **Authorship:** copyright Emiliano Manalo and contributors; the code is written with Anthropic's Claude under human
  direction and review, the model's outputs belonging to the user who directed them under Anthropic's terms, licensed
  GPL-2.0-or-later to the extent copyright subsists in them (the human selection, direction, review and editing); the
  `Co-Authored-By` trailer is attribution, not a licence grant.
