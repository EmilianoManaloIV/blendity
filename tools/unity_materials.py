#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unity's materials beside Blendity's (round 484, task 0460): what Unity's shader reads from each .mat of an unpacked
package, against what Blendity made of the same file.

    py tools/unity_materials.py table <Blendity.exe> <Blendity project> <scene to open> <out dir> [--cmd "<console line>"]...
    py tools/unity_materials.py state <Blendity project> nomaps
    py tools/unity_materials.py state <Blendity project> byhand <materials.json>
    py tools/unity_materials.py state <Blendity project> png8
    py tools/unity_materials.py --self-test

Standard library only. `table` reads every Unity .mat the project's unpacked packages hold on its own - the shader
through the package's GUID table (.unity-guids), the keywords, the floats and the texture slots - and works out the
metallic and the roughness Unity's shader shades with, as means over the maps it reads (the rule is the shader's own
input code: Unity's Standard and Silent's Filamented take metal from _MetallicGlossMap's red and smoothness from its
alpha times _GlossMapScale under the _METALLICGLOSSMAP keyword, the sliders without it; URP Lit the same with _Smoothness).
Then it runs Blendity headless on the project (which must be a scratch copy: Blendity writes a trash folder and the
listing; `--cmd` lines run after the scene opens), asks it for `unity materials all` and for `unity mapstats` of the maps,
and writes into <out dir>:
  materials.md / materials.json   per material: Unity's metallic and roughness with where each comes from, Blendity's
                                  (its value times its map's mean red), the maps Unity uses that Blendity has not
                                  attached, and what is wrong - see FAULTS below;
  blendity_materials.txt          Blendity's listing as it wrote it;  mapstats_N.txt  the maps' channel means;
  decoders_N.txt                  `unity mapstats decoders` of the PNGs a library may read its own way (16 bits a
                                  sample, or a gamma of their own): the build's reading beside Blendity's own decoder's.
`state` rewrites files of a scratch project (one with an empty BLENDITY_SCRATCH file beside its Assets folder - refused
otherwise) into a test state, so the same scene can be measured through the importer as it is:
  nomaps   every texture a .mat names gets a GUID no file has: the materials as a session reads them before the
           package's GUID table exists;
  byhand   each material whose metallic or roughness is off gets Unity's mean values on its sliders and its
           metallic-gloss slot emptied: what the scene looks like with the values right (one value per material, no map);
  png8     every 16-bit PNG becomes an 8-bit one of its high bytes: what the scene looks like when a build with libpng
           reads those images as Unity does.
`--self-test` checks the .mat reader, Unity's rule, the fault classes and the states on the committed materials in
tests/fixtures/unity_gloss (no Blendity needed).

FAULTS (a material can have several):
  not read                 Blendity's listing has no material for the file
  maps missing             a texture Unity uses is in the project, and Blendity's slot for it is empty or its file gone
  slot without keyword     _MetallicGlossMap holds a texture but the keyword is off - Unity uses the sliders - and
                           Blendity attached the texture or its values are off by more than TOLERANCE
  smoothness dropped       Unity's smoothness comes from a map's alpha; Blendity's roughness is off by more than TOLERANCE
  values differ            metallic or roughness off by more than TOLERANCE for another reason
  decoders differ          an image it names reads differently in this build than in Blendity's own decoder (by more
                           than one 8-bit step in a colour channel's mean): a 16-bit PNG read as linear light
  reflections off ignored  _GLOSSYREFLECTIONS_OFF: Unity draws no environment reflection on it
  highlights off ignored   _SPECULARHIGHLIGHTS_OFF: Unity draws no light highlight on it
  occlusion ignored        an occlusion map darkens Unity's ambient light and reflections (its mean under 0.98)
  not modelled             a shader this tool has no rule for: Unity's values unknown
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
FIXTURES = os.path.join(REPO, "tests", "fixtures", "unity_gloss")
TOLERANCE = 0.05
BUILTIN = "0000000000000000f000000000000000"
URP_LIT = "933532a4fcc9baf4fa0491de14d08ed7"
UNKNOWN_GUID = "00000000000000000000000000000001"  # (no file has it: `state nomaps`)
MODELLED = ("Standard", "Filamented", "Filamented (Roughness setup)", "URP Lit")


# ----------------------------------------------------------------------------------------------------------------------
# A Unity .mat, read the way Unity wrote it (2017.3+: "- _Name: ..."; 5.x: "- first: {name: _Name}" / "second: ...").

def parse_mat(text):
    """{name, shader: (fileID, guid), keywords: set, lightmap_flags, tex: {name: guid or ''}, floats: {name: value},
    colors: {name: (r, g, b, a)}}; None when the text holds no Material document."""
    if "--- !u!21 " not in text:
        return None
    m = {"name": "", "shader": (0, ""), "keywords": set(), "lightmap_flags": None, "tex": {}, "floats": {}, "colors": {}}
    section, entry, keyword_list = None, None, False
    for raw in text.splitlines():
        line = raw.strip()
        if keyword_list:
            if line.startswith("- "):
                m["keywords"].add(line[2:].strip())
                continue
            keyword_list = False
        a = re.match(r"m_Name:\s*(.*)$", line)
        if a and not m["name"]:
            m["name"] = a.group(1).strip()
            continue
        a = re.match(r"m_Shader:\s*\{fileID:\s*(-?\d+)(?:,\s*guid:\s*([0-9a-fA-F]+))?", line)
        if a:
            m["shader"] = (int(a.group(1)), (a.group(2) or "").lower())
            continue
        a = re.match(r"m_ShaderKeywords:\s*(.*)$", line)
        if a:
            m["keywords"].update(a.group(1).split())
            continue
        if line == "m_ValidKeywords:":
            keyword_list = True
            continue
        a = re.match(r"m_LightmapFlags:\s*(-?\d+)", line)
        if a:
            m["lightmap_flags"] = int(a.group(1))
            continue
        a = re.match(r"(m_TexEnvs|m_Floats|m_Colors|m_Ints):", line)
        if a:
            section, entry = a.group(1), None
            continue
        if section is None:
            continue
        a = re.match(r"-\s*first:\s*(?:\{name:\s*(\w+)\})?$", line)  # (5.x: the name is on this line or the next)
        if a:
            entry = a.group(1)
            continue
        a = re.match(r"name:\s*(\w+)$", line)
        if a:
            entry = a.group(1)
            continue
        a = re.match(r"second:\s*(.*)$", line)
        if a:
            line = a.group(1).strip()
            if not line:
                continue
        else:
            a = re.match(r"-\s*(\w+):\s*(.*)$", line)
            if a:
                entry, line = a.group(1), a.group(2).strip()
                if section == "m_TexEnvs" and entry not in m["tex"]:
                    m["tex"][entry] = ""
                if not line:
                    continue
        if entry is None:
            continue
        if section == "m_TexEnvs":
            a = re.match(r"m_Texture:\s*\{fileID:\s*(-?\d+)(?:,\s*guid:\s*([0-9a-fA-F]+))?", line)
            if a:
                m["tex"][entry] = (a.group(2) or "").lower() if int(a.group(1)) != 0 else ""
        elif section in ("m_Floats", "m_Ints"):
            a = re.match(r"(-?[0-9.eE+-]+)$", line)
            if a:
                try:
                    m["floats"][entry] = float(a.group(1))
                except ValueError:
                    pass
        elif section == "m_Colors":
            a = re.match(r"\{r:\s*([^,]+),\s*g:\s*([^,]+),\s*b:\s*([^,]+),\s*a:\s*([^}]+)\}", line)
            if a:
                try:
                    m["colors"][entry] = tuple(float(x) for x in a.groups())
                except ValueError:
                    pass
    return m


# ----------------------------------------------------------------------------------------------------------------------
# The project's GUID tables: each unpacked package's .unity-guids (guid, Unity's path, the path in the project) and the
# .meta texts kept beside it.

def read_guid_tables(project):
    """{guid: project-relative path}, {guid: .meta text} over every .unity-guids under the project's Assets."""
    paths, metas = {}, {}
    for root, dirs, files in os.walk(os.path.join(project, "Assets")):
        if ".unity-guids" not in files:
            continue
        with open(os.path.join(root, ".unity-guids"), encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.rstrip("\r\n").split("\t")
                if len(parts) < 3:
                    continue
                guid = parts[0].lower()
                paths.setdefault(guid, parts[2].replace("\\", "/"))
                meta = os.path.join(root, ".unity-meta", parts[0] + ".meta")
                if guid not in metas and os.path.isfile(meta):
                    with open(meta, encoding="utf-8", errors="replace") as g:
                        metas[guid] = g.read()
    return paths, metas


def meta_number(meta_text, key, default):
    a = re.search(r"^\s*" + re.escape(key) + r":\s*(-?\d+)", meta_text or "", re.M)
    return int(a.group(1)) if a else default


def shader_name(project, paths, shader):
    """The name Unity knows the shader by: the built-in ones by number, a file's own `Shader "..."` line."""
    file_id, guid = shader
    if guid == BUILTIN:
        return {46: "Standard", 45: "Standard (Specular setup)"}.get(file_id, "built-in %d" % file_id)
    if guid == URP_LIT:
        return "Universal Render Pipeline/Lit"
    rel = paths.get(guid)
    if not rel:
        return ""
    try:
        with open(os.path.join(project, rel), encoding="utf-8", errors="replace") as f:
            head = f.read(4096)
    except OSError:
        return ""
    a = re.search(r'Shader\s+"([^"]+)"', head)
    return a.group(1) if a else os.path.splitext(os.path.basename(rel))[0]


def family_of(name):
    """The rule this tool applies: Standard, Filamented (its Standard-like metallic setup), its Roughness setup, URP
    Lit; anything else is its own name (not modelled)."""
    n = name.lower()
    if "filamented" in n:
        if "(roughness setup)" in n:
            return "Filamented (Roughness setup)"
        if "(specular setup)" in n or "(cloth setup)" in n:
            return name
        return "Filamented"
    if n == "standard":
        return "Standard"
    if n in ("universal render pipeline/lit", "lit"):
        return "URP Lit"
    return name or "(a shader not in the project)"


# ----------------------------------------------------------------------------------------------------------------------
# Unity's values for one material. `stats(guid)` gives {"mean": [r, g, b, a], ...} for a texture in the project (its
# channels as data), or None.

def alpha_mean(st, meta_text):
    """The alpha Unity's shader samples: the image's own, 1 with Alpha Source None, the grey with From Gray Scale."""
    usage = meta_number(meta_text, "alphaUsage", 1)
    if usage == 0:
        return 1.0
    if usage == 2:
        return sum(st["mean"][:3]) / 3.0
    return st["mean"][3]


def unity_values(mat, family, stats, metas):
    f, kw, tex = mat["floats"], mat["keywords"], mat["tex"]
    out = {"family": family, "metallic": None, "smoothness": None, "roughness": None, "metal_from": "", "smooth_from": "",
           "reflections": "_GLOSSYREFLECTIONS_OFF" not in kw, "highlights": "_SPECULARHIGHLIGHTS_OFF" not in kw,
           "occlusion": None, "notes": []}
    if family not in MODELLED:
        return out

    def of(slot):
        guid = tex.get(slot, "")
        st = stats(guid) if guid else None
        if guid and st is None:
            out["notes"].append("%s's texture isn't in the project or couldn't be read" % slot)
        return guid, st

    urp = family == "URP Lit"
    map_keyword = "_METALLICSPECGLOSSMAP" if urp else "_METALLICGLOSSMAP"
    scale_name = "_Smoothness" if urp else "_GlossMapScale"
    slider = f.get("_Smoothness", 0.5) if urp else f.get("_Glossiness", 0.5)
    scale = f.get(scale_name, 1.0 if not urp else 0.5)
    albedo_alpha = "_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A" in kw
    main_slot = "_BaseMap" if urp and tex.get("_BaseMap") else "_MainTex"
    if family == "Filamented (Roughness setup)":  # (MetallicRough: _Glossiness is the roughness, _SpecGlossMap's red its map)
        guid, st = of("_MetallicGlossMap") if map_keyword in kw else ("", None)
        out["metallic"], out["metal_from"] = (st["mean"][0], "_MetallicGlossMap red") if st else (f.get("_Metallic", 0.0), "_Metallic")
        rguid, rst = of("_SpecGlossMap") if "_SPECGLOSSMAP" in kw else ("", None)
        rough, out["smooth_from"] = (rst["mean"][0], "_SpecGlossMap red (the roughness)") if rst else (f.get("_Glossiness", 0.5), "_Glossiness (the roughness)")
        out["roughness"], out["smoothness"] = rough, 1.0 - rough
    elif map_keyword in kw:
        guid, st = of("_MetallicGlossMap")
        if st is None:
            out["notes"].append("the metallic-gloss map can't be read: Unity's values unknown")
            return out
        out["metallic"], out["metal_from"] = st["mean"][0], "_MetallicGlossMap red"
        if meta_number(metas.get(guid), "sRGBTexture", 0) == 1:
            out["notes"].append("the metallic-gloss map is imported as sRGB: Unity linearises its red (the mean here is of the stored values)")
        if albedo_alpha:
            aguid, ast = of(main_slot)
            if ast is None:
                out["notes"].append("the albedo can't be read: Unity's smoothness unknown")
                return out
            out["smoothness"], out["smooth_from"] = alpha_mean(ast, metas.get(aguid)) * scale, "%s alpha x %s" % (main_slot, scale_name)
        else:
            out["smoothness"], out["smooth_from"] = alpha_mean(st, metas.get(guid)) * scale, "_MetallicGlossMap alpha x %s" % scale_name
        out["roughness"] = 1.0 - out["smoothness"]
    else:
        out["metallic"], out["metal_from"] = f.get("_Metallic", 0.0), "_Metallic"
        if albedo_alpha:
            aguid, ast = of(main_slot)
            if ast is None:
                out["notes"].append("the albedo can't be read: Unity's smoothness unknown")
                return out
            out["smoothness"], out["smooth_from"] = alpha_mean(ast, metas.get(aguid)) * scale, "%s alpha x %s" % (main_slot, scale_name)
        else:
            out["smoothness"], out["smooth_from"] = slider, "_Smoothness" if urp else "_Glossiness"
        out["roughness"] = 1.0 - out["smoothness"]
    oguid, ost = of("_OcclusionMap")
    if ost is not None:
        strength = f.get("_OcclusionStrength", 1.0)
        out["occlusion"] = 1.0 - strength + strength * ost["mean"][1]  # (LerpOneTo(the map's green, _OcclusionStrength))
    for k in ("metallic", "smoothness", "roughness"):
        if out[k] is not None:
            out[k] = min(1.0, max(0.0, out[k]))
    return out


def maps_unity_uses(mat, family):
    """(the .mat's slot, Blendity's slot) for each texture Unity's shader reads on this material."""
    kw, tex, used = mat["keywords"], mat["tex"], []
    urp = family == "URP Lit"
    main = "_BaseMap" if urp and tex.get("_BaseMap") else "_MainTex"
    if tex.get(main):
        used.append((main, "base_map"))
    if tex.get("_MetallicGlossMap") and ("_METALLICSPECGLOSSMAP" if urp else "_METALLICGLOSSMAP") in kw:
        used.append(("_MetallicGlossMap", "metallic_map"))
    if tex.get("_BumpMap") and "_NORMALMAP" in kw:
        used.append(("_BumpMap", "normal_map"))
    if tex.get("_EmissionMap") and "_EMISSION" in kw:
        used.append(("_EmissionMap", "emission_map"))
    return used


# ----------------------------------------------------------------------------------------------------------------------
# Blendity's side: the text of `unity materials` and of `unity mapstats`.

def unquote(s):
    return re.sub(r"\\(.)", r"\1", s[1:-1]) if len(s) >= 2 and s[0] == '"' and s[-1] == '"' else s


def words(line):
    return [unquote(w) for w in re.findall(r'"(?:[^"\\]|\\.)*"|\S+', line)]


def parse_listing(text):
    """[{name, asset, kind, users, first, metallic, roughness, ..., maps: {slot: (path, state)}}] from `unity materials`."""
    out, cur = [], None
    for line in text.splitlines():
        w = words(line)
        if not w:
            continue
        if w[0] == "material" and len(w) > 1:
            cur = {"name": w[1], "asset": "", "kind": "scene", "users": 0, "first": "", "maps": {}}
            out.append(cur)
        elif cur is None:
            continue
        elif w[0] == "end":
            cur = None
        elif w[0] == "asset" and len(w) > 2:
            cur["asset"], cur["kind"] = ("" if w[1] == "-" else w[1].replace("\\", "/")), w[2]
        elif w[0] == "users" and len(w) > 3:
            cur["users"], cur["first"] = int(w[1]), ("" if w[3] == "-" else w[3])
        elif w[0].endswith("_map"):
            cur["maps"][w[0]] = ("", "none") if len(w) < 3 else (w[1].replace("\\", "/"), w[2])
        else:
            for k in range(0, len(w) - 1, 2):  # (pairs: `metallic 1 roughness 0 specular 0.5`; a colour's three numbers under its first)
                try:
                    cur[w[k]] = float(w[k + 1])
                except ValueError:
                    break
                if w[k] == "base_color":
                    break
    return out


def finite_numbers(ws):
    """The words as numbers, or None when one is not a finite number (a float map holding NaN prints `-nan(ind)`)."""
    try:
        out = [float(x) for x in ws]
    except ValueError:
        return None
    return out if all(v == v and abs(v) != float("inf") for v in out) else None


def parse_mapstats(text):
    """{path as given: {size, mean, min, max, load_ms}} from `unity mapstats` (a missing or unreadable image, or one
    whose numbers are not finite, left out)."""
    out = {}
    for line in text.splitlines():
        w = words(line)
        if len(w) < 26 or w[0] != "mapstats" or w[2] != "size":
            continue
        num, rest = finite_numbers(w[10:14] + w[15:19] + w[20:24]), finite_numbers([w[3], w[4], w[8], w[25]])
        if num is None or rest is None:
            continue
        st = {"size": (int(rest[0]), int(rest[1])), "alpha": int(rest[2]), "mean": num[0:4], "min": num[4:8], "max": num[8:12],
              "load_ms": rest[3], "cached": len(w) > 26 and w[26] == "cached"}
        at = w.index("own_mean") if "own_mean" in w else -1  # (`unity mapstats decoders`: Blendity's own decoder's means)
        own = finite_numbers(w[at + 1:at + 5]) if at >= 0 and len(w) >= at + 5 else None
        if own:
            st["own_mean"] = own
        out[w[1].replace("\\", "/")] = st
    return out


def decoders_differ(st):
    """An image the build's library and Blendity's own decoder read differently (its colour channels' means apart by more
    than one 8-bit step): a 16-bit PNG taken as linear light, say."""
    own = st.get("own_mean") if st else None
    return bool(own) and any(abs(st["mean"][k] - own[k]) > 1.0 / 255.0 for k in range(3))


# ----------------------------------------------------------------------------------------------------------------------
# The comparison.

def effective(value, slot, listing_entry, stats_by_path):
    path, state = listing_entry["maps"].get(slot, ("", "none"))
    if not path or state != "ok":
        return value, ""
    st = stats_by_path.get(path)
    return (value * st["mean"][0], path) if st else (value, path + " (not measured)")


def classify(mat, unity, blendity, paths, stats_by_path):
    """The fault classes and the row's numbers for one material; `blendity` is its listing entry or None."""
    row = {"faults": [], "missing": [], "differ": [], "blendity_metallic": None, "blendity_roughness": None, "d_metallic": None, "d_roughness": None}
    if blendity is None:
        row["faults"].append("not read")
        return row
    row["blendity_metallic"], _ = effective(blendity.get("metallic", 0.0), "metallic_map", blendity, stats_by_path)
    row["blendity_roughness"], _ = effective(blendity.get("roughness", 0.5), "roughness_map", blendity, stats_by_path)
    for slot, b_slot in maps_unity_uses(mat, unity["family"]) if unity["family"] in MODELLED else []:  # (another shader's slots mean other things)
        in_project = mat["tex"].get(slot, "") in paths
        if in_project and blendity["maps"].get(b_slot, ("", "none"))[1] != "ok":
            row["missing"].append(slot)
    if row["missing"]:
        row["faults"].append("maps missing")
    urp = unity["family"] == "URP Lit"
    keyword = "_METALLICSPECGLOSSMAP" if urp else "_METALLICGLOSSMAP"
    slot_idle = unity["family"] in MODELLED and bool(mat["tex"].get("_MetallicGlossMap")) and keyword not in mat["keywords"]
    if unity["family"] not in MODELLED or unity["roughness"] is None:
        row["faults"].append("not modelled")
    else:
        row["d_metallic"] = row["blendity_metallic"] - unity["metallic"]
        row["d_roughness"] = row["blendity_roughness"] - unity["roughness"]
        off = abs(row["d_metallic"]) > TOLERANCE or abs(row["d_roughness"]) > TOLERANCE
        from_map = "alpha" in unity["smooth_from"] or "red" in unity["smooth_from"]
        # A texture in the slot without its keyword is only a fault when Blendity went by the slot (round 485 reads the
        # keyword: the file is as it was, the reading right).
        if slot_idle and (off or blendity["maps"].get("metallic_map", ("", "none"))[1] != "none"):
            row["faults"].append("slot without keyword")
        if off and from_map and abs(row["d_roughness"]) > TOLERANCE and "slot without keyword" not in row["faults"]:
            row["faults"].append("smoothness dropped")
        elif off and not row["faults"]:
            row["faults"].append("values differ")
        elif off and row["faults"] == ["maps missing"]:
            row["faults"].append("values differ")
    for slot in sorted(mat["tex"]):  # (any slot: a colour or a normal map read brighter is wrong whatever the shader)
        rel = paths.get(mat["tex"][slot])
        if rel and decoders_differ(stats_by_path.get(rel)) and os.path.basename(rel) not in row["differ"]:
            row["differ"].append(os.path.basename(rel))
    if row["differ"]:
        row["faults"].append("decoders differ")
    if not unity["reflections"]:
        row["faults"].append("reflections off ignored")
    if not unity["highlights"]:
        row["faults"].append("highlights off ignored")
    if unity["occlusion"] is not None and unity["occlusion"] < 0.98:
        row["faults"].append("occlusion ignored")
    return row


def build_rows(project, mats, paths, metas, stats_by_path, listing):
    """One row per Unity .mat: Unity's values, Blendity's, the faults. mats: {project-relative path: parsed .mat}."""
    by_asset = {e["asset"]: e for e in listing if e["asset"]}

    def stats(guid):  # (Unity's side: the file's own samples - Blendity's own decoder's means where both were measured)
        rel = paths.get(guid)
        st = stats_by_path.get(rel) if rel else None
        return dict(st, mean=st["own_mean"]) if st and st.get("own_mean") else st

    rows = []
    for rel in sorted(mats):
        mat = mats[rel]
        name = shader_name(project, paths, mat["shader"])
        unity = unity_values(mat, family_of(name), stats, metas)
        b = by_asset.get(rel)
        row = classify(mat, unity, b, paths, stats_by_path)
        # Whatever the shader: how many textures of the project the .mat names, and how many map slots Blendity filled.
        row["named"] = len({g for g in mat["tex"].values() if g in paths})
        row["attached"] = sum(1 for v in (b["maps"] if b else {}).values() if v[1] == "ok")
        row.update({"material": os.path.splitext(os.path.basename(rel))[0], "path": rel, "shader": name, "family": unity["family"],
                    "keywords": sorted(mat["keywords"]), "users": b["users"] if b else 0, "first": b["first"] if b else "",
                    "unity_metallic": unity["metallic"], "unity_roughness": unity["roughness"], "unity_smoothness": unity["smoothness"],
                    "metal_from": unity["metal_from"], "smooth_from": unity["smooth_from"], "occlusion": unity["occlusion"],
                    "reflections": unity["reflections"], "highlights": unity["highlights"], "notes": unity["notes"],
                    "blendity_maps": {k: v[0] + ("" if v[1] == "ok" else " (%s)" % v[1]) for k, v in (b["maps"] if b else {}).items() if v[0]},
                    "blendity_raw": {k: b.get(k) for k in ("metallic", "roughness", "specular", "exposure_occlusion")} if b else {}})
        rows.append(row)
    return rows


def num(v, digits=2):
    return "-" if v is None else ("%." + str(digits) + "f") % v


def decoder_lines(rows, stats_by_path, suspects):
    """The report's part on the images a library may read its own way (`unity mapstats decoders`)."""
    if not suspects:
        return []
    measured = [p for p in sorted(suspects, key=lambda p: os.path.basename(p).lower()) if stats_by_path.get(p, {}).get("own_mean")]
    differ = [p for p in measured if decoders_differ(stats_by_path[p])]
    lines = ["", "## Images a library may read its own way", "",
             "%d PNG(s) the materials name have 16 bits a sample or a gamma of their own; this build's decoder and Blendity's own were both asked for %d, and %d read differently."
             % (len(suspects), len(measured), len(differ))]
    if not differ:
        return lines
    lines += ["", "| image | bits | this build's mean R G B | Blendity's own decoder's (the file's samples) | named by |", "|---|---|---|---|---|"]
    for p in differ:
        st, name = stats_by_path[p], os.path.basename(p)
        by = sorted(r["material"] for r in rows if name in r["differ"])
        lines.append("| %s | %d | %.3f %.3f %.3f | %.3f %.3f %.3f | %s |" % ((name, suspects[p]["depth"]) + tuple(st["mean"][:3]) + tuple(st["own_mean"][:3]) + (", ".join(by) or "-",)))
    return lines


def report_lines(rows, stats_by_path, title, value_paths=None, suspects=None):
    """value_paths: the maps of the second table (all the measured ones when None)."""
    worn = [r for r in rows if r["users"]]
    lines = ["# " + title, "",
             "%d Unity materials, %d worn by the scene. Metallic and roughness are means over a material's maps; a difference of more than %.2f is a fault."
             % (len(rows), len(worn), TOLERANCE), ""]
    for label, group in (("worn by the scene", worn), ("all", rows)):
        counts = {}
        for r in group:
            for f in r["faults"] or ["ok"]:
                counts[f] = counts.get(f, 0) + 1
        lines.append("Faults, %s: %s." % (label, ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts, key=lambda k: (-counts[k], k))) or "none"))
    lines.append("Map slots Blendity filled, over the textures of the project the materials name: %d of %d worn by the scene, %d of %d in all."
                 % (sum(r["attached"] for r in worn), sum(r["named"] for r in worn), sum(r["attached"] for r in rows), sum(r["named"] for r in rows)))
    lines += ["", "| material | shader | worn by | Unity metal / rough | from | Blendity metal / rough | off by | maps (filled / named) | not attached | what is wrong |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for r in sorted(rows, key=lambda r: (-r["users"], r["material"].lower())):
        src = ", ".join(x for x in (r["metal_from"], r["smooth_from"]) if x)
        off = "-" if r["d_roughness"] is None else "%+.2f / %+.2f" % (r["d_metallic"], r["d_roughness"])
        lines.append("| %s | %s | %d | %s / %s | %s | %s / %s | %s | %d / %d | %s | %s |" % (
            r["material"], r["family"], r["users"], num(r["unity_metallic"]), num(r["unity_roughness"]), src or "-", num(r["blendity_metallic"]),
            num(r["blendity_roughness"]), off, r["attached"], r["named"], ", ".join(r["missing"]) or "-", ", ".join(r["faults"]) or "ok"))
    shown = [p for p in (stats_by_path if value_paths is None else value_paths) if p in stats_by_path]
    if shown:
        lines += ["", "## The maps Unity reads these values from", "",
                  "| map | size | mean red (metal) | mean green (occlusion) | mean alpha (smoothness) | loaded in |", "|---|---|---|---|---|---|"]
        for path in sorted(shown, key=lambda p: os.path.basename(p).lower()):
            st = stats_by_path[path]
            lines.append("| %s | %d x %d | %.3f | %.3f | %.3f | %s |" % (os.path.basename(path), st["size"][0], st["size"][1], st["mean"][0], st["mean"][1], st["mean"][3],
                                                                    "(cached)" if st.get("cached") else "%.0f ms" % st.get("load_ms", 0.0)))
        timed = [stats_by_path[p]["load_ms"] for p in shown if not stats_by_path[p].get("cached") and stats_by_path[p].get("load_ms")]
        if timed:
            lines += ["", "Loading these %d maps took %.0f ms in all (decode and mip levels, one after another on one thread)." % (len(timed), sum(timed))]
    return lines + decoder_lines(rows, stats_by_path, suspects)


# ----------------------------------------------------------------------------------------------------------------------
# PNG files read here, without Blendity: which ones a decoder may read its own way, `state png8`, the self-test's maps.

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
PNG_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}  # (colour type: samples a pixel; a palette's type 3 is not read here)


