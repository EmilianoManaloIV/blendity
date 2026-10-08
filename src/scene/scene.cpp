// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene.h"

#include "../core/core.h"
#include "../platform/platform.h"
#include "../render/colormanagement.h"
#include "../render/shading.h"
#include "physics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
  reg<MirrorModifier>("Mesh", "Mirrors the mesh across its local X / Y / Z planes.", "Mirror modifier");
  reg<ArrayModifier>("Mesh", "Repeats the mesh with an offset.", "Array modifier");
  reg<SolidifyModifier>("Mesh", "Gives surfaces thickness (a shell with rim faces).", "Solidify modifier (Simple)");
  reg<BooleanModifier>("Mesh", "Cuts, joins or intersects with another object (Manifold).", "Boolean modifier (Manifold solver)");
  reg<DecimateModifier>("Mesh", "Reduces the triangle count (meshoptimizer).", "Decimate modifier (Collapse)");
  reg<Light>("Rendering", "A directional (sun) or point light.", "Light object (Sun / Point)");
  reg<Camera>("Rendering", "Renders the Game view.", "Camera object (the active scene camera)");
  reg<Rotator>("Scripts", "Spins the object while in Play mode.", "A driver or keyframed rotation");
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
  }
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

void RenderSettings::reflect(Reflector &r) {
  static const char *engines[] = {"Rasterized (EEVEE-like)", "Path Traced (Cycles-like)"};
  static const char *formats[] = {"PNG", "JPEG", "Radiance HDR", "OpenEXR (half float)"};
  r.enumeration("Render Engine", engine, engines, 2);
  r.help("Rasterized: real-time deferred PBR with shadow maps. Path Traced: unbiased Monte Carlo light transport.");
  r.field("Resolution X", width, 16, 16384);
  r.field("Resolution Y", height, 16, 16384);
  r.field("Resolution %", percent, 1, 400);
  r.field("Preview Resolution %", preview_percent, 5, 100);
  r.help("Size of the Preview render, as a percentage of the output resolution.");
  r.samples("Preview Samples", preview_samples, 1, 65536);
  r.help("Path-traced samples for the Preview render (the rasterized engine ignores it).");
  r.field("Live Preview", live_preview);
  r.help("Re-render the preview automatically whenever the scene, camera or settings change.");
  if (r.all_fields() || engine == 1) {
    static const char *devices[] = {"CPU", "GPU Compute"};
    r.enumeration("Device", device, devices, 2);
    r.help("GPU Compute renders on the GPUs ticked under Render Devices (Vulkan: NVIDIA, AMD, Intel).\n"
           "Blender: Render Properties > Device, with the GPUs chosen in Preferences > System.");
    if (r.all_fields() || device == 1) {
      r.field("Also Use the CPU", gpu_with_cpu);
      r.help("Combined rendering: the CPU renders samples alongside the GPUs and the results are averaged\n"
             "(Blender: tick the CPU in the Render Devices list).");
      r.field("Hardware Ray Tracing", hardware_rt);
      r.help("Use the GPU's ray tracing hardware (NVIDIA RT cores, AMD ray accelerators, Intel RTUs) when it has it,\n"
             "instead of traversing Blendity's BVH in a compute shader (Blender: OptiX vs CUDA, HIP RT vs HIP).");
    }
    r.samples("Samples", samples, 1, 65536);
    r.help("Samples per pixel for the final render. Noise falls as 1/sqrt(samples): each doubling cuts it by ~30%.\nBlender: Sampling > Render > Max Samples.");
    r.samples("Viewport Samples", viewport_samples, 1, 65536);
    r.field("Max Bounces", max_bounces, 0, 64);
    r.field("Clamp Indirect", clamp_indirect, 0.1f, 0.0f, 1000.0f);
    r.help("Limits bright indirect samples to remove fireflies (0 = off). Blender: Light Paths > Clamping.");
    r.field("Denoise", denoise);
    static const char *denoisers[] = {"OpenImageDenoise", "A-Trous (built-in)"};
    r.enumeration("Denoiser", denoiser, denoisers, 2);
    r.help("OpenImageDenoise is Cycles' default AI denoiser (needs Blender's libraries). A-Trous is the built-in edge-avoiding filter.");
    r.field("Embree Ray Tracing", use_embree);
    r.help("Use Embree, Cycles' CPU ray tracing kernels, instead of Blendity's own BVH (needs Blender's libraries).");
    r.field("Path Guiding", path_guiding);
    r.help("Learns where light comes from and steers bounces there (OpenPGL, as Cycles' Light Paths > Path Guiding). Helps light that arrives indirectly or from small emissive surfaces.");
  }
  if (r.all_fields() || engine != 1) {
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
         "forward (blue) axis. Area: a rectangle or disc that lights its front, with soft shadows when path traced.\n"
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
  r.help("Tint the light with a black body's colour at the temperature below, as photographers and\n"
         "lighting artists do (Unity HDRP: Color Temperature; Blender: a Blackbody node).");
  if (use_temperature || all) {
    r.field("Temperature (K)", temperature, 25.0f, 1000.0f, 40000.0f);
    r.help("Kelvin: 1900 candle, 2700 tungsten bulb, 3500 halogen, 4000 moonlight / fluorescent,\n"
           "5500 noon sun, 6500 overcast sky (white), 9000+ blue sky in shade.");
  }
  r.field("Intensity", intensity, 0.01f, 0.0f, 8.0f);
  if (r.all_fields() || type != 0) r.field("Range", range, 0.05f, 0.01f, 1000.0f);
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
  r.help("Blur what is nearer or farther than the focus distance, like a real lens (path-traced renders).\n"
         "Blender: Camera > Depth of Field. Unity: HDRP Physical Camera aperture.");
  if (dof || all) {
    r.field("Focus Distance (m)", focus_distance, 0.05f, 0.01f, 100000.0f);
    r.field("F-Stop", f_stop, 0.05f, 0.1f, 128.0f);
    r.help("Aperture as a focal ratio: lower numbers (f/1.4) blur more, higher ones (f/16) keep more sharp.\n"
           "The aperture's diameter is focal length / f-stop.");
    r.field("Aperture Blades", blades, 0, 16);
    r.help("0: a round aperture. 3 or more: polygonal bokeh with that many sides.");
    r.field("Blade Rotation", blade_rotation, 0.5f, -180.0f, 180.0f);
  }
  r.field("Physical Exposure", physical_exposure);
  r.help("Brightness from ISO, shutter speed and f-stop, like a real camera. 0 stops at ISO 100, 1/60 s, f/2.8;\n"
         "each doubling of ISO or exposure time adds a stop. Unity: Physical Camera exposure.");
  if (physical_exposure || all) {
    r.field("ISO", iso, 10.0f, 1.0f, 409600.0f);
    r.field("Shutter Speed (1/s)", shutter, 1.0f, 0.001f, 100000.0f);
    if (!dof && !all) r.field("F-Stop", f_stop, 0.05f, 0.1f, 128.0f);  // shown with Depth of Field otherwise
  }
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

void Rotator::reflect(Reflector &r) { r.field("Degrees Per Second", degrees_per_second); }
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
}
void SmoothModifier::modify(Mesh &m) const { meshops::smooth_laplacian(m, factor, iterations); }

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
  r.field("Count", count, 1, 1000);
  r.field("Relative Offset", relative_offset);
  r.help("Offset as a multiple of the mesh size (Blender: Relative Offset > Factor).");
  r.field("Constant Offset", constant_offset);
  r.field("Merge", merge);
  r.field("Merge Distance", merge_distance, 0.0005f, 0.0f, 1.0f);
}
void ArrayModifier::modify(Mesh &m) const {
  meshops::make_array(m, count, relative_offset, constant_offset, merge ? merge_distance : 0.0f);
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
  r.text("Object", object);
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
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  };
  /* Relative placement + the cutter's mesh: moving either object re-cuts. */
  Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  mix(rel.m, sizeof(rel.m));
  mix(&tm, sizeof(tm));
  if (tm) mix(&tm->version, sizeof(tm->version));
  return h;
}

