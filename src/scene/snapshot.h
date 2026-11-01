// SPDX-License-Identifier: GPL-2.0-or-later
// Send to Blendity (round 374, task 0351): a snapshot of another program's scene - a folder <name>.blendity-snapshot
// holding a model (.glb or .fbx, the program's own exporter's) and snapshot.json, written last, naming what the model
// file can't carry: each object's lights and cameras in Live Link's own fields (docs/LIVELINK.md), its class in its
// engine (an Unreal Blueprint's path) and custom properties, and for an object the model doesn't hold its transform.
// Blender (the bridge's Send to Blendity, or a .blend read through Blender) and Unreal (its bridge's) write them.
#pragma once

#include "../core/json.h"
#include "scene.h"

#include <string>
#include <vector>

namespace bl {

struct SnapshotObject {
  std::string name, path;    // (path: the names from the scene's top, '/' between)
  std::string engine_class;  // (an Unreal actor's class or Blueprint; "" for none)
  std::string props;         // (its custom properties, "key = value" lines)
  bool has_light = false, has_camera = false, has_transform = false;
  Json light, camera;        // (Live Link's light and camera messages' fields)
  Transform transform;       // (world, Blendity's axes: left-handed, Y up, +Z forward, metres)
};

struct Snapshot {
  static constexpr size_t kMaxObjects = 100000;  // (a hostile file's list cut there)
  std::string source;  // ("Blender 5.2", "Unreal 5.5")
  std::string name, model;
  Quat model_rotation;  // (the turn from the model's import frame to the snapshot's: Unreal's FBX, 90 degrees about Y)
  std::vector<SnapshotObject> objects;
};

/* snapshot.json's text read; false (and why) when it isn't one. */
bool read_snapshot(const std::string &text, Snapshot &out, std::string &why);
/* The engine a snapshot came from ("Unreal", "Blender"): its source's first word. */
std::string snapshot_engine(const Snapshot &s);

/* Live Link's light and camera fields onto Blendity's components (the bridges send them as Live Link does). */
void snapshot_light(const Json &j, Light &l);
void snapshot_camera(const Json &j, Camera &c);

}  // namespace bl
