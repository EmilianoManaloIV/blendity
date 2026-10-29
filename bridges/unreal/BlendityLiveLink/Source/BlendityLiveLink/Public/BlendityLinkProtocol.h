// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity Live Link for Unreal (task 0277): the protocol without Unreal - the framing, a JSON reader, the glTF binary
// reader and the axes - in standard C++17, so Blendity's own test suite compiles and replays it on every build
// (tests/test_unreal_protocol.cpp) and the Unreal module only turns its results into actors and meshes.
//
// Axes: Live Link sends glTF's (right-handed, +Y up, metres; an object's front is +Z). Unreal is left-handed, +Z up,
// +X forward, centimetres: (x, y, z) in glTF is (100 z, -100 x, 100 y) in Unreal - the front to +X, the asset's right
// (-X in glTF) to +Y, up to +Z. That map is a mirror (determinant -1), so a rotation's axis goes the same way with its
// sign flipped (q' = (-qz, qx, -qy, qw)), scale follows the axes, and a triangle's winding is reversed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace blendity_link {

constexpr const char *kProtocol = "blendity-livelink/1";
constexpr const char *kBridgeVersion = "1.1";

/* ------------------------------------------------------------------------------------------------ JSON */

struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  double num = 0;
  bool b = false;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;
  const Json &operator[](const char *key) const;
  const Json &operator[](size_t i) const;
  double number(double def = 0) const { return type == Num ? num : def; }
  int64_t id() const { return type == Num ? (int64_t)num : 0; }
  bool boolean(bool def = false) const { return type == Bool ? b : def; }
  const std::string &string() const { return str; }
  size_t size() const { return type == Arr ? arr.size() : 0; }
  bool has(const char *key) const { return (*this)[key].type != Null; }
};

/* Parses one JSON value; false (and `out` Null) when the text isn't JSON. */
bool parse_json(const char *text, size_t len, Json &out);

/* ------------------------------------------------------------------------------------------------ framing */

struct Message {
  Json json;
  std::string type;            // json["t"]
  std::vector<uint8_t> bin;    // the attachment ("bin": N bytes after the line), empty when none
};

class Stream {
 public:
  void feed(const uint8_t *data, size_t n);
  /* The next whole message; false until more bytes come. A line that isn't JSON comes as a message of type "". */
  bool next(Message &out);
  size_t pending() const { return buf_.size() - pos_; }

 private:
  std::vector<uint8_t> buf_;
  size_t pos_ = 0;
};

/* ------------------------------------------------------------------------------------------------ axes */

struct Vec3 {
  double x = 0, y = 0, z = 0;
};
struct Quat {
  double x = 0, y = 0, z = 0, w = 1;
};

Vec3 position_to_unreal(const Json &a);  // glTF metres -> Unreal centimetres (a missing array: the origin)
Quat rotation_to_unreal(const Json &a);  // glTF (x, y, z, w) -> Unreal's FQuat order
Vec3 scale_to_unreal(const Json &a);     // (a missing array: one)
Vec3 direction_to_unreal(double x, double y, double z);  // unit vectors (normals): no scale

/* ------------------------------------------------------------------------------------------------ meshes */

struct GlbMesh {
  std::vector<Vec3> positions;  // Unreal's axes and centimetres
  std::vector<Vec3> normals;
  std::vector<float> uvs;       // u, v per vertex (glTF's and Unreal's V both run down)
  std::vector<int> vertex_of;   // each vertex's mesh vertex (_VERT): what a deform's positions are indexed by
  std::vector<int> normal_group;
  std::vector<std::vector<int>> sections;  // triangles per material slot, in Unreal's winding
  std::vector<int> slots;                  // each section's material slot
  size_t vertex_count() const { return positions.size(); }
};

bool read_glb(const uint8_t *glb, size_t n, GlbMesh &out, std::string *error = nullptr);
/* A deform's positions (float32 x, y, z per mesh vertex, glTF's axes) onto the mesh's vertices, the normals worked out
 * again; false when a vertex names a mesh vertex the deform doesn't have (the mesh is left as it was). */
bool apply_deform(GlbMesh &mesh, const uint8_t *data, size_t n);

/* ------------------------------------------------------------------------------------------------ discovery */

struct Endpoint {
  int port = 0;
  std::string scene, app;
};
/* ~/.blendity/livelink.json (BLENDITY_PREFS_DIR moves it): where Blendity's Live Link listens. */
std::string discovery_path();
/* The file's port and scene; false when there is none, it can't be read, or it is another protocol. */
bool read_discovery(const std::string &path, Endpoint &out);
/* The line a bridge sends first: who it is. */
std::string hello(const std::string &app);

}  // namespace blendity_link
