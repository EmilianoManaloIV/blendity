// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene.h"
#include "subdiv_chain.h"
#include "../core/json.h"
#include "animation.h"
#include "sequence.h"
#include "curve.h"
#include "stroke_object.h"

#include "../core/core.h"
#include "../platform/platform.h"
#include "../render/camera_filter.h"
#include "../render/colormanagement.h"
#include "../render/shading.h"
#include "physics.h"
#include "spring_bones.h"  // (SpringBone's registration)
#include "footsteps.h"     // (Footprint's and Footsteps')
#include "locomotion.h"    // (Locomotion's)
#include "../core/jobs.h"  // (NormalTransferModifier's search)

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace bl {

/* ===================================================================== */
/* Component registry                                                     */
/* ===================================================================== */

std::vector<ComponentInfo> &component_registry() {
  static std::vector<ComponentInfo> reg;
  return reg;
}

void register_component(ComponentInfo info) {
  for (auto &r : component_registry())
    if (r.name == info.name) { r = std::move(info); return; }
  component_registry().push_back(std::move(info));
}

const ComponentInfo *find_component_info(const std::string &name) {
  for (auto &r : component_registry())
    if (r.name == name) return &r;
  return nullptr;
}

std::unique_ptr<Component> create_component(const std::string &name) {
  const ComponentInfo *info = find_component_info(name);
  return info ? info->create() : nullptr;
}

template<class T> static void reg(const char *category, const char *help, const char *blender) {
  register_component({T::kName, category, help, blender, [] { return std::make_unique<T>(); }});
}

/* ---- round 329: ramps and curves as text (the shortest text that reads back exactly: append_float) ---- */

namespace {
constexpr size_t kRampMax = 32;  // (the stops or points a ramp or curve keeps; the widgets add no more)
size_t next_item(const std::string &s, size_t at) {
  const size_t a = s.find(';', at), b = s.find(',', at);
  return std::min(a == std::string::npos ? s.size() : a, b == std::string::npos ? s.size() : b);
}
}  // namespace

std::string ramp_to_text(const std::vector<RampStop> &stops) {
  std::string s;
  for (const RampStop &r : stops) {
    if (!s.empty()) s += "; ";
    for (float v : {r.pos, r.color.x, r.color.y, r.color.z}) append_float(s, v), s += ' ';
    s.pop_back();
  }
  return s;
}

bool ramp_from_text(const std::string &s, std::vector<RampStop> &stops) {
  std::vector<RampStop> out;
  size_t at = 0;
  while (at < s.size()) {
    const size_t end = next_item(s, at);
    RampStop r;
    float p, x, y, z;
    if (std::sscanf(s.substr(at, end - at).c_str(), "%f %f %f %f", &p, &x, &y, &z) == 4 && std::isfinite(p + x + y + z))
      r.pos = std::max(0.0f, std::min(1.0f, p)), r.color = {std::max(0.0f, x), std::max(0.0f, y), std::max(0.0f, z)}, out.push_back(r);
    at = end + 1;
  }
  if (out.size() > kRampMax) out.resize(kRampMax);
  std::stable_sort(out.begin(), out.end(), [](const RampStop &a, const RampStop &b) { return a.pos < b.pos; });
  stops = out;
  return true;
}

std::string curve_to_text(const std::vector<Vec2> &pts) {
  std::string s;
  for (const Vec2 &p : pts) {
    if (!s.empty()) s += "; ";
    append_float(s, p.x), s += ' ', append_float(s, p.y);
  }
  return s;
}

bool curve_from_text(const std::string &s, std::vector<Vec2> &pts) {
  std::vector<Vec2> out;
  size_t at = 0;
  while (at < s.size()) {
    const size_t end = next_item(s, at);
    float x, y;
    if (std::sscanf(s.substr(at, end - at).c_str(), "%f %f", &x, &y) == 2 && std::isfinite(x + y)) out.push_back({std::max(0.0f, std::min(1.0f, x)), std::max(0.0f, std::min(1.0f, y))});
    at = end + 1;
  }
  if (out.size() > kRampMax) out.resize(kRampMax);
  std::stable_sort(out.begin(), out.end(), [](const Vec2 &a, const Vec2 &b) { return a.x < b.x; });
  pts = out;
  return true;
}

/* Component names may have spaces, but no word of a name may start with a digit: scene files
 * write "component <name> <flags...>" and the loader takes words up to the first number. */
void register_builtin_components() {
  static bool done = false;
  if (done) return;
  done = true;
  reg<MeshFilter>("Mesh", "Holds the mesh geometry this GameObject displays.",
                  "Object Data (the Mesh datablock linked to an Object)");
  reg<MeshRenderer>("Mesh", "Draws the MeshFilter's mesh with a simple lit material.",
                    "Material slot + viewport display settings");
  reg<SubdivisionSurface>("Mesh", "Non-destructive Catmull-Clark subdivision applied before rendering.",
                          "Subdivision Surface modifier");
  reg<SmoothModifier>("Mesh", "Non-destructive Laplacian smoothing applied before rendering.", "Smooth modifier");
  reg<SmoothNormalsModifier>("Mesh", "Shading normals smoothed over the surface (the anime face: its shape stays, its bumps stop shading).", "Smooth Normals modifier");
  reg<NormalTransferModifier>("Mesh", "Shading normals taken from a proxy object's surface (a sphere round the head: Blender's Data Transfer trick).",
                              "Normal Transfer modifier");
  reg<MirrorModifier>("Mesh", "Mirrors the mesh across its local X / Y / Z planes.", "Mirror modifier");
  reg<ArrayModifier>("Mesh", "Repeats the mesh with an offset.", "Array modifier");
  reg<SolidifyModifier>("Mesh", "Gives surfaces thickness (a shell with rim faces).", "Solidify modifier (Simple)");
  reg<BooleanModifier>("Mesh", "Cuts, joins or intersects with another object (Manifold).", "Boolean modifier (Manifold solver)");
  reg<DecimateModifier>("Mesh", "Reduces the triangle count (meshoptimizer).", "Decimate modifier (Collapse)");
  reg<BevelModifier>("Mesh", "Rounds off edges sharper than an angle (or every edge).", "Bevel modifier");
  reg<TriangulateModifier>("Mesh", "Splits quads and n-gons into triangles.", "Triangulate modifier");
  reg<WeldModifier>("Mesh", "Merges vertices closer than a distance.", "Weld modifier");
  reg<WireframeModifier>("Mesh", "Turns every edge into a strut (a lattice).", "Wireframe modifier");
  reg<DisplaceModifier>("Mesh", "Pushes vertices along their normals by a noise texture.", "Displace modifier (procedural texture)");
  reg<SimpleDeformModifier>("Mesh", "Twists, bends, tapers or stretches the mesh along an axis.", "Simple Deform modifier");
  reg<CastModifier>("Mesh", "Pulls the mesh towards a sphere, cylinder or box.", "Cast modifier");
  reg<JiggleModifier>("Mesh", "Skin jiggle without bones: a vertex group's vertices trail the body's acceleration on a damped spring\n"
                              "(a breast, a belly on a rig with no such bones; task 0153). Runs in Play.",
                      "Jiggle (Wiggle Bones / jiggle physics add-ons)");
  reg<SkinModifier>("Mesh", "Moves the mesh with an armature's bones: each vertex by the bones its vertex groups are named after.",
                    "Armature modifier (Vertex Groups)");
  reg<Bone>("Rigging", "A bone of a skeleton: a skin moves the vertices of the vertex group named after it.", "Bone (in an Armature)");
  reg<Armature>("Rigging", "On a skeleton's root: how skins deform (Linear or Dual Quaternion), Rest Position, Show Bones.",
                "Armature object data");
  reg<SpringBone>("Rigging", "Secondary motion on the chain this bone starts (hair, an ear, a tail, a cloth strip): it lags, swings past and\n"
                             "settles as the body moves, kept outside the body's bones. Rigging > Spring Bones finds chains by name.",
                   "Spring bones (VRM, Cascadeur), Blender's Wiggle Bones add-on");
  reg<Locomotion>("Animation", "Walks and runs the character in Play wherever it goes (WASD, a PlayerController, a script's set_move): the\n"
                  "gait of Walk Cycle made live, the feet put on the ground. Needs Preferences > Animation Assistants > Locomotion.",
                  "a locomotion controller (Unity's Animator with a blend tree; CATMotion's path walking)");
  reg<Footprint>("Animation", "One footprint of a walk along a path: when the foot lands (Frame) and which foot. Move and turn it with the\n"
                 "gizmo; Rigging > Footsteps > Update walks the character on it.", "Biped's footsteps (3ds Max)");
  reg<Footsteps>("Animation", "Holds a character's footprints (its children) and the path they were laid along: Rigging > Footsteps.",
                 "Biped's footsteps (3ds Max)");
  reg<IKConstraint>("Rigging", "On the end bone of a chain (a shin, a forearm): the chain turns so its tip reaches the Target. "
                    "Move the target, not the bones; a Pole sets which way the knee or elbow points.",
                    "Inverse Kinematics bone constraint (with Pole Target)");
  reg<RigControl>("Rigging", "A controller shape to grab instead of bones (an IK target or pole, an FK circle): drawn over the scene, "
                  "colored by side - left blue, right red, the middle yellow.",
                  "a custom bone shape (Maya: a NURBS controller curve)");
  reg<Animation>("Animation", "Plays an animation clip (Assets/Animations/*.anim) on this object and the ones below it, in Play. "
                 "Make and edit clips in the Animation window.",
                 "an Action played by the object (Animation data)");
  reg<Sequencer>("Animation", "Plays a sequence of animation clips (Assets/Animations/*.seq): clips laid out on tracks, crossfading where "
                 "they overlap; its triggers' keys jump to a clip in Play. Lay sequences out in the Sequencer window.",
                 "the Nonlinear Animation editor's strips (NLA)");
  reg<MeshDeformModifier>("Mesh", "Binds the mesh to a closed cage mesh round it; moving the cage's vertices shapes the mesh smoothly.",
                          "Mesh Deform modifier (Bind)");
  reg<LatticeModifier>("Mesh", "Moves the mesh with a Lattice object's points: squash, bulge, taper.", "Lattice modifier");
  reg<Lattice>("Mesh", "A box of points round a mesh: a Lattice modifier moves the mesh with them. Click a point to move it with the gizmo.",
               "Lattice object");
  reg<CurveModifier>("Mesh", "Bends the mesh along a Curve object (rails, pipes, roads, chains).", "Curve modifier");
  reg<Curve>("Mesh", "A path of control points (Bezier, Catmull-Rom or Poly): Curve modifiers bend meshes along it and "
             "Array's Fit Curve fills it. Click a point to move it with the gizmo.",
             "Curve object (Bezier / Poly spline)");
  reg<ScrewModifier>("Mesh", "Sweeps the edges round an axis (a lathe): vases, bottles, springs.", "Screw modifier");
  reg<ProceduralShape>("Mesh", "Keeps the settings of a parametric shape (box, stairs, arch, pipe...) and rebuilds the mesh when they change.", "Add Mesh + Adjust Last Operation; Extra Objects shapes");
  reg<Light>("Rendering", "A directional (sun) or point light.", "Light object (Sun / Point)");
  reg<Strokes>("Mesh", "Brush strokes as geometry (task 0248): cards and ribbons painted onto surfaces, the object's mesh rebuilt from them with custom normals (Own, Surface or Canopy).", "Grease Pencil strokes / a cards mesh with custom normals");
  reg<ProbeVolume>("Rendering",
                   "Light probes placed automatically (dense near geometry): objects without a lightmap take their indirect "
                   "light from them. Baked by Generate Lighting, kept live by Realtime GI.",
                   "Unity: Adaptive Probe Volume; Blender EEVEE: Irradiance Volume");
  reg<Camera>("Rendering", "Renders the Game view.", "Camera object (the active scene camera)");
  reg<CameraFilters>("Rendering",
                     "A stack of filters on this camera, applied top to bottom: Retro Console looks (PS1, N64, Saturn, DOS) and more. "
                     "Add Filter picks them by category.",
                     "Compositor nodes on the render (Unity: the Post-processing Layer and Volume profile)");
  reg<Outline>("Rendering",
               "Toon outlines: a shell around the object in one color, so it has a line round its silhouette (the inverted "
               "hull). Pair it with a Toon material.",
               "Blender: Solidify (Flip Normals) + an outline material; Goo Engine's outlines");
  reg<Rotator>("Scripts", "Spins the object while in Play mode.", "A driver or keyframed rotation");
  reg<Wind>("Scripts", "A wind along this object's forward (or away from it): spring chains - hair, cloth, tails - move in it (task 0153).", "Blender's Wind force field");
  reg<Oscillator>("Scripts", "Moves the object back and forth while in Play mode.", "Noise / cycles F-Curve modifier");
  reg<PlayerController>("Scripts", "WASD / arrow keys move this object in Play mode.",
                        "No direct equivalent (Blender removed its game engine in 2.80)");
  reg<Rigidbody>("Physics", "Gravity, bounciness and simple sphere collisions in Play mode.",
                 "Rigid Body physics (blender/source/blender/blenkernel/intern/rigidbody.cc)");
}

/* ===================================================================== */
/* Component behaviour                                                    */
/* ===================================================================== */

void MeshRenderer::reflect(Reflector &r) {
  r.material_list("Materials", materials);
  r.help("Material per slot. Faces pick a slot with their material index (Blender: Material Slots, Assign in Edit Mode).");
  r.field("Cast Shadows", cast_shadows);
  r.field("Receive Shadows", receive_shadows);
  r.field("Show Wireframe", show_wireframe);
  static const char *kDisplay[] = {"Solid", "Wire", "Bounds"};
  r.enumeration("Display As", display_as, kDisplay, 3);
  r.help("How the Scene view draws the object: shaded, as its edges only, or as its bounding box\n"
         "(Blender: Object > Viewport Display > Display As). Wire and Bounds objects are still selectable.");
  r.field("Show in Renders", show_in_renders);
  r.help("Off: the Game view and rendered images leave the object out, as Blender does for Boolean cutters\n"
         "(Object > Visibility > Renders).");
  if (r.serializing()) r.text("Mesh Maps", mesh_maps), r.text("Mesh Maps Key", mesh_maps_key);  // (task 0243: the record, not for hand editing)
  /* Shape keys (task 0135): files and hashes keep the weights as a list (whatever order the components come in, a
   * key's exact name); the Inspector and clips get a slider per key of the mesh (BlendShape/<name>). */
  if (r.serializing()) r.weight_list("Shape Weights", shape_weights);
  const MeshFilter *mf = owner && !r.serializing() ? owner->mesh_filter() : nullptr;
  const Mesh *mesh = mf && mf->mesh ? mf->mesh.get() : nullptr;
  for (size_t i = 0; mesh && i < mesh->shape_keys.size(); i++) {
    const std::string field = "BlendShape/" + mesh->shape_keys[i].name;
    float w = shape_weight(mesh->shape_keys[i].name);
    const float before = w;
    r.field(field.c_str(), w, 0.01f, 0.0f, 1.0f);
    if (i == 0) r.help("The mesh's shape keys (blend shapes, morphs): how much of each is added to the basis, before the skin\n"
                       "deforms it. Animatable (Add Property > MeshRenderer > BlendShape/...).");
    if (w != before) set_shape_weight(mesh->shape_keys[i].name, w);
  }
  r.field("Contribute GI", contribute_gi);
  r.help("Part of baked lighting (Unity: Static > Contribute Global Illumination). It bounces light onto the\n"
         "others and gets a lightmap when you press Generate Lighting (Window > Lighting). Leave it off for\n"
         "things that move.");
  if (contribute_gi || r.all_fields()) {
    r.field("Scale In Lightmap", scale_in_lightmap, 0.01f, 0.0f, 100.0f);
    r.help("How many lightmap texels this object gets, relative to the Lightmap Resolution (Unity: Scale In Lightmap).");
    static const char *receive[] = {"Lightmaps", "Light Probes"};
    r.enumeration("Receive GI", receive_gi, receive, 2);
    r.help("Where its indirect light comes from: its own lightmap, or the probe volume (Unity: Receive Global\n"
           "Illumination). Objects that don't contribute always use the probes.");
    r.field("Generate Lightmap UVs", generate_lightmap_uvs);
    r.help("On: lightmap UVs are made at bake time (Smart UV Project + Pack Islands). Off: the mesh's own UVs\n"
           "are used, so they must not overlap.");
  }
}

int default_lighting_update_mode() {
  const char *e = std::getenv("BLENDITY_LIGHTING_UPDATES");
  return e && (std::string(e) == "automatic" || std::string(e) == "auto") ? 1 : 0;
}

void LightingSettings::reflect(Reflector &r) {
  static const char *upd[] = {"Manual", "Automatic"};
  r.enumeration("Lighting Updates", update_mode, upd, 2);
  r.help("Manual: lighting stays as last computed while you move things (direct light and shadows stay live);\n"
         "Update Lighting (Ctrl+Shift+L) refreshes Realtime GI, Generate Lighting bakes. Automatic: Realtime GI\n"
         "follows every change and Auto Generate re-bakes.");
  r.field("Preview Lighting", preview);
  r.help("A rough progressive bake (a quarter of the resolution, 16 then 64 then 256 samples) half a second after\n"
         "changes stop, shown instead of the bake and never saved (like Bakery's Realtime Preview).");
  r.field("Baked Global Illumination", baked_gi);
  r.help("Generate Lighting bakes the lights set to Baked or Mixed, the sky and their bounces into lightmaps for\n"
         "objects marked Contribute GI (Unity: Lighting > Mixed Lighting > Baked Global Illumination).");
  static const char *modes[] = {"Baked Indirect"};
  r.enumeration("Lighting Mode", lighting_mode, modes, 1);
  r.help("Baked Indirect: Mixed lights keep their realtime light and shadows; only their bounced light is baked.");
  r.field("Lightmap Resolution", texels_per_unit, 0.5f, 0.01f, 1000.0f);
  r.help("Texels per unit (metre) of surface (Unity's default is 40; 80 shows a small emissive object's glow in detail).");
  r.field("Max Lightmap Size", max_size, 16, 4096);
  r.field("Lightmap Padding", padding, 0, 32);
  r.help("Texels kept between charts so they don't bleed into each other.");
  r.field("Direct Samples", direct_samples, 1, 4096);
  r.help("Shadow rays per texel for each Baked area light.");
  r.field("Indirect Samples", indirect_samples, 1, 65536);
  r.help("Rays per texel gathering bounced light and the sky. More is smoother and slower.");
  r.field("Bounces", bounces, 0, 16);
  r.field("Denoise", denoise);
  r.field("Indirect Intensity", indirect_intensity, 0.01f, 0.0f, 100.0f);
  r.field("Auto Generate", auto_generate);
  r.help("Re-bake by itself a second after changes stop (Unity: Auto Generate). Realtime GI covers the time in between.");
  r.field("Realtime GI", realtime_gi);
  r.help("Voxel-based global illumination (Thiedemann et al., I3D 2011): one bounce of the sun's light and the sky's\n"
         "occlusion, live in the rasterized views. Objects without a valid lightmap use it, and lightmapped ones get\n"
         "the bounce of lights the bake doesn't hold. Nothing to bake: moving lights and objects updates it.");
  if (realtime_gi || r.all_fields()) {
    static const char *res[] = {"64", "128", "256"};
    r.enumeration("Voxel Resolution", gi_resolution, res, 3);
    r.help("Voxel columns per side over the scene (128 deep). More is finer and slower.");
    r.field("GI Radius", gi_radius, 0.05f, 0.01f, 1000.0f);
    r.help("How far bounced light and sky occlusion reach, in metres.");
    r.field("GI Rays", gi_rays, 1, 32);
    r.field("GI Intensity", gi_intensity, 0.01f, 0.0f, 10.0f);
    r.field("Sky Occlusion", gi_sky_occlusion);
    r.field("Bounce", gi_bounce);
    static const char *ds[] = {"Half", "Quarter"};
    r.enumeration("GI Resolution", gi_downsample, ds, 2);
    r.help("The screen resolution the rays are traced at, blurred and upsampled to every pixel.");
    r.field("RSM Resolution", gi_rsm_resolution, 32, 2048);
    r.help("The sun's reflective shadow map: where its light lands, for the bounce.");
    r.field("Specular Occlusion", gi_specular_occlusion, 0.01f, 0.0f, 1.0f);
    static const char *dev[] = {"Auto", "CPU", "GPU"};
    r.enumeration("GI Device", gi_device, dev, 3);
    r.help("Where realtime lightmaps and live probes gather: Auto uses the GPU (Vulkan compute) when there is one,\n"
           "5 to 8 times faster, and a whole pass fits in a frame; the CPU is the fallback and the reference.");
    r.field("Realtime Resolution", realtime_resolution, 0.1f, 0.0f, 100.0f);
    r.help("Realtime lightmaps (Unity: Realtime Resolution): texels per metre on Contribute GI objects, lit by the voxel GI\n"
           "and read like a baked map, so moving the camera traces nothing and the look doesn't change with zoom.\n"
           "0: off (every object traced per pixel, at the GI Resolution).");
  }
  r.field("Probe Min Spacing", probe_min_spacing, 0.05f, 0.05f, 100.0f);
  r.help("Probe volumes (a Probe Volume component): metres between probes near geometry. The volume gets coarser\n"
         "in open space, up to the max spacing (Unity: Adaptive Probe Volumes' Min / Max Probe Spacing).");
  r.field("Probe Max Spacing", probe_max_spacing, 0.1f, 0.1f, 1000.0f);
  r.field("Probe Normal Bias", probe_normal_bias, 0.01f, 0.0f, 10.0f);
  r.help("Metres a shaded point moves along its normal before reading the probes, so probes behind the surface don't leak.");
  r.field("Probe View Bias", probe_view_bias, 0.01f, 0.0f, 10.0f);
  r.field("Probe Leak Reduction", probe_leak_reduction);
  r.help("Probes behind the surface being shaded (the other side of a thin wall) count for almost nothing\n"
         "(Unity APV: Leak Reduction Mode).");
  r.field("Show Probes", show_probes);
}

void ProbeVolume::reflect(Reflector &r) {
  r.field("Global", global);
  r.help("On: around everything that contributes to GI. Off: a box of the size below, centred on this object.");
  if (!global || r.all_fields()) r.field("Size", size);
}

void EnvironmentSettings::reflect(Reflector &r) {
  static const char *modes[] = {"Gradient (Unity default)", "Sky (Hosek-Wilkie)", "HDRI", "Color"};
  r.enumeration("Source", mode, modes, 4);
  r.help("Background and ambient light. Blender: World > Surface. Unity: Lighting > Environment.");
  const bool all = r.all_fields();
  if (all || mode == 0) {
    r.color("Sky Color", sky);
    r.color("Equator Color", equator);
    r.color("Ground Color", ground);
    if (r.group("Stylized Sky", false)) {  // (task 0248)
      r.field("Sun Disc", sun_disc, 0.1f, 0.0f, 30.0f);
      r.help("A painted sun on the gradient sky: the disc's size in degrees (0: none), toward the first Directional Light.");
      r.field("Sun Disc Softness", sun_disc_softness, 0.01f, 0.0f, 1.0f);
      r.color("Sun Disc Color", sun_disc_color);
      r.field("Horizon Band", horizon_band, 0.01f, 0.0f, 0.5f);
      r.help("A band of color along the horizon (its half-width as a share of the sky; 0: none): the warm line of a painted sky.");
      r.color("Horizon Band Color", horizon_band_color);
    }
    r.group_end();
  }
  if (r.group("Fog", false)) {  // (task 0248: in every renderer, by the view distance)
    r.field("Fog", fog);
    r.field("Fog Start", fog_start, 0.1f, 0.0f, 10000.0f);
    r.field("Fog End", fog_end, 0.1f, 0.0f, 10000.0f);
    r.color("Fog Color", fog_color);
    r.color("Fog Color Far", fog_color_far);
    r.help("Distance fog: a surface's color is mixed toward the ramp - Fog Color at Fog Start to Fog Color Far at Fog End and\n"
           "beyond - by its distance from the eye, in the Scene view, the GPU view and renders alike (a stylization: no scattering;\n"
           "the Retro camera filter's fog is a display effect of its own).");
  }
  r.group_end();
  if (all || mode == 1) {
    r.field("Turbidity", turbidity, 0.02f, 1.0f, 10.0f);
    r.help("Haze in the atmosphere (Hosek-Wilkie model). The sun direction comes from the first Directional Light.");
    r.field("Ground Albedo", ground_albedo, 0.01f, 0.0f, 1.0f);
  }
  if (all || mode == 2) {
    r.texture("HDRI", hdri);
    r.help("Equirectangular .hdr (Radiance) or LDR image. Blender: Environment Texture node.");
  }
  if (all || mode == 3) r.color("Color", color);
  r.field("Strength", strength, 0.01f, 0.0f, 100.0f);
  r.field("Rotation", rotation, 0.5f, -360.0f, 360.0f);
}

/* What a rasterized view reads of the render settings (the views' cache keys hash only these, so changing
 * the path tracer's samples doesn't redraw the Game view). */
void RenderSettings::reflect_raster(Reflector &r) {
  r.field("Resolution X", width, 16, 16384);
  r.field("Resolution Y", height, 16, 16384);
  r.field("Resolution %", percent, 1, 400);
  r.field("Anti-Aliasing (SSAA)", raster_aa, 1, 4);
  r.field("Shadows", shadows);
  r.field("Shadow Resolution", shadow_resolution, 256, 8192);
  static const std::vector<const char *> vts = [] {
    std::vector<const char *> v;
    for (const std::string &n : view_transform_names()) v.push_back(n.c_str());
    return v;
  }();
  r.enumeration("View Transform", view_transform, vts.data(), (int)vts.size());
  r.field("Exposure", exposure, 0.02f, -10.0f, 10.0f);
}

void RenderSettings::reflect(Reflector &r) {
  static const char *engines[] = {"Rasterized (EEVEE-like)", "Path Traced (Cycles-like)", "MoonRay (external)"};
  static const char *formats[] = {"PNG", "JPEG", "Radiance HDR", "OpenEXR (half float)"};
  r.enumeration("Render Engine", engine, engines, 3);
  r.help("Rasterized: real-time deferred PBR with shadow maps. Path Traced: unbiased Monte Carlo light transport.\n"
         "MoonRay: DreamWorks' production renderer, an optional download (Edit > Preferences > Renderers), for F12 renders;\n"
         "previews use the path tracer. Without it installed, F12 path traces.");
  if (!r.all_fields() && engine == 2) {  // (MoonRay takes these two; saved with the path tracer's below)
    r.samples("Samples", samples, 1, 65536);
    r.help("Samples per pixel. MoonRay takes their square root per axis (64 -> 8 x 8).");
    r.field("Max Bounces", max_bounces, 0, 64);
  }
  r.field("Resolution X", width, 16, 16384);
  r.field("Resolution Y", height, 16, 16384);
  r.field("Resolution %", percent, 1, 400);
  r.field("Preview Resolution %", preview_percent, 5, 100);
  r.help("Size of the Preview render, as a percentage of the output resolution.");
  r.samples("Preview Samples", preview_samples, 1, 65536);
  r.help("Path-traced samples for the Preview render (the rasterized engine ignores it).");
  r.field("Live Preview", live_preview);
  r.help("Re-render the preview automatically whenever the scene, camera or settings change.");
  r.field("Transparent", transparent);
  r.help("Film > Transparent: the background is left out of the render (transparent in a PNG or EXR) while the world\n"
         "still lights the scene. Blender: Render Properties > Film > Transparent.");
  if (r.all_fields() || engine == 1) {
    static const char *devices[] = {"CPU", "GPU Compute", "GPU + CPU (Auto)"};
    r.enumeration("Device", device, devices, 3);
    r.help("GPU Compute renders on the GPUs ticked under Render Devices (Vulkan: NVIDIA, AMD, Intel).\n"
           "GPU + CPU (Auto): both, each taking samples as fast as it renders them, so a slow card (a GTX 1050 Ti traces\n"
           "in software) costs nothing and a fast one does most of the work. Blender: Render Properties > Device.");
    if (r.all_fields() || device == 1 || device == 2) {
      if (r.all_fields() || device == 1) r.field("Also Use the CPU", gpu_with_cpu);  // (Auto always does)
      r.help("Combined rendering: the CPU renders samples alongside the GPUs and the results are averaged\n"
             "(Blender: tick the CPU in the Render Devices list).");
      r.field("Hardware Ray Tracing", hardware_rt);
      r.help("Use the GPU's ray tracing hardware (NVIDIA RT cores, AMD ray accelerators, Intel RTUs) when it has it,\n"
             "instead of traversing Blendity's BVH in a compute shader (Blender: OptiX vs CUDA, HIP RT vs HIP).");
      static const char *const kBackends[3] = {"Auto", "Vulkan", "OptiX"};
      r.enumeration("GPU Backend", gpu_backend, kBackends, 3);
      r.help("Which API drives an NVIDIA card (task 0170): OptiX or Vulkan (GPU Compute); Auto is GPU Compute for now. The\n"
             "two run one integrator and render the same picture; OptiX measured about three times slower (its rays leave\n"
             "the kernel and come back). OptiX needs Hardware Ray Tracing. Blender: Preferences > System > Cycles Render Devices.");
    }
    r.samples("Samples", samples, 1, 65536);
    r.help("Samples per pixel for the final render. Noise falls as 1/sqrt(samples): each doubling cuts it by ~30%.\nBlender: Sampling > Render > Max Samples.");
    if (r.all_fields()) r.samples("Viewport Samples", viewport_samples, 1, 65536);  // kept in files; the Scene view no longer path traces
    r.field("Max Bounces", max_bounces, 0, 64);
    r.field("Clamp Indirect", clamp_indirect, 0.1f, 0.0f, 1000.0f);
    r.help("Limits bright indirect samples to remove fireflies (0 = off). Blender: Light Paths > Clamping.");
    r.field("Denoise", denoise);
    static const char *denoisers[] = {"OpenImageDenoise", "A-Trous (built-in)"};
    r.enumeration("Denoiser", denoiser, denoisers, 2);
    r.help("OpenImageDenoise is Cycles' default AI denoiser (needs Blender's libraries). A-Trous is the built-in edge-avoiding filter.");
    if (r.all_fields() || denoiser == 0) {
      static const char *denoise_devices[] = {"Auto", "CPU", "GPU"};
      r.enumeration("Denoise Device", denoise_device, denoise_devices, 3);
      r.help("Where OpenImageDenoise runs. Auto: an NVIDIA (CUDA) or AMD (HIP) card when there is one, else the CPU.\n"
             "GPU falls back to the CPU without one or after an error. Blender: Preferences > System > Cycles Render Devices.");
    }
    r.field("Embree Ray Tracing", use_embree);
    r.help("Use Embree, Cycles' CPU ray tracing kernels, instead of Blendity's own BVH (needs Blender's libraries).");
    r.field("Path Guiding", path_guiding);
    r.help("Learns where light comes from and steers bounces there (OpenPGL, as Cycles' Light Paths > Path Guiding). Helps light that arrives indirectly or from small emissive surfaces.");
  }
  if (r.all_fields() || engine == 0) {
    r.field("Anti-Aliasing (SSAA)", raster_aa, 1, 4);
    r.field("Shadows", shadows);
    r.field("Shadow Resolution", shadow_resolution, 256, 8192);
  }
  /* Built-in curves, then Blender's OpenColorIO views when available. */
  static const std::vector<const char *> vts = [] {
    std::vector<const char *> v;
    for (const std::string &n : view_transform_names()) v.push_back(n.c_str());
    return v;
  }();
  r.enumeration("View Transform", view_transform, vts.data(), (int)vts.size());
  r.help("Tone mapping from scene light to display (FoCG ch. 20). Blender: Color Management > View Transform.");
  r.field("Exposure", exposure, 0.02f, -10.0f, 10.0f);
  r.enumeration("File Format", file_format, formats, 4);
  r.help("Radiance HDR and OpenEXR keep linear light (path-traced renders). OpenEXR needs Blender's libraries.");
  if (r.all_fields() || file_format == 1) r.field("JPEG Quality", jpeg_quality, 1, 100);
}

Material &MeshRenderer::main_material() {
  if (materials.empty() || !materials[0]) {
    materials.resize(std::max<size_t>(1, materials.size()));
    materials[0] = make_material("Material", Vec3(0.8f));
  }
  return *materials[0];
}

void Light::reflect(Reflector &r) {
  const bool all = r.all_fields();
  static const char *types[] = {"Directional", "Point", "Spot", "Area"};
  r.enumeration("Type", type, types, 4);
  r.help("Directional: the sun, parallel rays. Point: all directions from a point. Spot: a cone along the light's\n"
         "forward (blue) axis. Area: a glowing rectangle or disc that lights its front from its whole surface, like a\n"
         "quad with an emissive material: the inverse square from every point of it, soft shadows when path traced.\n"
         "Unity: Light Type. Blender: Sun / Point / Spot / Area.");
  if (type == 2 || all) {
    r.field("Spot Angle", spot_angle, 0.5f, 1.0f, 179.0f);
    r.help("The full width of the cone, in degrees (Unity's Spot Angle; Blender's Spot Size).");
    r.field("Inner Spot Angle", inner_spot_angle, 0.5f, 0.0f, 179.0f);
    r.help("Inside this cone the light is at full brightness; it fades out to the Spot Angle\n"
           "(Unity's Inner Spot Angle; Blender's Spot Blend).");
  }
  if (type == 3 || all) {
    static const char *shapes[] = {"Rectangle", "Disc"};
    r.enumeration("Shape", area_shape, shapes, 2);
    r.field("Width", area_width, 0.01f, 0.001f, 1000.0f);
    r.field("Height", area_height, 0.01f, 0.001f, 1000.0f);
  }
  r.color("Color", color);
  r.field("Use Color Temperature", use_temperature);
  r.help("Tint the light with a black body's color at the temperature below, as photographers and\n"
         "lighting artists do (Unity HDRP: Color Temperature; Blender: a Blackbody node).");
  if (use_temperature || all) {
    r.field("Temperature (K)", temperature, 25.0f, 1000.0f, 40000.0f);
    r.help("Kelvin: 1900 candle, 2700 tungsten bulb, 3500 halogen, 4000 moonlight / fluorescent,\n"
           "5500 noon sun, 6500 overcast sky (white), 9000+ blue sky in shade.");
  }
  r.field("Intensity", intensity, 0.01f, 0.0f, 8.0f);
  if (type == 3 || all)
    r.help("How brightly the panel glows, per square metre: a quad with an emissive material of strength 3.14 x this\n"
           "(pi times) gives the same light, so a bigger panel gives more light and softer shadows.");
  if (r.all_fields() || type != 0) r.field("Range", range, 0.05f, 0.01f, 1000.0f);
  if (type == 3 || all) r.help("Where an area light's light ends: it falls as the inverse square and fades out to nothing at Range.");
  else if (type != 0 || all) r.help("How far the light reaches (Unity's falloff: nothing beyond Range).");
  static const char *light_modes[] = {"Realtime", "Mixed", "Baked"};
  r.enumeration("Mode", mode, light_modes, 3);
  r.help("Unity's Light Mode. Realtime: lit every frame, not in baked lighting. Mixed: realtime light and shadows,\n"
         "its bounced light baked. Baked: exists only through the bake (lightmaps and probes), nothing drawn live;\n"
         "Preview Lighting shows it before a bake. The path tracer lights with every mode.");
}

Vec3 Light::final_color() const { return use_temperature ? Vec3(color.x, color.y, color.z) * kelvin_to_rgb(temperature) : color; }

Vec3 kelvin_to_rgb(float kelvin) {
  /* The Planckian locus (Kang et al. 2002, as used by colour scientists for
   * 1667 - 25000 K) to CIE xy, then XYZ with Y = 1, then linear sRGB. */
  const double t = std::max(1000.0, std::min(40000.0, (double)kelvin));
  const double t2 = t * t, t3 = t2 * t;
  const double x = t <= 4000.0 ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                               : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
  const double x2 = x * x, x3 = x2 * x;
  const double y = t <= 2222.0   ? -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683
                   : t <= 4000.0 ? -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867
                                 : 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
  const double X = x / y, Y = 1.0, Z = (1.0 - x - y) / y;
  double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
  double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
  double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
  r = std::max(0.0, r);
  g = std::max(0.0, g);
  b = std::max(0.0, b);
  const double m = std::max({r, g, b, 1e-9});
  return Vec3((float)(r / m), (float)(g / m), (float)(b / m));
}

/* Camera sensor sizes in mm (Blender: scripts/presets/camera; Unity's Sensor Type list). */
static const char *const kSensorPresets[] = {"Custom",
                                             "Full Frame 35mm (36 x 24)",
                                             "APS-C (23.6 x 15.6)",
                                             "APS-C Canon (22.3 x 14.9)",
                                             "Micro Four Thirds (17.3 x 13)",
                                             "Super 35 (24.89 x 18.66)",
                                             "1 inch (13.2 x 8.8)",
                                             "Medium Format (43.8 x 32.9)",
                                             "IMAX 65mm (70.41 x 52.63)",
                                             "Phone (6.17 x 4.55)"};
static const float kSensorSizes[][2] = {{36, 24},       {36, 24},       {23.6f, 15.6f}, {22.3f, 14.9f},  {17.3f, 13.0f},
                                        {24.89f, 18.66f}, {13.2f, 8.8f}, {43.8f, 32.9f}, {70.41f, 52.63f}, {6.17f, 4.55f}};
static const char *const kAspectModes[] = {"Render Resolution", "Sensor", "16:9", "16:10", "4:3", "3:2", "1:1",
                                           "21:9", "2.39:1 (Anamorphic)", "9:16 (Portrait)", "Custom"};
static const float kAspects[] = {0, 0, 16.0f / 9.0f, 1.6f, 4.0f / 3.0f, 1.5f, 1.0f, 21.0f / 9.0f, 2.39f, 9.0f / 16.0f, 0};
constexpr int kSensorPresetCount = sizeof(kSensorPresets) / sizeof(kSensorPresets[0]);
constexpr int kAspectModeCount = sizeof(kAspectModes) / sizeof(kAspectModes[0]);

void Camera::reflect(Reflector &r) {
  const bool all = r.all_fields();
  static const char *clear[] = {"Skybox", "Solid Color"};
  r.enumeration("Clear Flags", clear_flags, clear, 2);
  if (clear_flags == 1 || all) r.color("Background", background);
  r.field("Orthographic", orthographic);
  if (orthographic || all) r.field("Size", ortho_size, 0.05f, 0.01f, 1000.0f);
  if (!orthographic || all) {
    r.field("Physical Camera", physical);
    r.help("Set the view from a real lens and sensor: focal length, sensor size and lens shift.\n"
           "Unity: Physical Camera. Blender: Lens Unit = Millimeters.");
  }
  if ((!orthographic && !physical) || all) {
    r.field("Field Of View", fov, 0.2f, 1.0f, 179.0f);
    r.help("Vertical field of view in degrees (FoCG ch. 8.5).");
  }
  if ((!orthographic && physical) || all) {
    r.field("Focal Length (mm)", focal_length, 0.5f, 1.0f, 5000.0f);
    r.help("Longer lenses see less of the scene (zoom in) and blur the background more.\n"
           "The field of view follows from this and the sensor size.");
    r.enumeration("Sensor", sensor_preset, kSensorPresets, kSensorPresetCount);
    r.help("Sensor (film gate) size. Smaller sensors crop the image (a longer effective focal length).");
    if (sensor_preset > 0 && sensor_preset < kSensorPresetCount) {
      sensor_width = kSensorSizes[sensor_preset][0];
      sensor_height = kSensorSizes[sensor_preset][1];
    }
    if (sensor_preset == 0 || all) {
      r.field("Sensor Width (mm)", sensor_width, 0.1f, 1.0f, 200.0f);
      r.field("Sensor Height (mm)", sensor_height, 0.1f, 1.0f, 200.0f);
    }
    static const char *fit[] = {"Auto", "Horizontal", "Vertical"};
    r.enumeration("Sensor Fit", sensor_fit, fit, 3);
    r.help("Which image side the sensor spans. Auto: the sensor width covers the larger side (Blender).\n"
           "Unity calls this Gate Fit.");
    r.field("Lens Shift X", shift_x, 0.005f, -2.0f, 2.0f);
    r.field("Lens Shift Y", shift_y, 0.005f, -2.0f, 2.0f);
    r.help("Moves the image without tilting the camera (architectural shots keep verticals straight).\n"
           "In fractions of the image width / height.");
  }
  r.enumeration("Aspect Ratio", aspect_mode, kAspectModes, kAspectModeCount);
  r.help("The shape of the image this camera renders. Render Resolution keeps Render Settings' width and height;\n"
         "anything else keeps the width and sets the height (the Game view letterboxes to it).");
  if (aspect_mode == kAspectModeCount - 1 || all) r.field("Custom Aspect", custom_aspect, 0.01f, 0.1f, 10.0f);
  r.field("Near", near_clip, 0.01f, 0.001f, 100.0f);
  r.field("Far", far_clip, 1.0f, 0.1f, 100000.0f);
  r.field("Depth of Field", dof);
  r.help("Blur what is nearer or farther than the focus distance, like a real lens (renders, the Game view and the Camera Preview).\n"
         "Blender: Camera > Depth of Field. Unity: HDRP Physical Camera aperture.");
  if (dof || all) {
    r.field("Focus Distance (m)", focus_distance, 0.05f, 0.01f, 100000.0f);
    r.help("How far in front of the camera things are sharp. Nearer things blur as well as farther ones:\n"
           "focus on something behind another object and the one in front goes soft.");
    r.field("Keep Focus on Point", focus_track);
    r.help("Pick Focus Point turns this on: the picked point stays in focus while the camera moves\n"
           "(Blender: Depth of Field > Focus Object). Turn it off to set the distance by hand.");
    if (focus_track || all) r.field("Focus Point", focus_point);
    r.field("F-Stop", f_stop, 0.05f, 0.1f, 128.0f);
    r.help("Aperture as a focal ratio: lower numbers (f/1.4) blur more, higher ones (f/16) keep more sharp.\n"
           "The aperture's diameter is focal length / f-stop.");
    r.field("Aperture Blades", blades, 0, 16);
    r.help("0: a round aperture. 3 or more: polygonal bokeh with that many sides.");
    r.field("Blade Rotation", blade_rotation, 0.5f, -180.0f, 180.0f);
  }
  r.field("Render in Sequence", in_sequence);
  r.help("Render Camera Sequence (Render window) renders every camera with this on, one after another,\n"
         "each to its own image: a shot list of the scene from different angles.");
  if (in_sequence || all) {
    r.field("Sequence Order", sequence_order, -1000, 1000);
    r.help("Lower numbers render first; cameras with the same number go in Hierarchy order.");
  }
  r.field("Physical Exposure", physical_exposure);
  r.help("Brightness from ISO, shutter speed and f-stop, like a real camera. 0 stops at ISO 100, 1/60 s, f/2.8;\n"
         "each doubling of ISO or exposure time adds a stop. Unity: Physical Camera exposure.");
  if (physical_exposure || all) {
    r.field("ISO", iso, 10.0f, 1.0f, 409600.0f);
    r.field("Shutter Speed (1/s)", shutter, 1.0f, 0.001f, 100000.0f);
    if (!dof && !all) r.field("F-Stop", f_stop, 0.05f, 0.1f, 128.0f);  // shown with Depth of Field otherwise
  }
  /* Composition guides (task 0033): drawn over the views, so not part of the render hash. Keep this the last call:
   * anything a render reads goes above it. */
  if (!r.hashing()) reflect_guides(r);
}

void Camera::reflect_guides(Reflector &r) {
  const bool all = r.all_fields();
  /* One drop-down in the Inspector (task 0145), closed until wanted: the guides are a dozen rows. */
  if (!r.group("Composition Guides", false)) {
    r.group_end();
    return;
  }
  r.field("Show Guides", show_guides);
  r.help("Composition guides over what this camera sees in the Game view, the Camera Preview and while piloting\n"
         "(never in renders). Blender: Camera > Viewport Display > Composition Guides. Game view: the Guides menu.");
  if (show_guides || all) {
    r.field("Thirds", guide_thirds);
    r.help("The rule of thirds: two lines each way at a third of the frame; put subjects on them or where they cross.");
    r.field("Center", guide_center);
    r.field("Center Diagonal", guide_center_diagonal);
    r.field("Golden Ratio", guide_golden);
    r.help("Lines at 0.382 and 0.618 of the frame (the golden section).");
    r.field("Golden Triangle A", guide_golden_tri_a);
    r.field("Golden Triangle B", guide_golden_tri_b);
    r.help("A diagonal and, from the other two corners, lines square to it running on to the far edge (Blender's;\n"
           "A: the diagonal rising to the right, B: falling).");
    r.field("Harmonious Triangle A", guide_harmony_a);
    r.field("Harmonious Triangle B", guide_harmony_b);
    r.help("A diagonal and lines from the other two corners to the golden-section points (0.382 / 0.618) of the far\n"
           "edge (Blender's).");
    r.field("Title Safe", guide_title_safe);
    if (guide_title_safe || all) {
      r.field("Title Safe X", title_safe_x, 0.005f, 0.0f, 1.0f);
      r.field("Title Safe Y", title_safe_y, 0.005f, 0.0f, 1.0f);
    }
    r.field("Action Safe", guide_action_safe);
    if (guide_action_safe || all) {
      r.field("Action Safe X", action_safe_x, 0.005f, 0.0f, 1.0f);
      r.field("Action Safe Y", action_safe_y, 0.005f, 0.0f, 1.0f);
    }
    r.color_alpha("Guide Color", guide_color, "Guide Opacity", guide_opacity);
  }
  r.group_end();
}

float Camera::image_aspect(float render_aspect) const {
  if (aspect_mode <= 0 || aspect_mode >= kAspectModeCount) return render_aspect;
  if (aspect_mode == 1) return sensor_width / std::max(1e-3f, sensor_height);
  if (aspect_mode == kAspectModeCount - 1) return std::max(0.05f, custom_aspect);
  return kAspects[aspect_mode];
}

float Camera::vertical_fov_deg(float aspect) const {
  if (!physical) return fov;
  const float f = std::max(0.1f, focal_length);
  int fit = sensor_fit;
  if (fit == 0) fit = aspect >= 1.0f ? 1 : 2;  // Auto: the sensor width spans the larger side
  const float size = sensor_fit == 2 ? sensor_height : sensor_width;
  float half = std::atan(size / (2.0f * f));
  if (fit == 1) half = std::atan(std::tan(half) / std::max(1e-3f, aspect));  // horizontal -> vertical
  return std::min(179.0f, 2.0f * half * kRad2Deg);
}

float Camera::focal_length_mm(float aspect) const {
  if (physical) return focal_length;
  /* A field-of-view camera as a lens on a 36 mm sensor (Blender's default). */
  float half_v = fov * 0.5f * kDeg2Rad;
  float half = aspect >= 1.0f ? std::atan(std::tan(half_v) * aspect) : half_v;
  return 36.0f / (2.0f * std::tan(std::max(1e-4f, half)));
}

Mat4 Camera::projection(float aspect) const {
  Mat4 p = orthographic ? Mat4::ortho(ortho_size, aspect, near_clip, far_clip)
                        : Mat4::perspective(vertical_fov_deg(aspect) * kDeg2Rad, aspect, near_clip, far_clip);
  if (physical && !orthographic && (shift_x != 0.0f || shift_y != 0.0f)) {
    /* Off-axis projection: the image moves by the shift, the view direction doesn't. */
    p.m[8] -= 2.0f * shift_x;
    p.m[9] -= 2.0f * shift_y;
  }
  return p;
}

float Camera::aperture_radius(float aspect) const {
  if (!dof || orthographic || f_stop <= 0.0f) return 0.0f;
  return focal_length_mm(aspect) / (2.0f * f_stop) * 0.001f;  // mm -> m (Cycles: blender_camera.cpp)
}

float Camera::exposure_stops() const {
  if (!physical_exposure) return 0.0f;
  /* EV relative to ISO 100, 1/60 s, f/2.8: log2(t * S/100 / N^2). */
  const float t = 1.0f / std::max(1e-3f, shutter), n = std::max(0.1f, f_stop);
  return std::log2(t * (iso / 100.0f) / (n * n)) - std::log2((1.0f / 60.0f) / (2.8f * 2.8f));
}

/* ------------------------------------------------------- Camera filters */

/* ---------------------------------------------------------- Filter effects */

const std::vector<FilterEffectInfo> &filter_effect_infos() {
  /* Menu order: by category, then as listed. */
  static const std::vector<FilterEffectInfo> infos = {
      {RetroConsoleFilter::kName, "Retro Console",
       "Render the way an old console drew 3D: PlayStation, Nintendo 64, Sega Saturn or a DOS PC (resolution, wobbling vertices, "
       "warping textures, texture filtering, color depth and dither).",
       [] { return std::make_unique<RetroConsoleFilter>(); }},
      {ColorGradingEffect::kName, "Color", "Exposure, contrast, saturation, white balance and lift / gamma / gain, in linear light.",
       [] { return std::make_unique<ColorGradingEffect>(); }},
      {PosterizeEffect::kName, "Color", "Fewer shades per channel: flat bands of color.", [] { return std::make_unique<PosterizeEffect>(); }},
      {GrayscaleEffect::kName, "Color", "Black and white, or a warm old-photo sepia.", [] { return std::make_unique<GrayscaleEffect>(); }},
      {InvertEffect::kName, "Color", "A negative image.", [] { return std::make_unique<InvertEffect>(); }},
      {ColorRampEffect::kName, "Color", "The picture's brightness through a few colors (Blender's Color Ramp): flat cel tones or a gradient map.",
       [] { return std::make_unique<ColorRampEffect>(); }},
      {VignetteEffect::kName, "Lens", "Darker (or tinted) corners, drawing the eye to the middle.", [] { return std::make_unique<VignetteEffect>(); }},
      {ChromaticAberrationEffect::kName, "Lens", "Red and blue fringes toward the edges, as from a cheap lens.",
       [] { return std::make_unique<ChromaticAberrationEffect>(); }},
      {FilmGrainEffect::kName, "Lens", "Film-like noise, strongest in the midtones; it moves in Play mode.", [] { return std::make_unique<FilmGrainEffect>(); }},
      {LensDistortionEffect::kName, "Lens", "Barrel (fisheye-like, below 0) or pincushion (above 0) bending of straight lines.",
       [] { return std::make_unique<LensDistortionEffect>(); }},
      {PixelateEffect::kName, "Stylize", "Blocks of one color.", [] { return std::make_unique<PixelateEffect>(); }},
      {EdgeOutlineEffect::kName, "Stylize", "Lines where objects meet and where the depth jumps: a toon / sketch look.",
       [] { return std::make_unique<EdgeOutlineEffect>(); }},
      {CrtEffect::kName, "Stylize", "An old TV: scanlines, a curved tube, phosphor stripes and flicker.", [] { return std::make_unique<CrtEffect>(); }},
      {SharpenEffect::kName, "Stylize", "Crisper edges (an unsharp mask).", [] { return std::make_unique<SharpenEffect>(); }},
      {HalftoneEffect::kName, "Stylize", "Comic / manga dots: a dot screen whose dots grow with the darkness (Mono ink on paper, or Color).",
       [] { return std::make_unique<HalftoneEffect>(); }},
      {HatchingEffect::kName, "Stylize", "Pen hatching: lines that cross more densely in darker areas.", [] { return std::make_unique<HatchingEffect>(); }},
      {LineArtEffect::kName, "Stylize", "Ink lines on silhouettes, where objects meet, and on creases sharper than an angle (a manga / anime line).",
       [] { return std::make_unique<LineArtEffect>(); }},
      {PaperEffect::kName, "Stylize", "A paper texture under the picture: blotches, fibres and grain.", [] { return std::make_unique<PaperEffect>(); }},
      {AnimeEffect::kName, "Stylize", "The anime look in one effect: ink lines on silhouettes and creases, a little more saturation and contrast, a soft bloom.",
       [] { return std::make_unique<AnimeEffect>(); }},
      {MangaEffect::kName, "Stylize", "A printed manga page in one effect: greys, halftone dots, ink lines and paper.",
       [] { return std::make_unique<MangaEffect>(); }},
      {BloomEffect::kName, "Bloom & glow", "Light brighter than a threshold spills into the pixels around it: glowing lights, emissive materials, sun glints.",
       [] { return std::make_unique<BloomEffect>(); }},
  };
  return infos;
}

const FilterEffectInfo *find_filter_effect_info(const std::string &name) {
  for (const FilterEffectInfo &i : filter_effect_infos())
    if (i.name == name) return &i;
  return nullptr;
}

std::unique_ptr<FilterEffect> create_filter_effect(const std::string &name) {
  const FilterEffectInfo *i = find_filter_effect_info(name);
  return i ? i->create() : nullptr;
}

namespace {
/* Forwards every field with a prefix on its name ("E0 Width"), so a list of effects shares one
 * flat key space in files, hashes and `set`. */
struct PrefixReflector : Reflector {
  Reflector &in;
  std::string prefix;
  PrefixReflector(Reflector &r, std::string p) : in(r), prefix(std::move(p)) {}
  std::string n(const char *name) const { return prefix + name; }
  bool group(const char *name, bool open) override { return in.group(name, open); }
  void group_end() override { in.group_end(); }
  void field(const char *name, float &v, float speed, float mn, float mx) override { in.field(n(name).c_str(), v, speed, mn, mx); }
  void field(const char *name, int &v, int mn, int mx) override { in.field(n(name).c_str(), v, mn, mx); }
  void samples(const char *name, int &v, int mn, int mx) override { in.samples(n(name).c_str(), v, mn, mx); }
  void field(const char *name, bool &v) override { in.field(n(name).c_str(), v); }
  void field(const char *name, Vec3 &v) override { in.field(n(name).c_str(), v); }
  void color(const char *name, Vec3 &v) override { in.color(n(name).c_str(), v); }
  void color_alpha(const char *name, Vec3 &rgb, const char *alpha_name, float &alpha) override {
    in.color_alpha(n(name).c_str(), rgb, n(alpha_name).c_str(), alpha);
  }
  void enumeration(const char *name, int &v, const char *const *options, int count) override {
    in.enumeration(n(name).c_str(), v, options, count);
  }
  void text(const char *name, std::string &v) override { in.text(n(name).c_str(), v); }
  void object(const char *name, std::string &v, const char *type) override { in.object(n(name).c_str(), v, type); }
  void asset(const char *name, std::string &p, const char *ext, const char *kind) override { in.asset(n(name).c_str(), p, ext, kind); }
  void mesh(const char *name, MeshPtr &m) override { in.mesh(n(name).c_str(), m); }
  void help(const char *h) override { in.help(h); }
  void texture(const char *name, TextureRef &t) override { in.texture(n(name).c_str(), t); }
  void material_list(const char *name, std::vector<MaterialPtr> &m) override { in.material_list(n(name).c_str(), m); }
  bool all_fields() const override { return in.all_fields(); }
  bool hashing() const override { return in.hashing(); }
};
}  // namespace

void Reflector::filter_effects(const char *name, FilterEffectList &effects) {
  /* The types, ';'-separated (names have spaces). */
  std::string types;
  for (const auto &e : effects) types += std::string(e->type_name()) + ";";
  const std::string before = types;
  text(name, types);
  if (types != before) {  // a reader brought another list: rebuild it (unknown types are skipped)
    FilterEffectList fresh;
    size_t a = 0;
    while (a < types.size()) {
      size_t b = types.find(';', a);
      if (b == std::string::npos) b = types.size();
      if (b > a)
        if (auto e = create_filter_effect(types.substr(a, b - a))) fresh.push_back(std::move(e));
      a = b + 1;
    }
    effects = std::move(fresh);
  }
  for (size_t i = 0; i < effects.size(); i++) {
    PrefixReflector p(*this, "E" + std::to_string(i) + " ");
    p.field("Enabled", effects[i]->enabled);
    effects[i]->reflect(p);
  }
}

CameraFilters::CameraFilters(const CameraFilters &o) : ComponentBase<CameraFilters, CameraFilter>(o) {
  for (const auto &e : o.effects) effects.push_back(e->clone());
}

CameraFilters &CameraFilters::operator=(const CameraFilters &o) {
  if (this == &o) return *this;
  ComponentBase<CameraFilters, CameraFilter>::operator=(o);
  effects.clear();
  for (const auto &e : o.effects) effects.push_back(e->clone());
  return *this;
}

void CameraFilters::reflect(Reflector &r) { r.filter_effects("Effects", effects); }

void CameraFilters::contribute(FilterStack &s) const {
  for (const auto &e : effects)
    if (e->enabled) e->contribute(s);
}

FilterEffect *CameraFilters::add(const std::string &type) {
  auto e = create_filter_effect(type);
  if (!e) return nullptr;
  effects.push_back(std::move(e));
  return effects.back().get();
}

void RetroConsoleFilter::apply_preset(int c) {
  applied_console = c;
  screen_door = false;
  fog = false;
  switch (c) {
    default:
    case PS1:
      /* Sony PlayStation (1994): 320 x 240, vertices at whole pixels, affine texture mapping,
       * nearest texels from at most 256 x 256 pages with no mipmaps, 15-bit colour through the
       * GPU's 4x4 dither. No fog by default (games faked it per vertex). */
      width = 320, height = 240, fit = 0;
      vertex_snap = true, snap_grid = 1.0f, affine_textures = true;
      texture_filter = Nearest, max_texture_size = 256, mipmaps = false;
      color_depth = RetroImageParams::Bits15, dither = RetroImageParams::Ps1;
      break;
    case N64:
      /* Nintendo 64 (1996): 320 x 240, sub-pixel vertices and perspective-correct textures, but
       * tiny textures (4 KB of texture memory: about 64 x 64) smoothed by the RDP's 3-point
       * filter, with mipmaps, 16-bit colour with a dither, and fog hiding the draw distance. */
      width = 320, height = 240, fit = 0;
      vertex_snap = false, snap_grid = 1.0f, affine_textures = false;
      texture_filter = ThreePoint, max_texture_size = 64, mipmaps = true;
      color_depth = RetroImageParams::Bits15, dither = RetroImageParams::Bayer4;
      fog = true, fog_start = 15.0f, fog_end = 60.0f, fog_color = {0.45f, 0.5f, 0.6f};
      break;
    case Saturn:
      /* Sega Saturn (1994): 320 x 224, quads at whole pixels with affine (forward) texture
       * mapping, nearest texels, 15-bit colour without dither, and see-through surfaces as a
       * checkerboard mesh (VDP1's half-transparency was slow, so games used the mesh). */
      width = 320, height = 224, fit = 0;
      vertex_snap = true, snap_grid = 1.0f, affine_textures = true;
      texture_filter = Nearest, max_texture_size = 256, mipmaps = false;
      color_depth = RetroImageParams::Bits15, dither = RetroImageParams::NoDither;
      screen_door = true;
      break;
    case DOS:
      /* A DOS PC with VGA (early 1990s software 3D): 320 x 200 in 256 colours (mode 13h),
       * affine texture mapping, nearest texels, an ordered dither into the palette. */
      width = 320, height = 200, fit = 0;
      vertex_snap = false, snap_grid = 1.0f, affine_textures = true;
      texture_filter = Nearest, max_texture_size = 256, mipmaps = false;
      color_depth = RetroImageParams::Palette256, dither = RetroImageParams::Bayer4;
      break;
  }
}

void RetroConsoleFilter::reflect(Reflector &r) {
  const bool all = r.all_fields();
  static const char *consoles[] = {"PlayStation (PS1)", "Nintendo 64", "Sega Saturn", "DOS PC (VGA)"};
  r.enumeration("Console", console, consoles, 4);
  r.help("Fills in the fields below with that console's look. Change any of them afterwards.");
  /* Read first, so a preset chosen here (or loaded from a file) is filled in before the fields:
   * a file's own field values then override it. */
  if (console != applied_console) apply_preset(console);
  r.field("Width", width, 16, 4096);
  r.field("Height", height, 16, 4096);
  r.help("The resolution the camera renders at, scaled up with square pixels.\n"
         "PS1 and N64 games mostly ran at 320 x 240, the Saturn at 320 x 224, DOS games at 320 x 200.");
  static const char *fits[] = {"Fill View", "Letterbox"};
  r.enumeration("Fit", fit, fits, 2);
  r.help("Fill View: keep the height and widen to the view's shape (square pixels).\n"
         "Letterbox: the console's own frame (4:3 for 320 x 240) with black bars.");
  r.field("Vertex Snap", vertex_snap);
  r.help("Round every vertex to whole pixels, as the PS1 and Saturn did: edges and shapes jitter as things move.");
  if (vertex_snap || all) r.field("Snap Grid (px)", snap_grid, 0.05f, 0.25f, 16.0f);
  r.field("Affine Textures", affine_textures);
  r.help("Map textures without perspective correction (PS1, Saturn, early PC 3D): they bend and swim on polygons\n"
         "seen at an angle, most on big ones close to the camera. Rasterized views only.");
  static const char *filters[] = {"As Material", "Nearest", "Linear", "Trilinear", "3-Point (N64)"};
  r.enumeration("Texture Filter", texture_filter, filters, 5);
  r.help("3-Point: the N64's filter, blending three texels instead of four (a slightly jagged smoothness).");
  r.field("Max Texture Size", max_texture_size, 0, 16384);
  r.help("Textures larger than this are drawn from a smaller copy (their mipmap), like a console's small texture memory.\n0: no limit.");
  r.field("Mipmaps", mipmaps);
  r.help("Off: one texture size at every distance (the PS1 had no mipmaps): far textures shimmer.");
  static const char *depths[] = {"24-bit", "15-bit", "256 colors"};
  r.enumeration("Colour Depth", color_depth, depths, 3);
  r.help("15-bit: 32 levels per channel (PS1, N64, Saturn). 256 colors: a fixed VGA-style palette (DOS).");
  if (color_depth != RetroImageParams::Full || all) {
    static const char *dithers[] = {"Off", "PS1 4x4", "Bayer 4x4"};
    r.enumeration("Dither", dither, dithers, 3);
    r.help("Hides color banding with a fixed pattern (the PS1's own matrix, or Bayer's).");
  }
  r.field("Screen-Door Transparency", screen_door);
  r.help("Draw see-through surfaces as a checkerboard of opaque pixels instead of blending (the Saturn's mesh).");
  r.field("Fog", fog);
  r.help("Fade geometry toward a color with distance, hiding a short draw distance. The sky is left alone:\n"
         "set the camera's Background to the fog color for a classic foggy horizon. Not in path-traced renders.");
  if (fog || all) {
    r.field("Fog Start", fog_start, 0.1f, 0.0f, 100000.0f);
    r.field("Fog End", fog_end, 0.1f, 0.0f, 100000.0f);
    r.color("Fog Colour", fog_color);
  }
}

void RetroConsoleFilter::contribute(FilterStack &s) const {
  if (vertex_snap && finite_bits(snap_grid) && snap_grid > 0.0f) s.vertex_snap = snap_grid;
  if (affine_textures) s.affine_uv = true;
  if (screen_door) s.screen_door = true;
  static const int kFilter[] = {-1, (int)TexFilter::Closest, (int)TexFilter::Linear, (int)TexFilter::Trilinear, (int)TexFilter::ThreePoint};
  if (texture_filter > 0 && texture_filter < 5) s.tex.filter = kFilter[texture_filter];
  if (max_texture_size > 0) s.tex.max_size = s.tex.max_size > 0 ? std::min(s.tex.max_size, max_texture_size) : max_texture_size;
  if (!mipmaps) s.tex.mipmaps = false;
  if (width > 0 && height > 0) {
    s.width = width;
    s.height = height;
    s.fit = fit == 1 ? FilterStack::Letterbox : FilterStack::Fill;
  }
  RetroImageParams p;
  p.color_depth = color_depth;
  p.dither = dither;
  p.fog = fog;
  p.fog_start = fog_start;
  p.fog_end = fog_end;
  p.fog_color = {linear_to_srgb(fog_color.x), linear_to_srgb(fog_color.y), linear_to_srgb(fog_color.z)};  // the pass works on display colours
  if (p.fog || p.color_depth != RetroImageParams::Full)
    s.passes.push_back([p](RenderTarget &rt, const FilterFrame *frame) { apply_retro_image(rt, p, frame); });
}

/* Color, Lens and Stylize (task 0006). Colours in the Inspector are linear; the passes work on
 * display colours. */
static Vec3 display_color(Vec3 c) { return {linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z)}; }

