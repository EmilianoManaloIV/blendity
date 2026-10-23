// SPDX-License-Identifier: GPL-2.0-or-later
// Animation clips from model files (task 0139, ADR 0047): an FBX's animation stacks and a glTF's animations as
// AnimationClips on the import (ImportResult::clips, paths relative to the import's root), as Unity's importer makes
// them. Called by the FBX and glTF readers once their nodes are made.
#pragma once

#include "import.h"

#include <functional>
#include <unordered_map>

struct ufbx_scene;
struct ufbx_node;

namespace bl {

struct Json;

/* Each stack a clip at the file's frame rate: every node's local transform evaluated a frame at a time (a track for
 * each property that changes), each blend channel's weight as a BlendShape track. `index`: ufbx node -> import node. */
void fbx_import_clips(const ufbx_scene *scene, const std::unordered_map<const ufbx_node *, int> &index, ImportResult &out);
/* Each animation a clip: translation, scale and weights keyed at the samplers' times (cubic splines baked), rotation
 * baked a frame at a time from the samplers' slerp; the frame rate the one the times fit best. */
void gltf_import_clips(const Json &doc, const std::vector<int> &node_index, const std::function<std::vector<float>(int, int &)> &read_accessor,
                       ImportResult &out);

}  // namespace bl
