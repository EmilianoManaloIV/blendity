// SPDX-License-Identifier: GPL-2.0-or-later
// Round 419 (ADR 0112): the allocator the build with Blender's libraries uses on Windows - oneTBB's tbbmalloc_proxy
// (Blender's own choice), which replaces malloc and new process-wide by being linked (build.bat; build.sh on Linux when
// asked for). The dependency-free build keeps the system allocator.
#include "allocator.h"

#if defined(BL_WITH_TBBMALLOC) && defined(_WIN32)
/* oneTBB's proxy (tbbmalloc_proxy.h): 0 when every function it replaces was replaced - false when the proxy turned
 * itself off (TBB_MALLOC_DISABLE_REPLACEMENT=1) or couldn't patch the runtime. Declared here: the header asks the
 * linker for the library by name, which this build passes by path. */
extern "C" __declspec(dllimport) int TBB_malloc_replacement_log(char ***function_replacement_log_ptr);
#endif

namespace bl {

bool allocator_replaced() {
#if defined(BL_WITH_TBBMALLOC) && defined(_WIN32)
  return TBB_malloc_replacement_log(nullptr) == 0;
#elif defined(BL_WITH_TBBMALLOC)
  return true;  // (Linux: the proxy's symbols interpose the C library's once it is loaded - linked, it is)
#else
  return false;
#endif
}

const char *allocator_name() {
#if defined(BL_WITH_TBBMALLOC)
  return allocator_replaced() ? "oneTBB's scalable allocator (tbbmalloc_proxy)" : "the system's (oneTBB's proxy is built in but turned off or failed)";
#else
  return "the system's";
#endif
}

}  // namespace bl
