// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing tab with textures and cameras (task 0183, ADR 0058 amended): a texture asset's albedo opened for
// editing, the result applied back as an Image layer on top of its stack (the procedural layers stay underneath), and
// the stack's global adjustments turned into the active camera's filters ("Use as Camera Look").
#include "editor.h"

#include "../texture/procedural_texture.h"

#include <cmath>

namespace bl {

namespace {

float dec_srgb(float e) { return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f); }
float enc_srgb(float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; }

/* A texture asset by name ("Bricks") or project path. */
/* (Not Editor::texture_by_name, which takes a name only.) */
ProceduralTexturePtr texture_by_name_or_path(const std::string &s) {
  return texture_asset(s.find('/') == std::string::npos ? "Assets/Textures/" + s + ".ptex" : s);
}

/* The project path of the image an edit of `pt` is applied as, beside it. */
std::string edit_image_path(const ProceduralTexture &pt) {
  const std::string &a = pt.asset_path;
  return a.substr(0, a.size() - fs::extension(a).size()) + " edit.png";
}

/* Reads an Image layer's image path (every other field ignored). */
struct ImagePathProbe : Reflector {
  std::string path;
  void field(const char *, float &, float, float, float) override {}
  void field(const char *, int &, int, int) override {}
  void field(const char *, bool &) override {}
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
  void texture(const char *, TextureRef &t) override { path = t.path; }
};

/* The Image layer that holds `pt`'s applied edit, or null. */
TextureLayer *edit_layer(ProceduralTexture &pt, const std::string &image) {
  for (auto &l : pt.layers) {
    if (std::string(l->type_name()) != "Image") continue;
    ImagePathProbe probe;
    l->reflect(probe);
    if (probe.path == image) return l.get();
  }
  return nullptr;
}

/* Lift, gamma and gain (linear, per channel: the camera's Color Grading) that pass through a Tone Curve's black,
 * middle grey and white. */
void curve_to_lgg(const edit::Adjustment &a, ColorGradingEffect &cg) {
  for (int k = 0; k < 3; k++) {
    auto through = [&](float lin) {
      float e = a.curve[0].size() < 2 ? enc_srgb(lin) : edit::curve_eval(a.curve[0], enc_srgb(lin));
      if (a.curve[1 + k].size() >= 2) e = edit::curve_eval(a.curve[1 + k], e);
      return dec_srgb(std::max(0.0f, e));
    };
    const float lo = through(0.0f), hi = std::max(1e-4f, through(1.0f)), mid = std::max(1e-6f, through(0.18f));
    /* The camera's (gain (v + lift (1 - v)))^(1 / gamma) through all three: gamma from
     * 0.18 hi^g + 0.82 lo^g = mid^g (bisection: the left side over the right is monotone in g), then gain and lift. */
    auto f = [&](float g) { return 0.18f * std::pow(hi, g) + 0.82f * std::pow(lo, g) - std::pow(mid, g); };
    float a = 0.05f, b = 10.0f;
    const bool bracket = f(a) * f(b) < 0.0f;
    for (int it = 0; bracket && it < 60; it++) {
      const float m = 0.5f * (a + b);
      (f(a) * f(m) <= 0.0f ? b : a) = m;
    }
    const float g = bracket ? 0.5f * (a + b) : 1.0f;
    cg.gamma[k] = g;
    cg.gain[k] = clampf(std::pow(hi, g), 0.0f, 10.0f);
    cg.lift[k] = clampf(std::pow(lo, g) / std::max(1e-6f, std::pow(hi, g)), -1.0f, 1.0f);
  }
}

}  // namespace

/* ---- textures ---- */

