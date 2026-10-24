// SPDX-License-Identifier: GPL-2.0-or-later
/* A synthetic MMD character (PMX and PMD) written by the tests, so no third-party model is needed (round 156, task
 * 0136). Shared by the unit tests (tests/test_assets.cpp) and the stress program's codecs section, which mutates it
 * 240 ways (task 0177). */
#pragma once

#include "../../src/core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace bl {

/* A synthetic MMD character written by the tests (no third-party model): a lower body, a left leg of three bones with
 * MMD's leg IK, a two-bone hair chain under physics, a front-facing quad skinned to the thigh and the knee, and a hair
 * card on the hair; a vertex morph "あ" and a group morph "笑い" (half of it); one material with a texture in a
 * subfolder (a backslash path), an edge and a toon ramp; a right leg without IK (two legs: a rig the walking tools
 * read). MMD's space: facing -Z, 8 cm units. */
struct R156Bin {
  std::string s;
  void u8(int v) { s += (char)(uint8_t)v; }
  void u16(int v) { u8(v & 0xFF), u8(v >> 8 & 0xFF); }
  void i32(int64_t v) {
    for (int k = 0; k < 4; k++) u8((int)((uint64_t)v >> (8 * k) & 0xFF));
  }
  void f(float v) {
    char b[4];
    std::memcpy(b, &v, 4);
    s.append(b, 4);
  }
  void v3(Vec3 v) { f(v.x), f(v.y), f(v.z); }
  /* PMX text: UTF-8 as it is, or UTF-16LE (the common case) from UTF-8 (the Basic Multilingual Plane is enough). */
  void text(const std::string &t, bool utf8) {
    if (utf8) return i32((int64_t)t.size()), void(s += t);
    std::string w;
    for (size_t i = 0; i < t.size();) {
      const uint8_t a = (uint8_t)t[i];
      uint32_t c = a;
      if (a >= 0xE0) c = (a & 0x0F) << 12 | ((uint8_t)t[i + 1] & 0x3F) << 6 | ((uint8_t)t[i + 2] & 0x3F), i += 3;
      else if (a >= 0xC0) c = (a & 0x1F) << 6 | ((uint8_t)t[i + 1] & 0x3F), i += 2;
      else i++;
      w += (char)(c & 0xFF), w += (char)(c >> 8);
    }
    i32((int64_t)w.size()), s += w;
  }
  void fixed(const std::string &t, size_t n) { s += t.substr(0, n) + std::string(n - std::min(n, t.size()), '\0'); }
};
/* The character's parts, shared by the PMX and PMD writers. */
struct R156Bone {
  const char *name, *sjis;
  Vec3 head;
  int parent, tail;  // tail: a bone, or -1 (then `offset`)
  Vec3 offset;
};
inline const R156Bone r156_bones[] = {
    {"センター", "\x83\x5a\x83\x93\x83\x5e\x81\x5b", {0, 8, 0}, -1, -1, {0, -1, 0}},
    {"下半身", "\x89\xba\x94\xbc\x90\x67", {0, 10, 0}, 0, -1, {0, -2, 0}},
    {"左足", "\x8d\xb6\x91\xab", {1, 10, 0}, 1, 3, {}},
    {"左ひざ", "\x8d\xb6\x82\xd0\x82\xb4", {1, 5, 0}, 2, 4, {}},
    {"左足首", "\x8d\xb6\x91\xab\x8e\xf1", {1, 1, 0}, 3, -1, {0, -1, -1}},
    {"左足ＩＫ", "\x8d\xb6\x91\xab\x82\x68\x82\x6a", {1, 1, 0}, 0, -1, {0, 0, 1}},
    {"髪1", "\x94\xaf\x31", {0, 15, 1}, 1, 7, {}},
    {"髪2", "\x94\xaf\x32", {0, 12, 1.5f}, 6, -1, {0, -2, 0.5f}},
    {"右足", "\x89\x45\x91\xab", {-1, 10, 0}, 1, 9, {}},
    {"右ひざ", "\x89\x45\x82\xd0\x82\xb4", {-1, 5, 0}, 8, 10, {}},
    {"右足首", "\x89\x45\x91\xab\x8e\xf1", {-1, 1, 0}, 9, -1, {0, -1, -1}}};