void ColorGradingEffect::reflect(Reflector &r) {
  r.field("Exposure", exposure, 0.02f, -10.0f, 10.0f);
  r.help("In stops: +1 doubles the light, -1 halves it.");
  r.field("Contrast", contrast, 0.01f, -1.0f, 1.0f);
  r.field("Saturation", saturation, 0.01f, -1.0f, 1.0f);
  r.help("-1: grey. 0: unchanged. 1: twice as vivid.");
  r.field("Temperature", temperature, 0.01f, -1.0f, 1.0f);
  r.help("Warmer (orange) or cooler (blue) white balance.");
  r.field("Tint", tint, 0.01f, -1.0f, 1.0f);
  r.help("Toward magenta (+) or green (-).");
  r.color("Lift", lift);
  r.help("Raises the shadows by color (Lift / Gamma / Gain, as in color grading suites). Black: no change.");
  r.field("Gamma", gamma);
  r.help("Bends the midtones per channel (1: no change; above 1 brighter).");
  r.field("Gain", gain);
  r.help("Scales the highlights per channel (1: no change).");
}
void ColorGradingEffect::contribute(FilterStack &s) const {
  ColorGradingParams p;
  p.exposure = exposure, p.contrast = contrast, p.saturation = saturation, p.temperature = temperature, p.tint = tint;
  p.lift = lift, p.gamma = gamma, p.gain = gain;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *) { apply_color_grading(rt, p); });
}

