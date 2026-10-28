// SPDX-License-Identifier: GPL-2.0-or-later
// Stroke objects (ADR 0084, task 0248): the mesh from the stroke list, the list as text.
#include "stroke_object.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace bl {

namespace {

Vec3 safe_normalize(Vec3 v, Vec3 fallback) {
  const float l = length(v);
  return l > 1e-8f ? v / l : fallback;
}

/* A right vector for a quad facing `n` with `up` its vertical (made orthogonal to n). */
void card_frame(Vec3 n, Vec3 up_in, Vec3 &right, Vec3 &up) {
  up = up_in - n * dot(up_in, n);
  if (length(up) < 1e-6f) up = std::fabs(n.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f), up = up - n * dot(up, n);
  up = safe_normalize(up, Vec3(0.0f, 1.0f, 0.0f));
  right = safe_normalize(cross(up, n), Vec3(1.0f, 0.0f, 0.0f));
}

}  // namespace

void Strokes::reflect(Reflector &r) {
  static const char *modes[] = {"Own", "Surface", "Canopy"};
  r.enumeration("Normals", normals, modes, 3);
  r.help("How the cards shade: Own - each card's facing (a flat sprite); Surface - the normal of the surface it was\n"
         "painted on (leaves that take their branch's light); Canopy - away from the Canopy centre (a bush of flat cards\n"
         "shades as one round volume: the hand-painted foliage trick).");
  r.field("Canopy", canopy);
  if (r.serializing()) {
    std::string text = strokes_to_text(*this);
    r.text("Strokes", text);
    if (text != strokes_to_text(*this)) strokes_from_text(text, *this);  // (loading)
  }
}

std::string strokes_to_text(const Strokes &s) {
  std::string out;
  char buf[256];
  for (const StrokeCard &c : s.cards) {
    std::snprintf(buf, sizeof buf, "C %g %g %g %g %g %g %g %g %g %g %g %g %g\n", c.center.x, c.center.y, c.center.z, c.facing.x, c.facing.y, c.facing.z, c.up.x, c.up.y, c.up.z, c.surface_normal.x, c.surface_normal.y,
                  c.surface_normal.z, c.size);
    out += buf;
  }
  for (const StrokeRibbon &rb : s.ribbons) {
    std::snprintf(buf, sizeof buf, "R %g %g %g %g %g %g %g %zu", rb.facing.x, rb.facing.y, rb.facing.z, rb.surface_normal.x, rb.surface_normal.y, rb.surface_normal.z, rb.width, rb.points.size());
    out += buf;
    for (const Vec3 &p : rb.points) {
      std::snprintf(buf, sizeof buf, " %g %g %g", p.x, p.y, p.z);
      out += buf;
    }
    out += "\n";
  }
  return out;
}

bool strokes_from_text(const std::string &text, Strokes &s) {
  s.cards.clear(), s.ribbons.clear();
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream ls(line);
    char kind = 0;
    ls >> kind;
    if (kind == 'C') {
      StrokeCard c;
      ls >> c.center.x >> c.center.y >> c.center.z >> c.facing.x >> c.facing.y >> c.facing.z >> c.up.x >> c.up.y >> c.up.z >> c.surface_normal.x >> c.surface_normal.y >> c.surface_normal.z >> c.size;
      if (!ls.fail()) s.cards.push_back(c);
    }
    else if (kind == 'R') {
      StrokeRibbon rb;
      size_t n = 0;
      ls >> rb.facing.x >> rb.facing.y >> rb.facing.z >> rb.surface_normal.x >> rb.surface_normal.y >> rb.surface_normal.z >> rb.width >> n;
      if (ls.fail() || n > 100000) continue;
      for (size_t i = 0; i < n && !ls.fail(); i++) {
        Vec3 p;
        ls >> p.x >> p.y >> p.z;
        rb.points.push_back(p);
      }
      if (rb.points.size() >= 2) s.ribbons.push_back(std::move(rb));
    }
  }
  return true;
}

MeshPtr Strokes::build_mesh() const {
  auto m = std::make_shared<Mesh>();
  std::vector<Vec3> custom;
  auto shade_normal = [&](Vec3 own, Vec3 surface, Vec3 at) {
    if (normals == 1) return safe_normalize(surface, own);
    if (normals == 2) return safe_normalize(at - canopy, own);
    return own;
  };
  for (const StrokeCard &c : cards) {
    const Vec3 n = safe_normalize(c.facing, Vec3(0.0f, 0.0f, 1.0f));
    Vec3 right, up;
    card_frame(n, c.up, right, up);
    const float h = std::max(1e-4f, c.size) * 0.5f;
    const uint32_t v0 = m->add_vert(c.center - right * h - up * h), v1 = m->add_vert(c.center + right * h - up * h), v2 = m->add_vert(c.center + right * h + up * h), v3 = m->add_vert(c.center - right * h + up * h);
    const uint32_t vs[4] = {v0, v1, v2, v3}, back[4] = {v3, v2, v1, v0};
    const Vec2 uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}, uvb[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
    m->add_face(vs, 4, uv);
    m->add_face(back, 4, uvb);  // (the back as a face of its own: a renderer's back-side flip never turns the shared custom normal)
    for (int k = 0; k < 4; k++) custom.push_back(shade_normal(n, c.surface_normal, m->positions[vs[k]]));
  }
  for (const StrokeRibbon &rb : ribbons) {
    const size_t n = rb.points.size();
    if (n < 2) continue;
    const Vec3 facing = safe_normalize(rb.facing, Vec3(0.0f, 0.0f, 1.0f));
    float total = 0.0f;
    std::vector<float> along(n, 0.0f);
    for (size_t i = 1; i < n; i++) along[i] = along[i - 1] + length(rb.points[i] - rb.points[i - 1]);
    total = std::max(1e-6f, along[n - 1]);
    std::vector<uint32_t> left(n), right(n);
    for (size_t i = 0; i < n; i++) {
      const Vec3 t = safe_normalize((i + 1 < n ? rb.points[i + 1] : rb.points[i]) - (i > 0 ? rb.points[i - 1] : rb.points[i]), Vec3(1.0f, 0.0f, 0.0f));
      const Vec3 side = safe_normalize(cross(facing, t), Vec3(0.0f, 1.0f, 0.0f)) * (std::max(1e-4f, rb.width) * 0.5f);
      left[i] = m->add_vert(rb.points[i] - side), right[i] = m->add_vert(rb.points[i] + side);
      custom.push_back(shade_normal(facing, rb.surface_normal, rb.points[i]));
      custom.push_back(shade_normal(facing, rb.surface_normal, rb.points[i]));
    }
    for (size_t i = 1; i < n; i++) {
      const uint32_t vs[4] = {left[i - 1], right[i - 1], right[i], left[i]}, back[4] = {left[i], right[i], right[i - 1], left[i - 1]};
      const float u0 = along[i - 1] / total, u1 = along[i] / total;
      const Vec2 uv[4] = {{u0, 0.0f}, {u0, 1.0f}, {u1, 1.0f}, {u1, 0.0f}}, uvb[4] = {{u1, 0.0f}, {u1, 1.0f}, {u0, 1.0f}, {u0, 0.0f}};
      m->add_face(vs, 4, uv);
      m->add_face(back, 4, uvb);
    }
  }
  m->sync_attributes();
  m->custom_normals = std::move(custom);
  m->touch();
  return m;
}

}  // namespace bl
