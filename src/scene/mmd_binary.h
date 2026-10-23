// SPDX-License-Identifier: GPL-2.0-or-later
// Little-endian reads for MikuMikuDance's files (PMX, PMD, VMD: tasks 0136-0137) that never pass the end: past it,
// `bad` is set and everything after reads as zero.
#pragma once

#include "../core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace bl {

struct MmdReader {
  const std::string &d;
  size_t at = 0;
  bool bad = false;
  explicit MmdReader(const std::string &data) : d(data) {}
  bool need(size_t n) {
    if (bad || n > d.size() - at) bad = true;
    return !bad;
  }
  template <class T> T get() {
    T v{};
    if (!need(sizeof(T))) return v;
    std::memcpy(&v, d.data() + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  float f() {
    const float v = get<float>();
    return std::isfinite(v) ? v : 0.0f;
  }
  Vec3 v3() {
    const float x = f(), y = f(), z = f();
    return {x, y, z};
  }
  Vec4 v4() {
    const float x = f(), y = f(), z = f(), w = f();
    return {x, y, z, w};
  }
  std::string bytes(size_t n) {
    if (!need(n)) return {};
    std::string s = d.substr(at, n);
    at += n;
    return s;
  }
  void skip(size_t n) {
    if (need(n)) at += n;
  }
  /* A count of records at least `size` bytes each: more than could fit is a damaged file. */
  size_t count(int64_t n, size_t size) {
    if (n < 0 || (size && (uint64_t)n > (d.size() - std::min(at, d.size())) / size)) bad = true;
    return bad ? 0 : (size_t)n;
  }
  /* PMX indices: 1, 2 or 4 bytes, signed (-1: none); vertex indices of 1 or 2 bytes are unsigned. */
  int32_t index(int size) {
    if (size == 1) return get<int8_t>();
    if (size == 2) return get<int16_t>();
    return get<int32_t>();
  }
  int32_t vertex(int size) {
    if (size == 1) return get<uint8_t>();
    if (size == 2) return get<uint16_t>();
    return get<int32_t>();
  }
};

}  // namespace bl
