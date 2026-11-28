// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the Inspector's Add Component search and its visible scalar widgets.
// See docs/perf/0640-input-sweep.md for the release object bank and isolated runtime.
#include "src/editor/editor.h"
#include "src/texture/procedural_texture.h"
#include "src/scene/pointcloud_object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>

using namespace bl;
namespace {
using EventType = platform::EventType;

void pointer(Editor &ed, EventType type, int x, int y) {
  platform::Event e;
  e.type = type;
  e.x = x;
  e.y = y;
  ed.step_frame_headless({e});
}

void click(Editor &ed, int x, int y) {
  pointer(ed, EventType::MouseMove, x, y);
  pointer(ed, EventType::MouseDown, x, y);
  pointer(ed, EventType::MouseUp, x, y);
}

void key(Editor &ed, int code, int mods = 0) {
  platform::Event e;
  e.type = EventType::KeyDown;
  e.key = code;
  e.mods = mods;
  ed.step_frame_headless({e});
  e.type = EventType::KeyUp;
  ed.step_frame_headless({e});
}

void type_text(Editor &ed, const std::string &value) {
  for (char c : value) {
    platform::Event e;
    e.type = EventType::Text;
    e.codepoint = static_cast<unsigned char>(c);
    ed.step_frame_headless({e});
  }
}

bool visible(Editor &ed, const std::string &label, Recti &r, bool scroll, WindowKind kind = WindowKind::Inspector) {
  for (int step = 0; step < 32; ++step) {
    ed.step_frame_headless();
    if (!ed.button_rect_for_test(label, r)) return false;
    const Recti pane = ed.window_rect_for_test(kind);
    if (r.w <= 0 || r.h <= 0 || pane.w <= 0) return false;
    if (r.y >= pane.y + 25 && r.bottom() <= pane.bottom() - 3) return true;
    if (!scroll) return false;
    platform::Event e;
    e.type = EventType::Wheel;
    e.x = pane.x + pane.w / 2;
    e.y = pane.y + pane.h / 2;
    e.wheel_y = r.y < pane.y + 25 ? 2.0f : -2.0f;
    pointer(ed, EventType::MouseMove, e.x, e.y);
    ed.step_frame_headless({e});
  }
  return false;
}

struct Scalar {
  char kind;
  double value, low, high;
};

struct ReadFields : Reflector {
  std::map<std::string, Scalar> values;
  void field(const char *name, float &v, float, float lo, float hi) override { values[name] = {'f', v, lo, hi}; }
  void field(const char *name, int &v, int lo, int hi) override { values[name] = {'i', double(v), double(lo), double(hi)}; }
  void field(const char *name, bool &v) override { values[name] = {'b', double(v), 0, 1}; }
  void field(const char *, Vec3 &) override {}
  void color(const char *, Vec3 &) override {}
  void enumeration(const char *, int &, const char *const *, int) override {}
  void text(const char *, std::string &) override {}
  void mesh(const char *, MeshPtr &) override {}
};

template<class Owner>
ReadFields snapshot(Owner &component) {
  ReadFields reader;
  component.reflect(reader);
  return reader;
}

void edit(Editor &ed, const Recti &r, const std::string &literal, int finish) {
  const int x = r.x + r.w / 2, y = r.y + r.h / 2;
  click(ed, x, y);
  click(ed, x, y);
  key(ed, platform::KEY_A, platform::MOD_CTRL);
  type_text(ed, literal);
  key(ed, finish);
  ed.step_frame_headless();
}

template<class Owner>
void check_fields(Editor &ed, Owner &component, const std::string &prefix = "", WindowKind pane = WindowKind::Inspector) {
  const auto initial = snapshot(component).values;
  for (const auto &entry : initial) {
    const auto &name = entry.first;
    Recti box;
    auto locate = [&]() {
      if (std::string(component.type_name()) != "Window Grid" || name != "Frame")
        return visible(ed, "field:" + prefix + name, box, true, pane);
      // The later Frame color and earlier Frame number share their recorded name.
      Recti top, bottom;
      if (!visible(ed, "field:" + prefix + "Window", top, true, pane) ||
          !ed.button_rect_for_test("field:" + prefix + "Lit Share", bottom)) return false;
      for (const auto &c : ed.ui_for_test().controls) {
        if (std::string(c.kind) == "number" && c.r.x == top.x && c.r.y > top.y && c.r.y < bottom.y &&
            c.r.intersect(c.clip).h == c.r.h) {
          box = c.r;
          return true;
        }
      }
      return false;
    };
    if (!locate()) continue;
    auto read = [&]() {
      const auto values = snapshot(component).values;
      auto it = values.find(name);
      return it == values.end() ? NAN : it->second.value;
    };
    const Scalar scalar = entry.second;
    const double before = read();
    if (!std::isfinite(before)) continue;
    const double step = scalar.kind == 'f' ? 0.25 : 1.0;
    double target = before + step <= scalar.high ? before + step : before - step;
    if (scalar.kind == 'b') target = 1.0 - before;
    if (target < scalar.low || target > scalar.high || target == before) continue;
    std::ostringstream literal;
    literal.precision(9);
    literal << target;
    if (scalar.kind == 'b') click(ed, 2, 4);
    else edit(ed, box, literal.str(), platform::KEY_ESCAPE);
    const double canceled = read();
    if (canceled != before || !locate()) {
      std::printf("CONTROL\t%s\t%s\t%.9g\t%.9g\n", component.type_name(), name.c_str(), before, canceled);
      continue;
    }
    if (scalar.kind == 'b') click(ed, box.x + 5, box.y + box.h / 2);
    else edit(ed, box, literal.str(), platform::KEY_ENTER);
    const double after = read();
    const bool pass = std::isfinite(after) && std::abs(after - target) <= 1e-5 && std::abs(after - before) > 1e-5;
    std::printf("%s\t%s\t%s\t%c\t%.9g\t%.9g\t%.9g\t%.9g\t%d,%d,%d,%d\n",
                pass ? "FIELD" : "MISS", component.type_name(), name.c_str(), scalar.kind,
                before, canceled, target, after, box.x, box.y, box.w, box.h);
    if (pass) {
      if (scalar.kind == 'b') click(ed, box.x + 5, box.y + box.h / 2);
      else {
        std::ostringstream restore;
        restore.precision(9);
        restore << before;
        edit(ed, box, restore.str(), platform::KEY_ENTER);
      }
    }
  }
}

bool open_choice(Editor &ed, const std::string &name, Recti &choice, const std::string &opener = "Add Component", WindowKind pane = WindowKind::Inspector, bool effect_row = false) {
  Recti button;
  if (!visible(ed, opener, button, true, pane)) return false;
  click(ed, button.x + button.w / 2, button.y + button.h / 2);
  ed.step_frame_headless();
  if (opener != "Add Filter") type_text(ed, name);
  ed.step_frame_headless();
  if (!visible(ed, "menu:" + name, choice, false, pane)) return false;
  if (opener == "Add Filter" && (name == "Anime" || name == "Manga")) {
    // Each name appears twice: first the effect under Stylize, later the preset under Looks.
    std::vector<Recti> matches;
    for (const auto &c : ed.ui_for_test().controls)
      if (std::string(c.kind) == "menu" && c.label == name) matches.push_back(c.r);
    if (matches.size() != 2 || matches[0].y >= matches[1].y || choice.y != matches[1].y) return false;
    choice = effect_row ? matches.front() : matches.back();
  }
  return true;
}

void exercise(const std::string &name) {
  Editor ed;
  ed.init_headless(950, 1000);
  ed.record_buttons_for_test(true);
  ed.command("layout L(3:0)");
  ed.command("inspector essentials off");
  ed.command("create Empty");
  ed.step_frame_headless();
  auto *object = ed.selected_object();
  if (!object) return;
  const auto object_id = object->id;
  auto find = [&]() -> Component * {
    object = ed.scene().find(object_id);
    if (!object) return nullptr;
    for (auto &c : object->components) if (c->type_name() == name) return c.get();
    return nullptr;
  };
  Recti choice;
  if (find() || !open_choice(ed, name, choice)) {
    std::printf("MISSING\t%s\tinitial component or menu row\n", name.c_str());
    return;
  }
  click(ed, 2, 4);
  ed.step_frame_headless();
  if (find() || !open_choice(ed, name, choice)) {
    std::printf("MISSING\t%s\tcancel or reopen\n", name.c_str());
    return;
  }
  click(ed, choice.x + choice.w / 2, choice.y + choice.h / 2);
  ed.step_frame_headless();
  auto *component = find();
  std::printf("%s\t%s\t%d,%d,%d,%d\n", component ? "COMPONENT" : "MISSING", name.c_str(), choice.x, choice.y, choice.w, choice.h);
  if (component) check_fields(ed, *component);
}
template<class List>
bool add_to_stack(Editor &ed, const std::string &name, const std::string &opener, WindowKind pane, List &items, bool effect_row = false) {
  const size_t before = items.size();
  Recti choice;
  if (!open_choice(ed, name, choice, opener, pane, effect_row)) return false;
  click(ed, 2, 4);
  ed.step_frame_headless();
  if (items.size() != before || !open_choice(ed, name, choice, opener, pane, effect_row)) return false;
  click(ed, choice.x + choice.w / 2, choice.y + choice.h / 2);
  ed.step_frame_headless();
  if (items.size() != before + 1 || std::string(items.back()->type_name()) != name) return false;
  const bool duplicate = opener == "Add Filter" && (name == "Anime" || name == "Manga");
  const char *section = duplicate ? (effect_row ? "Stylize" : "Looks") : "registered type";
  std::printf("STACK\t%s\t%s\t%zu\t%zu\t%d,%d,%d,%d\t%s\n", opener.c_str(), name.c_str(),
              before, items.size(), choice.x, choice.y, choice.w, choice.h, section);
  return true;
}

void exercise_stack(const std::string &name, bool layer, int number, bool effect_row = false) {
  Editor ed;
  ed.init_headless(1100, 1100);
  ed.record_buttons_for_test(true);
  ed.command("jobs async off");
  ed.command("inspector essentials off");
  const auto kind = layer ? WindowKind::Textures : WindowKind::Inspector;
  ed.command("layout L(" + std::to_string(static_cast<int>(kind)) + ":0)");
  if (layer) {
    ed.command("select Cube");
    ed.command("surface new Gesture640_" + std::to_string(number));
    auto texture = ed.surface_of_for_test(ed.selected_object(), nullptr);
    if (!texture) return;
    texture->width = texture->height = 64;
    for (auto &existing : texture->layers) existing->ui_expanded = false;
    ed.select_texture_for_test(texture);
    ed.step_frame_headless();
    if (add_to_stack(ed, name, "Add Layer", kind, texture->layers)) {
      auto &added = *texture->layers.back();
      added.ui_expanded = true;
      check_fields(ed, added, "L" + std::to_string(texture->layers.size() - 1) + " ", kind);
    } else std::printf("MISSING\tAdd Layer\t%s\n", name.c_str());
  } else {
    ed.command("create Empty");
    auto *selected = ed.selected_object();
    if (!selected) return;
    auto *stack = selected->add<CameraFilters>();
    ed.step_frame_headless();
    if (add_to_stack(ed, name, "Add Filter", kind, stack->effects, effect_row)) {
      stack->effects.back()->ui_expanded = true;
      check_fields(ed, *stack->effects.back(), "E0 ", kind);
    } else std::printf("MISSING\tAdd Filter\t%s\n", name.c_str());
  }
}
bool context_fields() {
  Editor ed;
  ed.init_headless(1100, 1100);
  ed.record_buttons_for_test(true);
  ed.command("layout L(3:0)");
  ed.command("inspector essentials off");
  ed.command("select Cube");
  auto *cube = ed.selected_object();
  if (!cube || !cube->evaluated_mesh()) return false;
  auto *collider = cube->add<ColliderComponent>();
  collider->fit = false; // A newly added collider normally fits itself on the first frame.
  collider->size = Vec3(7);
  collider->center = Vec3(9);
  const auto bounds = cube->evaluated_mesh()->bounds();
  ed.step_frame_headless();
  Recti field;
  if (!visible(ed, "field:Fit to Mesh", field, true)) return false;
  click(ed, 2, 4);
  if (length(collider->size - Vec3(7)) != 0 || length(collider->center - Vec3(9)) != 0) return false;
  click(ed, field.x + 5, field.y + field.h / 2);
  ed.step_frame_headless();
  if (collider->fit || length(collider->size - (bounds.max - bounds.min)) > 1e-5f ||
      length(collider->center - (bounds.max + bounds.min) * 0.5f) > 1e-5f) return false;
  std::puts("CONTEXT\tfield/ColliderComponent/Fit to Mesh\tinitial size 7 and center 9; off-target unchanged; fitted size and center match mesh bounds; checkbox self-cleared");
  ed.command("create Test Splats");
  auto *cloud = ed.selected_object() ? ed.selected_object()->get<PointCloudObject>() : nullptr;
  if (!cloud || !cloud->data || !cloud->data->is_splats()) return false;
  ed.step_frame_headless();
  if (!visible(ed, "field:SH Degree", field, true)) return false;
  const int degree = cloud->data->sh_degree, target = degree == 0 ? 1 : 0;
  edit(ed, field, std::to_string(target), platform::KEY_ESCAPE);
  if (cloud->data->sh_degree != degree) return false;
  edit(ed, field, std::to_string(target), platform::KEY_ENTER);
  if (cloud->data->sh_degree != target) return false;
  std::printf("CONTEXT\tfield/PointCloudObject/SH Degree\tinitial %d; canceled %d; committed %d on Test Splats\n", degree, degree, target);
  return true;
}

struct MaterialFields {
  Material &material;
  const char *type_name() const { return "Material"; }
  void reflect(Reflector &reader) { material.reflect(reader); }
};

void exercise_material(int surface, int shading, bool liltoon) {
  Editor ed;
  ed.init_headless(1100, 1100);
  ed.record_buttons_for_test(true);
  ed.command("layout L(" + std::to_string(static_cast<int>(WindowKind::Materials)) + ":0)");
  ed.step_frame_headless();
  Recti create;
  if (!ed.button_rect_for_test("New Material Asset", create)) {
    std::puts("MISSING\tMaterial\tNew Material Asset button");
    return;
  }
  click(ed, create.x + create.w / 2, create.y + create.h / 2);
  ed.step_frame_headless();
  auto material = ed.material_selected_for_test();
  if (!material) {
    std::puts("MISSING\tMaterial\tasset selection");
    return;
  }
  material->surface = surface;
  material->shading_model = shading;
  material->lil.on = liltoon;
  material->touch();
  ed.step_frame_headless();
  MaterialFields owner{*material};
  std::printf("MATERIAL\t%d\t%d\t%d\n", surface, shading, int(liltoon));
  check_fields(ed, owner, "", WindowKind::Materials);
  std::fflush(stdout);
}
} // namespace

int main(int argc, char **argv) {
  register_builtin_components();
  if (argc == 2 && std::string(argv[1]) == "--contexts") {
    exercise_stack("Anime", false, 90, true);
    exercise_stack("Manga", false, 91, true);
    exercise_stack("Window Grid", true, 92);
    return context_fields() ? 0 : 1;
  }
  if (argc == 2 && std::string(argv[1]) == "--materials") {
    for (int surface = 0; surface < 5; ++surface) exercise_material(surface, 0, false);
    exercise_material(0, 1, false);
    exercise_material(0, 1, true);
    exercise_material(0, 2, false);
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--stacks") {
    int number = 0;
    for (const auto &info : filter_effect_infos()) {
      exercise_stack(info.name, false, ++number);
      std::fflush(stdout);
    }
    for (const auto &info : texture_layer_infos()) {
      exercise_stack(info.name, true, ++number);
      std::fflush(stdout);
    }
    return 0;
  }
  std::vector<std::string> names;
  for (const auto &info : component_registry()) names.push_back(info.name);
  for (const auto &name : names) {
    if (argc > 1 && std::find(argv + 1, argv + argc, name) == argv + argc) continue;
    exercise(name);
    std::fflush(stdout);
  }
}
