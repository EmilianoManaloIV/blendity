// SPDX-License-Identifier: GPL-2.0-or-later
// Dependency-free production UV benchmark. See docs/perf/0626-uv.md for paired runs.
#include "core/jobs.h"
#include "scene/uv.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#if defined(BL_UV_BENCH_ALLOCATIONS) && defined(_WIN32)
#  include <malloc.h>
#endif

namespace {
std::atomic<bool> count_allocations{false};
std::atomic<uint64_t> allocations{0}, bytes{0};

#ifdef BL_UV_BENCH_ALLOCATIONS
void record_allocation(size_t size) {
  if (count_allocations.load(std::memory_order_relaxed)) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
  }
}
#endif
}

#ifdef BL_UV_BENCH_ALLOCATIONS
void *operator new(size_t size) {
  if (void *p = std::malloc(std::max(size, size_t(1)))) {
    record_allocation(size);
    return p;
  }
  throw std::bad_alloc();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

void *operator new(size_t size, std::align_val_t alignment) {
  void *p = nullptr;
#ifdef _WIN32
  p = _aligned_malloc(std::max(size, size_t(1)), size_t(alignment));
#else
  if (posix_memalign(&p, size_t(alignment), std::max(size, size_t(1))) != 0) p = nullptr;
#endif
  if (!p) throw std::bad_alloc();
  record_allocation(size);
  return p;
}
void *operator new[](size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void *p, size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void *p, size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
#endif

namespace {
using namespace bl;
using Clock = std::chrono::steady_clock;

struct Fixture {
  Mesh mesh;
  uvops::Mask selected;
  std::vector<uint8_t> vertices;
  std::vector<int> islands;
  int island_count = 0;
};

Fixture make_fixture(int side, bool fragmented) {
  Fixture f;
  const int tile = fragmented ? 2 : side;
  for (int y = 0; y < side; y += tile) {
    for (int x = 0; x < side; x += tile) {
      const uint32_t base = uint32_t(f.mesh.positions.size());
      for (int j = 0; j <= tile; ++j) {
        for (int i = 0; i <= tile; ++i) {
          const float u = float(x + i), v = float(y + j);
          const float h = 0.9f * std::fabs(u - side * 0.5f) + 0.7f * std::sin(u * 0.2f) * std::cos(v * 0.16f);
          f.mesh.positions.push_back({u, h, v});
          f.vertices.push_back(uint8_t(x == 0 && i == 0));
        }
      }
      const float scale = fragmented ? 0.25f + float((x + y) % 10) * 0.1f : 1.0f / float(side);
      for (int j = 0; j < tile; ++j) {
        for (int i = 0; i < tile; ++i) {
          const uint32_t a = base + uint32_t(i + j * (tile + 1)), b = a + uint32_t(tile + 1);
          const uint32_t quad[] = {a, a + 1, b + 1, b};
          const Vec2 p{float(i) * scale + x / 8.0f, float(j) * scale + y / 8.0f};
          const Vec2 t[] = {p, p + Vec2(scale, 0), p + Vec2(scale, scale), p + Vec2(0, scale)};
          const bool include = y + j < side - 2;
          f.mesh.add_face(quad, 4, t);
          f.selected.push_back(uint8_t(include));
          f.islands.push_back(include ? f.island_count : -1);
          // Deliberate user seams outside the mask, and sharp marks inside it.
          if (!include) f.mesh.set_seam(quad[0], quad[1], true);
          else if (i == 0 && j == 0) f.mesh.set_sharp(quad[0], quad[1], true);
        }
      }
      if (y < side - 2) ++f.island_count;
    }
  }
  return f;
}

enum class Check { Coordinates, Reset, Pack, Atlas, Unwrap, SharpUnwrap, Density, Islands, Seams, Stretch, Report };
struct Result {
  int count = 0;
  std::vector<int> labels;
  std::vector<float> stretch;
  float distortion = 0;
  float worst = 0;
  double seam_length = 0;
};

struct Operator {
  const char *name;
  Check check;
  bool bounded_fixture;
  void (*run)(Mesh &, const Fixture &, Result &);
};

const Operator operators[] = {
    {"islands_uv", Check::Islands, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::compute_islands(m, &f.selected, false, r.labels);
     }},
    {"islands_seams", Check::Islands, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::compute_islands(m, &f.selected, true, r.labels);
     }},
    {"mark_vertices", Check::Seams, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::set_seams_from_vertices(m, f.vertices, true);
     }},
    {"clear_vertices", Check::Seams, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::set_seams_from_vertices(m, f.vertices, false);
     }},
    {"seams_from_sharp", Check::Seams, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = int(meshops::seams_from_sharp(m, 30.0f, &f.selected));
     }},
    {"sharp_unwrap", Check::SharpUnwrap, false, [](Mesh &m, const Fixture &f, Result &r) {
       meshops::seams_from_sharp(m, 30.0f, &f.selected);
       r.count = uvops::unwrap_lscm(m, &f.selected);
     }},
    {"unwrap", Check::Unwrap, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::unwrap_lscm(m, &f.selected);
     }},
    {"smart", Check::Atlas, false, [](Mesh &m, const Fixture &f, Result &r) {
       r.count = uvops::smart_project(m, &f.selected);
     }},
    {"parts", Check::Atlas, true, [](Mesh &m, const Fixture &f, Result &r) {
       const auto report = uvops::part_unwrap(m, &f.selected);
       r.count = report.charts;
       r.distortion = report.distortion;
       r.worst = report.distortion_max;
       r.seam_length = report.seam_length;
     }},
    {"cube", Check::Coordinates, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::project_cube(m, &f.selected, std::max(0.001f, length(m.bounds().extent()) * 2.0f / std::sqrt(3.0f)));
     }},
    {"cylinder", Check::Coordinates, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::project_cylinder(m, &f.selected);
     }},
    {"sphere", Check::Coordinates, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::project_sphere(m, &f.selected);
     }},
    {"view", Check::Atlas, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::project_view(m, &f.selected, Mat4::perspective(1, 1, 0.1f, 300));
     }},
    {"reset", Check::Reset, false, [](Mesh &m, const Fixture &f, Result &) { uvops::reset(m, &f.selected); }},
    {"pack_rotate", Check::Pack, false, [](Mesh &m, const Fixture &f, Result &) { uvops::pack_islands(m, &f.selected); }},
    {"pack_axis", Check::Pack, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::pack_islands(m, &f.selected, 0.02f, false);
     }},
    {"average", Check::Density, false, [](Mesh &m, const Fixture &f, Result &) {
       uvops::average_island_scale(m, &f.selected);
     }},
    {"stretch", Check::Stretch, false, [](Mesh &m, const Fixture &, Result &r) { r.stretch = uvops::face_area_stretch(m); }},
    {"report", Check::Report, false, [](Mesh &m, const Fixture &f, Result &r) {
       const auto report = uvops::uv_report(m, &f.selected);
       r.count = report.charts;
       r.distortion = report.distortion_mean;
       r.worst = report.distortion_max;
       r.seam_length = report.seam_length;
     }},
};

