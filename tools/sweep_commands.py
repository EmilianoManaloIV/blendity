#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the operations stress sweep's console command table (ADR 0105).

Use --check to verify the committed table. Each registered verb needs either
one maintained invocation in EXAMPLES or a reason to leave it out in SKIP.
"""
import argparse
import difflib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = """
activecamera Cube
aimlight -1 0 3
alignview
alltools on
anim new
animemat Nope
applymods
armature reset Hips
assignmat Assets/Materials/MA.mat
assist on ai
autopose on
autorig build
autosmooth off
avatar eyes
bakedialog textures
bevelprofile 0.9
blockout new
board fit
body show on
bone add Bone 1
boolean difference
brush save
budget pc
camera 0 0 6
camerapreview lock
clearguides
colorattr add
component Outline
create Cube
curve select Path 0
customnormals clear
delete Cube
deletemat Orange
develop undo
device cpu
dock bringback
draw guide
drawfillet 0
drawmode perp off
drawpoint 0 0 0
drawproject view
droponsurface 450 300
duplicate
edit on
editop fill
edittool array
engine path
esel 48 49 49 50 50 61 61 72 72 71 71 70 70 59 59 48
exportoptions glb
facemat Assets/Materials/Red.mat
facenormals cube
favorite No Such Command
feel Maya
filter remove 0
filters 0
fk add Bone
flatten 1
focus 0 0 0 2.8
fps 60
fsel 0
gpu list
guide 0.37 0 -3 0 0 1
guides off
guidesfromedges
hair rig
hs grid 3 2
ik fitsizes
insetmode region 0.1
inspector lock
jobs cancel Building Hall
join
keymap Maya
layout 2 by 3
layoutguide reset
lesson 1
libs
lighting seed
loopcuts 3 0.25
matdelete R80 Oak, No Such Material, R80Pine
material Toon
matset Name Renamed
meshdeform bind
meshop flip
motioncheck
navbuttons always
newmat Red
newscene
newslotmat 1
newtex Fresh
ngon on
origin bounds
overlaps merge
overlay all
paint on
palette run create cube
panel new Favs
paper new
perf clip
pickfocus
pilot
play
pointcloud degree 1
points merge Path 0 99
pose copy
position 0 1 0
preferences
preview
probes show
proportional off
pushpull individual on
quality low
quickmenu pick Delete
raster simd on
redo clamp 1
redraw always
refimage
removeslot 1
removeunusedslots
report stats
research
rig humanoid
rotate 0 90 0
samples 8
scale 2
scatter on
sculpt on
seam 0 1
select P
selectadd Q
selectall
separate loose
seq new
set Light.Mode 2
set_extrude_individual 1
settex Fresh Width 128
shading solid
shape filter
shortcut clear stats
sketch on
sketchbuild box
sketchcanvas 1
sketchcircle -2 0 0 0.5
sketchguide off
sketchlift
sketchline 0 0 0 0 0 1
sketchproject
sketchstroke 300 300 400 320
sketchtrace edge
skin bind
smat apply MyMetal
smooth
snap bogus
splat stats
spring find
stats
strokeobj on
subdivide 3
surface list
surfacealign on
tabletmode on
tabs all on
texlayer Fresh reset 1
texlist
texture uvgrid
theme Neon
threads 2
toast a toast by hand
tool move
toonstyle Nope
ui save
undo
uv pack
uvsel all
uvxf fit
vgroup add A
viewport status
views rtshadows
viewscale 50
viewshape remove 1
visemes
vpaint on
vpautomask none
vpblend bogus
vpbrush blur
vpcolor a b c
vpset front on
vpstroke 5
vsel 0
walk cycle walk
weightpaint on
wheel open
where
window Game
world sky
wpautomask none
wpbrush add
wpclean 0.6
wpfalloff smooth
wpfront off
wpmirror on
wpnormalize
wpstroke
zfight fix
""".strip().splitlines()

SKIP = {
    "new": "replaces the scene the sweep measures", "open": "replaces the scene", "save": "writes the scene (the files rows have it)",
    "stress": "builds scenes", "bench": "a benchmark of its own", "benchscene": "builds scenes", "update": "the updater (network)",
    "moonray": "an external program", "vr": "a headset", "ai": "a download", "unity": "the installed Unity", "livelink": "a network server",
    "stop": "ends Play", "help": "prints the list", "clear": "clears the log", "screenshot": "writes a file", "saverender": "writes a file",
    "render": "the responsiveness rows", "renderanim": "the responsiveness rows", "rendersequence": "the responsiveness rows",
    "bake": "the responsiveness rows", "meshmaps": "the responsiveness rows", "baketex": "the responsiveness rows",
    "import": "the files rows", "export": "the files rows", "deleteasset": "deletes a project file",
    "texdelete": "deletes a project file", "moveasset": "moves a project file", "renameasset": "renames a project file",
    "mkfolder": "makes a project folder",
    "manual": "opens the manual in the system browser", "references": "opens the Learn window on the References lesson (the window rows have it)",
}


def registered_verbs(root):
    registry = root / "src" / "editor" / "editor_commands.cpp"
    source = registry.read_text(encoding="utf-8")
    match = re.search(r"kConsoleCommands\[\] = \{([^}]*)\}", source, re.S)
    if not match:
        raise ValueError(f"Cannot find kConsoleCommands in {registry}")
    return set(re.findall(r'"([^"]+)"', match[1]))


def table(verbs, examples, skips):
    rows = {verb: ("", reason) for verb, reason in skips.items()}
    for line in examples:
        verb = line.split()[0]
        if verb in rows:
            raise ValueError(f"Duplicate sweep entry: {verb}")
        rows[verb] = (line, "")

    errors = []
    for verb in sorted(verbs - rows.keys()):
        errors.append(f"Missing sweep entry for registered verb: {verb}")
    for verb in sorted(rows.keys() - verbs):
        errors.append(f"Stale sweep entry for removed verb: {verb}")
    if errors:
        raise ValueError("\n".join(errors))

    out = ["// GENERATED by tools/sweep_commands.py (round 411): one invocation per console verb for the inventory sweep.",
           "// {verb, the line to run (empty: not run), why not}", "static const SweepCommand kSweepCommands[] = {"]
    for verb in sorted(rows):
        line, reason = rows[verb]
        fields = ", ".join(json.dumps(field, ensure_ascii=False) for field in (verb, line, reason))
        out.append("  {" + fields + "},")
    out.append("};")
    return "\n".join(out) + "\n"


def run(root, examples, skips, check):
    path = root / "stress" / "sweep_commands.inc"
    try:
        verbs = registered_verbs(root)
        expected = table(verbs, examples, skips)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1

    current = path.read_text(encoding="utf-8") if path.exists() else ""
    if current == expected:
        print(f"sweep_commands.inc: current ({len(verbs)} verbs)")
        return 0
    if check:
        print("stress/sweep_commands.inc is out of date: run tools/sweep_commands.py")
        sys.stdout.writelines(difflib.unified_diff(current.splitlines(True), expected.splitlines(True),
                                                   fromfile="committed", tofile="expected", n=0))
        return 1
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write(expected)
    print(f"wrote stress/sweep_commands.inc ({len(verbs)} verbs)")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report stale entries without writing the table")
    sys.exit(run(ROOT, EXAMPLES, SKIP, parser.parse_args().check))
