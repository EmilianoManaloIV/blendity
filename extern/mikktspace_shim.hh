// SPDX-License-Identifier: GPL-2.0-or-later
// Provides the few build-environment definitions Blender's MikkTSpace port
// expects (normally from BLI_sys_types.h / BLI_compiler_compat.h), so the
// vendored files in extern/mikktspace stay byte-identical to Blender's.
#pragma once

#include <cstdint>

using uint = unsigned int;
#ifndef LIKELY
#  define LIKELY(x) (x)
#endif
#ifndef UNLIKELY
#  define UNLIKELY(x) (x)
#endif

#include "mikktspace/mikktspace.hh"
