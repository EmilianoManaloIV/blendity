// SPDX-License-Identifier: GPL-2.0-or-later
// A surface in the editor (task 0244, ADR 0080): the `surface` commands - a surface asset for the active object, its
// layers, mask stacks, presets, groups, fields and rect evaluation - the object's mesh maps loaded into it, and the
// worker that evaluates a surface whose stack changed while the views keep the last result.
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/mesh_maps.h"
#include "../texture/surface.h"
#include "../texture/smart_material.h"

#include <algorithm>
#include <cstdlib>

namespace bl {

namespace {

bool is_int(const std::string &a) { return !a.empty() && a.size() <= 9 && std::all_of(a.begin(), a.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); }

}  // namespace

/* The surface asset the active object's material points at (its Base Map "X.ptex#albedo" with the Surface flag). */
ProceduralTexturePtr Editor::surface_of(GameObject *g, std::string *why) const {
  const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
  if (!mr || mr->materials.empty() || !mr->materials[0]) {
    if (why) *why = g ? g->name + " has no material (Textures window > New > Surface on the Selected Object makes one)" : "select a mesh object first";
    return nullptr;
  }
  std::string asset;
  int output = 0;
  for (const TextureRef *map : {&mr->materials[0]->base_map, &mr->materials[0]->metallic_map, &mr->materials[0]->roughness_map, &mr->materials[0]->emission_map})
    if (split_texture_ref(map->path, asset, output))
      if (ProceduralTexturePtr pt = texture_asset(asset))
        if (pt->surface) return pt;
  if (why) *why = g->name + "'s material has no surface (Textures window > New > Surface on the Selected Object makes one)";
  return nullptr;
}

/* The object's mesh maps (task 0243's record) read into the surface at the stack's size. */
bool Editor::surface_load_maps(GameObject *g, ProceduralTexture &pt, std::string *why, bool step) {
  const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
  if (!mr || mr->mesh_maps.empty()) {
    if (why) *why = (g ? g->name : std::string("the object")) + " has no mesh maps baked (Object > Bake Mesh Maps)";
    return false;
  }
  auto maps = std::make_shared<SurfaceMaps>();
  std::string err;
  if (!load_surface_maps(resolve_asset_path(mr->mesh_maps), std::max(pt.width, pt.height), *maps, &err)) {
    if (why) *why = err;
    return false;
  }
  if (mesh_maps_stale(g)) Log::warn("Surface: %s's mesh maps are stale (the mesh changed since they were baked): bake them again for exact generators", g->name.c_str());
  if (step) note_texture_edit(pt);
  pt.maps = maps;
  pt.maps_from = mr->mesh_maps;
  pt.touch();
  if (step) mark_changed("Load Mesh Maps");
  return true;
}

/* Whether a layer, or one in a group, writes the emission channel. */
static bool stack_writes_emission(const TextureLayerList &ls);
bool surface_stack_writes_emission(const ProceduralTexture &pt) { return stack_writes_emission(pt.layers); }
static bool stack_writes_emission(const TextureLayerList &ls) {
  for (const auto &l : ls)
    if (l->enabled && ((l->targets & TargetEmission) || (l->children() && stack_writes_emission(*l->children())))) return true;
  return false;
}

void Editor::surface_sync_emission(const ProceduralTexture &pt) {
  if (pt.asset_path.empty()) return;
  const bool writes = stack_writes_emission(pt.layers);
  const std::string prefix = pt.asset_path + "#";
  for (const auto &gp : scene_->objects()) {
    GameObject &g = *gp;
    MeshRenderer *mr = g.get<MeshRenderer>();
    if (!mr) continue;
    for (MaterialPtr &m : mr->materials) {
      if (!m || m->base_map.path.compare(0, prefix.size(), prefix) != 0) continue;
      const bool has = m->emission_map.path.compare(0, prefix.size(), prefix) == 0;
      if (writes == has) continue;
      m->emission_map = writes ? TextureRef{pt.asset_path + "#emission", false} : TextureRef{};
      if (writes) m->emission_strength = std::max(m->emission_strength, 1.0f);
      m->touch();
    }
  }
}

/* surface new [name]: a surface asset for the active object - the object's own UV space, its mesh maps when baked -
 * assigned to the material's maps (albedo, normal, roughness, metallic, emission). */
bool Editor::surface_new(const std::string &name_in) {
  GameObject *g = active_object();
  if (!g || !g->get<MeshRenderer>()) {
    Log::warn("surface new: select a mesh object first");
    return false;
  }
  ProceduralTexture src;
  src.surface = true;
  src.seamless = false;
  src.width = src.height = 1024;
  src.live_shading = false;  // (a surface is its images: the generators read maps the live path has no texels for)
  src.add("Fill");
  std::string base = name_in.empty() ? g->name + "Surface" : name_in;
  base.erase(std::remove_if(base.begin(), base.end(), [](char ch) { return ch == '/' || ch == '\\' || ch == '#' || ch == ' '; }), base.end());  // (one console token: settex takes the path as one word)
  std::string rel = "Assets/Textures/" + base + ".ptex";
  for (int n = 2; fs::exists(resolve_asset_path(rel)) || texture_asset(rel); n++) rel = "Assets/Textures/" + base + std::to_string(n) + ".ptex";  // (no space: one console token)
  src.name = fs::stem(rel);
  ProceduralTexturePtr pt = create_texture_asset(src, rel);
  if (!pt) {
    Log::error("surface new: could not write %s", rel.c_str());
    return false;
  }
  std::string why;
  if (!surface_load_maps(g, *pt, &why, false)) Log::info("Surface: %s (Mesh Map layers read flat grey until then)", why.c_str());  // (part of this one step)
  assign_texture_asset(g, rel);
  texture_selected_ = pt;
  texture_paths_listed_ = -100;
  project_listed_ = -100;
  mark_changed("New Surface " + pt->name);
  Log::info("Surface %s on %s: %d x %d, a Fill layer; add layers, effects and masks in the Textures window", pt->name.c_str(), g->name.c_str(),
            pt->width, pt->height);
  return true;
}

/* Task 0273: the Textures window's layer menu - a mask preset, the mask cleared, the layer grouped with the one below,
 * the layer saved as a smart material - on the texture it edits. */
bool Editor::texture_layer_op(const std::string &asset, const std::string &op, int layer, const std::string &arg) {
  ProceduralTexturePtr pt = texture_asset(asset);
  if (!pt || layer < 0 || layer >= (int)pt->layers.size()) return false;
  TextureLayer &l = *pt->layers[(size_t)layer];
  if (op == "smat") {  // (no name: the dialog asks for one, a free one offered)
    const std::string refusal = smart_material_refusal(l);
    if (!refusal.empty()) return refuse("Save as Smart Material: " + refusal, "", ""), false;
    if (!arg.empty()) return smat_write(l.clone(), arg);
    smat_asset_ = asset, smat_layer_ = layer;
    dialog_text_ = smat_unique_name(l.name.empty() ? std::string("Smart Material") : l.name);
    dialog_ = Dialog::SaveSmat;
    return true;
  }
  if (op != "preset" && op != "clearmask" && op != "group") return false;
  if (op == "group" && layer + 1 >= (int)pt->layers.size()) return false;
  note_texture_edit(*pt);
  if (op == "preset") apply_surface_mask_preset(l, arg), mark_changed("Mask Preset " + arg);
  else if (op == "clearmask") l.mask_layers.clear(), mark_changed("Clear Mask");
  else {  // (group: checked above)
    auto grp = create_texture_layer("Group");
    TextureLayerList *children = grp->children();
    for (int k = layer; k <= layer + 1; k++) children->push_back(std::move(pt->layers[(size_t)k]));
    pt->layers.erase(pt->layers.begin() + layer, pt->layers.begin() + layer + 2);
    pt->layers.insert(pt->layers.begin() + layer, std::move(grp));
    mark_changed("Group Layers");
  }
  pt->touch();
  return true;
}

/* surface new [name] | add <layer type> | mask add <layer> <effect type> | mask preset <layer> <name> | group <from> <to> |
 * set <Field words...> <value> | eval [x0 y0 x1 y1] | maps | list */
void Editor::surface_command(const std::vector<std::string> &t, const std::string &line) {
  (void)line;  // (the tokens are enough: a value with spaces is not supported - name layers without them)
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? t[i] : d; };
  const std::string sub = to_lower(arg(1, "list"));
  if (sub == "new") {
    std::string name;
    for (size_t k = 2; k < t.size(); k++) name += (k > 2 ? " " : "") + t[k];
    surface_new(name);
    return;
  }
  GameObject *g = active_object();
  std::string why;
  ProceduralTexturePtr pt = surface_of(g, &why);
  if (!pt) {
    Log::warn("surface: %s", why.c_str());
    return;
  }
  auto rest = [&](size_t from) {
    std::string s;
    for (size_t k = from; k < t.size(); k++) s += (k > from ? " " : "") + t[k];
    return s;
  };
  const int n = (int)pt->layers.size();
  if (sub == "list") {
    Log::info("Surface %s (%d x %d, %s maps): %d layer%s", pt->name.c_str(), pt->width, pt->height, pt->maps ? "with" : "no", n, n == 1 ? "" : "s");
    for (int k = 0; k < n; k++) {
      const TextureLayer &l = *pt->layers[(size_t)k];
      Log::info("  %d: %s%s%s - %zu mask effect%s", k, l.type_name(), l.name.empty() ? "" : (" '" + l.name + "'").c_str(), l.enabled ? "" : " (off)", l.mask_layers.size(),
                l.mask_layers.size() == 1 ? "" : "s");
    }
    return;
  }
  if (sub == "add") {
    const std::string type = rest(2);
    if (texture_stack_op(pt->asset_path, "add", 0, 0, type)) Log::info("Surface %s: added %s (%zu layers)", pt->name.c_str(), type.c_str(), pt->layers.size());
    return;
  }
  if (sub == "mask") {
    const std::string what = to_lower(arg(2, ""));
    const int layer = is_int(arg(3, "")) ? std::atoi(arg(3, "").c_str()) : -1;
    if (layer < 0 || layer >= n) {
      Log::warn("surface mask add <layer 0-%d> <effect type> | preset <layer> edgewear|dirt|dust|metaledgewear|positiongradient|thickness | clear <layer>", std::max(0, n - 1));
      return;
    }
    if (what == "add") {
      const std::string type = rest(4);
      if (texture_stack_op(pt->asset_path, "add", 0, 0, type, layer, "Mask")) Log::info("Surface %s: layer %d's mask has %zu effect(s)", pt->name.c_str(), layer, pt->layers[(size_t)layer]->mask_layers.size());
      return;
    }
    if (what == "preset") {
      const std::vector<std::string> &presets = surface_mask_presets();
      if (std::find(presets.begin(), presets.end(), to_lower(arg(4, ""))) == presets.end()) {  // (checked before anything changes)
        Log::warn("surface mask preset: edgewear | dirt | dust | metaledgewear | positiongradient | thickness");
        return;
      }
      note_texture_edit(*pt);
      apply_surface_mask_preset(*pt->layers[(size_t)layer], arg(4, ""));
      pt->touch();
      mark_changed("Mask Preset " + arg(4, ""));
      Log::info("Surface %s: layer %d's mask is the %s preset (%zu effects; edit them in the Textures window)", pt->name.c_str(), layer, arg(4, "").c_str(), pt->layers[(size_t)layer]->mask_layers.size());
      return;
    }
    if (what == "clear") {
      note_texture_edit(*pt);
      pt->layers[(size_t)layer]->mask_layers.clear();
      pt->touch();
      mark_changed("Clear Mask");
      return;
    }
    Log::warn("surface mask add | preset | clear");
    return;
  }
  if (sub == "group") {
    const int from = is_int(arg(2, "")) ? std::atoi(arg(2, "").c_str()) : -1, to = is_int(arg(3, "")) ? std::atoi(arg(3, "").c_str()) : -1;
    if (from < 0 || to < from || to >= n) {
      Log::warn("surface group <from> <to>: the layers from..to (0-%d) moved into a new Group layer", std::max(0, n - 1));
      return;
    }
    note_texture_edit(*pt);
    auto grp = create_texture_layer("Group");
    TextureLayerList *children = grp->children();
    for (int k = from; k <= to; k++) children->push_back(std::move(pt->layers[(size_t)k]));
    pt->layers.erase(pt->layers.begin() + from, pt->layers.begin() + to + 1);
    pt->layers.insert(pt->layers.begin() + from, std::move(grp));
    pt->touch();
    mark_changed("Group Layers");
    Log::info("Surface %s: layers %d-%d are a group at %d (%zu layers now)", pt->name.c_str(), from, to, from, pt->layers.size());
    return;
  }
  if (sub == "set") {
    /* surface set <Field words> <value>: settex on the surface ("L0 Opacity 0.5", "L1 M0 Low 0.4" for a mask effect's,
     * "L0 C1 Opacity 1" for a group's child's). */
    if (t.size() < 4) {
      Log::warn("surface set <Field> <value> (e.g. surface set L1 Opacity 0.5; a mask effect's field: L1 M0 Low 0.4; a group's child: L0 C1 Opacity 1)");
      return;
    }
    /* The field's words joined (settex takes one word and matches names without their spaces): every token up to
     * the last one is the field, the last the value - a Vec3 value is given with commas (1,0.5,0). */
    std::string field;
    for (size_t k = 2; k + 1 < t.size(); k++) field += t[k];
    std::string value = t.back();
    std::replace(value.begin(), value.end(), ',', ' ');
    if (pt->asset_path.find(' ') != std::string::npos) {
      Log::warn("surface set: the asset's path has a space (%s); rename it, or use the Textures window", pt->asset_path.c_str());
      return;
    }
    command("settex " + pt->asset_path + " " + field + " " + value);
    return;
  }
  if (sub == "maps") {
    if (surface_load_maps(g, *pt, &why)) Log::info("Surface %s: %s's mesh maps loaded (%d x %d)", pt->name.c_str(), g->name.c_str(), pt->maps->w, pt->maps->h);
    else Log::warn("surface maps: %s", why.c_str());
    return;
  }
  if (sub == "eval") {
    /* surface eval [x0 y0 x1 y1]: the stack evaluated whole, or the layers from the selected one up inside the rect
     * on the kept composite - the rect engine, timed. */
    TexPlanes planes;
    const auto t0 = std::chrono::steady_clock::now();
    if (t.size() >= 6 && is_int(arg(2, "")) && is_int(arg(3, "")) && is_int(arg(4, "")) && is_int(arg(5, ""))) {
      const size_t from = (size_t)std::max(0, std::min(n - 1, surface_active_layer_));
      std::map<std::string, std::vector<float>> anchors;
      pt->evaluate_below(planes, from, &anchors);
      const auto t1 = std::chrono::steady_clock::now();
      LayerContext rc = pt->context(planes.w, planes.h);
      if (rect_halo(*pt, from, rc) < 0) Log::info("Surface %s: a Warp or Tile above layer %zu reads anywhere - the whole surface is evaluated", pt->name.c_str(), from);
      evaluate_rect(*pt, from, std::atoi(arg(2, "").c_str()), std::atoi(arg(3, "").c_str()), std::atoi(arg(4, "").c_str()), std::atoi(arg(5, "").c_str()), planes, anchors);
      const auto t2 = std::chrono::steady_clock::now();
      Log::info("Surface %s: the composite below layer %zu in %.1f ms, the rect from it up in %.2f ms (%d x %d)", pt->name.c_str(), from, std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count(), planes.w, planes.h);
      return;
    }
    pt->evaluate(planes);
    Log::info("Surface %s: evaluated whole in %.1f ms (%d x %d, %zu layers)", pt->name.c_str(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), planes.w, planes.h,
              pt->layers.size());
    return;
  }
  if (sub == "layer") {  // surface layer <i>: the active layer the rect evaluation starts from
    surface_active_layer_ = is_int(arg(2, "")) ? std::atoi(arg(2, "").c_str()) : 0;
    return;
  }
  Log::warn("surface new [name] | add <type> | mask add <layer> <type> | mask preset <layer> <name> | mask clear <layer> | group <from> <to> | set <Field> <value> | layer <i> | eval [x0 y0 x1 y1] | maps | list");
}

