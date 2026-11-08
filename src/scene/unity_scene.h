// SPDX-License-Identifier: GPL-2.0-or-later
// Unity prefabs and scenes into Blendity's objects (round 370, task 0347): GameObjects with their Transforms (Unity
// and Blendity share Y-up, left-handed, metres: nothing flips), MeshFilter / MeshRenderer / SkinnedMeshRenderer meshes,
// Lights, Cameras, Box / Sphere / Capsule / Mesh Colliders, nested prefabs with their overrides - and every other
// component (scripts, Udon, VRChat's) an EngineComponent with its YAML kept as it was.
#pragma once

#include "import.h"
#include "scene.h"
#include "unity_yaml.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bl {

/* Unity's built-in meshes (Library/unity default resources), by the fileID a MeshFilter names them with. */
const char *unity_builtin_mesh(int64_t file_id);
constexpr const char *kUnityBuiltinGuid = "0000000000000000e000000000000000";

/* Round 395: what a prefab override's target is - its object and the class of its document (1 GameObject,
 * 4 Transform, 23 MeshRenderer, 137 SkinnedMeshRenderer, 108 Light, ...; 0 unknown). */
struct UnityTarget {
  GameObject *object = nullptr;
  int class_id = 0;
};
using UnityTargets = std::map<int64_t, UnityTarget>;

/* Round 395: a model's sub-objects by their fileIDs (an FBX's nodes as Unity numbers them): the class and the node's
 * path below the model's root ("" the root; names joined by '/'; a legacy table's entries hold the node's name). */
struct UnityModelPart {
  int class_id = 0;
  std::string path;
};
using UnityModelIds = std::map<int64_t, UnityModelPart>;
/* A model's sub-asset as Unity 2019.3+ numbers it: xxh64 of "Type:<type>-><name><n>" read as signed (n: the name's
 * occurrence, 0 first) - its meshes and materials. (Its nodes' numbers come from Unity: .unity-model-ids.) */
/* Round 455: Engine Components of Unity Light Probe Groups (an older scene file) get the LightProbeGroupComponent read from their text. */
int unity_probe_groups_from_engine(Scene &scene);
int64_t unity_hashed_file_id(const std::string &type, const std::string &name, int occurrence = 0);
/* What a model's .meta and Unity's constants say without Unity: the root's GameObject, Transform, renderer and filter
 * (the same numbers in every model, the persistent ones and the legacy 100000s) and a legacy table's names
 * (fileIDToRecycleName, internalIDToNameTable). */
void unity_model_ids_from_meta(const YamlNode &meta, UnityModelIds &out);
/* Round 399: a model with one top node, as Unity places it - that node is the root (with the model's name and its own
 * transform, renderer and children), not a child of an empty root: the root is removed and the node takes its place.
 * Returns the root to use (`root` when the model has several top nodes, or the root holds anything). An armature keeps
 * its own name (its skins find it by that). SideRail's gratings, a model's prefab variant with a collider on its root,
 * had stood at the scene's top in an empty root, the scene's parent and name given to the node inside. */
GameObject *unity_collapse_model_root(Scene &scene, GameObject *root);

/* Round 399: a model's parts as Unity imports them - a material no face of the part uses is no slot of its renderer
 * (Unity makes a submesh only for a material in use: SideRail's Tunnel_Rail had 5 slots here, 1 there). The faces'
 * material numbers follow. Returns the slots dropped. */
int unity_drop_unused_materials(ImportResult &r);

/* Round 399: Unity's fileID, in a prefab or scene, for an object of a prefab nested in it: its id in the nested prefab
 * XOR the PrefabInstance document's, the top bit cleared. */
int64_t unity_nested_file_id(int64_t inner, int64_t prefab_instance);

/* Round 399: a texture's size on the card as Unity imports it, from its .meta beside it or in its package folder's
 * .unity-meta (the Standalone override when it's on, else the default): its largest side capped, its format's bytes a
 * texel (a crunched format counted as the one it unpacks to, as VRChat's SDK does; automatic: DXT5 with alpha or for a
 * normal map, else DXT1), mips a third more. False when Unity has no settings for it. */
bool unity_texture_megabytes(const std::string &asset_path, int width, int height, bool has_alpha, double &mb);

/* Round 399: a model's material remapping - its .meta's externalObjects (Unity's Materials tab: each embedded material by
 * name to a .mat's GUID), which Unity puts on the model's meshes wherever the model is placed. */
