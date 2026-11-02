# Task 0349: the way back to Unity - Export Unity Package (Phase E, part 4)

- **Status:** done (round 372, 2026-11-01)
- **Asked:** Phase E: "using a game engine as a refinement and finishing touch phase" with code kept in the engines -
  so what's done in Blendity goes back to the Unity project, every reference intact.

## What
- **`scene/unity_package` writing:**
  - `crc32`, running over data in parts.
  - **`GzipWriter`:** a gzip member written as data comes.
    - One-megabyte deflate blocks in one bit stream. Each block is LZ77 through a 3-byte hash (up to 32 tries a
      position) coded with the fixed Huffman tables, or stored when that's fewer bits.
    - The trailer holds the CRC-32 and length. `gzip` is one call.
  - `tar_append`: ustar with the prefix for long names and a GNU long-name entry past that.
  - `write_unity_package`: each GUID's folder with asset (unless a folder), asset.meta and pathname.
  - `stable_unity_guid`: a path's GUID, the same every export (FNV-1a of the package and the path, twice).
- **`UnityOrigin`:** each object imported from a prefab or scene (task 0347) records its file and its GameObject's
  and Transform's fileIDs. Objects of nested prefabs are marked nested.
- **`patch_unity_objects`:** documents by their headers. A GameObject's `m_Name` and `m_IsActive` lines and a
  Transform's `m_LocalRotation`, `m_LocalPosition` and `m_LocalScale` lines are replaced, with each line's own end kept
  (CRLF). Every other byte stays, and stripped documents aren't touched. Names are written as Unity writes them
  (`unity_yaml_string`: plain, or double-quoted with \uXXXX and surrogate pairs). Only lines whose values differ from
  the document's are written, floats in their shortest exact text, and the Euler hint with a turned rotation.
- **`Editor::export_unity_package(folder, out)`** - a job (the tray, Cancel; at once headless):
  - **The walk:** every file and folder under the package's folder (dot files - .unity-guids, .unity-meta - left out).
  - **Known paths** (from .unity-guids) keep their GUID, Unity path and kept .meta.
  - **New ones** get their path's stable GUID, a path under Assets, and a minimal .meta (Unity fills in the importer).
  - **The prefabs and scenes** are patched with the scene's edits of the objects imported from them. Nested ones are
    counted and left out, and so are copies: a duplicate carries its original's UnityOrigin, and the first wins.
  - **The worker** reads, patches and compresses one file at a time, then writes the output whole, or nothing on
    Cancel. An unreadable file is left out and named.
  - **The output:** written to Exports/<folder>.unitypackage, or where asked. It's refused inside the package's own
    folder or over a file of another kind. The project's files are never written over.
- **Entry points:**
  - **File > Export Unity Package...**, or `export unitypackage` with no folder: the selected object's package,
    found through its UnityOrigin, a parent's, or one below it (an import's root).
  - **`export unitypackage <folder> [out]`**: both may hold spaces; the folder is the longest part that is one.

## The Opus review's fixes
- **Blocker:** every block but the last was padded to a byte, which corrupts a deflate stream for every standard
  reader past a megabyte. One bit stream now runs across the blocks (stored blocks align only after their own
  header). 3 MB compressible and mixed inputs are checked by Blendity's strict reader, and once by Python's gzip
  (opt-in: BLENDITY_DUMP_GZIP).
- **Majors:**
  - **The export stalled the editor and held the package three times.** It's now a job: a file at a time,
    compressed as it goes.
  - **A duplicate's edits overwrote the original's.** The first object per fileID wins, and copies are counted.
  - **New files got a new random GUID each export.** A path's GUID is now stable.
- **Minors:**
  - **Untouched objects were rewritten with float noise.** Only differing values are written, in their shortest exact
    text.
  - **The menu refused right after an import** (the root was selected): it now looks below the selection too.
  - **A name ending in ':'** is quoted.
  - **The console takes the longest folder.**
  - **The output** isn't allowed into the package's folder or over another kind of file.
  - **An unreadable file is named**, not packed empty.
  - **The CRC table** is made thread-safely.
  - **The Euler hint** is written with a turned rotation.
- **Tests:**
  - multi-block streams;
  - stable GUIDs;
  - an untouched rotation and scale left as written;
  - a duplicate left out;
  - the selection's export;
  - cleanup on every way out.

## Checked
- **`tests_round372`:**
  - **Archive writing:**
    - 127 KB of Unity YAML gzips to 8 KB and back;
    - 70 KB of random bytes are stored (28 bytes more) and back;
    - an empty file round-trips;
    - CRC-32 of "123456789" is CBF43926;
    - a 300-byte tar name and a folder entry read back;
    - new GUIDs are 32 hex digits and differ.
  - **Unity's quoting:** "Cube (1)" plain; "a: b" and "- x" quoted; 看板 as \u770B\u677F; an emoji as a surrogate
    pair; a quoted name read back.
  - **The round trip:**
    - a package (a folder, a CRLF prefab with a MonoBehaviour, a script) imported, the prefab brought in;
    - its object moved to (1.5, 0.25, -3) and renamed 看板, and a file added;
    - exported: the folder's, prefab's and script's GUIDs and .meta files the same;
    - the name and position lines replaced with CRLF kept, the MonoBehaviour's document byte for byte;
    - the new file under a new GUID with its .meta;
    - the project's prefab file unchanged.
  - **The Try-it.**

## Not done
- **Objects added or deleted in Blendity** going into the prefabs (new GameObjects with their meshes: an FBX per mesh,
  and the fileIDs Unity would give its parts).
- **Edits to components** (a light's colour, a material's values) going back: the transform, name and active state do.
- **Renaming or moving a file inside the package's folder** gives it a new GUID, and the objects imported from it lose
  their way back. Moving an object to another parent writes its local transform under the old parent.
- **Real Unity:** the exported package hasn't been opened in Unity here. The format follows Unity's (and the reader
  that reads Unity's own packages), with no Unity in the test environment.
- **Godot, Blender and Unreal:** tasks 0350-0351.
