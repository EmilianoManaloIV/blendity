# SPDX-License-Identifier: GPL-2.0-or-later
r"""Run against real MSVC from a below-normal x64 developer prompt holding a build slot.

    py -3 tests/test_incremental_windows.py C:\blab\new-fixture --stock C:\before\build.bat

The work directory must not exist and must be on NTFS. Native logs are retained
there. The optional stock entry point supplies the clean reference when testing
an uncommitted candidate.
"""

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
EXES = {"app": "Blendity.exe", "tests": "blendity_tests.exe"}


def put(root, path, contents):
    destination = root / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(contents, encoding="utf-8")


def digest(path):
    return sha256(path.read_bytes()).hexdigest()


def check(actual, expected):
    if actual != expected:
        raise AssertionError(f"Expected {expected!r}, got {actual!r}")


def source(program, offset):
    return f"""#include <cstdio>
#include "api.h"
int main() {{
    std::printf("{program} %d %zu %d %d %d %d\\n", payload(), sizeof(Payload),
                bias(), optional(), linked_extra, {offset} + library_value());
}}
"""


def create_fixture(tree, stock):
    shutil.copy2(ROOT / "build.bat", tree / "build.bat")
    shutil.copy2(stock, tree / "clean.bat")
    worker = tree / "tools/dev/incremental_windows.py"
    worker.parent.mkdir(parents=True)
    shutil.copy2(ROOT / "tools/dev/incremental_windows.py", worker)
    for name in ("extern/ufbx/ufbx.c", "extern/sky/source/sky_hosek.cpp",
                 "extern/sky/source/sky_multiple_scattering.cpp"):
        put(tree, name, "")
    put(tree, "VERSION", "1.0.0\n")
    put(tree, "licenses/notice.txt", "fixture notice\n")
    put(tree, "stress/stress_main.cpp", "int main() {}\n")
    put(tree, "extern/sky/include/layout.h", "struct Payload { unsigned char x; };\n")
    put(tree, "extern/sky/include/api.h", """#include "layout.h"
int payload();
int bias();
int optional();
int library_value();
extern int linked_extra;
""")
    put(tree, "src/core/payload.cpp", '#include <api.h>\nint payload() { return sizeof(Payload); }\n')
    put(tree, "src/core/bias.cpp", """#include <cstdio>
#include <shadow.h>
#ifndef FIXTURE_BIAS
#define FIXTURE_BIAS 0
#endif
int linked_extra = 0;
int bias() { return SHADOW_VALUE + FIXTURE_BIAS; }
""")
    put(tree, "src/core/library.cpp", """#ifdef BL_WITH_TBB
int from_archive();
int library_value() { return from_archive(); }
#else
int library_value() { return 0; }
#endif
""")
    put(tree, "src/core/optional.cpp", """#if __has_include(<optional_fixture.h>)
#include <optional_fixture.h>
#else
#define OPTIONAL_VALUE 0
#endif
int optional() { return OPTIONAL_VALUE; }
""")
    put(tree, "src/app/main.cpp", source("app", 0))
    put(tree, "tests/test_main.cpp", source("tests", 0))
    put(tree, "include-second/shadow.h", "#define SHADOW_VALUE 1\n")
    (tree / "include-first").mkdir()


