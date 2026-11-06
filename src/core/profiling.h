// SPDX-License-Identifier: GPL-2.0-or-later
// Round 419 (ADR 0112): Tracy zones for a developer's build (BLENDITY_TRACY=1 - the Tracy client compiled in from
// ../blender/extra/tracy, the profiler's viewer connecting over the network). Off, the macros are nothing: no code, no
// data, no cost, and the shipped builds never have it.
#pragma once

#ifdef BL_WITH_TRACY
#  include <tracy/Tracy.hpp>
#  define BL_ZONE(name) ZoneScopedN(name)
#  define BL_FRAME_MARK() FrameMark
#else
#  define BL_ZONE(name) \
    do {               \
    } while (0)
#  define BL_FRAME_MARK() \
    do {                  \
    } while (0)
#endif
