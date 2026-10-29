// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the editor tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "../src/scene/raycast.h"
#include "../src/scene/stroke_object.h"
#include "test_common.h"
#include "../src/render/canvas.h"
#include "../src/texture/smart_material.h"
#include "../src/texture/paint_layer.h"
#include "../src/texture/surface.h"
#include "../src/render/mesh_maps.h"
#include <cstring>
#include "../src/render/texture_bake.h"

#include <chrono>
#include <thread>











/* ===================================================================== */
/* Shift-drag extrude, seams from sharp, light aiming, material assets,    */
/* drawing, Follow / Spin / Slice, Reset XForm, the pivot fix              */
/* ===================================================================== */




/* ===================================================================== */
/* Round 15: keymaps, draw axes, Z-fighting check, UV tools, light hash  */
/* ===================================================================== */

static void round15_tests() {
  auto key = [](int k, int mods = 0) {
    platform::Event e;
    e.type = platform::EventType::KeyDown;
    e.key = k;
    e.mods = mods;
    return e;
  };
  auto key_up = [](int k) {
    platform::Event e;
    e.type = platform::EventType::KeyUp;
    e.key = k;
    return e;
  };
  auto move = [](int x, int y) {
    platform::Event e;
    e.type = platform::EventType::MouseMove;
    e.x = x;
    e.y = y;
    return e;
  };
  test("light colour temperature (and every other light setting) re-renders the Camera Preview", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    GameObject *sun = by_name(ed.scene(), "Directional Light");
    CHECK(sun && sun->get<Light>());
    if (!sun) return;
    const uint64_t h0 = ed.render_hash_for_test();
    sun->get<Light>()->use_temperature = true;
    const uint64_t h1 = ed.render_hash_for_test();
    sun->get<Light>()->temperature = 3200.0f;
    const uint64_t h2 = ed.render_hash_for_test();
    sun->get<Light>()->spot_angle = 50.0f;
    const uint64_t h3 = ed.render_hash_for_test();
    CHECK(h0 != h1 && h1 != h2 && h2 != h3);
  });
  test("keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Max and SketchUp", [&] {
    auto bound = [](const Keymap &k, const char *id, const char *chord) {
      auto it = k.find(id);
      const KeyChord c = parse_chord(chord);
      return it != k.end() && std::find(it->second.begin(), it->second.end(), c) != it->second.end();
    };
    const Keymap unity = keymap_preset("Unity"), blender = keymap_preset("Blender"), maya = keymap_preset("Maya"), max = keymap_preset("3ds Max"),
                 su = keymap_preset("SketchUp");
    CHECK(bound(unity, "tool.move", "W") && bound(unity, "tool.scale", "R") && bound(unity, "edit.undo", "Ctrl+Z") && bound(unity, "view.frame", "F"));
    CHECK(bound(blender, "transform.grab", "G") && bound(blender, "transform.rotate", "R") && bound(blender, "transform.scale", "S") &&
          bound(blender, "mesh.extrude", "E") && bound(blender, "edit.delete", "X") && bound(blender, "mesh.merge_center", "M"));
    CHECK(bound(maya, "mode.edit_toggle", "F8") && bound(maya, "mode.vertex", "F9") && bound(maya, "mode.face", "F11") &&
          bound(maya, "mesh.proportional", "B") && bound(maya, "edit.redo", "Shift+Z"));
    CHECK(bound(max, "view.frame", "Z") && bound(max, "mode.face", "4") && bound(max, "mesh.bevel", "Ctrl+Shift+C"));
    CHECK(bound(su, "mesh.push_pull", "P") && bound(su, "draw.rectangle", "R") && bound(su, "draw.circle", "C") && bound(su, "tool.move", "M"));
    /* The old "Blender Transform Keys" preference on top of Unity. */
    const Keymap ub = keymap_preset("Unity", true);
    CHECK(bound(ub, "transform.rotate", "R") && bound(ub, "tool.scale", "T") && !bound(ub, "tool.scale", "R"));
    /* No preset has a real clash. */
    for (const char *pn : kKeymapPresets) {
      const Keymap k = keymap_preset(pn);
      size_t clashes = 0;
      std::string first;
      for (auto &kv : k)
        for (const KeyChord &c : kv.second) {
          const auto cl = keymap_conflicts(k, kv.first, c);
          if (!cl.empty() && first.empty()) first = kv.first + " / " + cl[0] + " on " + chord_text(c);
          clashes += cl.size();
        }
      std::printf("    %s: %zu clash(es) %s\n", pn, clashes, first.c_str());
      CHECK(clashes == 0);
    }
    /* Chords print and parse both ways. */
    for (const char *t : {"Ctrl+Shift+Z", "Alt+F", "F12", "Delete", "Ctrl+=", "Shift+.", "Tab", "Space"}) CHECK(chord_text(parse_chord(t)) == t);
  });
  test("keymap: the keys pressed run what the preset says; rebinding, clashes and saving work", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    const Recti r = ed.scene_view_rect();
    ed.command("select Cube");
    ed.step_frame_headless({move(r.x + r.w / 2, r.y + r.h / 2)});
    auto press = [&](int k, int mods = 0) {
      ed.step_frame_headless({key(k, mods)});
      ed.step_frame_headless({key_up(k)});
    };
    /* Unity: Tab edits, 3 is face mode, Tab leaves. */
    press(platform::KEY_TAB);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_TAB);
    CHECK(!ed.in_edit_mode());
    /* Maya: F8 edits, W is the Move tool. */
    ed.command("keymap Maya");
    CHECK(ed.keymap_preset_name() == "Maya");
    press(platform::KEY_F8);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_F8);
    CHECK(!ed.in_edit_mode());
    /* Rebind: Edit Mode on Ctrl+Shift+E instead. */
    ed.command("keymap bind mode.edit_toggle Ctrl+Shift+E");
    press(platform::KEY_F8);
    CHECK(!ed.in_edit_mode());
    press(platform::KEY_E, platform::MOD_CTRL | platform::MOD_SHIFT);
    CHECK(ed.in_edit_mode());
    press(platform::KEY_E, platform::MOD_CTRL | platform::MOD_SHIFT);
    /* A clash is reported: bind Undo's key to Redo too. */
    const auto clash = keymap_conflicts(ed.keymap(), "edit.redo", parse_chord("Ctrl+Z"));
    CHECK(!clash.empty() && clash[0] == "edit.undo");
    /* The user's changes survive a save and load. */
    const std::string saved = ed.keymap_overrides_text();
    CHECK(saved.find("mode.edit_toggle=Ctrl+Shift+E") != std::string::npos);
    Editor ed2;
    ed2.init_headless(800, 600);
    ed2.command("keymap Maya");
    ed2.parse_keymap_overrides(saved);
    CHECK(ed2.shortcut_text("mode.edit_toggle") == "Ctrl+Shift+E");
    CHECK(ed2.shortcut_text("tool.move") == "W");
    /* Blender: G grabs (a modal move). */
    ed.command("keymap Blender");
    CHECK(ed.shortcut_text("mode.edit_toggle") == "Tab");  // a preset starts fresh
    ed.command("keymap Unity");
  });
  test("draw: snap axes in local (the rotated object's) or global (the world's) space", [&] {
    for (int global = 0; global < 2; global++) {
      Editor ed;
      ed.init_headless(800, 600);
      ed.step_frame_headless();
      ed.command("create Cube");
      GameObject *g = ed.selected_object();
      g->set_local_euler({0, 30, 0});
      g->set_world_position({0, 0.5f, 0});
      ed.command("edit face");
      ed.command(global ? "drawmode axes global" : "drawmode axes local");
      ed.command("draw rectangle");
      const Vec3 a = g->world_matrix().point({-0.2f, 0.5f, -0.2f}), b = g->world_matrix().point({0.2f, 0.5f, 0.2f});
      ed.command(strprintf("drawpoint %.6f %.6f %.6f", a.x, a.y, a.z));
      ed.command(strprintf("drawpoint %.6f %.6f %.6f", b.x, b.y, b.z));
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      const size_t inner = m.face_count() - 1;
      /* The rectangle's sides in world space: along world X / Z (global) or the cube's (local). */
      bool world_aligned = true, local_aligned = true;
      for (uint32_t k = 0; k < m.face_size(inner); k++) {
        const Vec3 e = normalize(g->world_matrix().dir(m.positions[m.face_verts(inner)[(k + 1) % m.face_size(inner)]] - m.positions[m.face_verts(inner)[k]]));
        world_aligned = world_aligned && (std::fabs(e.x) > 0.999f || std::fabs(e.z) > 0.999f);
        const Vec3 l = normalize(m.positions[m.face_verts(inner)[(k + 1) % m.face_size(inner)]] - m.positions[m.face_verts(inner)[k]]);
        local_aligned = local_aligned && (std::fabs(l.x) > 0.999f || std::fabs(l.z) > 0.999f);
      }
      std::printf("    %s axes: %u corners, world-aligned %d, object-aligned %d\n", global ? "global" : "local", m.face_size(inner), (int)world_aligned,
                  (int)local_aligned);
      CHECK(global ? world_aligned && !local_aligned : local_aligned && !world_aligned);
    }
  });
  test("z-fighting check: duplicates, inner walls and hidden faces found, the right ones removed", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    /* A cube duplicated in place: six pairs, one of each pair goes. */
    ed.command("select Cube");
    GameObject *cube = ed.selected_object();
    ed.command("duplicate");
    GameObject *copy = ed.selected_object();
    CHECK(copy && copy != cube);
    ed.command("zfight scene");
    const size_t n = ed.zfight_count();
    std::printf("    duplicated cube: %zu pair(s)\n", n);
    CHECK(n >= 6);
    ed.command("zfight fix");
    std::printf("    after fixing: %zu pair(s); faces %zu + %zu\n", ed.zfight_count(), cube->get<MeshFilter>()->mesh->face_count(),
                copy->get<MeshFilter>()->mesh->face_count());
    CHECK(ed.zfight_count() == 0);
    CHECK(cube->get<MeshFilter>()->mesh->face_count() + copy->get<MeshFilter>()->mesh->face_count() == 6);
    /* Two boxes joined side by side: the wall between them is two faces back to back; both go. */
    Mesh two = *primitives::cube();
    Mesh b2 = *primitives::cube();
    meshops::append_mesh(two, b2, Mat4::trs({1, 0, 0}, Quat(), {1, 1, 1}));
    meshops::merge_by_distance(two, 1e-5f);
    auto pairs = meshops::zfight_pairs(two);
    std::printf("    joined boxes: %zu pair(s), identical %d, back to back %d, both removable %d\n", pairs.size(), pairs.empty() ? 0 : (int)pairs[0].identical,
                pairs.empty() ? 0 : (int)!pairs[0].same_direction, pairs.empty() ? 0 : (int)(pairs[0].remove_a && pairs[0].remove_b));
    CHECK(pairs.size() == 1 && pairs[0].identical && !pairs[0].same_direction && pairs[0].remove_a && pairs[0].remove_b);
    std::vector<uint8_t> drop(two.face_count(), 0);
    drop[pairs[0].a] = drop[pairs[0].b] = 1;
    meshops::delete_faces(two, drop);
    CHECK(closed_manifold(two) && meshops::zfight_pairs(two).empty());
    /* A small face lying on a big one (same way): the small one is hidden and can go; a partial overlap can't. */
    Mesh stack;
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 2), Vec3(0, 0, 2), Vec3(0.5f, 0, 0.5f), Vec3(1, 0, 0.5f), Vec3(1, 0, 1), Vec3(0.5f, 0, 1),
                   Vec3(1.5f, 0, 1.5f), Vec3(3, 0, 1.5f), Vec3(3, 0, 3), Vec3(1.5f, 0, 3)})
      stack.add_vert(p);
    const uint32_t big[4] = {0, 3, 2, 1}, small[4] = {4, 7, 6, 5}, part[4] = {8, 11, 10, 9};
    stack.add_face(big, 4);
    stack.add_face(small, 4);
    stack.add_face(part, 4);
    pairs = meshops::zfight_pairs(stack);
    size_t hidden = 0, partial = 0;
    for (const auto &p2 : pairs) {
      if (p2.remove_a || p2.remove_b) {
        hidden++;
        CHECK((p2.remove_a ? p2.a : p2.b) == 1u);  // the small face
      }
      else partial++;
    }
    std::printf("    stacked faces: %zu pair(s), %zu hidden (removable), %zu partial\n", pairs.size(), hidden, partial);
    CHECK(hidden == 1 && partial == 1);
  });
  test("uv editor: face / island selection, rotate, scale, move, flip, fit, align; synced with Edit Mode", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("uv smart");
    GameObject *g = ed.selected_object();
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    CHECK(m.has_uvs());
    ed.command("uvsel mode face");
    ed.command("uvsel face 0");
    CHECK(ed.uv_selected_corners() == m.face_size(0));
    CHECK(ed.edit_face_selected(0) && !ed.edit_face_selected(1));  // synced
    auto corners = [&]() {
      std::vector<Vec2> v;
      for (uint32_t k = m.face_offsets[0]; k < m.face_offsets[1]; k++) v.push_back(m.uvs[k]);
      return v;
    };
    auto bounds = [](const std::vector<Vec2> &v) {
      Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
      for (Vec2 t : v) lo = Vec2(std::min(lo.x, t.x), std::min(lo.y, t.y)), hi = Vec2(std::max(hi.x, t.x), std::max(hi.y, t.y));
      return std::make_pair(lo, hi);
    };
    const auto b0 = bounds(corners());
    const Vec2 size0 = b0.second - b0.first;
    ed.command("uvxf rotate 90");
    const auto b1 = bounds(corners());
    CHECK_NEAR(b1.second.x - b1.first.x, size0.y, 1e-4f);  // width and height swap
    CHECK_NEAR(b1.second.y - b1.first.y, size0.x, 1e-4f);
    ed.command("uvxf scale 2");
    const auto b2 = bounds(corners());
    CHECK_NEAR(b2.second.x - b2.first.x, 2 * size0.y, 1e-4f);
    ed.command("uvxf fit");
    const auto b3 = bounds(corners());
    CHECK_NEAR(std::max(b3.second.x - b3.first.x, b3.second.y - b3.first.y), 1.0f, 1e-4f);
    CHECK_NEAR((b3.first.x + b3.second.x) * 0.5f, 0.5f, 1e-4f);
    ed.command("uvxf move 0.25 -0.1");
    const auto b4 = bounds(corners());
    CHECK_NEAR(b4.first.x - b3.first.x, 0.25f, 1e-4f);
    CHECK_NEAR(b4.first.y - b3.first.y, -0.1f, 1e-4f);
    const std::vector<Vec2> before_flip = corners();
    ed.command("uvxf flip_u");
    ed.command("uvxf flip_u");
    const std::vector<Vec2> after_two = corners();
    for (size_t k = 0; k < before_flip.size(); k++) CHECK(length(before_flip[k] - after_two[k]) < 1e-5f);  // twice = unchanged
    ed.command("uvxf align_left");
    for (Vec2 t : corners()) CHECK_NEAR(t.x, b4.first.x, 1e-5f);
    /* Island selection and select all / invert. */
    ed.command("uvsel island 2");
    const size_t island = ed.uv_selected_corners();
    CHECK(island >= m.face_size(2));
    ed.command("uvsel invert");
    CHECK(ed.uv_selected_corners() == m.corner_count() - island);
    ed.command("uvsel all");
    CHECK(ed.uv_selected_corners() == m.corner_count());
    /* The window draws with the new tool row. */
    ed.command("window UV Editor");
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::UVEditor).w > 0);
  });
  test("edit tools: every operator has a group (selection tools together), and Extrude Individual is a switch", [&] {
    size_t other = 0, selects = 0;
    for (const EditOpInfo &op : edit_op_table()) {
      const int gidx = edit_op_group(op.op);
      if (gidx == kEditGroupCount - 1) {
        other++;
        std::printf("    in Other: %s\n", op.op);
      }
      if (starts_with(std::string(op.op), "select_")) {
        selects++;
        CHECK(gidx == 0);
      }
    }
    CHECK(selects >= 10);
    CHECK(other <= 1);  // only Origin to Selection
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("fsel facing 0 1 0 1 0 0");
    ed.command("set_extrude_individual 1");
    ed.command("editop extrude");
    const size_t ind = ed.selected_object()->get<MeshFilter>()->mesh->face_count();
    ed.command("edit off");
    ed.command("create Cube");
    ed.command("fsel facing 0 1 0 1 0 0");
    ed.command("set_extrude_individual 0");
    ed.command("editop extrude");
    const size_t reg = ed.selected_object()->get<MeshFilter>()->mesh->face_count();
    std::printf("    extrude two faces: individual %zu faces, region %zu faces\n", ind, reg);
    CHECK(ind > reg);
    ed.command("window Modeling Tools");
    ed.step_frame_headless();
  });
}


/* ===================================================================== */
/* Round 16: Delete in the Hierarchy, drawing in open space, the UV      */
/* gizmo, Merge by Distance, Z-fighting highlights                       */
/* ===================================================================== */


/* ===================================================================== */
/* Round 17: dropdowns in dialogs, the starter scene's teapot, drawing on */
/* any axis plane, rotating imprinted faces, deleting materials, curved   */
/* surfaces                                                               */
/* ===================================================================== */


/* ===================================================================== */
/* Round 18: bevel's modal, turning shapes on rectangles, perpendicular  */
/* snaps, guides from edges and axes, Make Face, overlapping elements,    */
/* drawn shapes' transforms, Fluent-style tools                           */
/* ===================================================================== */


/* Round 26 (task 0002): what the AddressSanitizer / UBSan job found, each with a test that
 * failed under the sanitizer before its fix. */

/* Round 28: the Inspector's component and modifier menus act when an item is clicked. Popup
 * menus run at the end of the frame, after the Inspector has drawn: their items used to write to
 * the Inspector's locals, so Remove Component, Move Up and the modifier menu did nothing. */
static void round28_tests() {
  using ET = platform::EventType;
  auto ev = [](ET t, int x, int y) {
    platform::Event e;
    e.type = t;
    e.x = x;
    e.y = y;
    return e;
  };
  auto click = [&](Editor &ed, int x, int y) {
    ed.step_frame_headless({ev(ET::MouseMove, x, y)});
    ed.step_frame_headless({ev(ET::MouseDown, x, y)});
    ed.step_frame_headless({ev(ET::MouseUp, x, y)});
    ed.step_frame_headless();
    ed.step_frame_headless();
  };
  /* Opens a section's menu and clicks its item'th entry (no separators above it). */
  auto menu = [&](Editor &ed, const std::string &key, int item) {
    const Recti b = ed.inspector_menu_rect_for_test(key);
    if (b.w <= 0) return false;
    click(ed, b.x + b.w / 2, b.y + b.h / 2);
    const auto &rects = ed.ui_for_test().popup_rects();
    if (rects.empty()) return false;
    const Recti r = rects.back();
    const int row = ed.ui_for_test().row_h();
    click(ed, r.x + r.w / 2, r.y + ed.ui_for_test().px(4) + row * item + row / 2);
    return true;
  };
  auto index_of = [](GameObject *g, const char *type) {
    for (size_t i = 0; i < g->components.size(); i++)
      if (std::string(g->components[i]->type_name()) == type) return (int)i;
    return -1;
  };
  test("Inspector: Remove Component, Move Up and Reset in a component's menu act on a click, and undo", [&] {
    Editor ed;
    ed.init_headless(1600, 1000);  // tall enough for the Cube's whole Inspector (task 0045 added a Vertex Groups row)
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("component Rotator");
    ed.command("component Oscillator");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    const size_t n0 = cube->components.size();
    /* Move Up: the Oscillator goes above the Rotator. */
    int osc = index_of(cube, "Oscillator"), rot = index_of(cube, "Rotator");
    CHECK(osc == rot + 1);
    CHECK(menu(ed, "Oscillator" + std::to_string(osc), 1));
    std::printf("    after Move Up: Rotator at %d, Oscillator at %d\n", index_of(cube, "Rotator"), index_of(cube, "Oscillator"));
    CHECK(index_of(cube, "Oscillator") == rot && index_of(cube, "Rotator") == rot + 1);
    /* Reset: an edited field goes back to its default. */
    auto *r = cube->get<Rotator>();
    r->degrees_per_second = Vec3(1, 2, 3);
    ed.step_frame_headless();
    CHECK(menu(ed, "Rotator" + std::to_string(index_of(cube, "Rotator")), 2));
    r = cube->get<Rotator>();
    CHECK(r && r->degrees_per_second.x == Rotator().degrees_per_second.x && r->degrees_per_second.y == Rotator().degrees_per_second.y);
    /* Remove Component. */
    CHECK(menu(ed, "Oscillator" + std::to_string(index_of(cube, "Oscillator")), 0));
    std::printf("    components: %zu before, %zu after Remove\n", n0, cube->components.size());
    CHECK(cube->components.size() == n0 - 1 && index_of(cube, "Oscillator") < 0);
    /* Undo brings it back (and the Inspector stays usable). */
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");
    CHECK(cube && index_of(cube, "Oscillator") >= 0);
    /* A choice made on one object never lands on another: select the Sphere before the next frame. */
    ed.command("select Cube");
    ed.step_frame_headless();
    const int osc2 = index_of(cube, "Oscillator");
    const Recti b = ed.inspector_menu_rect_for_test("Oscillator" + std::to_string(osc2));
    click(ed, b.x + b.w / 2, b.y + b.h / 2);  // the menu is open
    CHECK(!ed.ui_for_test().popup_rects().empty());
    const Recti pr = ed.ui_for_test().popup_rects().back();
    const int px = pr.x + pr.w / 2, py = pr.y + ed.ui_for_test().px(4) + ed.ui_for_test().row_h() / 2;  // Remove Component
    ed.step_frame_headless({ev(ET::MouseMove, px, py)});
    ed.step_frame_headless({ev(ET::MouseDown, px, py)});
    ed.command("select Sphere");  // before the Inspector draws again
    ed.step_frame_headless({ev(ET::MouseUp, px, py)});
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    Remove chosen, then the Sphere selected: the Cube keeps its Oscillator: %d\n", index_of(ed.scene().find_by_name("Cube"), "Oscillator") == osc2);
    CHECK(index_of(ed.scene().find_by_name("Cube"), "Oscillator") == osc2);
    CHECK(index_of(ed.scene().find_by_name("Sphere"), "Oscillator") < 0);
  });
  test("Inspector: a modifier's menu (Apply, Duplicate) acts on a click", [&] {
    Editor ed;
    ed.init_headless(1600, 900);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("component ArrayModifier");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    const int arr = index_of(cube, "ArrayModifier");
    CHECK(arr >= 0);
    const size_t verts0 = cube->get<MeshFilter>()->mesh->vert_count();
    CHECK(menu(ed, "mod" + std::to_string(arr), 1));  // Duplicate
    int arrays = 0;
    for (auto &c : cube->components) arrays += std::string(c->type_name()) == "ArrayModifier";
    std::printf("    Array modifiers after Duplicate: %d\n", arrays);
    CHECK(arrays == 2);
    CHECK(menu(ed, "mod" + std::to_string(index_of(cube, "ArrayModifier")), 0));  // Apply the first
    arrays = 0;
    for (auto &c : cube->components) arrays += std::string(c->type_name()) == "ArrayModifier";
    const size_t verts1 = cube->get<MeshFilter>()->mesh->vert_count();
    std::printf("    after Apply: %d modifiers left, %zu -> %zu vertices in the mesh\n", arrays, verts0, verts1);
    CHECK(arrays == 1 && verts1 > verts0);
  });
}


/* Task 0005 review: an effect's field typed in the Inspector reaches every selected camera's stack
 * (multi-object editing, as task 0004's separate components did). */


/* ===================================================================== */
/* Round 39 (tasks 0016-0019): lighting without lag                       */
/* ===================================================================== */



/* ===================================================================== */
/* Round 41 (task 0020): manual lighting updates and a rough preview      */
/* ===================================================================== */



/* ===================================================================== */
/* Round 43 (task 0021): Baked lights exist only through the bake         */
/* ===================================================================== */



/* ===================================================================== */
/* Round 44 (task 0022): area lights bake smoothly, previews aren't blocky */
/* ===================================================================== */


/* Round 45 (task 0023, ADR 0015): the per-object render-hash memo. */

/* Round 46 (task 0024): render-frame waste. */

/* ===================================================================== */
/* Round 56 (task 0034): the Research window retired; papers in Learn     */
/* ===================================================================== */


/* ===================================================================== */
/* Round 59 (task 0037, ADR 0021): GPU devices open in the background     */
/* ===================================================================== */

static void tests_round84() {
  /* ---------------------------------------------------------------- round 84: the README and the manual (task 0060) */
  test("docs: the manual has its 13 chapters, the README and the lighting guide point to it, and every lesson it cites exists", [] {
    /* The repository's folder: the working directory or one above it (ctest runs from build/, the exe from dist/). */
    std::string top = fs::current_dir();
    for (int up = 0; up < 4 && !fs::exists(fs::join(top, "docs/MANUAL.md")); up++) top = fs::parent(top);
    std::string manual, readme, lighting;
    CHECK(fs::read_file(fs::join(top, "docs/MANUAL.md"), manual) && fs::read_file(fs::join(top, "README.md"), readme) &&
          fs::read_file(fs::join(top, "docs/guides/lighting.md"), lighting));
    manual.erase(std::remove(manual.begin(), manual.end(), '\r'), manual.end());  // (a CRLF checkout)
    int chapters = 0;
    for (int k = 1; k <= 13; k++) chapters += manual.find("\n## " + std::to_string(k) + ". ") != std::string::npos;
    CHECK(chapters == 13);
    CHECK(readme.find("docs/MANUAL.md") != std::string::npos && lighting.find("MANUAL.md#7-lighting-and-rendering") != std::string::npos);
    /* The Learn tab: lesson titles, and each lesson's "# " sections under it. */
    std::map<std::string, std::set<std::string>> learn;
    for (int i = 0; i < lesson_count(); i++) {
      std::set<std::string> &sections = learn[lesson(i).title];
      std::istringstream body(lesson(i).body);
      std::string line;
      while (std::getline(body, line))
        if (line.rfind("# ", 0) == 0) sections.insert(line.substr(2));
    }
    /* Every *name* in the manual's "> Learn:" notes: a lesson ("Bevel, Bridge and Push Through": titles may hold
     * commas), or "Lesson > section, section" with each section in that lesson. */
    std::vector<std::string> wrong;
    auto check = [&](std::string cited) {
      for (size_t d; (d = cited.find("  ")) != std::string::npos;) cited.erase(d, 1);  // (a name wrapped onto the next line)
      const size_t gt = cited.find(" > ");
      if (gt == std::string::npos) {
        if (!learn.count(cited)) wrong.push_back(cited);
        return;
      }
      const auto it = learn.find(cited.substr(0, gt));
      if (it == learn.end()) return wrong.push_back(cited.substr(0, gt));
      for (const std::string &part : split_list(cited.substr(gt + 3), ','))
        if (!it->second.count(part)) wrong.push_back(it->first + " > " + part);
    };
    std::istringstream is(manual);
    std::string line, note;
    bool in_note = false;
    while (std::getline(is, line)) {
      if (line.rfind("> Learn:", 0) == 0) in_note = true, note.clear();
      if (!in_note) continue;
      if (line.rfind(">", 0) != 0) {
        in_note = false;
        continue;
      }
      note += line.substr(1) + " ";
      for (size_t a = note.find('*'), b; a != std::string::npos && (b = note.find('*', a + 1)) != std::string::npos; a = note.find('*')) {
        const std::string cited = note.substr(a + 1, b - a - 1);
        note.erase(0, b + 1);
        check(cited);
      }
    }
    for (const std::string &m : wrong) std::printf("    not in the Learn tab: '%s'\n", m.c_str());
    CHECK(wrong.empty());
    CHECK(fs::is_web_address("https://github.com/x") && !fs::is_web_address("C:/Users/x/MANUAL.md") && !fs::is_web_address("docs/MANUAL.md"));
  });
}

static void tests_round87() {
  /* ---------------------------------------------------------------- round 87: the editor's feel (task 0063) */
  test("feel: Blender, Maya, Unreal and Unity each navigate the Scene view with their own mouse buttons, and bind their own keys", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    /* A drag with `button` held (and modifiers), 60 px right and 20 down; what it did to the camera. */
    auto drag = [&](int button, int mods) {
      const SceneCamera before = ed.scene_camera_for_test();
      platform::Event m, d, mv, u;
      m.type = mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      d.button = u.button = button;
      m.mods = d.mods = mv.mods = u.mods = mods;
      m.x = d.x = cx, m.y = d.y = cy, mv.x = u.x = cx + 60, mv.y = u.y = cy + 20;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({mv});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
      const SceneCamera &after = ed.scene_camera_for_test();
      struct { bool turned, panned, zoomed; } did{std::fabs(after.yaw - before.yaw) > 1.0f, length(after.pivot - before.pivot) > 1e-3f,
                                                  std::fabs(after.distance - before.distance) > 1e-3f};
      return did;
    };
    const int LMB = 0, RMB = 1, MMB = 2;
    /* Blender: MMB orbits, Shift+MMB pans, Ctrl+MMB zooms; RMB and Alt+LMB don't move the view. */
    ed.command("feel Blender");
    CHECK(ed.nav_style_for_test() == NavStyle::Blender && ed.shortcut_for_test("transform.grab") == "G");
    CHECK(drag(MMB, 0).turned);
    auto pan = drag(MMB, platform::MOD_SHIFT);
    CHECK(pan.panned && !pan.turned);
    CHECK(drag(MMB, platform::MOD_CTRL).zoomed);
    auto rmb = drag(RMB, 0);
    CHECK(!rmb.turned && !rmb.panned && !rmb.zoomed);
    CHECK(!drag(LMB, platform::MOD_ALT).turned);
    /* Maya: Alt+LMB tumbles, Alt+MMB tracks, Alt+RMB dollies; RMB alone does nothing to the view. */
    ed.command("feel Maya");
    CHECK(ed.shortcut_for_test("tool.move") == "W" && ed.shortcut_for_test("mode.edit_toggle") == "F8");
    CHECK(drag(LMB, platform::MOD_ALT).turned);
    CHECK(drag(MMB, platform::MOD_ALT).panned);
    CHECK(drag(RMB, platform::MOD_ALT).zoomed);
    CHECK(!drag(RMB, 0).turned);
    /* Unreal: RMB looks round (flies), Alt+P plays, End drops to the floor, Alt+J is the top view. */
    ed.command("feel Unreal");
    CHECK(ed.shortcut_for_test("play.toggle") == "Alt+P" && ed.shortcut_for_test("object.drop_to_floor") == "End" && ed.shortcut_for_test("view.top") == "Alt+J");
    CHECK(drag(RMB, 0).turned);
    CHECK(drag(LMB, platform::MOD_ALT).turned);
    /* Unreal: LMB pressed first, then RMB: both held pan (not a box selection). */
    {
      const SceneCamera before = ed.scene_camera_for_test();
      platform::Event m, dl, dr, mv, ur, ul;
      m.type = mv.type = platform::EventType::MouseMove;
      dl.type = dr.type = platform::EventType::MouseDown, ur.type = ul.type = platform::EventType::MouseUp;
      dl.button = ul.button = LMB, dr.button = ur.button = RMB;
      m.x = dl.x = dr.x = cx, m.y = dl.y = dr.y = cy, mv.x = ur.x = ul.x = cx + 40, mv.y = ur.y = ul.y = cy + 30;
      for (const platform::Event &e : {m, dl, dr, mv, ur, ul}) ed.step_frame_headless({e});
      ed.step_frame_headless();
      CHECK(length(ed.scene_camera_for_test().pivot - before.pivot) > 1e-3f && std::fabs(ed.scene_camera_for_test().yaw - before.yaw) < 1e-3f);
    }
    /* Maya's plain MMB doesn't move the view (it is the manipulator's there). */
    ed.command("feel Maya");
    CHECK(!drag(MMB, 0).panned);
    /* Unity: as it always was. */
    ed.command("feel Unity");
    CHECK(ed.nav_style_for_test() == NavStyle::Unity && ed.shortcut_for_test("tool.scale") == "R" && ed.shortcut_for_test("play.toggle") == "Ctrl+P");
    CHECK(drag(RMB, 0).turned && drag(LMB, platform::MOD_ALT).turned && drag(MMB, 0).panned && !drag(MMB, 0).turned);
    CHECK(drag(RMB, platform::MOD_ALT).zoomed && drag(LMB, platform::MOD_ALT | platform::MOD_CTRL).panned);
    CHECK(!drag(MMB, 0).zoomed);
    ed.command("feel Commodore");  // (refused: still Unity)
    CHECK(ed.nav_style_for_test() == NavStyle::Unity);
  });

  test("feel: in the Blender feel Alt+click selects an edge loop, as in Blender, and the view stays put", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("feel Blender");
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.command("edit edge");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    /* The edge nearest the camera: its middle on screen. */
    const Mesh &m = *cube->get<MeshFilter>()->mesh;
    const Vec3 eye = ed.scene_camera_for_test().position();
    Vec3 best_mid(0.0f);
    float best = 1e30f;
    for (size_t f = 0; f + 1 < m.face_offsets.size(); f++)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        const uint32_t n = c + 1 < m.face_offsets[f + 1] ? c + 1 : m.face_offsets[f];
        const Vec3 mid = cube->world_matrix().point((m.positions[m.corner_verts[c]] + m.positions[m.corner_verts[n]]) * 0.5f);
        if (length(mid - eye) < best) best = length(mid - eye), best_mid = mid;
      }
    int x = 0, y = 0;
    CHECK(ed.screen_of_for_test(best_mid, x, y));
    const float yaw = ed.scene_camera_for_test().yaw;
    platform::Event mv, d, u;
    mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
    mv.x = d.x = u.x = x, mv.y = d.y = u.y = y, mv.mods = d.mods = u.mods = platform::MOD_ALT;
    ed.step_frame_headless({mv});
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    std::printf("    Alt+click on an edge: %zu edge(s) selected (a cube's corners are poles: its loop is the edge)\n", ed.edge_sel_count_for_test());
    CHECK(ed.edge_sel_count_for_test() == 1 && ed.scene_camera_for_test().yaw == yaw);
    mv.mods = d.mods = u.mods = platform::MOD_ALT | platform::MOD_CTRL;  // Ctrl+Alt+click: the ring round the cube
    ed.step_frame_headless({mv});
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    std::printf("    Ctrl+Alt+click: %zu edges (a cube's ring is 4)\n", ed.edge_sel_count_for_test());
    CHECK(ed.edge_sel_count_for_test() == 4 && ed.scene_camera_for_test().yaw == yaw);
  });

  test("feel: Unreal's End drops the selection onto the ground and Alt+J looks from the top; the feel is kept in the prefs", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("feel Unreal");
    ed.command("create Cube");
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube != nullptr);
    if (!cube) return;
    cube->set_world_position({1, 3, 0});
    ed.mark_changed_for_test("lift");
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    platform::Event m, k;
    m.type = platform::EventType::MouseMove, m.x = r.x + r.w / 2, m.y = r.y + r.h / 2;
    k.type = platform::EventType::KeyDown, k.key = platform::KEY_END, k.x = m.x, k.y = m.y;
    ed.step_frame_headless({m});
    ed.step_frame_headless({k});
    ed.step_frame_headless();
    const AABB b = cube->world_bounds();
    std::printf("    after End: the cube's bottom at y %.4f\n", b.min.y);
    CHECK(std::fabs(b.min.y) < 1e-4f && std::fabs(cube->world_position().x - 1.0f) < 1e-5f);
    platform::Event j = k;
    j.key = platform::KEY_J, j.mods = platform::MOD_ALT;
    ed.step_frame_headless({j});
    /* The view turns over 0.28 s of real time: frames until it has (a sanitizer build's frames are slow; CI once ran
     * out of a fixed 0.5 s). */
    const double t0 = now_seconds();
    do ed.step_frame_headless();
    while ((ed.scene_camera_for_test().animating || now_seconds() - t0 < 0.3) && now_seconds() - t0 < 20.0);
    CHECK(ed.scene_camera_for_test().ortho && std::fabs(ed.scene_camera_for_test().pitch - 90.0f) < 1.0f);
    /* The prefs keep the feel (and its keys). */
    const std::string prefs = fs::join(test_dir(), "r87_prefs.txt");
    ed.prefs_for_test(prefs);
    ed.save_prefs_for_test();
    Editor ed2;
    ed2.init_headless(1400, 900);
    ed2.prefs_for_test(prefs);
    ed2.load_prefs_for_test();
    CHECK(ed2.nav_style_for_test() == NavStyle::Unreal && ed2.shortcut_for_test("play.toggle") == "Alt+P");
    /* An unknown feel in the prefs: Unity. */
    fs::write_file(prefs, "feel=Commodore\n");
    Editor ed3;
    ed3.init_headless(800, 600);
    ed3.prefs_for_test(prefs);
    ed3.load_prefs_for_test();
    CHECK(ed3.nav_style_for_test() == NavStyle::Unity);
    std::remove(prefs.c_str());
    /* The user's own key change survives a change of feel. */
    ed.command("keymap bind view.frame Shift+F");
    ed.command("feel Maya");
    CHECK(ed.shortcut_for_test("view.frame") == "Shift+F" && ed.shortcut_for_test("mode.edit_toggle") == "F8");
    /* An empty with a cube below it: End drops the pair by the cube's bottom. */
    ed.command("feel Unreal");
    GameObject *holder = ed.scene().create("R87Holder");
    holder->set_world_position({0, 5, 0});
    ed.scene().set_parent(cube, holder);
    cube->set_local_position({0, -1, 0});
    ed.select_for_test(holder->id, false);
    ed.mark_changed_for_test("parent");
    ed.step_frame_headless();
    ed.step_frame_headless({m});
    ed.step_frame_headless({k});
    ed.step_frame_headless();
    std::printf("    the empty and its cube after End: cube bottom %.4f, empty at %.4f\n", cube->world_bounds().min.y, holder->world_position().y);
    CHECK(std::fabs(cube->world_bounds().min.y) < 1e-4f && holder->world_position().y > 1.0f);
  });
}

static void tests_round88() {
  /* ---------------------------------------------------------------- round 88: typed object and asset fields (task 0064) */
  test("fields: a Skin's Armature field refuses a Cube dragged from the Hierarchy and takes the Armature; its picker lists only armatures", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    ed.command("create Sphere");
    ed.command("create Cube");
    ed.command("component SkinModifier");
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube && cube->get<SkinModifier>());
    if (!cube || !cube->get<SkinModifier>()) return;
    auto at = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    using ET = platform::EventType;
    auto drag_onto = [&](const std::string &object, const std::string &field) {
      int hx = 0, hy = 0;
      Recti f;
      if (!ed.hierarchy_row_for_test(object, hx, hy) || !ed.button_rect_for_test("field:" + field, f)) return false;
      const int fx = f.x + f.w / 3, fy = f.y + f.h / 2;
      ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
      ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
      for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, hx + (fx - hx) * k / 4, hy + (fy - hy) * k / 4)});
      ed.step_frame_headless({at(ET::MouseUp, fx, fy)});
      ed.step_frame_headless();
      return true;
    };
    auto shows = [&](const std::string &text) {
      const std::vector<std::string> l = ed.button_labels_for_test();
      return std::find(l.begin(), l.end(), "shown:" + text) != l.end();
    };
    CHECK(shows("None (Armature)"));
    const size_t steps = ed.undo_steps_for_test();
    CHECK(drag_onto("Sphere", "Armature"));
    std::printf("    after dropping the Sphere: '%s'\n", cube->get<SkinModifier>()->object.c_str());
    CHECK(cube->get<SkinModifier>()->object.empty() && ed.active_object_for_test() == cube);
    CHECK(drag_onto("Armature", "Armature"));
    std::printf("    after dropping the Armature: '%s'\n", cube->get<SkinModifier>()->object.c_str());
    CHECK(cube->get<SkinModifier>()->object == "Armature" && ed.undo_steps_for_test() == steps + 1);  // (the refused drop: no step)
    ed.step_frame_headless();
    CHECK(shows("Armature (Armature)"));
    /* The picker: None and the one armature. */
    Recti f;
    CHECK(ed.button_rect_for_test("field:Armature", f));
    const int px = f.right() - f.h / 2, py = f.y + f.h / 2;
    ed.step_frame_headless({at(ET::MouseMove, px, py)});
    ed.step_frame_headless({at(ET::MouseDown, px, py)});
    ed.step_frame_headless({at(ET::MouseUp, px, py)});
    ed.step_frame_headless();
    std::vector<std::string> items = ed.button_labels_for_test();
    const bool none = std::find(items.begin(), items.end(), "menu:None") != items.end();
    const bool arm = std::find(items.begin(), items.end(), "menu:Armature") != items.end();
    const bool sphere = std::find(items.begin(), items.end(), "menu:Sphere") != items.end();
    std::printf("    picker: None %d, Armature %d, Sphere %d\n", (int)none, (int)arm, (int)sphere);
    CHECK(none && arm && !sphere);
    /* Picking None empties it: one more step, which undo takes back. */
    Recti item;
    CHECK(ed.button_rect_for_test("menu:None", item));
    ed.step_frame_headless({at(ET::MouseMove, item.x + 10, item.y + item.h / 2)});
    ed.step_frame_headless({at(ET::MouseDown, item.x + 10, item.y + item.h / 2)});
    ed.step_frame_headless({at(ET::MouseUp, item.x + 10, item.y + item.h / 2)});
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    std::printf("    after picking None: '%s'\n", cube->get<SkinModifier>()->object.c_str());
    CHECK(cube->get<SkinModifier>()->object.empty() && ed.undo_steps_for_test() == steps + 2);
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *c2 = ed.scene().find(cube->id);
    CHECK(c2 && c2->get<SkinModifier>() && c2->get<SkinModifier>()->object == "Armature");
  });

  test("fields: a .anim dragged from the Project window onto an Animation's Clip field sets it, a .seq is refused, and the object stays selected", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    std::remove(resolve_asset_path("Assets/Animations/R88Walk.anim").c_str());
    std::remove(resolve_asset_path("Assets/Animations/R88Seq.seq").c_str());
    clear_clip_assets();
    clear_sequence_assets();
    CHECK(create_clip_asset("Assets/Animations/R88Walk.anim", "R88Walk") && create_sequence_asset("Assets/Animations/R88Seq.seq", "R88Seq"));
    ed.command("create Cube");
    ed.command("component Animation");
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube && cube->get<Animation>());
    if (!cube || !cube->get<Animation>()) return;
    const uint64_t cube_id = cube->id;
    ed.command("window Project");
    ed.command("window Inspector");
    ed.project_open_folder_for_test(resolve_asset_path("Assets/Animations"), "R88");  // (earlier runs leave many clips there)
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    auto at = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    using ET = platform::EventType;
    auto drag_file = [&](const std::string &file) {
      Recti row, f;
      if (!ed.button_rect_for_test("file:" + file, row) || !ed.button_rect_for_test("field:Clip", f)) {
        std::printf("    no row '%s' or field; recorded:", file.c_str());
        for (const std::string &l : ed.button_labels_for_test())
          if (l.rfind("file:", 0) == 0 || l.rfind("field:", 0) == 0) std::printf(" [%s]", l.c_str());
        std::printf("\n");
        return false;
      }
      const int x0 = row.x + row.w / 2, y0 = row.y + row.h / 2, fx = f.x + f.w / 3, fy = f.y + f.h / 2;
      ed.step_frame_headless({at(ET::MouseMove, x0, y0)});
      ed.step_frame_headless({at(ET::MouseDown, x0, y0)});
      for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, x0 + (fx - x0) * k / 4, y0 + (fy - y0) * k / 4)});
      ed.step_frame_headless({at(ET::MouseUp, fx, fy)});
      ed.step_frame_headless();
      return true;
    };
    CHECK(drag_file("R88Seq.seq"));
    CHECK(cube->get<Animation>()->clip.empty() && ed.active_object_for_test() == cube);
    CHECK(drag_file("R88Walk.anim"));
    std::printf("    the Clip after the drop: '%s'; the Cube still selected: %d\n", cube->get<Animation>()->clip.c_str(), (int)(ed.active_object_for_test() == cube));
    CHECK(cube->get<Animation>()->clip == "Assets/Animations/R88Walk.anim" && ed.active_object_for_test() == cube);
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *after = ed.scene().find(cube_id);  // (by id: the sample scene has a Cube of its own)
    const Animation *an = after ? after->get<Animation>() : nullptr;
    std::printf("    after undo: cube %d, Animation %d, clip '%s'\n", (int)(after != nullptr), (int)(an != nullptr), an ? an->clip.c_str() : "-");
    CHECK(an && an->clip.empty());  // (the drop alone undone)
    /* A plain click on a file row still selects it (on release). */
    Recti row;
    CHECK(ed.button_rect_for_test("file:R88Walk.anim", row));
    ed.step_frame_headless({at(ET::MouseMove, row.x + 10, row.y + row.h / 2)});
    ed.step_frame_headless({at(ET::MouseDown, row.x + 10, row.y + row.h / 2)});
    ed.step_frame_headless({at(ET::MouseUp, row.x + 10, row.y + row.h / 2)});
    ed.step_frame_headless();
    CHECK(ed.active_object_for_test() == nullptr && ed.project_selected_for_test() == resolve_asset_path("Assets/Animations/R88Walk.anim"));
  });
}

static void tests_round99() {
  /* ---------------------------------------------------------------- round 99: themes (task 0073) */
  test("themes: Emiliano Light and Dark take the site's colours, the choice is kept in the prefs, an unknown name is refused", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    CHECK(ed.theme_name_for_test() == "Dark");
    ed.command("theme Emiliano Light");
    CHECK(ed.theme_name_for_test() == "Emiliano Light");
    CHECK(ed.ui_for_test().theme.window == Color::hex(0xF1EEE5) && ed.ui_for_test().theme.text == Color::hex(0x292927) &&
          ed.ui_for_test().theme.focus == Color::hex(0xD7882B));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();  // (every window draws in a light theme)
    ed.command("theme emiliano dark");  // (case aside)
    CHECK(ed.ui_for_test().theme.window == Color::hex(0x292927) && ed.ui_for_test().theme.text == Color::hex(0xF1EEE5) &&
          ed.ui_for_test().theme.ok == Color::hex(0x45E8A2));
    ed.command("theme Neon");  // (refused)
    CHECK(ed.theme_name_for_test() == "Emiliano Dark");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();  // (every window draws in it)
    const std::string prefs = fs::join(test_dir(), "r99_prefs.txt");
    ed.prefs_for_test(prefs);
    ed.save_prefs_for_test();
    Editor ed2;
    ed2.init_headless(800, 600);
    ed2.prefs_for_test(prefs);
    ed2.load_prefs_for_test();
    CHECK(ed2.theme_name_for_test() == "Emiliano Dark" && ed2.ui_for_test().theme.window == Color::hex(0x292927));
    std::remove(prefs.c_str());
  });
}

static void tests_round101() {
  /* ---------------------------------------------------------------- round 101: touch, pen and the laptop minimum (task 0075) */
  test("touch: two fingers over the Scene view pinch-zoom, pan with the fingers and twist round the pivot; elsewhere they do nothing", [] {
    Editor ed;
    ed.init_headless(1280, 720);  // (1080p at 150 %: the HP Envy 13.3's logical size)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const Recti sr = ed.scene_rect_for_test();
    auto gesture = [&](int x, int y, float zoom, float px, float py, float rot) {
      platform::Event e;
      e.type = platform::EventType::Gesture;
      e.x = x, e.y = y, e.zoom = zoom, e.pan_x = px, e.pan_y = py, e.rotate = rot;
      ed.step_frame_headless({e});
    };
    const int cx = sr.x + sr.w / 2, cy = sr.y + sr.h / 2;
    platform::Event mv;
    mv.type = platform::EventType::MouseMove, mv.x = cx, mv.y = cy;
    ed.step_frame_headless({mv});
    const float d0 = ed.scene_camera_for_test().distance, yaw0 = ed.scene_camera_for_test().yaw;
    const Vec3 p0 = ed.scene_camera_for_test().pivot;
    gesture(cx, cy, 2.0f, 0, 0, 0);
    std::printf("    pinch x2: distance %.3f -> %.3f\n", d0, ed.scene_camera_for_test().distance);
    CHECK(std::fabs(ed.scene_camera_for_test().distance - d0 / 2.0f) < 1e-4f * d0);
    gesture(cx, cy, 1.0f, 40.0f, 0.0f, 0.0f);
    const Vec3 moved = ed.scene_camera_for_test().pivot - p0;
    std::printf("    two-finger drag right 40 px: the pivot moved %.3f along the view's left\n", -dot(moved, ed.scene_camera_for_test().right()));
    CHECK(dot(moved, ed.scene_camera_for_test().right()) < -0.01f);  // (the scene follows the fingers: the view goes left)
    gesture(cx, cy, 1.0f, 0, 0, 30.0f);
    CHECK(std::fabs(ed.scene_camera_for_test().yaw - (yaw0 - 30.0f)) < 1e-3f);
    /* Over the Inspector: nothing. */
    const float d1 = ed.scene_camera_for_test().distance;
    gesture(1260, cy, 2.0f, 0, 0, 0);
    CHECK(ed.scene_camera_for_test().distance == d1);
    /* NaN from a driver: ignored. */
    gesture(cx, cy, std::nanf(""), 0, 0, std::nanf(""));
    CHECK(std::isfinite(ed.scene_camera_for_test().distance) && std::isfinite(ed.scene_camera_for_test().yaw));
  });

  test("pen: a stroke pressed half as hard adds half the weight", [] {
    auto paint = [](float pressure) {
      R68 t;
      t.ed.command("wpbrush add 40 0.5 1");
      platform::Event pen;
      pen.type = platform::EventType::MouseMove, pen.x = 10, pen.y = 10, pen.pressure = pressure;
      t.ed.step_frame_headless({pen});
      t.stroke({Vec3(0, 0, 0)});
      return t.weight_at({0, 0, 0});
    };
    const float full = paint(1.0f), half = paint(0.5f);
    std::printf("    one dab: pressure 1 %.3f, pressure 0.5 %.3f\n", full, half);
    CHECK(full > 0.1f && std::fabs(half - full * 0.5f) < 0.02f);
  });

  test("laptop minimum: the first run on a 4-core / 8-thread or 8 GB machine starts on Low and says so, offering High", [] {
    Editor ed;
    ed.init_headless(1280, 720);
    const std::string prefs = fs::join(test_dir(), "r101_prefs_none.txt");
    std::remove(prefs.c_str());
    ed.prefs_for_test(prefs);
    ed.load_prefs_for_test();  // (no file: the first run)
    const bool low = Editor::recommended_tier() == 0;
    std::printf("    this machine: %d threads, recommended %s, the welcome shown %d\n", sys::hardware_threads(), low ? "Low" : "High",
                (int)ed.first_run_dialog_for_test());
    CHECK(ed.first_run_dialog_for_test());  // (task 0261: every first run asks where the artist comes from)
    /* The minimum machine (BLENDITY_MIN_SPEC emulates it): Low and the welcome; Use High switches and keeps it. */
    set_env("BLENDITY_MIN_SPEC", "1");
    Editor e2;
    e2.init_headless(1280, 720);
    e2.record_buttons_for_test(true);
    e2.prefs_for_test(prefs);
    e2.load_prefs_for_test();
    set_env("BLENDITY_MIN_SPEC", "0");
    CHECK(e2.first_run_dialog_for_test() && e2.quality_tier() == 0);
    for (int i = 0; i < 2; i++) e2.step_frame_headless();  // (it draws at 1280 x 720)
    Recti b;
    const bool found_high = e2.button_rect_for_test("Use High", b);
    std::printf("    Use High found %d at %d,%d %dx%d (bottom %d of 720)\n", (int)found_high, b.x, b.y, b.w, b.h, b.bottom());
    CHECK(found_high && b.bottom() <= 720 && b.right() <= 1280);  // (it fits 1080p at 150 %)
    using ET = platform::EventType;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) {
      platform::Event e;
      e.type = t, e.x = b.x + b.w / 2, e.y = b.y + b.h / 2;
      e2.step_frame_headless({e});
    }
    std::printf("    on the minimum machine: Use High clicked: tier %d, welcome %d\n", e2.quality_tier(), (int)e2.first_run_dialog_for_test());
    CHECK(e2.quality_tier() == 2 && !e2.first_run_dialog_for_test());
    std::string saved;
    CHECK(fs::read_file(prefs, saved) && saved.find("quality_tier=2") != std::string::npos);
    std::remove(prefs.c_str());
  });
}

static void tests_round102() {
  /* ---------------------------------------------------------------- round 102: installers (task 0076) */
  test("installers: a .scene given alone (a double-click) is the scene to open and its project is found above it; option values aren't", [] {
    const std::string proj = fs::join(test_dir(), "r102 project");
    fs::make_dirs(fs::join(proj, "Assets/Scenes/Levels"));
    const std::string scene = fs::join(proj, "Assets/Scenes/Levels/Level 1.scene");
    {
      std::string a0 = "Blendity.exe", a1 = scene;
      char *argv[] = {&a0[0], &a1[0]};
      const std::string got = Editor::scene_in_args(2, argv);
      std::printf("    double-clicked: '%s'\n", got.c_str());
      CHECK(got == fs::normalize(scene));
      CHECK(Editor::find_project_root({fs::parent(got)}) == proj || fs::normalize(Editor::find_project_root({fs::parent(got)})) == fs::normalize(proj));
    }
    {
      std::string a0 = "Blendity", a1 = "--layout", a2 = "x.scene", a3 = "--scene", a4 = "y.scene";
      char *argv[] = {&a0[0], &a1[0], &a2[0], &a3[0], &a4[0]};
      CHECK(Editor::scene_in_args(5, argv) == fs::normalize("y.scene"));  // (--layout's value isn't a scene; --scene's is)
      std::string b0 = "Blendity", b1 = "LEVEL.SCENE";
      char *argv2[] = {&b0[0], &b1[0]};
      CHECK(Editor::scene_in_args(2, argv2) == fs::normalize("LEVEL.SCENE"));  // (any case, as Windows' associations)
    }
    CHECK(Editor::find_project_root({fs::join(test_dir(), "no such folder/deeper")}).empty());
  });

}

static void tests_round103() {
  /* ---------------------------------------------------------------- round 103: FFmpeg linked (task 0077) */
  test("video: with FFmpeg linked, PNG frames become an H.264 MP4 of the same count, an odd size padded to even", [] {
    if (!video::available()) {
      std::printf("    built without FFmpeg: Render Animation runs an ffmpeg program or writes frames only\n");
      CHECK(!video::encode_png_frames({"x.png"}, 24.0f, fs::join(test_dir(), "r103.mp4"), nullptr));
      return;
    }
    std::printf("    %s\n", video::description().c_str());
    CHECK(video::description().find("libx264") != std::string::npos);  // (Blender's FFmpeg brings x264)
    std::vector<std::string> frames;
    for (int k = 0; k < 6; k++) {  // a 101 x 57 square moving right
      std::vector<uint32_t> px(101 * 57, 0xFF202020u);
      for (int y = 20; y < 36; y++)
        for (int x = 10 + k * 12; x < 26 + k * 12; x++) px[(size_t)y * 101 + x] = 0xFFE07020u;
      frames.push_back(fs::join(test_dir(), strprintf("r103_%04d.png", k + 1)));
      CHECK(write_png(frames.back(), px.data(), 101, 57, 101, false));
    }
    const std::string out = fs::join(test_dir(), "r103 clip é.mp4");  // (a space and an accent: UTF-8 to FFmpeg)
    fs::remove_file(out);
    std::string error;
    video::init();
    const bool ok = video::encode_png_frames(frames, 24.0f, out, &error);
    int w = 0, h = 0;
    const int n = ok ? video::probe_frames(out, &w, &h) : -1;
    std::printf("    encoded %d: %d frame(s) of %d x %d %s\n", (int)ok, n, w, h, error.c_str());
    CHECK(ok && n == 6 && w == 102 && h == 58);
    CHECK(fs::remove_file(out));  // (the UTF-8 name FFmpeg wrote is the one asked for)
    /* A frame missing halfway: refused with a reason, and no unplayable half-file left. */
    std::vector<std::string> broken = frames;
    broken[3] = fs::join(test_dir(), "no such frame.png");
    std::string err2;
    CHECK(!video::encode_png_frames(broken, 24.0f, out, &err2) && !err2.empty() && !fs::exists(out));
    std::printf("    a missing frame: '%s'\n", err2.c_str());
    /* Cancelled (the editor closing): stops, no file. */
    std::atomic<bool> cancel{true};
    std::string err3;
    CHECK(!video::encode_png_frames(frames, 24.0f, out, &err3, &cancel) && !fs::exists(out));
    for (const std::string &f : frames) std::remove(f.c_str());
  });
}

static void tests_round106() {
  /* ---------------------------------------------------------------- round 106: libraries optional at run time (task 0080) */
  test("libraries: this build knows whether it has Blender's libraries; --probe answers at once; the Libraries dialog and the choice", [] {
    const launch::Status &st = launch::status();
    std::printf("    built with the libraries: %d; full program '%s'; downloads go to %s\n", (int)st.full, launch::full_program_name().c_str(),
                launch::user_libs_dir().c_str());
#if defined(BL_WITH_TBB)
    CHECK(st.full);
#else
    CHECK(!st.full);
#endif
    {
      std::string a0 = "Blendity", a1 = "--probe";
      char *argv[] = {&a0[0], &a1[0]};
      CHECK(launch::hand_over(2, argv) == 0);  // (a probe: "my libraries loaded")
    }
    CHECK(!launch::full_build_ready(fs::join(test_dir(), "no such install")));
    CHECK(launch::user_libs_dir().find(releases::build_tag()) != std::string::npos);
    /* The Libraries dialog, from a release that has this build's libraries. */
    Editor ed;
    ed.init_headless(1280, 720);
    ed.record_buttons_for_test(true);
    const std::string p = releases::platform();
    const std::string json = strprintf("[{\"tag_name\":\"%s\",\"name\":\"this\",\"body\":\"\",\"published_at\":\"2026-10-20T00:00:00Z\",\"prerelease\":true,"
                                       "\"assets\":[{\"name\":\"blendity-libs-%s-0.10.0.zip\",\"browser_download_url\":\"https://x/l\",\"size\":157286400},"
                                       "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://x/s\",\"size\":10}]}]",
                                       releases::build_tag(), p.c_str());
    CHECK(ed.show_libraries_for_test(json));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    Recti b;
    CHECK(ed.button_rect_for_test("Download", b) && b.bottom() <= 720);
    CHECK(ed.button_rect_for_test("Never ask", b));
    using ET = platform::EventType;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) {
      platform::Event e;
      e.type = t, e.x = b.x + b.w / 2, e.y = b.y + b.h / 2;
      ed.step_frame_headless({e});
    }
    /* Never ask is kept with the preferences; so is turning the libraries off. */
    const std::string prefs = fs::join(test_dir(), "r106_prefs.txt");
    ed.prefs_for_test(prefs);
    ed.command("update libsoff");
    ed.save_prefs_for_test();
    std::string saved;
    CHECK(fs::read_file(prefs, saved) && saved.find("libs_never_ask=1") != std::string::npos && saved.find("use_libs=0") != std::string::npos);
    std::remove(prefs.c_str());
  });
}

static void tests_round109() {
  /* ---------------------------------------------------------------- round 109: measuring the editor's frames (task 0083) */
  test("profiler: an Edit Mode drag with the Move gizmo is counted - the mesh's copy, its render mesh builds, the writes, the hashes", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0");
    ed.command("camera 0 20 6 0 0.5 0");
    ed.command("tool move");
    r34_steps(ed, 2);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    const Vec3 wp = cube->world_matrix().point(cube->get<MeshFilter>()->mesh->positions[0]);
    const FrameProfile p0 = ed.frame_profile_totals();
    const MeshCounts m0 = mesh_counts();
    r34_gizmo_drag_x(ed, wp, 50);
    const FrameProfile p = ed.frame_profile_totals();
    const MeshCounts m = mesh_counts() - m0;
    std::printf("    a 5-frame drag: %llu mesh copies, %llu render meshes built, %llu moved in place, %llu edge tables, %llu scene hashes; "
                "writes %.3f ms\n",
                (unsigned long long)m.copies, (unsigned long long)m.full_builds, (unsigned long long)m.in_place, (unsigned long long)m.edge_builds,
                (unsigned long long)(p.scene_hashes - p0.scene_hashes), p.ms_edit_apply - p0.ms_edit_apply);
    CHECK(m.copies >= 1);       // (the mesh was shared with the undo step: the first write copies it)
    CHECK(m.full_builds + m.in_place >= 2);  // (every moved frame updates the render mesh: built, or moved in place)
    CHECK(p.ms_edit_apply > p0.ms_edit_apply && p.scene_hashes > p0.scene_hashes);
    /* Readings subtract field by field. */
    MeshCounts a, b;
    a.copies = 5, a.ms_edges = 2.5, b.copies = 2, b.ms_edges = 1.0;
    const MeshCounts d = a - b;
    CHECK(d.copies == 3 && std::fabs(d.ms_edges - 1.5) < 1e-9);
  });

  test("profiler: the Profiler window draws the time between frames and the mesh caches' work while a drag runs", [] {
    Editor ed;
    r34_open(ed);
    ed.record_buttons_for_test(true);
    ed.command("window Profiler");
    ed.command("select Cube");
    ed.command("edit vertex");
    ed.command("vsel 0");
    ed.command("camera 0 20 6 0 0.5 0");
    ed.command("tool move");
    r34_steps(ed, 3);
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && ed.window_rect_for_test(WindowKind::Profiler).w > 0);
    if (cube) r34_gizmo_drag_x(ed, cube->world_matrix().point(cube->get<MeshFilter>()->mesh->positions[0]), 40);
    r34_steps(ed, 2);  // (drawn with a full second of frame times and a frame's mesh work: no crash, nothing out of range)
    Recti r;
    CHECK(ed.button_rect_for_test("Between frames (last second)", r));
    CHECK(ed.button_rect_for_test("Mesh copies / render meshes built / moved in place", r));
    CHECK(ed.button_rect_for_test("Edit Mode: operator / mesh writes / overlap check", r));
  });
}

static void tests_round110() {
  /* ---------------------------------------------------------------- round 110: frame pacing (task 0084) */
  test("pacing: a 60 fps cap holds with a coarse timer and a precise one, input wakes a wait early, and one very late wake-up doesn't leave the pacer polling", [] {
    {  /* 13 ms frames, every blocking wait rounded up to a 15.6 ms timer tick */
    /* A pretend clock and a pretend wait as coarse as Windows' default timer: a blocking wait ends at the next tick
     * after it was due; a wait of 0 just lets a tenth of a millisecond go by (a poll). */
    double t = 0.0;
    const double tick = 0.015625;
    auto now = [&] { return t; };
    auto coarse = [&](double s) {
      if (s <= 0.0) {
        t += 0.0001;
        return;
      }
      t = std::ceil((t + s) / tick) * tick;
    };
    auto run = [&](bool pacer_on) {
      FramePacer pacer;
      std::vector<double> gaps;
      t = 0.0;
      double last = 0.0, prev = -1.0;
      for (int k = 0; k < 600; k++) {
        if (pacer_on) pacer.wait_until(last + 1.0 / 60.0, now, coarse);
        for (double wait = last + 1.0 / 60.0 - t; !pacer_on && wait > 0.0005; wait = last + 1.0 / 60.0 - t) coarse(wait);  // (the old loop)
        if (prev >= 0) gaps.push_back((t - prev) * 1000.0);
        prev = last = t;
        t += 0.013;  // the frame's work
      }
      gaps.erase(gaps.begin(), gaps.begin() + 20);  // (after it has learnt)
      double mean = 0;
      for (double g : gaps) mean += g;
      std::sort(gaps.begin(), gaps.end());
      return std::make_pair(mean / (double)gaps.size(), gaps[gaps.size() * 95 / 100]);
    };
    const auto before = run(false), after = run(true);
    std::printf("    between frames: waiting in poll_events %.1f ms (95%% within %.1f); the pacer %.2f ms (95%% within %.2f)\n", before.first, before.second,
                after.first, after.second);
    CHECK(before.first > 25.0);                                  // (what task 0083 measured: 33 fps)
    /* 60 fps, 95 % of frames on time. (The margin that keeps the pacer from oversleeping a coarse tick shrinks now and
     * then so it can't get stuck - the test below; under a coarse timer that costs a late frame in about seventy.) */
    CHECK(std::fabs(after.first - 1000.0 / 60.0) < 0.3 && after.second < 17.2);
    }
    {  /* a precise timer: the pacer blocks for nearly all of the wait (no spinning) */
    double t = 0.0, blocked = 0.0;
    int polls = 0;
    auto now = [&] { return t; };
    auto precise = [&](double s) {
      if (s <= 0.0) {
        t += 0.0001, polls++;
        return;
      }
      t += s + 0.0001;  // (a tenth of a millisecond late)
      blocked += s;
    };
    FramePacer pacer;
    for (int k = 0; k < 60; k++) pacer.wait_until(t + 0.004, now, precise);
    std::printf("    60 waits of 4 ms: %.1f ms blocked, %d polls, margin %.2f ms\n", blocked * 1000.0, polls, pacer.lateness() * 1000.0);
    CHECK(blocked > 0.8 * 60 * 0.004);
    CHECK(pacer.lateness() < 0.001);
    }
    {  /* input wakes a wait early; the pacer goes on waiting until the frame's turn */
    double t = 0.0;
    int calls = 0;
    auto now = [&] { return t; };
    auto woken = [&](double s) {  // every blocking wait is cut short by a mouse move after 2 ms
      calls++;
      t += s <= 0.0 ? 0.0001 : std::min(s, 0.002);
    };
    FramePacer pacer;
    pacer.wait_until(0.016, now, woken);
    CHECK(t >= 0.016 - 0.0001 && t < 0.0175);
    CHECK(calls >= 7);  // (several waits, each woken)
    }
    {  /* one very late wake-up (a debugger, a busy machine) doesn't leave the pacer polling for good */
    double t = 0.0, blocked = 0.0;
    bool hiccup = true;
    auto now = [&] { return t; };
    auto wait = [&](double s) {
      if (s <= 0.0) {
        t += 0.0001;
        return;
      }
      t += s + (hiccup ? 0.030 : 0.0001);  // the first blocking wait ends 30 ms late; then on time
      hiccup = false;
      blocked += s;
    };
    FramePacer pacer;
    double last = 0.0;
    for (int k = 0; k < 200; k++) {
      pacer.wait_until(last + 1.0 / 60.0, now, wait);
      last = t;
      t += 0.013;
    }
    const double before = blocked;
    for (int k = 0; k < 20; k++) {
      pacer.wait_until(last + 1.0 / 60.0, now, wait);
      last = t;
      t += 0.013;
    }
    std::printf("    after the hiccup: margin %.2f ms; the last 20 frames blocked %.1f ms of their waits\n", pacer.lateness() * 1000.0,
                (blocked - before) * 1000.0);
    CHECK(pacer.lateness() < 0.002);
    CHECK(blocked - before > 20 * 0.002);  // (blocking again, not polling)
    }
  });

  test("present: only the bands of rows that changed go to the window; a size change or a paint request sends all of it", [] {
    const int w = 300, h = 100;  // (100 rows: six 16-row bands and one of 4)
    std::vector<uint32_t> px((size_t)w * h, 0xFF202020u);
    std::vector<uint64_t> bands;
    auto runs = Editor::changed_rows(px.data(), w, h, bands, false);
    CHECK(runs.size() == 1 && runs[0].first == 0 && runs[0].second == h);  // (nothing presented yet: all)
    CHECK(Editor::changed_rows(px.data(), w, h, bands, false).empty());    // (the same frame again: nothing)
    px[(size_t)20 * w + 7] = 0xFFFFFFFFu;                                  // one pixel in band 1 (rows 16-31)
    px[(size_t)40 * w + 299] = 0xFF00FF00u;                                // and one in band 2 (rows 32-47), at a row's end
    px[(size_t)99 * w + 0] = 0xFF0000FFu;                                  // and one in the short last band
    runs = Editor::changed_rows(px.data(), w, h, bands, false);
    std::printf("    %zu runs:", runs.size());
    for (auto &r : runs) std::printf(" [%d, %d)", r.first, r.second);
    std::printf("\n");
    CHECK(runs.size() == 2 && runs[0] == std::make_pair(16, 48) && runs[1] == std::make_pair(96, 100));
    CHECK(Editor::changed_rows(px.data(), w, h, bands, true).size() == 1);  // (asked for all)
    std::vector<uint32_t> taller((size_t)w * 120, 0xFF202020u);
    runs = Editor::changed_rows(taller.data(), w, 120, bands, false);
    CHECK(runs.size() == 1 && runs[0].second == 120);  // (a new size: all)
  });
  test("present: a change only in a pixel's top bits (alpha), in two places of one band, is still seen", [] {
    const int w = 64, h = 16;
    std::vector<uint32_t> px((size_t)w * h, 0xFF000000u);
    std::vector<uint64_t> bands;
    Editor::changed_rows(px.data(), w, h, bands, true);
    px[1] ^= 0x80000000u;  // the top bit of the first 8-byte word
    px[9] ^= 0x80000000u;  // and of another word in the same chain
    CHECK(Editor::changed_rows(px.data(), w, h, bands, false).size() == 1);
    px[3] ^= 0x80000000u;  // two words in different chains
    px[5] ^= 0x80000000u;
    CHECK(Editor::changed_rows(px.data(), w, h, bands, false).size() == 1);
  });
  test("present (task 0198): the card takes up only the changed tiles' columns of each band - a narrow panel's change is a narrow rect, neighbouring bands with the same columns one rect", [] {
    const int w = 1000, h = 100;  // (tiles 256 wide: 0-255, 256-511, 512-767, 768-999)
    std::vector<uint32_t> px((size_t)w * h, 0xFF202020u);
    std::vector<uint64_t> tiles;
    auto rects = Editor::changed_rects(px.data(), w, h, tiles, false);
    CHECK(rects.size() == 1 && rects[0].x0 == 0 && rects[0].x1 == w && rects[0].y0 == 0 && rects[0].y1 == h);  // (nothing presented yet: all)
    CHECK(Editor::changed_rects(px.data(), w, h, tiles, false).empty());
    for (int y = 16; y < 48; y++) px[(size_t)y * w + 300] = 0xFFFFFFFFu;  // a column in tile 1, bands 1 and 2
    px[(size_t)99 * w + 999] = 0xFF00FF00u;                              // the last tile of the short last band
    rects = Editor::changed_rects(px.data(), w, h, tiles, false);
    for (const auto &r : rects) std::printf("    [%d, %d) x [%d, %d)\n", r.x0, r.x1, r.y0, r.y1);
    CHECK(rects.size() == 2);
    CHECK(rects.size() == 2 && rects[0].x0 == 256 && rects[0].x1 == 512 && rects[0].y0 == 16 && rects[0].y1 == 48);
    CHECK(rects.size() == 2 && rects[1].x0 == 768 && rects[1].x1 == 1000 && rects[1].y0 == 96 && rects[1].y1 == 100);
    CHECK(Editor::changed_rects(px.data(), w, h, tiles, true).size() == 1);  // (asked for all)
  });
}

static void tests_round115() {
  /* ---------------------------------------------------------------- round 115: the Edit Mode cage in parallel (task 0089) */
  test("overlays: lines and points recorded in parallel batches draw exactly what one call each draws (depth-tested, clipped, behind the eye)", [] {
    /* A scene's depth to test against, then 30,000 lines and 10,000 points - some off screen, some behind the camera
     * - drawn one call each into one target and in batches into another. */
    MeshPtr ball = primitives::uv_sphere(1.0f, 32, 24);
    std::vector<DrawItem> items(1);
    items[0].mesh = &ball->render_mesh();
    items[0].model = Mat4::translate({0, 0, 5});
    const Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, 5}, {0, 1, 0});
    const Mat4 p = Mat4::perspective(60 * kDeg2Rad, 320 / 200.0f, 0.1f, 100);
    LightingEnv env;
    uint32_t seed = 115;
    auto rnd = [&] { return (float)((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.0f; };
    std::vector<Vec3> pts(40000);
    for (Vec3 &q : pts) q = Vec3(rnd() * 12 - 6, rnd() * 8 - 4, rnd() * 14 - 4);  // (z from -4: some behind the eye)
    Image img[3];  // one call each, batches, no overlays (what they covered)
    RenderTarget rt[3];
    for (int k = 0; k < 3; k++) {
      img[k].resize(320, 200);
      rt[k].attach(img[k], {0, 0, 320, 200});
      Renderer3D r;
      RasterOptions opt;
      opt.shade = ShadeMode::Deferred;
      r.begin(&rt[k], v, p, env, opt);
      r.clear(0xFF303030);
      for (auto &it : items) r.add(it);
      r.flush();
      r.begin_overlay_batch();
      auto line_col = [](size_t i) { return i % 3 == 0 ? Color::hex(0xFFA733) : Color::hex(0x0A0A0A, 220); };
      if (k == 0) {
        for (size_t i = 0; i + 1 < 60000; i += 2) r.line(pts[i % 40000], pts[i % 40000] + Vec3(0.15f, 0.1f, 0.05f), line_col(i / 2), true, 5e-4f);
        for (size_t i = 0; i < 10000; i++) r.point(pts[i * 4], 2.5f, i % 5 ? Color::hex(0x000000) : Color::hex(0xFFA733), true);
      }
      else if (k == 1) {
        r.lines(30000, [&](size_t i, Vec3 &a, Vec3 &b, uint32_t &col) {
          a = pts[(2 * i) % 40000], b = pts[(2 * i) % 40000] + Vec3(0.15f, 0.1f, 0.05f), col = line_col(i);
          return true;
        }, true, 5e-4f);
        r.points(10000, 2.5f, [&](size_t i, Vec3 &q, uint32_t &col) {
          q = pts[i * 4], col = i % 5 ? Color::hex(0x000000) : Color::hex(0xFFA733);
          return true;
        }, true);
      }
      r.end_overlay_batch();
    }
    size_t drawn = 0;
    for (size_t i = 0; i < img[0].pixels.size(); i++) drawn += img[0].pixels[i] != img[2].pixels[i];
    std::printf("    %zu of %zu pixels changed by the overlays\n", drawn, img[0].pixels.size());
    CHECK(drawn > 5000 && drawn < img[0].pixels.size());
    CHECK(img[0].pixels == img[1].pixels);
    /* A `get` returning false leaves its item out, as not calling line() would. */
    Image a, b;
    RenderTarget ra, rb;
    a.resize(64, 64), b.resize(64, 64);
    ra.attach(a, {0, 0, 64, 64}), rb.attach(b, {0, 0, 64, 64});
    for (int k = 0; k < 2; k++) {
      Renderer3D r;
      RasterOptions opt;
      r.begin(k ? &rb : &ra, v, p, env, opt);
      r.clear(0xFF000000);
      if (k == 0) r.line({-1, 0, 5}, {1, 0, 5}, 0xFFFFFFFF, false);
      else
        r.lines(2, [](size_t i, Vec3 &x, Vec3 &y, uint32_t &c) {
          x = {-1, (float)i, 5}, y = {1, (float)i, 5}, c = 0xFFFFFFFF;
          return i == 0;
        }, false);
    }
    CHECK(a.pixels == b.pixels);
  });

}

static void tests_round142() {
  /* ---------------------------------------------------------------- round 142: physics for animation written up (task 0123) */
  test("learn: the physics lesson's Try it makes a person jump in straight lines, finds its contacts and shows the flight corrected", [] {
    std::string cmds;
    for (int i = 0; i < lesson_count() && cmds.empty(); i++) {
      const std::string body = lesson(i).body;
      const size_t at = body.find("| A person jumping in straight lines");
      const size_t start = at == std::string::npos ? std::string::npos : body.rfind("! cmd:", at);
      if (start != std::string::npos) cmds = body.substr(start + 6, at - start - 6);
    }
    CHECK(!cmds.empty());
    Editor ed;
    ed.init_headless(1400, 900);
    size_t a = 0;
    while (a <= cmds.size()) {  // (as the Learn tab runs them: ';'-separated console commands)
      const size_t e = cmds.find(';', a);
      std::string c = cmds.substr(a, e == std::string::npos ? std::string::npos : e - a);
      while (!c.empty() && c.front() == ' ') c.erase(c.begin());
      while (!c.empty() && c.back() == ' ') c.pop_back();
      if (!c.empty()) ed.command(c);
      if (e == std::string::npos) break;
      a = e + 1;
    }
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const size_t contacts = clip ? clip->contacts.size() : 0;
    const body::FlightResult &fr = ed.body_result_for_test();
    std::printf("    %zu contacts found; the flight shown: %s, %zu flight(s), up to %.2f m changed\n", contacts, ed.body_ghost_for_test() ? "yes" : "no",
                fr.flights.size(), fr.flights.empty() ? 0.0f : fr.flights[0].shift);
    CHECK(contacts >= 2 && ed.body_ghost_for_test() && fr.flights.size() == 1 && fr.flights[0].shift > 0.05f);
  });
}

static void tests_round165() {
  /* ---------------------------------------------------------------- round 165: Inspector groups (task 0145) */
  test("inspector groups: the camera's Composition Guides are one closed drop-down, opened by its header; `set`, saving and the hash see the fields regardless", [] {
    Editor ed;
    r34_open(ed);
    ed.command("select Main Camera");
    ed.command("guides thirds off");
    ed.command("guides show on");    // (the guides' rows show inside the group only while Show Guides is on)
    ed.command("filter add Bloom");  // (a filter stack below the camera: its Add Filter button marks where the rows below end up)
    r34_steps(ed, 2);
    const Recti hdr = ed.inspector_menu_rect_for_test("group:Composition Guides");
    CHECK(hdr.w > 0);
    CHECK(!ed.foldout_open_for_test("grp_Composition Guides"));
    GameObject *cam = ed.scene().find_by_name("Main Camera");
    Camera *c = cam ? cam->get<Camera>() : nullptr;
    CHECK(c && !c->guide_thirds);
    if (!c || hdr.w <= 0) return;
    /* Closed: the console's `set` still reaches a guide, the file keeps it, the render hash ignores it (as before). */
    const uint64_t h0 = hash_component(*c);
    ed.command("set Camera.Thirds 1");
    CHECK(c->guide_thirds && hash_component(*c) == h0);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    GameObject *bc = back.find_by_name("Main Camera");
    CHECK(bc && bc->get<Camera>() && bc->get<Camera>()->guide_thirds);
    /* The header opens it (the rows below move down) and closes it again. */
    const Recti filters_before = ed.inspector_menu_rect_for_test("add_filter");
    r34_click(ed, hdr.x + hdr.w / 2, hdr.y + hdr.h / 2);
    r34_steps(ed, 1);
    CHECK(ed.foldout_open_for_test("grp_Composition Guides"));
    const Recti filters_after = ed.inspector_menu_rect_for_test("add_filter");
    std::printf("    the camera's Add Filter button: y %d closed, %d open\n", filters_before.y, filters_after.y);
    CHECK(filters_before.w > 0 && filters_after.y > filters_before.y + ed.ui_for_test().row_h() * 5);
    const Recti hdr2 = ed.inspector_menu_rect_for_test("group:Composition Guides");
    r34_click(ed, hdr2.x + hdr2.w / 2, hdr2.y + hdr2.h / 2);
    r34_steps(ed, 1);
    CHECK(!ed.foldout_open_for_test("grp_Composition Guides"));
  });

  test("inspector groups: a material's Toon fields sit in a Toon group (open) and the axis ones in Axis Material (closed), shown only when the model is Toon", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Cube");
    ed.command("select Cube");
    r34_steps(ed, 2);
    const bool toon_before = ed.inspector_menu_rect_for_test("group:Toon").w > 0;
    ed.command("material Toon");
    r34_steps(ed, 2);
    const Recti toon = ed.inspector_menu_rect_for_test("group:Toon"), axis = ed.inspector_menu_rect_for_test("group:Axis Material");
    GameObject *cube = ed.scene().find_by_name("Cube");
    MeshRenderer *mr = cube ? cube->get<MeshRenderer>() : nullptr;
    CHECK(mr && !mr->materials.empty() && mr->materials[0]->shading_model == 1);
    if (!mr || mr->materials.empty()) return;
    const std::string key = "grp_" + std::to_string((uintptr_t)mr->materials[0].get());  // (a material's groups keep their state per material)
    std::printf("    Toon group before the preset: %s; after: Toon %s (open %d), Axis Material %s (open %d)\n", toon_before ? "shown" : "absent",
                toon.w > 0 ? "shown" : "absent", (int)ed.foldout_open_for_test(key + "Toon"), axis.w > 0 ? "shown" : "absent",
                (int)ed.foldout_open_for_test(key + "Axis Material"));
    CHECK(!toon_before && toon.w > 0 && axis.w > 0);
    CHECK(ed.foldout_open_for_test(key + "Toon") && !ed.foldout_open_for_test(key + "Axis Material"));
    /* An axis material of its own opens its Axis Material group (the state is per material, so a plain one seen
     * first doesn't close it for the rest). */
    ed.command("create Sphere");
    ed.command("select Sphere");
    ed.command("material Toon");
    GameObject *sphere = ed.scene().find_by_name("Sphere");
    MaterialPtr sm = sphere && sphere->get<MeshRenderer>() && !sphere->get<MeshRenderer>()->materials.empty() ? sphere->get<MeshRenderer>()->materials[0] : nullptr;
    CHECK(sm != nullptr);
    if (!sm) return;
    sm->axis_mode = 1, sm->touch();
    r34_steps(ed, 2);
    CHECK(ed.foldout_open_for_test("grp_" + std::to_string((uintptr_t)sm.get()) + "Axis Material"));
    /* Every other reader sees the grouped fields. */
    struct Seen : Reflector {
      std::vector<std::string> names;
      void field(const char *n, float &, float, float, float) override { names.push_back(n); }
      void field(const char *n, int &, int, int) override { names.push_back(n); }
      void field(const char *n, bool &) override { names.push_back(n); }
      void field(const char *n, Vec3 &) override { names.push_back(n); }
      void color(const char *n, Vec3 &) override { names.push_back(n); }
      void enumeration(const char *n, int &, const char *const *, int) override { names.push_back(n); }
      void text(const char *n, std::string &) override { names.push_back(n); }
      void mesh(const char *n, MeshPtr &) override { names.push_back(n); }
    } seen;
    mr->materials[0]->reflect(seen);
    const bool threshold = std::find(seen.names.begin(), seen.names.end(), "Toon Threshold") != seen.names.end();
    const bool axis_mode = std::find(seen.names.begin(), seen.names.end(), "Axis Mode") != seen.names.end();
    CHECK(threshold && axis_mode);
  });

}

static void tests_round165_208() {
  /* ---------------------------------------------------------------- round 165: overlay layers (task 0146) */
  auto r165_rig = [](Editor &ed) {
    r34_open(ed);
    ed.command("rig humanoid");
    GameObject *rig = ed.scene().find_by_name("Humanoid");
    if (rig) rig->set_world_position({0, 0, 3.0f});
    ed.command("camera 0 1.2 9 0 0.8 3");
    ed.mark_changed_for_test("R165 setup");
    r34_steps(ed, 2);
    return rig;
  };
  auto r165_bone_mid = [](Editor &ed, const char *name, int &x, int &y) {
    GameObject *b = ed.scene().find_by_name(name);
    if (!b || !b->get<Bone>()) return false;
    return ed.screen_of_for_test((ed.bone_head(*b) + ed.bone_tail(*b)) * 0.5f, x, y);
  };
  test("overlays: with the Bones layer off a click on a bone selects nothing and its handles aren't drawn, while the rig controls stay; all off / all on restores every layer", [&] {
    Editor ed;
    GameObject *rig = r165_rig(ed);
    CHECK(rig != nullptr);
    if (!rig) return;
    int x = 0, y = 0;
    CHECK(r165_bone_mid(ed, "RightLowerLeg", x, y));
    ed.command("select Main Camera");  // (something else selected: the click must change it)
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    GameObject *shin = ed.scene().find_by_name("RightLowerLeg");
    std::printf("    bones on: clicking the shin selects %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.active_object_for_test() == shin);
    ed.record_buttons_for_test(true);
    r34_steps(ed, 1);
    Recti hr;
    const bool handles_on = ed.button_rect_for_test("bone_tail", hr);
    ed.command("overlay bones off");
    CHECK(!ed.overlay_shown_for_test("bones") && ed.overlay_shown_for_test("controls"));
    r34_steps(ed, 1);
    const bool handles_off = ed.button_rect_for_test("bone_tail", hr);
    std::printf("    the selected bone's tail handle: drawn %d with the layer on, %d with it off\n", (int)handles_on, (int)handles_off);
    CHECK(handles_on && !handles_off);
    ed.command("select Main Camera");  // (something else selected: the click must change it)
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    std::printf("    bones off: clicking where the shin is selects %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.active_object_for_test() != shin);
    /* The Hierarchy (the console) still selects it. */
    ed.command("select RightLowerLeg");
    CHECK(ed.active_object_for_test() == shin);
    /* All off, all on. */
    ed.command("overlay all off");
    bool any = false;
    for (const char *k : {"bones", "controls", "icons", "points", "origins", "footprints", "outline", "grid", "gizmos", "stats"}) any = any || ed.overlay_shown_for_test(k);
    CHECK(!any);
    ed.command("overlay all on");
    bool all = true;
    for (const char *k : {"bones", "controls", "icons", "points", "origins", "footprints", "outline", "grid", "gizmos", "stats"}) all = all && ed.overlay_shown_for_test(k);
    CHECK(all);
    ed.command("overlay nothing off");  // unknown: a warning, no change
    ed.command("overlay bones maybe");  // not on, off or toggle: a warning, no change
    CHECK(ed.overlay_shown_for_test("bones"));
    ed.command("overlay bones off");
    ed.command("overlay all");  // some off: all on (the menu's All Overlays)
    CHECK(ed.overlay_shown_for_test("bones") && ed.overlay_shown_for_test("stats"));
    ed.command("overlay all");  // all on: all off
    CHECK(!ed.overlay_shown_for_test("bones") && !ed.overlay_shown_for_test("grid"));
    ed.command("overlay all on");
  });

  test("overlays: with the Light & Camera Icons layer off a click on a light's icon selects nothing; the Overlays menu toggles a layer and the choice survives the preferences", [&] {
    Editor ed;
    r34_open(ed);
    ed.command("camera 0 1.2 9 0 0.8 3");
    r34_steps(ed, 2);
    GameObject *sun = ed.scene().find_by_name("Directional Light");
    CHECK(sun != nullptr);
    if (!sun) return;
    int x = 0, y = 0;
    CHECK(ed.screen_of_for_test(sun->world_position(), x, y));
    ed.command("select Main Camera");  // (something else selected: the click must change it)
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.active_object_for_test() == sun);
    ed.command("overlay icons off");
    ed.command("select Main Camera");  // (something else selected: the click must change it)
    r34_steps(ed, 1);
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    std::printf("    icons off: clicking the sun's icon selects %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.active_object_for_test() != sun);
    ed.command("overlay icons on");
    /* The menu: the Overlays button, then the Bones row. */
    ed.record_buttons_for_test(true);
    r34_steps(ed, 1);
    const Recti btn = ed.overlay_rect_for_test("button");
    CHECK(btn.w > 0);
    if (btn.w <= 0) return;
    r34_click(ed, btn.x + btn.w / 2, btn.y + btn.h / 2);
    r34_steps(ed, 1);
    const Recti row = ed.overlay_rect_for_test("Bones");
    CHECK(row.w > 0);
    if (row.w <= 0) return;
    r34_click(ed, row.x + row.w / 2, row.y + row.h / 2);
    r34_steps(ed, 1);
    std::printf("    the menu's Bones row clicked: bones %d, the menu %s\n", (int)ed.overlay_shown_for_test("bones"), ed.popup_count_for_test() ? "still open" : "closed");
    CHECK(!ed.overlay_shown_for_test("bones") && ed.popup_count_for_test() == 0);
    /* Remembered: saved with the preferences (a scratch file), read back. */
    const std::string prefs = fs::join(test_dir(), "r165_prefs.txt");
    ed.prefs_for_test(prefs);
    ed.save_prefs_for_test();
    ed.command("overlay bones on");
    ed.load_prefs_for_test();
    CHECK(!ed.overlay_shown_for_test("bones"));
    std::string text;
    fs::read_file(prefs, text);
    CHECK(text.find("overlays_off=bones") != std::string::npos);
    /* The keys written after it are still read (a dangling else once stopped the reader there: the review). */
    fs::write_file(prefs, "overlays_off=bones\nstats=0\n");
    ed.command("overlay stats on");
    ed.load_prefs_for_test();
    CHECK(!ed.overlay_shown_for_test("bones") && !ed.overlay_shown_for_test("stats"));
    ed.command("overlay all on");
  });

}

static void coverage_tests() {
  test("keymap actions: every action id runs through run_action (a renamed or dropped id is caught), the view and tool ones with their effect", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Cube");
    auto esc = [] {
      platform::Event e;
      e.type = platform::EventType::KeyDown;
      e.key = platform::KEY_ESCAPE;
      return e;
    };
    /* Object mode: the tools, the views, the objects, Play. */
    const size_t objects_before = ed.scene().object_count();
    for (const char *id : {"tool.view", "tool.move", "tool.rotate", "tool.scale", "tool.transform", "view.perspective", "view.front", "view.top", "view.side",
                           "view.frame", "object.create_empty", "object.create_empty_child", "object.move_to_view", "object.drop_to_floor", "edit.select_all",
                           "edit.select_none", "edit.rename", "edit.adjust_last", "assets.refresh", "play.step", "window.render", "window.learn", "file.save_as",
                           "file.open", "file.save", "render.screenshot", "edit.undo", "edit.redo", "lighting.update", "mode.edit_toggle"}) {
      ed.command("select Cube");
      const bool ran = ed.run_action(id);
      ed.step_frame_headless();
      if (!ran) std::printf("    %s: refused\n", id);
      CHECK(ran);
      ed.close_dialog_for_test();
    }
    CHECK(ed.scene().object_count() >= objects_before + 2);                 // (object.create_empty and _child made one each; undo / redo gave them back)
    CHECK(ed.window_rect_for_test(WindowKind::Render).w > 0);                // (window.render opened it)
    ed.command("select Cube");
    ed.run_action("object.join");  // (one object: nothing to join, no harm)
    ed.step_frame_headless();
    CHECK(ed.run_action("view.active_camera") == true);  // (a cube: warns, still an action)
    ed.command("select Main Camera");
    CHECK(ed.run_action("view.active_camera") && ed.main_camera_owner_for_test() == ed.scene().find_by_name("Main Camera"));
    ed.run_action("play.toggle");
    ed.step_frame_headless();
    ed.run_action("play.pause");
    ed.run_action("play.toggle");  // (back out of Play)
    ed.step_frame_headless();
    /* Edit Mode: the mesh tools (each begins an operator or a modal; Esc / a frame ends it), the modes, the selections. */
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("fsel 0");
    for (const char *id : {"mode.vertex", "mode.edge", "mode.face", "mesh.extrude", "mesh.inset", "mesh.bevel", "mesh.bridge", "mesh.connect", "mesh.dissolve",
                           "mesh.duplicate", "mesh.fill", "mesh.merge_center", "mesh.merge_distance", "mesh.push_pull", "mesh.push_through",
                           "mesh.recalc_normals", "mesh.shrink_fatten", "mesh.to_sphere", "mesh.triangulate", "mesh.tris_to_quads", "mesh.proportional",
                           "select.invert", "select.linked", "select.more", "select.less", "edit.duplicate", "edit.delete", "mesh.knife", "transform.grab",
                           "transform.rotate", "transform.scale", "mode.edit_toggle"}) {
      ed.command("edit face");
      ed.command("fsel 0");
      const bool ran = ed.run_action(id);
      ed.step_frame_headless({esc()});
      ed.step_frame_headless();
      if (!ran) std::printf("    %s: refused\n", id);
      CHECK(ran);
      ed.command("edit off");
      ed.command("undo");
      ed.command("select Cube");
    }
    /* The ones that need the mouse over the Scene view say no without it. */
    ed.command("edit face");
    CHECK(!ed.run_action("mesh.loop_cut") && !ed.run_action("select.linked_pick"));
    ed.command("edit off");
    /* The draw shapes begin a drawing (run_action says yes to any draw.* id: the drawing is the check); the curve action
     * needs a curve. */
    for (const char *id : {"draw.rectangle", "draw.circle", "draw.polygon", "draw.polyline", "draw.arc"}) {
      CHECK(ed.run_action(id) && ed.draw_point_count() != SIZE_MAX);
      ed.step_frame_headless({esc()});
    }
    ed.run_action("draw.nonsense");
    CHECK(ed.draw_point_count() == SIZE_MAX);
    CHECK(!ed.run_action("points.toggle_closed"));  // (no curve selected)
    CHECK(!ed.run_action("no.such.action"));
    /* F12: the render runs on the editor's own frames; waited for. render.animation dispatches (it refuses without a
     * clip, with a warning: a selection with a clip is the render-animation tests' business). */
    CHECK(ed.run_action("render.image"));
    for (int i = 0; i < 400 && ed.rendering_for_test(); i++) ed.step_frame_headless();
    CHECK(ed.render_image_for_test().width > 0 && !ed.rendering_for_test());  // (the rasterizer renders in the call; a path-traced one on later frames)
    CHECK(ed.run_action("render.animation"));
    ed.step_frame_headless();
    CHECK(ed.run_action("file.new"));  // (a changed scene: it asks first; the dialog is closed as a click outside would)
    ed.step_frame_headless();
    ed.close_dialog_for_test();
  });

  test("console: the commands without a test of their own answer and do what they say (samples, world, scale, threads, proportional, loopcuts, selectall, subdivide, seam, focus, export, screenshot, deleteasset, the rest print)", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("samples 16");
    CHECK(ed.scene().render.samples == 16);
    ed.command("world color");
    CHECK(ed.scene().environment.mode == 3);
    ed.command("world gradient");
    CHECK(ed.scene().environment.mode == 0);
    ed.command("scale 1.5");
    ed.step_frame_headless();
    ed.command("scale 0");  // (0: follow the DPI, the default)
    ed.command("threads 2");
    CHECK(JobSystem::global().thread_limit() == 2);
    ed.command("threads 0");
    CHECK(JobSystem::global().thread_limit() != 2);
    ed.command("proportional on 2");
    ed.command("proportional off");
    ed.command("loopcuts 3 0.25");
    ed.command("surfacealign on");
    ed.command("surfacealign off");
    ed.command("raster simd off");
    ed.command("raster simd on");
    ed.command("raster uniform on");
    /* selectall: every object with a renderer. */
    ed.command("create Sphere 2");
    ed.command("selectall");
    size_t renderers = 0;
    ed.scene().for_each([&](GameObject &g) { renderers += g.get<MeshRenderer>() != nullptr; });
    ed.command("delete");  // (what selectall selected: every one of them goes, then comes back)
    size_t left = 0;
    ed.scene().for_each([&](GameObject &g) { left += g.get<MeshRenderer>() != nullptr; });
    CHECK(renderers >= 3 && left == 0);
    ed.command("undo");
    /* subdivide: a cube's six faces become 24. */
    ed.command("select Cube");
    ed.command("subdivide 1");
    CHECK(ed.scene().find_by_name("Cube")->mesh_filter()->mesh->face_count() == 24);
    ed.command("undo");
    ed.command("smooth");
    ed.command("undo");
    /* seam: the edge between two vertices marked. */
    ed.command("select Cube");
    ed.command("seam 0 1");
    ed.command("edit off");
    CHECK(!ed.scene().find_by_name("Cube")->mesh_filter()->mesh->seams.empty());
    ed.command("undo");
    /* focus: the camera's depth of field on, tracking the point. */
    ed.command("select Main Camera");
    ed.command("focus 0 0 0 2.8");
    const Camera *cam = ed.scene().find_by_name("Main Camera")->get<Camera>();
    CHECK(cam && cam->dof && cam->focus_track);
    ed.command("alignview");
    ed.command("select Cube");
    ed.command("droponsurface 450 300");
    ed.command("guidesfromedges");
    /* export: a file written; exportoptions: the dialog opens. */
    const std::string glb = fs::join(test_dir(), "console_export.glb");
    std::remove(glb.c_str());
    ed.command("export glb " + glb);
    CHECK(fs::exists(glb));
    ed.command("exportoptions glb");
    ed.step_frame_headless();
    CHECK(ed.dialog_open_for_test());
    ed.close_dialog_for_test();
    /* screenshot: a PNG of the frame. */
    const std::string png = fs::join(test_dir(), "console_shot.png");
    std::remove(png.c_str());
    ed.command("screenshot " + png);
    CHECK(fs::exists(png));
    /* deleteasset: the material's file goes to the (scratch) trash. */
    ed.command("newmat Gone");
    const std::string mat = resolve_asset_path("Assets/Materials/Gone.mat");
    CHECK(fs::exists(mat));
    ed.command("deleteasset Assets/Materials/Gone.mat");
    ed.step_frame_headless();
    CHECK(!fs::exists(mat));
    /* The ones that print: no harm; the window ones open. */
    ed.command("help");
    ed.command("libs");
    ed.command("stats");
    ed.command("texlist");
    ed.command("clear");
    ed.command("references");
    ed.command("texture uvgrid");
    ed.command("bench 1");
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Learn).w > 0);  // (references opened the Learn window on its lesson)
    for (auto [title, kind] : {std::pair<const char *, WindowKind>{"Render", WindowKind::Render}, {"Animation", WindowKind::Animation}, {"Sequencer", WindowKind::Sequencer}}) {
      ed.command(std::string("window ") + title);  // (one at a time: they share a leaf, and only the front tab has a rect)
      ed.step_frame_headless();
      CHECK(ed.window_rect_for_test(kind).w > 0);
    }
  });
}

/* Round 185 (task 0165): an operator copies the mesh once; the mesh before it is the undo step's own pointer. */
static void tests_round185() {
  test("an operator copies the mesh once: an extrude on the 50k-face sphere makes one mesh copy (the mesh before it stays the undo step's very pointer, which undo gives back), redo re-applies it, and the Profiler counts the operator's own time", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *big = ed.scene().create("Big Sphere");
    big->set_local_position({0, 2, -8});
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 256, 196);
    big->get<MeshFilter>()->mesh->smooth = false;  // (shaded flat: Auto Smooth's count is taken, once)
    big->add<MeshRenderer>();
    ed.mark_changed_for_test("Big Sphere");  // (the sphere's creation is an undo step: the next one is the extrude's)
    ed.command("select Big Sphere");
    ed.command("edit vertex");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();  // (the edits committed: the undo step shares the mesh)
    /* (The overlap check on a worker holds the mesh while it runs: the step would look shared and its bytes uncounted.) */
    for (int i = 0; i < 600 && ed.overlap_pending_for_test(); i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ed.step_frame_headless();
    }
    const Mesh *before = big->get<MeshFilter>()->mesh.get();
    const size_t faces0 = before->face_count();
    ed.command("fsel 100 101 102");
    ed.step_frame_headless();
    CHECK(big->get<MeshFilter>()->mesh.get() == before);  // (a selection copies nothing)
    const MeshCounts c0 = mesh_counts();
    const FrameProfile p0 = ed.frame_profile_totals();
    ed.command("editop extrude");
    const MeshCounts c1 = mesh_counts() - c0;
    ed.step_frame_headless();  // (the frame's profile is summed at its end)
    const FrameProfile p1 = ed.frame_profile_totals();
    const size_t faces1 = big->get<MeshFilter>()->mesh->face_count();
    std::printf("    extrude three faces of %zu: %llu mesh copies (%.1f ms), the operator %.1f ms, %zu faces after\n", faces0,
                (unsigned long long)c1.copies, c1.ms_copies, p1.ms_edit_op - p0.ms_edit_op, faces1);
    CHECK(c1.copies == 1);
    CHECK(faces1 > faces0);
    CHECK(big->get<MeshFilter>()->mesh.get() != before);
    CHECK(p1.ms_edit_op > p0.ms_edit_op);
    CHECK(ed.last_op_valid_for_test());
    ed.step_frame_headless();
    /* The undo step holds the mesh before the operator although the operator's `before` shares it: the budget counts it. */
    std::printf("    the undo stack: %zu bytes, the mesh %zu\n", ed.undo_bytes(), before->memory_bytes());
    CHECK(ed.undo_bytes() >= before->memory_bytes());
    /* F9's re-run: one copy again, the shared mesh as it was. */
    const uint64_t before_version = before->version;
    const MeshCounts c2 = mesh_counts();
    ed.command("redo amount 0.3");
    const MeshCounts c3 = mesh_counts() - c2;
    std::printf("    redo amount 0.3: %llu mesh copies, %zu faces\n", (unsigned long long)c3.copies, big->get<MeshFilter>()->mesh->face_count());
    CHECK(c3.copies == 1);
    CHECK(before->version == before_version && before->face_count() == faces0 && !before->smooth);
    CHECK(big->get<MeshFilter>()->mesh->face_count() == faces1);
    ed.step_frame_headless();
    ed.command("undo");
    const GameObject *again = ed.scene().find_by_name("Big Sphere");
    if (again && again->get<MeshFilter>()->mesh->face_count() != faces0) {  // (F9's adjustment was its own step)
      ed.command("undo");
      again = ed.scene().find_by_name("Big Sphere");
    }
    CHECK(again && again->get<MeshFilter>()->mesh.get() == before);  // the very mesh, not a copy of it
    CHECK(again && again->get<MeshFilter>()->mesh->face_count() == faces0);
    ed.command("redo");
    again = ed.scene().find_by_name("Big Sphere");
    CHECK(again && again->get<MeshFilter>()->mesh->face_count() == faces1);
  });
}

/* Round 186 (task 0166): a selection changes the selection, not the mesh. */
static void tests_round186() {
  test("a selection operator copies nothing: Select More on the 50k-face sphere keeps the very mesh (no copy, no shadow map or Game view drawn again, the Scene view alone), Select Less takes it back, and Select Loop, Linked and Invert work on the shared mesh too", [&] {
    Editor ed;
    ed.init_headless(1600, 1000);
    ed.set_render_cache_verify(false);  // (it counts renders: verify mode would re-render every hit)
    ed.command("layout 2 by 3");
    GameObject *big = ed.scene().create("Big Sphere");
    big->set_local_position({0, 2, -8});
    big->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 256, 196);
    big->add<MeshRenderer>();
    ed.mark_changed_for_test("Big Sphere");
    ed.command("select Big Sphere");
    ed.command("edit face");
    ed.command("fsel 100");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Mesh *mesh = big->get<MeshFilter>()->mesh.get();
    auto selected = [&] { return (size_t)std::count(ed.face_sel_for_test().begin(), ed.face_sel_for_test().end(), 1); };
    const size_t one = selected();
    CHECK(one == 1);
    const MeshCounts c0 = mesh_counts();
    const FrameProfile p0 = ed.frame_profile_totals();
    ed.command("edittool select_more");
    ed.step_frame_headless();
    const MeshCounts c1 = mesh_counts() - c0;
    const FrameProfile p1 = ed.frame_profile_totals();
    const size_t more = selected();
    std::printf("    Select More: %zu faces selected (was 1), %llu mesh copies, %llu view renders, %llu shadow renders, Game view %.2f ms\n", more,
                (unsigned long long)c1.copies, (unsigned long long)(p1.view_renders - p0.view_renders),
                (unsigned long long)(p1.shadow_renders - p0.shadow_renders), p1.ms_game - p0.ms_game);
    CHECK(more > one);
    CHECK(c1.copies == 0);
    CHECK(big->get<MeshFilter>()->mesh.get() == mesh);
    CHECK(p1.view_renders - p0.view_renders == 1);
    CHECK(p1.shadow_renders - p0.shadow_renders == 0);
    ed.command("edittool select_less");
    ed.step_frame_headless();
    CHECK(selected() == one);
    CHECK(big->get<MeshFilter>()->mesh.get() == mesh);
    ed.command("edittool select_invert");
    CHECK(selected() == mesh->face_count() - one);
    ed.command("edittool select_invert");
    ed.command("edittool select_linked");
    CHECK(selected() == mesh->face_count());  // (one connected sphere)
    ed.command("edit edge");
    const auto &edge = mesh->edge_cache()[mesh->edge_cache().size() / 2];
    ed.command(strprintf("vsel %u %u", edge.first, edge.second));  // (one edge: its two vertices)
    CHECK(ed.elem_for_test() == EditElement::Edge);
    ed.command("edittool select_loop");
    const size_t loop = (size_t)std::count(ed.vert_sel_for_test().begin(), ed.vert_sel_for_test().end(), 1);
    std::printf("    Select Loop from one edge: %zu vertices\n", loop);
    CHECK(loop > 2);
    CHECK(big->get<MeshFilter>()->mesh.get() == mesh);
    const MeshCounts c2 = mesh_counts() - c0;
    CHECK(c2.copies == 0);
  });
}

/* Round 187 (task 0167): undo gives back Edit Mode's selection. */
static void tests_round187() {
  test("undo gives back Edit Mode's selection: after an extrude of three faces, undo selects the three faces again (the mesh as it was, its selection as it was), redo the extruded ones", [&] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.step_frame_headless();
    ed.command("select Cube");
    ed.command("edit face");
    ed.command("fsel 1 2 3");
    ed.step_frame_headless();
    const std::vector<uint8_t> before = ed.face_sel_for_test();
    ed.command("editop extrude");
    ed.step_frame_headless();
    const std::vector<uint8_t> after = ed.face_sel_for_test();
    const size_t faces_after = after.size();
    ed.command("undo");
    ed.step_frame_headless();
    std::printf("    the cube: %zu faces with 3 selected; after the extrude %zu faces, %zu selected; undo: %zu faces, %zu selected\n", before.size(),
                faces_after, (size_t)std::count(after.begin(), after.end(), 1), ed.face_sel_for_test().size(),
                (size_t)std::count(ed.face_sel_for_test().begin(), ed.face_sel_for_test().end(), 1));
    CHECK(faces_after > before.size());
    CHECK(ed.face_sel_for_test() == before);
    ed.command("redo");
    ed.step_frame_headless();
    CHECK(ed.face_sel_for_test() == after);
  });
}

/* Task 0181: the Editing window's adjustments, document and console. */
static edit::Image flat_image(int w, int h, Vec3 c) {
  edit::Image img;
  img.resize(w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float *p = img.at(x, y);
      p[0] = c.x, p[1] = c.y, p[2] = c.z, p[3] = 1.0f;
    }
  return img;
}

static Vec3 px3(const edit::Image &img, int x, int y) {
  const float *p = img.at(x, y);
  return Vec3(p[0], p[1], p[2]);
}

static edit::Image adjusted(const edit::Image &in, const std::string &type, std::initializer_list<std::pair<const char *, double>> fields) {
  edit::Adjustment a = edit::make_adjustment(type);
  for (auto &f : fields) CHECK(a.set(f.first, (float)f.second));
  edit::Image out;
  edit::apply(a, in, out);
  return out;
}

static float enc_srgb(float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; }

static void tests_round198() {
  test("editing adjustments: exposure +1 doubles light, contrast 0 is the identity, highlights -100 lowers 0.9 and leaves 0.2, a tone curve lifts 0.5 to 0.75, HSL red -100 greys red and keeps blue", [] {
    const edit::Image grey = flat_image(8, 8, Vec3(0.18f));
    CHECK(std::fabs(px3(adjusted(grey, "Exposure", {{"Exposure", 1}}), 3, 3).x - 0.36f) < 1e-4f);
    const edit::Image mixed = flat_image(4, 4, Vec3(0.7f, 0.3f, 0.05f));
    const edit::Image c0 = adjusted(mixed, "Contrast", {{"Contrast", 0}});
    CHECK(c0.px == mixed.px);
    const edit::Image c50 = adjusted(mixed, "Contrast", {{"Contrast", 50}});
    CHECK(px3(c50, 0, 0).x > 0.7f && px3(c50, 0, 0).z < 0.05f);  // (apart from middle grey)
    const Vec3 hi = px3(adjusted(flat_image(4, 4, Vec3(0.9f)), "Highlights", {{"Highlights", -100}}), 1, 1);
    const Vec3 lo = px3(adjusted(flat_image(4, 4, Vec3(0.2f)), "Highlights", {{"Highlights", -100}}), 1, 1);
    CHECK(hi.x < 0.9f * 0.9f && std::fabs(lo.x - 0.2f) < 0.002f);
    const Vec3 sh = px3(adjusted(flat_image(4, 4, Vec3(0.02f)), "Shadows", {{"Shadows", 100}}), 1, 1);
    CHECK(sh.x > 0.02f * 1.2f);
    edit::Adjustment curve = edit::make_adjustment("tone curve");
    curve.curve[0] = {Vec2(0, 0), Vec2(0.5f, 0.75f), Vec2(1, 1)};
    edit::Image cv;
    edit::apply(curve, flat_image(2, 2, Vec3(srgb_to_linear(0.5f))), cv);
    CHECK(std::fabs(enc_srgb(px3(cv, 0, 0).x) - 0.75f) < 1e-3f);
    CHECK(std::fabs(edit::curve_eval({Vec2(0, 0), Vec2(0.5f, 0.75f), Vec2(1, 1)}, 0.25f) - 0.45f) < 0.1f);  // (smooth, no overshoot)
    edit::Image two;
    two.resize(2, 1);
    const float rb[8] = {1, 0, 0, 1, 0, 0, 1, 1};
    std::copy(rb, rb + 8, two.px.begin());
    const edit::Image h = adjusted(two, "HSL", {{"Red Saturation", -100}});
    const Vec3 r = px3(h, 0, 0), b = px3(h, 1, 0);
    CHECK(std::fabs(r.x - r.y) < 1e-4f && std::fabs(r.y - r.z) < 1e-4f);
    CHECK(std::fabs(b.x) < 1e-5f && std::fabs(b.y) < 1e-5f && std::fabs(b.z - 1.0f) < 1e-4f);
  });

  test("editing adjustments: white balance, vibrance, clarity, dehaze, sharpen, noise reduction and vignette are the identity at their defaults; warmer is redder; crop, straighten, flip and quarter turns move the right pixels", [] {
    edit::Image grad;
    grad.resize(200, 120);
    for (int y = 0; y < 120; y++)
      for (int x = 0; x < 200; x++) {
        float *p = grad.at(x, y);
        p[0] = x / 200.0f, p[1] = y / 120.0f, p[2] = 0.25f, p[3] = 1.0f;
      }
    for (const char *t : {"Temperature / Tint", "Vibrance / Saturation", "Clarity", "Sharpen", "Noise Reduction", "Vignette", "Flip / Rotate", "Crop / Straighten"}) {
      edit::Image out;
      edit::apply(edit::make_adjustment(t), grad, out);
      float worst = out.w == grad.w && out.h == grad.h ? 0.0f : 1.0f;
      for (size_t i = 0; worst < 1.0f && i < out.px.size(); i++) worst = std::max(worst, std::fabs(out.px[i] - grad.px[i]));
      if (worst >= 1e-4f) std::printf("    not the identity at its defaults: %s (%g)\n", t, worst);
      CHECK(worst < 1e-4f);
    }
    const Vec3 warm = px3(adjusted(flat_image(2, 2, Vec3(0.5f)), "Temperature / Tint", {{"Temperature", 50}}), 0, 0);
    CHECK(warm.x > warm.z * 1.05f);
    const Vec3 vib = px3(adjusted(flat_image(2, 2, Vec3(0.5f, 0.4f, 0.4f)), "Vibrance / Saturation", {{"Vibrance", 100}}), 0, 0);
    CHECK(vib.x - vib.z > 0.1f);
    edit::Image hazy = flat_image(64, 64, Vec3(0.5f, 0.52f, 0.55f));  // (a hazy object under a bright sky: the airlight)
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 64; x++) hazy.at(x, y)[0] = hazy.at(x, y)[1] = hazy.at(x, y)[2] = 0.9f;
    const edit::Image clear = adjusted(hazy, "Dehaze", {{"Dehaze", 80}});
    CHECK(px3(clear, 32, 56).x < 0.4f && std::fabs(px3(clear, 32, 4).x - 0.9f) < 0.05f);
    const edit::Image vig = adjusted(flat_image(64, 64, Vec3(0.5f)), "Vignette", {{"Amount", -100}});
    CHECK(std::fabs(px3(vig, 32, 32).x - 0.5f) < 1e-4f && px3(vig, 0, 0).x < 0.3f);
    const edit::Image cr = adjusted(grad, "Crop / Straighten", {{"X", 10}, {"Y", 10}, {"Width", 100}, {"Height", 50}});
    CHECK(cr.w == 100 && cr.h == 50);
    CHECK(cr.w == 100 && px3(cr, 0, 0).x == px3(grad, 10, 10).x && px3(cr, 99, 49).y == px3(grad, 109, 59).y);
    const edit::Image tilt = adjusted(grad, "Crop / Straighten", {{"X", 50}, {"Y", 30}, {"Width", 100}, {"Height", 60}, {"Angle", 10}});
    CHECK(tilt.w == 100 && std::fabs(px3(tilt, 50, 30).x - px3(grad, 100, 60).x) < 0.01f && px3(tilt, 0, 0).y != px3(grad, 50, 30).y);
    const edit::Image turn = adjusted(grad, "Flip / Rotate", {{"Quarter Turns", 1}});
    CHECK(turn.w == 120 && turn.h == 200 && px3(turn, 0, 0).y == px3(grad, 0, 119).y && px3(turn, 119, 0).y == px3(grad, 0, 0).y);
    const edit::Image flip = adjusted(grad, "Flip / Rotate", {{"Flip Horizontal", 1}});
    CHECK(px3(flip, 0, 5).x == px3(grad, 199, 5).x);
    for (const edit::AdjustmentInfo &info : edit::adjustment_infos()) {  // every type survives its text form
      edit::Adjustment a = edit::make_adjustment(info.name), back;
      for (size_t f = 0; f < a.v.size(); f++) a.v[f] = info.fields[f].mn + (info.fields[f].mx - info.fields[f].mn) * 0.3f;
      a.enabled = false;
      CHECK(edit::from_text(edit::to_text(a), back) && back.type == a.type && back.v == a.v && !back.enabled);
    }
    edit::Adjustment none;
    CHECK(!edit::from_text("Not An Adjustment | x=1", none) && !edit::make_adjustment("Exposure").set("Nope", 1));
  });

  test("editing document: a slider on the last step reruns only that step; undo removes the last adjustment; save and reopen reproduce the export byte for byte; every preset applies", [] {
    const std::string dir = fs::join(test_dir(), "editing");
    fs::make_dirs(dir);
    std::vector<uint32_t> src(64 * 40);
    for (int y = 0; y < 40; y++)
      for (int x = 0; x < 64; x++) src[(size_t)y * 64 + x] = 0xFF000000u | (uint32_t)(x * 4) << 16 | (uint32_t)(y * 6) << 8 | 0x40u;
    const std::string png = fs::join(dir, "source.png");
    CHECK(write_png(png, src.data(), 64, 40, 64));
    edit::Doc doc;
    std::string err;
    CHECK(doc.open_file(png, err));
    CHECK(doc.add("Exposure") == 0 && doc.add("Clarity") == 1 && doc.add("Vignette") == 2);
    doc.result();
    CHECK(doc.evaluated_steps() == 3);
    CHECK(doc.set(2, "Amount", -40));
    doc.result();
    CHECK(doc.evaluated_steps() == 1);
    CHECK(doc.set_path("Exposure", 0.5f));
    doc.result();
    CHECK(doc.evaluated_steps() == 3);
    CHECK(doc.undo() && doc.stack()[0].v[0] == 0.0f);
    CHECK(doc.undo() && doc.stack().size() == 3 && doc.stack()[2].v[0] == 0.0f);
    CHECK(doc.undo() && doc.stack().size() == 2);
    CHECK(doc.redo() && doc.stack().size() == 3);
    CHECK(doc.set_path("Crop / Straighten.Width", 30) && doc.set_path("Exposure.Exposure", 1));
    const std::string a = fs::join(dir, "a.png"), b = fs::join(dir, "b.png"), e = fs::join(dir, "look.edit");
    CHECK(doc.export_file(a, err) && doc.save(e, err));
    edit::Doc again;
    CHECK(again.load(e, err) && again.stack().size() == doc.stack().size() && again.export_file(b, err));
    std::string ba, bb;
    CHECK(fs::read_file(a, ba) && fs::read_file(b, bb) && !ba.empty() && ba == bb);
    Bitmap out;
    CHECK(load_image(a, out, err) && out.width == 30 && out.height == 40);
    for (const edit::Preset &p : edit::builtin_presets()) {
      CHECK(doc.apply_preset(p.name));
      const edit::Image &r = doc.result();
      bool finite = !r.empty();
      for (float v : r.px) finite = finite && std::isfinite(v);
      CHECK(finite && (std::string(p.name) == "Neutral") == doc.stack().empty());
    }
    CHECK(!doc.apply_preset("No Such Look") && !doc.open_file(fs::join(dir, "missing.png"), err));
    /* A source that isn't a file (a render): kept beside the .edit, reopened with its view; a picture opened after it
     * is shown plainly again. */
    edit::Image light = flat_image(16, 8, Vec3(0.3f, 1.7f, 0.05f));
    light.at(3, 2)[3] = 0.25f;
    edit::Doc render;
    render.open_pixels(light, "Render");
    render.set_view(ViewTransform::Filmic, 0.5f);
    CHECK(render.add("Exposure") == 0 && render.set(0, "Exposure", -1));
    const std::string re = fs::join(dir, "render.edit");
    CHECK(render.save(re, err) && render.edit_path() == re && !render.source_path().empty());
    edit::Doc back;
    CHECK(back.load(re, err) && back.view() == ViewTransform::Filmic && back.view_exposure() == 0.5f && back.stack().size() == 1);
    const float tol = exr_available() ? 1e-3f : 0.01f;  // (EXR keeps half floats; RGBE HDR about 8 bits of mantissa)
    CHECK(back.source().w == 16 && std::fabs(px3(back.source(), 5, 5).y - 1.7f) < tol * 1.7f);
    if (exr_available()) CHECK(back.source().at(3, 2)[3] == 0.25f);
    CHECK(back.open_file(png, err) && back.view() == ViewTransform::Standard && back.view_exposure() == 0.0f && back.edit_path().empty());
  });

  test("editing window: develop open, add, set and split show the developed picture beside the source; export writes the crop; undo; a render opens with its light", [] {
    const std::string dir = fs::join(test_dir(), "editing_window");
    fs::make_dirs(dir);
    std::vector<uint32_t> src(40 * 30, 0xFF808080u);  // mid grey (sRGB 128)
    const std::string png = fs::join(dir, "grey.png");
    CHECK(write_png(png, src.data(), 40, 30, 40));
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("window Editing");
    ed.command("develop open " + png);
    ed.command("develop set Exposure.Exposure 1");
    ed.step_frame_headless();
    const std::vector<uint32_t> &shown = ed.edit_display_for_test();
    const int want = display::linear_to_srgb8(2.0f * srgb_to_linear(128 / 255.0f));
    CHECK(shown.size() == 40 * 30 && std::abs((int)((shown[0] >> 16) & 255) - want) <= 1);
    CHECK(ed.edit_doc_for_test().stack().size() == 1);
    ed.command("develop split on");
    ed.step_frame_headless();
    CHECK(ed.edit_split_for_test() == 0.5f && ed.edit_before_for_test().size() == shown.size() && ((ed.edit_before_for_test()[0] >> 16) & 255) == 128);
    ed.command("develop add Crop / Straighten");
    ed.command("develop set Crop / Straighten.Width 20");
    ed.command("develop set Crop / Straighten.Height 10");
    const std::string out = fs::join(dir, "out.png");
    ed.command("develop export " + out);
    Bitmap b;
    std::string err;
    CHECK(load_image(out, b, err) && b.width == 20 && b.height == 10);
    ed.command("develop undo");
    ed.command("develop undo");
    ed.command("develop undo");
    CHECK(ed.edit_doc_for_test().stack().size() == 1);
    ed.command("develop set Nothing.Here 3");  // (refused: nothing added)
    ed.command("develop add Nothing");
    CHECK(ed.edit_doc_for_test().stack().size() == 1);
    ed.command("develop preset Manga Contrast");
    ed.step_frame_headless();
    const uint32_t m = ed.edit_display_for_test()[0];
    CHECK(((m >> 16) & 255) == ((m >> 8) & 255) && ((m >> 8) & 255) == (m & 255));  // (black and white)
    ed.command("edit face");  // (Edit Mode's command is untouched)
    ed.command("develop open render");  // (nothing rendered: refused, the image stays)
    CHECK(ed.edit_doc_for_test().source().w == 40);
  });
}

/* Task 0182: masks on the Editing window's adjustments. */
static void tests_round199() {
  test("editing masks: a radial mask keeps an exposure change inside its radius; a luminance range acts on the shadows only; subtract, intersect, invert and a brush; the text form round-trips", [] {
    const edit::Image grey = flat_image(64, 64, Vec3(0.18f));
    edit::Adjustment ex = edit::make_adjustment("Exposure");
    ex.set("Exposure", 1);
    edit::MaskPart radial = edit::make_mask_part(edit::MaskKind::Radial, 64, 64);
    radial.p[0] = 0.5f, radial.p[1] = 0.5f, radial.p[2] = radial.p[3] = 0.25f, radial.p[4] = 0.2f;
    ex.mask = {radial};
    edit::Image out;
    edit::apply(ex, grey, out);
    CHECK(std::fabs(px3(out, 32, 32).x - 0.36f) < 1e-4f);   // the centre: the whole change
    CHECK(std::fabs(px3(out, 2, 2).x - 0.18f) < 1e-4f);     // a corner, outside: untouched
    CHECK(std::fabs(px3(out, 32, 50).x - 0.18f) < 1e-4f);   // just outside the radius (0.25 of 64 = 16 px)
    ex.mask[0].invert = true;
    edit::apply(ex, grey, out);
    CHECK(std::fabs(px3(out, 32, 32).x - 0.18f) < 1e-4f && std::fabs(px3(out, 2, 2).x - 0.36f) < 1e-4f);
    edit::Image tones = flat_image(2, 1, Vec3(0.02f));  // a shadow and a highlight
    tones.at(1, 0)[0] = tones.at(1, 0)[1] = tones.at(1, 0)[2] = 0.8f;
    edit::MaskPart shadows = edit::make_mask_part(edit::MaskKind::Luminance);
    shadows.p[0] = 0.0f, shadows.p[1] = 0.3f, shadows.p[2] = 0.05f;
    ex.mask = {shadows};
    edit::apply(ex, tones, out);
    CHECK(std::fabs(px3(out, 0, 0).x - 0.04f) < 1e-4f && std::fabs(px3(out, 1, 0).x - 0.8f) < 1e-4f);
    edit::MaskPart left = edit::make_mask_part(edit::MaskKind::Linear);  // full at x 0, none at x 1
    left.p[0] = 0.0f, left.p[1] = 0.5f, left.p[2] = 1.0f, left.p[3] = 0.5f;
    edit::MaskPart all = left;
    all.p[2] = 100.0f;  // (nearly flat: about 1 everywhere)
    std::vector<float> w;
    std::vector<edit::MaskPart> parts = {all, left};
    parts[1].op = edit::MaskOp::Subtract;
    edit::mask_weights(parts, grey, w);
    CHECK(w[(size_t)32 * 64 + 1] < 0.05f && w[(size_t)32 * 64 + 62] > 0.9f);  // (everything minus the left)
    parts[1].op = edit::MaskOp::Intersect;
    edit::mask_weights(parts, grey, w);
    CHECK(w[(size_t)32 * 64 + 1] > 0.9f && w[(size_t)32 * 64 + 62] < 0.05f);
    edit::MaskPart red = edit::make_mask_part(edit::MaskKind::Color);
    const float pure_red[3] = {1, 0, 0}, pure_blue[3] = {0, 0, 1}, grey3[3] = {0.5f, 0.5f, 0.5f};
    CHECK(edit::mask_part_weight(red, 0.5f, 0.5f, pure_red) > 0.99f && edit::mask_part_weight(red, 0.5f, 0.5f, pure_blue) == 0.0f &&
          edit::mask_part_weight(red, 0.5f, 0.5f, grey3) == 0.0f);
    const float rose[3] = {1.0f, 0.0f, 0.03f};  // (hue about 349: across 0 from the red band's 0, inside it)
    CHECK(edit::mask_part_weight(red, 0.5f, 0.5f, rose) > 0.99f);
    edit::MaskPart soft = radial;  // (the feather: full inside 1 - feather of the radius, half way through it about half)
    soft.p[4] = 0.5f;
    const float mid_feather = edit::mask_part_weight(soft, 0.5f + 0.25f * 0.75f, 0.5f, grey3);
    CHECK(edit::mask_part_weight(soft, 0.5f + 0.25f * 0.4f, 0.5f, grey3) == 1.0f && mid_feather > 0.4f && mid_feather < 0.6f);
    edit::MaskPart light = edit::make_mask_part(edit::MaskKind::Painted, 64, 64);  // (a soft pen still paints to full)
    for (int k = 0; k < 200; k++) edit::paint_dab(light, 0.5f, 0.5f, 0.05f, 0.05f, 0.5f, false);
    CHECK(edit::mask_part_weight(light, 0.5f, 0.5f, grey3) == 1.0f);
    for (int k = 0; k < 200; k++) edit::paint_dab(light, 0.5f, 0.5f, 0.05f, 0.05f, 0.5f, true);
    CHECK(edit::mask_part_weight(light, 0.5f, 0.5f, grey3) == 0.0f);
    edit::MaskPart brush = edit::make_mask_part(edit::MaskKind::Painted, 64, 32);
    CHECK(brush.pw == edit::kPaintSize && brush.ph == edit::kPaintSize / 2);
    edit::paint_dab(brush, 0.25f, 0.5f, 0.05f, 1.0f, 0.5f, false);
    CHECK(edit::mask_part_weight(brush, 0.25f, 0.5f, grey3) > 0.99f && edit::mask_part_weight(brush, 0.75f, 0.5f, grey3) == 0.0f);
    edit::paint_dab(brush, 0.25f, 0.5f, 0.05f, 1.0f, 0.5f, true);
    CHECK(edit::mask_part_weight(brush, 0.25f, 0.5f, grey3) < 0.01f);
    edit::paint_dab(brush, 0.6f, 0.3f, 0.1f, 0.5f, 0.0f, false);
    std::vector<edit::MaskPart> all_kinds = {radial, shadows, left, red, brush}, back;
    all_kinds[1].op = edit::MaskOp::Subtract, all_kinds[3].invert = true;
    CHECK(edit::mask_from_text(edit::mask_to_text(all_kinds), back) && back.size() == 5);
    CHECK(back.size() == 5 && back[1].op == edit::MaskOp::Subtract && back[3].invert && back[4].paint == brush.paint && back[0].p[2] == radial.p[2]);
    edit::Adjustment masked = edit::make_adjustment("Clarity"), again;
    masked.mask = all_kinds;
    CHECK(edit::from_text(edit::to_text(masked), again) && again.mask.size() == 5);
    CHECK(!edit::mask_from_text("cloud,add,0,1,2,3,4,5", back) && !edit::mask_from_text("paint,add,0,0,0,0,0,0,4,4,zz", back));
    edit::Adjustment crop = edit::make_adjustment("Crop / Straighten");  // (a crop ignores a mask: it acts on the whole picture)
    crop.set("Width", 10);
    crop.mask = {radial};
    edit::apply(crop, grey, out);
    CHECK(out.w == 10);
  });

  test("editing masks in the window: develop mask adds, paints, inverts and removes parts with undo; the .edit file keeps them; Show draws the mask in red", [] {
    const std::string dir = fs::join(test_dir(), "editing_masks");
    fs::make_dirs(dir);
    std::vector<uint32_t> src(48 * 32, 0xFF808080u);
    const std::string png = fs::join(dir, "grey.png");
    CHECK(write_png(png, src.data(), 48, 32, 48));
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("window Editing");
    ed.command("develop open " + png);
    ed.command("develop add Exposure");
    ed.command("develop set Exposure.Exposure 1");
    ed.command("develop mask 0 add radial 0.5 0.5 0.2 0.3 0.1");
    edit::Doc &doc = ed.edit_doc_for_test();
    CHECK(doc.stack().size() == 1 && doc.stack()[0].mask.size() == 1 && doc.stack()[0].mask[0].p[3] == 0.3f);
    const edit::Image &r = doc.result();
    CHECK(px3(r, 24, 16).x > px3(r, 0, 0).x * 1.9f && std::fabs(px3(r, 0, 0).x - srgb_to_linear(128 / 255.0f)) < 1e-4f);
    ed.command("develop mask 0 paint 0.05 0.1 0.04");
    CHECK(doc.stack()[0].mask.size() == 2 && doc.stack()[0].mask[1].kind == edit::MaskKind::Painted);
    CHECK(px3(doc.result(), 2, 3).x > px3(doc.result(), 47, 31).x * 1.5f);
    ed.command("develop mask 0 invert 0 on");
    CHECK(doc.stack()[0].mask[0].invert);
    ed.command("develop undo");
    CHECK(!doc.stack()[0].mask[0].invert);
    ed.command("develop mask 0 subtract luminance 0.9 1 0.05");
    CHECK(doc.stack()[0].mask.size() == 3 && doc.stack()[0].mask[2].op == edit::MaskOp::Subtract);
    ed.command("develop mask 0 remove 2");
    CHECK(doc.stack()[0].mask.size() == 2);
    const std::string e = fs::join(dir, "masked.edit");
    ed.command("develop save " + e);
    edit::Doc again;
    std::string err;
    CHECK(again.load(e, err) && again.stack().size() == 1 && again.stack()[0].mask.size() == 2 && again.stack()[0].mask[1].paint == doc.stack()[0].mask[1].paint);
    ed.command("develop mask show on");
    ed.command("develop mask 0 add radial");  // (the part added is selected: Show draws the step's mask over the picture)
    ed.step_frame_headless();
    const std::vector<float> &shown = ed.edit_mask_weights_for_test();
    CHECK(shown.size() == 48 * 32 && shown[(size_t)16 * 48 + 24] > 0.99f && shown[0] < 0.01f);
    ed.command("develop mask 0 invert -1 on");  // (no such part: refused, nothing read out of range)
    ed.command("develop mask 0 remove 99");
    ed.command("develop mask -3 clear");
    ed.command("develop mask 0 add radial nan 0.5");
    CHECK(doc.stack()[0].mask.size() == 3);
    ed.command("develop mask 0 clear");
    CHECK(doc.stack()[0].mask.empty());
    ed.command("develop mask 5 clear");  // (no such step: refused)
    ed.command("develop mask 0 add cloud");
    CHECK(doc.stack()[0].mask.empty());
  });
}

/* Task 0183: textures through the Editing tab, and the stack as a camera look. */
static void tests_round200() {
  test("editing a texture: Apply puts the edit on top of the texture's stack as an albedo Image layer matching the export within 1/255; editing again goes on from the same adjustments, not on top", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ProceduralTexturePtr pt = ed.new_texture_asset_for_test("Bricks");
    CHECK(pt != nullptr);
    if (!pt) return;
    pt->width = pt->height = 128;
    pt->touch();
    const size_t layers = pt->layers.size();
    const Bitmap before = pt->output(0);
    ed.command("develop open texture " + pt->asset_path);
    edit::Doc &doc = ed.edit_doc_for_test();
    CHECK(doc.is_open() && doc.source().w == 128);
    ed.command("develop set Exposure.Exposure 0.5");
    ed.command("develop set HSL.Red Hue 30");
    ed.command("develop apply");
    CHECK(pt->layers.size() == layers + 1 && pt->layers.back()->targets == TargetAlbedo);
    std::string err;
    Bitmap exported, after = pt->output(0);
    const std::string png = resolve_asset_path(pt->asset_path.substr(0, pt->asset_path.size() - 5) + " edit.png");
    CHECK(load_image(png, exported, err) && exported.width == 128 && after.width == 128);
    int worst = 0;
    for (size_t i = 0; i < after.rgba8.size() && i < exported.rgba8.size(); i++)
      if (i % 4 != 3) worst = std::max(worst, std::abs((int)after.rgba8[i] - (int)exported.rgba8[i]));
    std::printf("    the texture's albedo after Apply vs the export: worst %d / 255\n", worst);
    CHECK(worst <= 1);
    CHECK(after.rgba8 != before.rgba8);
    {  // (an Image layer reads its file the right way up: the file's top row is the albedo's top row)
      std::vector<uint32_t> half(64 * 64, 0xFF000000u);
      for (size_t i = 0; i < 64 * 32; i++) half[i] = 0xFFFFFFFFu;  // (white on top)
      const std::string top = fs::join(test_dir(), "top_white.png");
      CHECK(write_png(top, half.data(), 64, 64, 64));
      ProceduralTexture plain;
      plain.width = plain.height = 64;
      TextureLayer *l = plain.add("Image");
      CHECK(l && ed.set_layer_field_for_test(*l, "Image", top));
      const Bitmap o = plain.output(0);
      CHECK(o.width == 64 && o.rgba8[(size_t)(2 * 64 + 2) * 4] > 200 && o.rgba8[(size_t)(60 * 64 + 2) * 4] < 50);
    }
    ed.command("develop open texture " + pt->asset_path);  // (again: the albedo under the edit, the same adjustments)
    CHECK(doc.stack().size() == 2 && doc.source().px.size() == (size_t)128 * 128 * 4);
    ed.command("develop set Exposure.Exposure 0.25");
    ed.command("develop apply");
    CHECK(pt->layers.size() == layers + 1);  // (the same layer, its image rewritten)
    CHECK(!ed.edit_doc_for_test().stack().empty());
    ed.command("develop open texture Nothing At All");  // (refused)
  });

  test("editing as a camera look: an exposure +1 stack gives the Main Camera a Color Grading with exposure 1 that doubles mid-grey; contrast, saturation, a curve, a vignette and sharpening carry over, masked and other steps are left out", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    edit::Doc &doc = ed.edit_doc_for_test();
    doc.open_pixels(flat_image(8, 8, Vec3(0.18f)), "grey");
    ed.command("develop set Exposure.Exposure 1");
    ed.command("develop look");
    GameObject *cam = ed.main_camera_owner_for_test();
    CHECK(cam && cam->get<CameraFilters>());
    if (!cam || !cam->get<CameraFilters>()) return;
    CameraFilters *cf = cam->get<CameraFilters>();
    auto first = [&](const char *type) -> FilterEffect * {  // (the camera may have had filters of its own)
      for (auto &e : cf->effects)
        if (std::string(e->type_name()) == type) return e.get();
      return nullptr;
    };
    const auto *cg = dynamic_cast<const ColorGradingEffect *>(first("Color Grading"));
    CHECK(cg && cg->exposure == 1.0f);
    std::vector<uint32_t> px(4, 0xFF000000u | 0x76u << 16 | 0x76u << 8 | 0x76u);  // (sRGB 118: linear 0.18)
    RenderTarget rt;
    rt.color = px.data(), rt.stride = 2, rt.width = 2, rt.height = 2;
    ColorGradingParams p;
    p.exposure = cg ? cg->exposure : 0.0f;
    apply_color_grading(rt, p);
    const int want = display::linear_to_srgb8(2.0f * srgb_to_linear(118 / 255.0f));
    CHECK(std::abs((int)((px[0] >> 16) & 255) - want) <= 1);
    const size_t n = cf->effects.size();
    const bool had_vignette = first("Vignette"), had_sharpen = first("Sharpen");
    ed.command("develop set Contrast.Contrast 20");
    ed.command("develop set Vibrance / Saturation.Saturation -30");
    ed.command("develop curve rgb 0,0.05 0.5,0.55 1,0.95");
    ed.command("develop set Vignette.Amount -40");
    ed.command("develop set Sharpen.Amount 75");
    ed.command("develop set Clarity.Clarity 30");
    ed.command("develop add Shadows");
    ed.command("develop set Shadows.Shadows 40");
    ed.command("develop mask 7 add radial");
    ed.command("develop look");
    const size_t added = (had_vignette ? 0 : 1) + (had_sharpen ? 0 : 1);  // (the Color Grading reused; Clarity and the masked Shadows left out)
    CHECK(cf->effects.size() == n + added);
    ed.command("develop look");
    CHECK(cf->effects.size() == n + added);  // (again: the same effects, replaced)
    const auto *g2 = dynamic_cast<const ColorGradingEffect *>(first("Color Grading"));
    CHECK(g2 && g2->exposure == 1.0f && std::fabs(g2->contrast - 0.2f) < 1e-6f && std::fabs(g2->saturation + 0.3f) < 1e-6f);
    CHECK(g2 && g2->lift.x > 0.0f && g2->gain.x < 1.0f && g2->gamma.x > 0.05f);
    if (g2) {  // (the curve's black, grey and white through the camera's lift / gamma / gain)
      auto lgg = [&](float v) { return std::pow(g2->gain.x * (v + g2->lift.x * (1.0f - v)), 1.0f / g2->gamma.x); };
      auto dec = [](float e) { return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f); };
      CHECK(std::fabs(lgg(0.0f) - dec(0.05f)) < 1e-3f && std::fabs(lgg(1.0f) - dec(0.95f)) < 1e-3f);
    }
    const auto *vg = dynamic_cast<const VignetteEffect *>(first("Vignette"));
    CHECK(vg && std::fabs(vg->intensity - (1.0f - std::pow(0.6f, 1.0f / 2.4f))) < 1e-5f && first("Sharpen"));
    edit::Doc &d2 = ed.edit_doc_for_test();
    d2.open_pixels(flat_image(4, 4, Vec3(0.5f)), "only clarity");
    ed.command("develop set Clarity.Clarity 40");
    ed.command("develop look");  // (nothing a camera does: refused, nothing added)
    CHECK(cf->effects.size() == n + added);
  });
}

/* Task 0191: the Editing tab at 4K - a preview the window's size, tables for the display axis. */
static void tests_round201() {
  test("editing preview: the window's preview is the source made no larger than asked, the stack on it matches the full result made small, a crop on it is the crop at its scale; noise reduction still smooths", [] {
    edit::Image src;
    src.resize(400, 300);
    for (int y = 0; y < 300; y++)
      for (int x = 0; x < 400; x++) {
        float *p = src.at(x, y);
        p[0] = 0.9f * x / 400, p[1] = 0.7f * y / 300, p[2] = 0.2f + 0.1f * ((x / 25 + y / 25) % 2), p[3] = 1.0f;
      }
    edit::Doc doc;
    doc.open_pixels(src, "gradient");
    doc.add("Exposure"), doc.add("Contrast"), doc.add("Tone Curve"), doc.add("HSL");
    doc.set_path("Exposure", 0.4f), doc.set_path("Contrast", 20), doc.set_path("HSL.Orange Saturation", 30);
    doc.set_curve(2, 0, {Vec2(0, 0.03f), Vec2(0.5f, 0.6f), Vec2(1, 1)});
    const edit::Image &pv = doc.preview(200);
    CHECK(pv.w == 200 && pv.h == 150 && std::fabs(doc.preview_scale() - 0.5f) < 1e-6f);
    const edit::Image pv_copy = pv;
    const edit::Image &full = doc.result();
    double diff = 0;
    for (int y = 0; y < 150; y++)
      for (int x = 0; x < 200; x++)
        for (int k = 0; k < 3; k++) {
          float avg = 0;  // (the full result's 2 x 2 block)
          for (int j = 0; j < 4; j++) avg += full.at(2 * x + j % 2, 2 * y + j / 2)[k] * 0.25f;
          diff += std::fabs(avg - pv_copy.at(x, y)[k]);
        }
    diff /= 200.0 * 150 * 3;
    std::printf("    the preview vs the full result made small: mean difference %.5f\n", diff);
    CHECK(diff < 0.01);
    CHECK(&doc.preview(1000) == &doc.result());  // (a picture smaller than the preview: the full result itself)
    CHECK(doc.set_path("Crop / Straighten.Width", 100) && doc.set_path("Crop / Straighten.Height", 50) && doc.set_path("Crop / Straighten.X", 40));
    const edit::Image &cropped = doc.preview(200);
    CHECK(cropped.w == 50 && cropped.h == 25 && doc.result().w == 100 && doc.result().h == 50);
    {  // (the crop's content: the preview's corner is the full crop's corner made small)
      const float full_corner = doc.result().at(1, 1)[0];
      CHECK(std::fabs(doc.preview(200).at(0, 0)[0] - full_corner) < 0.02f);
    }
    /* Buffers reused: a middle step changed, then a masked and a spatial step, against a document made fresh. */
    doc.set_path("Contrast", 35);
    doc.add("Clarity"), doc.set_path("Clarity", 30);
    doc.add("Vignette"), doc.set_path("Vignette.Amount", -40);
    edit::MaskPart circle = edit::make_mask_part(edit::MaskKind::Radial, 400, 300);
    doc.set_mask((int)doc.stack().size() - 1, {circle});
    const edit::Image again = doc.result();
    edit::Doc fresh;
    fresh.open_pixels(src, "gradient");
    fresh.set_stack(doc.stack());
    CHECK(again.px == fresh.result().px);
    int sw = 0, sh = 0;
    doc.step_input_size((int)doc.stack().size(), sw, sh);
    CHECK(sw == 100 && sh == 50);
    edit::Image noisy = flat_image(64, 64, Vec3(0.3f));
    uint32_t seed = 7;
    for (float &v : noisy.px) {
      seed = seed * 1664525u + 1013904223u;
      v = (&v - noisy.px.data()) % 4 == 3 ? 1.0f : std::max(0.0f, v + ((seed >> 8) / 16777216.0f - 0.5f) * 0.1f);
    }
    edit::Image smooth;
    edit::Adjustment nr = edit::make_adjustment("Noise Reduction");
    nr.set("Luminance", 100), nr.set("Color", 100);
    edit::apply(nr, noisy, smooth);
    auto variance = [](const edit::Image &img) {
      double m = 0, m2 = 0;
      for (int i = 0; i < img.w * img.h; i++) m += img.px[(size_t)i * 4 + 1], m2 += img.px[(size_t)i * 4 + 1] * img.px[(size_t)i * 4 + 1];
      m /= img.w * img.h;
      return m2 / (img.w * img.h) - m * m;
    };
    CHECK(variance(smooth) < variance(noisy) * 0.5);
  });
}

/* Task 0187: windows beside the main one. */
static platform::Event mouse_ev(platform::EventType t, int x, int y, int button = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.button = button;
  return e;
}

static bool has_content(const Image &fb) {  // (more than one colour)
  for (uint32_t p : fb.pixels)
    if (p != fb.pixels[0]) return true;
  return false;
}

static void tests_round203() {
  test("windows: dock move Render newwindow gives it a window of its own that draws, takes its own clicks and popups, scales on its own, and gives the tab back when closed", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("window Render");
    ed.step_frame_headless();
    CHECK(ed.in_main_dock_for_test(WindowKind::Render));
    ed.command("dock move Render newwindow");
    CHECK(ed.window_count_for_test() == 2 && !ed.in_main_dock_for_test(WindowKind::Render));
    ed.step_frame_headless();
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(has_content(w->fb) && has_content(ed.main_fb_for_test()));
    ed.command("window Render");  // (it is in the other window: shown there, not docked twice)
    CHECK(!ed.in_main_dock_for_test(WindowKind::Render) && ed.window_count_for_test() == 2);
    // (the window's one tab is at its top left)
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, 30, 10)});
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseDown, 30, 10, 1), mouse_ev(platform::EventType::MouseUp, 30, 10, 1)});
    ed.step_frame_headless();
    CHECK(w->ui.any_popup_open());  // (its tab menu, in its own window)
    CHECK(!ed.ui_for_test().any_popup_open());
    w->ui.close_popups();
    w->scale_for_test = 1.5f;
    ed.step_frame_headless();
    CHECK(w->ui.scale == 1.5f && ed.ui_for_test().scale == 1.0f);
    const std::string layout = ed.windows_layout_for_test();
    CHECK(layout.rfind("W(", 0) == 0 && layout.find("L(9:0)") != std::string::npos);
    ed.step_window_headless(1, {platform::Event{platform::EventType::Quit}});
    CHECK(ed.window_count_for_test() == 1 && ed.in_main_dock_for_test(WindowKind::Render));
    ed.restore_windows_for_test(layout);  // (the layout string brings the window back)
    CHECK(ed.window_count_for_test() == 2 && !ed.in_main_dock_for_test(WindowKind::Render));
    ed.dock_reset_for_test("Default");  // (a layout for the main window: Render stays in its own window, not in both)
    CHECK(!ed.in_main_dock_for_test(WindowKind::Render) && ed.window_count_for_test() == 2);
    ed.command("dock move Render main");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 1 && ed.in_main_dock_for_test(WindowKind::Render));
    ed.command("dock move Nothing newwindow");  // (refused)
    CHECK(ed.window_count_for_test() == 1);
    ed.restore_windows_for_test("W(1,2,3,4,0,H");  // (a preferences line cut short: nothing, no crash)
    ed.restore_windows_for_test("W(5000,5000,90000,90000,3,L(8:0))");  // (a display gone, a size too big: on the primary, fitted)
    CHECK(ed.window_count_for_test() == 2);
    EditorWindow *fitted = ed.window_for_test(3);
    CHECK(fitted && fitted->fb.width <= 1920 && fitted->fb.height <= 1080 && fitted->x >= 0 && fitted->x < 1920);
  });

  test("windows: displays are listed; a tab moves to the second display's middle; a tab dragged out of the editor gets a window at the mouse", [] {
    const std::vector<platform::Monitor> real = platform::monitors();
    CHECK(!real.empty() && real[0].width > 0 && real[0].height > 0);
    Editor ed;
    ed.init_headless(1200, 800);
    ed.set_monitors_for_test({platform::Monitor{0, 0, 1920, 1080, 1.0f, "One", true}, platform::Monitor{1920, 0, 2560, 1440, 1.5f, "Two", false}});
    ed.command("dock move Console monitor 2");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 2);
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w && w->monitor == 1 && w->x >= 1920 && w->fb.width == 2560 * 3 / 4);
    ed.command("dock move Console monitor 1");  // (the same window, moved)
    CHECK(ed.window_count_for_test() == 2 && w && w->monitor == 0 && w->x < 1920);
    ed.command("dock move Console main");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 1);
    const Recti leaf = ed.dock_leaf_rect_for_test(WindowKind::Inspector);  // (Inspector: the first tab of its area)
    CHECK(leaf.w > 0);
    const int tx = leaf.x + 12, ty = leaf.y + 6;
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, tx, ty)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, tx, ty)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, tx + 300, ty + 40)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, 1300, ty + 40)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, 1300, ty + 40)});
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 2 && !ed.in_main_dock_for_test(WindowKind::Inspector));
  });
}

/* Task 0188: a render-only window, full screen, a pen on the Editing brush, Tablet mode. */
static void tests_round204() {
  test("windows: render-only shows the render alone, fitted to the window, its controls only at the top; full screen takes the display; Tablet makes a window's UI 1.5 times as big", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    Image pic;
    pic.resize(800, 450);
    for (size_t i = 0; i < pic.pixels.size(); i++) pic.pixels[i] = 0xFF000000u | (uint32_t)(i % 800 * 255 / 800) << 16;
    ed.set_render_image_for_test(pic);
    ed.command("dock move Render newwindow");
    ed.step_frame_headless();
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w != nullptr);
    if (!w) return;
    ed.command("window renderonly on");
    w->ui.record_buttons = true;
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, w->fb.width / 2, w->fb.height / 2)});
    CHECK(w->render_only && w->ui.button_rects.empty());  // (no chrome while the mouse is away from the top)
    const Recti pr = ed.render_picture_rect_for_test();
    CHECK(std::abs(pr.w - w->fb.width) <= 2 || std::abs(pr.h - w->fb.height) <= 2);
    std::printf("    the picture %d x %d in a %d x %d window\n", pr.w, pr.h, w->fb.width, w->fb.height);
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, 40, 6)});
    CHECK(w->ui.button_rects.count("Show Tabs") == 1 && w->ui.button_rects.count("Full Screen") == 1);
    ed.set_monitors_for_test({platform::Monitor{0, 0, 1920, 1080, 1.0f, "One", true}, platform::Monitor{1920, 0, 2560, 1440, 1.0f, "Two", false}});
    w->monitor = 1;
    const int before_w = w->fb.width;
    ed.command("window fullscreen on");
    CHECK(w->fullscreen && w->fb.width == 2560 && w->fb.height == 1440 && w->x == 1920 && w->y == 0);
    ed.command("window fullscreen off");
    CHECK(!w->fullscreen && w->fb.width == before_w);
    const Recti fsb = w->ui.button_rects.count("Full Screen") ? w->ui.button_rects["Full Screen"] : Recti{0, 0, 0, 0};
    const int main_w = ed.main_fb_for_test().width;
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, fsb.x + 5, fsb.y + 5)});
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseDown, fsb.x + 5, fsb.y + 5)});
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseUp, fsb.x + 5, fsb.y + 5)});
    CHECK(w->fullscreen && w->fb.width == 2560 && ed.main_fb_for_test().width == main_w);  // (the strip's window, not the main one)
    platform::Event esc;
    esc.type = platform::EventType::KeyDown, esc.key = platform::KEY_ESCAPE;
    ed.step_window_headless(1, {esc});
    CHECK(!w->fullscreen && w->fb.width == before_w);  // (Esc leaves)
    ed.command("window fullscreen on");
    const Recti tabs_button = w->ui.button_rects.count("Show Tabs") ? w->ui.button_rects["Show Tabs"] : Recti{0, 0, 0, 0};
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, tabs_button.x + 5, tabs_button.y + 5)});
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseDown, tabs_button.x + 5, tabs_button.y + 5)});
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseUp, tabs_button.x + 5, tabs_button.y + 5)});
    CHECK(!w->render_only && !w->fullscreen);  // (Show Tabs leaves full screen too: no frameless window left without its strip)
    ed.command("window renderonly off");
    ed.step_window_headless(1, {mouse_ev(platform::EventType::MouseMove, w->fb.width / 2, w->fb.height / 2)});
    CHECK(!w->render_only && !w->ui.button_rects.empty());  // (the Render window's own controls again)
    const float main_scale = ed.ui_for_test().scale;
    ed.command("window tablet on 7");  // (no such window: refused, the main window untouched)
    CHECK(ed.ui_for_test().scale == main_scale);
    ed.command("window tablet on 1");
    ed.step_frame_headless();
    CHECK(std::fabs(w->ui.scale - 1.5f * main_scale) < 1e-4f && ed.ui_for_test().scale == main_scale);
    CHECK(w->ui.row_h() >= ed.ui_for_test().row_h() * 1.4f);
    ed.command("window tablet off 1");
    CHECK(std::fabs(w->ui.scale - main_scale) < 1e-4f);
    ed.command("dock move Render main");
    ed.command("window renderonly on");  // (no window of its own left: refused)
  });

  test("windows: a pen's pressure, tilt and eraser reach the Editing tab's brush: half pressure and 30 degrees of tilt make a 0.875 times wide dab, the eraser end erases", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    const float size = ed.edit_brush_radius_for_test();
    platform::Event pen = mouse_ev(platform::EventType::MouseMove, 300, 300);
    pen.pen = true, pen.pressure = 0.5f, pen.tilt_x = 30.0f, pen.tilt_y = 0.0f;
    ed.step_frame_headless({pen});
    CHECK(std::fabs(ed.edit_brush_radius_for_test() - size * 0.7f * 1.25f) < 1e-5f);
    pen.pressure = 1.0f, pen.tilt_x = 0.0f;
    ed.step_frame_headless({pen});
    CHECK(std::fabs(ed.edit_brush_radius_for_test() - size) < 1e-5f);
    platform::Event mouse = mouse_ev(platform::EventType::MouseMove, 310, 300);
    ed.step_frame_headless({mouse});
    CHECK(ed.edit_brush_radius_for_test() == size && !ed.ui_for_test().in.pen);
    pen.eraser = true;
    ed.step_frame_headless({pen});
    CHECK(ed.ui_for_test().in.eraser && ed.ui_for_test().in.pen);
  });
}

/* Task 0189: the command palette, Quick Favorites, the status bar's hints, Add Component's recent, the splitter. */
static platform::Event key_ev(int key, int mods = 0) {
  platform::Event e;
  e.type = platform::EventType::KeyDown, e.key = key, e.mods = mods;
  return e;
}

static std::vector<platform::Event> typed(const std::string &s) {
  std::vector<platform::Event> out;
  for (char c : s) {
    platform::Event e;
    e.type = platform::EventType::Text, e.codepoint = (uint32_t)c;
    out.push_back(e);
  }
  return out;
}

static void tests_round205() {
  test("command palette: Ctrl+K, typing \"hair\" puts Rig Hair from Mesh first and Enter runs it; every keymap action, window and layout is in it; the console's palette lists and runs", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    const std::vector<std::string> hair = ed.palette_names_for_test("hair");
    CHECK(!hair.empty() && hair[0] == "Rig Hair from Mesh");
    CHECK(!ed.palette_names_for_test("unwrap").empty() && ed.palette_names_for_test("unwrap")[0] == "Unwrap (LSCM)");
    CHECK(!ed.palette_names_for_test("editing window").empty() && ed.palette_names_for_test("editing window")[0] == "Editing Window");
    std::vector<std::string> all = ed.palette_names_for_test("");
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());  // (every name once: favorites key on them)
    for (const ShortcutAction &a : shortcut_actions()) CHECK(!ed.palette_names_for_test(a.label).empty());
    CHECK(ed.palette_names_for_test("zzqq nothing").empty());
    ed.step_frame_headless({key_ev(platform::KEY_K, platform::MOD_CTRL)});
    CHECK(ed.ui_for_test().any_popup_open());
    ed.step_frame_headless(typed("hair"));
    ed.step_frame_headless({key_ev(platform::KEY_ENTER)});
    ed.step_frame_headless();
    CHECK(ed.last_palette_command_for_test() == "hair rig" && !ed.ui_for_test().any_popup_open());
    CHECK(ed.palette_names_for_test("")[0] == "Rig Hair from Mesh");  // (used lately: first with no query)
    ed.command("palette run create cube");
    CHECK(ed.last_palette_command_for_test() == "create Cube" && ed.active_object_for_test() && ed.active_object_for_test()->name.find("Cube") == 0);
    ed.step_frame_headless({key_ev(platform::KEY_F3)});  // (Blender's search key opens it too)
    CHECK(ed.ui_for_test().any_popup_open());
    ed.step_frame_headless(typed("unw"));
    ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
    ed.step_frame_headless();
    CHECK(!ed.ui_for_test().any_popup_open());  // (Esc closes it, typing or not)
    ed.command("dock move Console newwindow");
    CHECK(ed.window_count_for_test() == 2);
    ed.command("palette run bring windows back");
    CHECK(ed.window_count_for_test() == 1);
  });

  test("Quick Favorites: favorite adds a command for this layout, Shift+Q lists it, it runs from there, and the preferences keep it; the status bar says what the mouse does and offers Undo; Add Component lists what was added last; the splitter's grab zone is 8 pixels", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("favorite smart uv project");  // (any case)
    ed.command("favorite No Such Command");  // (refused)
    CHECK(ed.favorites_for_test() == std::vector<std::string>{"Smart UV Project"});
    ed.step_frame_headless({key_ev(platform::KEY_Q, platform::MOD_SHIFT)});
    CHECK(ed.ui_for_test().any_popup_open());
    ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
    CHECK(!ed.ui_for_test().any_popup_open());
    ed.command("layout Animation");
    CHECK(ed.favorites_for_test().empty());  // (each layout its own)
    ed.command("layout Default");
    CHECK(ed.favorites_for_test().size() == 1);
    ed.set_scene_hovered_for_test(true);
    ed.set_tool_for_test(Tool::Move);
    const std::string move = ed.statusbar_text_for_test();
    ed.set_tool_for_test(Tool::Rotate);
    const std::string rotate = ed.statusbar_text_for_test();
    CHECK(move != rotate && move.find("Move") != std::string::npos && rotate.find("ring") != std::string::npos);
    ed.set_scene_hovered_for_test(false);
    CHECK(ed.statusbar_text_for_test().find("Ctrl+K") != std::string::npos);
    ed.command("create Cube");
    ed.step_frame_headless();
    CHECK(ed.statusbar_text_for_test().find("Last: ") != std::string::npos);
    ed.command("component Rigidbody");
    ed.command("component MirrorModifier");
    CHECK(ed.add_component_recent_for_test().size() == 2 && ed.add_component_recent_for_test()[0] == "MirrorModifier");
    ed.step_frame_headless();
    const Recti ins = ed.dock_leaf_rect_for_test(WindowKind::Inspector);  // (its left edge is a splitter in the Default layout)
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, ins.x + 3, ins.y + ins.h / 2)});  // (3 pixels inside: the old 2-pixel zone missed it)
    CHECK(ed.ui_for_test().cursor == platform::Cursor::ResizeH);
  });
}

/* Task 0190: the Inspector's essentials, the layouts' guides, Open Recent, Redo's label, a typed gizmo move. */
static void tests_round206() {
  test("Inspector essentials: a Camera shows 7 fields and Show more until opened; All Fields shows everything; the guide strip shows a layout's purpose and three steps until Got it, for good; Open Recent; Redo names the action", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.set_essentials_for_test(true);
    ed.command("create Camera");  // (a Camera: dozens of fields; a Point Light shows only a few)
    ed.step_frame_headless();
    ed.record_buttons_for_test(true);
    ed.step_frame_headless();
    const auto &br = ed.ui_for_test().button_rects;
    bool more = false;
    for (const auto &kv : br) more = more || kv.first.rfind("Show ", 0) == 0 && kv.first.find(" more") != std::string::npos;
    CHECK(more);
    ed.command("inspector essentials off");
    ed.step_frame_headless();
    bool still = false;
    for (const auto &kv : ed.ui_for_test().button_rects) still = still || kv.first.find(" more") != std::string::npos;
    CHECK(!still);
    ed.command("inspector essentials on");
    ed.set_guides_for_test(true);
    ed.command("layout UV Unwrapping");
    ed.step_frame_headless();
    CHECK(ed.ui_for_test().button_rects.count("Smart UV Project") == 1 && ed.ui_for_test().button_rects.count("Got it") == 1);
    const Recti got = ed.ui_for_test().button_rects.at("Got it");
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, got.x + 4, got.y + 4)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, got.x + 4, got.y + 4)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, got.x + 4, got.y + 4)});
    ed.step_frame_headless();
    CHECK(ed.guide_seen_for_test("UV Unwrapping") && ed.ui_for_test().button_rects.count("Got it") == 0);
    ed.command("layout Default");  // (Default has no guide: nothing drawn)
    ed.step_frame_headless();
    CHECK(ed.ui_for_test().button_rects.count("Got it") == 0);
    const std::string scene = fs::join(test_dir(), "recent.scene");
    Scene saved;
    build_default_scene(saved);
    CHECK(save_scene(saved, scene));
    CHECK(ed.open_scene_for_test(scene));
    CHECK(!ed.recent_scenes_for_test().empty() && ed.recent_scenes_for_test()[0] == scene);
    ed.command("create Sphere");
    ed.step_frame_headless();
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(!ed.redo_label_for_test().empty());  // (Edit > Redo names it: "Redo <action>")
  });

  test("a number typed while dragging a gizmo axis moves exactly that far along it; every layout's guide names commands the palette has", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    for (const char *layout : {"Reference & Planning", "Blocking Out", "Modeling & Topology", "UV Unwrapping", "Texturing & Materials", "Lighting & Rendering",
                               "Editing", "Rigging", "Skinning & Weight Painting", "Animation"})
      CHECK(!ed.palette_guide_steps_for_test(layout).empty() && ed.palette_guide_steps_for_test(layout).size() == 3);
    platform::Event typing;
    typing.type = platform::EventType::Text;
    Vec3 delta(0.3f, 0.0f, 0.0f);
    for (char c : std::string("2*1.25")) {
      typing.codepoint = (uint32_t)c;
      ed.step_frame_headless({typing});
      CHECK(ed.typed_axis_move_for_test(Vec3(1, 0, 0), delta) || c == '*');
    }
    CHECK(std::fabs(delta.x - 2.5f) < 1e-5f && delta.y == 0.0f && ed.gizmo_typed_for_test() == "2*1.25");
    // During a real arrow drag the typed keys are the number's, not shortcuts (Maya's Backspace and Delete delete)
    ed.command("feel Maya");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.select_for_test(cube->id, false);
    ed.step_frame_headless();
    int px = 0, py = 0, ax = 0, ay = 0;
    CHECK(ed.project_to_window(cube->world_position(), px, py) && ed.project_to_window(cube->world_position() + Vec3(0.3f, 0, 0), ax, ay));
    const float len = std::max(1.0f, std::hypot((float)(ax - px), (float)(ay - py)));
    int hx = -1, hy = -1;
    for (int k = 8; k < 240 && hx < 0; k += 2) {
      const int x = px + (int)((ax - px) / len * k), y = py + (int)((ay - py) / len * k);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, x, y)});
      if (ed.gizmo_hot_for_test() == 0) hx = x, hy = y;  // (H_MOVE_X)
    }
    CHECK(hx >= 0);
    const float x0 = cube->world_position().x;
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, hx, hy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, hx + 6, hy)});
    CHECK(ed.gizmo_dragging_for_test());
    typing.codepoint = '2';
    ed.step_frame_headless({typing, key_ev(platform::KEY_2)});
    CHECK(ed.gizmo_typed_for_test() == "2" && std::fabs(cube->world_position().x - x0 - 2.0f) < 1e-4f);
    ed.step_frame_headless({key_ev(platform::KEY_BACKSPACE), key_ev(platform::KEY_DELETE)});
    CHECK(ed.gizmo_typed_for_test().empty());
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, hx + 6, hy)});
    CHECK(ed.scene().find_by_name("Cube") != nullptr);
  });
}

/* ---------------------------------------------------------------- round 207: undo without redrawing what didn't change (task 0185) */
static void tests_round207() {
  test("undo of a colour change keeps the shadow map: the restored scene's materials are copies, and a colour doesn't cast", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.set_render_cache_verify(false);  // (it counts renders: verify mode would re-render every hit)
    ed.viewport_device_for_test(1);      // (the CPU views keep the map this counts; the GPU's draws its own)
    ed.command("layout 2 by 3");
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube != nullptr);
    if (!cube) return;
    MaterialPtr mat = std::make_shared<Material>();  // (the scene's own material: a snapshot copies it)
    cube->get<MeshRenderer>()->materials = {mat};
    ed.mark_changed_for_test("Material");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    mat->base_color = Vec3(0.9f, 0.1f, 0.1f);
    ed.mark_changed_for_test("Colour");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const FrameProfile p0 = ed.frame_profile_totals();
    CHECK(p0.shadow_renders >= 1);  // (the sun has a map to keep)
    ed.command("undo");
    ed.step_frame_headless();
    const FrameProfile &p1 = ed.frame_profile_totals();
    const GameObject *restored = ed.scene().find_by_name("Cube");
    const MeshRenderer *mr = restored ? restored->get<MeshRenderer>() : nullptr;
    const bool copied = mr && !mr->materials.empty() && mr->materials[0];
    CHECK(copied && mr->materials[0].get() != mat.get());  // (a copy: what the key used to miss on)
    CHECK(copied && length(mr->materials[0]->base_color - Material().base_color) < 1e-6f);  // (the colour went back)
    CHECK(p1.view_renders > p0.view_renders);  // (the views did draw the undo: the map was there to reuse)
    CHECK(p1.shadow_renders == p0.shadow_renders);
  });
}

/* ---------------------------------------------------------------- round 209: entering Edit Mode draws what shows it (task 0185) */
static void tests_round209() {
  test("entering and leaving Edit Mode redraws the Scene view only: the Game view's picture and the scene's render hash don't change", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.set_render_cache_verify(false);  // (it counts renders: verify mode would re-render every hit)
    ed.viewport_device_for_test(1);
    ed.command("layout 2 by 3");
    ed.command("select Cube");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const uint64_t h0 = ed.scene_render_hash_for_test();
    const FrameProfile p0 = ed.frame_profile_totals();
    ed.command("edit vertex");
    ed.step_frame_headless();
    const FrameProfile p1 = ed.frame_profile_totals();
    CHECK(ed.scene_render_hash_for_test() == h0);
    CHECK(p1.view_renders - p0.view_renders == 1);  // (the Scene view, with its cage)
    ed.command("edit off");
    ed.step_frame_headless();
    CHECK(ed.frame_profile_totals().view_renders - p1.view_renders == 1);
    /* And no picture kept is wrong: verify mode draws every cached picture again and compares. */
    ed.set_render_cache_verify(true);
    const uint64_t m0 = Editor::render_cache_mismatches_all();
    for (const char *c : {"edit vertex", "edit off", "edit face", "edit off"}) {
      ed.command(c);
      ed.step_frame_headless();
    }
    CHECK(Editor::render_cache_mismatches_all() == m0);
  });
}

/* ---------------------------------------------------------------- round 222: the parity scenes (task 0202) */
static void tests_round222() {
  test("benchscene builds the scenes Blender's viewport is timed on: the 50k sphere selected, ten spheres sharing one mesh, 10,000 cubes, 1,000 keyed boxes that move; an unknown name builds nothing", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("benchscene sphere50k");
    GameObject *s = ed.scene().find_by_name("Parity Sphere");
    CHECK(s && s->get<MeshFilter>() && s->get<MeshFilter>()->mesh->face_count() == 256u * 196u);
    CHECK(ed.selected_object() == s);
    CHECK(!ed.scene().find_by_name("Utah Teapot") && !ed.scene().find_by_name("Main Camera"));  // (the starter scene emptied but for its light)
    ed.command("benchscene million");
    size_t spheres = 0;
    const Mesh *shared = nullptr;
    bool one_mesh = true;
    ed.scene().for_each([&](GameObject &g) {
      if (g.name.rfind("Sphere ", 0) != 0 || !g.get<MeshFilter>()) return;
      spheres++;
      if (shared && shared != g.get<MeshFilter>()->mesh.get()) one_mesh = false;
      shared = g.get<MeshFilter>()->mesh.get();
    });
    CHECK(spheres == 10 && one_mesh);
    CHECK(!ed.scene().find_by_name("Parity Sphere"));  // (a new scene each time)
    ed.command("benchscene cubes10k");
    size_t cubes = 0;
    ed.scene().for_each([&](GameObject &g) { cubes += g.name.rfind("Cube ", 0) == 0; });
    CHECK(cubes == 10000);
    ed.command("benchscene keyed1k");
    GameObject *box = ed.scene().find_by_name("Box 3");
    AnimationClipPtr clip = ed.anim_clip_for_test();
    CHECK(box && clip && clip->tracks.size() == 3000);
    if (!box || !clip) return;
    const float y1 = box->local().position.y;
    GameObject *root = ed.scene().find_by_name("Keyed");
    apply_clip(*clip, *root, 30.0f);
    std::printf("    Box 3 at frame 1: y %.2f, at frame 30: y %.2f\n", y1, box->local().position.y);
    CHECK(std::fabs(box->local().position.y - (y1 + 3.0f)) < 1e-4f);  // (box k rises k % 7 by frame 30)
    const size_t objects = ed.scene().object_count();
    ed.command("benchscene nothing");
    CHECK(ed.scene().object_count() == objects);
  });
}

/* ---------------------------------------------------------------- round 227: duplicating many objects (task 0207) */
static void tests_round227() {
  test("Duplicate on many objects names each copy as one at a time did - the smallest free number, a name already taken skipped - and puts it right after its original; 2,000 under one parent come out with 2,000 new names", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("newscene");
    Scene &s = ed.scene();
    GameObject *parent = s.create("Group");
    GameObject *a = s.create("Cube", parent), *b = s.create("Cube (2)", parent), *c = s.create("Cube", parent);
    ed.mark_changed_for_test("Cubes");
    ed.step_frame_headless();
    const std::vector<GameObject *> made = s.duplicate_many({a, b, c});
    std::string order;
    for (GameObject *g : parent->children) order += (order.empty() ? "" : ", ") + g->name;
    std::printf("    %s\n", order.c_str());
    CHECK(made.size() == 3 && made[0]->name == "Cube (1)" && made[1]->name == "Cube (3)" && made[2]->name == "Cube (4)");
    CHECK(parent->children.size() == 6 && parent->children[1] == made[0] && parent->children[3] == made[1] && parent->children[5] == made[2]);
    /* Through the editor: 2,000 selected, duplicated in one go. */
    ed.command("newscene");
    ed.command("stress 2000 Cube");
    ed.command("selectall");
    ScopedTimer t;
    ed.command("duplicate");
    const double ms = t.ms();
    GameObject *root = nullptr;
    ed.scene().for_each([&](GameObject &g) { if (!root && g.children.size() > 100) root = &g; });
    std::set<std::string> seen;
    size_t copies = 0;
    if (root)
      for (GameObject *g : root->children) {
        seen.insert(g->name);
        copies += g->name.find(" (") != std::string::npos;
      }
    std::printf("    2,000 duplicated in %.1f ms: %zu children, %zu names, %zu copies named \"Cube (n)\"\n", ms, root ? root->children.size() : 0,
                seen.size(), copies);
    CHECK(root && root->children.size() == 4000 && copies == 2000 && seen.size() == 2001);  // ("Cube" and 2,000 numbered)
    const std::vector<GameObject *> sel = ed.selected_objects_for_test();  // (the copies: the 2,000 cubes and the starter scene's two)
    CHECK(sel.size() >= 2000 && std::all_of(sel.begin(), sel.end(), [](GameObject *g) { return g->name.find(" (") != std::string::npos; }));
    bool paired = root != nullptr;  // (each copy right after its original: originals at even places)
    for (size_t i = 0; root && i + 1 < root->children.size(); i += 2)
      paired = paired && root->children[i]->name == "Cube" && root->children[i + 1]->name.find("Cube (") == 0;
    CHECK(paired);
    /* The same object twice: the later copy goes right after it, as one at a time put it. */
    ed.command("newscene");
    GameObject *one = ed.scene().create("Lamp");
    const std::vector<GameObject *> twice = ed.scene().duplicate_many({one, one});
    order.clear();
    for (GameObject *g : ed.scene().roots) order += (order.empty() ? "" : ", ") + g->name;
    std::printf("    the same object twice: %s\n", order.c_str());
    CHECK(twice.size() == 2 && twice[0]->name == "Lamp (1)" && twice[1]->name == "Lamp (2)");
    auto at = std::find(ed.scene().roots.begin(), ed.scene().roots.end(), one);
    CHECK(at + 2 < ed.scene().roots.end() && *(at + 1) == twice[1] && *(at + 2) == twice[0]);
  });
}

/* Task 0232 (ADR 0070): a kind once per window, the view state per window, drops onto other windows, duplicates, the
 * Inspector's pin. */
static void tests_round252() {
  using platform::EventType;
  test("windows: a tab dragged out of the main window and dropped over another editor window joins that window's dock (the main one no longer has it); dock move <Window> window <n> does the same from the console", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const int id = ed.open_window_for_test(WindowKind::Render, 600, 500);
    EditorWindow *w = ed.window_for_test(id);
    CHECK(w != nullptr);
    if (!w) return;
    w->x = 1300, w->y = 100;  // (beside the main window, not over it)
    ed.step_window_headless(id, {});
    ed.step_frame_headless();
    const Recti leaf = ed.dock_leaf_rect_for_test(WindowKind::Inspector);
    CHECK(leaf.w > 0);
    const int tx = leaf.x + 12, ty = leaf.y + 6;
    ed.step_frame_headless({mouse_ev(EventType::MouseMove, tx, ty)});
    ed.step_frame_headless({mouse_ev(EventType::MouseDown, tx, ty)});
    ed.step_frame_headless({mouse_ev(EventType::MouseMove, tx + 300, ty + 40)});
    /* Over the other window's middle (1300 + 300, 100 + 300 on the desktop = the main window's 1600, 400). */
    ed.step_frame_headless({mouse_ev(EventType::MouseMove, 1600, 400)});
    ed.step_frame_headless({mouse_ev(EventType::MouseUp, 1600, 400)});
    ed.step_frame_headless();
    ed.step_frame_headless();
    std::printf("    windows %zu; the Inspector in window %d: %d, in the main dock: %d\n", ed.window_count_for_test(), id,
                ed.in_window_dock_for_test(id, WindowKind::Inspector), ed.in_main_dock_for_test(WindowKind::Inspector));
    CHECK(ed.window_count_for_test() == 2 && ed.in_window_dock_for_test(id, WindowKind::Inspector) && !ed.in_main_dock_for_test(WindowKind::Inspector));
    ed.command(strprintf("dock move Hierarchy window %d", id));
    ed.step_frame_headless();
    CHECK(ed.in_window_dock_for_test(id, WindowKind::Hierarchy) && !ed.in_main_dock_for_test(WindowKind::Hierarchy));
    ed.command("dock move Hierarchy main");
    ed.step_frame_headless();
    CHECK(ed.in_main_dock_for_test(WindowKind::Hierarchy) && !ed.in_window_dock_for_test(id, WindowKind::Hierarchy));
  });

  test("windows: dock duplicate Scene opens a second Scene view in a window of its own with a camera of its own (an orbit there leaves the main one's alone), the main dock keeps its Scene view, a layout reset keeps both, and the cameras round-trip the windows preference", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const float yaw0 = ed.scene_camera_for_test().yaw;
    ed.command("dock duplicate Scene");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 2 && ed.in_main_dock_for_test(WindowKind::Scene) && ed.in_window_dock_for_test(1, WindowKind::Scene));
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w != nullptr);
    if (!w) return;
    ed.step_window_headless(1, {});
    const SceneCamera *cam = ed.window_camera_for_test(1);
    CHECK(cam && std::fabs(cam->yaw - yaw0) < 1e-4f);
    /* Alt+LMB drag in the second window orbits it (Unity's navigation). */
    const int cx = w->fb.width / 2, cy = w->fb.height / 2;
    platform::Event down = mouse_ev(EventType::MouseDown, cx, cy);
    down.mods = platform::MOD_ALT;
    ed.step_window_headless(1, {mouse_ev(EventType::MouseMove, cx, cy)});
    ed.step_window_headless(1, {down});
    for (int i = 1; i <= 10; i++) {
      platform::Event mv = mouse_ev(EventType::MouseMove, cx + 12 * i, cy);
      mv.mods = platform::MOD_ALT;
      ed.step_window_headless(1, {mv});
    }
    platform::Event up = mouse_ev(EventType::MouseUp, cx + 120, cy);
    up.mods = platform::MOD_ALT;
    ed.step_window_headless(1, {up});
    ed.step_frame_headless();
    cam = ed.window_camera_for_test(1);
    std::printf("    the main camera's yaw %.1f, the second window's %.1f\n", ed.scene_camera_for_test().yaw, cam ? cam->yaw : 0.0f);
    CHECK(cam && std::fabs(cam->yaw - yaw0) > 5.0f && std::fabs(ed.scene_camera_for_test().yaw - yaw0) < 1e-4f);
    ed.command("layout Default");
    ed.step_frame_headless();
    CHECK(ed.in_main_dock_for_test(WindowKind::Scene) && ed.in_window_dock_for_test(1, WindowKind::Scene));
    /* The windows preference carries the second window's camera. */
    const std::string saved = ed.windows_layout_for_test();
    CHECK(saved.find(";V(") != std::string::npos);
    Editor ed2;
    ed2.init_headless(1200, 800);
    ed2.step_frame_headless();
    ed2.set_monitors_for_test({{0, 0, 3000, 2000, 1.0f, "Test", true}});
    ed2.restore_windows_for_test(saved);
    ed2.step_frame_headless();
    const SceneCamera *cam2 = ed2.window_camera_for_test(1);
    CHECK(ed2.window_count_for_test() == 2 && ed2.in_main_dock_for_test(WindowKind::Scene) && cam2 && std::fabs(cam2->yaw - cam->yaw) < 1e-3f);
  });

  test("windows: an Inspector pinned to an object keeps showing it while the selection moves on, and follows again when unpinned (the pin is the window's own)", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("create Cube");
    ed.step_frame_headless();
    CHECK(ed.selected_objects_for_test().size() == 1);
    const uint64_t cube = ed.selected_objects_for_test().empty() ? 0 : ed.selected_objects_for_test()[0]->id;  // (the new one: the sample scene has a Cube too)
    ed.command("inspector lock");
    CHECK(ed.inspector_lock_for_test() == cube && cube != 0);
    ed.command("create Sphere");
    ed.step_frame_headless();
    CHECK(ed.inspector_lock_for_test() == cube && !ed.selected_objects_for_test().empty() && ed.selected_objects_for_test()[0]->id != cube);
    ed.command("inspector unlock");
    CHECK(ed.inspector_lock_for_test() == 0);
    /* A second Inspector in its own window has a pin of its own. */
    ed.command("dock duplicate Inspector");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 2 && ed.in_main_dock_for_test(WindowKind::Inspector) && ed.in_window_dock_for_test(1, WindowKind::Inspector));
    ed.command("inspector lock");  // (the main window's: the console runs with the main state)
    CHECK(ed.inspector_lock_for_test() != 0);
    ed.step_window_headless(1, {});
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w && w->view && w->view->inspector_lock == 0);
    ed.command("inspector unlock");
  });

  test("windows: a pen stroke begun in a duplicate Scene view goes on while the main window's Scene view draws (the interaction is the window's), and lands as one stroke; a duplicate Game view opens; closing the windows frees their caches and returns the tabs", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("dock duplicate Scene");
    ed.step_frame_headless();
    EditorWindow *w = ed.window_for_test(1);
    CHECK(w != nullptr);
    if (!w) return;
    ed.step_window_headless(1, {});
    ed.command("sketch on");
    const size_t strokes0 = ed.sketch_for_test().strokes.size();
    auto pen = [](EventType t, int x, int y) {
      platform::Event e = mouse_ev(t, x, y);
      e.pointer = platform::Pointer::Pen, e.pen = true, e.pressure = 0.8f;
      return e;
    };
    const int sx = w->fb.width / 2, sy = w->fb.height / 2;
    ed.step_window_headless(1, {pen(EventType::MouseMove, sx, sy)});
    ed.step_window_headless(1, {pen(EventType::MouseDown, sx, sy)});
    for (int i = 1; i <= 8; i++) {
      ed.step_window_headless(1, {pen(EventType::MouseMove, sx + 10 * i, sy + 4 * i)});  // (each window frame runs the main frame too)
      CHECK(ed.sketch_for_test().strokes.size() == strokes0);  // (the main window's Scene view doesn't end it)
      CHECK(ed.interaction_window_for_test() == 1);
    }
    ed.step_window_headless(1, {pen(EventType::MouseUp, sx + 90, sy + 36)});
    ed.step_frame_headless();
    std::printf("    strokes %zu -> %zu\n", strokes0, ed.sketch_for_test().strokes.size());
    CHECK(ed.sketch_for_test().strokes.size() == strokes0 + 1 && ed.interaction_window_for_test() == -1);
    ed.command("sketch off");
    ed.command("dock duplicate Game");
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 3 && ed.in_main_dock_for_test(WindowKind::Game) && ed.in_window_dock_for_test(2, WindowKind::Game));
    CHECK(ed.window_caches_for_test() >= 1);
    ed.command("dock bringback");
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(ed.window_count_for_test() == 1 && ed.window_caches_for_test() == 0 && ed.in_main_dock_for_test(WindowKind::Scene) && ed.in_main_dock_for_test(WindowKind::Game));
  });

  test("windows: a duplicate moved to another window takes its own instance (the main window keeps its Scene view), and a kind the target already holds is shown there", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("dock duplicate Scene");
    ed.step_frame_headless();
    const int other = ed.open_window_for_test(WindowKind::Render, 600, 500);
    ed.step_frame_headless();
    CHECK(other == 2 && ed.in_window_dock_for_test(1, WindowKind::Scene));
    WindowMove m;
    m.kind = WindowMove::ToWindow, m.tab = WindowKind::Scene, m.window_id = other, m.x = 300, m.y = 300, m.source = 1;
    ed.queue_window_move_for_test(m);
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(ed.in_main_dock_for_test(WindowKind::Scene) && ed.in_window_dock_for_test(other, WindowKind::Scene) && ed.window_for_test(1) == nullptr);
    /* The main window's Scene view moved onto a window that has one: shown there, the main one gone. */
    ed.command(strprintf("dock move Scene window %d", other));
    ed.step_frame_headless();
    CHECK(!ed.in_main_dock_for_test(WindowKind::Scene) && ed.in_window_dock_for_test(other, WindowKind::Scene));
    ed.command("dock bringback");
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(ed.in_main_dock_for_test(WindowKind::Scene));
  });
}

/* Task 0235 (ADR 0073): the module registry; tabs that fit, fold and follow the stage; the breadcrumb; the Inspector's
 * fold toggle; the layout guide after two sessions. */
static void tests_round255() {
  using platform::EventType;
  auto step = [](Editor &ed, std::vector<platform::Event> events = {}) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  };
  auto mouse = [](EventType t, int x, int y) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y;
    return e;
  };
  auto click = [&](Editor &ed, const Recti r) {  // (by value: button_rects is rebuilt every frame, so a reference into it dangles after the first step)
    step(ed, {mouse(EventType::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed, {mouse(EventType::MouseDown, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed, {mouse(EventType::MouseUp, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed);
  };
  /* A click that must take: pressed again (up to three times) until `done` says so - a frame may be drawn without the
   * widget under the pointer having been laid out yet. */
  auto click_until = [&](Editor &ed, const std::string &button, std::function<bool()> done) {
    for (int attempt = 0; attempt < 3 && !done(); attempt++) {
      const auto &br = ed.ui_for_test().button_rects;
      if (!br.count(button)) {
        step(ed);
        continue;
      }
      const Recti r = br.at(button);
      click(ed, r);
      if (!done()) std::printf("    (the click on %s at (%d,%d %dx%d) didn't take on attempt %d)\n", button.c_str(), r.x, r.y, r.w, r.h, attempt + 1);
    }
    return done();
  };
  auto inside = [](const Recti &r, const Recti &leaf) { return r.w > 0 && r.x >= leaf.x && r.right() <= leaf.right(); };
  const WindowKind six[6] = {WindowKind::Materials, WindowKind::Textures, WindowKind::Lighting, WindowKind::Rigging, WindowKind::Animation, WindowKind::Sequencer};

  test("the registry: a module per window kind, each title unique; the titles and icons the editor shows are the registry's", [] {
    int n = 0;
    const Module *m = modules(n);
    CHECK(n == (int)WindowKind::Count);
    std::set<std::string> names;
    for (int i = 0; i < n; i++) {
      CHECK((int)m[i].kind == i && std::string(window_title(m[i].kind)) == m[i].name && window_icon(m[i].kind) == m[i].icon);
      names.insert(m[i].name);
    }
    CHECK((int)names.size() == n);
    CHECK(module_of(WindowKind::Materials).folds && std::string(module_of(WindowKind::Materials).group) == "Shading" && !module_of(WindowKind::Scene).folds && module_of(WindowKind::Scene).duplicates && !module_of(WindowKind::Layers).duplicates);
  });

  test("tabs that fit: a 600-px leaf with six tabs shows all six inside the strip (icons for the inactive ones), a 170-px leaf shows the active one and lists the rest in the overflow", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.record_buttons_for_test(true);
    ed.command("tabs fold off");
    ed.command("layout H(0.43,L(11,14,13,15,16,17:0),L(0:0))");
    step(ed);
    step(ed);
    const Recti leaf = ed.dock_leaf_rect_for_test(WindowKind::Materials);
    std::printf("    the leaf %d px wide\n", leaf.w);
    CHECK(leaf.w > 500 && leaf.w < 700 && ed.tab_overflow_for_test().empty());
    int right_most = 0;
    for (WindowKind k : six) {
      const Recti r = ed.tab_rect_for_test(k);
      CHECK(inside(r, leaf) && r.x >= right_most);  // (in order, none over another)
      right_most = r.right();
    }
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).w > ed.tab_rect_for_test(WindowKind::Textures).w);  // (the active one keeps its label)
    ed.command("layout H(0.12,L(11,14,13,15,16,17:0),L(0:0))");
    step(ed);
    step(ed);
    const Recti narrow = ed.dock_leaf_rect_for_test(WindowKind::Materials);
    const std::vector<WindowKind> over = ed.tab_overflow_for_test();
    std::printf("    the narrow leaf %d px wide; %zu tabs in the overflow\n", narrow.w, over.size());
    CHECK(narrow.w < 200 && !over.empty() && inside(ed.tab_rect_for_test(WindowKind::Materials), narrow));
    for (WindowKind k : six) {
      const bool overflowed = std::find(over.begin(), over.end(), k) != over.end();
      CHECK(overflowed ? ed.tab_rect_for_test(k).w == 0 : inside(ed.tab_rect_for_test(k), narrow));
    }
    ed.command("tabs fold on");
  });

  test("tab groups: Materials, Textures and Lighting in one leaf show as one Shading tab with a row of the three under it; a member pressed becomes the leaf's active tab; Fold Groups off shows three tabs", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.record_buttons_for_test(true);
    ed.command("layout H(0.5,L(11,14,13:0),L(0:0))");
    step(ed);
    step(ed);
    CHECK(ed.tab_label_for_test(WindowKind::Materials) == "Shading" && ed.tab_label_for_test(WindowKind::Lighting) == "Shading");
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).x == ed.tab_rect_for_test(WindowKind::Textures).x && ed.window_rect_for_test(WindowKind::Materials).w > 0);
    const auto &br = ed.ui_for_test().button_rects;
    CHECK(br.count("Textures") && br.count("Lighting"));
    CHECK(click_until(ed, "Textures", [&] { return ed.window_rect_for_test(WindowKind::Textures).w > 0; }));
    CHECK(ed.tab_label_for_test(WindowKind::Textures) == "Shading");
    ed.command("tabs fold off");
    step(ed);
    CHECK(ed.tab_label_for_test(WindowKind::Materials) == "Materials" && ed.tab_rect_for_test(WindowKind::Materials).x != ed.tab_rect_for_test(WindowKind::Textures).x);
    ed.command("tabs fold on");
    ed.record_buttons_for_test(false);
  });

  test("a stage's tabs: the Rigging layout hides a Materials tab (listed under the strip's +1) unless it was asked for or Show All Tabs is on; the Default layout hides nothing", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.record_buttons_for_test(true);
    ed.command("layout H(0.72,V(0.74,H(0.22,L(2:0),L(0,1:0)),L(4,5:0)),L(11,15,3:0))");  // (the Rigging layout with a Materials tab kept in the Inspector's leaf)
    ed.set_focus_for_test(Editor::FocusCreate | Editor::FocusObject | Editor::FocusRig | Editor::FocusDeform);
    step(ed);
    step(ed);
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).w == 0 && ed.tab_rect_for_test(WindowKind::Rigging).w > 0 && ed.window_rect_for_test(WindowKind::Rigging).w > 0);  // (the leaf shows Rigging; a hidden tab keeps last frame's window rect)
    CHECK(ed.tab_hidden_for_test() == std::vector<WindowKind>{WindowKind::Materials} && ed.ui_for_test().button_rects.count("+1"));  // (the strip says so)
    ed.command("window Materials");  // (asked for: shown whatever the stage)
    step(ed);
    step(ed);
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).w > 0 && ed.window_rect_for_test(WindowKind::Materials).w > 0);
    ed.forget_opened_tabs_for_test();  // (the next session)
    step(ed);
    step(ed);
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).w == 0 && ed.tab_rect_for_test(WindowKind::Rigging).w > 0 && ed.window_rect_for_test(WindowKind::Rigging).w > 0);  // (the leaf shows Rigging; a hidden tab keeps last frame's window rect)
    ed.command("tabs all on");
    step(ed);
    CHECK(ed.tab_rect_for_test(WindowKind::Materials).w > 0);
    ed.command("tabs all off");
    ed.command("layout Default");
    step(ed);
    step(ed);
    for (WindowKind k : {WindowKind::Scene, WindowKind::Materials, WindowKind::Render, WindowKind::Inspector}) CHECK(ed.tab_rect_for_test(k).w > 0);
  });

  test("the breadcrumb reads layout > mode > tool and `where` prints it; the Inspector's Fold folds every component and Unfold opens them, the folds in the preferences; a layout picked in two sessions hides its guide and Reset Guides brings it back", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.record_buttons_for_test(true);
    ed.command("select Cube");
    ed.command("layout Modeling & Topology");
    ed.command("edit");
    ed.command("tool move");
    step(ed);
    std::printf("    %s\n", ed.breadcrumb_for_test().c_str());
    CHECK(ed.breadcrumb_for_test() == "Modeling & Topology > Edit > Move");
    ed.command("where");
    ed.command("edit off");
    ed.command("layout Default");
    ed.command("select Cube");
    step(ed);
    step(ed);
    const auto &br = ed.ui_for_test().button_rects;
    CHECK(br.count("Fold") && ed.folded_components_for_test() == 0);
    CHECK(click_until(ed, "Fold", [&] { return ed.folded_components_for_test() > 0; }));  // (the header's button)
    const int folded = ed.folded_components_for_test();
    CHECK(click_until(ed, "Unfold", [&] { return ed.folded_components_for_test() == 0; }));
    ed.command("inspector fold");  // (the same from the console)
    step(ed);
    std::printf("    %d components folded; %s\n", folded, ed.full_prefs_text_for_test().c_str());
    CHECK(folded >= 2 && br.count("Unfold") && ed.full_prefs_text_for_test().find("fold=") != std::string::npos && ed.full_prefs_text_for_test().find("MeshFilter0") != std::string::npos);  // (the keys' order is the map's: it differs between platforms)
    ed.command("inspector unfold");
    step(ed);
    CHECK(ed.folded_components_for_test() == 0);
    /* The guide: one use a session however many times the layout is picked; hidden from the third session. */
    ed.command("layoutguide reset");  // (an earlier test may have dismissed it in the shared preferences)
    ed.command("layout Rigging");
    ed.command("layout Default");
    ed.command("layout Rigging");
    std::printf("    Rigging uses %d, hidden %d\n", ed.guide_uses_for_test("Rigging"), (int)ed.layout_guide_hidden_for_test());
    CHECK(ed.guide_uses_for_test("Rigging") == 1 && !ed.layout_guide_hidden_for_test() && ed.full_prefs_text_for_test().find("Rigging:1") != std::string::npos);
    ed.set_guide_uses_for_test("Rigging", 3);
    CHECK(ed.layout_guide_hidden_for_test());
    ed.command("layoutguide reset");
    CHECK(!ed.layout_guide_hidden_for_test() && ed.guide_uses_for_test("Rigging") == 0);
    CHECK(Editor::component_fold_key("MeshRenderer1") && Editor::component_fold_key("ModifierStack") && !Editor::component_fold_key("proj_C:/x") && !Editor::component_fold_key("mat_0x1F") && !Editor::component_fold_key("grp_0x1Fname"));
    ed.record_buttons_for_test(false);
  });
}

static void tests_round263() {
  test("mesh maps in the editor: the bake runs on a worker (the slowest frame printed), lands as five PNGs and a record on the Mesh Renderer in one undo step, undo clears the record; a mesh changed meanwhile drops the result; cancel; the record round-trips through the scene file and reads stale after a change", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_clock_for_test(100.0);
    ed.command("select Sphere");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    for (int i = 0; i < 10; i++) ed.step_frame_headless();  // (the cold frames: the render mesh, the pictures)
    int slowest_at = -1;
    auto wait = [&](double &slowest) {
      slowest = 0.0;
      for (int i = 0; i < 20000 && ed.meshmaps_busy_for_test(); i++) {
        const auto t0 = std::chrono::steady_clock::now();
        ed.step_frame_headless();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > slowest) slowest = ms, slowest_at = i;
        if (ed.meshmaps_busy_for_test()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return !ed.meshmaps_busy_for_test();
    };
    const size_t steps0 = ed.undo_steps_for_test();
    const std::vector<Vec2> uvs0 = G()->mesh_filter()->mesh->uvs;
    const bool own_usable = own_uvs_usable(*G()->mesh_filter()->mesh, 256);
    ed.command("meshmaps 256 8");
    CHECK(ed.meshmaps_busy_for_test());
    double slowest = 0.0;
    CHECK(wait(slowest));
    ed.command("meshmaps status");  // (commits the pending step)
    const MeshRenderer *mr = G()->get<MeshRenderer>();
    const std::string rel = mr ? mr->mesh_maps : "";
    auto files_of = [&](const std::string &base) {
      bool all = !base.empty();
      for (const char *suffix : {"_normal.png", "_position.png", "_curvature.png", "_ao.png", "_thickness.png"}) all = all && fs::exists(ed.resolve_asset_path_for_test(base + suffix));
      return all;
    };
    auto png_count = [&] {
      size_t n = 0;
      for (const DirEntry &e : fs::list(ed.resolve_asset_path_for_test("Assets/Textures/MeshMaps")))
        if (!e.is_dir && e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".png") == 0) n++;
      return n;
    };
    const bool files = files_of(rel);
    /* Usable own UVs are kept byte for byte (a textured object keeps its look); else the generated layout became the
     * mesh's UVs: one per corner, inside 0..1. */
    const std::vector<Vec2> &uvs1 = G()->mesh_filter()->mesh->uvs;
    bool uvs_ok = uvs1.size() == G()->mesh_filter()->mesh->corner_verts.size();
    for (const Vec2 &uv : uvs1) uvs_ok = uvs_ok && uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
    const bool kept = uvs1.size() == uvs0.size() && std::memcmp(uvs1.data(), uvs0.data(), uvs0.size() * sizeof(Vec2)) == 0;
    std::printf("    the sphere's own UVs usable %d, kept byte for byte %d\n", (int)own_usable, (int)kept);
    CHECK(uvs_ok && kept == own_usable);
    std::printf("    mesh maps: record '%s', the five files %d, %zu step(s), the slowest frame while they baked %.1f ms (frame %d; the last one lands the record), stale %d\n", rel.c_str(), (int)files,
                ed.undo_steps_for_test() - steps0, slowest, slowest_at, (int)ed.mesh_maps_stale_for_test(G()));
    CHECK(files && ed.undo_steps_for_test() == steps0 + 1 && !ed.mesh_maps_stale_for_test(G()));
    /* The file round trip. */
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    const GameObject *gb = back.find(gid);
    const MeshRenderer *mrb = gb ? gb->get<MeshRenderer>() : nullptr;
    CHECK(mrb && mrb->mesh_maps == rel && mrb->mesh_maps_key == mr->mesh_maps_key);
    /* The loaded object's mesh keys the same: the record reads current after a reopen (a hash, not a session count). */
    CHECK(gb && gb->mesh_filter() && gb->mesh_filter()->mesh && mesh_maps_key_text(*gb->mesh_filter()->mesh, gb->world_matrix()) == mrb->mesh_maps_key);
    /* Shade Smooth / Flat and the object's transform are in the key: the normal map and the world reads change. */
    {
      MeshPtr &mp = G()->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      mp->smooth = !mp->smooth;
      mp->touch();
      ed.mark_changed_for_test("shading");
    }
    CHECK(ed.mesh_maps_stale_for_test(G()));
    ed.command("undo");
    CHECK(!ed.mesh_maps_stale_for_test(G()));
    const Mat4 before_turn = G()->world_matrix();
    G()->set_local_euler(Vec3(0.0f, 90.0f, 0.0f)), ed.mark_changed_for_test("a turn");
    ed.step_frame_headless();
    const bool turned = std::memcmp(before_turn.m, G()->world_matrix().m, sizeof before_turn.m) != 0;
    const bool stale_turned = ed.mesh_maps_stale_for_test(G());
    ed.command("undo");
    std::printf("    the turn changed the world matrix %d; stale after it %d, after its undo %d\n", (int)turned, (int)stale_turned, (int)ed.mesh_maps_stale_for_test(G()));
    CHECK(stale_turned && !ed.mesh_maps_stale_for_test(G()));
    /* Stale after the mesh moves; undo clears the record. */
    ed.command("sculpt on");
    ed.command("sculpt off");
    {
      MeshPtr &mp = G()->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      mp->positions[0] += Vec3(0.0f, 0.01f, 0.0f);
      mp->deform_version++;
      ed.mark_changed_for_test("a nudge");
    }
    CHECK(ed.mesh_maps_stale_for_test(G()));
    ed.command("undo");  // (the nudge)
    ed.command("undo");  // (the maps' record)
    CHECK(G()->get<MeshRenderer>()->mesh_maps.empty() && files_of(rel));  // (the files stay, as Bake Textures' do)
    /* A change while it bakes drops the result and its files; the first bake's files survive; a cancel writes nothing. */
    const size_t pngs = png_count();
    ed.command("meshmaps 256 8");
    {
      MeshPtr &mp = G()->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      mp->positions[0] += Vec3(0.0f, 0.01f, 0.0f);
      mp->deform_version++;
      ed.mark_changed_for_test("a nudge meanwhile");
    }
    CHECK(wait(slowest));
    std::printf("    after a bake dropped for a change meanwhile: %zu PNGs (%zu before), the first bake's files %d\n", png_count(), pngs, (int)files_of(rel));
    CHECK(G()->get<MeshRenderer>()->mesh_maps.empty() && png_count() == pngs && files_of(rel));
    ed.command("meshmaps 256 8");
    ed.command("meshmaps cancel");
    CHECK(wait(slowest));
    CHECK(G()->get<MeshRenderer>()->mesh_maps.empty() && png_count() == pngs);
  });
}

static const Material &G_material(const GameObject *g) {
  static Material none;
  const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
  return mr && !mr->materials.empty() && mr->materials[0] ? *mr->materials[0] : none;
}

static void tests_round264() {
  test("a surface in the editor: surface new makes a surface asset on the active object's material (albedo, normal, roughness, metallic, emission maps), add / mask preset / set / group edit it with undo, and a changed stack evaluates on a worker while the frames go on (the slowest printed)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };  // (an undo rebuilds the scene: the pointer must be looked up again)
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    const size_t steps_before_new = ed.undo_steps_for_test();
    ed.command("surface new TestSurface");
    const MeshRenderer *mr = g->get<MeshRenderer>();
    CHECK(mr && !mr->materials.empty() && mr->materials[0]);
    if (!mr || mr->materials.empty() || !mr->materials[0]) return;
    const Material &m = *mr->materials[0];
    std::string asset;
    int out = -1;
    /* Four maps and Metallic 1 (the map multiplies it); no emission map until a layer writes emission (an emission map
     * makes the object a light in the path tracer). */
    const bool maps = split_texture_ref(m.base_map.path, asset, out) && out == 0 && split_texture_ref(m.metallic_map.path, asset, out) && out == (int)TextureOutput::Metallic &&
                      m.emission_map.path.empty() && !m.normal_map.path.empty() && !m.roughness_map.path.empty() && m.metallic == 1.0f;
    ProceduralTexturePtr pt = texture_asset(asset);
    std::printf("    surface new: %s, surface %d, %zu layer(s), the material's four maps and Metallic 1 %d, no emission map %d\n", asset.c_str(), pt ? (int)pt->surface : -1,
                pt ? pt->layers.size() : (size_t)0, (int)maps, (int)m.emission_map.path.empty());
    CHECK(pt && pt->surface && pt->layers.size() == 1 && maps);
    if (!pt) return;
    ed.step_frame_headless();
    std::printf("    surface new made %zu step(s)\n", ed.undo_steps_for_test() - steps_before_new);
    CHECK(ed.undo_steps_for_test() == steps_before_new + 1);  // (one step, its maps' load included)
    ed.command("surface add Noise");
    ed.command("surface mask preset 1 edgewear");
    ed.command("surface set L1 Opacity 0.5");
    CHECK(pt->layers.size() == 2 && pt->layers[1]->mask_layers.size() == 2 && std::fabs(pt->layers[1]->opacity - 0.5f) < 1e-6f);
    ed.command("surface set L1 M0 Low 0.4");
    bool low_set = false;
    {
      struct Read : Reflector {
        float low = -1.0f;
        void field(const char *n, float &v, float, float, float) override { if (std::string(n) == "L1 M0 Low") low = v; }
        void field(const char *, int &, int, int) override {}
        void field(const char *, bool &) override {}
        void field(const char *, Vec3 &) override {}
        void color(const char *, Vec3 &) override {}
        void enumeration(const char *, int &, const char *const *, int) override {}
        void text(const char *, std::string &) override {}
        void mesh(const char *, MeshPtr &) override {}
      } rd;
      pt->reflect(rd);
      low_set = std::fabs(rd.low - 0.4f) < 1e-6f;
    }
    CHECK(low_set);
    ed.step_frame_headless();  // (the pending step of the last field edit commits)
    const size_t steps = ed.undo_steps_for_test();
    ed.command("surface group 0 1");
    ed.step_frame_headless();  // (the step commits)
    std::printf("    after surface group 0 1: %zu layer(s), the first %s with %zu children, %zu step(s)\n", pt->layers.size(), pt->layers.empty() ? "-" : pt->layers[0]->type_name(),
                pt->layers.empty() || !pt->layers[0]->children() ? (size_t)0 : pt->layers[0]->children()->size(), ed.undo_steps_for_test() - steps);
    CHECK(pt->layers.size() == 1 && std::string(pt->layers[0]->type_name()) == "Group" && pt->layers[0]->children() && pt->layers[0]->children()->size() == 2 && ed.undo_steps_for_test() == steps + 1);
    ed.command("undo");
    pt = texture_asset(asset);
    std::printf("    after undo: %zu layer(s)\n", pt ? pt->layers.size() : (size_t)0);
    CHECK(pt && pt->layers.size() == 2);
    /* The worker: the stack changed, the frames step while it evaluates, the images land. */
    ed.command("surface add Blur");
    ed.command("surface set L2 Writes Emission 1");  // (the emission map follows the stack once the worker lands)
    double slowest = 0.0;
    int frames = 0;
    bool ran = false;
    for (int i = 0; i < 4000 && (i < 3 || ed.surface_busy_for_test()); i++) {
      const auto t0 = std::chrono::steady_clock::now();
      ed.step_frame_headless();
      slowest = std::max(slowest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
      frames++;
      ran = ran || ed.surface_busy_for_test();
      if (ed.surface_busy_for_test()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("    the surface's worker ran %d, %d frame(s) meanwhile, the slowest %.1f ms; busy still %d\n", (int)ran, frames, slowest, (int)ed.surface_busy_for_test());
    CHECK(ran && !ed.surface_busy_for_test());
    for (int i = 0; i < 4000 && (i < 3 || ed.surface_busy_for_test()); i++) {  // (the set after the add is a second job: let it land)
      ed.step_frame_headless();
      if (ed.surface_busy_for_test()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool emission_now = split_texture_ref(G_material(G()).emission_map.path, asset, out) && out == (int)TextureOutput::Emission;
    std::printf("    after a layer writes emission: the material's emission map %d\n", (int)emission_now);
    CHECK(emission_now);
    ed.command("surface eval 10 10 74 74");
    ed.command("surface list");
  });
}

static void tests_round265() {
  test("Paint Mode (ADR 0081): a dab paints the texels under the brush's disc on the near side only, with the brush's colour at the centre; the material brush writes roughness, metallic and height too; the eraser clears; Mirror X paints the mirror; a fill of the object covers it; the stroke's undo restores the tiles bit for bit, holding only the touched tiles", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    ed.command("delete");
    ed.command("create Sphere");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) ed.step_frame_headless();
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    G()->set_local_scale(Vec3(6.0f));  // (a sphere of three units nine units off fills a good part of the view: a dab is a part of it)
    ed.mark_changed_for_test("a big sphere");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    ed.command("surface new PaintSurf");
    ed.command("meshmaps 256 4");
    for (int i = 0; i < 4000 && ed.meshmaps_busy_for_test(); i++) ed.step_frame_headless(), std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ed.command("surface maps");
    ed.command("paint on");
    CHECK(ed.paint_for_test());
    if (!ed.paint_for_test()) return;
    Vec2 px;
    CHECK(ed.project_for_test(G()->world_position(), px));
    for (int i = 0; i < 30; i++) ed.step_frame_headless();  // (the id plane holds the sphere)
    ed.command("paint color 0.9 0.2 0.1");
    ed.command("paint roughness 0.2");
    ed.command("paint metallic 1");
    ed.command("paint height 0.8");
    ed.command("paint hardness 1");  // (the whole disc at full weight: the colour test reads any painted texel)
    ed.command("paint size 40");
    ed.command(strprintf("paint dab %g %g", px.x, px.y));
    PaintImagesPtr im = ed.paint_images_for_test();
    CHECK(im && !im->empty());
    if (!im || im->empty()) return;
    std::printf("    the stroke's undo step holds %zu bytes against the images' %zu\n", ed.paint_last_step_bytes_for_test(), im->size() * 13);
    CHECK(ed.paint_last_step_bytes_for_test() < im->size() * 13 / 4);
    const std::vector<uint32_t> after_dab = im->color;
    const std::vector<uint8_t> cover_dab = im->cover;
    size_t painted = 0, colour_ok = 0, values_ok = 0;
    for (size_t i = 0; i < im->size(); i++) {
      if (!im->cover[i]) continue;
      painted++;
      const uint32_t c = im->color[i];
      colour_ok += std::abs((int)((c >> 16) & 0xFF) - 243) <= 1 && std::abs((int)((c >> 8) & 0xFF) - 124) <= 1 && std::abs((int)(c & 0xFF) - 89) <= 1 ? 1 : 0;  // (0.9 0.2 0.1 in sRGB)
      values_ok += std::abs((int)im->rough[i] - 51) <= 1 && im->metal[i] == 255 && std::abs((int)im->height[i] - 204) <= 1 ? 1 : 0;
    }
    /* The far side: the surface's maps hold the world normal. The painted texels' mean normal faces the camera (a
     * 40-px disc on a sphere that fills a third of the view is a cap of some 15 degrees); a texel whose normal opposes
     * it is on the far side, and none of those may be painted. */
    std::string why;
    ProceduralTexturePtr pt = ed.surface_of_for_test(G(), &why);
    const std::shared_ptr<const SurfaceMaps> maps_keep = pt ? pt->maps : nullptr;  // (held: the test reads them after undos)
    const SurfaceMaps *maps = maps_keep.get();
    size_t far_painted = 0, far_total = 0, near_total = 0;
    if (maps && maps->w == im->w) {
      Vec3 mean(0.0f);
      for (size_t i = 0; i < im->size(); i++)
        if (im->cover[i] && maps->cover[i] > 0.5f) mean += maps->normal[i];
      const Vec3 to_cam = length(mean) > 1e-6f ? normalize(mean) : Vec3(0.0f, 0.0f, 1.0f);
      for (size_t i = 0; i < im->size(); i++) {
        if (maps->cover[i] < 0.5f) continue;
        const bool far = dot(maps->normal[i], to_cam) < -0.3f;
        if (far) far_total++, far_painted += im->cover[i] ? 1 : 0;
        else near_total++;
      }
    }
    std::printf("    a dab of 40 px on the sphere: %zu texels painted (%zu the brush's colour within 1/255, %zu its roughness / metallic / height), %zu of %zu far-side texels painted (%zu near-side texels)\n",
                painted, colour_ok, values_ok, far_painted, far_total, near_total);
    CHECK(painted > 100 && colour_ok == painted && values_ok == painted && painted < near_total + far_total);
    CHECK(maps && far_total > 0 && far_painted == 0);
    /* The eraser clears what the dab painted. */
    ed.command("paint erase on");
    ed.command(strprintf("paint dab %g %g", px.x, px.y));
    size_t left = 0;
    for (uint8_t c : im->cover) left += c ? 1 : 0;
    ed.command("paint erase off");
    std::printf("    the eraser over the same dab: %zu texels still covered\n", left);
    CHECK(left < painted / 10);
    /* Undo both strokes: bit for bit the images before them, the buffers kept, the steps' tiles small. */
    const uint32_t *buf_before = im->color.data();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("undo");
    CHECK(im->color == after_dab && im->cover == cover_dab);
    ed.command("undo");
    size_t any = 0;
    for (uint8_t c : im->cover) any += c ? 1 : 0;
    std::printf("    after two undos: %zu covered texels, the buffer %s, %zu step(s) fewer; the surface keeps its maps %d\n", any, im->color.data() == buf_before ? "kept" : "reallocated",
                steps - ed.undo_steps_for_test(), (int)(pt->maps == maps_keep));
    CHECK(any == 0 && im->color.data() == buf_before && ed.undo_steps_for_test() == steps - 2 && pt->maps == maps_keep);
    ed.command("redo");
    CHECK(im->color == after_dab);
    ed.command("undo");
    /* Mirror X: the dab and its mirror; then a fill of the whole object. */
    Vec2 side_px;  // (a point on the sphere a third of a radius right of its centre, as seen: on the surface whatever its size on screen)
    CHECK(ed.project_for_test(G()->world_position() + Vec3(1.0f, 0.0f, 0.0f), side_px));
    ed.command("paint symmetry x");
    ed.command(strprintf("paint dab %g %g", side_px.x, side_px.y));
    size_t mirrored = 0;
    for (uint8_t c : im->cover) mirrored += c ? 1 : 0;
    ed.command("paint symmetry none");
    ed.command("undo");
    ed.command(strprintf("paint dab %g %g", side_px.x, side_px.y));
    size_t single = 0;
    for (uint8_t c : im->cover) single += c ? 1 : 0;
    std::printf("    a dab beside the centre: %zu texels; with Mirror X %zu\n", single, mirrored);
    CHECK(single > 0 && mirrored > single * 3 / 2);
    ed.command("undo");
    ed.command("paint fill object");
    size_t filled = 0, maps_covered = 0;
    for (size_t i = 0; i < im->size(); i++) filled += im->cover[i] ? 1 : 0, maps_covered += maps && maps->cover[i] > 0.5f ? 1 : 0;
    std::printf("    paint fill object: %zu texels of %zu the maps cover (%d triangles; the editor's images are the test's %d)\n", filled, maps_covered, ed.paint_last_fill_for_test(),
                (int)(ed.paint_images_for_test() == im));
    CHECK(filled > maps_covered * 9 / 10);
    ed.command("paint off");
    CHECK(!ed.paint_for_test());
  });
}

static void tests_round266() {
  test("smart materials in the editor: smat apply puts Painted Metal on the active object's surface as one step and smat save writes it; a 2D dab lands at its UV; a stencil black on the left masks a dab there; the Texturing layout opens", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    ed.command("surface new SmatSurf");
    ed.step_frame_headless();  // (the step commits)
    std::string why;
    ProceduralTexturePtr pt = ed.surface_of_for_test(G(), &why);
    CHECK(pt && pt->layers.size() == 1);
    if (!pt) return;
    const size_t steps = ed.undo_steps_for_test();
    ed.command("smat apply Painted Metal");
    ed.step_frame_headless();
    std::printf("    smat apply: %zu layers, the top '%s' with %zu children, %zu step(s)\n", pt->layers.size(), pt->layers.back()->name.c_str(),
                pt->layers.back()->children() ? pt->layers.back()->children()->size() : (size_t)0, ed.undo_steps_for_test() - steps);
    CHECK(pt->layers.size() == 2 && pt->layers.back()->name == "Painted Metal" && pt->layers.back()->children() && pt->layers.back()->children()->size() == 2 && ed.undo_steps_for_test() == steps + 1);
    ed.command("smat save 1 MyMetal");
    CHECK(fs::exists(ed.resolve_asset_path_for_test("Assets/Materials/Smart/MyMetal.smat")));
    ed.command("smat apply MyMetal");
    CHECK(pt->layers.size() == 3 && pt->layers.back()->name == "MyMetal");
    /* Another object's surface takes the saved file too: its hash moves, and its generators read its own maps. */
    ed.command("select Sphere");
    ed.step_frame_headless();
    GameObject *sphere = ed.active_object_for_test();
    CHECK(sphere && sphere != G());
    ed.command("surface new SphereSurf");
    ed.step_frame_headless();
    ProceduralTexturePtr ps = ed.surface_of_for_test(sphere, &why);
    CHECK(ps && ps != pt && ps->layers.size() == 1);
    if (ps) {
      const uint64_t h0 = ps->hash();
      ed.command("smat apply MyMetal");
      ed.step_frame_headless();
      CHECK(ps->layers.size() == 2 && ps->hash() != h0 && pt->layers.size() == 3);
      ed.command("undo");
      ed.command("undo");  // (the sphere's surface too: the cube's undos below count from its own edits)
    }
    ed.command("select Cube");
    ed.step_frame_headless();
    ed.command("undo");
    std::printf("    after one undo: %zu layers\n", pt->layers.size());
    ed.command("undo");
    pt = ed.surface_of_for_test(G(), &why);
    std::printf("    after two undos: %zu layers (%s)\n", pt ? pt->layers.size() : (size_t)0, why.c_str());
    CHECK(pt && pt->layers.size() == 1);
    /* 2D painting: a dab at (0.25, 0.75) on the 1024 surface lands around image texel (256, 256). */
    ed.command("paint on");
    CHECK(ed.paint_for_test());
    if (!ed.paint_for_test()) return;
    ed.command("paint size 16");
    ed.command("paint hardness 1");
    CHECK(ed.paint_dab_uv_for_test(0.25f, 0.75f));
    PaintImagesPtr im = ed.paint_images_for_test();
    CHECK(im && !im->empty());
    if (!im || im->empty()) return;
    size_t painted = 0, off = 0;
    for (size_t i = 0; i < im->size(); i++) {
      if (!im->cover[i]) continue;
      painted++;
      const int x = (int)(i % im->w), y = (int)(i / im->w);
      if (std::abs(x - 256) > 19 || std::abs(y - 256) > 19) off++;  // (the disc of 16 plus the two-texel dilation)
    }
    std::printf("    a 2D dab at (0.25, 0.75): %zu texels around (256, 256), %zu elsewhere\n", painted, off);
    CHECK(painted > 600 && off == 0);
    ed.command("undo");
    /* The canvas by real events: the Textures window open on the surface, 2D painting on, a press on the preview pane,
     * a drag over several frames and the release - one stroke, one step, the coverage capped at the opacity. */
    ed.command("layout Texturing");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Textures).w > 0 && ed.window_rect_for_test(WindowKind::Scene).w > 0 && ed.window_rect_for_test(WindowKind::Inspector).w > 0 && ed.window_rect_for_test(WindowKind::Tools).w > 0);
    ed.select_texture_for_test(pt);
    ed.command("paint 2d on");
    ed.command("paint opacity 0.5");
    ed.command("paint size 24");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti pane = ed.texture_ui_rect_for_test("preview");
    CHECK(pane.w > 0);
    if (pane.w > 0) {
      const size_t steps0 = ed.undo_steps_for_test();
      const int y = pane.y + pane.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, pane.x + pane.w / 4, y)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, pane.x + pane.w / 4, y)});
      std::printf("    after the press on the canvas: %s\n", ed.paint_canvas_state_for_test().c_str());
      for (int i = 1; i <= 6; i++) ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, pane.x + pane.w / 4 + i * pane.w / 12, y)});
      const size_t steps_held = ed.undo_steps_for_test();
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, pane.x + pane.w * 3 / 4, y)});
      ed.step_frame_headless();
      im = ed.paint_images_for_test();
      size_t covered = 0, over = 0, maxc = 0;
      if (im)
        for (uint8_t c : im->cover) covered += c ? 1 : 0, over += c > 130 ? 1 : 0, maxc = std::max(maxc, (size_t)c);
      std::printf("    a canvas drag over 6 frames: %zu texels, the highest coverage %zu of 255 (opacity 0.5), %zu steps while held, %zu at the release\n", covered, maxc, steps_held - steps0, ed.undo_steps_for_test() - steps0);
      CHECK(covered > 5000 && over == 0 && steps_held == steps0 && ed.undo_steps_for_test() == steps0 + 1);
      if (ed.undo_steps_for_test() == steps0 + 1) ed.command("undo");
    }
    ed.command("paint 2d off");
    ed.command("paint opacity 1");
    ed.command("paint size 16");
    /* A stencil black on its left half: a dab projected into the left half of the view paints nothing; the right half paints. */
    Bitmap st;
    st.width = st.height = 2;
    st.rgba8 = {0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    std::string err;
    const std::string stencil = fs::join(scratch_project(), "stencil.png");
    {
      const uint32_t px[4] = {0xFF000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu};
      CHECK(write_png(stencil, px, 2, 2, 2, false));
    }
    ed.command("paint stencil " + stencil);
    Vec2 px;
    CHECK(ed.project_for_test(G()->world_position(), px));
    for (int i = 0; i < 30; i++) ed.step_frame_headless();
    const Recti view = ed.scene_rect_for_test();
    /* The square stencil is fit to the view's shorter side at its top left; the offset puts its black half over the cube,
     * then its white half. */
    const float sw = (float)std::min(view.w, view.h);
    ed.command(strprintf("paint stencil offset %g 0", px.x - sw * 0.25f));  // (the cube in the black half)
    ed.command("paint stencil scale 1");
    ed.command(strprintf("paint dab %g %g", px.x - 2.0f, px.y));
    size_t masked = 0;
    for (uint8_t c : im->cover) masked += c ? 1 : 0;
    ed.command(strprintf("paint stencil offset %g 0", px.x - sw * 0.75f));  // (the cube in the white half)
    ed.command(strprintf("paint dab %g %g", px.x - 2.0f, px.y));
    size_t through = 0;
    for (uint8_t c : im->cover) through += c ? 1 : 0;
    std::printf("    a stencil over the view: %zu texels painted through its black half, %zu through its white half\n", masked, through);
    CHECK(masked == 0 && through > 0);
    /* A NaN offset or a float image never reaches a bad index. */
    ed.command("paint stencil offset nan 0");
    ed.command(strprintf("paint dab %g %g", px.x, px.y));
    ed.command("paint stencil off");
    ed.command("paint off");
    /* --assign: the material reads the exported files (the separate maps for roughness and metallic beside the packing). */
    ed.command("export textures gltf Exports/Assigned --assign");
    const MeshRenderer *mr = G()->get<MeshRenderer>();
    const Material *am = mr && !mr->materials.empty() ? mr->materials[0].get() : nullptr;
    std::printf("    --assign: base '%s' rough '%s' metal '%s' emit '%s' metallic %g roughness %g\n", am ? am->base_map.path.c_str() : "-", am ? am->roughness_map.path.c_str() : "-", am ? am->metallic_map.path.c_str() : "-", am ? am->emission_map.path.c_str() : "-", am ? am->metallic : -1.0f, am ? am->roughness : -1.0f);
    const std::string ex = "Exports/Assigned/" + pt->name + "_";  // (the scratch project keeps earlier runs' assets: the surface's name may carry a number)
    /* The emission map follows the live rule (the stack writes emission - the Paint layer does); whether the live material already
     * carried one depends on the worker having landed, which the sanitizer build's timing made false: either is right here. */
    const bool assigned = am && am->base_map.path == ex + "Albedo.png" && am->roughness_map.path == ex + "Roughness.png" && am->metallic_map.path == ex + "Metallic.png" && (am->emission_map.path.empty() || am->emission_map.path == ex + "Emission.png") && am->metallic == 1.0f && am->roughness == 1.0f;
    CHECK(assigned && fs::exists(ed.resolve_asset_path_for_test(ex + "ORM.png")) && fs::exists(ed.resolve_asset_path_for_test(ex + "Metallic.png")));
    ed.command("undo");
  });
}

static void tests_round267() {
  test("the painterly brush (ADR 0083): a Bristle stamp has several peaks inside the disc and a Round one, a Flat tip turned 90 degrees swaps its extents; a wet blue stroke over red ends between them; jitter 0 paints the exact colour and jitter 0.2 in value stays within it; a checker grain halves the painted texels; Smudge pulls a patch's edge along the drag", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    g->set_local_scale(Vec3(6.0f));  // (the cube fills a good part of the view: a 30 px dab is a patch, not the whole face)
    ed.command("surface new BrushSurf");
    ed.step_frame_headless();
    ed.command("paint on");
    CHECK(ed.paint_for_test());
    if (!ed.paint_for_test()) return;
    PaintBrush &b = ed.paint_brush_for_test();
    /* The stamps, sampled on a 41 x 41 grid over the disc. */
    auto peaks = [&](int tip) {
      b.tip = tip, b.hardness = 0.0f;  // (no plateau: a peak is one cell)
      const int n = 41;
      std::vector<float> w((size_t)n * n);
      for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) w[(size_t)y * n + x] = ed.paint_stamp_for_test({(x - 20) / 20.0f, (y - 20) / 20.0f}, {1.0f, 0.0f}, 7u);
      int count = 0;
      for (int y = 1; y < n - 1; y++)
        for (int x = 1; x < n - 1; x++) {
          const float c = w[(size_t)y * n + x];
          if (c <= 0.05f) continue;
          bool top = true;
          for (int dy = -1; dy <= 1 && top; dy++)
            for (int dx = -1; dx <= 1; dx++)
              if ((dx || dy) && w[(size_t)(y + dy) * n + x + dx] > c) top = false;
          /* A plateau (the hard centre) counts once: only its first texel in scan order. */
          bool first = true;
          for (int dy = -1; dy <= 0 && first; dy++)
            for (int dx = -1; dx <= 1; dx++)
              if ((dy < 0 || dx < 0) && w[(size_t)(y + dy) * n + x + dx] == c) first = false;
          if (top && first) count++;
        }
      return count;
    };
    const int round_peaks = peaks(0), bristle_peaks = peaks(2);
    b.tip = 1, b.tip_ratio = 0.3f, b.hardness = 0.5f;  // (0.5 along the drag is inside the hard centre; 0.5 across is 1.67 radii out of the 0.3-wide ellipse)
    const float along = ed.paint_stamp_for_test({0.5f, 0.0f}, {1.0f, 0.0f}, 7u), across = ed.paint_stamp_for_test({0.0f, 0.5f}, {1.0f, 0.0f}, 7u);
    const float along_t = ed.paint_stamp_for_test({0.5f, 0.0f}, {0.0f, 1.0f}, 7u), across_t = ed.paint_stamp_for_test({0.0f, 0.5f}, {0.0f, 1.0f}, 7u);
    std::printf("    stamps: Round %d peak(s), Bristle %d; Flat along / across the drag %.2f / %.2f, turned 90 degrees %.2f / %.2f\n", round_peaks, bristle_peaks, along, across, along_t, across_t);
    CHECK(round_peaks == 1 && bristle_peaks >= 6 && along > 0.5f && across == 0.0f && along_t == 0.0f && across_t > 0.5f);
    b.tip = 0;
    /* The dabs below go on the cube's front face, at the view's centre. */
    Vec2 px;
    CHECK(ed.project_for_test(g->world_position(), px));
    for (int i = 0; i < 30; i++) ed.step_frame_headless();
    auto stroke = [&](float x0, float x1, int dabs) { ed.command(strprintf("paint stroke %g %g %g %g %d", x0, px.y, x1, px.y, dabs)); };  // (one stroke: the brush carries its pickup along it)
    auto count_where = [&](std::function<bool(uint32_t)> f) {
      PaintImagesPtr im = ed.paint_images_for_test();
      size_t n = 0;
      if (im)
        for (size_t i = 0; i < im->size(); i++)
          if (im->cover[i] && f(im->color[i])) n++;
      return n;
    };
    auto count = [&] { return count_where([](uint32_t) { return true; }); };
    ed.command("paint size 30");
    ed.command("paint hardness 1");
    ed.command("paint opacity 1");
    /* Wet: red first, then blue over it with wet 0.5 - texels with both red and blue above 0.2. */
    ed.command("paint color 1 0 0");
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    ed.command("paint color 0 0 1");
    ed.command("paint wet 0.5");
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    const size_t mixed = count_where([](uint32_t c) { return ((c >> 16) & 0xFF) > 51 && (c & 0xFF) > 51; }), pure_blue = count_where([](uint32_t c) { return ((c >> 16) & 0xFF) == 0 && (c & 0xFF) == 255; });
    std::printf("    a wet blue stroke over red: %zu mixed texels (red and blue both above 0.2), %zu pure blue\n", mixed, pure_blue);
    CHECK(mixed > 500 && pure_blue < mixed);
    ed.command("paint wet 0");
    ed.command("undo");
    ed.command("undo");
    /* Jitter: 0 paints the brush's exact colour; 0.2 in value keeps every texel's value within 0.2 (sRGB bytes compared in linear). */
    ed.command("paint color 0.5 0.25 0.1");
    ed.command("paint jitter 0 0 0");
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    const uint32_t exact = 0xFF000000u | (uint32_t)display::linear_to_srgb8(0.5f) << 16 | (uint32_t)display::linear_to_srgb8(0.25f) << 8 | display::linear_to_srgb8(0.1f);
    const size_t all0 = count(), same0 = count_where([&](uint32_t c) { return c == exact; });
    ed.command("undo");
    ed.command("paint jitter 0 0 0.2");
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    size_t distinct = 0, out_of_range = 0;
    {
      std::set<uint32_t> seen;
      PaintImagesPtr im = ed.paint_images_for_test();
      for (size_t i = 0; im && i < im->size(); i++) {
        if (!im->cover[i]) continue;
        seen.insert(im->color[i]);
        const float v = std::max({((im->color[i] >> 16) & 0xFF) / 255.0f, ((im->color[i] >> 8) & 0xFF) / 255.0f, (im->color[i] & 0xFF) / 255.0f});
        const float v0 = display::linear_to_srgb8(0.5f) / 255.0f;
        if (std::fabs(v - v0) > 0.2f + 0.02f) out_of_range++;  // (0.2 in linear value is less in sRGB bytes: a loose bound with a tolerance)
      }
      distinct = seen.size();
    }
    std::printf("    jitter 0: %zu of %zu texels the exact colour; jitter 0.2 in value: %zu distinct colours, %zu out of range\n", same0, all0, distinct, out_of_range);
    CHECK(all0 > 0 && same0 == all0 && distinct > 3 && out_of_range == 0);
    ed.command("paint jitter 0 0 0");
    ed.command("undo");
    /* Grain: a 2 x 2 checker at scale 1 lets every other texel through - about half the texels. */
    const std::string grain = fs::join(scratch_project(), "grain.png");
    {
      const uint32_t px4[4] = {0xFFFFFFFFu, 0xFF000000u, 0xFF000000u, 0xFFFFFFFFu};
      CHECK(write_png(grain, px4, 2, 2, 2, false));
    }
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    const size_t plain = count();
    ed.command("undo");
    ed.command("paint grain " + grain + " 16");
    stroke(px.x - 40.0f, px.x + 40.0f, 8);
    const size_t grained = count();
    ed.command("paint grain off");
    ed.command("undo");
    std::printf("    grain: %zu texels with a 2 x 2 checker at 16 texels a cell against %zu without (the dilation fills two texels of each hole's rim)\n", grained, plain);
    CHECK(grained > 0 && grained < plain * 0.85f);
    /* Smudge: a red patch, then a smudge stroke dragging rightwards on screen pulls red along the drag - in texel space, which
     * way the face's UVs run (measured first: two dabs at the drag's ends, their texel columns compared). */
    ed.command("paint color 1 0 0");
    ed.command("paint size 20");
    auto red_column = [&] {
      PaintImagesPtr imp = ed.paint_images_for_test();
      double sx = 0.0;
      size_t n = 0;
      for (size_t i = 0; imp && i < imp->size(); i++)
        if (imp->cover[i] && ((imp->color[i] >> 16) & 0xFF) > 128) sx += (double)(i % imp->w), n++;
      return n ? (float)(sx / n) : 0.0f;
    };
    ed.command(strprintf("paint dab %g %g", px.x - 36.0f, px.y));
    const float col_start = red_column();
    ed.command("undo");
    ed.command(strprintf("paint dab %g %g", px.x + 30.0f, px.y));
    const float col_end = red_column();
    ed.command("undo");
    const float drag_sign = col_end > col_start ? 1.0f : -1.0f;
    ed.command(strprintf("paint dab %g %g", px.x - 30.0f, px.y));
    const size_t red_before = count_where([](uint32_t c) { return ((c >> 16) & 0xFF) > 128; });
    PaintImagesPtr im = ed.paint_images_for_test();
    float cx_before = 0.0f;
    {
      double sx = 0.0;
      size_t n = 0;
      for (size_t i = 0; im && i < im->size(); i++)
        if (im->cover[i] && ((im->color[i] >> 16) & 0xFF) > 128) sx += (double)(i % im->w), n++;
      cx_before = n ? (float)(sx / n) : 0.0f;
    }
    ed.command("paint brush smudge");
    stroke(px.x - 36.0f, px.x + 30.0f, 22);
    ed.command("paint brush round");
    const size_t red_after = count_where([](uint32_t c) { return ((c >> 16) & 0xFF) > 128; });
    float cx_after = 0.0f;
    {
      double sx = 0.0;
      size_t n = 0;
      for (size_t i = 0; im && i < im->size(); i++)
        if (im->cover[i] && ((im->color[i] >> 16) & 0xFF) > 128) sx += (double)(i % im->w), n++;
      cx_after = n ? (float)(sx / n) : 0.0f;
    }
    std::printf("    smudge: %zu red texels before, %zu after; the red's centre moved from texel column %.1f to %.1f (the screen drag runs toward %s in this face's texels: the paint goes with it)\n", red_before, red_after, cx_before, cx_after, drag_sign > 0.0f ? "+x" : "-x");
    CHECK(red_before > 0 && red_after > red_before && (cx_after - cx_before) * drag_sign > 1.0f);
    ed.command("paint off");
  });
}

static void tests_round267b() {
  test("lighting painted in (ADR 0083): paint bakelight with a key from +X adds one Multiply Image layer as one step whose PNG is brighter on the sphere's +X side than its -X side; paint bakegradient is brighter at the top; the Gradient fill over the object is brighter at the top than the bottom; a palette saved and loaded has the same swatches and picks the brush's colour", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Sphere");
    for (int i = 0; i < 10; i++) ed.step_frame_headless();
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    G()->set_local_scale(Vec3(4.0f));
    ed.command("surface new LightSurf");
    ed.step_frame_headless();
    ed.command("meshmaps 128 2");
    for (int i = 0; i < 20000 && ed.meshmaps_busy_for_test(); i++) ed.step_frame_headless();
    ed.command("meshmaps status");
    ed.command("surface maps");
    ed.step_frame_headless();
    std::string why;
    ProceduralTexturePtr pt = ed.surface_of_for_test(G(), &why);
    CHECK(pt && pt->maps && !pt->maps->empty());
    if (!pt || !pt->maps || pt->maps->empty()) return;
    const size_t layers0 = pt->layers.size(), steps0 = ed.undo_steps_for_test();
    ed.command("paint bakelight key 1 0 0");
    for (int i = 0; i < 20000 && ed.bakelight_busy_for_test(); i++) ed.step_frame_headless();  // (the worker: the layer lands on a frame)
    ed.step_frame_headless();
    const bool added = pt->layers.size() == layers0 + 1 && std::string(pt->layers.back()->type_name()) == "Image" && pt->layers.back()->blend == (int)LayerBlend::Multiply;
    std::string rel;
    if (added) {  // (the layer's file: a name of its own per bake, so an earlier layer keeps its image)
      struct ReadPath : Reflector {
        std::string path;
        bool all_fields() const override { return true; }
        void field(const char *, float &, float, float, float) override {}
        void field(const char *, int &, int, int) override {}
        void field(const char *, bool &) override {}
        void field(const char *, Vec3 &) override {}
        void color(const char *, Vec3 &) override {}
        void enumeration(const char *, int &, const char *const *, int) override {}
        void text(const char *, std::string &) override {}
        void mesh(const char *, MeshPtr &) override {}
        void texture(const char *n, TextureRef &v) override {
          if (std::string(n) == "Image") path = v.path;
        }
      } rp;
      pt->layers.back()->reflect(rp);
      rel = rp.path;
    }
    Bitmap bm;
    std::string err;
    const bool loaded = !rel.empty() && load_image(ed.resolve_asset_path_for_test(rel), bm, err) && bm.width == pt->maps->w;
    float plus = 0.0f, minus = 0.0f;
    size_t pn = 0, mn = 0;
    for (size_t i = 0; loaded && i < pt->maps->normal.size(); i++) {
      if (pt->maps->cover[i] < 0.5f) continue;
      const float nx = pt->maps->normal[i].x, lum = bm.rgba8[i * 4] / 255.0f;
      if (nx > 0.8f) plus += lum, pn++;
      else if (nx < -0.8f) minus += lum, mn++;
    }
    std::printf("    bakelight from +X: %zu layer(s) (+%zu), %zu step(s); the PNG's +X side %.3f against -X %.3f\n", pt->layers.size(), pt->layers.size() - layers0, ed.undo_steps_for_test() - steps0, pn ? plus / pn : 0.0f, mn ? minus / mn : 0.0f);
    CHECK(added && ed.undo_steps_for_test() == steps0 + 1 && loaded && pn && mn && plus / pn > minus / mn + 0.2f);
    ed.command("undo");
    pt = ed.surface_of_for_test(G(), &why);
    CHECK(pt && pt->layers.size() == layers0);
    if (!pt) return;
    /* The scene's sun as the key: a directional light aimed down (-Y) makes the top brighter than the underside. */
    ed.command("select Directional Light");
    ed.step_frame_headless();
    GameObject *sun = ed.active_object_for_test();
    if (sun && sun->get<Light>()) {
      sun->set_local_euler(Vec3(90.0f, 0.0f, 0.0f));  // (+Z turned to -Y: the light points down)
      ed.command("select Sphere");
      ed.step_frame_headless();
      ed.command("paint bakelight");
      for (int i = 0; i < 20000 && ed.bakelight_busy_for_test(); i++) ed.step_frame_headless();
      ed.step_frame_headless();
      std::string rel2;
      if (pt->layers.size() == layers0 + 1) {
        struct ReadPath2 : Reflector {
          std::string path;
          bool all_fields() const override { return true; }
          void field(const char *, float &, float, float, float) override {}
          void field(const char *, int &, int, int) override {}
          void field(const char *, bool &) override {}
          void field(const char *, Vec3 &) override {}
          void color(const char *, Vec3 &) override {}
          void enumeration(const char *, int &, const char *const *, int) override {}
          void text(const char *, std::string &) override {}
          void mesh(const char *, MeshPtr &) override {}
          void texture(const char *n, TextureRef &v) override {
            if (std::string(n) == "Image") path = v.path;
          }
        } rp2;
        pt->layers.back()->reflect(rp2);
        rel2 = rp2.path;
      }
      Bitmap sm;
      const bool sl = !rel2.empty() && rel2 != rel && load_image(ed.resolve_asset_path_for_test(rel2), sm, err) && sm.width == pt->maps->w;
      float up = 0.0f, down = 0.0f;
      size_t un = 0, dn = 0;
      for (size_t i = 0; sl && i < pt->maps->normal.size(); i++) {
        if (pt->maps->cover[i] < 0.5f) continue;
        const float ny = pt->maps->normal[i].y, lum = sm.rgba8[i * 4] / 255.0f;
        if (ny > 0.8f) up += lum, un++;
        else if (ny < -0.8f) down += lum, dn++;
      }
      std::printf("    bakelight from the scene's sun aimed down: the top %.3f against the underside %.3f (its own file: %d)\n", un ? up / un : 0.0f, dn ? down / dn : 0.0f, (int)(rel2 != rel));
      CHECK(sl && un && dn && up / un > down / dn + 0.2f);
      ed.command("undo");
    }
    else ed.command("select Sphere");
    ed.command("paint bakegradient 1 1 1 0 0 0");
    for (int i = 0; i < 20000 && ed.bakelight_busy_for_test(); i++) ed.step_frame_headless();
    ed.step_frame_headless();
    std::string grel;
    if (pt->layers.size() == layers0 + 1) {
      struct ReadPath3 : Reflector {
        std::string path;
        bool all_fields() const override { return true; }
        void field(const char *, float &, float, float, float) override {}
        void field(const char *, int &, int, int) override {}
        void field(const char *, bool &) override {}
        void field(const char *, Vec3 &) override {}
        void color(const char *, Vec3 &) override {}
        void enumeration(const char *, int &, const char *const *, int) override {}
        void text(const char *, std::string &) override {}
        void mesh(const char *, MeshPtr &) override {}
        void texture(const char *n, TextureRef &v) override {
          if (std::string(n) == "Image") path = v.path;
        }
      } rp3;
      pt->layers.back()->reflect(rp3);
      grel = rp3.path;
    }
    Bitmap gm;
    const bool gl = !grel.empty() && load_image(ed.resolve_asset_path_for_test(grel), gm, err) && gm.width == pt->maps->w;
    float top = 0.0f, bottom = 0.0f;
    size_t tn = 0, bn = 0;
    for (size_t i = 0; gl && i < pt->maps->position.size(); i++) {
      if (pt->maps->cover[i] < 0.5f) continue;
      const float y = pt->maps->position[i].y, lum = gm.rgba8[i * 4] / 255.0f;
      if (y > 0.9f) top += lum, tn++;
      else if (y < 0.1f) bottom += lum, bn++;
    }
    std::printf("    bakegradient: the top %.3f against the bottom %.3f\n", tn ? top / tn : 0.0f, bn ? bottom / bn : 0.0f);
    CHECK(gl && tn && bn && top / tn > bottom / bn + 0.5f);
    ed.command("undo");
    /* The Gradient fill: dark at the bottom to light at the top over the object, read back by the maps' position. */
    ed.command("paint on");
    CHECK(ed.paint_for_test());
    if (!ed.paint_for_test()) return;
    ed.command("paint gradient 0.1 0.1 0.1 0.9 0.9 0.9 object y");
    PaintImagesPtr im = ed.paint_images_for_test();
    CHECK(im && !im->empty());
    float gtop = 0.0f, gbottom = 0.0f;
    size_t gtn = 0, gbn = 0;
    for (size_t i = 0; im && i < im->size(); i++) {
      if (!im->cover[i]) continue;
      const int x = (int)(i % im->w), y = (int)(i / im->w);
      const Vec3 q = pt->maps->sample3(pt->maps->position, (x + 0.5f) / im->w, 1.0f - (y + 0.5f) / im->h);
      const float lum = ((im->color[i] >> 16) & 0xFF) / 255.0f;
      if (q.y > 0.9f) gtop += lum, gtn++;
      else if (q.y < 0.1f) gbottom += lum, gbn++;
    }
    std::printf("    the Gradient fill: %zu texels; the top %.3f against the bottom %.3f\n", im ? im->size() : (size_t)0, gtn ? gtop / gtn : 0.0f, gbn ? gbottom / gbn : 0.0f);
    CHECK(gtn && gbn && gtop / gtn > gbottom / gbn + 0.5f);
    ed.command("undo");
    /* The palette. */
    ed.command("paint palette add 1 0 0");
    ed.command("paint palette add 0 1 0");
    ed.command("paint palette add 0.2 0.3 0.4");
    ed.command("paint palette save TestPal");
    CHECK(fs::exists(ed.resolve_asset_path_for_test("Assets/Palettes/TestPal.pal")));
    ed.command("paint palette remove 0");
    ed.command("paint palette remove 0");
    CHECK(ed.paint_palette_for_test().size() == 1);
    ed.command("paint palette load TestPal");
    const auto &pal = ed.paint_palette_for_test();
    CHECK(pal.size() == 3 && length(pal[2] - Vec3(0.2f, 0.3f, 0.4f)) < 1e-5f);
    ed.command("paint palette pick 1");
    CHECK(length(ed.paint_brush_for_test().color - Vec3(0.0f, 1.0f, 0.0f)) < 1e-6f);
    ed.command("paint palette shade 0");
    std::printf("    palette: %zu swatches after load; shade 0 of green is (%.2f, %.2f, %.2f)\n", pal.size(), ed.paint_brush_for_test().color.x, ed.paint_brush_for_test().color.y, ed.paint_brush_for_test().color.z);
    CHECK(std::fabs(ed.paint_brush_for_test().color.y - 0.5f) < 1e-4f && ed.paint_brush_for_test().color.x < 1e-6f);
    ed.command("paint off");
  });
}

static void tests_round268() {
  test("stroke objects (ADR 0084): a dab on the cube makes a Stroke object with one card facing the camera, Surface normals take the face's axis normal, Canopy normals point from the centre; a ribbon of N points is a strip of 2N vertices and N - 1 quads; each stroke is one undo step; the list survives its text form", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    for (int i = 0; i < 10; i++) ed.step_frame_headless();
    GameObject *cube = ed.active_object_for_test();
    CHECK(cube);
    if (!cube) return;
    cube->set_local_scale(Vec3(4.0f));
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    Vec2 px;
    CHECK(ed.project_for_test(cube->world_position(), px));
    const size_t steps0 = ed.undo_steps_for_test();
    ed.command("strokeobj size 0.3");
    ed.command(strprintf("strokeobj dab %g %g", px.x, px.y));
    ed.step_frame_headless();
    GameObject *so = ed.strokeobj_object_for_test();
    const Strokes *st = so ? so->get<Strokes>() : nullptr;
    const MeshFilter *mf = so ? so->mesh_filter() : nullptr;
    CHECK(ed.strokeobj_for_test() && st && mf && mf->mesh && st->cards.size() == 1 && mf->mesh->vert_count() == 4 && mf->mesh->face_count() == 2 && mf->mesh->custom_normals.size() == 4);  // (a card is two faces back to back: the back-side flip never turns the custom normal)
    if (!st || !mf || !mf->mesh || st->cards.empty()) return;
    const Ray ray = ed.screen_ray_for_test(px);
    const Vec3 toward = normalize(ray.dir) * -1.0f;
    const float facing_dot = dot(normalize(mf->mesh->custom_normals[0]), toward);
    std::printf("    a card at the cube's centre: %zu card(s), %zu vertices, its normal against the view direction %.4f (one degree is %.4f), %zu step(s)\n", st->cards.size(), mf->mesh->vert_count(), facing_dot, std::cos(1.0f * 3.14159265f / 180.0f), ed.undo_steps_for_test() - steps0);
    CHECK(facing_dot > std::cos(1.0f * 3.14159265f / 180.0f) && ed.undo_steps_for_test() == steps0 + 1);
    /* Surface normals: the cube's face under the dab is axis-aligned. */
    ed.command("strokeobj normals surface");
    ed.step_frame_headless();
    const Vec3 sn = normalize(mf->mesh->custom_normals[0]);
    const float axis_max = std::max({std::fabs(sn.x), std::fabs(sn.y), std::fabs(sn.z)});
    std::printf("    Surface normals: (%.3f, %.3f, %.3f)\n", sn.x, sn.y, sn.z);
    CHECK(axis_max > 0.999f);
    /* Canopy normals: from the centre given. */
    ed.command("strokeobj canopy 0 -10 0");
    ed.step_frame_headless();
    const Vec3 cn = normalize(mf->mesh->custom_normals[0]), expect = normalize(mf->mesh->positions[0] - Vec3(0.0f, -10.0f, 0.0f));  // (per vertex: from the centre to the corner)
    std::printf("    Canopy normals: (%.3f, %.3f, %.3f) against the expected (%.3f, %.3f, %.3f)\n", cn.x, cn.y, cn.z, expect.x, expect.y, expect.z);
    CHECK(dot(cn, expect) > 0.9999f);
    ed.command("strokeobj normals own");
    /* A ribbon along the face. */
    ed.command("strokeobj ribbon");
    ed.command("strokeobj width 0.1");
    ed.command(strprintf("strokeobj stroke %g %g %g %g 9", px.x - 60.0f, px.y, px.x + 60.0f, px.y));
    ed.step_frame_headless();
    const size_t n = st->ribbons.empty() ? 0 : st->ribbons[0].points.size();
    std::printf("    a ribbon: %zu ribbon(s) of %zu points; the mesh %zu vertices, %zu faces\n", st->ribbons.size(), n, mf->mesh->vert_count(), mf->mesh->face_count());
    CHECK(st->ribbons.size() == 1 && n >= 3 && mf->mesh->vert_count() == 4 + 2 * n && mf->mesh->face_count() == 2 + 2 * (n - 1));
    /* The text form round-trips. */
    Strokes copy;
    const std::string text = strokes_to_text(*st);
    CHECK(strokes_from_text(text, copy) && strokes_to_text(copy) == text && copy.cards.size() == 1 && copy.ribbons.size() == 1 && copy.ribbons[0].points.size() == n);
    /* Foliage: a canopy of 100 cards on a 1 m shell round (0, 3, 0), each facing out from the centre; grass of density
     * 40 on the plane's visible spot - about 40 pi blades, upright, their normals up. */
    const uint64_t orig_id = so->id;
    ed.command("strokeobj canopyfill 1 100 0 3 0");  // (a canopy has its own centre: a Stroke object of its own)
    ed.step_frame_headless();
    GameObject *soc = ed.strokeobj_object_for_test();
    const Strokes *stc = soc ? soc->get<Strokes>() : nullptr;
    bool outward = stc && soc != so && stc->cards.size() == 100;
    float shell_err = 0.0f;
    for (size_t i = 0; stc && i < stc->cards.size() && outward; i++) {
      const Vec3 w = soc->world_matrix().point(stc->cards[i].center) - Vec3(0.0f, 3.0f, 0.0f);
      shell_err = std::max(shell_err, std::fabs(length(w) - 1.0f));
      outward = outward && dot(normalize(w), normalize(stc->cards[i].facing)) > 0.999f;
    }
    std::printf("    canopyfill: a new Stroke object %d with %zu cards, on the shell within %.4f, facing out %d; its mesh %zu vertices\n", (int)(soc && soc != so), stc ? stc->cards.size() : (size_t)0, shell_err, (int)outward, soc && soc->mesh_filter() && soc->mesh_filter()->mesh ? soc->mesh_filter()->mesh->vert_count() : (size_t)0);
    CHECK(outward && shell_err < 1e-3f && stc->normals == 2 && soc->mesh_filter() && soc->mesh_filter()->mesh && soc->mesh_filter()->mesh->vert_count() == 400);
    ed.command("undo");
    ed.select_object(orig_id);
    ed.step_frame_headless();
    {
      const GameObject *plane = ed.scene().find_by_name("Plane");
      Vec2 gp;
      bool gfound = false;
      for (const Vec3 &cand : {Vec3(-3.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -3.0f), Vec3(-3.0f, 0.0f, -3.0f), Vec3(3.0f, 0.0f, -3.0f)}) {
        Vec2 p;
        if (!plane || !ed.project_for_test(cand, p)) continue;
        const Ray ray = ed.screen_ray_for_test(p);
        SceneHit hit;
        if (!scene_raycast(ed.scene(), ray.origin, ray.dir, 1e5f, hit) || hit.object != plane) continue;
        gp = p, gfound = true;
        break;
      }
      CHECK(gfound);
      if (gfound) {
        GameObject *so3 = ed.strokeobj_object_for_test();
        const Strokes *st3 = so3 ? so3->get<Strokes>() : nullptr;
        const size_t before = st3 ? st3->cards.size() : 0;
        ed.command(strprintf("strokeobj grass 40 0.25 1 %g %g", gp.x, gp.y));
        ed.step_frame_headless();
        so3 = ed.strokeobj_object_for_test();
        st3 = so3 ? so3->get<Strokes>() : nullptr;
        const size_t blades = st3 ? st3->cards.size() - (so3 == ed.scene().find(orig_id) ? before : 0) : 0;
        bool upright = st3 != nullptr;
        for (size_t i = before; st3 && i < st3->cards.size(); i++) upright = upright && std::fabs(st3->cards[i].facing.y) < 1e-3f && std::fabs(st3->cards[i].size - 0.25f) < 1e-5f;
        const size_t want = (size_t)std::lround(40.0f * 3.14159265f);
        std::printf("    grass: %zu blades (about %zu wanted), upright %d, normals mode %d\n", blades, want, (int)upright, st3 ? st3->normals : -1);
        CHECK(blades >= want * 8 / 10 && blades <= want * 12 / 10 + 1 && upright && st3->normals == 2);
        ed.command("undo");
        ed.select_object(orig_id);
        ed.step_frame_headless();
      }
    }
    so = ed.strokeobj_object_for_test();
    st = so ? so->get<Strokes>() : nullptr;
    mf = so ? so->mesh_filter() : nullptr;
    CHECK(so && st && mf && mf->mesh);
    if (!so || !st || !mf || !mf->mesh) return;
    const uint64_t so_id = so->id;
    ed.command("undo");  // (the scene is rebuilt: the object is looked up again)
    GameObject *so2 = ed.scene().find(so_id);
    const Strokes *st2 = so2 ? so2->get<Strokes>() : nullptr;
    CHECK(st2 && st2->ribbons.empty() && st2->cards.size() == 1 && so2->mesh_filter() && so2->mesh_filter()->mesh && so2->mesh_filter()->mesh->vert_count() == 4);
    ed.command("strokeobj off");
    CHECK(!ed.strokeobj_for_test());
  });
}

static void tests_round268b() {
  test("the Scatter brush (ADR 0084): a dab of density 4 in a 1 m disc on a ground plane places about 4 pi instances of the Sphere as children of one Scatter object, none where a Paint layer's mask covers the plane; the eraser removes them; merge joins them into one mesh with the instances' faces summed", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    GameObject *ground = ed.scene().find_by_name("Plane");  // (the sample scene's ground, at y = 0)
    CHECK(ground && ground->mesh_filter() && ground->mesh_filter()->mesh);
    if (!ground || !ground->mesh_filter() || !ground->mesh_filter()->mesh) return;
    for (int i = 0; i < 10; i++) ed.step_frame_headless();
    /* A ground spot the view sees and nothing hides: candidates projected and traced back along the pixel's ray. */
    Vec3 spot;
    Vec2 px;
    bool found = false;
    const std::vector<Vec3> cands = {Vec3(-3.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -3.0f), Vec3(-3.0f, 0.0f, -3.0f), Vec3(-1.5f, 0.0f, -3.0f), Vec3(-3.0f, 0.0f, 1.5f), Vec3(3.0f, 0.0f, -3.0f), Vec3(0.0f, 0.0f, 3.0f)};  // (well inside the plane, a metre or more from the cube and the sphere)
    for (const Vec3 &cand : cands) {
      Vec2 p;
      if (!ed.project_for_test(cand, p)) continue;
      const Recti view = ed.scene_rect_for_test();
      if (p.x < 10.0f || p.y < 10.0f || p.x > view.w - 10.0f || p.y > view.h - 10.0f) continue;
      const Ray ray = ed.screen_ray_for_test(p);
      SceneHit hit;
      if (!scene_raycast(ed.scene(), ray.origin, ray.dir, 1e5f, hit) || hit.object != ground) continue;
      spot = hit.point, px = p, found = true;
      break;
    }
    std::printf("    the ground spot (%.2f, %.2f, %.2f) at pixel (%.0f, %.0f)\n", spot.x, spot.y, spot.z, px.x, px.y);
    CHECK(std::fabs(spot.y) < 0.01f);
    CHECK(found);
    if (!found) return;
    const GameObject *sphere = ed.scene().find_by_name("Sphere");
    CHECK(sphere && sphere->mesh_filter() && sphere->mesh_filter()->mesh);
    if (!sphere || !sphere->mesh_filter() || !sphere->mesh_filter()->mesh) return;
    const size_t sphere_faces = sphere->mesh_filter()->mesh->face_count();
    ed.command("scatter Sphere");
    ed.command("scatter density 4");
    ed.command("scatter radius 1");
    ed.command("scatter scale 0.1 0.1");  // (small: the drops do not land on each other)
    ed.command("scatter seed 3");
    const size_t steps0 = ed.undo_steps_for_test();
    ed.command(strprintf("scatter dab %g %g", px.x, px.y));
    ed.step_frame_headless();
    GameObject *parent = ed.scatter_parent_for_test();
    const size_t n = parent ? parent->children.size() : 0;
    const float want = 4.0f * 3.14159265f;
    std::printf("    a dab of density 4 in a 1 m disc: %zu instances (about %.1f wanted), %zu step(s)\n", n, want, ed.undo_steps_for_test() - steps0);
    CHECK(parent && n >= (size_t)(want * 0.8f) && n <= (size_t)(want * 1.2f) + 1 && ed.undo_steps_for_test() == steps0 + 1);
    if (!parent) return;
    bool on_plane = true;
    for (const GameObject *c : parent->children) on_plane = on_plane && std::fabs(c->world_position().y) < 0.01f && length(Vec2(c->world_position().x, c->world_position().z) - Vec2(spot.x, spot.z)) <= 1.05f;
    if (!parent->children.empty()) {
      const Vec3 p0 = parent->children[0]->world_position();
      std::printf("    the first instance at (%.3f, %.3f, %.3f)\n", p0.x, p0.y, p0.z);
    }
    CHECK(on_plane);
    /* Merge: one mesh with the instances' faces. */
    ed.command("scatter merge");
    const GameObject *merged = ed.scene().find_by_name("Scatter Merged");
    std::printf("    merged: %zu faces against %zu x %zu\n", merged && merged->mesh_filter() && merged->mesh_filter()->mesh ? merged->mesh_filter()->mesh->face_count() : (size_t)0, n, sphere_faces);
    CHECK(merged && merged->mesh_filter() && merged->mesh_filter()->mesh && merged->mesh_filter()->mesh->face_count() == n * sphere_faces);
    ed.command("undo");
    /* The eraser: everything within the disc goes. */
    ed.command(strprintf("scatter erase %g %g", px.x, px.y));
    parent = ed.scatter_parent_for_test();
    std::printf("    after the eraser: %zu instance(s)\n", parent ? parent->children.size() : (size_t)0);
    CHECK(parent && parent->children.empty());
    /* The mask: the ground's surface with a Paint layer filled over the object keeps every instance off. */
    ed.command("select Plane");
    ed.step_frame_headless();
    ed.command("surface new GroundSurf");
    ed.step_frame_headless();
    ed.command("paint on");
    CHECK(ed.paint_for_test());
    if (ed.paint_for_test()) {
      ed.command("paint fill object");
      ed.command("paint off");
      ed.command("scatter mask 1");  // (the Paint layer sits above the surface's Fill)
      {
        std::string why;
        GameObject *gr = ed.scene().find_by_name("Plane");
        const bool m = gr && ed.scatter_masked_for_test(*gr, spot, why);
        std::printf("    the mask at the spot: %d (%s)\n", (int)m, why.c_str());
      }
      ed.command(strprintf("scatter dab %g %g", px.x, px.y));
      parent = ed.scatter_parent_for_test();
      std::printf("    with the painted mask: %zu instance(s)\n", parent ? parent->children.size() : (size_t)0);
      CHECK(parent && parent->children.empty());
      ed.command("scatter mask off");
    }
    ed.command("scatter off");
  });
}

/* The HCI measuring sticks (ADR 0085, task 0251): the widget rectangles every window and tool mode records, dumped for
 * tools/hci_fitts_audit.py; the status bar's hint over every mode and state. Both print their numbers; the thresholds
 * become assertions as the rounds fix what they find. */
static std::string hci_dump_dir() {  // (BLENDITY_HCI_DUMP_DIR, else the test's own folder: nothing left in the shared project)
  const char *e = std::getenv("BLENDITY_HCI_DUMP_DIR");
  const std::string d = e && *e ? std::string(e) : fs::join(test_dir(), "hci");
  fs::make_dirs(d);
  return d;
}

static std::string json_escape(const std::string &s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') o += '\\', o += (char)c;
    else if (c < 32) o += strprintf("\\u%04x", (int)c);
    else o += (char)c;
  }
  return o;
}

static void tests_round271() {
  test("hci: the Fitts dump - every window's and tool mode's recorded widget rectangles written as JSON at UI scales 1.0 and 1.5 for tools/hci_fitts_audit.py (the counts printed; the audit's threshold becomes an assertion in the area rounds)", [] {
    for (float scale : {1.0f, 1.5f}) {
      Editor ed;
      ed.init_headless(1400, 900);
      ed.set_clock_for_test(100.0);
      if (scale != 1.0f) ed.command("window tablet on");  // (Tablet Mode: the UI at 1.5x, as a 2-in-1 shows it)
      for (int i = 0; i < 5; i++) ed.step_frame_headless();
      ed.record_buttons_for_test(true);
      std::string json = "{\"scale\":" + strprintf("%.1f", scale) + ",";
      {
        const Recti v = ed.scene_rect_for_test();
        json += strprintf("\"scene\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},", v.x, v.y, v.w, v.h);
      }
      json += "\"contexts\":{";
      size_t contexts = 0, targets = 0;
      auto snapshot = [&](const std::string &context) {
        if (std::getenv("BLENDITY_HCI_TRACE")) std::printf("    snapshot %s\n", context.c_str()), std::fflush(stdout);
        for (int i = 0; i < 3; i++) ed.step_frame_headless();
        const auto &rects = ed.ui_for_test().button_rects;
        if (rects.empty()) return;
        if (contexts++) json += ",";
        json += "\"" + json_escape(context) + "\":{";
        bool first = true;
        for (const auto &kv : rects) {
          if (kv.first.rfind("file:", 0) == 0 || kv.first.rfind("mat:", 0) == 0) continue;  // (the project's and the materials' tiles: what the scratch project holds, not the editor's controls)
          json += (first ? "" : ",") + ("\"" + json_escape(kv.first) + "\":") + strprintf("{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}", kv.second.x, kv.second.y, kv.second.w, kv.second.h);
          first = false;
          targets++;
        }
        json += "}";
        ed.record_buttons_for_test(false);
        ed.record_buttons_for_test(true);
      };
      /* Every window, opened through the Window menu's command. */
      for (int k = 0; k < (int)WindowKind::Count; k++) {
        const std::string title = window_title((WindowKind)k);
        if (title.empty()) continue;
        if (std::getenv("BLENDITY_HCI_TRACE")) std::printf("    opening %s\n", title.c_str()), std::fflush(stdout);
        ed.command("window " + title);
        snapshot("window:" + title);
      }
      /* The tool modes on the sample scene's objects, with the Modeling Tools window open (the sections hold the brush controls). */
      if (std::getenv("BLENDITY_HCI_TRACE")) std::printf("    modes\n"), std::fflush(stdout);
      ed.command("layout Default");
      ed.command("window Modeling Tools");
      ed.command("alltools on");  // (every section, whatever the layout's stage: the brush controls are what the audit is for)
      ed.command("select Cube");
      snapshot("mode:object");
      ed.command("edit");
      snapshot("mode:edit");
      ed.command("edit off");
      ed.command("sculpt on");
      snapshot("mode:sculpt");
      const bool sculpt_controls = json.find("\"field:Size\"") != std::string::npos && json.find("\"field:Strength\"") != std::string::npos;  // (Modeling Tools' Sculpt section is in the dump)
      if (!sculpt_controls) std::printf("    (no field:Size / field:Strength in the sculpt snapshot; the Tools window %dx%d, sculpt %d)\n", ed.window_rect_for_test(WindowKind::Tools).w, ed.window_rect_for_test(WindowKind::Tools).h, (int)ed.sculpt_for_test());
      ed.command("sculpt off");
      ed.command("surface new FittsSurf");
      ed.command("paint on");
      snapshot("mode:paint");
      ed.command("paint off");
      CHECK(sculpt_controls);
      ed.command("strokeobj on");
      snapshot("mode:strokes");
      ed.command("strokeobj off");
      ed.command("scatter Sphere");
      ed.command("scatter on");
      snapshot("mode:scatter");
      ed.command("scatter off");
      json += "}}";
      const std::string path = fs::join(hci_dump_dir(), strprintf("fitts-%.1f.json", scale));
      CHECK(fs::write_file(path, json));
      std::printf("    scale %.1f: %zu contexts, %zu targets -> %s\n", scale, contexts, targets, path.c_str());
      CHECK(contexts >= 20 && targets > 100);
    }
  });

  test("hci: the status bar's hint over every tool mode and selection state (a table printed for docs/hci/hints-<round>.md; the holes are the baseline round 274 fills)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    struct Mode {
      const char *name, *on, *off, *word;  // (word: what a hint of the mode's own names)
    };
    const Mode modes[] = {{"object", "", "", "Move"}, {"edit", "edit", "edit off", "Edit"}, {"sculpt", "sculpt on", "sculpt off", "Sculpt"}, {"paint", "paint on", "paint off", "Paint"}, {"strokes", "strokeobj on", "strokeobj off", "Stroke"}, {"scatter", "scatter on", "scatter off", "Scatter"}, {"sketch", "sketch on", "sketch off", "Sketch"}};
    ed.command("select Cube");
    ed.command("surface new HintSurf");
    ed.command("scatter Sphere");
    int holes = 0, cells = 0;
    std::printf("    | mode | nothing selected | the Cube selected |\n    |---|---|---|\n");
    for (const Mode &m : modes) {
      std::string cell[2];
      for (int sel = 0; sel < 2; sel++) {
        if (sel) ed.command("select Cube"); else ed.clear_selection_for_test();
        if (!sel && *m.on) {  // (a tool mode needs a selection to turn on: not a hint's hole, a refusal's)
          cell[sel] = "n/a (needs a selection)";
          continue;
        }
        if (*m.on) ed.command(m.on);
        for (int i = 0; i < 2; i++) ed.step_frame_headless();
        ed.set_scene_hovered_for_test(true);  // (a frame resets the hover: read the hint at once, as the pointer over the view would)
        std::string h = ed.statusbar_hint_for_test();
        if (*m.off) ed.command(m.off);
        const bool generic = h.find(m.word) == std::string::npos;  // (a hole: the hint does not name the mode)
        cells++, holes += generic ? 1 : 0;
        if (h.size() > 60) h = h.substr(0, 57) + "...";
        for (char &c : h)
          if (c == '|') c = '/';
        cell[sel] = generic ? "**(none)** " + h : h;
      }
      std::printf("    | %s | %s | %s |\n", m.name, cell[0].c_str(), cell[1].c_str());
    }
    std::printf("    %d of %d cells have no hint of their own\n", holes, cells);
    CHECK(cells == 8);  // (object both ways; the six tool modes with the Cube selected)
    CHECK(holes == 0);  // (round 274's hint table: every mode names itself)
  });
}

static void tests_round272() {
  test("toasts (ADR 0085 F1): a refusal becomes a toast over the Scene view; one that knows its remedy carries a Fix button that runs the command; the same message twice is one toast with a count; a toast leaves after three seconds; Generate Lighting and the Editing window's Export are disabled with their reasons; the HCI log receives the events", [] {
    const std::string hci = fs::join(scratch_project(), "hci-272.log");
    fs::remove_file(hci);
#ifdef _WIN32
    _putenv_s("BLENDITY_HCI_LOG", hci.c_str());
#else
    setenv("BLENDITY_HCI_LOG", hci.c_str(), 1);
#endif
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    for (int i = 0; i < 5; i++) ed.step_frame_headless();
    ed.record_buttons_for_test(true);
    /* A refusal with no remedy: a toast with the text, no fix. */
    ed.clear_selection_for_test();
    ed.command("paint on");
    ed.step_frame_headless();
    const auto &toasts = ed.toasts_for_test();
    std::printf("    after 'paint on' with nothing selected: %zu toast(s)%s\n", toasts.size(), toasts.empty() ? "" : (": \"" + toasts[0].text + "\"").c_str());
    CHECK(toasts.size() == 1 && toasts[0].fix_cmd.empty() && toasts[0].text.find("select") != std::string::npos);
    /* The same refusal again within the window: one toast, counted. */
    ed.command("paint on");
    ed.step_frame_headless();
    if (!(toasts.size() == 1 && toasts[0].count == 2))
      for (const auto &t : toasts) std::printf("    (toast: \"%s\" x%d)\n", t.text.c_str(), t.count);
    CHECK(toasts.size() == 1 && toasts[0].count == 2);
    /* It leaves after three seconds: there at 2.9 s, gone at 3.6 s. */
    ed.set_clock_for_test(102.9);
    ed.step_frame_headless();
    CHECK(toasts.size() == 1);
    ed.set_clock_for_test(103.6);
    ed.step_frame_headless();
    ed.set_clock_for_test(104.2);
    ed.step_frame_headless();
    std::printf("    3.6 s later: %zu toast(s)\n", toasts.size());
    CHECK(toasts.empty());
    /* A refusal that knows its remedy: the cube has no surface; the Fix button makes one and Paint Mode then opens. */
    ed.command("select Cube");
    ed.command("paint on");
    ed.step_frame_headless();
    CHECK(toasts.size() == 1 && toasts[0].fix_cmd == "surface new" && toasts[0].fix_label == "make a surface");
    std::printf("    with the cube and no surface: %zu toast(s), fix '%s'\n", toasts.size(), toasts.empty() ? "" : toasts[0].fix_cmd.c_str());
    auto click_label = [&](const std::string &label) {  // (a click on a recorded widget by its label, through real events)
      for (int attempt = 0; attempt < 3; attempt++) {
        ed.step_frame_headless();
        const auto &br = ed.ui_for_test().button_rects;
        if (!br.count(label)) continue;
        const Recti r = br.at(label);
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
        return true;
      }
      return false;
    };
    std::string why_none;
    const bool clicked = click_label("Fix: make a surface");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const bool fixed = clicked && ed.surface_of_for_test(ed.active_object_for_test(), &why_none) != nullptr;
    if (!fixed) {
      std::string lg;
      fs::read_file(hci, lg);
      std::printf("    (fix click %d, toasts %zu, why '%s'; the HCI log so far:\n%s)\n", (int)clicked, toasts.size(), why_none.c_str(), lg.c_str());
    }
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    CHECK(fixed && toasts.empty());
    ed.command("paint on");
    ed.step_frame_headless();
    std::printf("    after the fix: Paint Mode %d\n", (int)ed.paint_for_test());
    CHECK(ed.paint_for_test());
    ed.command("paint off");
    /* Disabled with a reason: Generate Lighting during Play, the Editing window's Export with nothing open. */
    ed.command("toast a toast by hand");
    ed.step_frame_headless();
    CHECK(!toasts.empty() && toasts[0].text == "a toast by hand");
    /* Hovered, a toast waits: the pointer kept on it through four seconds, it is still there; moved off, it leaves. */
    ed.set_clock_for_test(110.0);
    ed.command("toast a toast that waits");
    ed.step_frame_headless();
    {
      const auto &br = ed.ui_for_test().button_rects;
      const bool recorded = br.count("toast:a toast that waits") > 0;
      const Recti r = recorded ? br.at("toast:a toast that waits") : Recti{};
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.set_clock_for_test(112.0);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.set_clock_for_test(114.0);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      const auto waits = [](const Editor::Toast &t) { return t.text == "a toast that waits"; };
      const bool kept = recorded && std::any_of(toasts.begin(), toasts.end(), waits);
      std::printf("    hovered for 4 s: frame recorded %d, still shown %d\n", (int)recorded, (int)kept);
      CHECK(kept);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, r.x + r.w + 40, r.y + r.h + 40)});
      ed.set_clock_for_test(118.0);
      ed.step_frame_headless();
      const bool gone = std::none_of(toasts.begin(), toasts.end(), waits);
      std::printf("    4 s after the pointer left: gone %d\n", (int)gone);
      CHECK(gone);
    }
    ed.command("layout Lighting & Rendering");  // (the layout that shows the Lighting window; `window Lighting` alone folds it behind its tab group)
    ed.command("window Lighting");
    ed.command("play");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    {
      const Recti lw = ed.window_rect_for_test(WindowKind::Lighting);
      std::printf("    (Lighting window %dx%d, Generate Lighting drawn %d, status '%s')\n", lw.w, lw.h, (int)ed.ui_for_test().button_rects.count("Generate Lighting"), ed.statusbar_text_for_test().substr(0, 40).c_str());
    }
    const std::string why_play = ed.button_disabled_why_for_test("Generate Lighting");
    {  /* The reason is the button's tooltip: the pointer on it through the tooltip's delay. */
      const auto &br = ed.ui_for_test().button_rects;
      const Recti gr = br.count("Generate Lighting") ? br.at("Generate Lighting") : Recti{};
      const int cx = gr.x + gr.w / 2, cy = gr.y + gr.h / 2;
      ed.set_clock_for_test(120.0);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.set_clock_for_test(120.8);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      const std::string tip = ed.ui_for_test().tooltip_text_for_test();
      std::printf("    Generate Lighting hovered in Play, the tooltip: '%s'\n", tip.c_str());
      CHECK(tip.find("playing") != std::string::npos);
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, 2, 2)});
    }
    ed.command("stop");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const std::string why_stopped = ed.button_disabled_why_for_test("Generate Lighting");
    std::printf("    Generate Lighting in Play: '%s'; after Stop: '%s'\n", why_play.c_str(), why_stopped.c_str());
    CHECK(why_play.find("playing") != std::string::npos);
    CHECK(why_stopped.empty() || why_stopped.find("Baked Global Illumination") != std::string::npos);
    const std::string why_clear = ed.button_disabled_why_for_test("Clear Baked Data");
    std::printf("    Clear Baked Data with nothing baked: '%s'\n", why_clear.c_str());
    CHECK(why_clear.find("Nothing baked") != std::string::npos);
    ed.command("window Editing");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const std::string why_export = ed.button_disabled_why_for_test("Export...");
    std::printf("    Editing > Export with nothing open: '%s'\n", why_export.c_str());
    CHECK(why_export.find("Nothing open") != std::string::npos);
    /* The Strokes and Scatter mode buttons with nothing selected. */
    ed.command("layout Default");
    ed.command("window Modeling Tools");
    ed.command("alltools on");
    ed.clear_selection_for_test();
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const std::string why_strokes = ed.button_disabled_why_for_test("Strokes"), why_scatter = ed.button_disabled_why_for_test("Scatter");
    std::printf("    nothing selected: Strokes '%s', Scatter '%s'\n", why_strokes.c_str(), why_scatter.c_str());
    CHECK(why_strokes.find("Select") != std::string::npos && why_scatter.find("Select") != std::string::npos);
    /* The HCI log. */
    std::string log;
    CHECK(fs::read_file(hci, log) && log.find("toast\tPaint:") != std::string::npos && log.find("fix\tsurface new") != std::string::npos && log.find("hint\t") != std::string::npos);
    std::printf("    the HCI log: %zu lines\n", (size_t)std::count(log.begin(), log.end(), '\n'));
#ifdef _WIN32
    _putenv_s("BLENDITY_HCI_LOG", "");
#else
    unsetenv("BLENDITY_HCI_LOG");
#endif
  });
}

static void tests_round273() {
  test("the job tray (ADR 0085 F2): a Mesh Maps bake is in the status bar's tray from its first frame with its start time and a Cancel, its progress moves (the frames until it does printed), its end raises an Info toast with the time and the HCI log has the start and done lines; a second bake cancelled through the tray's Cancel button lands nothing; `jobs` lists the running jobs", [] {
    const std::string hci = fs::join(scratch_project(), "hci-273.log");
    fs::remove_file(hci);
#ifdef _WIN32
    _putenv_s("BLENDITY_HCI_LOG", hci.c_str());
#else
    setenv("BLENDITY_HCI_LOG", hci.c_str(), 1);
#endif
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(200.0);
    ed.record_buttons_for_test(true);
    ed.command("select Sphere");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    for (int i = 0; i < 10; i++) ed.step_frame_headless();  // (the cold frames)
    const auto &toasts = ed.toasts_for_test();
    auto has_toast = [&](const char *words) {
      return std::any_of(toasts.begin(), toasts.end(), [&](const Editor::Toast &t) { return t.text.find(words) != std::string::npos; });
    };
    double clock = 200.0;
    bool saw_partial = false;  // (a progress strictly between 0 and 1 seen in the tray)
    auto wait = [&](int *first_progress) {  // (the frames while the worker runs; the test clock 0.1 s a frame)
      for (int i = 0; i < 20000 && ed.meshmaps_busy_for_test(); i++) {
        clock += 0.1;
        ed.set_clock_for_test(clock);
        ed.step_frame_headless();
        const std::vector<Editor::JobView> jobs = ed.jobs_for_test();
        if (first_progress && *first_progress < 0 && !jobs.empty() && jobs[0].progress > 0.0f) *first_progress = i;
        saw_partial = saw_partial || (!jobs.empty() && jobs[0].progress > 0.0f && jobs[0].progress < 1.0f);
        if (ed.meshmaps_busy_for_test()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      return !ed.meshmaps_busy_for_test();
    };
    ed.command("meshmaps 1024 16");
    ed.step_frame_headless();
    {
      const std::vector<Editor::JobView> jobs = ed.jobs_for_test();
      std::printf("    after the first frame: %zu job(s)%s\n", jobs.size(),
                  jobs.empty() ? "" : strprintf(" - '%s' started at %.1f, progress %.2f", jobs[0].label.c_str(), jobs[0].started, jobs[0].progress).c_str());
      CHECK(jobs.size() == 1 && jobs[0].label == "Mesh Maps" && jobs[0].started == 200.0 && jobs[0].cancellable);
      CHECK(ed.ui_for_test().button_rects.count("job:Cancel") > 0 && ed.ui_for_test().button_rects.count("job:tray") > 0);
    }
    int first_progress = -1;
    CHECK(wait(&first_progress));
    ed.step_frame_headless();
    std::printf("    the progress first moved after %d frame(s), a partial value seen %d; %zu job(s) in the tray after the bake\n", first_progress, (int)saw_partial, ed.jobs_for_test().size());
    CHECK(saw_partial);
    for (const auto &t : toasts) std::printf("    (toast: \"%s\")\n", t.text.c_str());
    CHECK(ed.jobs_for_test().empty());
    CHECK(has_toast("Mesh Maps done in"));
    const GameObject *G = ed.scene().find(gid);
    CHECK(G && G->get<MeshRenderer>() && !G->get<MeshRenderer>()->mesh_maps.empty());
    /* The second bake, larger, cancelled through the tray's button (a real click on its recorded rectangle). */
    const size_t steps = ed.undo_steps_for_test();
    ed.command("meshmaps 2048 16");
    ed.step_frame_headless();
    const auto &br = ed.ui_for_test().button_rects;
    int attempts = 0;
    bool cancel_sent = false;
    for (; attempts < 5 && !cancel_sent && ed.meshmaps_busy_for_test(); attempts++) {  // (the click re-aimed each try: the frame under load)
      if (!br.count("job:Cancel")) {
        ed.step_frame_headless();
        continue;
      }
      const Recti r = br.at("job:Cancel");
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      std::string so_far;
      cancel_sent = fs::read_file(hci, so_far) && so_far.find("job\tcancel Mesh Maps") != std::string::npos;
    }
    std::printf("    the tray's Cancel: %d attempt(s), registered %d, the bake still running %d\n", attempts, (int)cancel_sent, (int)ed.meshmaps_busy_for_test());
    CHECK(cancel_sent);
    CHECK(wait(nullptr));
    ed.step_frame_headless();
    std::printf("    after Cancel: %zu job(s), undo steps %zu (were %zu)\n", ed.jobs_for_test().size(), ed.undo_steps_for_test(), steps);
    for (const auto &t : toasts) std::printf("    (toast: \"%s\")\n", t.text.c_str());
    CHECK(ed.jobs_for_test().empty() && ed.undo_steps_for_test() == steps);
    CHECK(has_toast("Mesh Maps cancelled in"));
    ed.command("jobs");
    ed.step_frame_headless();
    std::string log;
    CHECK(fs::read_file(hci, log) && log.find("job\tstart Mesh Maps") != std::string::npos && log.find("job\tdone Mesh Maps") != std::string::npos &&
          log.find("job\tcancel Mesh Maps") != std::string::npos && log.find("job\tcancelled Mesh Maps") != std::string::npos);
    std::printf("    the HCI log: %zu lines\n", (size_t)std::count(log.begin(), log.end(), '\n'));
#ifdef _WIN32
    _putenv_s("BLENDITY_HCI_LOG", "");
#else
    unsetenv("BLENDITY_HCI_LOG");
#endif
  });
}

static void tests_round274() {
  test("the History window (ADR 0085 F6): the status bar's Last opens it; four changes list as four steps with the last current; a click on the first jumps back three steps, a click on the last undone step redoes them", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(300.0);
    ed.command("select Cube");
    ed.command("layout Default");
    ed.mark_changed_for_test("Step zero");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    {  /* The status bar's "Last:" opens the window. */
      const auto &br = ed.ui_for_test().button_rects;
      const bool last = br.count("status:Last") > 0;
      if (last) {
        const Recti r = br.at("status:Last");
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      }
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      const Recti hw = ed.window_rect_for_test(WindowKind::History);
      std::printf("    'Last:' recorded %d; the History window %dx%d after the click\n", (int)last, hw.w, hw.h);
      CHECK(last && hw.w > 0);
    }
    for (const char *label : {"Step one", "Step two", "Step three"}) {
      ed.mark_changed_for_test(label);
      ed.step_frame_headless();
    }
    Editor::HistoryRows rows = ed.history_rows_for_test();
    std::printf("    after three changes: %zu past, %zu future; the Hierarchy's leaf shows History %d\n", rows.past.size(), rows.future.size(), (int)(ed.ui_for_test().button_rects.count("history:0") > 0));
    CHECK(rows.past.size() == 4 && rows.future.empty() && rows.past[3] == "Step three");
    auto click = [&](const std::string &key) {
      const auto &br = ed.ui_for_test().button_rects;
      if (!br.count(key)) return false;
      const Recti r = br.at(key);
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      return true;
    };
    CHECK(click("history:0"));
    rows = ed.history_rows_for_test();
    std::printf("    after a click on the first step: %zu past, %zu future (undo steps %zu)\n", rows.past.size(), rows.future.size(), ed.undo_steps_for_test());
    CHECK(rows.past.size() == 1 && rows.future.size() == 3 && ed.undo_steps_for_test() == 1);
    CHECK(click("history:redo:2"));
    rows = ed.history_rows_for_test();
    std::printf("    after a click on the last undone step: %zu past, %zu future\n", rows.past.size(), rows.future.size());
    CHECK(rows.past.size() == 4 && rows.future.empty());
  });
  test("the hint table (ADR 0085 F3): the rows are listed by name; the row showing follows the state - Object Mode's tools, Edit Mode, Sculpt, Paint, Strokes, Scatter, the UV editor under the pointer, elsewhere the palette's; the shortcuts in the texts come from the keymap", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(320.0);
    const std::vector<std::string> names = ed.hint_row_names_for_test();
    std::printf("    %zu hint rows: ", names.size());
    for (const std::string &n : names) std::printf("%s; ", n.c_str());
    std::printf("\n");
    CHECK(names.size() >= 26 && names.back() == "object tools");
    ed.command("select Cube");
    ed.step_frame_headless();
    auto row_hovered = [&] {
      ed.set_scene_hovered_for_test(true);
      return ed.statusbar_hint_row_for_test();
    };
    struct Case {
      const char *on, *off, *row;
    };
    const Case cases[] = {{"", "", "object tools"}, {"edit", "edit off", "edit mode"}, {"sculpt on", "sculpt off", "sculpt"}, {"paint on", "paint off", "paint"},
                          {"strokeobj on", "strokeobj off", "strokes"}, {"scatter on", "scatter off", "scatter"}};
    ed.command("surface new HintSurf");
    ed.command("scatter Sphere");
    ed.step_frame_headless();
    for (const Case &c : cases) {
      if (*c.on) ed.command(c.on);
      ed.step_frame_headless();
      const std::string row = row_hovered();
      std::printf("    %-12s -> row '%s'\n", *c.on ? c.on : "(object)", row.c_str());
      CHECK(row == c.row);
      if (*c.off) ed.command(c.off);
      ed.step_frame_headless();
    }
    ed.set_scene_hovered_for_test(false);
    const std::string elsewhere = ed.statusbar_hint_for_test();
    std::printf("    elsewhere: row '%s': %s\n", ed.statusbar_hint_row_for_test().c_str(), elsewhere.c_str());
    CHECK(ed.statusbar_hint_row_for_test() == "elsewhere" && elsewhere.find("find any command") != std::string::npos);
    ed.set_binding("edit.palette", {KeyChord{platform::KEY_K, platform::MOD_CTRL | platform::MOD_SHIFT}});  // (the hint follows the keymap)
    ed.step_frame_headless();
    const std::string rebound = ed.statusbar_hint_for_test();
    std::printf("    with the palette rebound: %s\n", rebound.c_str());
    CHECK(rebound.find("Ctrl+Shift+K: find any command") != std::string::npos);
    ed.command("keymap Unity");
    ed.command("layout Texturing");
    ed.command("window UV Editor");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti uv = ed.window_rect_for_test(WindowKind::UVEditor);
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, uv.x + uv.w / 2, uv.y + uv.h / 2)});
    ed.set_scene_hovered_for_test(false);
    std::printf("    the UV editor %dx%d under the pointer: row '%s'\n", uv.w, uv.h, ed.statusbar_hint_row_for_test().c_str());
    CHECK(uv.w > 0 && ed.statusbar_hint_row_for_test() == "uv editor");
  });
  test("labels from the keymap (ADR 0085 F4): the Edit menu's shortcuts beside Select All, Duplicate, Delete and Frame Selected are the preset's chords under Unity and under Blender; the Window menu's too; the label audit's inventory is for the chain", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(330.0);
    ed.step_frame_headless();
    auto open_menu = [&](const char *name) {
      const auto &br = ed.ui_for_test().button_rects;
      const std::string key = std::string("menubar:") + name;
      if (!br.count(key)) return false;
      const Recti r = br.at(key);
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      ed.step_frame_headless();
      return true;
    };
    struct Row {
      const char *label, *action;
    };
    const Row rows[] = {{"Select All", "edit.select_all"}, {"Duplicate", "edit.duplicate"}, {"Delete", "edit.delete"}, {"Frame Selected", "view.frame"}};
    for (const char *preset : {"Unity", "Blender"}) {
      ed.command(std::string("keymap ") + preset);
      ed.step_frame_headless();
      CHECK(open_menu("Edit"));
      for (const Row &row : rows) {
        const std::string drawn = ed.menu_shortcut_for_test(row.label), want = ed.shortcut_text_for_test(row.action);
        std::printf("    %-8s %-15s drawn '%s', the keymap's '%s'\n", preset, row.label, drawn.c_str(), want.c_str());
        CHECK(drawn == want);
      }
      ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
      ed.step_frame_headless();
      CHECK(open_menu("Window"));
      const std::string hier = ed.menu_shortcut_for_test("Hierarchy"), want_h = ed.shortcut_text_for_test("window.hierarchy");
      std::printf("    %-8s Window > Hierarchy drawn '%s', the keymap's '%s'\n", preset, hier.c_str(), want_h.c_str());
      CHECK(hier == want_h);
      ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
      ed.step_frame_headless();
    }
    ed.command("keymap Unity");
    std::printf("    the Mesh menu's rows: extrude -> '%s' (the keymap's '%s'), select_loop -> '%s'\n", ed.op_keys_for_test("extrude").c_str(), ed.shortcut_text_for_test("mesh.extrude").c_str(), ed.op_keys_for_test("select_loop").c_str());
    CHECK(ed.op_keys_for_test("extrude") == ed.shortcut_text_for_test("mesh.extrude") && !ed.op_keys_for_test("extrude").empty() && ed.op_keys_for_test("select_loop") == "Double-click");
  });
  test("unsaved changes (ADR 0085 F5): New Scene with a changed scene asks - Cancel keeps it, Don't Save replaces it, Save with a file writes it then replaces; Exit during Play leaves Play before the auto-save, so the file holds the pre-Play edits (Play's own changes are discarded, as Unity does)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(310.0);
    auto click = [&](const std::string &key) {
      const auto &br = ed.ui_for_test().button_rects;
      if (!br.count(key)) return false;
      const Recti r = br.at(key);
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      return true;
    };
    ed.command("select Cube");
    ed.mark_changed_for_test("An edit");
    ed.step_frame_headless();
    CHECK(ed.scene_dirty_for_test());
    ed.new_scene_asked();
    ed.step_frame_headless();
    std::printf("    New Scene asked with a dirty scene: the prompt open %d, undo steps %zu\n", (int)ed.unsaved_dialog_open_for_test(), ed.undo_steps_for_test());
    CHECK(ed.unsaved_dialog_open_for_test() && ed.undo_steps_for_test() == 1);
    CHECK(click("unsaved:Cancel"));
    CHECK(!ed.unsaved_dialog_open_for_test() && ed.undo_steps_for_test() == 1 && ed.scene_dirty_for_test());
    ed.step_frame_headless();  // (a frame with no dialog open: the next ask is a fresh popup, as a user's would be)
    ed.new_scene_asked();
    ed.step_frame_headless();
    CHECK(click("unsaved:Don't Save"));
    std::printf("    after Don't Save: undo steps %zu, dirty %d, scene '%s'\n", ed.undo_steps_for_test(), (int)ed.scene_dirty_for_test(), ed.scene().name.c_str());
    CHECK(ed.undo_steps_for_test() == 0 && !ed.scene_dirty_for_test());
    /* Save, with a file: the scene is written, then replaced. */
    const std::string path = fs::join(scratch_project(), "Scenes/t274.scene");
    fs::make_dirs(fs::parent(path));
    fs::remove_file(path);
    ed.scene().path = path;
    ed.scene().name = "t274";
    ed.command("select Cube");
    ed.mark_changed_for_test("Another edit");
    ed.step_frame_headless();
    ed.new_scene_asked();
    ed.step_frame_headless();
    CHECK(click("unsaved:Save"));
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    after Save: the file exists %d, undo steps %zu, scene '%s'\n", (int)fs::exists(path), ed.undo_steps_for_test(), ed.scene().name.c_str());
    CHECK(fs::exists(path) && ed.undo_steps_for_test() == 0 && ed.scene().name == "Untitled");
    /* Exit during Play: Play ends first, then the auto-save. */
    ed.scene().path = path;
    ed.scene().name = "t274";
    ed.command("select Cube");
    ed.mark_changed_for_test("Before Play");
    ed.step_frame_headless();
    ed.command("play");
    ed.step_frame_headless();
    CHECK(ed.playing_for_test());
    const bool before = fs::exists(path);
    fs::remove_file(path);
    ed.request_close_for_test();
    std::printf("    Exit during Play: playing after %d, the scene written %d (existed before %d)\n", (int)ed.playing_for_test(), (int)fs::exists(path), (int)before);
    CHECK(!ed.playing_for_test() && fs::exists(path));
  });
}

static void tests_round275() {
  test("modeling, placing and UV (ADR 0085, task 0255): the Hierarchy's drop zones are thirds; the edit operations' new names; ] and [ set a bevel modal's segments as the wheel does", [] {
    std::printf("    drop zones of a 30 px row: %d %d %d %d %d (0 into, 1 before, 2 after)\n", hier_drop_mode(0, 30), hier_drop_mode(9, 30), hier_drop_mode(15, 30), hier_drop_mode(21, 30), hier_drop_mode(29, 30));
    CHECK(hier_drop_mode(0, 30) == 1 && hier_drop_mode(9, 30) == 1 && hier_drop_mode(10, 30) == 0 && hier_drop_mode(15, 30) == 0 && hier_drop_mode(20, 30) == 0 && hier_drop_mode(21, 30) == 2 && hier_drop_mode(29, 30) == 2);
    const EditOpInfo *ee = find_edit_op("extrude_edges"), *of = find_edit_op("select_overlapping"), *dv = find_edit_op("select_overlaps");
    CHECK(ee && std::string(ee->label) == "Extrude Edges" && of && std::string(of->label) == "Select Overlapping Faces" && dv && std::string(dv->label) == "Select Doubled Vertices");
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(400.0);
    ed.command("select Cube");
    ed.command("edit");
    ed.command("edit edge all");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const Recti view = ed.scene_view_rect();
    const int cx = view.x + view.w / 2, cy = view.y + view.h / 2;
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
    ed.step_frame_headless({key_ev(platform::KEY_B, platform::MOD_CTRL)});  // (Unity's Bevel chord starts the modal over the view)
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx + 40, cy)});
    const bool modal = ed.modal_active_for_test();
    const int segs0 = ed.last_op_segments_for_test();
    ed.step_frame_headless({key_ev(platform::KEY_RBRACKET)});
    ed.step_frame_headless({key_ev(platform::KEY_RBRACKET)});
    const int segs2 = ed.last_op_segments_for_test();
    ed.step_frame_headless({key_ev(platform::KEY_LBRACKET)});
    const int segs1 = ed.last_op_segments_for_test();
    std::printf("    the bevel modal started %d; segments %d -> %d after ] twice -> %d after [\n", (int)modal, segs0, segs2, segs1);
    CHECK(modal && segs2 == segs0 + 2 && segs1 == segs0 + 1);
    ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
    ed.step_frame_headless();
    CHECK(!ed.modal_active_for_test());
  });
  test("Boolean by name (task 0255): the object kept is chosen, not the one selected last - Cube kept and cut by the Sphere gives the same mesh whichever was selected last; the UV editor's hint names the keymap's tool chords", [] {
    size_t faces[2] = {0, 0};
    for (int order = 0; order < 2; order++) {
      Editor ed;
      ed.init_headless(1200, 800);
      const GameObject *cube = ed.scene().find_by_name("Cube"), *sphere = ed.scene().find_by_name("Sphere");
      CHECK(cube && sphere);
      if (!cube || !sphere) return;
      const uint64_t cube_id = cube->id;
      ed.scene().find(sphere->id)->set_world_position(ed.scene().find(cube_id)->world_position() + Vec3(0.5f, 0.5f, 0.5f));
      ed.select_for_test(order == 0 ? cube_id : sphere->id, false);
      ed.select_for_test(order == 0 ? sphere->id : cube_id, true);
      ed.step_frame_headless();
      if (order == 0) ed.boolean_for_test(0, true, cube_id);
      else ed.command("boolean difference kept Cube");  // (the console's form)
      ed.step_frame_headless();
      const GameObject *kept = ed.scene().find(cube_id);
      faces[order] = kept && kept->get<MeshFilter>() && kept->get<MeshFilter>()->mesh ? kept->get<MeshFilter>()->mesh->face_count() : 0;
      std::printf("    %s selected last: the Cube kept with %zu faces, the Sphere %s\n", order == 0 ? "Sphere" : "Cube", faces[order], ed.scene().find_by_name("Sphere") ? "still there" : "removed");
    }
    CHECK(faces[0] > 6 && faces[0] == faces[1]);
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("layout Texturing");
    ed.command("window UV Editor");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti uv = ed.window_rect_for_test(WindowKind::UVEditor);
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, uv.x + uv.w / 2, uv.y + uv.h / 2)});
    ed.command("keymap Blender");
    ed.step_frame_headless();
    ed.set_scene_hovered_for_test(false);
    const std::string blender_hint = ed.statusbar_hint_for_test();
    ed.command("keymap Unity");
    ed.step_frame_headless();
    ed.set_scene_hovered_for_test(false);
    const std::string unity_hint = ed.statusbar_hint_for_test();
    std::printf("    the UV editor's hint under Unity: %s\n    under Blender: %s\n", unity_hint.c_str(), blender_hint.c_str());
    CHECK(uv.w > 0 && unity_hint.find("W E R: move, turn, scale") != std::string::npos && blender_hint != unity_hint);
  });
}

static void tests_round276() {
  test("lighting and rendering in one place (ADR 0085, task 0256): the Render window points at the Lighting window for the Environment (a click opens it); its Render Devices section carries the Viewport and GI device rows; Add Component lists the Rendering components in a modeling layout", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(500.0);
    ed.command("layout Default");  // (a layout without the Lighting window: the click must bring it)
    ed.command("window Render");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto &br = ed.ui_for_test().button_rects;
    const bool open_lighting = br.count("Open Lighting") > 0, sky_field = br.count("field:Sky Color") > 0, lighting_before = ed.window_rect_for_test(WindowKind::Lighting).w > 0;
    std::printf("    Render window: Open Lighting button %d, an Environment field of its own %d, the Lighting window shown before the click %d\n", (int)open_lighting, (int)sky_field, (int)lighting_before);
    CHECK(open_lighting && !sky_field && !lighting_before);
    auto click = [&](const std::string &key) {
      if (!br.count(key)) return false;
      const Recti r = br.at(key);
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      ed.step_frame_headless();
      return true;
    };
    {  /* The Render window is short in this layout: scroll its settings column until the button is in view. */
      const Recti rw = ed.window_rect_for_test(WindowKind::Render);
      int scrolls = 0;
      for (; scrolls < 60 && br.count("Open Lighting") && br.at("Open Lighting").bottom() > rw.bottom() - 4; scrolls++) {
        platform::Event w;
        w.type = platform::EventType::Wheel;
        w.x = rw.x + 40, w.y = rw.y + rw.h / 2;
        w.wheel_y = -3;
        ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, w.x, w.y)});
        ed.step_frame_headless({w});
      }
      std::printf("    the Render window %dx%d: %d scroll step(s) to bring Open Lighting into view\n", rw.w, rw.h, scrolls);
    }
    CHECK(click("Open Lighting"));
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const Recti lw = ed.window_rect_for_test(WindowKind::Lighting), rw2 = ed.window_rect_for_test(WindowKind::Render);
    std::printf("    after the click: the Lighting window %dx%d, the Render window %dx%d, focused %d (Lighting %d)\n", lw.w, lw.h, rw2.w, rw2.h, ed.focused_window_for_test(), (int)WindowKind::Lighting);
    if (lw.w == 0) {
      ed.command("window Lighting");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      const Recti lw2 = ed.window_rect_for_test(WindowKind::Lighting);
      std::printf("    (the console's `window Lighting` in this layout: %dx%d)\n", lw2.w, lw2.h);
    }
    CHECK(lw.w > 0);
    ed.command("window Render");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const bool vrow = br.count("render:Viewport Device") > 0, grow = br.count("render:GI Device") > 0;
    std::printf("    Render Devices: the Viewport Device row %d, the GI Device row %d\n", (int)vrow, (int)grow);
    CHECK(vrow && grow);
    /* Add Component in a modeling layout lists the Rendering components. */
    ed.command("layout Default");
    ed.command("select Cube");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const bool opened = click("Add Component");
    const bool filters = br.count("menu:Camera Filters") > 0, light = br.count("menu:Light") > 0;
    std::printf("    Add Component in the Default layout: opened %d, Camera Filters %d, Light %d\n", (int)opened, (int)filters, (int)light);
    CHECK(opened && filters && light);
    ed.step_frame_headless({key_ev(platform::KEY_ESCAPE)});
  });
}

static void tests_round277() {
  test("compositing and materials within a pen's reach (ADR 0085, task 0257): a left drag pans the Render window's image; the Materials toolbar's Assign to Selection is disabled with its reason when nothing is selected; the Textures toolbar's Export Textures... opens the dialog", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(600.0);
    ed.command("layout Lighting & Rendering");
    ed.command("window Render");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    ed.command("preview");  // (a quick render: the image area pans only with a picture in it)
    for (int i = 0; i < 400 && ed.rendering_for_test(); i++) ed.step_frame_headless();
    ed.step_frame_headless();
    const Recti rw = ed.window_rect_for_test(WindowKind::Render);
    const Vec2 pan0 = ed.render_pan_for_test();
    const int cx = rw.x + rw.w * 2 / 3, cy = rw.y + rw.h * 2 / 3;  // (the image area: right of the settings column, below the bar)
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx + 30, cy + 10)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx + 60, cy + 20)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx + 60, cy + 20)});
    const Vec2 pan1 = ed.render_pan_for_test();
    /* A press that began outside the image (the settings column) and crossed into it pans nothing. */
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, rw.x + 20, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, rw.x + 20, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx + 50, cy)});
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx + 50, cy)});
    const Vec2 pan2 = ed.render_pan_for_test();
    std::printf("    the Render window %dx%d with a preview rendered %d: a left drag of 60 px moved the pan (%.0f, %.0f) -> (%.0f, %.0f)\n", rw.w, rw.h, (int)(ed.render_image_for_test().width > 0), pan0.x, pan0.y, pan1.x, pan1.y);
    std::printf("    a press from the settings column crossing the image: pan (%.0f, %.0f) -> (%.0f, %.0f)\n", pan1.x, pan1.y, pan2.x, pan2.y);
    CHECK(rw.w > 0 && pan1.x > pan0.x && pan2.x == pan1.x && pan2.y == pan1.y);
    ed.command("layout Texturing");
    ed.command("window Materials");
    ed.clear_selection_for_test();
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const std::string why = ed.button_disabled_why_for_test("Assign to Selection");
    const Recti mw = ed.window_rect_for_test(WindowKind::Materials);
    std::printf("    the Materials window %dx%d: Assign to Selection drawn %d, disabled: '%s'\n", mw.w, mw.h, (int)(ed.ui_for_test().button_rects.count("Assign to Selection") > 0), why.c_str());
    CHECK(mw.w > 0 && ed.ui_for_test().button_rects.count("Assign to Selection") > 0 && why.find("Select") != std::string::npos);
    ed.command("select Cube");
    ed.command("surface new ExportSurf");
    ed.command("window Textures");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto &br = ed.ui_for_test().button_rects;
    const bool button = br.count("Export Textures...") > 0;
    if (button) {
      const Recti r = br.at("Export Textures...");
      const int bx = r.x + r.w / 2, by = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, bx, by)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, bx, by)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, bx, by)});
      ed.step_frame_headless();
    }
    std::printf("    Textures toolbar: Export Textures... drawn %d, the dialog open %d, its Export button recorded %d\n", (int)button, (int)ed.export_textures_dialog_for_test(), (int)(br.count("exptex:Export") > 0));
    CHECK(button && ed.export_textures_dialog_for_test() && br.count("exptex:Export") > 0 && br.count("exptex:Cancel") > 0);
    ed.close_dialog_for_test();
  });
}

static void tests_round278() {
  test("animation within reach (ADR 0085, task 0258): the five animation actions exist and the presets bind them as stated, colliding with no other action's chord; Set Key adds a key on every track at the playhead, Record toggles, Next Key moves the playhead; the Scene view's bar carries the RECORD badge while Record is on", [] {
    struct Bind {
      const char *preset, *action, *chord;
    };
    const Bind binds[] = {{"Unity", "anim.key", "Shift+K"}, {"Unity", "anim.record", "Ctrl+Shift+K"}, {"Unity", "anim.prev_key", ","}, {"Unity", "anim.next_key", "."},
                          {"Unreal", "anim.key", "Shift+K"}, {"Blender", "anim.key", "Shift+I"}, {"Blender", "anim.next_key", "Up"}, {"Blender", "anim.prev_key", "Down"}, {"Maya", "anim.key", "S"}};
    for (const Bind &b : binds) {
      const Keymap k = keymap_preset(b.preset);
      const auto it = k.find(b.action);
      const std::string got = it == k.end() || it->second.empty() ? std::string("(none)") : chord_text(it->second[0]);
      std::printf("    %-8s %-14s %s\n", b.preset, b.action, got.c_str());
      CHECK(got == b.chord);
    }
    /* No animation chord collides with another action's in its preset. */
    int collisions = 0;
    for (const char *preset : {"Unity", "Unreal", "Blender", "Maya", "SketchUp"}) {
      const Keymap k = keymap_preset(preset);
      for (const auto &kv : k) {
        if (kv.first.rfind("anim.", 0) != 0) continue;
        for (const KeyChord &c : kv.second)
          for (const auto &other : k) {
            if (other.first == kv.first || std::find(other.second.begin(), other.second.end(), c) == other.second.end()) continue;
            collisions++;
            std::printf("    collision in %s: %s and %s share %s\n", preset, kv.first.c_str(), other.first.c_str(), chord_text(c).c_str());
          }
      }
    }
    CHECK(collisions == 0);
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(700.0);
    ed.command("select Cube");
    ed.command("layout Default");
    ed.command("window Animation");  // (Record sees the edits from the Animation window's frame)
    ed.command("anim new Hop");
    ed.command("anim key . Position 1 0 0");  // (two tracks - Y and X - with keys at 0 and 30)
    ed.command("anim key . Position 1 30 1");
    ed.command("anim key . Position 0 0 0");
    ed.command("anim key . Position 0 30 2");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("anim frame 12");
    ed.step_frame_headless();
    const AnimationClipPtr clip = ed.scene().find_by_name("Cube") ? ed.anim_clip_for_test() : nullptr;
    const size_t tracks = clip ? clip->tracks.size() : 0;
    size_t keys0 = 0, keys1 = 0;
    if (clip) for (const AnimTrack &t : clip->tracks) keys0 += t.keys.size();
    CHECK(ed.run_action("anim.key"));
    ed.step_frame_headless();
    if (clip) for (const AnimTrack &t : clip->tracks) keys1 += t.keys.size();
    std::printf("    the clip: %zu track(s), %zu key(s) -> %zu after Set Key at frame 12\n", tracks, keys0, keys1);
    CHECK(tracks == 2 && keys1 == keys0 + tracks);
    ed.command("anim frame 0");
    CHECK(ed.run_action("anim.next_key"));
    std::printf("    Next Key from frame 0: the playhead at %.0f (the Set Key at 12)\n", ed.anim_for_test().frame);
    CHECK(ed.anim_for_test().frame == 12.0f);
    /* The chord itself, over the Scene view: Shift+K under Unity adds a key at the playhead. */
    ed.command("anim frame 20");
    const Recti view = ed.scene_view_rect();
    ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, view.x + view.w / 2, view.y + view.h / 2)});
    ed.step_frame_headless({key_ev(platform::KEY_K, platform::MOD_SHIFT)});
    size_t keys2 = 0;
    if (clip) for (const AnimTrack &t : clip->tracks) keys2 += t.keys.size();
    std::printf("    Shift+K over the view at frame 20: %zu -> %zu keys\n", keys1, keys2);
    CHECK(keys2 == keys1 + tracks);
    /* In Play, Set Key refuses aloud with the fix. */
    ed.command("play");
    ed.step_frame_headless();
    CHECK(ed.run_action("anim.key"));
    ed.step_frame_headless();
    const auto &toasts = ed.toasts_for_test();
    const bool refused = std::any_of(toasts.begin(), toasts.end(), [](const Editor::Toast &t) { return t.text.find("not while Play") != std::string::npos && t.fix_cmd == "stop"; });
    std::printf("    Set Key in Play: refused with Fix: stop %d\n", (int)refused);
    CHECK(refused);
    ed.command("stop");
    ed.step_frame_headless();
    CHECK(ed.run_action("anim.record"));
    ed.step_frame_headless();
    const bool badge_on = ed.ui_for_test().button_rects.count("overlay:RECORD") > 0, rec_on = ed.anim_for_test().record;
    CHECK(ed.run_action("anim.record"));
    ed.step_frame_headless();
    const bool badge_off = ed.ui_for_test().button_rects.count("overlay:RECORD") > 0;
    std::printf("    Record toggled on: record %d, the RECORD badge %d; toggled off: badge %d\n", (int)rec_on, (int)badge_on, (int)badge_off);
    CHECK(rec_on && badge_on && !badge_off && !ed.anim_for_test().record);
  });
}

static void tests_round279() {
  test("rigging and weight paint within reach (ADR 0085, task 0259): the Add brush subtracts with Ctrl or the pen's eraser and the other brushes are left alone; a second mesh selected alone gets a Skeleton dropdown in the Skin section and, the skeleton picked, Add Armature (Skin) binds it to that skeleton", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.set_clock_for_test(800.0);
    ed.wp_brush_mode_for_test(0);
    const int add_plain = ed.wp_effective_mode_for_test(false, false), add_ctrl = ed.wp_effective_mode_for_test(true, false), add_eraser = ed.wp_effective_mode_for_test(false, true);
    ed.wp_brush_mode_for_test(3);
    const int blur_ctrl = ed.wp_effective_mode_for_test(true, false);
    ed.wp_brush_mode_for_test(0);
    std::printf("    Add brush: plain %d, Ctrl %d, eraser %d (0 add, 1 subtract); Blur with Ctrl %d\n", add_plain, add_ctrl, add_eraser, blur_ctrl);
    CHECK(add_plain == 0 && add_ctrl == 1 && add_eraser == 1 && blur_ctrl == 3);
    /* A skeleton rigged on the Cube; then the Sphere, selected alone, binds to it through the dropdown. */
    ed.command("select Cube");
    ed.command("create Armature");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const std::vector<std::string> roots = ed.skeleton_roots_for_test();
    std::printf("    skeleton roots after `create Armature`: %zu", roots.size());
    for (const std::string &r : roots) std::printf(" '%s'", r.c_str());
    std::printf("\n");
    CHECK(!roots.empty());
    if (roots.empty()) return;
    ed.command("select Sphere");
    ed.command("layout Rigging & Animation");
    ed.command("window Rigging");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto &br = ed.ui_for_test().button_rects;
    const bool dropdown = br.count("skin:Skeleton") > 0;
    const std::string why_none = ed.button_disabled_why_for_test("Add Armature (Skin)");
    GameObject *root = ed.scene().find_by_name(roots[0]);
    const GameObject *arm_parent_before = root ? root->parent : nullptr;
    const GameObject *sphere_parent_before = ed.scene().find_by_name("Sphere") ? ed.scene().find_by_name("Sphere")->parent : nullptr;
    ed.skin_pick_root_for_test(root ? root->id : 0);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const bool enabled = ed.button_disabled_why_for_test("Add Armature (Skin)").empty() && br.count("Add Armature (Skin)") > 0;
    std::printf("    the Skin section: Skeleton dropdown %d; Add Armature before a pick disabled '%s'; after the pick enabled %d\n", (int)dropdown, why_none.c_str(), (int)enabled);
    CHECK(dropdown && enabled);
    if (br.count("Add Armature (Skin)")) {
      const Recti r = br.at("Add Armature (Skin)");
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseMove, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseDown, cx, cy)});
      ed.step_frame_headless({mouse_ev(platform::EventType::MouseUp, cx, cy)});
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
    }
    const GameObject *sphere = ed.scene().find_by_name("Sphere"), *cube_after = ed.scene().find_by_name("Cube"), *arm = ed.scene().find_by_name(roots[0]);
    const SkinModifier *sk = sphere ? sphere->get<SkinModifier>() : nullptr;
    const bool armature = arm && arm->get<Armature>(), arm_parent_same = arm && arm->parent == arm_parent_before, sphere_parent_same = sphere && sphere->parent == sphere_parent_before;
    const size_t groups = sphere && sphere->get<MeshFilter>() && sphere->get<MeshFilter>()->mesh ? sphere->get<MeshFilter>()->mesh->vertex_groups.size() : 0;
    std::printf("    after Add Armature (Skin): the Sphere's Skin modifier %d, bound to '%s' (an Armature %d), %zu vertex group(s); the armature's parent unchanged %d, the Sphere's %d, the Cube still skinned %d\n",
                (int)(sk != nullptr), sk ? sk->object.c_str() : "", (int)armature, groups, (int)arm_parent_same, (int)sphere_parent_same, (int)(cube_after && cube_after->get<SkinModifier>()));
    CHECK(sk && sk->object == roots[0] && armature && groups > 0 && arm_parent_same && sphere_parent_same && cube_after && cube_after->get<SkinModifier>());
  });
}

static void tests_round281() {
  test("onboarding and transfer (ADR 0085, task 0261): the Adobe preset's keys; [ ] step the active brush by a quarter and nothing with no brush on; every theme's WCAG contrast printed and the High Contrast theme's at 4.5 or more; the welcome's origin sets the keys and the feel", [] {
    /* The Adobe preset. */
    const Keymap adobe = keymap_preset("Adobe");
    auto chord = [&](const Keymap &k, const char *id) {
      const auto it = k.find(id);
      return it == k.end() || it->second.empty() ? std::string("(none)") : chord_text(it->second[0]);
    };
    std::printf("    Adobe: paint %s, eraser %s, move %s, smaller %s, larger %s, rotate %s\n", chord(adobe, "mode.paint_toggle").c_str(), chord(adobe, "brush.erase").c_str(),
                chord(adobe, "tool.move").c_str(), chord(adobe, "brush.smaller").c_str(), chord(adobe, "brush.larger").c_str(), chord(adobe, "tool.rotate").c_str());
    CHECK(chord(adobe, "mode.paint_toggle") == "B" && chord(adobe, "brush.erase") == "E" && chord(adobe, "tool.move") == "V");
    CHECK(adobe.at("tool.move").size() == 2 && chord_text(adobe.at("tool.move")[1]) == "W");
    CHECK(chord(adobe, "brush.smaller") == "[" && chord(adobe, "brush.larger") == "]" && chord(adobe, "tool.rotate") == "(none)");
    for (const char *pn : {"Unity", "Blender", "Maya", "SketchUp"}) CHECK(chord(keymap_preset(pn), "brush.larger") == "]");
    CHECK(chord(keymap_preset("Unity"), "brush.erase") == "(none)");
    /* The brush step. */
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    CHECK(!ed.run_action_for_test("brush.larger"));  // (no brush mode: nothing)
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube);
    ed.select_for_test(cube->id, false);
    CHECK(ed.run_action_for_test("mode.sculpt_toggle") && ed.sculpt_for_test());
    const float r0 = ed.sculpt_brush_for_test().radius;
    ed.run_action_for_test("brush.larger");
    const float r1 = ed.sculpt_brush_for_test().radius;
    ed.run_action_for_test("brush.smaller");
    const float r2 = ed.sculpt_brush_for_test().radius;
    std::printf("    sculpt radius %.0f -> ] %.0f -> [ %.0f px\n", r0, r1, r2);
    CHECK(std::fabs(r1 - std::round(r0 * 1.25f)) < 0.5f && std::fabs(r2 - std::round(r1 * 0.8f)) < 0.5f);
    ed.run_action_for_test("mode.sculpt_toggle");
    ed.command("surface new BrushSurf");  // (Paint wants a surface on the material)
    CHECK(ed.run_action_for_test("mode.paint_toggle") && ed.paint_for_test());
    const float p0 = ed.paint_brush_for_test().radius;
    ed.run_action_for_test("brush.larger");
    std::printf("    paint size %.0f -> ] %.0f px\n", p0, ed.paint_brush_for_test().radius);
    CHECK(ed.paint_brush_for_test().radius > p0);
    CHECK(!ed.paint_brush_for_test().erase && ed.run_action_for_test("brush.erase") && ed.paint_brush_for_test().erase);
    ed.run_action_for_test("mode.paint_toggle");
    CHECK(!ed.run_action_for_test("brush.erase"));  // (the eraser is Paint's: refused aloud)
    /* The other brushes, the limits and the bottom of the scale. */
    ed.command("create Sphere");
    ed.select_for_test(cube->id, false);
    ed.command("scatter Sphere");
    ed.command("scatter on");
    CHECK(ed.scatter_for_test());
    ed.scatter_brush_for_test().radius = 900.0f;
    ed.run_action_for_test("brush.larger");
    std::printf("    scatter %d: radius 900 -> ] %.0f m (the field's limit 1000)\n", (int)ed.scatter_for_test(), ed.scatter_brush_for_test().radius);
    CHECK(std::fabs(ed.scatter_brush_for_test().radius - 1000.0f) < 0.01f);
    ed.command("scatter off");
    CHECK(ed.run_action_for_test("mode.sculpt_toggle") && ed.sculpt_for_test());
    ed.sculpt_brush_mut_for_test().radius = 1.0f;
    ed.run_action_for_test("brush.larger");
    const float up_from_1 = ed.sculpt_brush_for_test().radius;
    ed.sculpt_brush_mut_for_test().radius = 2.0f;
    ed.run_action_for_test("brush.smaller");
    const float down_from_2 = ed.sculpt_brush_for_test().radius;
    ed.sculpt_brush_mut_for_test().radius = 480.0f;
    ed.run_action_for_test("brush.larger");
    std::printf("    sculpt 1 -> ] %.0f, 2 -> [ %.0f, 480 -> ] %.0f (the field's 500)\n", up_from_1, down_from_2, ed.sculpt_brush_for_test().radius);
    CHECK(up_from_1 == 2.0f && down_from_2 == 1.0f && ed.sculpt_brush_for_test().radius == 500.0f);
    ed.run_action_for_test("mode.sculpt_toggle");
    /* The themes' contrast (WCAG 2, 1.4.3 and 1.4.11). */
    double hc_min = 99.0;
    for (int k = 0; k < ui::kThemeCount; k++) {
      const ui::Theme t = ui::theme_named(ui::kThemeNames[k]);
      const double text_window = ui::contrast_ratio(t.text, t.window), text_panel = ui::contrast_ratio(t.text, t.panel), text_button = ui::contrast_ratio(t.text, t.button),
                   text_field = ui::contrast_ratio(t.text, t.field), dim_panel = ui::contrast_ratio(t.text_dim, t.panel), warn = ui::contrast_ratio(t.warning, t.panel),
                   err = ui::contrast_ratio(t.error, t.panel), accent = ui::contrast_ratio(t.accent, t.panel), focus = ui::contrast_ratio(t.focus, t.panel);
      std::printf("    %-14s text/window %.1f  text/panel %.1f  text/button %.1f  text/field %.1f  dim/panel %.1f  warning %.1f  error %.1f  accent %.1f  focus %.1f\n",
                  ui::kThemeNames[k], text_window, text_panel, text_button, text_field, dim_panel, warn, err, accent, focus);
      if (std::string(ui::kThemeNames[k]) != "High Contrast") continue;
      const double on_accent = ui::contrast_ratio(t.text_on_accent, t.accent), on_progress = ui::contrast_ratio(t.text_bright, t.progress),
                   dim_selection = ui::contrast_ratio(t.text_dim, t.selection), text_selection = ui::contrast_ratio(t.text, t.selection),
                   dim_strip = ui::contrast_ratio(t.text_dim, t.strip), tip = ui::contrast_ratio(t.text, t.tooltip);
      std::printf("    High Contrast also: on the accent %.1f  on the tray's fill %.1f  dim on the selection %.1f  text on the selection %.1f  disabled %.1f  tooltip %.1f\n",
                  on_accent, on_progress, dim_selection, text_selection, dim_strip, tip);
      hc_min = std::min({text_window, text_panel, text_button, text_field, dim_panel, warn, err, accent, on_accent, on_progress, dim_selection, text_selection, dim_strip, tip});
      CHECK(focus >= 3.0 && ui::contrast_ratio(t.focus, t.warning) > 1.0);
    }
    std::printf("    High Contrast: the least of those %.1f (4.5 asked)\n", hc_min);
    CHECK(hc_min >= 4.5);
    CHECK(std::fabs(ui::contrast_ratio(Color::hex(0xFFFFFF), Color::hex(0x000000)) - 21.0) < 0.01 && std::fabs(ui::contrast_ratio(Color::hex(0x777777), Color::hex(0x777777)) - 1.0) < 0.01);
    /* The welcome asks the origin on every first run; its answer sets the keys and the feel. */
    Editor w;
    w.init_headless(1280, 720);
    const std::string prefs = fs::join(test_dir(), "r281_prefs_none.txt");
    std::remove(prefs.c_str());
    w.prefs_for_test(prefs);
    w.load_prefs_for_test();
    CHECK(w.first_run_dialog_for_test());
    /* A real click on the welcome's Unreal button: the keys and the feel follow, the welcome stays for Start. */
    w.record_buttons_for_test(true);
    for (int i = 0; i < 2; i++) w.step_frame_headless();
    Recti ub, sb;
    const bool found_unreal = w.button_rect_for_test("welcome:Unreal", ub);
    CHECK(found_unreal);
    using ET = platform::EventType;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) {
      platform::Event e;
      e.type = t, e.x = ub.x + ub.w / 2, e.y = ub.y + ub.h / 2;
      w.step_frame_headless({e});
    }
    w.step_frame_headless();
    std::printf("    Unreal clicked: keys %s, feel %s, welcome %d\n", w.keymap_preset_name().c_str(), kEditorFeels[(int)w.nav_style_for_test()], (int)w.first_run_dialog_for_test());
    CHECK(w.keymap_preset_name() == "Unreal" && w.nav_style_for_test() == NavStyle::Unreal && w.first_run_dialog_for_test());
    CHECK(w.button_rect_for_test("welcome:Start", sb) || w.button_rect_for_test("Use High", sb));
    /* Just start keeps the keys (Unreal's, just chosen) and closes the welcome. */
    Recti js;
    CHECK(w.button_rect_for_test("welcome:Just start", js));
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) {
      platform::Event e;
      e.type = t, e.x = js.x + js.w / 2, e.y = js.y + js.h / 2;
      w.step_frame_headless({e});
    }
    w.step_frame_headless();
    std::printf("    Just start: keys %s, welcome %d\n", w.keymap_preset_name().c_str(), (int)w.first_run_dialog_for_test());
    CHECK(w.keymap_preset_name() == "Unreal" && !w.first_run_dialog_for_test());
    w.apply_origin("adobe");
    std::printf("    origin adobe: keys %s, feel %s\n", w.keymap_preset_name().c_str(), kEditorFeels[(int)w.nav_style_for_test()]);
    CHECK(w.keymap_preset_name() == "Adobe" && w.nav_style_for_test() == NavStyle::Unity && w.shortcut_for_test("mode.paint_toggle") == "B");
    CHECK(w.shortcut_for_test("tool.move") == "V");  // (W kept beside it)
    w.apply_origin("blender");
    CHECK(w.keymap_preset_name() == "Blender" && w.nav_style_for_test() == NavStyle::Blender);
    /* The prefs keep the Adobe keys. */
    w.apply_origin("adobe");
    Editor w2;
    w2.init_headless(800, 600);
    w2.prefs_for_test(prefs);
    w2.load_prefs_for_test();
    CHECK(!w2.first_run_dialog_for_test() && w2.keymap_preset_name() == "Adobe" && w2.shortcut_for_test("brush.erase") == "E");
    /* `keymap table`: a row per action, a column per preset. */
    const std::string table = w.keymap_table();
    size_t rows = 0;
    for (char c : table) rows += c == '\n';
    std::printf("    keymap table: %zu lines, header '%s'\n", rows, table.substr(0, table.find('\n')).c_str());
    CHECK(rows == shortcut_actions().size() + 2 && table.find("| Brushes: Brush Larger | ] | ] | ] | ] | ] | ] | ] |") != std::string::npos);
  });
}

static void tests_round280() {
  test("files and the shell (ADR 0085, task 0260): Up and Down recall the console's history, Down past the newest clears; an unknown command is answered with the nearest known ones; the Wheel's object ring carries Set Key, Record and Play Clip in an Animation ring, every built-in ring within the Wheel's eight slices", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();  // (the first frame: the log so far is history to the toasts)
    ed.console_history_push_for_test("select Cube");
    ed.console_history_push_for_test("meshmaps 256 8");
    ed.console_recall_for_test(-1);
    const std::string up1 = ed.console_input_for_test();
    ed.console_recall_for_test(-1);
    const std::string up2 = ed.console_input_for_test();
    ed.console_recall_for_test(1);
    const std::string down1 = ed.console_input_for_test();
    ed.console_recall_for_test(1);
    const std::string down2 = ed.console_input_for_test();
    std::printf("    Up: '%s', Up: '%s', Down: '%s', Down: '%s'\n", up1.c_str(), up2.c_str(), down1.c_str(), down2.c_str());
    CHECK(up1 == "meshmaps 256 8" && up2 == "select Cube" && down1 == "meshmaps 256 8" && down2.empty());
    const std::vector<std::string> near = ed.command_suggestions_for_test("meshmap"), none = ed.command_suggestions_for_test("qzxv"), sk = ed.command_suggestions_for_test("sketchlin");
    std::printf("    'meshmap' -> %zu suggestion(s)%s; 'qzxv' -> %zu; 'sketchlin' -> %s\n", near.size(), near.empty() ? "" : (": " + near[0]).c_str(), none.size(), sk.empty() ? "(none)" : sk[0].c_str());
    CHECK(!near.empty() && near[0] == "meshmaps" && none.empty() && !sk.empty() && sk[0] == "sketchline");
    ed.console_history_push_for_test("x");
    ed.console_recall_for_test(1);  // (Down with no recall active: the field is left alone - here empty, so still empty)
    CHECK(ed.console_input_for_test().empty());
    ed.command("meshmap");
    ed.step_frame_headless();
    const auto &toasts = ed.toasts_for_test();
    const bool said = std::any_of(toasts.begin(), toasts.end(), [](const Editor::Toast &t) { return t.text.find("Did you mean: meshmaps") != std::string::npos; });
    std::printf("    the console's answer: %s\n", toasts.empty() ? "(nothing)" : toasts[0].text.c_str());
    CHECK(said);
    int widest = 0;
    for (const char *set : {"object", "edit", "sculpt", "paint"}) widest = std::max(widest, ed.wheel_set_max_ring_for_test(set));
    std::printf("    the widest built-in ring: %d slices (the Wheel shows %d)\n", widest, Editor::kWheelMaxSlicesForTest);
    CHECK(widest > 0 && widest <= Editor::kWheelMaxSlicesForTest);
    const std::vector<std::string> wheel = ed.wheel_set_labels_for_test("object");
    const bool set_key = std::find(wheel.begin(), wheel.end(), "Set Key") != wheel.end(), record = std::find(wheel.begin(), wheel.end(), "Record") != wheel.end(), play = std::find(wheel.begin(), wheel.end(), "Play Clip") != wheel.end();
    std::printf("    the object ring's slices: %zu; Set Key %d, Record %d, Play Clip %d\n", wheel.size(), (int)set_key, (int)record, (int)play);
    CHECK(set_key && record && play);
  });
}

void run_editor_tests() {
  round15_tests();
  round28_tests();
  tests_round84();
  tests_round87();
  tests_round88();
  tests_round99();
  tests_round101();
  tests_round102();
  tests_round103();
  tests_round106();
  tests_round109();
  tests_round110();
  tests_round115();
  tests_round142();
  tests_round165();
  tests_round165_208();
  tests_round185();
  tests_round186();
  tests_round187();
  tests_round198();
  tests_round199();
  tests_round200();
  tests_round201();
  tests_round203();
  tests_round204();
  tests_round205();
  tests_round206();
  tests_round207();
  tests_round209();
  tests_round222();
  tests_round227();
  tests_round252();
  tests_round255();
  tests_round263();
  tests_round264();
  tests_round265();
  tests_round266();
  tests_round267();
  tests_round267b();
  tests_round268();
  tests_round268b();
  tests_round271();
  tests_round272();
  tests_round273();
  tests_round274();
  tests_round275();
  tests_round276();
  tests_round277();
  tests_round278();
  tests_round279();
  tests_round280();
  tests_round281();
  coverage_tests();
}
