#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Derive the two identity-probe VPKs from Blendity's original Workshop fixtures."""

import argparse
import importlib.util
import json
from hashlib import sha256
from pathlib import Path
import sys


MAP = "maps/workshop_fixture"
PACKAGE = "WorkshopIdentity.vpk"
FLOOR = "floor/enamel"
PROP = "prop/enamel"
V1_PALETTE = ((40, 80, 120, 255), (84, 124, 164, 255))
V2_FLOOR = ((16, 102, 154, 255), (25, 117, 168, 255))
V2_PROP = ((160, 66, 27, 255), (179, 87, 43, 255))


def load_generator(path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("original_workshop_fixture", path)
    original = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original)
    return original


def revision(original, order, floor_palette, prop_palette):
    placements = {
        "floor": original.placement(f"{MAP}/floor.vmesh", "m_renderable"),
        "left": original.placement("models/workshop_fixture/plinth.vmdl", "m_renderableModel", -45, 0, 15),
        "right": original.placement("models/workshop_fixture/plinth.vmdl", "m_renderableModel", 42, 0, 25, 41),
    }
    world = original.modern_world()
    world[f"{MAP}/worldnode.vwnod_c"] = original.resource({
        "DATA": original.kv3({"m_sceneObjects": [placements[name] for name in order]}),
    })
    materials = original.modern_material(FLOOR, floor_palette) | original.modern_material(PROP, prop_palette)
    return world | materials


def material_description(name, roughness):
    stem = f"materials/workshop_fixture/{name}"
    return {"material": f"{stem}.vmat", "texture": f"{stem}.vtex", "saved_roughness": roughness}


def write_inputs(original, output):
    versions = {
        "v1": (("floor", "left", "right"), V1_PALETTE, V1_PALETTE),
        "v2": (("right", "left", "floor"), V2_FLOOR, V2_PROP),
    }
    cases = {}
    for version, (order, floor_palette, prop_palette) in versions.items():
        resources = revision(original, order, floor_palette, prop_palette)
        relative = f"{version}/{PACKAGE}"
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        archive = original.vpk(resources)
        path.write_bytes(archive)
        cases[version] = {
            "archive": relative,
            "sha256": sha256(archive).hexdigest(),
            "placement_order": order,
            "floor_rgba": floor_palette,
            "prop_rgba": prop_palette,
        }
    contract = {
        "map": f"{MAP}.vmap_c",
        "mesh_slots": 3,
        "project_materials": 2,
        "project_base_textures": 2,
        "pixel_coordinates": [[0, 0], [2, 0]],
        "floor": material_description(FLOOR, 0.37),
        "prop": material_description(PROP, 0.73),
        "versions": cases,
    }
    (output / "expected.json").write_bytes((json.dumps(contract, indent=2) + "\n").encode("utf-8"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generator", type=Path, help="path to tests/fixtures/source_workshop/generate.py")
    parser.add_argument("output", type=Path, help="scratch directory for the two archives and expected.json")
    args = parser.parse_args()
    write_inputs(load_generator(args.generator), args.output)
