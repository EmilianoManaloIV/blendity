# SPDX-License-Identifier: GPL-2.0-or-later
"""Check optional Vulkan selection in real build plans without compiling anything.

On Linux or macOS, use Python 3.10+ with Bash, CMake and Make. On Windows,
use Python 3.11+ through the py -3 launcher used by the batch resolver,
with CMake and Ninja on PATH.

The scratch directory must be new. Use --source to test another revision's
build entry points. Logs and generated plans remain in the scratch directory.

    python tests/test_optional_vulkan_build.py build/vulkan-plan-test
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
WINDOWS = sys.platform == "win32"
MACOS = sys.platform == "darwin"
VULKAN_HEADER = "vulkan/include/vulkan/vulkan.h"
SHADERC_HEADER = "shaderc/include/shaderc/shaderc.h"
SHADERC_WIN = "shaderc/lib/shaderc_shared.lib"
SHADERC_UNIX = "shaderc/lib/libshaderc_combined.a"
SHADERC = SHADERC_WIN if WINDOWS else SHADERC_UNIX
TBB = "tbb/lib/tbb12.lib" if WINDOWS else f"tbb/lib/libtbb.{'dylib' if MACOS else 'so'}"
CASES = (
    ("complete", (), True, True),
    ("missing-vulkan-header", (VULKAN_HEADER,), True, False),
    ("missing-shaderc-header", (SHADERC_HEADER,), True, False),
    ("missing-shaderc-link", (SHADERC,), True, False),
    ("cpu-only", (VULKAN_HEADER, SHADERC_HEADER, SHADERC), True, False),
    ("no-bundle", (), False, False),
    ("forced-free", (), False, False),
)


def put(path, contents=""):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8", newline="\n")


def run(command, project, env, log):
    with log.open("wb") as stream:
        result = subprocess.run(command, cwd=project, env=env, stdout=stream,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{command[0]} exited {result.returncode}; see {log}")


def make_project(project, source):
    entries = ["CMakeLists.txt", "VERSION"]
    entries += ["build.bat", "tools/dev/incremental_windows.py"] if WINDOWS else ["build.sh"]
    for name in entries:
        path = project / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, path)
    for directory in ("core", "image", "render", "texture", "editor", "research", "deps"):
        put(project / f"src/{directory}/fixture.cpp")
    for name in (
        "src/core/fixture.h", "src/scene/source_content.cpp",
        "src/platform/platform_win32.cpp", "src/platform/platform_cocoa.mm",
        "src/platform/platform_x11.cpp", "src/app/main.cpp", "tests/test_main.cpp",
        "stress/stress_main.cpp", "extern/ufbx/ufbx.c", "extern/sky/source/sky_hosek.cpp",
        "extern/sky/source/sky_multiple_scattering.cpp", "extern/libc_compat/libc_compat.c",
        "licenses/notice.txt",
    ):
        put(project / name)


def make_bundle(bundle, case, omitted):
    if case == "no-bundle":
        return
    for name in ("tbb/include/fixture.h", TBB, VULKAN_HEADER, SHADERC_HEADER, SHADERC):
        path = bundle / name
        # In partial bundles the package directories still exist.
        if case != "cpu-only":
            path.parent.mkdir(parents=True, exist_ok=True)
        if name not in omitted:
            put(path)
    if case == "missing-shaderc-link":
        put(bundle / (SHADERC_UNIX if WINDOWS else SHADERC_WIN))


def batch_plan(project, env):
    plan = project / "batch-plan"
    plan.mkdir()
    # The resolver only runs cl to log its version before recording the plan.
    put(project / "cl.cmd", "@echo compiler version fixture\n")
    run([env["COMSPEC"], "/d", "/c", "chcp 65001 >nul && build.bat release app"], project,
        dict(env, _BLENDITY_INCREMENTAL_PLAN=str(plan)), project / "batch.log")
    args = json.loads((plan / "resolved.json").read_bytes())
    return [arg[2:] for arg in args["compile"] if arg.startswith("/D")], args["link"]


def shell_plan(project, env):
    # Record one engine translation unit's compiler flags and the tests linker invocation.
    compiler = project / "compiler"
    triple = "arm64-apple-darwin" if MACOS else "x86_64-linux-gnu"
    put(compiler, f'''#!/bin/sh
case " $* " in
  *" -dumpmachine "*) echo {triple} ;;
  *" -c src/core/fixture.cpp "*) printf '%s\\n' "$@" > compile-args ;;
  *" -o dist/"*"/blendity_tests "*) printf '%s\\n' "$@" > link-args ;;
esac
''')
    compiler.chmod(0o755)
    run(["bash", "build.sh", "release", "tests"], project,
        dict(env, CC="./compiler", CXX="./compiler", BLENDITY_LIBDIR="bundle"),
        project / "shell.log")
    flags = (project / "compile-args").read_text().splitlines()
    return [arg[2:] for arg in flags if arg.startswith("-D")], (project / "link-args").read_text().splitlines()


def cmake_plan(project, env):
    output = project / "cmake"
    put(output / ".cmake/api/v1/query/codemodel-v2")
    modules = project / "modules"
    # Unrelated system dependencies, so the fixture needs no target SDK.
    put(modules / "FindThreads.cmake", "add_library(Threads::Threads INTERFACE IMPORTED)\n")
    put(modules / "FindX11.cmake")
    system, processor, compiler_id = ("Windows", "AMD64", "MSVC") if WINDOWS else (
        ("Darwin", "arm64", "AppleClang") if MACOS else ("Linux", "x86_64", "GNU"))
    compiler = Path(sys.executable).as_posix() if WINDOWS else "/usr/bin/true"
    toolchain = project / "toolchain.cmake"
    put(toolchain, f'''set(CMAKE_SYSTEM_NAME {system})
set(CMAKE_SYSTEM_PROCESSOR {processor})
set(CMAKE_SIZEOF_VOID_P 8)
foreach(lang C CXX OBJCXX)
  set(CMAKE_${{lang}}_COMPILER "{compiler}")
  set(CMAKE_${{lang}}_COMPILER_ID_RUN TRUE)
  set(CMAKE_${{lang}}_COMPILER_ID {compiler_id})
  set(CMAKE_${{lang}}_COMPILER_FORCED TRUE)
endforeach()
''')
    if WINDOWS:
        with toolchain.open("a", encoding="utf-8") as stream:
            stream.write("set(CMAKE_C_COMPILER_VERSION 19.51)\nset(CMAKE_CXX_COMPILER_VERSION 19.51)\n")
    generator = "Ninja" if WINDOWS else "Unix Makefiles"
    run(["cmake", "-S", ".", "-B", str(output), "-G", generator,
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_MODULE_PATH={modules}",
         f"-DBLENDITY_LIBDIR={env['BLENDITY_LIBDIR']}",
         f"-DBLENDITY_NO_LIBS={'ON' if env.get('BLENDITY_NO_LIBS') else 'OFF'}"],
        project, env, project / "cmake.log")
    reply = output / ".cmake/api/v1/reply"
    model = json.loads(next(reply.glob("codemodel-v2-*.json")).read_bytes())
    targets = {target["name"]: target["jsonFile"] for target in model["configurations"][0]["targets"]}
    engine = json.loads((reply / targets["blendity_engine"]).read_bytes())
    app = json.loads((reply / targets["Blendity"]).read_bytes())
    defines = [item["define"] for group in engine["compileGroups"] if group["language"] == "CXX"
               for item in group.get("defines", [])]
    links = [item["fragment"] for item in app["link"]["commandFragments"] if item["role"] == "libraries"]
    return defines, links


def linked(links, library):
    pattern = re.compile(rf'(?:^|[/\s"]){re.escape(library)}(?:$|[\s"])')
    return any(pattern.search(arg.replace("\\", "/")) for arg in links)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("--source", type=Path, default=ROOT)
    args = parser.parse_args()
    work = args.work.resolve()
    source = args.source.resolve()
    work.mkdir(parents=True)
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(("BLENDITY_", "_BLENDITY_"))
           and key not in ("EXTRA_FLAGS", "CFLAGS", "CXXFLAGS", "OBJCXXFLAGS", "LDFLAGS")}
    env.update(BLENDITY_NO_OPTIX="1", BLENDITY_NO_WARM="1", BLENDITY_ALLOCATOR="system")
    failed = 0
    for case, omitted, cpu, gpu in CASES:
        project = work / case
        bundle = project / "bundle"
        make_project(project, source)
        make_bundle(bundle, case, omitted)
        case_env = dict(env, BLENDITY_LIBDIR=str(bundle))
        if case == "forced-free":
            case_env["BLENDITY_NO_LIBS"] = "1"
        for plan in (batch_plan if WINDOWS else shell_plan, cmake_plan):
            name = f"{plan.__name__}/{case}"
            try:
                defines, links = plan(project, case_env)
                checks = {
                    "TBB define": ("BL_WITH_TBB" in defines, cpu),
                    # CMake can emit -ltbb for a shared library without a SONAME.
                    "TBB link": (linked(links, TBB) or "-ltbb" in links, cpu),
                    "Vulkan define": ("BL_WITH_VULKAN" in defines, gpu),
                    "shaderc link": (linked(links, SHADERC), gpu),
                    "other platform's shaderc link": (linked(links, SHADERC_UNIX if WINDOWS else SHADERC_WIN), False),
                }
                errors = [f"{label} was {actual}, expected {expected}"
                          for label, (actual, expected) in checks.items() if actual != expected]
                if errors:
                    raise ValueError(", ".join(errors))
                print(f"PASS {name}", flush=True)
            except (OSError, RuntimeError, ValueError, KeyError, StopIteration) as error:
                failed += 1
                print(f"FAIL {name}: {error}", flush=True)
    total = 2 * len(CASES)
    print(f"{total - failed}/{total} build plans passed; artifacts in {work}")
    return int(bool(failed))


if __name__ == "__main__":
    sys.exit(main())
