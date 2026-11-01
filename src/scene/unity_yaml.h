// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's YAML (round 370, task 0347): the subset Unity writes for .prefab, .unity, .mat and .meta files - documents
// headed "--- !u!<class> &<fileID>" (some "stripped"), block mappings and sequences by indentation (a sequence may sit
// at its key's own indent, as Unity writes them), flow mappings like {fileID: 123, guid: abc, type: 3}, plain and
// quoted scalars, plain scalars wrapped onto more-indented lines. No anchors beyond the headers, no tags, no merge keys.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bl {

struct YamlNode {
  enum Kind { Null, Scalar, Map, Seq } kind = Null;
  std::string value;                                    // (a scalar's text, unquoted)
  std::vector<std::pair<std::string, YamlNode>> map;    // (in the file's order)
  std::vector<YamlNode> seq;
  const YamlNode *get(const std::string &key) const;    // (nullptr when not a mapping or no such key)
  std::string str(const std::string &key, const std::string &def = "") const;
  double num(const std::string &key, double def = 0.0) const;
  /* A reference's parts: {fileID: n, guid: g, type: t}. */
  int64_t file_id(const std::string &key) const;
  std::string guid(const std::string &key) const;
};

/* One document: Unity's class number and the object's fileID from the header, its type ("GameObject"), its body (the
 * value under that type), and its text (kept for components Blendity keeps as they are). */
struct UnityDoc {
  int class_id = 0;
  int64_t file_id = 0;
  bool stripped = false;
  std::string header, type, text;  // (the "--- !u!..." line as written, its line end included)
  YamlNode body;
};

/* A Unity YAML file's documents; false with why when it isn't one ("%YAML" / "--- !u!" missing). */
bool parse_unity_yaml(const std::string &text, std::vector<UnityDoc> &out, std::string &why);
/* One block of YAML (no document headers) - a .meta's body, tests. */
YamlNode parse_yaml(const std::string &text);

}  // namespace bl
