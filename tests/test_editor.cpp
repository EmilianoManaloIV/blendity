// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the editor tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"











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
    CHECK(ed.first_run_dialog_for_test() == low);
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
    CHECK(e2.button_rect_for_test("Use High", b) && b.bottom() <= 720 && b.right() <= 1280);  // (it fits 1080p at 150 %)
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
  coverage_tests();
}