void DecimateModifier::reflect(Reflector &r) {
  r.field("Ratio", ratio, 0.01f, 0.0f, 1.0f);
  r.help("Fraction of triangles to keep (Blender: Decimate > Collapse > Ratio). Uses meshoptimizer; UV seams and material borders are kept.");
  if (!meshops::decimate_available()) r.help("This build has no meshoptimizer (needs Blender's libraries): the modifier does nothing.");
}
void DecimateModifier::modify(Mesh &m) const { meshops::decimate(m, ratio); }

/* ===================================================================== */
/* Hashing reflector                                                      */
/* ===================================================================== */

namespace {
struct HashReflector : Reflector {
  bool all_fields() const override { return true; }
  uint64_t h = 1469598103934665603ull;
  void mix(const void *p, size_t n) {
    auto *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
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
};
}  // namespace

uint64_t hash_component(Component &c) {
  HashReflector hr;
  hr.mix(c.type_name(), std::strlen(c.type_name()));
  hr.mix(&c.enabled, 1);
  c.reflect(hr);
  return hr.h;
}

uint64_t hash_reflect(const std::function<void(Reflector &)> &fn) {
  HashReflector hr;
  fn(hr);
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

void GameObject::set_world_matrix(const Mat4 &world) {
  Mat4 l = parent ? parent->world_matrix().inverse() * world : world;
  Transform t = local_;
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
  set_local(t);
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

const Mesh *GameObject::evaluated_mesh() const {
  auto *mf = get<MeshFilter>();
  if (!mf || !mf->mesh) return nullptr;
  uint64_t key = 1469598103934665603ull;
  bool any = false;
  auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
  mix((uint64_t)(uintptr_t)mf->mesh.get());
  mix(mf->mesh->version);
  for (auto &c : components)
    if (c->is_modifier() && c->enabled) {
      any = true;
      mix(hash_component(*c));
      mix(c->modifier_dependency_hash());
    }
  if (!any) return mf->mesh.get();
  if (eval_mesh_ && eval_key_ == key) return eval_mesh_.get();
  auto m = std::make_shared<Mesh>(*mf->mesh);
  for (auto &c : components)
    if (c->is_modifier() && c->enabled) c->modify(*m);
  m->touch();
  eval_mesh_ = m;
  eval_key_ = key;
  return eval_mesh_.get();
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

void Scene::move_from(Scene &o) {
  name = std::move(o.name);
  path = std::move(o.path);
  compress = o.compress;
  environment = std::move(o.environment);
  render = o.render;
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

static std::string unique_sibling_name(const std::vector<GameObject *> &siblings, const std::string &name) {
  std::string base = name;
  size_t p = base.rfind(" (");
  if (p != std::string::npos && base.back() == ')') base = base.substr(0, p);
  for (int n = 1;; n++) {
    std::string cand = base + " (" + std::to_string(n) + ")";
    bool used = false;
    for (auto *s : siblings)
      if (s->name == cand) { used = true; break; }
    if (!used) return cand;
  }
}

GameObject *Scene::duplicate(GameObject *go) {
  std::function<GameObject *(GameObject *, GameObject *)> copy = [&](GameObject *src, GameObject *parent) {
    GameObject *dst = create(src->name, parent);
    dst->active = src->active;
    dst->set_local(src->local());
    for (auto &c : src->components) dst->add_component(c->clone());
    for (GameObject *ch : src->children) copy(ch, dst);
    return dst;
  };
  GameObject *d = copy(go, go->parent);
  auto &list = go->parent ? go->parent->children : roots;
  d->name = unique_sibling_name(list, go->name);
  /* Place right after the original, like Unity. */
  list.erase(std::remove(list.begin(), list.end(), d), list.end());
  auto it = std::find(list.begin(), list.end(), go);
  list.insert(it == list.end() ? list.end() : it + 1, d);
  return d;
}

std::unique_ptr<Scene> Scene::clone() const {
  auto s = std::make_unique<Scene>();
  s->name = name;
  s->path = path;
  s->compress = compress;
  s->environment = environment;
  s->render = render;
  s->objects_.reserve(objects_.size());
  std::unordered_map<const Material *, MaterialPtr> mat_copy;
  for (auto &o : objects_) {
    auto c = std::make_unique<GameObject>();
    c->name = o->name;
    c->active = o->active;
    c->local_ = o->local_;
    c->world_ = o->world_;
    c->world_dirty_ = o->world_dirty_;
    c->eval_mesh_ = o->eval_mesh_;
    c->eval_key_ = o->eval_key_;
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

}  // namespace bl