void PosterizeEffect::reflect(Reflector &r) {
  r.field("Levels", levels, 2, 256);
  r.help("Shades per color channel (256: unchanged). Few levels give flat, poster-like bands.");
}
void PosterizeEffect::contribute(FilterStack &s) const {
  const int l = levels;
  s.passes.push_back([l](RenderTarget &rt, const FilterFrame *) { apply_posterize(rt, l); });
}

void GrayscaleEffect::reflect(Reflector &r) {
  static const char *modes[] = {"Grayscale", "Sepia"};
  r.enumeration("Mode", mode, modes, 2);
  r.field("Amount", amount, 0.01f, 0.0f, 1.0f);
}
void GrayscaleEffect::contribute(FilterStack &s) const {
  const bool sepia = mode == 1;
  const float a = amount;
  s.passes.push_back([sepia, a](RenderTarget &rt, const FilterFrame *) { apply_grayscale(rt, sepia, a); });
}

void InvertEffect::reflect(Reflector &r) { r.field("Amount", amount, 0.01f, 0.0f, 1.0f); }
void InvertEffect::contribute(FilterStack &s) const {
  const float a = amount;
  s.passes.push_back([a](RenderTarget &rt, const FilterFrame *) { apply_invert(rt, a); });
}

void VignetteEffect::reflect(Reflector &r) {
  r.field("Intensity", intensity, 0.01f, 0.0f, 1.0f);
  r.field("Smoothness", smoothness, 0.01f, 0.01f, 1.0f);
  r.help("How far in from the corners the darkening reaches, and how softly.");
  r.field("Roundness", roundness, 0.01f, 0.0f, 1.0f);
  r.help("1: a circle. 0: follows the frame's shape.");
  r.color("Colour", color);
}
void VignetteEffect::contribute(FilterStack &s) const {
  VignetteParams p;
  p.intensity = intensity, p.smoothness = smoothness, p.roundness = roundness, p.color = display_color(color);
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *) { apply_vignette(rt, p); });
}

