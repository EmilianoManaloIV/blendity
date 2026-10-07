// SPDX-License-Identifier: GPL-2.0-or-later
// Optional third-party libraries: the ones Blender itself builds against
// (blender/build_files/build_environment/cmake/versions.cmake), used when the
// prebuilt packages exist in blender/lib/<platform>. Each has a dependency-free
// fallback inside Blendity, so the editor works with or without them.
#pragma once

#include <string>
#include <vector>

namespace bl::deps {

struct Library {
  const char *name;
  bool enabled;          // compiled in (BL_WITH_*)
  std::string version;   // from the library's own headers, empty when disabled
  const char *used_for;  // what Blendity uses it for
  const char *fallback;  // what runs without it
};

const std::vector<Library> &libraries();
/* "Embree 4.4.1, OpenImageDenoise 2.5.0, ..." or "none (dependency-free build)". */
std::string summary();

}  // namespace bl::deps