class Fixture:
    def __init__(self, args):
        self.work = args.work.resolve()
        self.work.mkdir()
        self.tree = self.work / "fixture"
        self.tree.mkdir()
        create_fixture(self.tree, (args.stock or ROOT / "build.bat").resolve())
        self.cache = self.work / "cache"
        self.manifest = self.cache / "current.json"
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.upper().startswith(("BLENDITY_", "_BLENDITY_"))
                            and key.upper() not in ("CL", "_CL_", "LINK", "_LINK_")}
        self.environment.update(
            BLENDITY_NO_LIBS="1", BLENDITY_NO_OPTIX="1", BLENDITY_NO_WARM="1",
            BLENDITY_VCVARS_VER="14.51", BLENDITY_VCVARS_STRICT="1",
            BLENDITY_PROJECT=str(self.work / "project"),
            BLENDITY_PREFS_DIR=str(self.work / "prefs"),
            BLENDITY_TRASH=str(self.work / "trash"),
            CL="/Iinclude-first /Iinclude-second",
        )
        self.failures = []

    def launch(self, name, command, environment, cwd=None):
        directory = self.tree if cwd is None else cwd
        print(f"{name}: {subprocess.list2cmdline(command)} from {directory}", flush=True)
        log = self.work / (name + ".log")
        with log.open("wb") as stream:
            process = subprocess.Popen(command, cwd=directory, env=environment,
                                       stdout=stream, stderr=subprocess.STDOUT)
        return process, log

    @staticmethod
    def finish(request, success=True):
        process, log = request
        code = process.wait(timeout=180)
        text = log.read_text(encoding="utf-8", errors="replace")
        if (code == 0) != success:
            raise AssertionError(f"{log}: exit {code}\n{text}")
        return text

    def clean(self, name):
        self.finish(self.launch(name, ["cmd", "/d", "/c", "clean.bat", "release", "all"],
                                self.environment))
        return self.tree / "dist/windows"

    def incremental(self, name, target="app-tests", *, cwd=None, **overrides):
        environment = dict(self.environment, BLENDITY_INCREMENTAL="1",
                           BLENDITY_INCREMENTAL_DIR=str(self.cache), **overrides)
        command = ["cmd", "/d", "/c", "call", str(self.tree / "build.bat"), "release", target]
        return self.launch(name, command, environment, cwd)

    @staticmethod
    def counts(log):
        return (int(re.findall(r"Compiling (\d+) of \d+ objects", log)[-1]),
                int(re.findall(r"Linked (\d+) targets", log)[-1]))

    def build(self, name, expected=None, target="app-tests", **overrides):
        log = self.finish(self.incremental(name, target, **overrides))
        if expected is not None:
            check(self.counts(log), expected)
        return self.package()

    def package(self):
        return self.cache / json.loads(self.manifest.read_bytes())["package"]

    def expect(self, package, values, target="app"):
        result = subprocess.run([package / EXES[target]], cwd=self.tree, env=self.environment,
                                capture_output=True, text=True, check=True)
        log = f"{result.stdout.strip()} from {package / EXES[target]}"
        print(log, flush=True)
        check(result.stdout.strip(), f"{target} {values}")

    def matches_stock_objects(self):
        state = json.loads(self.manifest.read_bytes())
        config, = state["configurations"].values()
        for target in EXES:
            rsp = self.tree / "build/obj_release" / (target + "_objects.rsp")
            clean = rsp.read_text().splitlines()
            ordered = [Path(source).stem + ".obj" for source in config["links"][target]["sources"]]
            check(ordered, [Path(path).name for path in clean])

    def test(self, name, action):
        try:
            action()
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            self.failures.append(name)
            print(f"FAIL {name}: {error}", flush=True)
        else:
            print(f"PASS {name}", flush=True)

    def cold_and_warm(self):
        clean = self.clean("00-stock")
        package = self.build("01-empty-from-other-directory", (9, 2), cwd=self.work)
        self.matches_stock_objects()
        for target in EXES:
            self.expect(clean, "1 1 1 0 0 0", target)
            self.expect(package, "1 1 1 0 0 0", target)
        check(self.build("02-warm", (0, 0)), package)
        put(self.tree, "README.md", "documentation only\n")
        check(self.build("03-docs", (0, 0)), package)
        (package / "licenses/notice.txt").unlink()
        repaired = self.build("04-repair", (0, 0))
        check(digest(repaired / "licenses/notice.txt"), digest(self.tree / "licenses/notice.txt"))
        for executable in EXES.values():
            check(digest(repaired / executable), digest(package / executable))

    def target_and_abi(self):
        put(self.tree, "src/core/payload.cpp", '#include <api.h>\nint payload() { return sizeof(Payload) + 1; }\n')
        app = self.build("10-shared-app", (1, 1), "app")
        tests = self.build("11-shared-tests", (0, 1), "tests")
        self.expect(app, "2 1 1 0 0 0")
        self.expect(tests, "2 1 1 0 0 0", "tests")
        put(self.tree, "src/app/main.cpp", source("app", 5))
        self.build("12-app-edit-tests-stay-current", (0, 0), "tests")
        app = self.build("12-app-only", (1, 1), "app")
        put(self.tree, "tests/test_main.cpp", source("tests", 6))
        self.build("13-tests-edit-app-stays-current", (0, 0), "app")
        tests = self.build("13-tests-only", (1, 1), "tests")
        self.expect(app, "2 1 1 0 0 5")
        self.expect(tests, "2 1 1 0 0 6", "tests")
        put(self.tree, "extern/sky/include/layout.h", "struct Payload { unsigned long long x[2]; };\n")
        package = self.build("14-transitive-abi", (3, 2))
        clean = self.clean("15-stock-abi")
        for target, offset in (("app", 5), ("tests", 6)):
            values = f"17 16 1 0 0 {offset}"
            self.expect(package, values, target)
            self.expect(clean, values, target)

    def include_resolution(self):
        put(self.tree, "include-first/shadow.h", "#define SHADOW_VALUE 3\n")
        self.expect(self.build("20-shadow-add"), "17 16 3 0 0 5")
        put(self.tree, "include-first/shadow.h", "#define SHADOW_VALUE 4\n")
        self.expect(self.build("21-shadow-edit", (1, 2)), "17 16 4 0 0 5")
        (self.tree / "include-first/shadow.h").unlink()
        self.expect(self.build("22-shadow-remove"), "17 16 1 0 0 5")
        put(self.tree, "include-second/optional_fixture.h", "#define OPTIONAL_VALUE 2\n")
        self.expect(self.build("23-probe-fallback"), "17 16 1 2 0 5")
        put(self.tree, "include-first/optional_fixture.h", "#define OPTIONAL_VALUE 4\n")
        self.expect(self.build("24-probe-shadow"), "17 16 1 4 0 5")
        (self.tree / "include-first/optional_fixture.h").unlink()
        (self.tree / "include-second/optional_fixture.h").unlink()
        self.expect(self.build("25-probe-remove"), "17 16 1 0 0 5")

    def junction_include(self):
        junction = self.tree / "include-first/redirected"
        first = self.work / "first-junction-target"
        second = self.work / "second-junction-target"
        first.mkdir()
        put(second, "optional_fixture.h", "#define OPTIONAL_VALUE 9\n")
        unit = self.tree / "src/core/optional.cpp"
        original = unit.read_bytes()

        def point_junction_at(name, target):
            self.finish(self.launch(name, ["cmd", "/d", "/c", "mklink", "/J",
                                           str(junction), str(target)], self.environment))

        try:
            unit.write_bytes(original.replace(b"<optional_fixture.h>",
                                              b"<redirected/optional_fixture.h>"))
            point_junction_at("26-create-junction", first)
            self.expect(self.build("26-empty-junction"), "17 16 1 0 0 5")
            put(first, "optional_fixture.h", "#define OPTIONAL_VALUE 7\n")
            self.expect(self.build("26-junction-gains-header", (1, 2)), "17 16 1 7 0 5")
            junction.rmdir()
            point_junction_at("26-retarget-junction", second)
            self.expect(self.build("26-junction-changes-target"), "17 16 1 9 0 5")
        finally:
            unit.write_bytes(original)
            if junction.exists():
                junction.rmdir()

    def escaping_macro_probe(self):
        # A negative probe has no resolved file in MSVC's dependency report.
        unit = self.tree / "src/core/optional.cpp"
        original = unit.read_bytes()
        external = self.work / "outside/optional.h"
        paste_definitions = """#define PASTE_RAW(left,right) left##right
#define PASTE(left,right) PASTE_RAW(left,right)
#define OP_FIRST PASTE(__,has)
#define OP_LAST PASTE(_,include)
"""
        try:
            for name, definitions, operator in (
                ("direct", "", "__has_include"),
                ("pasted", paste_definitions, "PASTE(OP_FIRST,OP_LAST)"),
            ):
                put(self.tree, "src/core/optional.cpp", definitions + f"""#define EXTERNAL_HEADER "../../../outside/optional.h"
#if {operator}(EXTERNAL_HEADER)
#include EXTERNAL_HEADER
#else
#define EXTERNAL_VALUE 0
#endif
int optional() {{ return EXTERNAL_VALUE; }}
""")
                self.expect(self.build(f"27-{name}-probe-absent"), "17 16 1 0 0 5")
                put(self.work, "outside/optional.h", "#define EXTERNAL_VALUE 8\n")
                package = self.build(f"28-{name}-probe-outside", (1, 2))
                self.expect(self.clean(f"28-stock-{name}-probe"), "17 16 1 8 0 5")
                self.expect(package, "17 16 1 8 0 5")
                warm = self.finish(self.incremental(f"29-{name}-probe-warm"))
                check(self.counts(warm), (0, 0))
                if name == "pasted":
                    check("Preprocessed 1 TUs for include validation" in warm, True)
                external.unlink()
        finally:
            unit.write_bytes(original)
            external.unlink(missing_ok=True)

    def source_list_and_identity(self):
        put(self.tree, "src/core/added.cpp", """extern int linked_extra;
int set_extra() { return linked_extra = 12; }
int registration = set_extra();
""")
        self.expect(self.build("30-add-source"), "17 16 1 0 12 5")
        (self.tree / "src/core/added.cpp").unlink()
        self.expect(self.build("31-remove-source"), "17 16 1 0 0 5")
        self.environment["CL"] += " /DFIXTURE_BIAS=9"
        self.expect(self.build("32-compile-options", (9, 2)), "17 16 10 0 0 5")
        self.expect(self.build("33-build-identity", (9, 2), BLENDITY_BUILD="fixture"),
                    "17 16 10 0 0 5")
        previous = self.manifest.read_bytes()
        self.manifest.write_bytes(b"invalid json")
        try:
            log = self.finish(self.incremental("34-mismatched-toolset", BLENDITY_VCVARS_VER="14.50"),
                              success=False)
            check("Requested MSVC 14.50, but the active toolset is 14.51" in log, True)
            check(self.manifest.read_bytes(), b"invalid json")
        finally:
            self.manifest.write_bytes(previous)

    def library(self):
        libs = self.work / "libs"
        (libs / "tbb/include").mkdir(parents=True)
        (libs / "tbb/lib").mkdir()
        archive = libs / "tbb/lib/tbb12.lib"
        for value, counts in ((7, (9, 2)), (9, (0, 2))):
            put(self.tree, "archive.cpp", f"int from_archive() {{ return {value}; }}\n")
            self.finish(self.launch(f"40-archive-cl-{value}",
                                    ["cl", "/nologo", "/c", "/O2", "/MD", "archive.cpp"],
                                    self.environment))
            self.finish(self.launch(f"41-archive-lib-{value}",
                                    ["lib", "/nologo", f"/out:{archive}", "archive.obj"],
                                    self.environment))
            self.environment.update(BLENDITY_NO_LIBS="0", BLENDITY_LIBDIR=str(libs),
                                    BLENDITY_ALLOCATOR="system")
            self.expect(self.build(f"42-library-{value}", counts), f"17 16 10 0 0 {value + 5}")
        self.expect(self.build("43-library-warm", (0, 0)), "17 16 10 0 0 14")

    def failures_and_concurrency(self):
        package = self.package()
        old = {target: digest(package / exe) for target, exe in EXES.items()}
        active = self.manifest.read_bytes()

        def check_last_good():
            check(self.manifest.read_bytes(), active)
            for target, total in (("app", 14), ("tests", 15)):
                check(digest(package / EXES[target]), old[target])
                self.expect(package, f"17 16 10 0 0 {total}", target)

        put(self.tree, "src/core/bias.cpp", "#error fixture compile failure\n")
        log = self.finish(self.incremental("50-compile-fails"), success=False)
        check("fixture compile failure" in log, True)
        check_last_good()
        put(self.tree, "src/core/bias.cpp", "int bias() { return 25; }\nint linked_extra = 0;\n")
        put(self.tree, "tests/test_main.cpp", "int missing(); int main() { return missing(); }\n")
        log = self.finish(self.incremental("51-second-target-link-fails"), success=False)
        check("Linking Blendity.exe" in log, True)
        check("Link failed for blendity_tests.exe" in log, True)
        check("unresolved external symbol" in log and "missing" in log, True)
        check_last_good()

        put(self.tree, "tests/test_main.cpp", source("tests", 6))
        first = self.incremental("52-retry-first")
        second = self.incremental("53-retry-second")
        check(sorted(self.counts(self.finish(request)) for request in (first, second)), [(0, 0), (1, 2)])
        for target, total in (("app", 14), ("tests", 15)):
            self.expect(self.package(), f"17 16 25 0 0 {total}", target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("--stock", type=Path)
    args = parser.parse_args()
    if os.name != "nt" or not shutil.which("cl") or not shutil.which("lib"):
        parser.error("use a Windows x64 MSVC developer prompt")
    fixture = Fixture(args)
    tests = (fixture.cold_and_warm, fixture.target_and_abi, fixture.include_resolution,
             fixture.junction_include, fixture.escaping_macro_probe,
             fixture.source_list_and_identity, fixture.library, fixture.failures_and_concurrency)
    for test in tests:
        fixture.test(test.__name__, test)
    print(f"{len(tests) - len(fixture.failures)}/{len(tests)} scenarios passed", flush=True)
    return int(bool(fixture.failures))


if __name__ == "__main__":
    raise SystemExit(main())