void ChromaticAberrationEffect::reflect(Reflector &r) {
  r.field("Intensity", intensity, 0.01f, 0.0f, 1.0f);
  r.help("Color fringes toward the edges, as from a cheap lens.");
}
void ChromaticAberrationEffect::contribute(FilterStack &s) const {
  const float k = intensity;
  s.passes.push_back([k](RenderTarget &rt, const FilterFrame *) { apply_chromatic_aberration(rt, k); });
}

void FilmGrainEffect::reflect(Reflector &r) {
  r.field("Intensity", intensity, 0.01f, 0.0f, 1.0f);
  r.field("Size", size, 0.05f, 1.0f, 16.0f);
  r.help("Grain size in pixels.");
  r.field("Response", response, 0.01f, 0.0f, 1.0f);
  r.help("1: grain mostly in the midtones, as on film. 0: the same everywhere.\nThe grain moves in Play mode; elsewhere it holds still.");
}
void FilmGrainEffect::contribute(FilterStack &s) const {
  GrainParams p;
  p.intensity = intensity, p.size = size, p.response = response;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_film_grain(rt, p, f); });
}

void LensDistortionEffect::reflect(Reflector &r) {
  r.field("Intensity", intensity, 0.01f, -1.0f, 1.0f);
  r.help("Below 0: barrel (straight lines bow outward, like a wide or fisheye lens). Above 0: pincushion (they bow inward).");
  r.field("Scale", scale, 0.01f, 0.1f, 10.0f);
  r.help("Zoom, to push the black corners out of the frame.");
}
void LensDistortionEffect::contribute(FilterStack &s) const {
  const float k = intensity, sc = scale;
  s.passes.push_back([k, sc](RenderTarget &rt, const FilterFrame *) { apply_lens_distortion(rt, k, sc); });
}

void PixelateEffect::reflect(Reflector &r) {
  r.field("Cell Size", cell_size, 1, 512);
  r.help("Pixels per block. (Retro Console's resolution renders fewer pixels instead; this keeps the full render.)");
}
void PixelateEffect::contribute(FilterStack &s) const {
  const int n = cell_size;
  s.passes.push_back([n](RenderTarget &rt, const FilterFrame *f) { apply_pixelate(rt, n, f); });
}

void EdgeOutlineEffect::reflect(Reflector &r) {
  r.color("Colour", color);
  r.field("Thickness", thickness, 1, 8);
  r.field("Depth Sensitivity", depth_sensitivity, 0.005f, 0.001f, 1.0f);
  r.help("How big a jump in distance (as a fraction) draws a line: smaller finds more creases.");
  r.field("Object Edges", object_edges);
  r.help("Also outline where one object meets another.\nThe whole effect needs the rasterizer's depth: path-traced renders skip it.");
}
void EdgeOutlineEffect::contribute(FilterStack &s) const {
  OutlineParams p;
  p.color = display_color(color), p.thickness = thickness, p.depth_sensitivity = depth_sensitivity, p.object_edges = object_edges;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_edge_outline(rt, p, f); });
}

void CrtEffect::reflect(Reflector &r) {
  r.field("Scanlines", scanlines, 0.01f, 0.0f, 1.0f);
  r.field("Curvature", curvature, 0.01f, 0.0f, 1.0f);
  r.field("Phosphor Mask", mask, 0.01f, 0.0f, 1.0f);
  r.help("The tube's red / green / blue stripes.");
  r.field("Flicker", flicker, 0.01f, 0.0f, 1.0f);
  r.help("Brightness pulses in Play mode only.");
}
void CrtEffect::contribute(FilterStack &s) const {
  CrtParams p;
  p.scanlines = scanlines, p.curvature = curvature, p.mask = mask, p.flicker = flicker;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_crt(rt, p, f); });
}

void SharpenEffect::reflect(Reflector &r) { r.field("Amount", amount, 0.01f, 0.0f, 4.0f); }
void SharpenEffect::contribute(FilterStack &s) const {
  const float a = amount;
  s.passes.push_back([a](RenderTarget &rt, const FilterFrame *f) { apply_sharpen(rt, a, f); });
}

void BloomEffect::reflect(Reflector &r) {
  r.field("Intensity", intensity, 0.01f, 0.0f, 100.0f);
  r.field("Threshold", threshold, 0.01f, 0.0f, 1000.0f);
  r.help("Brightness (linear light, before exposure) above which things glow. 1 is a white surface in full light:\nemissive materials and lights above it bloom.");
  r.field("Soft Knee", soft_knee, 0.01f, 0.0f, 1.0f);
  r.help("0: a hard cut at the threshold. 1: the glow fades in gradually below it.");
  r.field("Scatter", scatter, 0.01f, 0.0f, 1.0f);
  r.help("How far the glow spreads: low keeps it tight round the light, high gives a wide haze.");
  r.field("Clamp", clamp, 1.0f, 0.0f, 65000.0f);
  r.help("Caps single very bright pixels so they don't blow up into large blobs.");
  r.color("Tint", tint);
}
void BloomEffect::contribute(FilterStack &s) const {
  BloomParams p;
  p.intensity = intensity, p.threshold = threshold, p.soft_knee = soft_knee, p.scatter = scatter, p.clamp = clamp, p.tint = tint;
  s.needs_hdr = true;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *) { apply_bloom(rt, p); });
}

void HalftoneEffect::reflect(Reflector &r) {
  static const char *modes[] = {"Mono", "Color"};
  r.enumeration("Mode", mode, modes, 2);
  r.field("Cell Size", cell, 0.1f, 2.0f, 128.0f);
  r.help("Dot spacing in pixels at 1080p (the same look at any resolution).");
  r.field("Angle", angle, 0.5f, -180.0f, 180.0f);
  r.field("Contrast", contrast, 0.01f, 0.0f, 8.0f);
  if (r.all_fields() || mode == 0) {
    r.color("Ink", ink);
    r.color("Paper", paper);
  }
}
void HalftoneEffect::contribute(FilterStack &s) const {
  HalftoneParams p;
  p.cell = cell, p.angle = angle, p.mode = mode, p.ink = display_color(ink), p.paper = display_color(paper), p.contrast = contrast;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_halftone(rt, p, f); });
}

void HatchingEffect::reflect(Reflector &r) {
  r.field("Spacing", spacing, 0.1f, 2.0f, 128.0f);
  r.field("Thickness", thickness, 0.05f, 0.25f, 32.0f);
  r.field("Angle", angle, 0.5f, -180.0f, 180.0f);
  r.field("Layers", layers, 1, 3);
  r.help("1: lines one way. 2: cross-hatching in the darker tones. 3: a diagonal too in the darkest.");
  r.field("Strength", strength, 0.01f, 0.0f, 1.0f);
  r.field("Keep Color", keep_color);
  r.help("Draw the lines over the picture instead of over paper.");
  r.color("Ink", ink);
  if (r.all_fields() || !keep_color) r.color("Paper", paper);
}
void HatchingEffect::contribute(FilterStack &s) const {
  HatchingParams p;
  p.spacing = spacing, p.thickness = thickness, p.angle = angle, p.layers = layers, p.strength = strength, p.keep_color = keep_color;
  p.ink = display_color(ink), p.paper = display_color(paper);
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_hatching(rt, p, f); });
}

void LineArtEffect::reflect(Reflector &r) {
  r.color("Color", color);
  r.field("Thickness", thickness, 0.05f, 0.5f, 8.0f);
  r.field("Silhouettes", silhouettes);
  r.help("Outlines against the background, and where a part steps in front of another.");
  r.field("Object Edges", object_edges);
  r.field("Creases", creases);
  r.field("Crease Angle", crease_angle, 0.5f, 1.0f, 179.0f);
  r.help("A line where the surface turns more than this many degrees (a cube's edges at 90; smooth curves draw none).");
  r.field("Taper", taper, 0.01f, 0.0f, 100.0f);
  r.help("Lines fade with distance (0: the same everywhere).");
}
void LineArtEffect::contribute(FilterStack &s) const {
  LineArtParams p;
  p.color = display_color(color), p.thickness = thickness, p.crease_angle = crease_angle, p.taper = taper;
  p.silhouettes = silhouettes, p.object_edges = object_edges, p.creases = creases;
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_line_art(rt, p, f); });
}

void PaperEffect::reflect(Reflector &r) {
  r.field("Strength", strength, 0.01f, 0.0f, 1.0f);
  r.field("Scale", scale, 0.01f, 0.05f, 50.0f);
  r.color("Tint", tint);
}
void PaperEffect::contribute(FilterStack &s) const {
  PaperParams p;
  p.strength = strength, p.scale = scale, p.tint = display_color(tint);
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *f) { apply_paper(rt, p, f); });
}

void ColorRampEffect::reflect(Reflector &r) {
  static const char *interp[] = {"Constant", "Linear"};
  r.field("Stops", stops, 2, 4);
  r.enumeration("Interpolation", interpolation, interp, 2);
  r.help("Constant: flat tones (cel); Linear: a gradient map.");
  Vec3 *cols[4] = {&color0, &color1, &color2, &color3};
  float *poss[4] = {&position0, &position1, &position2, &position3};
  static const char *cn[4] = {"Color 1", "Color 2", "Color 3", "Color 4"}, *pn[4] = {"Position 1", "Position 2", "Position 3", "Position 4"};
  for (int k = 0; k < 4; k++)
    if (r.all_fields() || k < stops) {
      r.color(cn[k], *cols[k]);
      r.field(pn[k], *poss[k], 0.01f, 0.0f, 1.0f);
    }
}
void ColorRampEffect::contribute(FilterStack &s) const {
  ColorRampParams p;
  p.stops = stops, p.interpolation = interpolation;
  const Vec3 cols[4] = {color0, color1, color2, color3};
  const float poss[4] = {position0, position1, position2, position3};
  for (int k = 0; k < 4; k++) p.color[k] = display_color(cols[k]), p.position[k] = poss[k];
  s.passes.push_back([p](RenderTarget &rt, const FilterFrame *) { apply_color_ramp(rt, p); });
}

const std::vector<std::string> &filter_looks() {
  static const std::vector<std::string> names = {"Anime", "Manga"};
  return names;
}

