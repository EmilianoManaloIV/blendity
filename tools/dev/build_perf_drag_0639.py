#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Link the 0639 drag probe against a verified incremental Windows release app bank."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
ENTRY = "src/app/main.cpp"
PROBE = "tools/perf_drag_0639.cpp"
RASTER = "src/render/raster.cpp"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def resolve_inputs(state, bank, stock, source_tree, renderer, link_config, app_flags):
    from incremental_windows import identity, path_key

    matches = [c for c in state["configurations"].values()
               if c["links"].get("app", {}).get("outputs", {}).get("Blendity.exe") == stock]
    if len(matches) != 1:
        raise ValueError("The stock app hash does not identify one incremental app configuration")
    config, = matches
    donor = Path(state["worktree"])
    app = config["links"]["app"]
    records = config["objects"]
    names = [path_key(donor / source) for source in app["sources"]]
    # A tests-only build may refresh a shared object while preserving the previous app link.
    # Match the owning build worker's ordered link key before using any current records.
    if any(name not in records for name in names):
        raise ValueError("The current bank no longer contains all objects linked into the stock app")
    ordered = [(name, records[name]["inputs"], records[name].get("preprocessed"), records[name]["digest"])
               for name in names]
    object_key = identity([link_config, app_flags, ordered])
    if object_key != app["objects"]:
        raise ValueError("Current bank objects or build flags differ from the recorded stock app link")
    linked = bank / app["path"] / "Blendity.exe"
    if digest(linked) != stock:
        raise ValueError("The recorded linked executable differs from the verified stock package")

    inputs = {}
    for name in app["sources"]:
        source = name.replace("\\", "/")
        if source in inputs:
            raise ValueError(f"Duplicate app source {source}")
        if source == ENTRY or (source == RASTER and renderer):
            path = ROOT / PROBE if source == ENTRY else renderer
            inputs[source] = {"source": str(path), "source_sha256": digest(path)}
            continue
        path = source_tree / source
        source_sha = digest(path)
        if digest(donor / source) != source_sha:
            raise ValueError(f"Working source differs from bank source: {source}")
        record = records[path_key(donor / source)]
        obj = bank / record["path"]
        if digest(obj) != record["digest"]:
            raise ValueError(f"Bank object differs from recorded digest: {obj}")
        inputs[source] = {"source": str(path), "source_sha256": source_sha,
                          "object": str(obj), "object_sha256": record["digest"]}
    if ENTRY not in inputs or (renderer and RASTER not in inputs):
        raise ValueError("The selected bank does not contain the app entry or requested override")
    return inputs, object_key, app["path"]


def log_flags(log, compiler, linker):
    from incremental_windows import Files, LINK_ENV, argv, identity, inspect_toolchain

    text = log.read_text(encoding="utf-8-sig", errors="replace")
    recorded = re.search(r"Tool fingerprints: cl=([0-9a-f]{64}) link=([0-9a-f]{64})", text)
    if not recorded or recorded.groups() != (digest(compiler), digest(linker)):
        raise ValueError("The current compiler and linker do not match the stock build log")

    def flags(label):
        prefix = f"[blendity] {label} flags: "
        return argv(next(line[len(prefix):] for line in text.splitlines() if line.startswith(prefix)))

    compile_flags = flags("Compile")
    link_flags = flags("Link")
    app_flags = flags("app link")
    if "/O2" not in compile_flags or "/DNDEBUG" not in compile_flags or link_flags[:1] != ["/link"]:
        raise ValueError("The build log must contain the stock release compile and link flags")
    values = dict(line[11:].split("=", 1) for line in text.splitlines()
                  if line.startswith("[blendity] ") and line[11:].split("=", 1)[0] in LINK_ENV)
    if any(values.get(name) != os.environ.get(name, "") for name in LINK_ENV):
        raise ValueError("The active Visual Studio linker environment differs from the stock build log")
    version = os.environ["VCToolsVersion"]
    resolved = {"environment": {"VCToolsVersion": version, "VCToolsInstallDir": os.environ["VCToolsInstallDir"]},
                "tools": {"cl": str(compiler), "link": str(linker)}}
    _, _, compiler_identity, linker_identity = inspect_toolchain(resolved, version.rstrip("/\\"), Files())
    link_config = identity([compile_flags, link_flags, compiler_identity, linker_identity,
                            {name: values[name] for name in LINK_ENV}])
    return compile_flags, link_flags, app_flags, link_config