/* ===================================================================== */
/* The worker: a changed surface evaluated off the main thread            */
/* ===================================================================== */

/* Each frame: a finished job's images go into the output cache and the materials re-resolve; then any loaded surface
 * whose hash moved on since its cached images gets a job (one at a time, the asset copied, the images with their mips
 * built on the worker), the cache answering with the last images for it meanwhile - so the views show the last result
 * until the next lands, whichever texture is selected. A surface whose maps were lost with the text (an undo, a
 * reopened file) reloads them from its record first. */
void Editor::surface_poll() {
  if (surface_job_ && surface_job_->done.load(std::memory_order_acquire)) {
    std::unique_ptr<SurfaceJob> job = std::move(surface_job_);
    if (job->thread.joinable()) job->thread.join();
    if (!job->cancel.load(std::memory_order_relaxed)) {
      procedural_texture_cache_store(job->path, job->built);
      surface_sync_emission(job->copy);  // (a layer started or stopped writing emission since the material was assigned)
      invalidate_material_textures();
      ui_.redraw = true;
    }
  }
  if (surface_job_ || texture_dragging_) return;
  for (const auto &entry : loaded_texture_assets()) {
    ProceduralTexturePtr t = entry.second;
    if (!t || !t->surface) continue;
    procedural_texture_set_async(t->asset_path, true);  // (a surface is always async: a material asking for a hash the cache lacks gets the last images, never a main-thread evaluation after the first)
    if (!t->maps && !t->maps_from.empty() && !surface_maps_failed_.count(t->asset_path)) {
      auto maps = std::make_shared<SurfaceMaps>();
      std::string err;
      if (load_surface_maps(resolve_asset_path(t->maps_from), std::max(t->width, t->height), *maps, &err)) t->maps = maps;
      else {
        surface_maps_failed_.insert(t->asset_path);
        Log::warn("Surface %s: %s (Mesh Map layers read flat grey; Textures > Load Mesh Maps after a new bake)", t->name.c_str(), err.c_str());
      }
    }
    const uint64_t h = procedural_texture_cache_hash(*t);
    if (procedural_texture_cached(t->asset_path, h)) continue;
    procedural_texture_set_async(t->asset_path, true);
    auto job = std::make_unique<SurfaceJob>();
    job->path = t->asset_path;
    job->hash = h;
    job->copy = *t;
    SurfaceJob *raw = job.get();
    job->thread = std::thread([raw] {
      JobSystem::global().run_background([raw] {
        TexPlanes planes;
        std::vector<LayerAux> aux;
        raw->copy.evaluate(planes, raw->copy.live_shading ? &aux : nullptr);
        if (!raw->cancel.load(std::memory_order_relaxed)) raw->built = procedural_texture_cache_build(raw->copy, raw->hash, planes, std::move(aux));
      });
      raw->done.store(true, std::memory_order_release);
    });
    surface_job_ = std::move(job);
    ui_.redraw = true;
    return;
  }
}

}  // namespace bl