bool equal(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

struct ChartBounds {
  Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};

  void include(Vec2 p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  }
};

bool valid_pack(const Mesh &m, const Fixture &f) {
  std::vector<int> labels;
  if (uvops::compute_islands(m, &f.selected, false, labels) != f.island_count || labels != f.islands) return false;
  std::vector<ChartBounds> boxes(size_t(f.island_count));
  for (size_t face = 0; face < labels.size(); ++face) {
    if (labels[face] < 0) continue;
    for (uint32_t c = m.face_offsets[face]; c < m.face_offsets[face + 1]; ++c) boxes[size_t(labels[face])].include(m.uvs[c]);
  }
  std::sort(boxes.begin(), boxes.end(), [](const ChartBounds &a, const ChartBounds &b) { return a.lo.x < b.lo.x; });
  for (size_t i = 0; i < boxes.size(); ++i) {
    const auto &a = boxes[i];
    if (a.hi.x <= a.lo.x || a.hi.y <= a.lo.y) return false;
    for (size_t j = i + 1; j < boxes.size() && boxes[j].lo.x < a.hi.x - 1e-6f; ++j) {
      const auto &b = boxes[j];
      if (b.lo.y < a.hi.y - 1e-6f && a.lo.y < b.hi.y - 1e-6f) return false;
    }
  }
  return true;
}

