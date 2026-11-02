# Task 0346: Unity packages unpacked into the project (Phase E, part 1)

- **Status:** done (round 369, 2026-10-31)
- **Asked:** "Is it possible to import Unity packages and prefab packs related to VRChat into Blendity (Mesh, Materials,
  Lights, no-code, etc.) So I can work on VRChat worlds & avatars & games using a game engine as a refinement and
  finishing touch phase?" The user chose: code stays in the engines, kept intact.
- **Plan:** Phase E's first round, E1. Prefabs and scenes (with the YAML reader) are next (task 0347), then materials
  and VRChat's components (0348), then the way back (0349).

## What
- **`scene/unity_package`:**
  - `gunzip`: RFC 1952's header fields skipped, the deflate stream inflated through the PNG reader's inflate, its length
    checked against the trailer, a size cap.
  - `read_tar`: ustar with its prefix, GNU long names, pax `path`; links and devices skipped.
  - `read_unity_package`: GUID folders with `asset`, `asset.meta` and `pathname`; folders from `folderAsset: yes`.
  - `unity_package_target`: Unity's path under Assets/Imported/<package>, "Assets/" dropped. A path with "..", an
    absolute path, a drive or a backslash escape is refused (zip-slip).
  - `meta_guid`.
- **`editor/unity_package_ui.cpp`:**
  - `PackageJob` reads, unpacks and writes on a worker, touching nothing of the editor. Headless runs do it at once.
  - The job tray shows "Unpacking <file>" with its progress and Cancel.
  - Once done, the Project window lists it and the Console gives the counts: models, textures, scripts and shaders kept
    as they are, prefabs / scenes / materials, other, folders. What was left out is named.
  - The GUIDs go in `.unity-guids` (guid, Unity path, project path), hidden from the Project window.
- **Entry points:** File > Import Unity Package..., a drop on the window, `import <file>.unitypackage`.

## Checked
- **`tests_round369`:**
  - gzip with a name field;
  - tar with ustar's prefix and a GNU long name;
  - a cut gzip and a zip refused;
  - .meta GUIDs;
  - package paths kept inside (seven escapes refused);
  - a built package unpacked: a folder, a model, a texture, a script byte for byte, the GUID map; two escaping
    entries left out and not written; the model imported;
  - a non-package refused;
  - the Try-it.

## The Opus review's fixes (packages come from the internet: the attack cases)
- **Majors:**
  - **A pathname that isn't UTF-8 threw on the worker** and closed Blendity. Such paths are now refused, and the
    worker catches anything, ending the job with the reason.
  - **The size limit didn't hold.** The trailer's size is the archive's word, and inflating had no cap, so a few MB
    could inflate to gigabytes or wrap past 4 GB. `zlib_inflate` now takes an output cap and fills up to it before
    refusing; memory is reserved from the compressed size; every allocation is inside the guard.
- **Minors:**
  - **Memory:** the package is held once (the file's bytes released once inflated, the tar once read, each entry
    copied once, each asset's bytes released once written).
  - **Cancel:** checked after the walk too, and the message names where the files unpacked so far are.
  - **Quitting mid-unpack** stops the job at its next file and waits for it.
  - **.unity-guids:** a line is added only for what was written. GUIDs must be 32 hex digits, and control characters
    are refused in paths.
  - **Collisions:** the same GUID twice is left out whole. Two paths that are one file on Windows (case, trailing
    dots) keep the first and leave out the second, named.
  - **Windows device names** (CON, NUL, COM1...) and parts ending in a dot or a space are refused.
  - **The folder's name** from the file can't be "..", dots, spaces or a device; "Package" when nothing's left.
  - **The File menu's tooltips** are on the right items.
  - **The escape test** checks where a careless join would land.
  - **`import x.unitypackage pbr`** goes to the package, not the shading import.
  - **A .meta without its file** makes nothing (it made a folder named like the file).
  - **`.asset` files** count as other, not scripts.
- **Tests:** not UTF-8, a tab, NUL / AUX / COM1, "x.", "x /", a Unicode name kept, the folder name's cases, the path
  key, a lying trailer stopped at the cap, the case collision and the GUID twice in a package.

## Not done
- **Prefabs, scenes, .mat files and VRChat components:** tasks 0347-0348.
- **PSD and TIFF textures** are kept but not read (the image loader has no PSD).
- **Re-importing over an earlier unpacking** overwrites its files without asking. Unity asks per file.
- **Cancelling during the inflate itself:** the cancel is seen once the archive is read. A cap keeps that bounded.
- **A cancelled job's files** stay (named in the Console) rather than being removed.
