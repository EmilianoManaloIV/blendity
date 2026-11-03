// SPDX-License-Identifier: GPL-2.0-or-later
#include "unity_sdk_types.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace bl {

namespace {
/* MD4 (RFC 1320), as Unity hashes a script's full name for its fileID. */
uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
void md4(const uint8_t *data, size_t len, uint8_t out[16]) {
  std::vector<uint8_t> msg(data, data + len);
  const uint64_t bits = (uint64_t)len * 8;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  for (int i = 0; i < 8; i++) msg.push_back((uint8_t)(bits >> (8 * i)));
  uint32_t a = 0x67452301, b = 0xEFCDAB89, c = 0x98BADCFE, d = 0x10325476;
  static const int k2[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15}, k3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
  static const int s1[4] = {3, 7, 11, 19}, s2[4] = {3, 5, 9, 13}, s3[4] = {3, 9, 11, 15};
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t X[16];
    for (int i = 0; i < 16; i++) X[i] = (uint32_t)msg[off + i * 4] | (uint32_t)msg[off + i * 4 + 1] << 8 | (uint32_t)msg[off + i * 4 + 2] << 16 | (uint32_t)msg[off + i * 4 + 3] << 24;
    uint32_t aa = a, bb = b, cc = c, dd = d;
    auto F = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); };
    auto G = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (x & z) | (y & z); };
    auto H = [](uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; };
    for (int i = 0; i < 16; i++) {  // (round 1)
      if (i % 4 == 0) a = rol(a + F(b, c, d) + X[i], s1[0]);
      else if (i % 4 == 1) d = rol(d + F(a, b, c) + X[i], s1[1]);
      else if (i % 4 == 2) c = rol(c + F(d, a, b) + X[i], s1[2]);
      else b = rol(b + F(c, d, a) + X[i], s1[3]);
    }
    for (int i = 0; i < 16; i++) {  // (round 2)
      if (i % 4 == 0) a = rol(a + G(b, c, d) + X[k2[i]] + 0x5A827999u, s2[0]);
      else if (i % 4 == 1) d = rol(d + G(a, b, c) + X[k2[i]] + 0x5A827999u, s2[1]);
      else if (i % 4 == 2) c = rol(c + G(d, a, b) + X[k2[i]] + 0x5A827999u, s2[2]);
      else b = rol(b + G(c, d, a) + X[k2[i]] + 0x5A827999u, s2[3]);
    }
    for (int i = 0; i < 16; i++) {  // (round 3)
      if (i % 4 == 0) a = rol(a + H(b, c, d) + X[k3[i]] + 0x6ED9EBA1u, s3[0]);
      else if (i % 4 == 1) d = rol(d + H(a, b, c) + X[k3[i]] + 0x6ED9EBA1u, s3[1]);
      else if (i % 4 == 2) c = rol(c + H(d, a, b) + X[k3[i]] + 0x6ED9EBA1u, s3[2]);
      else b = rol(b + H(c, d, a) + X[k3[i]] + 0x6ED9EBA1u, s3[3]);
    }
    a += aa, b += bb, c += cc, d += dd;
  }
  const uint32_t w[4] = {a, b, c, d};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(w[i] >> (8 * j));
}

const char *const kClasses[] = {
    "VRC.SDK3.Avatars.Components.VRCAvatarDescriptor", "VRC.SDK3.Avatars.Components.VRCStation", "VRC.SDK3.Avatars.Components.VRCHeadChop",
    "VRC.SDK3.Avatars.Components.VRCImpostorSettings", "VRC.SDK3.Avatars.Components.VRCSpatialAudioSource",
    "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBone", "VRC.SDK3.Dynamics.PhysBone.Components.VRCPhysBoneCollider",
    "VRC.SDK3.Dynamics.Contact.Components.VRCContactSender", "VRC.SDK3.Dynamics.Contact.Components.VRCContactReceiver",
    "VRC.SDK3.Dynamics.Constraint.Components.VRCParentConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCPositionConstraint",
    "VRC.SDK3.Dynamics.Constraint.Components.VRCRotationConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCScaleConstraint",
    "VRC.SDK3.Dynamics.Constraint.Components.VRCLookAtConstraint", "VRC.SDK3.Dynamics.Constraint.Components.VRCAimConstraint",
    "VRC.SDK3.Components.VRCSceneDescriptor", "VRC.SDK3.Components.VRCPickup", "VRC.SDK3.Components.VRCMirrorReflection", "VRC.SDK3.Components.VRCObjectSync",
    "VRC.SDK3.Components.VRCObjectPool", "VRC.SDK3.Components.VRCPortalMarker", "VRC.SDK3.Components.VRCAvatarPedestal", "VRC.SDK3.Components.VRCUiShape",
    "VRC.SDK3.Components.VRCUrlInputField", "VRC.SDK3.Components.VRCStation", "VRC.SDK3.Components.VRCSpatialAudioSource",
    "VRC.SDK3.Video.Components.VRCUnityVideoPlayer", "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoPlayer",
    "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoScreen", "VRC.SDK3.Video.Components.AVPro.VRCAVProVideoSpeaker", "VRC.Core.PipelineManager",
};  // (UdonBehaviour is a .cs script in the package: named by its file; review 403)
}  // namespace

int32_t unity_script_file_id(const std::string &full_name) {
  std::string s("s\0\0\0", 4);
  const size_t dot = full_name.rfind('.');  // (Unity hashes the namespace and the class with no dot between: "VRC.CorePipelineManager")
  s += dot == std::string::npos ? full_name : full_name.substr(0, dot) + full_name.substr(dot + 1);
  uint8_t h[16];
  md4((const uint8_t *)s.data(), s.size(), h);
  const uint32_t v = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
  return (int32_t)v;
}

std::string unity_sdk_class_name(int32_t file_id) {
  static const std::unordered_map<int32_t, std::string> table = [] {
    std::unordered_map<int32_t, std::string> t;
    for (const char *full : kClasses) {
      const char *dot = std::strrchr(full, '.');
      t[unity_script_file_id(full)] = dot ? dot + 1 : full;
    }
    return t;
  }();
  const auto it = table.find(file_id);
  return it == table.end() ? std::string() : it->second;
}

const char *const *unity_sdk_class_names(size_t *count) {
  if (count) *count = sizeof(kClasses) / sizeof(kClasses[0]);
  return kClasses;
}

}  // namespace bl
