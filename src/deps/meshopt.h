// SPDX-License-Identifier: GPL-2.0-or-later
// meshoptimizer is a DLL in Blender's Windows libraries: its API macro must say
// so before the header is included (its CMake config normally does this).
#pragma once

#ifdef BL_WITH_MESHOPT
#  if defined(_WIN32) && !defined(MESHOPTIMIZER_API)
#    define MESHOPTIMIZER_API __declspec(dllimport)
#  endif
#  include <meshoptimizer.h>
#endif
