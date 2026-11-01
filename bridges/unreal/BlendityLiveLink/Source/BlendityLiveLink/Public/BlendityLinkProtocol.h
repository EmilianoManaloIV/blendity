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
#include <type_traits>
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
  const Json &at(size_t i) const;  // (the null Json past the end)
  // Any integer index, one overload: a literal such as B[0] is an int, which MSVC found ambiguous between a size_t
  // overload and the key's (0 is also a null const char *), and two integer overloads would make int64 or uint32
  // ambiguous instead. A negative index is past the end.
  template <class I, typename std::enable_if<std::is_integral<I>::value, int>::type = 0>
  const Json &operator[](I i) const { return static_cast<long long>(i) < 0 ? at(arr.size()) : at(static_cast<size_t>(i)); }
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
  std::string inbox;  // (round 374: where Send to Blendity writes; "" from an older Blendity)
};
/* ~/.blendity/livelink.json (BLENDITY_PREFS_DIR moves it): where Blendity's Live Link listens. */
std::string discovery_path();
/* The file's port and scene; false when there is none, it can't be read, or it is another protocol. */
bool read_discovery(const std::string &path, Endpoint &out);
/* The line a bridge sends first: who it is. */
std::string hello(const std::string &app);

/* ------------------------------------------------------------------------------------------------ Send to Blendity */

/* Round 374: an actor as Send to Blendity's snapshot.json names it (docs/LIVELINK.md, Send to Blendity), from Unreal's
 * own values: the level's model (an FBX) carries the meshes; this carries what it can't. */
struct SnapActor {
  std::string name, path, cls;  // (path: the attach parents' labels, '/' between; cls: a Blueprint's class path, "" for none)
  double loc[3] = {0, 0, 0};    // (Unreal's: centimetres, X forward, Y right, Z up)
  double quat[4] = {0, 0, 0, 1};
  double scale[3] = {1, 1, 1};
  int light = 0;  // 0 none, 1 directional, 2 point, 3 spot, 4 rect
  double color[3] = {1, 1, 1};  // (linear)
  double intensity = 0;         // (lux for a directional light, candela for the others)
  double radius_cm = 1000, outer_deg = 44, inner_deg = 0, width_cm = 64, height_cm = 64;
  bool camera = false, ortho = false;
  double hfov_deg = 90, aspect = 16.0 / 9.0, near_cm = 10, far_cm = 100000, ortho_width_cm = 512;
};
/* snapshot.json's text: each actor's transform in Blendity's axes - Unreal's (x, y, z) cm is (y, z, x) / 100, both
 * left-handed, a rotation's parts permuted the same way, a light's or camera's +X front Blendity's +Z - its light and
 * camera in Live Link's fields (the applier's units turned around), its class. */
std::string snapshot_json(const std::string &source, const std::string &name, const std::string &model, const std::vector<SnapActor> &actors);
/* Unreal's FBX (Z up, front -Y, unforced front axis) comes into Blendity as (-x, z, y): 90 degrees about Y short of
 * the snapshot's (y, z, x). snapshot.json's model_rotation, which Blendity turns the model by (review 374). */
constexpr double kFbxModelRotation[4] = {0.0, 0.70710678118654752, 0.0, 0.70710678118654752};

}  // namespace blendity_link