def png_header(path):
    """What a PNG says before its pixels, read without decoding them: size, bits a sample, colour type and the gamma
    chunk a decoder may act on. None for a file that is not a PNG."""
    out = {"size": (0, 0), "depth": 0, "ctype": 0, "interlace": 0, "gamma": None, "srgb": False}
    try:
        with open(path, "rb") as f:
            if f.read(8) != PNG_MAGIC:
                return None
            while True:
                head = f.read(8)
                if len(head) < 8:
                    break
                n, kind = struct.unpack(">I4s", head)
                if kind in (b"IDAT", b"IEND"):
                    break
                body = f.read(n)
                f.seek(4, 1)
                if kind == b"IHDR" and len(body) == 13:
                    w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
                    out.update(size=(w, h), depth=depth, ctype=ctype, interlace=interlace)
                elif kind == b"gAMA" and len(body) == 4:
                    out["gamma"] = struct.unpack(">I", body)[0] / 100000.0
                elif kind == b"sRGB":
                    out["srgb"] = True
    except OSError:
        return None
    return out if out["depth"] else None


def decoder_may_differ(head):
    """A PNG a library can read its own way: 16 bits a sample (libpng's simple interface takes those as linear light), or
    a gamma of its own that no sRGB chunk overrides."""
    if not head:
        return False
    own_gamma = head["gamma"] is not None and not head["srgb"] and abs(head["gamma"] - 0.45455) > 0.002
    return head["depth"] == 16 or own_gamma


