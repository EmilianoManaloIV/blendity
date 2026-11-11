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
  /* Round 485 (ADR 0131): the GUID of the texture whose alpha Unity's shader takes this material's smoothness from
   * ("" when it comes from a slider), and whether that alpha's mean was not known - then the roughness is the one read
   * without it, and the caller can have the texture measured and read the material again. */
  std::string gloss_texture;
  bool gloss_pending = false;
};

/* The shader family from its name (its file's) and the material's properties. */
std::string unity_shader_family(const std::string &shader_name, const std::vector<std::string> &properties);

/* A Unity .mat's text into `m`. `texture` gives a texture GUID's project-relative path ("" when not in the project);
 * `shader_name` a shader GUID's name ("" for a built-in one: then the fileID says). False when it isn't a material.
 * `mean_alpha` (round 485, optional): the mean of the alpha Unity's shader samples from a texture - true and the
 * number when it is known; without it, or while it says false, a smoothness kept in a map is read as before. */
bool unity_material(const std::string &yaml, const std::function<std::string(const std::string &guid)> &texture,
                    const std::function<std::string(const std::string &guid, int64_t file_id)> &shader_name, Material &m, UnityMaterialInfo &info,
                    const std::function<std::vector<std::string>(const std::string &guid)> &shader_properties = {},
                    const std::function<bool(const std::string &guid, float &alpha)> &mean_alpha = {});

/* Round 485: the GUIDs of the textures a Unity .mat's gloss is read from by its keywords - the metallic-gloss map
 * under _METALLICGLOSSMAP (URP: _METALLICSPECGLOSSMAP), the albedo under _SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A. What an
 * import measures once (`.unity-stats`), so that reading a material never decodes an image. Empty for anything else. */
std::vector<std::string> unity_material_gloss_textures(const std::string &yaml);

/* Round 400: the property names a shader's source declares (its Properties block) - a .mat keeps every property any
 * shader it ever had set, and Unity reads only its shader's: `shader_properties` (optional) gives them, and the rest
 * are left out (SideRail's exit sign had taken a texture its shader no longer had). Empty when there's no block. */
std::vector<std::string> unity_shader_properties(const std::string &shader_text);

/* Round 401: a scene's skybox material as Blendity's world. `shader` the shader's name (Skybox/Procedural, Skybox/Panoramic,
 * Skybox/Cubemap, Skybox/6 Sided, Unlit/...); the fields each reads. */
struct UnitySkybox {
  std::string shader;
  std::string texture;  // Panoramic's _MainTex, Cubemap's _Tex (project-relative path, "" when not in the project)
  std::string sides[6];  // 6 Sided: front, back, left, right, up, down
  Vec3 tint{0.5f, 0.5f, 0.5f}, color{0.0f, 0.0f, 0.0f}, sky_tint{0.5f, 0.5f, 0.5f}, ground{0.369f, 0.349f, 0.341f};  // (linear)
  float exposure = 1.0f, rotation = 0.0f, sun_size = 0.04f, atmosphere_thickness = 1.0f;
};
bool unity_skybox(const std::string &yaml, const std::function<std::string(const std::string &guid)> &texture,
                  const std::function<std::string(const std::string &guid, int64_t file_id)> &shader_name, UnitySkybox &out);

}  // namespace bl
