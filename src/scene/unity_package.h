// SPDX-License-Identifier: GPL-2.0-or-later
// Unity packages (round 369, task 0346): a .unitypackage is a gzip'd tar with one folder per asset, named by the
// asset's GUID, holding `asset` (the file itself), `asset.meta` (Unity's import settings, YAML, with that GUID),
// `pathname` (the asset's path in the Unity project, "Assets/...") and perhaps `preview.png`. Read here without Unity;
// the GUIDs are kept beside the unpacked files for the way back (task 0349).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl {

/* A gzip member's contents (RFC 1952: the header's optional fields skipped, the deflate stream inflated, its length
 * checked against the trailer). False with why; nothing past `limit` bytes is kept. */
bool gunzip(const std::string &gz, std::string &out, std::string &why, size_t limit = (size_t)3 << 30);

/* A tar's files and folders (POSIX ustar with its prefix field, GNU long names, pax `path`); links and devices are
 * skipped. False with why for a truncated or malformed archive. */
struct TarEntry {
  std::string name, data;
  bool dir = false;
};
bool read_tar(const std::string &tar, std::vector<TarEntry> &out, std::string &why);

/* One asset of a package: its GUID, its path in the Unity project, its .meta text and its bytes (a folder has none). */
struct UnityAsset {
  std::string guid, pathname, meta, data;
  bool has_data = false, folder = false, duplicate = false;
};
/* A .unitypackage's assets, in the archive's order (an entry without a pathname is left out). `bytes` is used up
 * (released once inflated: a package is held once, not three times - review 369). An asset whose GUID folder comes
 * twice is marked `duplicate`. `cancel`, when set, stops the walk. */
bool read_unity_package(std::string &bytes, std::vector<UnityAsset> &out, std::string &why, const std::atomic<bool> *cancel = nullptr);

/* Where a package's asset goes: its Unity path under `into` ("Assets/Imported/<package>"), Unity's leading "Assets/"
 * dropped. "" for a path that would leave `into` - "..", an absolute path, a drive, a backslash trick - or is empty,
 * isn't UTF-8, holds a control character, or names a Windows device (CON, NUL, COM1...) or ends a part in a dot or a
 * space (Windows would write another file) - review 369. */
std::string unity_package_target(const std::string &pathname, const std::string &into);
/* The GUID a .meta names ("guid: 0123..."), "" when none. */
std::string meta_guid(const std::string &meta);
/* --- writing (round 372, task 0349: the way back to Unity) --------------------------------------------------- */
uint32_t crc32(const std::string &data, uint32_t crc = 0);  // (crc: the running value, for data in parts)
/* A gzip member (RFC 1952) written as its data comes, a deflate block a megabyte: LZ77 matches coded with the fixed
 * Huffman tables, or the block stored when that's fewer bits (an already-compressed PNG). One bit stream across the
 * blocks (review 372: a block padded to a byte broke every reader past a megabyte). */
class GzipWriter {
 public:
  GzipWriter();
  ~GzipWriter();
  void write(const std::string &data);
  void finish();                     // (the last block and the trailer: CRC-32 and length)
  std::string &output();             // (what's written so far; the caller may take it as it grows)
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::string gzip(const std::string &data);
/* A ustar entry (a long name split into prefix and name; past 255 bytes a GNU long-name entry first). */
void tar_append(std::string &tar, const std::string &name, const std::string &data, bool dir = false);
/* One asset's tar entries: its GUID's folder, asset (unless a folder), asset.meta, pathname. */
std::string unity_asset_entries(const UnityAsset &a);
/* A .unitypackage of `assets`. */
std::string write_unity_package(const std::vector<UnityAsset> &assets);
/* A GUID for a path that has none: the same every time for the same package and path (review 372: a new one each
 * export made Unity see another asset at the same place), 32 hex digits. */
std::string stable_unity_guid(const std::string &package, const std::string &pathname);

/* A package file's name as a folder name: path characters, leading and trailing dots and spaces and device names
 * replaced; "Package" when nothing is left (review 369: "...unitypackage" made the folder "..", Assets itself). */
std::string unity_package_folder(const std::string &file_stem);
/* The same path as Windows sees it, for spotting two that are one file: lower case, trailing dots and spaces gone. */
std::string unity_path_key(const std::string &path);
/* Whether `s` is UTF-8 (no overlong forms, no surrogates). */
bool valid_utf8(const std::string &s);

}  // namespace bl