def png_rows(path):
    """(w, h, bits a sample, colour type, the image's rows unfiltered) of a non-interlaced grey, grey + alpha, RGB or
    RGBA PNG of 8 or 16 bits a sample."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != PNG_MAGIC:
        raise ValueError(path + ": not a PNG")
    at, idat, meta = 8, [], None
    while at < len(data):
        n = struct.unpack(">I", data[at:at + 4])[0]
        kind, body = data[at + 4:at + 8], data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            meta = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        at += 12 + n
    if meta is None:
        raise ValueError(path + ": no header")
    w, h, depth, ctype, _, _, interlace = meta
    if depth not in (8, 16) or interlace != 0 or ctype not in PNG_CHANNELS:
        raise ValueError(path + ": only 8- or 16-bit grey, RGB or RGBA, not interlaced")
    bpp = PNG_CHANNELS[ctype] * depth // 8
    raw, stride, rows, prev, at = zlib.decompress(b"".join(idat)), w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        f, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            line = bytearray(map(lambda a, b: (a + b) & 255, line, prev))
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - bpp] if i >= bpp else 0) + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a, b, c = (line[i - bpp] if i >= bpp else 0), prev[i], (prev[i - bpp] if i >= bpp else 0)
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, depth, ctype, rows


def png_chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def png16_to_8(path):
    """A 16-bit PNG rewritten in place as an 8-bit one of its high bytes - the values Unity and Blendity's own decoder
    take from it. False (and the file untouched) for anything else."""
    head = png_header(path)
    if not head or head["depth"] != 16 or head["interlace"] != 0 or head["ctype"] not in PNG_CHANNELS:
        return False
    w, h, _, ctype, rows = png_rows(path)
    raw = b"".join(b"\x00" + bytes(line[0::2]) for line in rows)
    with open(path, "wb") as f:
        f.write(PNG_MAGIC + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0)) + png_chunk(b"IDAT", zlib.compress(raw, 6)) + png_chunk(b"IEND", b""))
    return True


# ----------------------------------------------------------------------------------------------------------------------
# `table`: Blendity run headless on the project.

def unity_mats(project, paths):
    mats = {}
    for guid, rel in paths.items():
        if not rel.lower().endswith(".mat"):
            continue
        try:
            with open(os.path.join(project, rel), encoding="utf-8", errors="replace") as f:
                m = parse_mat(f.read())
        except OSError:
            m = None
        if m:
            mats[rel] = m
    return mats


def value_textures(mats, paths):
    """The project-relative paths of the textures the rule reads channels of."""
    wanted = []
    for m in mats.values():
        slots = ["_MetallicGlossMap", "_OcclusionMap", "_SpecGlossMap"]
        if "_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A" in m["keywords"]:
            slots += ["_MainTex", "_BaseMap"]
        for s in slots:
            rel = paths.get(m["tex"].get(s, ""))
            if rel and rel not in wanted:
                wanted.append(rel)
    return wanted


def decoder_suspects(project, mats, paths):
    """{project-relative path: its header} of the PNGs any material names that a library may read its own way."""
    out = {}
    for m in mats.values():
        for guid in m["tex"].values():
            rel = paths.get(guid)
            if not rel or rel in out or not rel.lower().endswith(".png"):
                continue
            head = png_header(os.path.join(project, rel))
            if decoder_may_differ(head):
                out[rel] = head
    return out


def mapstats_cmds(textures, word, out, stem):
    """Console lines that write `unity mapstats` of the textures, a few images a line (a command line has a length
    limit), and the files they write."""
    cmds, files = [], []
    for i in range(0, len(textures), 8):
        files.append(os.path.join(out, "%s_%d.txt" % (stem, i // 8)))
        cmds.append("unity mapstats " + word + " ".join('"%s"' % t for t in textures[i:i + 8]) + ' to "%s"' % files[-1].replace("\\", "/"))
    return cmds, files


def run_blendity(exe, project, out, cmds, log_name):
    args = [exe, "--headless-screenshot", os.path.join(out, log_name + ".png"), "--size", "640x400", "--frames", "2"]
    for c in cmds:
        args += ["--cmd", c]
    env = dict(os.environ, BLENDITY_PROJECT=project, BLENDITY_TRASH=os.path.join(out, "trash"))
    with open(os.path.join(out, log_name + ".log"), "w", encoding="utf-8") as log:
        r = subprocess.run(args, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    return r.returncode


def table(exe, project, scene, out, extra_cmds):
    project, out = scratch_or_refuse(project, "`table` runs Blendity in the project (it opens the scene there and writes its trash)"), os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    paths, metas = read_guid_tables(project)
    if not paths:
        sys.exit("no unpacked Unity package (.unity-guids) under %s" % os.path.join(project, "Assets"))
    mats = unity_mats(project, paths)
    textures = value_textures(mats, paths)
    suspects = decoder_suspects(project, mats, paths)
    listing_file = os.path.join(out, "blendity_materials.txt")
    for f in os.listdir(out):
        if f == "blendity_materials.txt" or re.match(r"(mapstats|decoders)_\d+\.txt$", f):
            os.remove(os.path.join(out, f))
    # Before the scene opens, so each load is timed cold. The images a library may read its own way are read by both
    # decoders (`decoders`); the others once.
    cmds, stat_files = mapstats_cmds([t for t in textures if t not in suspects], "", out, "mapstats")
    more, more_files = mapstats_cmds(sorted(suspects), "decoders ", out, "decoders")
    cmds, stat_files = cmds + more, stat_files + more_files
    cmds += ["open " + scene] + list(extra_cmds) + ['unity materials all to "%s"' % listing_file.replace("\\", "/")]
    code = run_blendity(os.path.abspath(exe), project, out, cmds, "table")
    if code != 0 or not os.path.isfile(listing_file):
        sys.exit("Blendity didn't write its listing (exit %d; see %s) - it needs a build with `unity materials` (round 484 on)" % (code, os.path.join(out, "table.log")))
    with open(listing_file, encoding="utf-8", errors="replace") as f:
        listing = parse_listing(f.read())
    stats_by_path = {}
    for sf in stat_files:
        if os.path.isfile(sf):
            with open(sf, encoding="utf-8", errors="replace") as f:
                stats_by_path.update(parse_mapstats(f.read()))
    rows = build_rows(project, mats, paths, metas, stats_by_path, listing)
    scene_materials = [e for e in listing if e["kind"] == "scene" and e["users"]]
    lines = report_lines(rows, stats_by_path, "Unity's materials beside Blendity's: %s" % scene, textures, suspects)
    if scene_materials:
        lines += ["", "The scene also wears %d material(s) not linked to any Unity .mat: %s." % (
            len(scene_materials), ", ".join(sorted({e["name"] for e in scene_materials}))[:600])]
    with open(os.path.join(out, "materials.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(out, "materials.json"), "w", encoding="utf-8") as f:
        json.dump({"scene": scene, "tolerance": TOLERANCE, "materials": rows, "maps": stats_by_path, "decoder_suspects": suspects,
                   "unlinked_scene_materials": [e["name"] for e in scene_materials]}, f, indent=1)
    print("\n".join(lines))
    return 0


# ----------------------------------------------------------------------------------------------------------------------
# `state`: a scratch project's .mat files rewritten into a test state.

def scratch_or_refuse(project, what="`state` rewrites the project's files"):
    """The project's absolute path when it is marked as a scratch copy: an empty BLENDITY_SCRATCH file beside its Assets
    folder (BLENDITY_COPY, the name tools/unity_compare.py gives a copied Unity project, counts too)."""
    project = os.path.abspath(project)
    if not any(os.path.isfile(os.path.join(project, marker)) for marker in ("BLENDITY_SCRATCH", "BLENDITY_COPY")):
        sys.exit("refused: %s, so it only works on a scratch copy - write an empty BLENDITY_SCRATCH file beside %s" % (what, os.path.join(project, "Assets")))
    return project


def nomaps_text(text):
    """Every texture the .mat names gets a GUID no file has."""
    return re.sub(r"(m_Texture:\s*\{fileID:\s*-?[1-9]\d*,\s*guid:\s*)[0-9a-fA-F]+", lambda a: a.group(1) + UNKNOWN_GUID, text)


def set_float(text, name, value):
    """The float `name` set in the .mat's m_Floats (added at the list's head when the file doesn't have it)."""
    pat = re.compile(r"^(\s*-\s*" + re.escape(name) + r":\s*)(-?[0-9.eE+-]+)\s*$", re.M)
    if pat.search(text):
        return pat.sub(lambda a: a.group(1) + ("%.6g" % value), text, count=1)
    a = re.search(r"^(\s*)m_Floats:\s*$", text, re.M)
    if not a:
        return text
    return text[:a.end()] + "\n" + a.group(1) + "- %s: %.6g" % (name, value) + text[a.end():]