bool equal_density(const Mesh &m, const Fixture &f) {
  std::vector<double> area_uv(size_t(f.island_count)), area_geom(size_t(f.island_count));
  for (size_t face = 0; face < f.islands.size(); ++face) {
    if (f.islands[face] < 0) continue;
    const size_t k = size_t(f.islands[face]);
    const uint32_t b = m.face_offsets[face];
    const Vec3 anchor = m.positions[m.corner_verts[b]];
    Vec3 normal{};
    double area = 0;
    for (uint32_t c = b + 1; c + 1 < m.face_offsets[face + 1]; ++c) {
      normal += cross(m.positions[m.corner_verts[c]] - anchor, m.positions[m.corner_verts[c + 1]] - anchor);
      const Vec2 p = m.uvs[c] - m.uvs[b], q = m.uvs[c + 1] - m.uvs[b];
      area += double(p.x) * q.y - double(p.y) * q.x;
    }
    area_geom[k] += length(normal);
    area_uv[k] += std::fabs(area);
  }
  double min_density = 1e30, max_density = 0;
  for (size_t i = 0; i < area_uv.size(); ++i) {
    const double density = area_uv[i] / area_geom[i];
    min_density = std::min(min_density, density);
    max_density = std::max(max_density, density);
  }
  return min_density > 0 && max_density <= min_density * 1.005;
}

