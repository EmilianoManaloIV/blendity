// SPDX-License-Identifier: GPL-2.0-or-later
// Headless CPU editor probe. Invoke with CASE|all OUTPUT_PREFIX.
#include "editor/editor.h"
#include "scene/uv.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
using namespace bl;
using Clock = std::chrono::steady_clock;

struct Case {
  const char *name;
  int subdivisions, width, height;
};

constexpr Case cases[] = {
    {"tiny", 0, 800, 600},
    {"original-size", 5, 1600, 900},
    {"plain-hot", 5, 800, 600},
    {"mixed", 4, 1100, 720},
    {"alternating", 4, 1100, 720},
    {"stretch", 4, 1100, 720},
    {"selection", 4, 1100, 720},
    {"clipped", 4, 1100, 720},
    {"ordinary", 3, 1100, 720},
    {"sparse", 4, 1100, 720},
};
constexpr Vec2 unit_quad[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

bool reset_result(const Mesh &mesh) {
  if (!mesh.has_uvs()) return false;
  for (size_t face = 0; face < mesh.face_count(); ++face) {
    if (mesh.face_size(face) != 4) return false;
    for (uint32_t i = 0; i < 4; ++i)
      if (!same(mesh.uvs[mesh.face_offsets[face] + i], unit_quad[i])) return false;
  }
  return true;
}

bool click(Editor &ed, const char *label) {
  Recti rect;
  if (!ed.button_rect_for_test(label, rect)) return false;
  platform::Event event{};
  event.x = rect.x + rect.w / 2;
  event.y = rect.y + rect.h / 2;
  for (auto type : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp}) {
    event.type = type;
    ed.step_frame_headless({event});
  }
  return true;
}

void pan(Editor &ed) {
  const Recti rect = ed.uv_rect_for_test();
  platform::Event event{};
  event.x = rect.x + rect.w / 2;
  event.y = rect.y + rect.h / 2;
  event.type = platform::EventType::MouseMove;
  ed.step_frame_headless({event});
  event.type = platform::EventType::Gesture;
  event.pan_x = rect.w * 0.42f + 0.375f;
  event.pan_y = rect.h * 0.29f + 0.125f;
  ed.step_frame_headless({event});
}

void mix_winding(Mesh &mesh, size_t first, size_t step) {
  for (size_t face = first; face < mesh.face_count(); face += step) {
    const uint32_t b = mesh.face_offsets[face];
    std::swap(mesh.uvs[b + 1], mesh.uvs[b + 3]);
    mesh.set_seam(mesh.corner_verts[b], mesh.corner_verts[b + 1], true);
  }
  mesh.touch();
}

uint64_t uv_signature(const Mesh &mesh) {
  uint64_t h = 14695981039346656037ull;
  auto add = [&](uint32_t bits) { h = (h ^ bits) * 1099511628211ull; };
  for (Vec2 uv : mesh.uvs) {
    uint32_t x, y;
    std::memcpy(&x, &uv.x, sizeof(x));
    std::memcpy(&y, &uv.y, sizeof(y));
    add(x);
    add(y);
  }
  for (uint64_t seam : mesh.seams) {
    add(uint32_t(seam));
    add(uint32_t(seam >> 32));
  }
  return h;
}

bool write_pane(Editor &ed, const std::string &path) {
  const Recti rect = ed.uv_rect_for_test();
  const Image &image = ed.framebuffer();
  if (rect.w < 100 || rect.h < 100 || rect.x < 0 || rect.y < 0 || rect.right() > image.width || rect.bottom() > image.height)
    return false;
  std::ofstream out(path, std::ios::binary);
  for (int y = rect.y; y < rect.bottom(); ++y)
    out.write(reinterpret_cast<const char *>(image.pixels.data() + size_t(y) * image.width + rect.x), rect.w * sizeof(uint32_t));
  return bool(out);
}

