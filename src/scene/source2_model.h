// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace bl {

class SourceContent;
struct ImportResult;

bool import_source2_model(const SourceContent &content, const std::string &virtual_path, ImportResult &out);
bool import_source2_renderable(const SourceContent &content, const std::string &virtual_path, int draw_call, ImportResult &out);

}  // namespace bl