def clear_slot(text, slot):
    """The texture slot `slot` emptied (its tiling kept)."""
    pat = re.compile(r"(^\s*-\s*" + re.escape(slot) + r":\s*\n\s*m_Texture:\s*)\{[^}]*\}", re.M)
    return pat.sub(lambda a: a.group(1) + "{fileID: 0}", text, count=1)


def byhand_text(text, row):
    """Unity's mean metallic and smoothness on the sliders, the metallic-gloss slot emptied: what today's importer then
    reads is the two values, with no map."""
    smooth = 1.0 - row["unity_roughness"]
    text = clear_slot(text, "_MetallicGlossMap")
    text = set_float(text, "_Metallic", row["unity_metallic"])
    text = set_float(text, "_Glossiness", smooth)
    if row["family"] == "URP Lit":
        text = set_float(text, "_Smoothness", smooth)
    return text


def state_png8(project):
    """Every 16-bit PNG under the scratch project's Assets rewritten as 8-bit (slow in plain Python: a 4096 x 4096 image
    takes about a minute)."""
    project = scratch_or_refuse(project)
    changed = 0
    for root, _, files in os.walk(os.path.join(project, "Assets")):
        for name in sorted(files):
            if name.lower().endswith(".png") and png16_to_8(os.path.join(root, name)):
                changed += 1
                print("png8: %s" % os.path.relpath(os.path.join(root, name), project).replace("\\", "/"), flush=True)
    print("%d 16-bit PNG(s) rewritten as 8-bit in %s" % (changed, project))
    return 0


