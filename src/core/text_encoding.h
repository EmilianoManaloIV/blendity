// SPDX-License-Identifier: GPL-2.0-or-later
// Text in other encodings, to UTF-8 (task 0136): MMD's PMX is UTF-16 (or UTF-8), its PMD and VMD Shift-JIS. Shift-JIS
// is decoded by the platform (Windows' code page 932, iconv on Linux and macOS): no table of our own, no dependency.
#pragma once

#include <string>

namespace bl {

/* UTF-16 little-endian bytes to UTF-8 (surrogate pairs joined; a lone surrogate becomes U+FFFD). */
std::string utf16le_to_utf8(const std::string &bytes);
/* Shift-JIS (Microsoft's code page 932) to UTF-8; stops at the first NUL (fixed-size MMD fields). Undecodable bytes
 * become U+FFFD; plain ASCII passes through. */
std::string sjis_to_utf8(const std::string &bytes);

}  // namespace bl