std::map<std::string, std::string> unity_material_remap(const YamlNode &meta);
/* ...applied to the model's objects: each renderer slot whose material has a remapped name takes `material(guid)`
 * (nullptr: left as it is). Returns the .mat files converted. */
int apply_unity_material_remap(const std::vector<GameObject *> &made, const std::map<std::string, std::string> &remap,
                               const std::function<MaterialPtr(const std::string &guid)> &material);
/* ...on one list of slots (a mesh's as a MeshFilter places it), `converted` keeping each .mat converted once. */
void remap_unity_slots(std::vector<MaterialPtr> &slots, const std::map<std::string, std::string> &remap,
                       const std::function<MaterialPtr(const std::string &guid)> &material, std::map<std::string, MaterialPtr> &converted);

/* A .unity-model-ids file (written by Unity through Blendity: "<guid>\t<fileID>\t<class>\t<path>" a line), by GUID. */
void parse_unity_model_ids(const std::string &text, std::map<std::string, UnityModelIds> &out);
/* A model's objects (objects_from_model's: `made` its nodes' objects in r.nodes' order, under `root`) as override
 * targets by `ids`. A model with one top node: Unity makes that node its root - the root's numbers are that node's,
 * its transform the one the instance overrides (review 395: placed over the node's own, it had turned twice). */
void unity_model_targets(const ImportResult &r, GameObject *root, const std::vector<GameObject *> &made, const UnityModelIds &ids, UnityTargets &out);

/* What the builder asks for and what it counts. */
struct UnityImport {
  /* A mesh by its asset's GUID and the fileID within it (an FBX's submesh), for the object of that name (Unity names
   * a model's GameObjects after its nodes: the part when the .meta names none); `materials` its model's own. */
  std::function<MeshPtr(const std::string &guid, int64_t file_id, const std::string &object, std::vector<MaterialPtr> &materials)> mesh;
  /* A script's class by its GUID ("" when unknown, or not a .cs file: a DLL holds many). */
  std::function<std::string(const std::string &guid)> script_name;
  /* A prefab's documents by its GUID (false when it isn't in the project, or isn't a YAML prefab). */
  std::function<bool(const std::string &guid, std::vector<UnityDoc> &docs)> prefab;
  /* A model placed as a prefab (an FBX dragged into a scene: a VRChat avatar's usual root): its nodes made as objects
   * under one, returned; nullptr when the GUID isn't a model. */
  std::function<GameObject *(const std::string &guid, Scene &scene, UnityTargets &parts)> model;  // (round 395: `parts` its nodes by fileID, as far as known)
  /* Round 398: a Light Volume's baked textures (Texture0-2's GUIDs) for its object - Unity's bake, read back. */
  std::function<void(GameObject &g, const std::string &t0, const std::string &t1, const std::string &t2)> light_volume_textures;
  /* A material by its GUID (round 371: a Unity .mat converted; nullptr when not in the project). */
  std::function<MaterialPtr(const std::string &guid)> material;
  int depth = 0;
  std::string source;  // (round 372: the file the documents came from, project-relative - each object's UnityOrigin)
  std::function<std::string(const std::string &guid)> path_of;  // (a GUID's project-relative path: a nested prefab's source)
  /* Shared down the nesting (review 370): the prefabs being built (one instancing itself is refused) and the objects
   * made so far against a cap. */
  std::shared_ptr<std::set<std::string>> building;
  std::shared_ptr<int> total;
  int max_objects = 200000;
  int objects = 0, meshes = 0, missing_meshes = 0, lights = 0, cameras = 0, colliders = 0, engine = 0, prefabs = 0, overrides_kept = 0;
  int dropped = 0;  // (components whose object couldn't be found)
  int reflection_probes = 0;  // (round 401)
  int probe_groups = 0;       // (round 455: Light Probe Groups that came in as objects)
  int overrides_applied = 0, model_overrides_waiting = 0;  // (round 395: overrides on a model's parts whose fileIDs aren't known yet)
  int model_overrides_stale = 0;                           // (round 399: ...or that Unity's own numbers don't have: parts the model no longer has)
  std::shared_ptr<std::set<std::string>> models_numbered = std::make_shared<std::set<std::string>>();  // (round 399: models Unity has numbered - ctx.model adds them; shared down the nesting)
  std::shared_ptr<MaterialPtr> builtin_default = std::make_shared<MaterialPtr>();  // (round 399: Unity's Default-Material, one for the import)
  std::shared_ptr<std::set<const GameObject *>> physboned = std::make_shared<std::set<const GameObject *>>();  // (review 399: roots a PhysBone of this import is on)
  std::set<std::string> models_waiting;                    // (...their models' GUIDs: what Unity is asked for)
  int materials = 0, outlines = 0, springs = 0;  // (round 371)
  int light_volumes = 0;                         // (round 397: VRC Light Volumes and their lights)
  std::vector<std::string> notes;
};

