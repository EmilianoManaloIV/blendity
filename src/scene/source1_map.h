// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace bl {

class SourceContent;
struct ImportResult;

bool import_source1_map(const SourceContent &content, const std::string &virtual_path, ImportResult &out);

}  // namespace bl