bool apply_filter_look(CameraFilters &cf, const std::string &name_in) {
  const std::string name = to_lower(name_in);
  /* One effect each (task 0154); until then a look was three or four effects appended, which older scenes still hold. */
  if (name == "anime") return cf.add(AnimeEffect::kName) != nullptr;
  if (name == "manga") return cf.add(MangaEffect::kName) != nullptr;
  return false;
}

void AnimeEffect::reflect(Reflector &r) {
  r.field("Lines", lines);
  r.help("Ink lines on silhouettes, where objects meet, and on creases sharper than Crease Angle.");
  if (r.all_fields() || lines) {
    r.color("Line Color", line_color);
    r.field("Line Thickness", line_thickness, 0.05f, 0.5f, 8.0f);
    r.field("Crease Angle", crease_angle, 0.5f, 1.0f, 179.0f);
  }
  r.field("Saturation", saturation, 0.01f, -1.0f, 1.0f);
  r.help("A little more color (0: unchanged; 1: twice as vivid).");
  r.field("Contrast", contrast, 0.01f, -1.0f, 1.0f);
  r.field("Bloom", bloom, 0.01f, 0.0f, 100.0f);
  r.help("The soft glow round bright things (0: none).");
  if (r.all_fields() || bloom > 0.0f) r.field("Bloom Threshold", bloom_threshold, 0.01f, 0.0f, 1000.0f);
}
/* The parts' own passes, so the one effect draws exactly what the three did. */
void AnimeEffect::contribute(FilterStack &s) const {
  if (lines) {
    LineArtEffect l;
    l.color = line_color, l.thickness = line_thickness, l.crease_angle = crease_angle;
    l.contribute(s);
  }
  ColorGradingEffect g;
  g.saturation = saturation, g.contrast = contrast;
  g.contribute(s);
  if (bloom > 0.0f) {
    BloomEffect b;
    b.intensity = bloom, b.threshold = bloom_threshold;
    b.contribute(s);
  }
}

void MangaEffect::reflect(Reflector &r) {
  r.field("Grayscale", grayscale);
  r.field("Tones", tones, 0, 8);
  r.help("0: smooth greys. 2 to 8: that many flat tones (screentone steps) before the dots.");
  r.field("Dots", dots);
  if (r.all_fields() || dots) {
    r.field("Dot Cell", dot_cell, 0.1f, 2.0f, 128.0f);
    r.help("Dot spacing in pixels at 1080p (the same look at any resolution).");
    r.field("Dot Contrast", dot_contrast, 0.01f, 0.0f, 8.0f);
  }
  r.field("Lines", lines);
  if (r.all_fields() || lines) r.field("Line Thickness", line_thickness, 0.05f, 0.5f, 8.0f);
  r.field("Paper", paper, 0.01f, 0.0f, 1.0f);
  r.help("How much of the paper's grain shows through (0: none).");
}
void MangaEffect::contribute(FilterStack &s) const {
  if (grayscale) GrayscaleEffect().contribute(s);
  if (tones >= 2) {
    PosterizeEffect p;
    p.levels = tones;
    p.contribute(s);
  }
  if (dots) {
    HalftoneEffect h;
    h.cell = dot_cell, h.contrast = dot_contrast;
    h.contribute(s);
  }
  if (lines) {
    LineArtEffect l;
    l.thickness = line_thickness;
    l.contribute(s);
  }
  if (paper > 0.0f) {
    PaperEffect p;
    p.strength = paper;
    p.contribute(s);
  }
}

void Rotator::reflect(Reflector &r) { r.field("Degrees Per Second", degrees_per_second); }

void Outline::reflect(Reflector &r) {
  r.field("Thickness", thickness, 0.001f, 0.0f, 10.0f);
  r.help("How far the outline shell stands out from the surface.");
  r.field("World Space", world_space);
  r.help("Thickness in metres whatever the object's scale (off: in the object's own units).");
  r.color("Color", color);
  r.help("The line's color. (The shell is the inverted hull: seen from behind, an open surface such as a plane shows\n"
         "it as a sheet; it suits closed meshes.)");
  r.text("Thickness Group", thickness_group);
  r.help("A vertex group scaling the width per vertex (task 0149; Guilty Gear paints it): 1 the full width, 0 no line\n"
         "there (inside the mouth, where the hair meets the scalp). Empty: the full width everywhere.");
  r.field("Distance Fade", distance_fade, 0.01f, 0.0f, 10.0f);
  r.help("The line thinner far away: the thickness divided by 1 + this x the metres to the eye (0: the same everywhere).");
  r.field("Min Pixels", min_pixels, 0.1f, 0.0f, 64.0f);
  r.field("Max Pixels", max_pixels, 0.1f, 0.0f, 64.0f);
  r.help("The line's width on screen kept between these, by the object's distance (0: no limit) - a constant line\n"
         "whatever the zoom, in the views and raster renders.");
}

float Outline::outline_distance_scale(float distance, float px_per_m, float world_scale, int view) const {
  if (!finite_bits(distance) || !(distance > 1e-4f) || !finite_bits(px_per_m) || !(px_per_m > 0.0f)) return 1.0f;
  float scale = 1.0f;
  const float fade = finite_bits(distance_fade) ? std::max(0.0f, distance_fade) : 0.0f;
  if (fade > 0.0f) scale /= 1.0f + fade * distance;
  /* The line's width in metres: the thickness, times the object's scale when it is in the object's units. */
  float base = finite_bits(thickness) ? std::max(0.0f, thickness) : 0.0f;
  if (!world_space && finite_bits(world_scale) && world_scale > 0.0f) base *= world_scale;
  if (!(base > 0.0f)) return scale;
  const float px = base * scale * px_per_m / distance;  // the line's width on screen, in pixels
  const float lo = finite_bits(min_pixels) ? std::max(0.0f, min_pixels) : 0.0f, hi = finite_bits(max_pixels) ? std::max(0.0f, max_pixels) : 0.0f;
  float want = px;
  if (lo > 0.0f) want = std::max(want, lo);
  if (hi > 0.0f) want = std::min(want, hi);  // (the maximum wins when the two cross)
  if (want != px && px > 0.0f) scale *= want / px;
  if (!finite_bits(scale) || !(scale > 0.0f)) return 1.0f;
  /* In 2 % steps, so an orbiting camera doesn't rebuild the shell every frame (a rebuild frees the last one); with a
   * hysteresis (task 0164): the step only changes once the scale has moved a third of a step past the last one, so a
   * dolly that hovers on a boundary doesn't pop between two shells. */
  const float step = std::log(1.02f), raw = std::log(scale) / step;
  float idx = std::round(raw);
  int &last = last_step_[view == 0 ? 0 : 1];
  if (last != kNoStep && std::fabs(raw - (float)last) < 0.5f + 1.0f / 3.0f) idx = (float)last;
  last = (int)idx;
  return std::exp(idx * step);
}

const Mesh *Outline::shell(const Mesh &src, float world_scale, int slot, float thickness_scale) const {
  const float s = world_space ? (finite_bits(world_scale) && world_scale > 1e-6f ? world_scale : 1.0f) : 1.0f;
  const float offset = thickness / s * (finite_bits(thickness_scale) && thickness_scale > 0.0f ? thickness_scale : 1.0f);
  if (!(offset > 0.0f) || !finite_bits(offset) || src.face_count() == 0) return nullptr;
  ShellCache &c = shells_[std::max(0, std::min(kShellSlots - 1, slot))];
  const uint64_t serial = src.render_mesh().serial;  // unique per build of the source (not a reused address)
  if (c.mesh && serial != 0 && serial == c.serial && offset == c.offset && c.group == thickness_group) return c.mesh.get();
  /* The per-vertex width (task 0149): the thickness group's weight, 1 without one (or one the mesh hasn't). */
  const int gi = thickness_group.empty() ? -1 : src.find_group(thickness_group);
  const std::vector<float> *gw = gi >= 0 && src.vertex_groups[(size_t)gi].weights.size() == src.positions.size() ? &src.vertex_groups[(size_t)gi].weights : nullptr;
  /* Smoothed normals: each vertex's faces' area-weighted normals, so the shell stays closed at hard edges. */
  std::vector<Vec3> vn(src.positions.size(), Vec3(0.0f));
  for (size_t f = 0; f < src.face_count(); f++) {
    const uint32_t *v = src.face_verts(f);
    const uint32_t n = src.face_size(f);
    Vec3 fn(0.0f);
    for (uint32_t k = 0; k < n; k++) fn += cross(src.positions[v[k]], src.positions[v[(k + 1) % n]]);  // Newell
    if (!finite_bits(fn.x) || !finite_bits(fn.y) || !finite_bits(fn.z)) continue;
    for (uint32_t k = 0; k < n; k++) vn[v[k]] += fn;
  }
  auto m = std::make_shared<Mesh>();
  m->name = "Outline";
  m->smooth = true;
  m->positions.reserve(src.positions.size());
  for (size_t i = 0; i < src.positions.size(); i++) {
    const float len = length(vn[i]);
    const Vec3 d = len > 1e-20f && finite_bits(len) ? vn[i] / len : Vec3(0.0f);
    const float w = gw ? clampf(finite_bits((*gw)[i]) ? (*gw)[i] : 0.0f, 0.0f, 1.0f) : 1.0f;
    m->positions.push_back(src.positions[i] + d * (offset * w));
  }
  std::vector<uint32_t> rev;
  for (size_t f = 0; f < src.face_count(); f++) {
    const uint32_t *v = src.face_verts(f);
    const uint32_t n = src.face_size(f);
    rev.assign(v, v + n);
    std::reverse(rev.begin(), rev.end());  // flipped: its outside faces in
    m->add_face(rev.data(), rev.size());
  }
  c.mesh = m;
  c.serial = serial;
  c.offset = offset;
  c.group = thickness_group;
  return c.mesh.get();
}

const std::vector<MaterialPtr> &Outline::materials() const {
  const Vec3 c(finite_bits(color.x) ? color.x : 0.0f, finite_bits(color.y) ? color.y : 0.0f, finite_bits(color.z) ? color.z : 0.0f);
  if (mats_.empty() || c.x != mats_color_.x || c.y != mats_color_.y || c.z != mats_color_.z) {
    auto mat = make_material("Outline", c);
    mat->unlit = true;
    mat->cast_shadows = false;
    mat->outline_hull = true;
    mats_ = {mat};
    mats_color_ = c;
  }
  return mats_;
}
void Rotator::update(PlayContext &ctx) {
  Quat dq = Quat::euler(degrees_per_second * ctx.dt);
  owner->set_local_rotation(owner->local().rotation * dq);
}

void Oscillator::reflect(Reflector &r) {
  r.field("Axis", axis);
  r.field("Amplitude", amplitude, 0.01f);
  r.field("Frequency", frequency, 0.01f, 0.0f, 50.0f);
}
void Oscillator::start(PlayContext &) { origin = owner->local().position; }
void Oscillator::update(PlayContext &ctx) {
  owner->set_local_position(origin + normalize(axis) * (amplitude * std::sin(ctx.time * frequency * 2.0f * kPi)));
}

void PlayerController::reflect(Reflector &r) {
  r.field("Move Speed", move_speed, 0.05f, 0.0f, 100.0f);
  r.field("Turn Speed", turn_speed, 1.0f, 0.0f, 1000.0f);
  r.help("Degrees per second. Use WASD or the arrow keys in the Game view.");
}
void PlayerController::update(PlayContext &ctx) {
  if (!ctx.key_down) return;
  using namespace platform;
  float fwd = (ctx.key_down(KEY_W) || ctx.key_down(KEY_UP) ? 1.0f : 0.0f) - (ctx.key_down(KEY_S) || ctx.key_down(KEY_DOWN) ? 1.0f : 0.0f);
  float turn = (ctx.key_down(KEY_D) || ctx.key_down(KEY_RIGHT) ? 1.0f : 0.0f) - (ctx.key_down(KEY_A) || ctx.key_down(KEY_LEFT) ? 1.0f : 0.0f);
  if (turn != 0) owner->set_local_rotation(Quat::axis_angle({0, 1, 0}, turn * turn_speed * kDeg2Rad * ctx.dt) * owner->local().rotation);
  if (fwd != 0) {
    Vec3 dir = owner->local().rotation.rotate({0, 0, 1});
    owner->set_local_position(owner->local().position + dir * (fwd * move_speed * ctx.dt));
  }
}

void Rigidbody::reflect(Reflector &r) {
  r.field("Mass", mass, 0.01f, 0.001f, 10000.0f);
  r.field("Use Gravity", use_gravity);
  r.field("Is Kinematic", is_kinematic);
  r.help("Kinematic bodies are moved by scripts, not by physics.");
  r.field("Bounciness", bounciness, 0.01f, 0.0f, 1.0f);
  r.field("Drag", drag, 0.01f, 0.0f, 10.0f);
}

void SubdivisionSurface::reflect(Reflector &r) {
  r.field("Levels", levels, 0, 5);
  r.help("Each level multiplies the face count by ~4. Blender: Levels Viewport.");
  r.field("Smooth (Catmull-Clark)", smooth);
}
void SubdivisionSurface::modify(Mesh &m) const {
  m = meshops::subdivide(m, levels, smooth);
  if (smooth && levels > 0) m.smooth = true;
}

void SmoothModifier::reflect(Reflector &r) {
  r.field("Factor", factor, 0.01f, -2.0f, 2.0f);
  r.field("Iterations", iterations, 0, 200);
  reflect_group_limit(r, vertex_group, invert_group);
}
void SmoothModifier::modify(Mesh &m) const { meshops::smooth_laplacian(m, factor, iterations); }

/* The vertex group's weight per vertex (1 everywhere without one), inverted when asked; a NaN weight counts as 0. */
static std::vector<float> group_weights_of(const Mesh &m, const std::string &group, bool invert) {
  std::vector<float> w(m.vert_count(), 1.0f);
  const int gi = group.empty() ? -1 : m.find_group(group);
  if (gi >= 0 && m.vertex_groups[(size_t)gi].weights.size() == m.vert_count())
    for (size_t i = 0; i < w.size(); i++) {
      const float raw = m.vertex_groups[(size_t)gi].weights[i];
      const float g = clampf(finite_bits(raw) ? raw : 0.0f, 0.0f, 1.0f);
      w[i] = invert ? 1.0f - g : g;
    }
  return w;
}
/* `a` turned toward `b` by t, unit length; `a` when the blend has no length (opposite normals at t = 0.5). */
static Vec3 blend_normal(Vec3 a, Vec3 b, float t) {
  const Vec3 n = lerp(a, b, t);
  const float len = length(n);
  return finite_bits(len) && len > 1e-6f ? n / len : a;
}
/* The closest point of the triangle ABC to p, as barycentric weights of B and C (Ericson, Real-Time Collision
 * Detection 5.1.5): each early return is one region of the plane - a vertex, an edge or the inside. */
static void closest_point_on_triangle(Vec3 p, Vec3 A, Vec3 Bv, Vec3 C, float &u, float &v) {
  const Vec3 ab = Bv - A, ac = C - A, ap = p - A;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  u = 0, v = 0;
  if (d1 <= 0.0f && d2 <= 0.0f) return;  // A
  const Vec3 bp = p - Bv;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) {  // B
    u = 1;
    return;
  }
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {  // on AB
    u = d1 / (d1 - d3);
    return;
  }
  const Vec3 cp = p - C;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) {  // C
    v = 1;
    return;
  }
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {  // on AC
    v = d2 / (d2 - d6);
    return;
  }
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {  // on BC
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    u = 1 - w, v = w;
    return;
  }
  const float denom = 1.0f / (va + vb + vc);  // inside
  u = vb * denom, v = vc * denom;
}

void SmoothNormalsModifier::reflect(Reflector &r) {
  r.field("Factor", factor, 0.01f, 0.0f, 1.0f);
  r.help("How far each vertex's normal moves toward its neighbours' average, each iteration (1: all the way).");
  r.field("Iterations", iterations, 0, 100);
  reflect_group_limit(r, vertex_group, invert_group);
}
void SmoothNormalsModifier::modify(Mesh &m) const {
  const size_t nv = m.vert_count();
  if (!nv) return;
  std::vector<Vec3> n = m.custom_normals_fit() ? m.custom_normals : meshops::vertex_normals(m);
  const std::vector<Vec3> start = n;
  /* The neighbours along the faces' edges. */
  std::vector<std::vector<uint32_t>> adj(nv);
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t k = m.face_size(f);
    for (uint32_t c = 0; c < k; c++) {
      const uint32_t a = v[c], b = v[(c + 1) % k];
      if (a < nv && b < nv) adj[a].push_back(b), adj[b].push_back(a);
    }
  }
  const float k = clampf(factor, 0.0f, 1.0f);
  std::vector<Vec3> next(nv);
  for (int it = 0; it < std::max(0, std::min(iterations, 100)); it++) {
    for (size_t i = 0; i < nv; i++) {
      Vec3 sum(0.0f);
      for (uint32_t j : adj[i]) sum += n[j];
      const float len = length(sum);
      next[i] = adj[i].empty() || !(len > 1e-12f) ? n[i] : blend_normal(n[i], sum / len, k);
    }
    n.swap(next);
  }
  const std::vector<float> w = group_weights_of(m, vertex_group, invert_group);
  m.custom_normals.resize(nv);
  for (size_t i = 0; i < nv; i++) m.custom_normals[i] = blend_normal(start[i], n[i], w[i]);
}

static const GameObject *proxy_target(const Component &c, const std::string &name) {
  if (!c.owner || !c.owner->scene || name.empty()) return nullptr;
  const GameObject *t = c.owner->scene->find_by_name(name);
  return t && t != c.owner && t->evaluated_mesh() ? t : nullptr;
}
void NormalTransferModifier::reflect(Reflector &r) {
  r.object("Proxy", object, "MeshFilter");
  r.help("The object whose surface gives the normals: a sphere round a head makes a face shade like a ball (anime's\n"
         "flat face), a capsule a limb. Rigging > Anime Face Normals adds one fitted to the mesh.");
  r.field("Blend", blend, 0.01f, 0.0f, 1.0f);
  r.help("How far the normals go from the mesh's own to the proxy's (1: all the way).");
  reflect_group_limit(r, vertex_group, invert_group);
}
void NormalTransferModifier::modify(Mesh &m) const {
  const GameObject *t = proxy_target(*this, object);
  if (!t || !owner) return;
  const Mesh &pm = *t->evaluated_mesh();
  const RenderMesh &prm = pm.render_mesh();  // (its own vertices and shading normals: the triangles index these)
  if (prm.indices.size() < 3 || prm.normals.size() != prm.positions.size()) return;
  /* The proxy's triangles in the owner's own space (both world matrices known), so the nearest point is found there. */
  const Mat4 to_owner = owner->world_matrix().inverse() * t->world_matrix();
  const Mat4 nm = to_owner.inverse().transposed();
  std::vector<Vec3> pp(prm.positions.size()), pnn(prm.positions.size());
  for (size_t i = 0; i < pp.size(); i++) pp[i] = to_owner.point(prm.positions[i]), pnn[i] = normalize(nm.dir(prm.normals[i]));
  const size_t nv = m.vert_count();
  std::vector<Vec3> own = m.custom_normals_fit() ? m.custom_normals : meshops::vertex_normals(m);
  const std::vector<float> w = group_weights_of(m, vertex_group, invert_group);
  const float k = clampf(blend, 0.0f, 1.0f);
  m.custom_normals.resize(nv);
  /* Each vertex: the nearest point on the proxy's triangles, the normal interpolated there. A brute-force search:
   * the proxy is a few hundred triangles (Anime Face Normals makes a 16 x 12 sphere) and the modifier runs once per
   * edit, before a skin (the skin turns the normals with the bones), so no spatial structure is worth its upkeep. */
  JobSystem::global().parallel_for((int64_t)nv, 256, [&](int64_t a, int64_t b) {
    for (int64_t vi = a; vi < b; vi++) {
      const Vec3 p = m.positions[(size_t)vi];
      float best = std::numeric_limits<float>::max();
      Vec3 bn = own[(size_t)vi];
      for (size_t tri = 0; tri + 2 < prm.indices.size(); tri += 3) {
        const uint32_t i0 = prm.indices[tri], i1 = prm.indices[tri + 1], i2 = prm.indices[tri + 2];
        if (i0 >= pp.size() || i1 >= pp.size() || i2 >= pp.size()) continue;
        float u, v;
        closest_point_on_triangle(p, pp[i0], pp[i1], pp[i2], u, v);
        const Vec3 q = pp[i0] + (pp[i1] - pp[i0]) * u + (pp[i2] - pp[i0]) * v;
        const float d = dot(q - p, q - p);
        if (d < best) best = d, bn = pnn[i0] * (1 - u - v) + pnn[i1] * u + pnn[i2] * v;
      }
      const float len = length(bn);
      m.custom_normals[(size_t)vi] = blend_normal(own[(size_t)vi], finite_bits(len) && len > 1e-12f ? bn / len : own[(size_t)vi], k * w[(size_t)vi]);
    }
  });
}
uint64_t NormalTransferModifier::modifier_dependency_hash() const {
  const GameObject *t = proxy_target(*this, object);
  if (!t || !owner) return 0;
  uint64_t h = t->evaluated_mesh()->version * kFnvPrime;
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  for (int i = 0; i < 16; i++) {
    uint32_t b;
    std::memcpy(&b, &rel.m[i], 4);
    h = (h ^ b) * kFnvPrime;
  }
  return h;
}

