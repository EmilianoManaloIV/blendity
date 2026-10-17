// SPDX-License-Identifier: GPL-2.0-or-later
/* Build-time helper (task 0097): compiles a CUDA file to PTX with NVRTC, CUDA's runtime compiler. NVRTC brings its own
 * headers and needs no host compiler, where nvcc runs the C++ compiler's own headers through CUDA's front end - and
 * CUDA 12.8's can't parse Visual Studio 2026's. Used by build.bat only when the CUDA Toolkit is on this machine.
 *   ptx_compile <in.cu> <out.ptx> [-I<dir>...] [other NVRTC options...] */
#include <nvrtc.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: ptx_compile <in.cu> <out.ptx> [options...]\n");
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::stringstream src;
  src << in.rdbuf();
  if (!in) {
    std::fprintf(stderr, "ptx_compile: can't read %s\n", argv[1]);
    return 1;
  }
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, src.str().c_str(), argv[1], 0, nullptr, nullptr) != NVRTC_SUCCESS) return 1;
  std::vector<const char *> opts(argv + 3, argv + argc);
  const nvrtcResult r = nvrtcCompileProgram(prog, (int)opts.size(), opts.data());
  size_t log_size = 0;
  nvrtcGetProgramLogSize(prog, &log_size);
  if (log_size > 1) {
    std::string log(log_size, '\0');
    nvrtcGetProgramLog(prog, &log[0]);
    std::fprintf(stderr, "%s\n", log.c_str());
  }
  if (r != NVRTC_SUCCESS) return 1;
  size_t n = 0;
  nvrtcGetPTXSize(prog, &n);
  std::string ptx(n, '\0');
  nvrtcGetPTX(prog, &ptx[0]);
  nvrtcDestroyProgram(&prog);
  std::ofstream out(argv[2], std::ios::binary);
  out.write(ptx.data(), (std::streamsize)(n > 0 && ptx[n - 1] == '\0' ? n - 1 : n));
  return out ? 0 : 1;
}
