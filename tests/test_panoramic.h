// SPDX-License-Identifier: GPL-2.0-or-later
// Round 538 (task 0518): shared helpers of the panoramic camera tests (test_render.cpp, test_editor.cpp). A scene of six
// emissive, differently coloured walls round a camera at the origin looking along +Z, one wall in each axis direction, so
// the colour a panoramic picture shows at a pixel says which direction the pixel looks along.
#pragma once

#include "test_common.h"
#include "../src/editor/panoramic_view.h"

/* Face order as panoramic_face(): 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z (ahead), 5 -Z (behind). */
inline Vec3 r538_wall_colour(int face) {
  static const Vec3 c[6] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
  return c[face];
}
inline const char *r538_wall_name(int face) {
  static const char *n[6] = {"red (+X)", "green (-X)", "blue (+Y)", "yellow (-Y)", "cyan (+Z)", "magenta (-Z)"};
  return face >= 0 && face < 6 ? n[face] : "none";
}

/* Which wall a displayed pixel shows (by its chroma, so the lighting and the view transform don't matter), -1 for grey / black / none. */
inline int r538_classify(uint32_t px) {
  const int c[3] = {(int)((px >> 16) & 255), (int)((px >> 8) & 255), (int)(px & 255)};
  const int mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
  if (mx - mn < 40) return -1;
  float best = 1e9f;
  int face = -1;
  for (int f = 0; f < 6; f++) {
    const Vec3 w = r538_wall_colour(f);
    float d = 0;
    for (int k = 0; k < 3; k++) d += std::fabs((c[k] - mn) / (float)(mx - mn) - w[k]);
    if (d < best) best = d, face = f;
  }
  return best < 0.9f ? face : -1;
}

/* The wall a unit direction points at squarely (its largest component at least `dominant`), -1 between walls. */
inline int r538_wall_of(Vec3 d, float dominant = 0.85f) {
  const float len = length(d);
  if (!(len > 0.5f)) return -1;
  d = d / len;
  const float a[3] = {std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)};
  int axis = a[0] >= a[1] && a[0] >= a[2] ? 0 : a[1] >= a[2] ? 1 : 2;
  if (a[axis] < dominant) return -1;
  const float s = axis == 0 ? d.x : axis == 1 ? d.y : d.z;
  return axis * 2 + (s < 0 ? 1 : 0);
}

/* The scene: everything but the Main Camera off, the camera at the origin facing +Z, six 14 m walls 5 m away in emissive,
 * double-sided colours. Returns the camera's object. */
inline GameObject *r538_six_walls(Editor &ed) {
  ed.scene().for_each([&](GameObject &g) {
    if (g.name != "Main Camera") g.active = false;
  });
  GameObject *cam = ed.scene().find_by_name("Main Camera");
  if (cam) {
    cam->set_local_position({0, 0, 0});
    cam->set_local_rotation(Quat());
  }
  static const Vec3 pos[6] = {{5, 0, 0}, {-5, 0, 0}, {0, 5, 0}, {0, -5, 0}, {0, 0, 5}, {0, 0, -5}};
  static const Vec3 euler[6] = {{0, 90, 0}, {0, 90, 0}, {90, 0, 0}, {90, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (int f = 0; f < 6; f++) {
    GameObject *w = ed.scene().create(std::string("Wall ") + r538_wall_name(f));
    w->add<MeshFilter>()->mesh = primitives::quad(14.0f);
    MaterialPtr m = make_material(std::string("wall ") + r538_wall_name(f), Vec3(0.0f));
    m->emission = r538_wall_colour(f);
    m->emission_strength = 1.0f;
    m->specular = 0.0f;
    m->double_sided = true;
    w->add<MeshRenderer>()->materials = {m};
    w->set_local_position(pos[f]);
    w->set_local_rotation(Quat::euler(euler[f]));
  }
  ed.mark_changed_for_test("six walls");
  return cam;
}

/* How a picture compares with the projection's own idea of it. `at(x, y)` gives the pixel. A pixel is judged when its centre and
 * the four points half a pixel off it all look squarely at the same wall (so a seam or a fisheye's rim, where one pixel
 * covers two walls, is left out). */
struct R538Check {
  int judged = 0, wrong = 0, black_outside = 0, outside_wrong = 0;
  std::string first_wrong;
};
inline R538Check r538_compare(const PanoramicParams &p, int w, int h, const std::function<uint32_t(int, int)> &at, int stride = 3, int margin = 0) {
  R538Check r;
  for (int y = margin; y < h - margin; y += stride)
    for (int x = margin; x < w - margin; x += stride) {
      const float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
      const Vec3 d = panoramic_direction(p, u, v);
      const uint32_t px = at(x, y);
      if (length(d) < 0.5f) {  // (outside: black - but a rim pixel is part inside: only pixels a whole pixel away from the circle count)
        bool deep = true;
        for (int k = 0; k < 4 && deep; k++) {
          const float du = (k < 2 ? (k ? 1.0f : -1.0f) : 0.0f) / w, dv = (k >= 2 ? (k == 3 ? 1.0f : -1.0f) : 0.0f) / h;
          deep = length(panoramic_direction(p, u + du, v + dv)) < 0.5f;
        }
        if (!deep) continue;
        r.black_outside++;
        if ((px & 0xFFFFFF) != 0) r.outside_wrong++;
        continue;
      }
      const int want = r538_wall_of(d);
      if (want < 0) continue;
      bool steady = true;
      for (int k = 0; k < 4 && steady; k++) {
        const float du = (k < 2 ? (k ? 0.5f : -0.5f) : 0.0f) / w, dv = (k >= 2 ? (k == 3 ? 0.5f : -0.5f) : 0.0f) / h;
        steady = r538_wall_of(panoramic_direction(p, u + du, v + dv)) == want;
      }
      if (!steady) continue;
      r.judged++;
      const int got = r538_classify(px);
      if (got != want) {
        r.wrong++;
        if (r.first_wrong.empty()) r.first_wrong = strprintf("pixel (%d, %d) of %dx%d: wanted %s, got %s (0x%06X)", x, y, w, h, r538_wall_name(want), r538_wall_name(got), px & 0xFFFFFF);
      }
    }
  return r;
}

/* The camera preview's picture inside its inset: the rectangle the inset draws the picture in (the header row and the frame cut off). */
inline Recti r538_preview_picture(Editor &ed) {
  const Recti pr = ed.cam_preview_rect_for_test();
  const int pad4 = ed.ui_for_test().px(4), row = ed.ui_for_test().row_h();
  if (pr.w <= 0) return Recti{};
  return Recti{pr.x + pad4, pr.y + row + pad4, pr.w - 2 * pad4, pr.h - row - 2 * pad4};
}