void MirrorModifier::reflect(Reflector &r) {
  r.field("Axis X", x);
  r.field("Axis Y", y);
  r.field("Axis Z", z);
  r.help("Mirrors across the object's local planes (Blender: Mirror modifier). Model one half, see both.");
  r.field("Merge", merge);
  r.field("Merge Distance", merge_distance, 0.0005f, 0.0f, 1.0f);
}
void MirrorModifier::modify(Mesh &m) const { meshops::mirror(m, x, y, z, merge ? merge_distance : 0.0f); }

void ArrayModifier::reflect(Reflector &r) {
  static const char *kModes[] = {"Offset", "Radial"};
  r.enumeration("Mode", mode, kModes, 2);
  r.help("Offset: copies in a row. Radial: copies round an axis through the origin (Plasticity / UModeler radial array;\nBlender does this with an Empty as the Object Offset).");
  r.field("Count", count, 1, 1000);
  const bool all = r.all_fields();
  if (all || mode == 0) {
    r.field("Relative Offset", relative_offset);
    r.help("Offset as a multiple of the mesh size (Blender: Relative Offset > Factor).");
    r.field("Constant Offset", constant_offset);
  }
  if (all || mode == 1) {
    static const char *kAxes3[] = {"X", "Y", "Z"};
    r.enumeration("Axis", radial_axis, kAxes3, 3);
    r.field("Angle", radial_angle, 1.0f, -3600.0f, 3600.0f);
    r.help("360 spreads the copies evenly round a full turn.");
  }
  if (all || mode == 0) {
    static const char *kFit[] = {"Fixed Count", "Fit Length", "Fit Curve"};
    r.enumeration("Fit Type", fit_type, kFit, 3);
    r.help("Blender's Fit Type: Fixed Count uses Count; Fit Length makes as many copies as fit the length; Fit Curve as many\n"
           "as fit the curve's length (then add a Curve modifier on the same curve to bend them along it). Each copy takes one\n"
           "step, so the copies end within the length (Blender adds one more, ending past it).");
    if (all || fit_type == 1) r.field("Fit Length", fit_length, 0.05f, 0.0f, 1e5f);
    if (all || fit_type == 2) {
      r.object("Curve", curve, "Curve");
      r.help("Name of the Curve object whose length the copies fill.");
    }
  }
  r.field("Merge", merge);
  r.field("Merge Distance", merge_distance, 0.0005f, 0.0f, 1.0f);
}
void ArrayModifier::modify(Mesh &m) const {
  if (mode == 1) {
    meshops::radial_array(m, count, radial_axis, radial_angle, merge ? merge_distance : 0.0f);
    return;
  }
  int n = count;
  if (fit_type == 1 || fit_type == 2) {
    /* As many copies as fit: each takes one step (the offset between copies). */
    const float L = fit_type == 1 ? fit_length : array_curve_length(*this);
    const AABB b = m.bounds();
    const Vec3 size = b.max - b.min;
    const float step = length(Vec3(relative_offset.x * size.x, relative_offset.y * size.y, relative_offset.z * size.z) + constant_offset);
    n = 1;
    if (finite_bits(L) && finite_bits(step) && step > 1e-6f && L > 0.0f) n = (int)std::max(1.0, std::min(1000.0, std::floor(L / step + 1e-4)));
  }
  meshops::make_array(m, n, relative_offset, constant_offset, merge ? merge_distance : 0.0f);
}

void reflect_group_limit(Reflector &r, std::string &group, bool &invert) {
  r.text("Vertex Group", group);
  r.help("Only the vertices of this vertex group move, each as much as its weight (Blender: Vertex Group). Empty: the\n"
         "whole mesh; a name the mesh hasn't: the whole mesh too.");
  r.field("Invert Group", invert);
}

/* The points after a count, each with a "P<i> " prefix (ADR 0008's scheme): files, undo, hashing and
 * `set Curve.P2Position 1 0 0` see every point. */
void Curve::reflect(Reflector &r) {
  static const char *kTypes[] = {"Bezier", "Catmull-Rom", "Poly"};
  r.enumeration("Type", type, kTypes, 3);
  r.help("Bezier: smooth, near the points (Blender's Auto handles). Catmull-Rom: smooth, through every point. Poly: straight lines.");
  r.field("Closed", closed);
  r.help("Joins the last point to the first (Blender: Cyclic).");
  r.field("Resolution", resolution, 1, 256);
  r.help("Samples per segment (Blender: Resolution Preview U).");
  int n = (int)points.size();
  const int before = n;
  r.field("Points", n, 0, kMaxPoints);
  if (n != before) points.resize((size_t)std::max(0, std::min(n, kMaxPoints)));
  for (size_t i = 0; i < points.size(); i++) {
    PrefixReflector p(r, "P" + std::to_string(i) + " ");
    p.field("Position", points[i].position);
    p.field("Tilt", points[i].tilt, 1.0f, -36000.0f, 36000.0f);
    p.field("Radius", points[i].radius, 0.01f, 0.0f, 1000.0f);
  }
}

/* The resolution first: a change resamples the shape (Blender keeps it the same way), then each point's offset with a
 * "P<i> " prefix. The Inspector lists the offsets only for small lattices (the gizmo moves points). */
void Lattice::reflect(Reflector &r) {
  int u = points_u, v = points_v, w = points_w;
  r.field("Points U", u, 2, kMaxRes);
  r.field("Points V", v, 2, kMaxRes);
  r.field("Points W", w, 2, kMaxRes);
  r.help("Points along each side (Blender: Resolution U / V / W). Changing it keeps the shape.");
  if (u != points_u || v != points_v || w != points_w || (int)offsets.size() != size()) resample(u, v, w);
  static const char *kInterp[] = {"Linear", "B-Spline"};
  r.enumeration("Interpolation", interpolation, kInterp, 2);
  r.help("Linear follows the points exactly; B-Spline is smoother (Blender's default).");
  interpolation = std::max(0, std::min(interpolation, 1));
  if (!r.all_fields() && offsets.size() > 64) return;
  for (size_t i = 0; i < offsets.size(); i++) {
    PrefixReflector p(r, "P" + std::to_string(i) + " ");
    p.field("Offset", offsets[i]);
  }
}

void LatticeModifier::reflect(Reflector &r) {
  r.object("Object", object, "Lattice");
  r.help("Name of the Lattice object whose points move the mesh (Blender: Lattice > Object).");
  r.field("Strength", strength, 0.01f, 0.0f, 1.0f);
  reflect_group_limit(r, vertex_group, invert_group);
}

void CurveModifier::reflect(Reflector &r) {
  r.object("Object", object, "Curve");
  r.help("Name of the Curve object to bend along (Blender: Curve > Curve Object).");
  static const char *kAxes[] = {"X", "Y", "Z", "-X", "-Y", "-Z"};
  r.enumeration("Deform Axis", deform_axis, kAxes, 6);
  r.help("The mesh's axis that runs along the curve; the mesh's origin sits at the curve's start.");
  reflect_group_limit(r, vertex_group, invert_group);
}

void SolidifyModifier::reflect(Reflector &r) {
  r.field("Thickness", thickness, 0.005f, -10.0f, 10.0f);
  r.field("Offset", offset, 0.01f, -1.0f, 1.0f);
  r.help("-1 grows the shell inward, +1 outward (Blender: Solidify, Simple mode).");
  r.field("Even Thickness", even_thickness);
  r.field("Fill Rim", fill_rim);
}
void SolidifyModifier::modify(Mesh &m) const { meshops::solidify(m, thickness, offset, even_thickness, fill_rim); }

void BooleanModifier::reflect(Reflector &r) {
  r.object("Object", object, "MeshFilter");
  r.help("Name of the GameObject to cut with (Blender: Boolean > Object). Both meshes must be closed.");
  static const char *ops[] = {"Difference", "Union", "Intersect"};
  r.enumeration("Operation", operation, ops, 3);
  if (!meshops::boolean_available()) r.help("This build has no Manifold (needs Blender's libraries): the modifier does nothing.");
}

/* The cutter's evaluated mesh, guarding against cycles (A cuts B cuts A). */
static const GameObject *boolean_target(const BooleanModifier &b, const Mesh **mesh) {
  thread_local int depth = 0;
  *mesh = nullptr;
  if (!b.owner || !b.owner->scene || b.object.empty() || depth > 8) return nullptr;
  const GameObject *t = b.owner->scene->find_by_name(b.object);
  if (!t || t == b.owner) return nullptr;
  depth++;
  *mesh = t->evaluated_mesh();
  depth--;
  return t;
}

void BooleanModifier::modify(Mesh &m) const {
  const Mesh *tm = nullptr;
  const GameObject *t = boolean_target(*this, &tm);
  if (!t || !tm) return;
  Mat4 b_to_a = owner->world_matrix().inverse() * t->world_matrix();
  std::string err;
  if (!meshops::boolean_op(m, *tm, b_to_a, (meshops::BooleanOp)operation, &err)) {
    if (err != last_error) Log::warn("Boolean on '%s': %s", owner->name.c_str(), err.c_str());
    last_error = err;
  }
  else last_error.clear();
}

uint64_t BooleanModifier::modifier_dependency_hash() const {
  const Mesh *tm = nullptr;
  const GameObject *t = boolean_target(*this, &tm);
  if (!t) return 0;
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = fnv1a(p, n, h); };
  /* Relative placement + the cutter's mesh: moving either object re-cuts. */
  Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  mix(rel.m, sizeof(rel.m));
  mix(&tm, sizeof(tm));
  if (tm) mix(&tm->version, sizeof(tm->version));
  if (tm) mix(&tm->deform_version, sizeof(tm->deform_version));  // a deformed cutter is the same mesh, moved (ADR 0022)
  return h;
}

void DecimateModifier::reflect(Reflector &r) {
  r.field("Ratio", ratio, 0.01f, 0.0f, 1.0f);
  r.help("Fraction of triangles to keep (Blender: Decimate > Collapse > Ratio). Uses meshoptimizer; UV seams and material borders are kept.");
  if (!meshops::decimate_available()) r.help("This build has no meshoptimizer (needs Blender's libraries): the modifier does nothing.");
}
void DecimateModifier::modify(Mesh &m) const { meshops::decimate(m, ratio); }

static const char *const kAxes[] = {"X", "Y", "Z"};

void BevelModifier::reflect(Reflector &r) {
  r.field("Width", width, 0.005f, 0.0f, 1000.0f);
  r.help("How far the bevel reaches along the faces on each side (Blender: Amount).");
  r.field("Segments", segments, 1, 32);
  r.help("1 cuts a flat chamfer; more segments round it.");
  r.field("Profile", profile, 0.01f, 0.0f, 1.0f);
  r.help("The shape across the bevel (with 2+ segments): 0.5 round, 0.25 flat, toward 1 convex (out to a square\n"
         "corner), toward 0 concave (scooped in). Blender: Profile.");
  static const char *kLimit[] = {"None (every edge)", "Angle"};
  r.enumeration("Limit Method", limit_method, kLimit, 2);
  if (r.all_fields() || limit_method == 1) {
    r.field("Angle", angle, 0.5f, 0.0f, 180.0f);
    r.help("Only edges whose faces meet at more than this many degrees are bevelled (Blender: 30 by default).");
  }
}
void BevelModifier::modify(Mesh &m) const { meshops::bevel_modifier(m, width, segments, limit_method == 1 ? angle : -1.0f, nullptr, profile); }

void TriangulateModifier::reflect(Reflector &r) {
  r.field("Minimum Vertices", min_vertices, 4, 1000);
  r.help("Faces with at least this many corners are split: 4 does quads and n-gons, 5 only n-gons.");
}
void TriangulateModifier::modify(Mesh &m) const { meshops::triangulate_min(m, min_vertices); }

void WeldModifier::reflect(Reflector &r) {
  r.field("Distance", distance, 0.0005f, 0.0f, 1000.0f);
  r.help("Vertices closer than this become one (Blender: Weld > Distance).");
}
void WeldModifier::modify(Mesh &m) const {
  if (std::isfinite(distance) && distance >= 0.0f) meshops::merge_by_distance(m, distance);
}

void WireframeModifier::reflect(Reflector &r) {
  r.field("Thickness", thickness, 0.002f, 0.0f, 100.0f);
  r.field("Even Thickness", even_thickness);
  r.field("Replace Original", replace_original);
  r.help("Off: the faces stay and the frame is added around them.");
}
void WireframeModifier::modify(Mesh &m) const { meshops::wireframe(m, thickness, even_thickness, !replace_original); }

void DisplaceModifier::reflect(Reflector &r) {
  r.field("Strength", strength, 0.01f, -100.0f, 100.0f);
  r.field("Midlevel", midlevel, 0.01f, 0.0f, 1.0f);
  r.help("The texture value that leaves a vertex where it is; above pushes out, below pulls in.");
  r.field("Noise Size", noise_scale, 0.01f, 0.001f, 1000.0f);
  r.help("The size of the noise's bumps in object units (Blender: Clouds texture > Size).");
  r.field("Detail", octaves, 1, 8);
  r.field("Seed", seed, 0, 100000);
  static const char *kDir[] = {"Normal", "X", "Y", "Z"};
  r.enumeration("Direction", direction, kDir, 4);
  reflect_group_limit(r, vertex_group, invert_group);
}
void DisplaceModifier::modify(Mesh &m) const {
  meshops::displace(m, strength, midlevel, noise_scale, direction, octaves, (uint32_t)std::max(0, seed));
}

void SimpleDeformModifier::reflect(Reflector &r) {
  static const char *kModes[] = {"Twist", "Bend", "Taper", "Stretch"};
  r.enumeration("Mode", mode, kModes, 4);
  if (r.all_fields() || mode <= 1) {
    r.field("Angle", angle, 0.5f, -3600.0f, 3600.0f);
    r.help("Degrees of twist or bend across the limited length.");
  }
  if (r.all_fields() || mode >= 2) r.field("Factor", factor, 0.01f, -10.0f, 10.0f);
  r.enumeration("Axis", axis, kAxes, 3);
  r.help(mode == 1 ? "The axis the mesh bends around." : "The axis the deformation runs along (Y is up here; Blender's default is its up, Z).");
  r.field("Lower Limit", lower, 0.01f, 0.0f, 1.0f);
  r.field("Upper Limit", upper, 0.01f, 0.0f, 1.0f);
  r.help("The part of the mesh (0 = one end, 1 = the other) that deforms; the rest follows rigidly.");
  reflect_group_limit(r, vertex_group, invert_group);
}
void SimpleDeformModifier::modify(Mesh &m) const { meshops::simple_deform(m, mode, mode <= 1 ? angle : factor, axis, lower, upper); }

void CastModifier::reflect(Reflector &r) {
  static const char *kShapes[] = {"Sphere", "Cylinder", "Cuboid"};
  r.enumeration("Shape", shape, kShapes, 3);
  r.field("Factor", factor, 0.01f, -10.0f, 10.0f);
  r.help("0 leaves the mesh alone, 1 makes it the shape (negative pushes the other way).");
  r.field("Radius", radius, 0.01f, 0.0f, 10000.0f);
  r.help("0 uses the vertices' average distance from the origin.");
  if (r.all_fields() || shape == 1) r.enumeration("Axis", axis, kAxes, 3);
  reflect_group_limit(r, vertex_group, invert_group);
}
void CastModifier::modify(Mesh &m) const { meshops::cast(m, shape, factor, radius, axis); }

void ScrewModifier::reflect(Reflector &r) {
  r.field("Angle", angle, 1.0f, -36000.0f, 36000.0f);
  r.field("Steps", steps, 2, 512);
  r.help("Copies round the turn (Blender: Steps Viewport).");
  r.field("Screw", screw, 0.01f, -1000.0f, 1000.0f);
  r.help("How far each turn rises along the axis: springs and threads.");
  r.field("Iterations", iterations, 1, 100);
  r.enumeration("Axis", axis, kAxes, 3);
  r.field("Merge", merge);
  r.help("Weld the vertices that lie on the axis.");
  r.field("Flip", flip);
  r.help("Turn the result's faces around.");
}
void ScrewModifier::modify(Mesh &m) const { meshops::screw(m, angle, steps, screw, axis, iterations, merge, flip); }

/* ===================================================================== */
/* Hashing reflector                                                      */
/* ===================================================================== */

namespace {
struct HashReflector : Reflector {
  bool all_fields() const override { return true; }
  bool hashing() const override { return true; }
  bool serializing() const override { return true; }
  uint64_t h = kFnvBasisLegacy;
  /* A byte at a time, and it stays that way: the lights' and the world's hashes are part of
   * lightmap_scene_key, saved with every bake (a new formula would mark them all out of date). */
  void mix(const void *p, size_t n) {
    auto *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= kFnvPrime; }
  }
  void field(const char *, float &v, float, float, float) override { mix(&v, 4); }
  void field(const char *, int &v, int, int) override { mix(&v, 4); }
  void field(const char *, bool &v) override { mix(&v, 1); }
  void field(const char *, Vec3 &v) override { mix(&v, 12); }
  void color(const char *, Vec3 &v) override { mix(&v, 12); }
  void enumeration(const char *, int &v, const char *const *, int) override { mix(&v, 4); }
  void text(const char *, std::string &v) override { mix(v.data(), v.size()); }
  void mesh(const char *, MeshPtr &m) override {
    const void *p = m.get();
    mix(&p, sizeof(p));
    if (m) mix(&m->version, 8);
  }
  void texture(const char *, TextureRef &t) override {
    mix(t.path.data(), t.path.size());
    mix(&t.non_color, 1);
  }
  void material_list(const char *, std::vector<MaterialPtr> &mats) override {
    for (auto &m : mats) {
      const void *p = m.get();
      mix(&p, sizeof(p));
      if (m) mix(&m->version, 8);
    }
  }
  void weight_list(const char *, std::vector<std::pair<std::string, float>> &list) override {
    for (auto &[name, w] : list) {
      mix(name.data(), name.size());
      mix(&w, 4);
    }
  }
};
}  // namespace

