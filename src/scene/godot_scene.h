// SPDX-License-Identifier: GPL-2.0-or-later
// Godot scenes into Blendity's objects (round 373, task 0350): a .tscn (and the .tres resources it names) read as
// text - Godot 4's format 3 and Godot 3's format 2 - its nodes made into objects: Node3D, MeshInstance3D (Godot's
// primitive meshes, StandardMaterial3D), lights, Camera3D, CollisionShape3D, instanced scenes and models; every other
// node and every script an EngineComponent with its section kept as Godot wrote it. Godot's axes are glTF's: X
// mirrors both ways, as every importer and Live Link's Godot bridge do (ADR 0055), and a light or camera (looking
// down -Z) is its object turned 180 degrees about Y. Its transforms' edits go back into the same lines.
#pragma once

#include "scene.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bl {

/* One "[tag key=value ...]" section and its "key = value" properties (a value may run over several lines). */
struct GodotSection {
  std::string tag;
  std::vector<std::pair<std::string, std::string>> attrs, props;
  std::string text;  // (the section as written, header included)
  std::string attr(const std::string &k) const;
  std::string prop(const std::string &k) const;  // ("" when it isn't there)
  bool has(const std::string &k) const;
};
bool parse_godot_text(const std::string &text, std::vector<GodotSection> &out, std::string &why);

/* Godot's values: a quoted string unescaped; the numbers in a constructor (Vector3(1, 2, 3), Color(...),
 * Transform3D(...)); an ExtResource / SubResource's id, "1" or "1_abc" (Godot 3's numeric and 4's quoted). */
std::string godot_string(const std::string &v);
std::vector<double> godot_numbers(const std::string &v);
std::string godot_ref(const std::string &v);

/* A Transform3D(...) / Transform(...) (12 numbers: the basis by rows, then the origin) as Blendity's local transform,
 * X mirrored; and back. A light's or camera's own turn is the builder's (godot_turn). `ok` false: a number wasn't
 * finite (identity returned). */
Transform godot_transform(const std::string &v, bool *ok = nullptr);
std::string godot_transform_text(const Transform &t, bool format4);
/* The 180-degree turn about Y a light or camera carries (`self`), and that its children's local transforms carry
 * against it (`parent`): its own inverse, so the same call goes both ways. */
Transform godot_turn(Transform t, bool self, bool parent);
/* Two transforms' texts the same to a millionth (each number relative to its size). */
bool godot_same_transform(const std::string &a, const std::string &b);

/* A scene file's key for loop checks: normalized, forward slashes, lower case. */
std::string godot_path_key(const std::string &path);

/* What the builder asks for and counts. */
struct GodotImport {
  static constexpr int kMaxObjects = 100000;  // (all instancing together: a hostile file can't fill memory)
  int max_objects = kMaxObjects;
  std::string res_root;  // (the Godot project's folder: res:// there)
  std::string source;    // (the scene's project-relative path: each object's GodotOrigin)
  std::function<GameObject *(const std::string &abs_path, Scene &scene)> model;  // (a .glb / .gltf / .obj / .fbx instanced)
  std::function<std::string(const std::string &abs_path)> texture;               // (an image's project-relative path)
  int depth = 0;
  std::shared_ptr<std::set<std::string>> building;  // (scenes being built: godot_path_key)
  std::shared_ptr<int> total;                       // (objects made, shared by the instanced scenes)
  int objects = 0, meshes = 0, materials = 0, lights = 0, cameras = 0, colliders = 0, engine = 0, scenes = 0, overrides = 0;
  std::vector<std::string> notes;
};

/* The scene's nodes as objects under `parent`; its root node returned (nullptr when there's none). */
GameObject *build_godot_scene(const std::vector<GodotSection> &sections, Scene &scene, GameObject *parent, GodotImport &ctx);

/* Round 373: the edits of objects imported from a .tscn - their transform and visibility - written into their
 * nodes' sections (a line replaced, added after the header, or a visible = false taken away); every other byte as
 * it was. `path` is the node's path from the scene's root ("." for the root); `local` Blendity's, turned back as
 * GodotOrigin says; `moved` false keeps the transform line as it is. */
struct GodotEdit {
  std::string path;
  Transform local;
  bool visible = true, moved = true, turned = false, parent_turned = false;
};
std::string patch_tscn(const std::string &text, const std::vector<GodotEdit> &edits, int *changed = nullptr);

}  // namespace bl