const char *run(const Case &c, const std::string &prefix) {
  const std::string name = c.name;
  const bool ordinary = name == "ordinary" || name == "sparse";
  const bool selection = name == "selection" || name == "mixed" || name == "alternating";
  Editor ed;
  ed.init_headless(c.width, c.height);
  ed.record_buttons_for_test(true);
  ed.command("jobs async off");
  ed.command("select Cube");
  if (c.subdivisions) ed.command("subdivide " + std::to_string(c.subdivisions));
  ed.command(ordinary ? "uv cylinder" : "uv smart");
  ed.command("layout UV Unwrapping");
  ed.step_frame_headless();
  GameObject *obj = ed.selected_object();
  auto *filter = obj ? obj->get<MeshFilter>() : nullptr;
  const size_t faces = 6u * (1u << (2 * c.subdivisions));
  if (!filter || !filter->mesh || filter->mesh->face_count() != faces || !filter->mesh->has_uvs()) return "mesh setup failed";
  if (name == "stretch") {
    Mesh &mesh = *mesh_make_mutable(filter->mesh);
    for (Vec3 &p : mesh.positions)
      if (p.x > 0) p.x *= 1.5f;
    mesh.touch();
    if (!click(ed, "Stretch")) return "Stretch button was unavailable";
  }
  if (name == "clipped") pan(ed);
  if (selection) {
    ed.command("edit face");
    ed.command("uvsel mode face");
    ed.command("uvsel island 17");
    ed.command("uvsel all");
  }
  const uint64_t before = uv_signature(*filter->mesh);
  const auto start = Clock::now();
  ed.command(ordinary ? "uv smart" : "uv reset");
  const auto dispatched = Clock::now();
  if (name == "mixed" || name == "alternating" || name == "clipped") {
    Mesh &mesh = *mesh_make_mutable(filter->mesh);
    mix_winding(mesh, name == "alternating" ? 1 : 63, name == "alternating" ? 2 : 71);
  }
  if (selection) ed.command(name == "selection" ? "uvsel face 44" : "uvsel face 200");
  if (name == "sparse") {
    Mesh &mesh = *mesh_make_mutable(filter->mesh);
    for (size_t face : {size_t(0), size_t(79), size_t(229)})
      std::copy_n(unit_quad, 4, mesh.uvs.begin() + mesh.face_offsets[face]);
    mesh.touch();
  }
  ed.step_frame_headless();
  const auto visible = Clock::now();
  using Milliseconds = std::chrono::duration<double, std::milli>;
  double repeated_ms = 0;
  if (name == "plain-hot") {
    const auto repeat = Clock::now();
    ed.step_frame_headless();
    repeated_ms = Milliseconds(Clock::now() - repeat).count();
  }
  const uint64_t after = uv_signature(*filter->mesh);
  if (after == before || !filter->mesh->has_uvs()) return "the action did not change UVs";
  if ((name == "tiny" || name == "original-size" || name == "plain-hot" || name == "stretch") &&
      !reset_result(*filter->mesh)) return "Reset did not produce all unit quads";
  if (name == "stretch") {
    const std::vector<float> values = uvops::face_area_stretch(*filter->mesh);
    const auto range = std::minmax_element(values.begin(), values.end());
    if (values.empty() || *range.second - *range.first <= 0.2f) return "Stretch fixture lacks area variation";
  }
  if (!write_pane(ed, prefix + "-" + name + ".rgba")) return "pane capture failed";
  const Recti pane = ed.uv_rect_for_test();
  std::printf("ROW %s %zu %d %d %016llx %.6f %.6f %.6f\n", c.name, faces, pane.w, pane.h, (unsigned long long)after,
              Milliseconds(dispatched - start).count(), Milliseconds(visible - start).count(), repeated_ms);
  std::fflush(stdout);
  return nullptr;
}
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fputs("usage: uv0636-pane CASE|all OUTPUT_PREFIX\n", stderr);
    return 2;
  }
  bool found = false;
  for (const Case &c : cases) {
    if (std::strcmp(argv[1], "all") != 0 && std::strcmp(argv[1], c.name) != 0) continue;
    found = true;
    if (const char *error = run(c, argv[2])) {
      std::fprintf(stderr, "%s: %s\n", c.name, error);
      return 1;
    }
  }
  if (!found) std::fputs("unknown UV pane benchmark case\n", stderr);
  return found ? 0 : 2;
}
