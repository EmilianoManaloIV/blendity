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
