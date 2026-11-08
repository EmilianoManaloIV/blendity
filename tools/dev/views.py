"""The views' rows for Pass 18's performance rounds: Shinano's Scene view (and Scene + Game) and 50,000 cubes' Scene
view, windowed on the card. Usage: py tools/dev/views.py <exe> <out dir> [label]  (BL_SHINANO: a project holding the
imported Shinano prefab - the user's copy, never committed)"""
import json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import bench_world  # noqa: E402

exe, out = sys.argv[1], sys.argv[2]
label = sys.argv[3] if len(sys.argv) > 3 else "run"
os.makedirs(out, exist_ok=True)
SHINANO = os.environ.get("BL_SHINANO", "C:/bltest399/default_project")
PREFAB = "import Assets/Imported/Shinano/Shinano/Prefab/Shinano.prefab"
rows = {}
for name, project, cmds in (
    ("Shinano, Scene view", SHINANO, [PREFAB]),
    ("Shinano, Scene + Game view", SHINANO, [PREFAB, "window Game"]),
    ("50,000 cubes, Scene view", SHINANO, ["benchscene cubes50k"]),
):
    png = os.path.join(out, "%s_%s.png" % (label, name.replace(" ", "_").replace(",", "").replace("+", "and")))
    r = bench_world.windowed(exe, project, cmds, png)
    rows[name] = r
    card = (r or {}).get("card", {})
    print("%-28s card %s ms  parts %s" % (name, card.get("ms", "-"), (r or {}).get("parts")), flush=True)
with open(os.path.join(out, label + ".json"), "w", encoding="utf-8") as f:
    json.dump(rows, f, indent=1)