def link(output, source_tree, compiler, compile_flags, link_flags, inputs):
    from incremental_windows import response_file

    for source, row in inputs.items():
        if "object" in row:
            continue
        path = Path(row["source"])
        obj = output / (path.stem + ".obj")
        response = output / (path.stem + "-compile.rsp")
        include_dir = source_tree / ("src/render" if source == RASTER else "tools")
        compile_source = output / path.name
        shutil.copyfile(path, compile_source)
        if digest(compile_source) != row["source_sha256"]:
            raise ValueError(f"Source changed before compilation: {path}")
        response_file(response, ["/c", *compile_flags, "/I" + str(include_dir), str(compile_source), "/Fo" + str(obj)])
        subprocess.run([str(compiler), "@" + str(response)], cwd=source_tree, check=True)
        if digest(compile_source) != row["source_sha256"]:
            raise ValueError(f"Source changed during compilation: {path}")
        row["compile_source"] = str(compile_source)
        row["object"] = str(obj)
        row["object_sha256"] = digest(obj)

    executable = output / "perf_drag_0639.exe"
    response = output / "link.rsp"
    response_file(response, ["/nologo", "/Fe:" + str(executable), *[row["object"] for row in inputs.values()]],
                  [*link_flags, "/SUBSYSTEM:CONSOLE"])
    subprocess.run([str(compiler), "@" + str(response)], cwd=source_tree, check=True)
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bank", type=Path, help="incremental build directory containing current.json")
    parser.add_argument("stock_app", type=Path, help="verified Blendity.exe from the bank")
    parser.add_argument("build_log", type=Path, help="the stock release app build log")
    parser.add_argument("output", type=Path, help="new output directory")
    parser.add_argument("--source-tree", type=Path, default=ROOT, help="checkout matching the stock bank")
    parser.add_argument("--renderer-source", type=Path, help="optional exact raster.cpp candidate")
    parser.add_argument("--renderer-sha256", help="expected digest of the candidate")
    parser.add_argument("--verify-inputs", action="store_true", help="validate inputs and exit before compiling")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This helper consumes a Windows MSVC object bank")
    compiler, linker = shutil.which("cl.exe"), shutil.which("link.exe")
    if not compiler or not linker or any(os.environ.get(name) for name in ("CL", "_CL_", "LINK", "_LINK_")):
        parser.error("Use the stock x64 developer environment with empty CL and LINK overrides")
    if bool(args.renderer_source) != bool(args.renderer_sha256):
        parser.error("Specify both the renderer's source path and its SHA-256")
    renderer = args.renderer_source.resolve(strict=True) if args.renderer_source else None
    if renderer and digest(renderer) != args.renderer_sha256:
        parser.error("Renderer file differs from its requested SHA-256")
    bank = args.bank.resolve()
    stock = digest(args.stock_app)
    manifest = (bank / "current.json").read_bytes()
    state = json.loads(manifest.decode("utf-8-sig"))
    source_tree = args.source_tree.resolve()
    compile_flags, link_flags, app_flags, link_config = log_flags(args.build_log, Path(compiler), Path(linker))
    inputs, object_key, link_path = resolve_inputs(state, bank, stock, source_tree, renderer, link_config, app_flags)
    print(f"Verified {len(inputs)} inputs for {stock} from {link_path} ({object_key})", flush=True)
    if args.verify_inputs:
        return
    output = args.output.resolve()
    output.mkdir(parents=True)
    executable = link(output, source_tree, compiler, compile_flags, link_flags, inputs)
    record = {"stock_exe": stock, "stock_app_link": link_path, "stock_app_object_key": object_key,
              "bank_manifest_sha256": hashlib.sha256(manifest).hexdigest(), "executable": digest(executable),
              "records": inputs}
    (output / "inputs.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(executable, digest(executable))


if __name__ == "__main__":
    main()
