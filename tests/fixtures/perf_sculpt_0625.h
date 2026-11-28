// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../../src/editor/editor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace perf_sculpt_0625 {

using namespace bl;

template<typename T> bool equal_bytes(const std::vector<T> &a, const std::vector<T> &b) {
  return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

inline bool same_layout(const Mesh &a, const Mesh &b) {
  return a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         a.face_group == b.face_group && a.face_material == b.face_material && a.face_smooth == b.face_smooth && equal_bytes(a.uvs, b.uvs);
}

inline bool same_mesh(const Mesh &a, const Mesh &b) {
  return same_layout(a, b) && equal_bytes(a.positions, b.positions) && equal_bytes(a.sculpt_mask, b.sculpt_mask);
}

/* Compare the entire output of repeated runs of one package, not just the number of vertices touched. */
template<typename T> void hash_add(uint64_t &h, const std::vector<T> &values) {
  h = (h ^ values.size()) * 1099511628211ull;
  const auto *bytes = reinterpret_cast<const unsigned char *>(values.data());
  for (size_t i = 0; i < values.size() * sizeof(T); i++) h = (h ^ bytes[i]) * 1099511628211ull;
}

inline uint64_t hash(const Mesh &m) {
  uint64_t h = 14695981039346656037ull;
  hash_add(h, m.positions);
  hash_add(h, m.face_offsets);
  hash_add(h, m.corner_verts);
  hash_add(h, m.sculpt_mask);
  hash_add(h, m.face_group);
  hash_add(h, m.face_material);
  hash_add(h, m.face_smooth);
  hash_add(h, m.uvs);
  return h;
}

enum class Input { Mouse, Pen, Eraser, Idle };

inline double wall_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void step(Editor &ed, std::vector<platform::Event> events = {}) {
  ed.set_clock_for_test(ed.clock_for_test() + 0.02);
  ed.step_frame_headless(std::move(events));
}

inline Vec2 pixel_of(Editor &ed, Vec3 world) {
  Vec2 px;
  ed.project_for_test(world, px);
  return px;
}

struct Case {
  const char *name;
  const char *brush;
  const char *stroke;
  int subdivisions = 3;
  const char *option = "";
  Input input = Input::Mouse;
};

inline bool settings_match(const Case &row, const SculptBrush &brush) {
  static const char *brushes[] = {"draw", "smooth", "grab", "clay", "inflate", "flatten", "pinch", "crease", "scrape", "snake", "mask"};
  static const char *strokes[] = {"dots", "dot", "rect", "freehand", "spray"};
  if (brush.kind < 0 || brush.kind >= 11 || brush.stroke < 0 || brush.stroke >= 5 ||
      std::strcmp(brushes[brush.kind], row.brush) || std::strcmp(strokes[brush.stroke], row.stroke)) return false;
  const std::string option = row.option;
  if (option.empty()) return !brush.sym_x && !brush.sym_y && !brush.sym_z && brush.radial == 1 && !brush.front_only && !brush.tilt && !brush.pressure && brush.lazy == 0;
  if (option == "sculpt symmetry x") return brush.sym_x;
  if (option == "sculpt symmetry y") return brush.sym_y;
  if (option == "sculpt symmetry z") return brush.sym_z;
  if (option == "sculpt symmetry radial 8") return brush.radial == 8;
  if (option == "sculpt lazy 6") return brush.lazy == 6;
  if (option == "sculpt front on") return brush.front_only;
  if (option == "sculpt tilt on") return brush.tilt;
  if (option == "sculpt pressure size") return brush.pressure == 2;
  if (option == "sculpt pressure strength") return brush.pressure == 1;
  return false;
}

inline void move(Editor &ed, platform::EventType type, int x, int y, Input input, int sample) {
  platform::Event ev;
  ev.type = type, ev.x = x, ev.y = y;
  if (input == Input::Pen || input == Input::Eraser) {
    ev.pointer = platform::Pointer::Pen;
    ev.pen = true;
    ev.eraser = input == Input::Eraser;
    ev.pressure = 0.4f + 0.1f * float(sample % 6);
    ev.tilt_x = 32.0f;
    ev.tilt_y = -15.0f;
  }
  step(ed, {ev});
}

inline void roughen(Mesh &m) {
  m.sculpt_mask.resize(m.vert_count());
  for (size_t v = 0; v < m.vert_count(); v++) {
    Vec3 &p = m.positions[v];
    p *= 1.0f + 0.001f * float(int((v * 71) % 17) - 8);
    m.sculpt_mask[v] = p.x > 0.30f ? 1.0f : 0.25f;
  }
  // Corner-varying data also exercises seams after applied subdivision.
  m.uvs.resize(m.corner_verts.size());
  for (size_t c = 0; c < m.uvs.size(); c++) m.uvs[c] = {float(c % 97) / 96.0f, float(c % 71) / 70.0f};
  m.face_group.resize(m.face_count());
  m.face_smooth.resize(m.face_count(), 1);
  m.face_material.resize(m.face_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++) m.face_group[f] = m.face_center(f).x < 0.0f ? 1 : 2;
  m.touch();
}

inline bool fail(const Case &row, const char *problem) {
  std::fprintf(stderr, "SCULPT0625 %s: %s\n", row.name, problem);
  return false;
}

inline bool pointer_case(const Case &row, int moves, int strokes, bool report) {
  using platform::EventType;
  Editor ed;
  ed.init_headless(1400, 900);
  ed.set_clock_for_test(100.0);
  ed.command("select Cube");
  ed.command("delete");
  ed.command("create Sphere");
  ed.command(strprintf("subdivide %d", row.subdivisions));
  step(ed);
  for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
  ed.command("sculpt on");
  GameObject *g = ed.active_object_for_test();
  if (!g || !g->mesh_filter() || !g->mesh_filter()->mesh || !ed.sculpt_for_test()) return fail(row, "sphere creation or Sculpt entry");
  const uint64_t id = g->id;
  auto current = [&] { return ed.scene().find(id)->mesh_filter()->mesh.get(); };
  mesh_make_mutable(g->mesh_filter()->mesh);
  roughen(*current());
  ed.mark_changed_for_test("Rough sculpt fixture");
  ed.command(std::string("sculpt brush ") + row.brush);
  ed.command(std::string("sculpt stroke ") + row.stroke);
  for (const char *command : {"sculpt size 22", "sculpt strength 0.19", "sculpt falloff smooth", "sculpt carve off", "sculpt front off",
                              "sculpt pressure off", "sculpt tilt off", "sculpt symmetry off", "sculpt lazy 0", "sculpt dyntopo off", "sculpt alpha off"})
    ed.command(command);
  if (*row.option) ed.command(row.option);
  if (!settings_match(row, ed.sculpt_brush_for_test())) return fail(row, "selected brush, stroke or options");
  const Recti view = ed.scene_rect_for_test();
  const Vec2 centre = pixel_of(ed, g->world_position());
  const int cx = view.x + (int)centre.x, cy = view.y + (int)centre.y;
  const int travel = (int)std::min(36.0f, length(pixel_of(ed, g->world_position() + ed.scene_camera_for_test().right() * 0.55f) - centre));
  if (travel < 12 || current()->vert_count() < (row.subdivisions == 3 ? 23000u : 1000u) || !current()->has_uvs()) {
    std::fprintf(stderr, "travel=%d vertices=%zu UVs=%zu\n", travel, current()->vert_count(), current()->uvs.size());
    return fail(row, "fixture density, UVs or projected travel");
  }
  bool ready = false;
  for (int frame = 0; frame < 100 && !ready; frame++) {
    step(ed);
    /* Read only the ID plane here, so the first timed stroke builds its own ray grid. */
    ready = ed.scene_id_for_test(cx - view.x - travel / 2, cy - view.y) == id;
  }
  if (!ready) return fail(row, "starting pointer did not hit the selected sphere");
  step(ed);
  const size_t steps = ed.undo_steps_for_test();
  const size_t snapshots = ed.sculpt_undo_snapshots_for_test();
  const MeshPtr before = mesh_copy_bare(*current());
  const uint64_t initial_hash = hash(*before);
  const bool idle = row.input == Input::Idle;
  const bool mask = std::strcmp(row.brush, "mask") == 0;
  std::vector<double> frame_ms;
  frame_ms.reserve(moves);
  for (int s = 0; s < strokes; s++) {
    const int x0 = cx - travel / 2, y0 = cy + (s % 2) * 4;
    move(ed, EventType::MouseMove, x0, y0, row.input, 0);
    const double t0 = wall_seconds();
    move(ed, idle ? EventType::MouseMove : EventType::MouseDown, x0, y0, row.input, 0);
    const double t1 = wall_seconds();
    int x = x0, y = y0;
    frame_ms.clear();
    for (int i = 1; i <= moves; i++) {
      x = x0 + i * travel / moves;
      y = y0 + i % 8;
      const double start = wall_seconds();
      move(ed, EventType::MouseMove, x, y, row.input, i);
      frame_ms.push_back((wall_seconds() - start) * 1000.0);
    }
    const double t2 = wall_seconds();
    move(ed, idle ? EventType::MouseMove : EventType::MouseUp, x, y, row.input, moves);
    const double t3 = wall_seconds();
    const Mesh &after = *current();
    const bool finite = std::all_of(after.positions.begin(), after.positions.end(), [](Vec3 p) {
      return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    });
    size_t moved = 0;
    bool held_still = true;
    for (size_t v = 0; v < std::min(before->vert_count(), after.vert_count()); v++) {
      if (std::memcmp(&before->positions[v], &after.positions[v], sizeof(Vec3)) == 0) continue;
      moved++;
      if (before->sculpt_mask[v] == 1.0f) held_still = false;
    }
    size_t masked = 0;
    bool mask_direction = true;
    if (after.sculpt_mask.size() == before->sculpt_mask.size()) {
      for (size_t v = 0; v < after.sculpt_mask.size(); v++) {
        const float start = before->sculpt_mask[v], end = after.sculpt_mask[v];
        if (start == end) continue;
        masked++;
        mask_direction &= row.input == Input::Eraser ? end >= 0.0f && end < start : end <= 1.0f && end > start;
      }
    }
    const bool valid = finite && held_still && mask_direction && same_layout(*before, after) && after.vert_count() == before->vert_count() && after.mask_fits() &&
                       (idle ? moved == 0 && masked == 0 : mask ? moved == 0 && masked > 0 : moved > 0 && masked == 0) &&
                       ed.active_object_for_test() == ed.scene().find(id) && ed.sculpt_for_test() &&
                       ed.undo_steps_for_test() == steps + (idle ? 0u : 1u) && ed.sculpt_undo_snapshots_for_test() == snapshots;
    const size_t dabs = ed.sculpt_dabs_for_test();
    const uint64_t output_hash = hash(after);
    bool restored = true;
    if (!idle) {
      ed.command("undo");
      restored = same_mesh(*before, *current()) && ed.undo_steps_for_test() == steps && ed.sculpt_for_test();
    }
    if (report) {
      std::sort(frame_ms.begin(), frame_ms.end());
      std::printf("SCULPT0625 %s %d %zu %zu %d %zu %zu %zu %016llx %016llx %.3f %.3f %.3f %.3f %d\n",
                  row.name, s, before->vert_count(), before->face_count(), moves, dabs, moved, masked,
                  (unsigned long long)initial_hash, (unsigned long long)output_hash, (t1 - t0) * 1000.0,
                  (t2 - t1) * 1000.0, (t3 - t2) * 1000.0, frame_ms[(frame_ms.size() * 95 + 99) / 100 - 1], (int)(valid && restored));
    }
    if (!valid) return fail(row, "stroke output, selection or undo count");
    if (!restored) return fail(row, "undo failed to restore mesh bytes");
  }
  return true;
}

}  // namespace perf_sculpt_0625
