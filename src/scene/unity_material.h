// SPDX-License-Identifier: GPL-2.0-or-later
// Unity materials into Blendity's (round 371, task 0348): a .mat (YAML) read by its shader - the built-in Standard
// and URP Lit / Unlit to PBR, the toon shaders VRChat avatars use (lilToon, Poiyomi, MToon, UTS2) to Toon with their
// shade colours and outlines - from their documented property names. An unknown shader keeps its main texture and
// colour, and says so.
#pragma once

#include "material.h"

#include <functional>
#include <string>
#include <vector>

namespace bl {

/* What came across besides the material: the shader's family, an outline to put on the objects, what didn't map. */
struct UnityMaterialInfo {
  std::string shader;   // ("Standard", "URP Lit", "Unlit", "lilToon", "Poiyomi", "MToon", "UTS2", or the shader's own name)
  std::string shader_name;  // (the shader's own name, as its file declares it: "Hidden/lilToonOutline")
  std::vector<std::string> notes;  // (what came across only in part)
  bool known = true;    // (false: only the main texture and colour taken)
  float outline_width = 0.0f;  // metres (0: none)
  Vec3 outline_color{0.0f, 0.0f, 0.0f};
};

/* The shader family from its name (its file's) and the material's properties. */
std::string unity_shader_family(const std::string &shader_name, const std::vector<std::string> &properties);

/* A Unity .mat's text into `m`. `texture` gives a texture GUID's project-relative path ("" when not in the project);
 * `shader_name` a shader GUID's name ("" for a built-in one: then the fileID says). False when it isn't a material. */
bool unity_material(const std::string &yaml, const std::function<std::string(const std::string &guid)> &texture,
                    const std::function<std::string(const std::string &guid, int64_t file_id)> &shader_name, Material &m, UnityMaterialInfo &info,
                    const std::function<std::vector<std::string>(const std::string &guid)> &shader_properties = {});

/* Round 400: the property names a shader's source declares (its Properties block) - a .mat keeps every property any
 * shader it ever had set, and Unity reads only its shader's: `shader_properties` (optional) gives them, and the rest
 * are left out (SideRail's exit sign had taken a texture its shader no longer had). Empty when there's no block. */
std::vector<std::string> unity_shader_properties(const std::string &shader_text);

}  // namespace bl