def state(project, which, materials_json=None):
    project = scratch_or_refuse(project)
    paths, _ = read_guid_tables(project)
    by_path = {}
    if which == "byhand":
        with open(materials_json, encoding="utf-8") as f:
            by_path = {r["path"]: r for r in json.load(f)["materials"]}
    changed = 0
    for rel in sorted(p for p in paths.values() if p.lower().endswith(".mat")):
        full = os.path.join(project, rel)
        try:
            with open(full, encoding="utf-8", errors="replace", newline="") as f:
                text = f.read()
        except OSError:
            continue
        if which == "nomaps":
            new = nomaps_text(text)
        else:
            row = by_path.get(rel)
            wrong = row and row["unity_roughness"] is not None and any(x in row["faults"] for x in ("smoothness dropped", "slot without keyword", "values differ"))
            new = byhand_text(text, row) if wrong else text
        if new != text:
            with open(full, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            changed += 1
            print("%s: %s" % (which, rel))
    print("%d material file(s) rewritten in %s" % (changed, project))
    return 0


# ----------------------------------------------------------------------------------------------------------------------
# The self-test, on tests/fixtures/unity_gloss.

def read_png_rgba(path):
    """(w, h, bytes R G B A) of a PNG as its file holds it: 8 bits a sample, a 16-bit sample's high byte."""
    w, h, depth, ctype, rows = png_rows(path)
    ch, step = PNG_CHANNELS[ctype], depth // 8
    out = bytearray()
    for line in rows:
        s = line[0::step]  # (the high byte of each sample)
        for x in range(w):
            p = s[x * ch:x * ch + ch]
            out += (p[0:1] * 3 if ch < 3 else p[0:3]) + (p[ch - 1:ch] if ch in (2, 4) else b"\xff")
    return w, h, bytes(out)


def png_stats(path):
    w, h, px = read_png_rgba(path)
    n = w * h
    mean = [sum(px[k::4]) / (255.0 * n) for k in range(4)]
    return {"size": (w, h), "alpha": 1, "mean": mean, "min": [min(px[k::4]) / 255.0 for k in range(4)], "max": [max(px[k::4]) / 255.0 for k in range(4)],
            "load_ms": 0.0, "cached": False}


def fixture_project(tmp):
    """The committed materials as an unpacked package in a scratch project: <tmp>/Assets/Imported/UnityGloss."""
    dest = os.path.join(tmp, "Assets", "Imported", "UnityGloss")
    shutil.copytree(FIXTURES, dest)
    return tmp


def self_test():
    import tempfile
    fails = []

    def check(ok, what):
        if not ok:
            fails.append(what)

    with open(os.path.join(FIXTURES, "expected.json"), encoding="utf-8") as f:
        expected = json.load(f)
    with tempfile.TemporaryDirectory() as tmp:
        project = fixture_project(tmp)
        paths, metas = read_guid_tables(project)
        mats = unity_mats(project, paths)
        check(len(mats) == len(expected["materials"]), "the fixture's .mat files: %d read, %d expected" % (len(mats), len(expected["materials"])))
        stats_by_path = {rel: png_stats(os.path.join(project, rel)) for rel in paths.values() if rel.lower().endswith(".png")}
        with open(os.path.join(FIXTURES, "blendity_today.txt"), encoding="utf-8") as f:
            listing = parse_listing(f.read())
        rows = {r["material"]: r for r in build_rows(project, mats, paths, metas, stats_by_path, listing)}
        for name, want in expected["materials"].items():
            r = rows.get(name)
            if r is None:
                fails.append("%s: no row" % name)
                continue
            for key in ("unity_metallic", "unity_roughness", "occlusion"):
                if key not in want:
                    continue
                got = r[key]
                same = (got is None) == (want[key] is None) and (got is None or abs(got - want[key]) <= 0.005)
                check(same, "%s: %s %s, the README works out %s" % (name, key, got, want[key]))
            check(r["family"] == want["family"], "%s: family %r, expected %r" % (name, r["family"], want["family"]))
            check(sorted(r["faults"]) == sorted(want["faults_today"]), "%s: faults %s, expected %s" % (name, sorted(r["faults"]), sorted(want["faults_today"])))
        lines = report_lines(list(rows.values()), stats_by_path, "self-test")
        check(not any("smoothness dropped" in line or "slot without keyword" in line for line in lines), "round 485's reading: no smoothness dropped, no slot without keyword")
        # Round 484's reading of the same files - what this test held as "today" until round 485 put it right (the
        # keyword's rule, a map's smoothness as its mean). The fault classes still name each of them: (metallic,
        # roughness, the map in the metallic slot when the reading differs there).
        round_484 = {"gloss_in_metal_alpha": (1.0, 0.0, None), "gloss_scaled": (1.0, 0.5, None), "gloss_in_albedo_alpha": (0.1, 0.5, None),
                     "slot_without_keyword": (1.0, 0.0, "mask_r204_g255_a51.png"), "filamented_mask": (1.0, 0.0, None), "urp_lit_mask": (1.0, 0.5, None)}
        old = [dict(e, maps=dict(e["maps"])) for e in listing]
        for e in old:
            was = round_484.get(os.path.splitext(os.path.basename(e["asset"]))[0])
            if not was:
                continue
            e["metallic"], e["roughness"] = was[0], was[1]
            if was[2]:
                e["maps"]["metallic_map"] = (os.path.dirname(e["asset"]) + "/" + was[2], "ok")
        rows_old = {r["material"]: r for r in build_rows(project, mats, paths, metas, stats_by_path, old)}
        for name, want in expected["materials"].items():
            was = sorted(want.get("faults_round_484", want["faults_today"]))
            got = sorted(rows_old[name]["faults"]) if name in rows_old else None
            check(got == was, "%s as round 484 read it: faults %s, expected %s" % (name, got, was))
        lines_old = report_lines(list(rows_old.values()), stats_by_path, "self-test")
        check(any(line.startswith("| gloss_in_metal_alpha |") and "smoothness dropped" in line for line in lines_old), "the report's row for gloss_in_metal_alpha as round 484 read it")
        # The table of means an unpack writes beside the GUIDs (round 485, ADR 0131): the committed one has a line for
        # each map a material reads its gloss from by its keywords, with the file's bytes, its size and the means this
        # reader finds in the PNG.
        with open(os.path.join(FIXTURES, ".unity-stats"), encoding="utf-8", newline="") as f:
            stats_lines = [line.split("\t") for line in f.read().split("\n") if line]
        gloss = set()
        for m in mats.values():
            if "_METALLICGLOSSMAP" in m["keywords"] or "_METALLICSPECGLOSSMAP" in m["keywords"]:
                gloss.add(m["tex"].get("_MetallicGlossMap", ""))
            if "_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A" in m["keywords"]:
                gloss.add(m["tex"].get("_BaseMap") or m["tex"].get("_MainTex", ""))
        gloss.discard("")
        check(sorted(w[0] for w in stats_lines) == sorted(gloss) and all(len(w) == 8 for w in stats_lines),
              ".unity-stats: a line of eight fields for each gloss map (%d lines, %d maps)" % (len(stats_lines), len(gloss)))
        for w in stats_lines:
            rel = paths.get(w[0], "")
            st = stats_by_path.get(rel)
            if not st or len(w) != 8:
                fails.append(".unity-stats: %s is not a map this reader measured" % w[0])
                continue
            same = (int(w[1]) == os.path.getsize(os.path.join(project, rel)) and (int(w[2]), int(w[3])) == tuple(st["size"]) and
                    all(abs(float(w[4 + k]) - st["mean"][k]) < 1e-6 for k in range(4)))
            check(same, ".unity-stats: %s's line %s, the PNG reads %s %s" % (os.path.basename(rel), w[1:], st["size"], st["mean"]))
        # A listing with Unity's own values is clean of value faults; one without the material says `not read`.
        fixed = [dict(e) for e in listing]
        for e in fixed:
            r = rows.get(os.path.splitext(os.path.basename(e["asset"]))[0])
            if r and r["unity_roughness"] is not None:
                e["metallic"], e["roughness"], e["maps"] = r["unity_metallic"], r["unity_roughness"], {k: v for k, v in e["maps"].items() if k not in ("metallic_map", "roughness_map")}
        rows2 = {r["material"]: r for r in build_rows(project, mats, paths, metas, stats_by_path, fixed)}
        for name, r in rows2.items():
            check(not any(x in r["faults"] for x in ("smoothness dropped", "values differ")), "%s: Unity's own values still read as a value fault: %s" % (name, r["faults"]))
        rows3 = build_rows(project, mats, paths, metas, stats_by_path, [])
        check(all(r["faults"][:1] == ["not read"] for r in rows3), "an empty listing: every material `not read`")
        # The 5.x layout reads like the modern one.
        old = ("--- !u!21 &2100000\nMaterial:\n  m_Name: Old\n  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n  m_ShaderKeywords: _METALLICGLOSSMAP\n"
               "  m_SavedProperties:\n    m_TexEnvs:\n    - first:\n        name: _MetallicGlossMap\n      second:\n        m_Texture: {fileID: 2800000, guid: 0123456789abcdef0123456789abcdef, type: 3}\n"
               "    m_Floats:\n    - first:\n        name: _GlossMapScale\n      second: 0.5\n    m_Colors:\n    - first:\n        name: _Color\n      second: {r: 1, g: 0.5, b: 0.25, a: 1}\n")
        o = parse_mat(old)
        check(o and o["tex"].get("_MetallicGlossMap") == "0123456789abcdef0123456789abcdef" and o["floats"].get("_GlossMapScale") == 0.5 and
              o["colors"].get("_Color") == (1.0, 0.5, 0.25, 1.0) and "_METALLICGLOSSMAP" in o["keywords"], "the 5.x layout")
        check(parse_mat("not a material") is None, "text without a Material document")
        # The texts of both commands.
        ms = parse_mapstats('mapstats "Assets/a b.png" size 8 4 float 0 alpha 1 mean 0.100000 0.200000 0.300000 0.400000 min 0.000000 0.100000 0.200000 0.300000 '
                            'max 0.500000 0.600000 0.700000 0.800000 load_ms 12.5\nmapstats "gone.png" missing\n')
        check(list(ms) == ["Assets/a b.png"] and ms["Assets/a b.png"]["mean"] == [0.1, 0.2, 0.3, 0.4] and ms["Assets/a b.png"]["size"] == (8, 4) and
              abs(ms["Assets/a b.png"]["load_ms"] - 12.5) < 1e-9, "the mapstats line")
        md = parse_mapstats('mapstats "Assets/n.png" size 8 8 float 0 alpha 1 mean 0.729412 0.729412 1.000000 1.000000 min 0.729412 0.729412 1.000000 1.000000 '
                            'max 0.729412 0.729412 1.000000 1.000000 load_ms 0.4 cached own_mean 0.501961 0.501961 1.000000 1.000000\n').get("Assets/n.png", {})
        check(md.get("own_mean") == [0.501961, 0.501961, 1.0, 1.0] and md.get("cached") and decoders_differ(md) and not decoders_differ(ms["Assets/a b.png"]) and
              not decoders_differ(dict(md, own_mean=md["mean"])), "the mapstats line with both decoders' means")
        cmds, files = mapstats_cmds(["a.png"] * 9, "decoders ", "out", "decoders")
        check(len(cmds) == 2 and len(files) == 2 and cmds[0].startswith('unity mapstats decoders "a.png" "a.png"') and cmds[1].count('"a.png"') == 1 and
              cmds[1].endswith('out/decoders_1.txt"'), "the mapstats command lines")
        # The 16-bit map: read here as its file holds it (its high bytes), and the one image a library may read its own
        # way. With both decoders' means for it - as a build with libpng gives them - the material naming it is marked.
        n16 = next((rel for rel in paths.values() if rel.endswith("/normal16_flat.png")), "")
        heads = {os.path.basename(rel): png_header(os.path.join(project, rel)) for rel in paths.values()}
        check(n16 in stats_by_path and abs(stats_by_path[n16]["mean"][0] - 128 / 255.0) < 1e-6 and stats_by_path[n16]["mean"][2:] == [1.0, 1.0], "the 16-bit map's high bytes")
        check(heads["normal16_flat.png"]["depth"] == 16 and heads["normal_flat.png"]["depth"] == 8 and heads["normal_flat.png"]["size"] == (8, 8) and
              heads["mirror.mat"] is None, "a PNG's header")
        check(decoder_may_differ({"depth": 8, "gamma": 1.0, "srgb": False}) and not decoder_may_differ({"depth": 8, "gamma": 0.45455, "srgb": False}) and
              not decoder_may_differ({"depth": 8, "gamma": 1.0, "srgb": True}) and not decoder_may_differ(None), "which PNGs a library may read its own way")
        suspects = decoder_suspects(project, mats, paths)
        check(list(suspects) == [n16], "the images a library may read its own way: %s" % sorted(suspects))
        both = dict(stats_by_path)
        both[n16] = dict(stats_by_path[n16], mean=[186 / 255.0, 186 / 255.0, 1.0, 1.0], own_mean=stats_by_path[n16]["mean"])
        rows_d = build_rows(project, mats, paths, metas, both, listing)
        check([r["material"] for r in rows_d if "decoders differ" in r["faults"]] == ["sixteen_bit_normal"], "the material whose 16-bit map two decoders read differently")
        same = dict(stats_by_path)
        same[n16] = dict(stats_by_path[n16], own_mean=stats_by_path[n16]["mean"])
        check(not any("decoders differ" in r["faults"] for r in build_rows(project, mats, paths, metas, same, listing)), "two decoders that agree: no fault")
        lines_d = report_lines(rows_d, both, "self-test", [], suspects)
        check(any(line.startswith("| normal16_flat.png | 16 | 0.729 0.729 1.000 | 0.502 0.502 1.000 | sixteen_bit_normal |") for line in lines_d), "the report's row for the 16-bit map")
        check(not any("read its own way" in line for line in lines), "a report without suspects has no decoders part")
        # `state`: refused without the marker; png8, byhand and nomaps on the scratch copy.
        try:
            scratch_or_refuse(project)
            fails.append("`state` wasn't refused on a project without BLENDITY_SCRATCH")
        except SystemExit:
            pass
        try:
            table("no.exe", project, "no.scene", os.path.join(tmp, "out"), [])
            fails.append("`table` wasn't refused on a project without BLENDITY_SCRATCH")
        except SystemExit as refusal:
            check("scratch copy" in str(refusal) and not os.path.exists(os.path.join(tmp, "out")), "`table`'s refusal names the reason and writes nothing")
        nan = parse_mapstats('mapstats "f.exr" size 8 8 float 1 alpha 0 mean -nan(ind) 0.5 0.5 1.0 min 0.0 0.0 0.0 1.0 max inf 1.0 1.0 1.0 load_ms 0.3\n')
        check(nan == {}, "a map whose numbers are not finite is left out, not a crash")
        open(os.path.join(project, "BLENDITY_SCRATCH"), "w").close()
        pngs = {rel: open(os.path.join(project, rel), "rb").read() for rel in paths.values() if rel.lower().endswith(".png")}
        state_png8(project)
        check(png_header(os.path.join(project, n16))["depth"] == 8 and read_png_rgba(os.path.join(project, n16))[2] == bytes((128, 128, 255, 255)) * 64,
              "png8: the 16-bit map is 8-bit with its high bytes")
        check(all(open(os.path.join(project, rel), "rb").read() == data for rel, data in pngs.items() if rel != n16), "png8 left the 8-bit maps as they were")
        check(not decoder_suspects(project, mats, paths), "after png8 no image is left that a library may read its own way")
        with open(os.path.join(tmp, "materials.json"), "w", encoding="utf-8") as f:
            json.dump({"materials": list(rows_old.values())}, f)  # (round 484's reading: it has materials that are off)
        state(project, "byhand", os.path.join(tmp, "materials.json"))
        mats_bh = unity_mats(project, paths)
        for rel, m in mats_bh.items():
            r = rows_old[os.path.splitext(os.path.basename(rel))[0]]
            if not any(x in r["faults"] for x in ("smoothness dropped", "slot without keyword", "values differ")):
                check(m == mats[rel], "%s: byhand changed a material that wasn't off" % rel)
                continue
            check(m["tex"].get("_MetallicGlossMap", "") == "" and abs(m["floats"].get("_Metallic", -1) - r["unity_metallic"]) < 1e-5 and
                  abs(m["floats"].get("_Glossiness", -1) - (1.0 - r["unity_roughness"])) < 1e-5, "%s: byhand's sliders" % rel)
        state(project, "nomaps")
        for rel, m in unity_mats(project, paths).items():
            check(all(g in ("", UNKNOWN_GUID) for g in m["tex"].values()), "%s: nomaps left a known texture" % rel)
    for f in fails:
        print("FAIL: " + f)
    print("unity_materials self-test: " + ("%d failed" % len(fails) if fails else "all passed"))
    return 1 if fails else 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if argv[:1] == ["table"] and len(argv) >= 5:
        extra, rest = [], argv[5:]
        while rest:
            if rest[0] == "--cmd" and len(rest) > 1:
                extra.append(rest[1])
                rest = rest[2:]
            else:
                sys.exit("unknown or incomplete option " + rest[0])
        return table(argv[1], argv[2], argv[3], argv[4], extra)
    if argv[:1] == ["state"] and len(argv) == 3 and argv[2] == "nomaps":
        return state(argv[1], "nomaps")
    if argv[:1] == ["state"] and len(argv) == 3 and argv[2] == "png8":
        return state_png8(argv[1])
    if argv[:1] == ["state"] and len(argv) == 4 and argv[2] == "byhand":
        return state(argv[1], "byhand", argv[3])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