namespace {
struct JsonReflector : Reflector {
  JsonWriter w;
  bool all_fields() const override { return true; }
  bool serializing() const override { return true; }
  void field(const char *n, float &v, float, float, float) override { w.key(n).num(v); }
  void field(const char *n, int &v, int, int) override { w.key(n).integer(v); }
  void field(const char *n, bool &v) override { w.key(n).boolean(v); }
  void field(const char *n, Vec3 &v) override { w.key(n).nums(&v.x, 3); }
  void color(const char *n, Vec3 &v) override { w.key(n).nums(&v.x, 3); }
  void enumeration(const char *n, int &v, const char *const *, int) override { w.key(n).integer(v); }
  void text(const char *n, std::string &v) override { w.key(n).str(v); }
  void mesh(const char *, MeshPtr &) override {}
  void texture(const char *n, TextureRef &t) override { w.key(n).str(t.path); }
};
}  // namespace

std::string reflect_to_json(const std::function<void(Reflector &)> &fn) {
  JsonReflector jr;
  jr.w.obj();
  fn(jr);
  jr.w.end();
  return jr.w.out;
}

uint64_t hash_component(Component &c) {
  HashReflector hr;
  hr.mix(c.type_name(), std::strlen(c.type_name()));
  hr.mix(&c.enabled, 1);
  c.reflect(hr);
  return hr.h;
}

uint64_t hash_reflect_fn(void (*fn)(void *, Reflector &), void *ctx) {
  HashReflector hr;
  fn(ctx, hr);
  return hr.h;
}

/* ===================================================================== */
/* GameObject                                                             */
/* ===================================================================== */