/* Every GUID a project knows (round 371: shared by the scene import and the material loader): the unpacked packages'
 * .unity-guids with their kept .meta texts, and any .meta beside an asset. */
struct UnityGuidEntry {
  std::string path, meta;  // (absolute; the .meta's text)
};
using UnityGuidIndex = std::map<std::string, UnityGuidEntry>;
UnityGuidIndex unity_guid_index(const std::string &assets_dir, const std::string &project_root);
/* The project's index, kept a few seconds (a material loads a texture's path through it; many load at once). */
/* `fresh`: read again now (an import: files may have just arrived). */
std::shared_ptr<const UnityGuidIndex> unity_guid_index_cached(const std::string &assets_dir, const std::string &project_root, bool fresh = false);
/* A Unity shader's name by its GUID and fileID: the built-in Standard, URP's Lit and Unlit, else its file's name. */
std::string unity_shader_name(const UnityGuidIndex &index, const std::string &guid, int64_t file_id);
/* The material loader for Unity .mat files (set_foreign_material_loader): its textures and shader through the
 * project's index. */
bool load_unity_material(const std::string &text, const std::string &path, Material &m);

/* A model's nodes as objects (their hierarchy, transforms, meshes and materials) under one named `name` - a model
 * placed in a Unity prefab, or instanced in a Godot scene (round 373). */
GameObject *objects_from_model(const ImportResult &r, const std::string &name, Scene &scene, std::vector<GameObject *> *made = nullptr);

/* Round 395: a Unity scene's RenderSettings into the environment - its ambient (flat or gradient) and its fog (Unity's
 * exponential fog as the linear distance it reaches 95% at); what doesn't map said in `notes`. False: none there. */
struct UnitySkybox;
bool unity_render_settings(const std::vector<UnityDoc> &docs, EnvironmentSettings &env, std::vector<std::string> &notes,
                           const std::function<bool(const std::string &guid, UnitySkybox &out)> &skybox = {}, UnitySceneLink *link = nullptr);

/* Round 421 (ADR 0113): a Unity scene's lightmap settings - its LightingSettings asset when it references one (Unity
 * 2020+, `lighting_asset` its text), else the scene's own m_LightmapEditorSettings - into Blendity's bake settings:
 * Lightmap Resolution, Max Lightmap Size, Padding, direct and indirect samples, bounces, the filter on or off. What
 * Blendity doesn't bake yet (Directional mode, Shadowmask / Subtractive, AO, compression, the environment's own sample
 * count, the denoiser's kind) is kept in `out` for the caller's notes. A LightingSettings document embedded in the
 * scene (m_LightingSettings by fileID alone) is read too. Numbers are clamped to the Lighting window's ranges. False:
 * the scene has none. */
bool unity_bake_settings(const std::vector<UnityDoc> &docs, const std::string &lighting_asset, UnityBakeSettings &out);

/* Round 401: a Post Processing (PPv2) profile as Blendity reads it - each effect's parameters where the profile
 * overrides them (overrideState 1), Unity's defaults otherwise. Tonemapper 0 None, 1 Neutral, 2 ACES. Effects
 * Blendity doesn't draw (ambient occlusion, depth of field, chromatic aberration, grain, auto exposure, motion blur)
 * are flagged, kept and said. */