constexpr int r156_nbones = (int)(sizeof(r156_bones) / sizeof(r156_bones[0]));
/* Vertices: the quad on the leg (0-3, facing the viewer at -Z), the hair card (4-7). */
struct R156Vert {
  Vec3 p;
  float u, v;
  int b0, b1;
  float w0;
};
inline const R156Vert r156_verts[] = {{{0.5f, 4, -0.5f}, 0, 1, 3, 3, 1}, {{0.5f, 9, -0.5f}, 0, 0, 2, 2, 1}, {{1.5f, 4, -0.5f}, 1, 1, 3, 3, 1},
                                      {{1.5f, 9, -0.5f}, 1, 0, 2, 2, 1}, {{-0.5f, 15, 1}, 0, 0, 6, 6, 1}, {{-0.5f, 12, 1.5f}, 0, 1, 6, 7, 0.5f},
                                      {{0.5f, 15, 1}, 1, 0, 6, 6, 1}, {{0.5f, 12, 1.5f}, 1, 1, 6, 7, 0.5f}};
inline const int r156_tris[] = {0, 1, 2, 2, 1, 3, 4, 6, 5, 6, 7, 5};  // (clockwise as the viewer sees them: MMD's front)
/* What the PMX writer can add to the character: every optional record MMD files use (the "kitchen sink": two extra
 * UVs, BDEF4 / SDEF / QDEF vertices, every bone flag, bone / UV / material / flip / impulse morphs, a display frame, a
 * joint); 130 unused vertices first, so the real ones have one-byte indices past 127 (unsigned); D bones the leg's
 * skin is weighted to; a fork in the hair's dynamic bones; MMD's shared toon03 instead of a ramp beside the model; a
 * second copy of the leg quad with other normals (a hard edge); the triangles written the other way round. */