static Quat quat_from_matrix(const Mat4 &m) {
  Vec3 x = normalize(m.column(0)), y = normalize(m.column(1)), z = normalize(m.column(2));
  float tr = x.x + y.y + z.z;
  Quat q;
  if (tr > 0) {
    float s = std::sqrt(tr + 1.0f) * 2;
    q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
  }
  else if (x.x > y.y && x.x > z.z) {
    float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2;
    q = {0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
  }
  else if (y.y > z.z) {
    float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2;
    q = {(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
  }
  else {
    float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2;
    q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
  }
  return normalize(q);
}

const Mat4 &GameObject::world_matrix() const {
  if (world_dirty_) {
    world_ = parent ? parent->world_matrix() * local_matrix() : local_matrix();
    world_dirty_ = false;
  }
  return world_;
}

Mat4 GameObject::world_matrix_uncached() const {
  Mat4 m = local_matrix();
  for (const GameObject *p = parent; p; p = p->parent) m = p->local_matrix() * m;
  return m;
}

void GameObject::mark_dirty() {
  /* Before the early return: a node already stale since its hash was memoised must still invalidate
   * the memo (ADR 0015), and the memo is only refreshed by a hash, which reads the world matrix. */
  transform_version++;
  /* Invariant: a dirty node's descendants are dirty too, so we can stop early. */
  if (world_dirty_) return;
  world_dirty_ = true;
  for (GameObject *c : children) c->mark_dirty();
}

Quat GameObject::world_rotation() const {
  return parent ? normalize(parent->world_rotation() * local_.rotation) : local_.rotation;
}

void GameObject::set_world_position(Vec3 p) {
  set_local_position(parent ? parent->world_matrix().inverse().point(p) : p);
}

void GameObject::set_world_rotation(Quat q) {
  set_local_rotation(parent ? parent->world_rotation().conjugate() * q : q);
}

Transform transform_from_matrix(const Mat4 &l) {
  Transform t;
  t.position = l.translation();
  t.scale = {length(l.column(0)), length(l.column(1)), length(l.column(2))};
  /* Preserve mirroring (negative determinant) on X. */
  if (dot(cross(l.column(0), l.column(1)), l.column(2)) < 0) t.scale.x = -t.scale.x;
  Mat4 r = l;
  for (int i = 0; i < 3; i++) {
    float s = i == 0 ? t.scale.x : (i == 1 ? t.scale.y : t.scale.z);
    if (std::fabs(s) > 1e-12f)
      for (int k = 0; k < 3; k++) r.m[i * 4 + k] /= s;
  }
  t.rotation = quat_from_matrix(r);
  t.euler_hint = t.rotation.to_euler();
  return t;
}

void GameObject::set_world_matrix(const Mat4 &world) {
  set_local(transform_from_matrix(parent ? parent->world_matrix().inverse() * world : world));
}

bool GameObject::active_in_hierarchy() const {
  for (const GameObject *g = this; g; g = g->parent)
    if (!g->active) return false;
  return true;
}

Component *GameObject::add_component(std::unique_ptr<Component> c) {
  if (!c) return nullptr;
  if (c->unique())
    for (auto &e : components)
      if (std::strcmp(e->type_name(), c->type_name()) == 0) return e.get();
  c->owner = this;
  components.push_back(std::move(c));
  return components.back().get();
}

void GameObject::remove_component(Component *c) {
  for (size_t i = 0; i < components.size(); i++)
    if (components[i].get() == c) {
      components.erase(components.begin() + i);
      return;
    }
}

uint64_t GameObject::evaluated_key(int kind) const {
  kind = std::max(0, std::min(kind, 2));
  auto *mf = mesh_filter();
  if (!mf || !mf->mesh) return 0;
  uint64_t key = kFnvBasis;
  auto mix = [&](uint64_t v) { key = (key ^ v) * kFnvPrime; };
  mix((uint64_t)(uintptr_t)mf->mesh.get());
  mix(mf->mesh->version);
  mix(mf->mesh->deform_version);  // (as evaluated_keys: a positions-only change is a change the views must see)
  mix((uint64_t)kind);
  for (auto &c : components)
    if (c->is_modifier() && (kind == 1 ? c->show_in_render : c->enabled && (kind == 0 || c->show_in_editmode))) {
      mix(hash_component(*c));
      mix(c->modifier_dependency_hash());
    }
  return key;
}

void GameObject::evaluated_keys(uint64_t out[3]) const {
  auto *mf = mesh_filter();
  if (!mf || !mf->mesh) {
    out[0] = out[1] = out[2] = 0;
    return;
  }
  auto mix = [](uint64_t &key, uint64_t v) { key = (key ^ v) * kFnvPrime; };
  for (int kind = 0; kind < 3; kind++) {  // the same prefix as evaluated_key(kind)
    out[kind] = kFnvBasis;
    mix(out[kind], (uint64_t)(uintptr_t)mf->mesh.get());
    mix(out[kind], mf->mesh->version);
    mix(out[kind], mf->mesh->deform_version);  // (ADR 0022: a positions-only change - a sculpt dab, task 0236 - is a change the views must see)
    mix(out[kind], (uint64_t)kind);
  }
  /* The shape keys' weights (task 0135): renders and the viewport show them, Edit Mode's cage the basis. */
  if (const uint64_t sh = shape_key_hash(*mf->mesh, get<MeshRenderer>()))
    for (int kind = 0; kind < 2; kind++) mix(out[kind], sh);
  for (auto &c : components) {
    if (!c->is_modifier()) continue;
    const bool on[3] = {c->enabled, c->show_in_render, c->enabled && c->show_in_editmode};
    if (!on[0] && !on[1] && !on[2]) continue;
    const uint64_t hc = hash_component(*c), hd = c->modifier_dependency_hash();  // hashed once, not per kind
    for (int kind = 0; kind < 3; kind++)
      if (on[kind]) mix(out[kind], hc), mix(out[kind], hd);
  }
}

/* One modifier of a stack (ADR 0024): a deformer limited to a vertex group moves each vertex by its weight; a modifier
 * that changes the vertex count without carrying the groups gets them from the mesh before it, by position. */
void run_modifier(const Component &c, Mesh &m) {
  bool invert = false;
  const std::string *group = c.limit_group(invert);
  const int gi = group && !group->empty() ? m.find_group(*group) : -1;
  if (gi >= 0 && c.is_deform_only() && m.vertex_groups[(size_t)gi].weights.size() == m.vert_count()) {
    const std::vector<Vec3> before = m.positions;
    c.modify(m);
    if (m.positions.size() != before.size()) return;
    const std::vector<float> &w = m.vertex_groups[(size_t)gi].weights;
    for (size_t i = 0; i < before.size(); i++) {
      float k = finite_bits(w[i]) ? std::max(0.0f, std::min(w[i], 1.0f)) : 0.0f;
      if (invert) k = 1.0f - k;
      m.positions[i] = k == 0.0f ? before[i] : before[i] + (m.positions[i] - before[i]) * k;  // 0: untouched, even by a NaN
    }
    return;
  }
  if (!c.is_deform_only() && !m.vertex_groups.empty()) {
    /* What the transfer reads, not a whole copy (no render caches, UVs or flags). */
    Mesh before;
    before.positions = m.positions;
    before.face_offsets = m.face_offsets;
    before.corner_verts = m.corner_verts;
    before.vertex_groups = m.vertex_groups;
    c.modify(m);
    /* Mirror, Array and Join copy the groups themselves; a modifier that made a new mesh, or renumbered without
     * them, gets them from the mesh before it, by position, through a map kept while the topology stays. */
    if (m.groups_fit() && m.vertex_groups.size() == before.vertex_groups.size()) return;
    if (!before.groups_fit()) {
      meshops::transfer_vertex_groups(before, m);
      return;
    }
    const uint64_t in_key = meshops::topology_key(before), out_key = meshops::topology_key(m);
    std::shared_ptr<meshops::GroupTransferMap> map = c.group_map;
    if (!map || map->in_key != in_key || map->out_key != out_key) {
      map = std::make_shared<meshops::GroupTransferMap>();
      meshops::group_transfer_map(before, m, *map);
      map->in_key = in_key, map->out_key = out_key;
      c.group_map = map;
    }
    meshops::apply_group_map(*map, before, m);
    return;
  }
  c.modify(m);
}

const Mesh *GameObject::evaluated_mesh(int kind) const {
  kind = std::max(0, std::min(kind, 2));
  auto *mf = mesh_filter();
  if (!mf || !mf->mesh) return nullptr;
  uint64_t key = kFnvBasis;
  bool any = false;
  auto mix = [&](uint64_t v) { key = (key ^ v) * kFnvPrime; };
  mix((uint64_t)(uintptr_t)mf->mesh.get());
  mix(mf->mesh->version);
  uint64_t topo_key = key;  // (ADR 0075: the key without the base's deform_version - what the subdivision chain follows)
  auto tmix = [&](uint64_t v) { topo_key = (topo_key ^ v) * kFnvPrime; };
  mix(mf->mesh->deform_version);  // (ADR 0075: a positions-only change - a sculpt dab - re-evaluates the stack; the views' keys already read it)
  /* Shape keys first (task 0135): the basis with the renderer's keys added, then the modifiers (the skin) on that.
   * Edit Mode's cage (kind 2) shows the basis it edits, as Blender does. */
  const MeshRenderer *shaping = kind == 2 ? nullptr : get<MeshRenderer>();
  const uint64_t shape_hash = shape_key_hash(*mf->mesh, shaping);
  const bool shaped = shape_hash != 0;
  auto on = [kind](const Component &c) {
    return c.is_modifier() && (kind == 1 ? c.show_in_render : c.enabled && (kind == 0 || c.show_in_editmode));
  };
  /* The deforming tail (ADR 0022): the modifiers after the last one that changes topology. Edit Mode's
   * cage (kind 2) keeps the full path. */
  size_t split = 0;
  for (size_t i = 0; i < components.size(); i++)
    if (on(*components[i]) && (!components[i]->is_deform_only() || kind == 2)) split = i + 1;
  uint64_t head_key = key, head_topo_key = topo_key;
  bool any_head = false, any_tail = false;
  const SubdivisionSurface *chain_sub = nullptr;  // (ADR 0075: the head is one Subdivision Surface, levels 1-3)
  int head_count = 0;
  for (size_t i = 0; i < components.size(); i++) {
    Component &c = *components[i];
    if (!on(c)) continue;
    any = true;
    (i < split ? any_head : any_tail) = true;
    const uint64_t hc = hash_component(c), hd = c.modifier_dependency_hash();
    mix(hc);
    mix(hd);
    if (i < split) tmix(hc), tmix(hd), head_count++, chain_sub = dynamic_cast<const SubdivisionSurface *>(&c);
    if (i + 1 == split) head_key = key, head_topo_key = topo_key;
  }
  if (head_count != 1 || !chain_sub || chain_sub->levels < 1 || chain_sub->levels > 3) chain_sub = nullptr;
  /* The weights are part of the head when a modifier runs before the tail; otherwise the keys are added on the
   * deform path, so a weight changing frame by frame deforms again rather than copying the mesh. */
  if (shaped && any_head) head_key = (head_key ^ shape_hash) * kFnvPrime, head_topo_key = (head_topo_key ^ shape_hash) * kFnvPrime;
  if (shaped) mix(shape_hash);
  if (shaped && !any_head) any_tail = true;  // (shaped from the basis's positions again each time, as a deformer is)
  if (!any && !shaped) {  // (nothing drawn over the mesh: the caches go with it - a chain's tables are not kept for nothing)
    eval_mesh_[kind] = nullptr, eval_head_[kind] = nullptr, eval_chain_[kind] = nullptr;
    return mf->mesh.get();
  }
  if (eval_mesh_[kind] && eval_key_[kind] == key) return eval_mesh_[kind].get();
  auto run = [&](Mesh &m, size_t from, size_t to) {
    for (size_t i = from; i < to; i++)
      if (on(*components[i])) run_modifier(*components[i], m);
  };
  /* The head: built whole when its key changed - or, for one Subdivision Surface whose base only moved (ADR 0075),
   * followed in place by its chain, the moved vertices' footprint recomputed level by level. */
  bool head_followed = false;  // (the chain moved the head's positions only: its tail may deform again in place)
  auto chain_follows = [&]() {
    if (!chain_sub || !eval_chain_[kind] || !eval_head_[kind] || eval_head_topo_[kind] != head_topo_key) return false;
    std::vector<Vec3> base = mf->mesh->positions;
    apply_shape_keys(*mf->mesh, shaping, base);
    const bool shared_out = eval_mesh_[kind] == eval_head_[kind];
    if (eval_head_[kind].use_count() > (shared_out ? 2 : 1)) {  // (a snapshot holds the head: it is written through a copy)
      auto c = std::make_shared<Mesh>(*eval_head_[kind]);
      if (shared_out) eval_mesh_[kind] = c;
      eval_head_[kind] = c;
    }
    const size_t written = eval_chain_[kind]->update(base, *eval_head_[kind]);
    if (written == meshops::SubdivChain::kRefused) return false;  // (not the mesh the chain was built for: rebuilt)
    if (written) eval_head_[kind]->deform_version++;
    eval_head_key_[kind] = head_key;
    head_followed = true;
    return true;
  };
  auto refresh_head = [&]() {
    if (eval_head_[kind] && eval_head_key_[kind] == head_key) return;
    if (chain_follows()) return;
    auto h = std::make_shared<Mesh>(*mf->mesh);
    apply_shape_keys(*mf->mesh, shaping, h->positions);
    h->shape_keys.clear();  // (an evaluated mesh is shaped already: Apply Modifiers must not add the keys again)
    eval_chain_[kind] = nullptr;
    if (chain_sub) {
      const Mesh in = *h;
      eval_chain_[kind] = meshops::SubdivChain::build(in, chain_sub->levels, chain_sub->smooth, *h);
      if (eval_chain_[kind] && chain_sub->smooth) h->smooth = true;                                   // (as the modifier sets it)
      if (eval_chain_[kind] && !in.vertex_groups.empty()) meshops::transfer_vertex_groups(in, *h);  // (as run_modifier carries them, by position)
    }
    if (!eval_chain_[kind]) run(*h, 0, split);
    h->touch();
    eval_head_[kind] = h;
    eval_head_key_[kind] = head_key;
    eval_head_topo_[kind] = head_topo_key;
  };
  if (!any_tail && chain_sub) {  // (the stack is the subdivision alone: the head is the result)
    refresh_head();
    eval_mesh_[kind] = eval_head_[kind];
    eval_key_[kind] = key;
    eval_from_[kind] = 0;
    return eval_mesh_[kind].get();
  }
  if (!any_tail) {
    auto m = std::make_shared<Mesh>(*mf->mesh);
    apply_shape_keys(*mf->mesh, shaping, m->positions);
    m->shape_keys.clear();  // (an evaluated mesh is shaped already: Apply Modifiers must not add the keys again)
    run(*m, 0, components.size());
    m->touch();
    eval_mesh_[kind] = m;
    eval_key_[kind] = key;
    eval_from_[kind] = 0;
    eval_head_[kind] = nullptr;  // nothing to deform from: don't keep a second copy
    eval_chain_[kind] = nullptr;
    return eval_mesh_[kind].get();
  }
  const Mesh *head = mf->mesh.get();
  if (any_head) {
    refresh_head();
    head = eval_head_[kind].get();
  }
  else
    eval_head_[kind] = nullptr, eval_chain_[kind] = nullptr;
  std::shared_ptr<Mesh> m = eval_mesh_[kind];
  const bool same_topology = m && m->positions.size() == head->positions.size() && m->corner_verts.size() == head->corner_verts.size() &&
                             m->face_offsets.size() == head->face_offsets.size();
  /* The tail deforms again in place when the head is the one it was deformed from (its key), or when the chain moved
   * that head's positions only (its topology key); a head rebuilt whole takes the full path - its connectivity may
   * have changed under the same counts. */
  const bool same_head = eval_from_[kind] == head_key || (head_followed && eval_from_topo_[kind] == head_topo_key);
  if (m && same_head && same_topology) {
    /* Deform again from the head's positions. A copy shared with a snapshot (Scene::clone) or another
     * scene is never written: it is copied first, render mesh included, which still saves the rebuild. */
    if (m.use_count() > 2) m = std::make_shared<Mesh>(*m);  // more holders than eval_mesh_ and this one
    const uint64_t v = m->version;
    m->positions = head->positions;
    m->custom_normals = head->custom_normals;  // (task 0149: a normal modifier starts from the head's, not its own last result)
    if (!any_head) apply_shape_keys(*mf->mesh, shaping, m->positions);  // (no head: the basis shaped here)
    m->deform_version++;  // before the tail runs: a deformer reading render_mesh() must not get last frame's
    run(*m, split, components.size());
    if (m->positions.size() == head->positions.size() && m->corner_verts.size() == head->corner_verts.size() &&
        m->face_offsets.size() == head->face_offsets.size())
      m->version = v;  // the deformers touch() it; only the positions moved
    else
      m->touch();  // a modifier that claims to deform changed topology: rebuild as usual
  }
  else {
    m = std::make_shared<Mesh>(*head);
    if (!any_head) apply_shape_keys(*mf->mesh, shaping, m->positions);
    m->shape_keys.clear();
    run(*m, split, components.size());
    m->touch();
  }
  eval_mesh_[kind] = m;
  eval_key_[kind] = key;
  eval_from_[kind] = head_key;
  eval_from_topo_[kind] = head_topo_key;
  return eval_mesh_[kind].get();
}

Curve *GameObject::curve() const { return static_cast<Curve *>(find_role(Component::RenderRole::Curve)); }
Lattice *GameObject::lattice() const { return static_cast<Lattice *>(find_role(Component::RenderRole::Lattice)); }
Component *GameObject::point_source() const {
  for (auto &c : components)
    if (c->render_role() == Component::RenderRole::Curve || c->render_role() == Component::RenderRole::Lattice) return c.get();
  return nullptr;
}

AABB GameObject::world_bounds() const {
  const Mesh *m = evaluated_mesh();
  if (!m) {
    AABB b;
    b.add(world_position());
    return b;
  }
  return m->render_mesh().bounds.transformed(world_matrix());
}

/* ===================================================================== */
/* Scene                                                                  */
/* ===================================================================== */

Scene::Scene() {
  static std::atomic<uint64_t> next{1};
  serial = next.fetch_add(1, std::memory_order_relaxed);
}

void Scene::move_from(Scene &o) {
  serial = o.serial;
  name = std::move(o.name);
  path = std::move(o.path);
  compress = o.compress;
  environment = std::move(o.environment);
  render = o.render;
  lighting = o.lighting;
  guides = std::move(o.guides);
  sketch = std::move(o.sketch);
  active_camera = o.active_camera;
  sequence = o.sequence;
  director.reset();  // (a Play session's player belongs to it)
  roots = std::move(o.roots);
  objects_ = std::move(o.objects_);
  by_id_ = std::move(o.by_id_);
  physics = std::move(o.physics);
  next_id_ = o.next_id_;
  for (auto &g : objects_) g->scene = this;
  o.roots.clear();
  o.objects_.clear();
  o.by_id_.clear();
}

GameObject *Scene::adopt(std::unique_ptr<GameObject> go, uint64_t id) {
  go->id = id;
  go->scene = this;
  go->index_in_scene = objects_.size();
  GameObject *raw = go.get();
  by_id_[id] = raw;
  objects_.push_back(std::move(go));
  next_id_ = std::max(next_id_, id + 1);
  return raw;
}

GameObject *Scene::create(const std::string &name, GameObject *parent) {
  auto go = std::make_unique<GameObject>();
  go->name = name;
  GameObject *raw = adopt(std::move(go), next_id_);
  raw->parent = parent;
  if (parent) parent->children.push_back(raw);
  else roots.push_back(raw);
  return raw;
}

void Scene::destroy(GameObject *go) {
  if (!go) return;
  if (go->id == active_camera) active_camera = 0;  // back to the name rule
  while (!go->children.empty()) destroy(go->children.back());
  auto &list = go->parent ? go->parent->children : roots;
  list.erase(std::remove(list.begin(), list.end(), go), list.end());
  by_id_.erase(go->id);
  size_t idx = go->index_in_scene;
  if (idx != objects_.size() - 1) {
    std::swap(objects_[idx], objects_.back());
    objects_[idx]->index_in_scene = idx;
  }
  objects_.pop_back();
}

void Scene::clear() {
  objects_.clear();
  by_id_.clear();
  roots.clear();
  next_id_ = 1;
  active_camera = 0;
  sequence.clear();
  director.reset();
}

GameObject *Scene::find(uint64_t id) const {
  auto it = by_id_.find(id);
  return it == by_id_.end() ? nullptr : it->second;
}

GameObject *Scene::find_by_name(const std::string &n) const {
  for (auto &o : objects_)
    if (o->name == n) return o.get();
  return nullptr;
}

bool Scene::is_ancestor(const GameObject *ancestor, const GameObject *node) const {
  for (const GameObject *p = node; p; p = p->parent)
    if (p == ancestor) return true;
  return false;
}

void Scene::set_parent(GameObject *child, GameObject *new_parent, int index, bool keep_world) {
  if (!child || child == new_parent || (new_parent && is_ancestor(child, new_parent))) return;
  Mat4 world = child->world_matrix();
  auto &old_list = child->parent ? child->parent->children : roots;
  old_list.erase(std::remove(old_list.begin(), old_list.end(), child), old_list.end());
  child->parent = new_parent;
  auto &list = new_parent ? new_parent->children : roots;
  if (index < 0 || index > (int)list.size()) list.push_back(child);
  else list.insert(list.begin() + index, child);
  child->world_dirty_ = false;  // force mark_dirty to propagate
  child->mark_dirty();
  if (keep_world) child->set_world_matrix(world);
}

GameObject *Scene::duplicate(GameObject *go) { return duplicate_many({go}).front(); }

/* A copy's name is "Name (n)" with the smallest n no sibling has (Unity's rule); its place is right after the original.
 * Task 0207: the siblings' names gathered once per parent, each base keeping where its count got to, and each list
 * rebuilt once - one copy at a time tried every n against every sibling and moved the list each time: 10,000 copies
 * under one parent took eleven minutes. */
std::vector<GameObject *> Scene::duplicate_many(const std::vector<GameObject *> &gos) {
  struct Names {
    std::unordered_set<std::string> used;
    std::unordered_map<std::string, int> next;  // the first n not yet tried, per base
  };
  std::unordered_map<GameObject *, Names> names;  // per parent (nullptr: the roots)
  std::unordered_map<GameObject *, std::vector<GameObject *>> after;  // each original's copies, as they will follow it
  std::unordered_set<GameObject *> copies;
  /* A copy of a subtree in which an earlier copy already sits after its original (one at a time, it would be there). */
  std::function<GameObject *(GameObject *, GameObject *)> copy = [&](GameObject *src, GameObject *parent) {
    GameObject *dst = create(src->name, parent);
    dst->active = src->active;
    dst->set_local(src->local());
    for (auto &c : src->components) dst->add_component(c->clone());
    for (GameObject *ch : src->children) {
      if (copies.count(ch)) continue;
      copy(ch, dst);
      auto a = after.find(ch);
      if (a != after.end())
        for (GameObject *later : a->second) copy(later, dst);
    }
    return dst;
  };
  std::vector<GameObject *> out;
  out.reserve(gos.size());
  for (GameObject *go : gos) {
    auto &list = go->parent ? go->parent->children : roots;
    auto found = names.find(go->parent);
    if (found == names.end()) {  // (before any copy joins the list)
      found = names.emplace(go->parent, Names{}).first;
      for (GameObject *s : list) found->second.used.insert(s->name);
    }
    Names &nm = found->second;
    GameObject *d = copy(go, go->parent);
    std::string base = go->name;
    const size_t p = base.rfind(" (");
    if (p != std::string::npos && base.back() == ')') base = base.substr(0, p);
    int &n = nm.next.emplace(base, 1).first->second;
    std::string cand = base + " (" + std::to_string(n) + ")";
    while (nm.used.count(cand)) cand = base + " (" + std::to_string(++n) + ")";
    n++;
    nm.used.insert(cand);
    d->name = cand;
    auto &follow = after[go];
    follow.insert(follow.begin(), d);  // (the latest copy of an object goes right after it, ahead of earlier ones)
    copies.insert(d);
    out.push_back(d);
  }
  /* Each list once: the copies out of the end, each set right after its original (like Unity). */
  for (auto &kv : names) {
    auto &list = kv.first ? kv.first->children : roots;
    std::vector<GameObject *> rebuilt, stray;
    rebuilt.reserve(list.size());
    std::unordered_set<GameObject *> placed;
    for (GameObject *c : list) {
      if (copies.count(c)) {
        stray.push_back(c);
        continue;
      }
      rebuilt.push_back(c);
      auto a = after.find(c);
      if (a == after.end()) continue;
      rebuilt.insert(rebuilt.end(), a->second.begin(), a->second.end());
      placed.insert(a->second.begin(), a->second.end());
    }
    for (GameObject *c : stray)  // (an original no longer in its parent's list: its copy stays at the end, as before)
      if (!placed.count(c)) rebuilt.push_back(c);
    list = std::move(rebuilt);
  }
  return out;
}

std::unique_ptr<Scene> Scene::clone() const {
  auto s = std::make_unique<Scene>();
  s->name = name;
  s->path = path;
  s->compress = compress;
  s->environment = environment;
  s->render = render;
  s->lighting = lighting;
  s->guides = guides;
  s->sketch = sketch;
  s->active_camera = active_camera;
  s->sequence = sequence;
  s->objects_.reserve(objects_.size());
  std::unordered_map<const Material *, MaterialPtr> mat_copy;
  for (auto &o : objects_) {
    auto c = std::make_unique<GameObject>();
    c->name = o->name;
    c->active = o->active;
    c->local_ = o->local_;
    c->world_ = o->world_;
    c->world_dirty_ = o->world_dirty_;
    for (int k = 0; k < 3; k++) {
      c->eval_mesh_[k] = o->eval_mesh_[k];
      c->eval_key_[k] = o->eval_key_[k];
      c->eval_head_[k] = o->eval_head_[k];
      c->eval_head_key_[k] = o->eval_head_key_[k];
      c->eval_from_[k] = o->eval_from_[k];
    }
    for (auto &comp : o->components) {
      auto cc = comp->clone();
      cc->owner = c.get();
      /* Materials are small and edited in place, so snapshots get their own
       * copies (shared slots stay shared); meshes stay copy-on-write. */
      if (auto *mr = dynamic_cast<MeshRenderer *>(cc.get()))
        for (auto &m : mr->materials)
          if (m) {
            auto it = mat_copy.find(m.get());
            if (it == mat_copy.end()) it = mat_copy.emplace(m.get(), std::make_shared<Material>(*m)).first;
            m = it->second;
          }
      c->components.push_back(std::move(cc));
    }
    s->adopt(std::move(c), o->id);
  }
  for (auto &o : objects_) {
    GameObject *c = s->find(o->id);
    if (o->parent) c->parent = s->find(o->parent->id);
    c->children.reserve(o->children.size());
    for (GameObject *ch : o->children) c->children.push_back(s->find(ch->id));
  }
  for (GameObject *r : roots) s->roots.push_back(s->find(r->id));
  s->next_id_ = next_id_;
  return s;
}

void Scene::for_each(const std::function<void(GameObject &)> &fn) const {
  for (auto &o : objects_) fn(*o);
}

void Scene::for_each_ordered(const std::function<void(GameObject &, int)> &fn) const {
  std::vector<std::pair<GameObject *, int>> stack;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back({*it, 0});
  while (!stack.empty()) {
    auto [g, d] = stack.back();
    stack.pop_back();
    fn(*g, d);
    for (auto it = g->children.rbegin(); it != g->children.rend(); ++it) stack.push_back({*it, d + 1});
  }
}

size_t Scene::memory_bytes() const {
  size_t b = sizeof(Scene) + objects_.capacity() * sizeof(void *) + by_id_.size() * 32;
  std::vector<const Mesh *> meshes;
  for (auto &o : objects_) {
    b += sizeof(GameObject) + o->name.capacity() + o->children.capacity() * sizeof(void *);
    b += o->components.size() * 96;
    if (auto *mf = o->get<MeshFilter>())
      if (mf->mesh) meshes.push_back(mf->mesh.get());
  }
  std::sort(meshes.begin(), meshes.end());
  meshes.erase(std::unique(meshes.begin(), meshes.end()), meshes.end());
  for (auto *m : meshes) b += m->memory_bytes();
  return b;
}

void Scene::start(PlayContext &ctx) {
  ctx.scene = this;
  for (auto &o : objects_)
    if (o->active_in_hierarchy())
      for (auto &c : o->components)
        if (c->enabled) c->start(ctx);
  /* Jolt world for this play session (null without Jolt or rigid bodies). */
  physics = physics_create(*this);
  /* The scene's sequence (task 0070): a Sequencer of the scene's own, with no object (its bars name theirs). */
  director.reset();
  if (!sequence.empty()) {
    auto d = std::make_shared<Sequencer>();
    d->sequence = sequence;
    d->start(ctx);
    director = d;
  }
}

void Scene::update(PlayContext &ctx) {
  ctx.scene = this;
  /* Snapshot the list: scripts may create objects. */
  std::vector<GameObject *> list;
  list.reserve(objects_.size());
  for (auto &o : objects_) list.push_back(o.get());
  for (GameObject *o : list)
    if (o->active_in_hierarchy())
      for (auto &c : o->components)
        if (c->enabled) c->update(ctx);
  if (director) director->update(ctx);  // (after the components: the scene's sequence has the last word, before IK)
  if (physics) physics_world_step(*physics, ctx.dt);
  else physics_step(*this, ctx.dt);
}

/* ===================================================================== */
/* Physics (GEA Vol. II ch. 14 "Collision and Rigid Body Dynamics")       */
/* ===================================================================== */

void physics_step(Scene &scene, float dt) {
  if (dt <= 0) return;
  struct Body {
    GameObject *go;
    Rigidbody *rb;
    Vec3 c;
    float r;
  };
  std::vector<Body> bodies;
  std::vector<AABB> statics;
  scene.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *rb = g.get<Rigidbody>();
    if (rb && rb->enabled) {
      AABB b = g.world_bounds();
      Vec3 e = b.extent();
      bodies.push_back({&g, rb, b.center(), std::max({e.x, e.y, e.z, 0.05f})});
    }
    else if (g.get<MeshFilter>() && g.get<MeshRenderer>()) {
      statics.push_back(g.world_bounds());
    }
  });
  const float sub = 4;  // sub-steps for stability
  const float h = dt / sub;
  for (int s = 0; s < (int)sub; s++) {
    for (Body &b : bodies) {
      if (b.rb->is_kinematic) continue;
      if (b.rb->use_gravity) b.rb->velocity.y -= 9.81f * h;
      b.rb->velocity *= std::max(0.0f, 1.0f - b.rb->drag * h);
      b.c += b.rb->velocity * h;
      for (const AABB &box : statics) {
        Vec3 q = vmax(box.min, vmin(b.c, box.max));
        Vec3 d = b.c - q;
        float dist = length(d);
        if (dist >= b.r) continue;
        Vec3 n = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
        b.c += n * (b.r - dist);
        float vn = dot(b.rb->velocity, n);
        if (vn < 0) {
          b.rb->velocity -= n * ((1.0f + b.rb->bounciness) * vn);
          Vec3 vt = b.rb->velocity - n * dot(b.rb->velocity, n);
          b.rb->velocity -= vt * std::min(1.0f, 4.0f * h);  // friction
        }
      }
    }
    for (size_t i = 0; i < bodies.size(); i++)
      for (size_t j = i + 1; j < bodies.size(); j++) {
        Body &a = bodies[i], &b = bodies[j];
        Vec3 d = b.c - a.c;
        float dist = length(d), rr = a.r + b.r;
        if (dist >= rr || dist < 1e-6f) continue;
        Vec3 n = d / dist;
        float ia = a.rb->is_kinematic ? 0.0f : 1.0f / a.rb->mass, ib = b.rb->is_kinematic ? 0.0f : 1.0f / b.rb->mass;
        if (ia + ib <= 0) continue;
        float pen = rr - dist;
        a.c -= n * (pen * ia / (ia + ib));
        b.c += n * (pen * ib / (ia + ib));
        float vrel = dot(b.rb->velocity - a.rb->velocity, n);
        if (vrel < 0) {
          float e = std::min(a.rb->bounciness, b.rb->bounciness);
          float j_imp = -(1.0f + e) * vrel / (ia + ib);
          a.rb->velocity -= n * (j_imp * ia);
          b.rb->velocity += n * (j_imp * ib);
        }
      }
  }
  for (Body &b : bodies) {
    if (b.rb->is_kinematic) continue;
    AABB cur = b.go->world_bounds();
    b.go->set_world_position(b.go->world_position() + (b.c - cur.center()));
  }
}

/* ===================================================================== */
/* Default content                                                        */
/* ===================================================================== */

GameObject *create_primitive(Scene &scene, const std::string &kind, GameObject *parent) {
  MeshPtr mesh;
  if (kind == "Cube") mesh = primitives::cube();
  else if (kind == "Sphere") mesh = primitives::uv_sphere();
  else if (kind == "Icosphere") mesh = primitives::ico_sphere();
  else if (kind == "Cylinder") mesh = primitives::cylinder();
  else if (kind == "Cone") mesh = primitives::cone();
  else if (kind == "Torus") mesh = primitives::torus();
  else if (kind == "Plane") mesh = primitives::plane();
  else if (kind == "Quad") mesh = primitives::quad();
  else if (kind == "Teapot") mesh = primitives::teapot();
  GameObject *go = scene.create(kind, parent);
  if (mesh) {
    go->add<MeshFilter>()->mesh = mesh;
    go->add<MeshRenderer>();
  }
  else if (kind == "Directional Light" || kind == "Point Light" || kind == "Spot Light" || kind == "Area Light") {
    auto *l = go->add<Light>();
    l->type = kind == "Point Light" ? 1 : kind == "Spot Light" ? 2 : kind == "Area Light" ? 3 : 0;
    if (l->type == 0) go->set_local_euler({50, -30, 0});
    if (l->type >= 2) {
      go->set_local_position({0, 3, 0});
      go->set_local_euler({90, 0, 0});  // pointing down at the scene
      l->color = {1, 1, 1};
      l->intensity = 2.0f;
    }
  }
  else if (kind == "Camera") {
    go->add<Camera>();
  }
  return go;
}

void build_default_scene(Scene &scene) {
  scene.clear();
  scene.name = "SampleScene";
  /* Blender's default view transform is AgX (OpenColorIO); built-in Filmic otherwise. */
  const int agx = colormanagement::view_index("AgX");
  scene.render.view_transform = agx >= 0 ? 3 + agx : 1;
  GameObject *cam = create_primitive(scene, "Camera");
  cam->name = "Main Camera";
  cam->set_local_position({0, 2.2f, -6.5f});
  cam->set_local_euler({14, 0, 0});
  GameObject *sun = create_primitive(scene, "Directional Light");
  sun->set_local_position({0, 3, 0});
  sun->set_local_euler({50, 80, 0});  // side light so shadows read from both default views
  GameObject *ground = create_primitive(scene, "Plane");
  ground->get<MeshRenderer>()->materials = {make_material("Ground", {0.55f, 0.56f, 0.58f})};
  GameObject *cube = create_primitive(scene, "Cube");
  cube->set_local_position({0, 0.5f, 0});
  GameObject *sphere = create_primitive(scene, "Sphere");
  sphere->set_local_position({1.6f, 0.5f, 0.8f});
  sphere->get<MeshRenderer>()->materials = {make_material("Blue", {0.35f, 0.6f, 0.95f})};
  GameObject *cyl = create_primitive(scene, "Cylinder");
  cyl->set_local_position({-1.7f, 0.5f, 0.9f});
  cyl->set_local_scale({0.8f, 0.5f, 0.8f});
  cyl->get<MeshRenderer>()->materials = {make_material("Orange", {0.95f, 0.55f, 0.2f})};
}

void build_starter_scene(Scene &scene) {
  scene.clear();
  scene.name = "SampleScene";
  const int agx = colormanagement::view_index("AgX");
  scene.render.view_transform = agx >= 0 ? 3 + agx : 1;
  /* Blender's start-up file has a cube, a camera and a point light; Blendity's
   * has the Utah teapot on a ground plane, a camera and a point light. */
  GameObject *cam = create_primitive(scene, "Camera");
  cam->name = "Main Camera";
  cam->set_local_position({0, 2.0f, -5.0f});
  cam->set_local_euler({16, 0, 0});
  GameObject *light = create_primitive(scene, "Point Light");
  light->name = "Point Light";
  light->set_local_position({2.0f, 3.0f, -2.0f});
  Light *l = light->get<Light>();
  l->color = {1, 1, 1};
  l->intensity = 3.0f;
  l->range = 15.0f;
  GameObject *ground = create_primitive(scene, "Plane");
  ground->get<MeshRenderer>()->materials = {make_material("Ground", {0.55f, 0.56f, 0.58f})};
  GameObject *pot = create_primitive(scene, "Teapot");
  pot->name = "Utah Teapot";
  pot->get<MeshRenderer>()->materials = {make_material("Teapot", {0.85f, 0.82f, 0.76f})};
}

}  // namespace bl