bool Editor::edit_open_texture(const std::string &name) {
  ProceduralTexturePtr pt = texture_by_name_or_path(name);
  if (!pt) {
    Log::warn("Editing: no texture asset '%s' (a name in Assets/Textures, or a project path)", name.c_str());
    return false;
  }
  /* The albedo without an edit applied earlier, then that edit's stack again (so editing goes on, not on top). */
  const std::string image = edit_image_path(*pt);
  ProceduralTexture under = *pt;  // (a copy, the applied edit off: the asset itself isn't touched)
  TextureLayer *applied = edit_layer(under, image);
  if (applied) applied->enabled = false;
  const Bitmap albedo = under.output(0);
  edit_doc_.open_pixels(edit::image_from_bitmap(albedo), "Texture: " + pt->name);
  std::string err;
  if (applied && !edit_doc_.load_stack(resolve_asset_path(image) + ".edit", err))
    Log::warn("Editing: %s's applied edit has no adjustments beside it (%s): Apply will replace that edit", pt->name.c_str(), err.c_str());
  edit_texture_asset_ = pt->asset_path;
  edit_opened_++;
  edit_mask_select(-1, -1);
  edit_zoom_ = 0, edit_pan_ = Vec2(0, 0);
  Log::info("Editing: the texture %s's albedo (%d x %d)%s", pt->name.c_str(), albedo.width, albedo.height, applied ? ", with its applied edit's adjustments" : "");
  return true;
}

/* The result as `<texture> edit.png` beside the asset and an Image layer on top of its stack writing the albedo
 * only (the layer an earlier Apply made is reused); the stack kept beside the image for the next Edit Texture. */
bool Editor::edit_apply_to_texture() {
  ProceduralTexturePtr pt = edit_texture_asset_.empty() ? nullptr : texture_asset(edit_texture_asset_);
  if (!pt || !edit_doc_.is_open()) {
    Log::warn("Editing: open a texture first (Textures window > Edit, or develop open texture <name>)");
    return false;
  }
  const edit::Image &result = edit_doc_.result();
  if (result.w != edit_doc_.source().w || result.h != edit_doc_.source().h) {
    Log::warn("Editing: a crop or turn changed the size: a texture's edit keeps its size (turn those steps off to apply)");
    return false;
  }
  const std::string image = edit_image_path(*pt), abs = resolve_asset_path(image);
  std::string err;
  if (!edit_doc_.export_file(abs, err) || !edit_doc_.save_stack(abs + ".edit", err)) {
    Log::warn("Editing: couldn't write %s: %s", image.c_str(), err.c_str());
    return false;
  }
  texture_cache_forget(abs);
  procedural_texture_cache_forget(pt->asset_path);  // (the layer and its path may be the same as before: its file isn't)
  note_texture_edit(*pt);
  TextureLayer *layer = edit_layer(*pt, image);
  if (!layer) layer = add_edit_layer(*pt, image);
  if (!layer) return false;
  layer->enabled = true;
  pt->touch();
  mark_changed("Apply Edit to " + pt->name);
  image_assets_time_ = -100, project_listed_ = -100;
  Log::info("Editing: %s's albedo is now the edit (an Image layer on top: turn it off to see the texture without it)", pt->name.c_str());
  return true;
}

TextureLayer *Editor::add_edit_layer(ProceduralTexture &pt, const std::string &image) {
  TextureLayer *layer = pt.add("Image");
  if (!layer) return nullptr;
  layer->targets = TargetAlbedo;  // (the colour only: the stack's height and roughness stay)
  layer->opacity = 1.0f;
  if (!set_layer_field(*layer, "Image", image)) Log::warn("Editing: the Image layer didn't take %s", image.c_str());
  return layer;
}

/* ---- the camera ---- */

/* The stack's global adjustments as the Main Camera's filters: Exposure, Contrast, Saturation, Temperature / Tint and the
 * Tone Curve into a Color Grading; a darkening Vignette and Sharpen as their own. A look given before is replaced
 * (the camera's first effect of each kind is reused). Masked steps and the ones a camera filter can't do are named
 * and left out. */