struct R156Opts {
  bool utf8 = false, sink = false, pad = false, dbones = false, fork = false, shared_toon = false, hard = false, mirrored = false;
};
inline std::string r156_pmx(const R156Opts &o) {
  const bool utf8 = o.utf8;
  const int pad = o.pad ? 130 : 0, extra_uvs = o.sink ? 2 : 0;
  const int nverts = pad + 8 + (o.hard ? 4 : 0), nbones = r156_nbones + (o.dbones ? 3 : 0) + (o.fork ? 1 : 0);
  const int dleg[3] = {r156_nbones, r156_nbones + 1, r156_nbones + 2};  // (左足D, 左ひざD, 左足首D)
  auto leg_bone = [&](int bone) { return o.dbones && bone >= 2 && bone <= 4 ? dleg[bone - 2] : bone; };
  R156Bin b;
  b.s = "PMX ";
  b.f(2.0f);
  b.u8(8);
  for (int g : {utf8 ? 1 : 0, extra_uvs, 1, 1, 1, 2, 1, 1}) b.u8(g);  // (one-byte indices but the bones' two: both kinds read)
  b.text("テスト", utf8), b.text("Test", utf8), b.text("", utf8), b.text("", utf8);
  b.i32(nverts);
  for (int i = 0; i < pad; i++) {
    b.v3({0, 0, 0}), b.v3({0, 0, -1}), b.f(0), b.f(0);
    for (int k = 0; k < 4 * extra_uvs; k++) b.f(0);
    b.u8(0), b.u16(0), b.f(1.0f);
  }
  for (int copy = 0; copy < (o.hard ? 2 : 1); copy++)
    for (size_t i = 0; i < 8; i++) {
      if (copy == 1 && i >= 4) break;  // (the hard copy: the leg quad again)
      const R156Vert &v = r156_verts[i];
      b.v3(v.p), b.v3(copy ? Vec3(0, 1, 0) : Vec3(0, 0, -1)), b.f(v.u), b.f(v.v);
      for (int k = 0; k < 4 * extra_uvs; k++) b.f(0.25f);
      const int b0 = leg_bone(v.b0), b1 = leg_bone(v.b1);
      if (o.sink && i == 0) {  // (BDEF4: the one bone four times)
        b.u8(2);
        for (int k = 0; k < 4; k++) b.u16(b0);
        for (int k = 0; k < 4; k++) b.f(0.25f);
      }
      else if (o.sink && i == 1) {  // (SDEF: as BDEF2, then its centre and two points)
        b.u8(3), b.u16(b0), b.u16(b1), b.f(v.w0);
        for (int k = 0; k < 9; k++) b.f(0);
      }
      else if (o.sink && i == 2) {  // (QDEF)
        b.u8(4);
        for (int k = 0; k < 4; k++) b.u16(b0);
        for (int k = 0; k < 4; k++) b.f(0.25f);
      }
      else if (v.w0 >= 1.0f) b.u8(0), b.u16(b0);
      else b.u8(1), b.u16(b0), b.u16(b1), b.f(v.w0);
      b.f(1.0f);
    }
  const int ntris = 4 + (o.hard ? 2 : 0);
  b.i32(3 * ntris);
  for (int t = 0; t < ntris; t++) {
    const int base = t < 4 ? pad : pad + 8 - 4;  // (the hard copy's triangles on its vertices)
    const int *tri = r156_tris + 3 * (t < 4 ? t : t - 4);
    if (o.mirrored) b.u8(base + tri[0]), b.u8(base + tri[2]), b.u8(base + tri[1]);
    else b.u8(base + tri[0]), b.u8(base + tri[1]), b.u8(base + tri[2]);
  }
  b.i32(2), b.text("tex\\skin.png", utf8), b.text("toon.png", utf8);
  b.i32(1);
  b.text("肌", utf8), b.text("skin", utf8);
  b.f(0.8f), b.f(0.6f), b.f(0.5f), b.f(1.0f), b.v3({0, 0, 0}), b.f(5.0f), b.v3({0.2f, 0.2f, 0.2f});
  b.u8(0x10 | 0x04);
  b.f(0.1f), b.f(0.05f), b.f(0.2f), b.f(1.0f), b.f(1.5f);  // (its edge: colour, size)
  b.u8(0), b.u8(0xFF), b.u8(0);                             // (texture 0, no sphere, mode 0)
  if (o.shared_toon) b.u8(1), b.u8(2);                      // (MMD's toon03)
  else b.u8(0), b.u8(1);                                    // (a toon texture: 1)
  b.text("", utf8), b.i32(3 * ntris);
  b.i32(nbones);
  for (int i = 0; i < nbones; i++) {
    const bool d = i >= r156_nbones && i < r156_nbones + 3 && o.dbones, hair3 = i == nbones - 1 && o.fork;
    const R156Bone &bn = r156_bones[d ? 2 + (i - r156_nbones) : hair3 ? 7 : i];
    b.text(d ? std::string(bn.name) + "D" : hair3 ? "髪3" : bn.name, utf8), b.text("", utf8);
    b.v3(d ? bn.head : hair3 ? Vec3(0, 12, -1.5f) : bn.head);
    b.u16(d ? (i == r156_nbones ? 1 : i - 1) : hair3 ? 6 : bn.parent < 0 ? 0xFFFF : bn.parent), b.i32(0);
    const bool ik = !d && !hair3 && std::string(bn.name) == "左足ＩＫ";
    const bool sink_flags = o.sink && !d && !hair3 && i == 7;  // (髪2: a fixed axis, local axes, an external parent)
    const bool tail_bone = bn.tail >= 0 && !hair3;
    b.u16((tail_bone ? 0x0001 : 0) | 0x0002 | 0x0008 | 0x0010 | (ik ? 0x0020 | 0x0004 : 0) | (d ? 0x0100 : 0) | (sink_flags ? 0x0400 | 0x0800 | 0x2000 : 0));
    if (tail_bone) b.u16(d ? leg_bone(bn.tail) : bn.tail);
    else b.v3(hair3 ? Vec3(0, -2, -0.5f) : bn.offset);
    if (d) b.u16(2 + (i - r156_nbones)), b.f(1.0f);  // (copies the leg bone's turn, all of it)
    if (sink_flags) b.v3({0, 1, 0}), b.v3({1, 0, 0}), b.v3({0, 0, 1}), b.i32(0);
    if (!ik) continue;
    b.u16(4), b.i32(40), b.f(2.0f), b.i32(2);
    b.u16(3), b.u8(1), b.v3({-3.14f, 0, 0}), b.v3({-0.01f, 0, 0});  // (the knee: limited to X)
    b.u16(2), b.u8(0);
  }
  b.i32(2 + (o.sink ? 5 : 0));
  b.text("あ", utf8), b.text("a", utf8), b.u8(3), b.u8(1), b.i32(1), b.u8(pad), b.v3({0, 1, 0});
  b.text("笑い", utf8), b.text("smile", utf8), b.u8(4), b.u8(0), b.i32(1), b.u8(0), b.f(0.5f);
  if (o.sink) {  // (a bone, a UV, a material, a flip and an impulse morph, one record each)
    b.text("bone", utf8), b.text("", utf8), b.u8(4), b.u8(2), b.i32(1), b.u16(2), b.v3({0, 0, 0}), b.f(0), b.f(0), b.f(0), b.f(1);
    b.text("uv", utf8), b.text("", utf8), b.u8(4), b.u8(3), b.i32(1), b.u8(pad), b.f(0), b.f(0), b.f(0), b.f(0);
    b.text("mat", utf8), b.text("", utf8), b.u8(4), b.u8(8), b.i32(1), b.u8(0), b.u8(0);
    for (int k = 0; k < 28; k++) b.f(1.0f);
    b.text("flip", utf8), b.text("", utf8), b.u8(4), b.u8(9), b.i32(1), b.u8(0), b.f(1.0f);
    b.text("impulse", utf8), b.text("", utf8), b.u8(4), b.u8(10), b.i32(1), b.u8(0), b.u8(0), b.v3({}), b.v3({});
  }
  b.i32(o.sink ? 1 : 0);  // (display frames)
  if (o.sink) b.text("Root", utf8), b.text("", utf8), b.u8(1), b.i32(2), b.u8(0), b.u16(0), b.u8(1), b.u8(0);
  const int nrigids = 2 + (o.fork ? 2 : 0);
  b.i32(nrigids);
  for (int k = 0; k < nrigids; k++) {
    const int bone = k == 0 ? 6 : k == 1 ? 1 : k == 2 ? 7 : nbones - 1;
    b.text("", utf8), b.text("", utf8), b.u16(bone), b.u8(0), b.u16(0), b.u8(0), b.v3({0.5f, 0, 0}), b.v3({}), b.v3({});
    for (int j = 0; j < 5; j++) b.f(0.5f);
    b.u8(bone == 1 ? 0 : 1);
  }
  b.i32(o.sink ? 1 : 0);  // (joints)
  if (o.sink) {
    b.text("j", utf8), b.text("", utf8), b.u8(0), b.u8(0), b.u8(1);
    for (int k = 0; k < 24; k++) b.f(0);
  }
  return b.s;
}
inline std::string r156_pmd() {
  R156Bin b;
  b.s = "Pmd";
  b.f(1.0f), b.fixed("\x83\x65\x83\x58\x83\x67", 20), b.fixed("", 256);
  b.i32(8);
  for (const R156Vert &v : r156_verts) {
    b.v3(v.p), b.v3({0, 0, -1}), b.f(v.u), b.f(v.v), b.u16(v.b0), b.u16(v.b1), b.u8((int)std::lround(v.w0 * 100)), b.u8(0);
  }
  b.i32(12);
  for (int i : r156_tris) b.u16(i);
  b.i32(1);
  b.v3({0.8f, 0.6f, 0.5f}), b.f(1.0f), b.f(5.0f), b.v3({0, 0, 0}), b.v3({0.2f, 0.2f, 0.2f}), b.u8(0), b.u8(1), b.i32(12);
  b.fixed("tex\\skin.png*face.sph", 20);
  b.u16(r156_nbones);
  for (const R156Bone &bn : r156_bones) {
    b.fixed(bn.sjis, 20), b.u16(bn.parent < 0 ? 0xFFFF : bn.parent), b.u16(bn.tail < 0 ? 0 : bn.tail), b.u8(0), b.u16(0), b.v3(bn.head);
  }
  b.u16(1), b.u16(5), b.u16(4), b.u8(2), b.u16(40), b.f(1.0f), b.u16(3), b.u16(2);
  /* Morphs: the base (vertex 0, where it is), then "あ" by its place in the base's list. */
  b.u16(2);
  b.fixed("base", 20), b.i32(1), b.u8(0), b.i32(0), b.v3(r156_verts[0].p);
  b.fixed("\x82\xa0", 20), b.i32(1), b.u8(3), b.i32(0), b.v3({0, 1, 0});
  b.u8(0), b.u8(0), b.i32(0), b.u8(0);  // (display lists, no English names)
  b.fixed("toon.png", 100);
  for (int k = 1; k < 10; k++) b.fixed("", 100);
  b.i32(1);
  b.fixed("", 20), b.u16(6), b.u8(0), b.u16(0), b.u8(0), b.v3({0.5f, 0, 0}), b.v3({}), b.v3({});
  for (int k = 0; k < 5; k++) b.f(0.5f);
  b.u8(1);
  b.i32(0);
  return b.s;
}

}  // namespace bl
