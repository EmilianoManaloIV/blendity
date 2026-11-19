// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "import.h"

namespace bl {
class SourceContent;

namespace source2 {

std::string compiled_path(const std::string &path);
MaterialPtr read_material(const SourceContent &content, const std::string &path, ImportResult &result);

}  // namespace source2
}  // namespace bl
