// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "material.h"

namespace bl {

class SourceContent;
struct ImportResult;

// Reads a mounted VMT and its textures. Missing or unsupported resources yield
// a fallback material and a note with the virtual path.
MaterialPtr import_source1_material(const SourceContent &content, const std::string &vmt_path, ImportResult &out);

}  // namespace bl