const char *validate(const Fixture &f, const Mesh &input, const Mesh &m, const Operator &op, const Result &r) {
  if (!m.has_uvs() || m.corner_verts != input.corner_verts || m.face_offsets != input.face_offsets ||
      m.positions != input.positions || m.sharp_edges != input.sharp_edges) return "geometry or UV layout changed";
  const bool changes_seams = op.check == Check::Seams || op.check == Check::SharpUnwrap;
  if (!changes_seams && m.seams != input.seams) return "seams changed";
  const bool atlas = op.check == Check::Atlas || op.check == Check::Pack || op.check == Check::Reset ||
                     op.check == Check::Unwrap || op.check == Check::SharpUnwrap;
  const bool edits = op.check == Check::Coordinates || atlas || op.check == Check::Density;
  bool changed = false;
  const Vec2 reset_quad[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (size_t face = 0; face < m.face_count(); ++face) {
    const uint32_t b = m.face_offsets[face];
    for (uint32_t c = b; c < m.face_offsets[face + 1]; ++c) {
      const Vec2 p = m.uvs[c];
      if (!finite_bits(p.x) || !finite_bits(p.y)) return "non-finite UV";
      const bool differs = !equal(p, input.uvs[c]);
      if ((!f.selected[face] || !edits) && differs) return "unselected or read-only UV changed";
      if (!f.selected[face]) continue;
      changed |= differs;
      if (atlas && (p.x < -1e-6f || p.y < -1e-6f || p.x > 1.000001f || p.y > 1.000001f)) return "UV outside atlas";
      if (op.check == Check::Reset && !equal(p, reset_quad[c - b])) return "wrong reset corner";
    }
  }
  if (edits && (op.check != Check::Density || f.island_count > 1) && !changed) return "selected UVs did not move";
  if (op.check == Check::Pack && !valid_pack(m, f)) return "packing changed island membership or overlaps";
  if (op.check == Check::Density && !equal_density(m, f)) return "island densities disagree";
  if (op.check == Check::Islands && (r.count != f.island_count || r.labels != f.islands)) return "wrong island membership";
  if (op.check == Check::Unwrap && r.count != f.island_count) return "wrong unwrap island count";
  if (op.check == Check::SharpUnwrap &&
      (!std::includes(m.seams.begin(), m.seams.end(), input.seams.begin(), input.seams.end()) ||
       m.seams.size() == input.seams.size())) return "sharp unwrap lost existing seams or added none";
  if (op.check == Check::Seams) {
    const size_t new_count = m.seams.size() > input.seams.size() ? m.seams.size() - input.seams.size() : input.seams.size() - m.seams.size();
    if (r.count <= 0 || size_t(r.count) != new_count) return "wrong seam change count";
  }
  if (op.check == Check::Stretch &&
      (r.stretch.size() != m.face_count() ||
       !std::all_of(r.stretch.begin(), r.stretch.end(), [](float x) { return finite_bits(x) && x > 0; })))
    return "invalid stretch";
  if (!finite_bits(r.distortion) || !finite_bits(r.worst) || !finite_bits(r.seam_length)) return "invalid chart metrics";
  if (op.check == Check::Report && r.count != f.island_count) return "wrong report chart count";
  return nullptr;
}

struct Signature {
  uint64_t value = 14695981039346656037ull;

  template<typename T> void add(const T &item) {
    const auto *raw = reinterpret_cast<const unsigned char *>(&item);
    for (size_t i = 0; i < sizeof(T); ++i) value = (value ^ raw[i]) * 1099511628211ull;
  }
};

uint64_t signature(const Mesh &m, const Result &r) {
  Signature h;
  for (Vec2 p : m.uvs) {
    h.add(p.x);
    h.add(p.y);
  }
  for (uint64_t seam : m.seams) h.add(seam);
  for (int label : r.labels) h.add(label);
  for (float stretch : r.stretch) h.add(stretch);
  h.add(r.count);
  h.add(r.distortion);
  h.add(r.worst);
  h.add(r.seam_length);
  return h.value;
}

bool measure(const Fixture &f, const char *fixture, const Operator &op, int samples) {
  Mesh input = f.mesh;
  if (std::strcmp(op.name, "clear_vertices") == 0) uvops::set_seams_from_vertices(input, f.vertices, true);
  const int selected = int(std::count(f.selected.begin(), f.selected.end(), uint8_t(1)));
  uint64_t calls = 0, total_bytes = 0, expected = 0;
  for (int sample = -2; sample < samples; ++sample) {
    Mesh m = input;  // restore the same edit selection and mesh outside the measurement
    Result result;
    count_allocations.store(sample == -1, std::memory_order_relaxed);
    const auto start = Clock::now();
    op.run(m, f, result);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    count_allocations.store(false, std::memory_order_relaxed);
    const uint64_t h = signature(m, result);
    if (sample == -2) expected = h;
    const char *error = validate(f, input, m, op, result);
    if (error || h != expected) {
      std::fprintf(stderr, "%s %s sample %d: %s\n", fixture, op.name, sample, error ? error : "output changed between calls");
      return false;
    }
    if (sample == -1) {
      calls = allocations.exchange(0, std::memory_order_relaxed);
      total_bytes = bytes.exchange(0, std::memory_order_relaxed);
    }
    if (sample >= 0 || (sample == -1 && samples == 0)) {
      std::printf("%s,%s,%zu,%d,%d,%d,%.6f,%llu,%llu,%016llx,%d,%.9g,%.9g,%.9g\n",
                  fixture, op.name, m.face_count(), selected, JobSystem::global().thread_count(), sample,
                  sample >= 0 ? ms : 0.0, (unsigned long long)calls, (unsigned long long)total_bytes, (unsigned long long)h,
                  result.count, result.distortion, result.worst, result.seam_length);
    }
  }
  return true;
}

bool run_fixture(int side, bool fragmented, const char *filter, int samples) {
  const Fixture full = make_fixture(side, fragmented);
  const Fixture bounded = make_fixture(std::min(side, 24), fragmented);
  for (const Operator &op : operators) {
    if (filter && !std::strstr(op.name, filter)) continue;
    if (!measure(op.bounded_fixture ? bounded : full, fragmented ? "fragmented" : "connected", op, samples)) return false;
  }
  return true;
}
}

int main(int argc, const char **argv) {
  const int side = argc > 1 ? std::atoi(argv[1]) : 96;
  const int samples = argc > 2 ? std::atoi(argv[2]) : 5;
  const char *filter = argc > 3 ? argv[3] : nullptr;
  if (side < 4 || side > 256 || side % 2 != 0 || samples < 0 || samples > 100 || argc > 4 ||
      (filter && std::none_of(std::begin(operators), std::end(operators), [&](const Operator &op) {
         return std::strstr(op.name, filter) != nullptr;
       }))) {
    std::fprintf(stderr, "usage: uv0626 [even side 4..256] [samples 0..100] [operator substring]\n");
    return 2;
  }
  std::fprintf(stderr, "LSCM solver: %s\n", uvops::lscm_solver_name());
  std::puts("fixture,operator,faces,selected,threads,sample,ms,new_calls,new_bytes,signature,result_count,distortion_mean,distortion_worst,seam_length");
  return run_fixture(side, false, filter, samples) && run_fixture(side, true, filter, samples) ? 0 : 1;
}
