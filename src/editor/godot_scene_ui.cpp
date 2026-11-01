// SPDX-License-Identifier: GPL-2.0-or-later
// Godot scenes in and back (round 373, task 0350): import <file>.tscn (and a drop) brings its nodes in under one root
// as one Undo step - res:// paths found from the project.godot above the file, instanced scenes and models, textures
// where they are; export tscn writes the objects' transforms and visibility back into the scene's own lines, every
// other byte as Godot wrote it (scripts included), to Exports/<file>.tscn or where asked.
#include "editor.h"

#include "../scene/godot_scene.h"
#include "../scene/material.h"
#include "../scene/unity_scene.h"
#include "../core/core.h"

#include <set>

namespace bl {

namespace {

/* The Godot project's folder: the nearest one above the file holding project.godot (else the file's own). */
std::string godot_project_root(const std::string &file) {
  const std::string start = fs::parent(fs::normalize(file));
  std::string dir = start;
  for (int i = 0; i < 32 && !dir.empty(); i++) {
    if (fs::exists(fs::join(dir, "project.godot"))) return dir;
    const std::string up = fs::parent(dir);
    if (up == dir) break;
    dir = up;
  }
  return start;
}

}  // namespace

/* import <file>.tscn: its nodes under one root named for the file, at the view's pivot, selected; one Undo step. */
bool Editor::import_godot_scene(const std::string &path) {
  std::string text, why;
  std::vector<GodotSection> sections;
  if (!fs::read_file(path, text)) return Log::warn("Import Godot scene: can't read %s", path.c_str()), false;
  if (!parse_godot_text(text, sections, why)) return Log::warn("Import Godot scene: %s - %s", fs::filename(path).c_str(), why.c_str()), false;
  if (sections[0].tag != "gd_scene") return Log::warn("Import Godot scene: %s is a resource, not a scene", fs::filename(path).c_str()), false;
  if (!std::any_of(sections.begin(), sections.end(), [](const GodotSection &s) { return s.tag == "node"; }))
    return Log::warn("Import Godot scene: %s has no nodes", fs::filename(path).c_str()), false;
  std::map<std::string, ImportResult> models;  // (a model instanced many times: read once)
  GodotImport ctx;
  ctx.res_root = godot_project_root(path);
  ctx.source = forward_slashes(make_asset_relative(fs::normalize(path)));
  ctx.building = std::make_shared<std::set<std::string>>(std::set<std::string>{godot_path_key(path)});  // (review 373: the file itself counts)
  ctx.model = [&](const std::string &abs, Scene &scene) -> GameObject * {
    if (!model_extension_supported(fs::extension(abs))) return nullptr;
    auto m = models.find(abs);
    if (m == models.end() && (m = models.emplace(abs, ImportResult{}).first, !import_model(abs, m->second))) Log::warn("Import Godot scene: %s - %s", fs::filename(abs).c_str(), m->second.error.c_str());
    return m->second.nodes.empty() ? nullptr : objects_from_model(m->second, fs::stem(abs), scene);
  };
  ctx.texture = [&](const std::string &abs) { return fs::exists(abs) ? forward_slashes(make_asset_relative(fs::normalize(abs))) : std::string(); };
  commit_undo();
  GameObject *root = scene_->create(fs::stem(path));
  root->set_world_position(cam_.pivot);
  build_godot_scene(sections, *scene_, root, ctx);
  expanded_.insert(root->id);
  select(root->id);
  mark_changed("Import " + fs::filename(path));
  for (const auto &[n, count] : count_notes(ctx.notes)) Log::warn("Import Godot scene: %s%s", n.c_str(), count > 1 ? strprintf(" (x%d)", count).c_str() : "");
  Log::info("Import Godot scene: %s - %d object(s), %d mesh(es), %d material(s), %d light(s), %d camera(s), %d collider(s), %d instanced scene(s), "
            "%d override(s) of their nodes, %d node(s) and script(s) kept as they are", fs::filename(path).c_str(), ctx.objects, ctx.meshes, ctx.materials,
            ctx.lights, ctx.cameras, ctx.colliders, ctx.scenes, ctx.overrides, ctx.engine);
  return true;
}

/* The edits of the objects imported from `file`: the first object per node path (a duplicate carries its original's
 * GodotOrigin - counted, left out); an object whose transform is as it came in keeps its line (review 373). */
std::vector<GodotEdit> Editor::godot_edits(const std::string &file, bool format4, int &copies) {
  std::vector<GodotEdit> out;
  std::set<std::string> seen;
  copies = 0;
  scene_->for_each([&](GameObject &g) {
    const GodotOrigin *o = g.get<GodotOrigin>();
    if (!o || o->file != file) return;
    if (!seen.insert(o->path).second) return (void)copies++;
    GodotEdit e{o->path, g.local(), g.active, true, o->turned, o->parent_turned};
    e.moved = !godot_same_transform(o->transform, godot_transform_text(godot_turn(g.local(), o->turned, o->parent_turned), format4));
    out.push_back(e);
  });
  return out;
}

/* The Godot origin of the selection: its own, a parent's, or one below it (an import's root). */
static const GodotOrigin *selected_godot_origin(GameObject *g) {
  for (GameObject *p = g; p; p = p->parent)
    if (const GodotOrigin *o = p->get<GodotOrigin>()) return o;
  std::function<const GodotOrigin *(GameObject *)> below = [&](GameObject *x) -> const GodotOrigin * {
    for (GameObject *c : x->children) {
      const GodotOrigin *o = c->get<GodotOrigin>() ? c->get<GodotOrigin>() : below(c);
      if (o) return o;
    }
    return nullptr;
  };
  return g ? below(g) : nullptr;
}

/* export tscn [out]: the selected object's Godot scene with its edits, to Exports/<file>.tscn or `out`. */
bool Editor::export_godot_scene(const std::string &out_in) {
  const GodotOrigin *o = selected_godot_origin(active_object());
  if (!o) return refuse("Export Godot Scene: select an object imported from a Godot scene (.tscn) first", "", ""), false;
  const std::string file = o->file, src = resolve_asset_path(file);
  std::string text, why;
  std::vector<GodotSection> sections;
  if (!fs::read_file(src, text) || !parse_godot_text(text, sections, why)) return Log::warn("Export Godot Scene: can't read %s (moved?)", src.c_str()), false;
  const std::string out = fs::normalize(!out_in.empty() ? resolve_asset_path(out_in) : fs::join(fs::join(project_root_, "Exports"), fs::filename(src)));
  if (fs::exists(out) && fs::extension(out) != ".tscn") return Log::warn("Export Godot Scene: %s is another kind of file - not written over", out.c_str()), false;
  int copies = 0, changed = 0;
  const std::string patched = patch_tscn(text, godot_edits(file, sections[0].attr("format") != "2", copies), &changed);
  fs::make_dirs(fs::parent(out));
  if (!fs::write_file(out, patched)) return Log::warn("Export Godot Scene: can't write %s", out.c_str()), false;
  if (copies) Log::info("Export Godot Scene: %d duplicate(s) of imported objects left out (they're Blendity's)", copies);
  Log::info("Export Godot Scene: %s - %d line(s) changed, every other line as Godot wrote it", out.c_str(), changed);
  return true;
}

}  // namespace bl