struct UnityPostProfile {
  struct Grading {
    bool on = false;
    int tonemapper = 0;
    float post_exposure = 0.0f, temperature = 0.0f, tint = 0.0f, hue_shift = 0.0f, saturation = 0.0f, contrast = 0.0f;
    Vec3 color_filter{1.0f, 1.0f, 1.0f};
    float lift[4] = {1, 1, 1, 0}, gamma[4] = {1, 1, 1, 0}, gain[4] = {1, 1, 1, 0};  // (xyz the wheel's colour, w its offset)
  } grading;
  struct Bloom {
    bool on = false;
    float intensity = 0.0f, threshold = 1.0f, soft_knee = 0.5f, diffusion = 7.0f, clamp = 65472.0f;
    Vec3 color{1.0f, 1.0f, 1.0f};
  } bloom;
  struct Vignette {
    bool on = false;
    float intensity = 0.0f, smoothness = 0.2f, roundness = 1.0f;
    Vec3 color{0.0f, 0.0f, 0.0f};
  } vignette;
  std::vector<std::string> undrawn;  // (the effects on in the profile that Blendity doesn't draw)
};
/* The profile asset's text into `out`; false when it isn't one. */
bool unity_post_profile(const std::string &yaml, UnityPostProfile &out);
/* The profile onto a camera's look (round 401): its tonemapper and exposure as the scene's view transform and exposure,
 * its grading, bloom (PPv2's intensity 2^(i / 10) - 1, its threshold gamma, its diffusion a tenth as the scatter) and
 * vignette as the camera's filter effects (replacing ones of the same kind). */
void unity_post_apply(const UnityPostProfile &p, Scene &scene, GameObject &camera, std::vector<std::string> &notes);

/* Round 372: an imported object's edits in its Unity file - its name and active state on its GameObject, its local
 * transform on its Transform. */
struct UnityEdit {
  int64_t file_id = 0;
  bool transform = false;
  std::string name;
  bool active = true;
  Vec3 position, scale{1, 1, 1}, euler;  // (euler: the Inspector's angles - m_LocalEulerAnglesHint when the rotation changes)
  Quat rotation;
  /* Round 401: the document's own fields (a Camera's, a Light's, the scene's RenderSettings) - each key's line rewritten
   * when its value differs ("m_Shadows/m_Type": a key inside a nested mapping). The document is the one with `file_id`,
   * or, with `class_id` set, the one of that class on the GameObject `game_object` (0: the first of that class). */
  int class_id = 0;
  int64_t game_object = 0;
  std::vector<std::pair<std::string, std::string>> fields;
  /* Round 403: the whole document as Blendity holds it (an Engine Component edited in the Inspector): it replaces the
   * file's document of `file_id` when the two differ. */
  std::string document;
};
/* Round 401: a Camera's, a Light's and a Reflection Probe's Unity fields as the object holds them now (edits by class and
 * GameObject), and the scene's RenderSettings as the world holds them (an edit by class). */
void unity_component_edits(const GameObject &g, int64_t game_object, std::vector<UnityEdit> &out);
UnityEdit unity_world_edit(const EnvironmentSettings &e, const UnitySceneLink &link);
/* Unity's YAML scalars (round 401): a number, a colour (Blendity's linear colour as Unity's gamma one) and a vector. */
std::string unity_yaml_num(double v);
std::string unity_yaml_color(Vec3 linear, float alpha = 1.0f);
std::string unity_yaml_vec3(Vec3 v);
/* `text` (a .prefab or .unity) with the edits written into their documents' lines - m_Name, m_IsActive,
 * m_LocalPosition / Rotation / Scale (and the Euler hint) - only where the value differs from the file's (review 372:
 * untouched objects were rewritten with float noise); every other byte as it was, line ends too; `changed` how many
 * lines changed. */
std::string patch_unity_objects(const std::string &text, const std::vector<UnityEdit> &edits, int *changed = nullptr);
/* Round 403: one field's line of a document rewritten ("key" or "block/key"); false when the document has no such line. */
bool unity_set_field(std::string &doc, const std::string &key, const std::string &value);
/* Round 403: Unity's field name as the Inspector shows it ("m_" dropped, the camel case split). */
std::string unity_field_label(const std::string &key);
/* A string as Unity's YAML writes it: plain when it can be, else double-quoted with non-ASCII as \uXXXX. */
std::string unity_yaml_string(const std::string &s);

/* The documents' objects made in `scene` under `parent`, by their GameObject's and Transform's fileIDs. */
std::map<int64_t, GameObject *> build_unity_objects(const std::vector<UnityDoc> &docs, Scene &scene, GameObject *parent, UnityImport &ctx,
                                                    UnityTargets *targets = nullptr);

}  // namespace bl