bool Editor::edit_use_as_camera_look() {
  GameObject *owner = nullptr;
  if (!main_camera(*scene_, &owner) || !owner) {
    Log::warn("Editing: no Main Camera to give the look to");
    return false;
  }
  CameraLook look;
  for (const edit::Adjustment &a : edit_doc_.stack()) {
    if (!a.enabled) continue;
    if (!a.mask.empty() || !camera_look_step(a, look)) look.skipped += (look.skipped.empty() ? "" : ", ") + a.type + (a.mask.empty() ? "" : " (masked)");
  }
  if (!look.graded && !look.vignette && !look.sharpen) {
    Log::warn("Editing: nothing in this stack is a camera filter's (left out: %s)", look.skipped.empty() ? "everything" : look.skipped.c_str());
    return false;
  }
  CameraFilters *cf = owner->get<CameraFilters>();
  if (!cf) cf = owner->add<CameraFilters>();
  if (look.graded) *camera_effect<ColorGradingEffect>(*cf) = look.cg;
  if (look.vignette) *camera_effect<VignetteEffect>(*cf) = look.vg;
  if (look.sharpen) *camera_effect<SharpenEffect>(*cf) = look.sh;
  mark_changed("Use as Camera Look");
  Log::info("Editing: %s's Camera Filters now carry the look%s%s", owner->name.c_str(), look.skipped.empty() ? "" : "; left out (no camera filter does them): ",
            look.skipped.c_str());
  return true;
}

/* The camera's first effect of a kind, added at the end when it has none. */
template<class T> T *Editor::camera_effect(CameraFilters &cf) {
  for (auto &e : cf.effects)
    if (T *t = dynamic_cast<T *>(e.get())) return t;
  cf.effects.push_back(std::make_unique<T>());
  return static_cast<T *>(cf.effects.back().get());
}

/* One step into the look; false when no camera filter does it. The Color Grading applies white balance, exposure,
 * contrast, saturation, then lift / gamma / gain: repeated contrasts and saturations multiply, an exposure after a
 * contrast is moved before it (the same picture: contrast is a power curve through middle grey). */
bool Editor::camera_look_step(const edit::Adjustment &a, CameraLook &look) {
  ColorGradingEffect &cg = look.cg;
  if (a.type == "Exposure") cg.exposure += a.get("Exposure") / (1.0f + cg.contrast);
  else if (a.type == "Contrast") cg.contrast = clampf((1.0f + cg.contrast) * (1.0f + a.get("Contrast") / 100.0f) - 1.0f, -0.99f, 1.0f);
  else if (a.type == "Vibrance / Saturation" && a.get("Vibrance") == 0.0f)
    cg.saturation = clampf((1.0f + cg.saturation) * (1.0f + a.get("Saturation") / 100.0f) - 1.0f, -1.0f, 1.0f);
  else if (a.type == "Temperature / Tint") cg.temperature = a.get("Temperature") / 100.0f, cg.tint = a.get("Tint") / 100.0f;
  else if (a.type == "Tone Curve") curve_to_lgg(a, cg);
  else return camera_look_effect(a, look);
  look.graded = true;
  return true;
}

/* The steps that are effects of their own: a darkening Vignette (its corner's light matched: the camera blends
 * toward black in display values), Sharpen. */
bool Editor::camera_look_effect(const edit::Adjustment &a, CameraLook &look) {
  if (a.type == "Vignette" && a.get("Amount") < 0.0f && a.get("Midpoint") == 50.0f) {
    look.vg.intensity = clampf(1.0f - std::pow(1.0f + a.get("Amount") / 100.0f, 1.0f / 2.4f), 0.0f, 1.0f);
    look.vg.smoothness = std::max(0.05f, a.get("Feather") / 100.0f);
    look.vg.roundness = (a.get("Roundness") + 100.0f) / 200.0f;
    look.vignette = true;
    return true;
  }
  if (a.type == "Sharpen" && a.get("Amount") > 0.0f) {
    look.sh.amount = clampf(a.get("Amount") / 150.0f, 0.0f, 1.0f);
    look.sharpen = true;
    return true;
  }
  return false;
}

}  // namespace bl
