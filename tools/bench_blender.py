# SPDX-License-Identifier: GPL-2.0-or-later
"""Blender as the benchmark (task 0178): the scenes and operations Blendity's stress rows time, run in Blender headless,
so docs/PERFORMANCE.md's Pass 13 table has a "Blender 5.2" column measured on the same machine.

    "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b --python tools/bench_blender.py -- --out build/bench/blender.json [--quick] [--gpu 4070]

Each row says what Blender does beside what Blendity's row does (the method per row is in docs/tasks/0178). Times
are the mean of three runs of perf_counter around the call (Blendity's editor rows are means of three frames too);
the Cycles rows fit a line through three sample counts so a render's fixed cost drops out. Blendity is left-handed
and Y-up, Blender right-handed and Z-up: a Blendity point (x, y, z) is placed at (x, z, y) here (a mirror image, which
costs the same). Every picture-making row checks its camera sees the scene (a coverage fraction in the method).
"""
import bpy, bmesh, json, math, os, sys, time, tempfile
from mathutils import Vector

def arg(name, default=None):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name not in argv: return default
    i = argv.index(name)
    if i + 1 < len(argv) and not argv[i + 1].startswith("--"): return argv[i + 1]
    return True

QUICK = arg("--quick", False) is not False
OUT = arg("--out", None) or os.path.join(tempfile.gettempdir(), "bench_blender.json")
GPU = arg("--gpu", "4070")  # the one card the Blendity rows use (a substring of its name)
rows = []

def mean_ms(fn, repeats=3):
    total = 0.0
    for _ in range(repeats):
        t0 = time.perf_counter()
        fn()
        total += (time.perf_counter() - t0) * 1000.0
    return total / repeats

def row(name, value, unit, method):
    rows.append({"row": name, "blender": value, "unit": unit, "method": method})
    print(f"  {name}: {value:.2f} {unit}" if value >= 0 else f"  {name}: failed ({method})")

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def at(x, y, z):
    """A Blendity (left-handed, Y-up) point in Blender's axes."""
    return (x, z, y)

def view3d_override():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type != "VIEW_3D": continue
            region = next(r for r in area.regions if r.type == "WINDOW")
            return {"window": window, "screen": window.screen, "area": area, "region": region}
    return None

def in_view3d(op, **kw):
    """Runs an operator that wants a 3D view (headless Blender has one window with a default screen)."""
    ov = view3d_override()
    if ov is None: return op(**kw)
    with bpy.context.temp_override(**ov):
        return op(**kw)

def sphere_50k():
    """Blendity's benchmark mesh: a UV sphere of 256 x 196 segments (50,176 faces)."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=256, ring_count=196, radius=1.0)
    return bpy.context.active_object

def camera_at(pos, target, fov_deg_vertical):
    bpy.ops.object.camera_add(location=at(*pos))
    cam = bpy.context.active_object
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(fov_deg_vertical)
    bpy.ops.object.empty_add(location=at(*target))
    aim = bpy.context.active_object
    c = cam.constraints.new("TRACK_TO")
    c.target, c.track_axis, c.up_axis = aim, "TRACK_NEGATIVE_Z", "UP_Y"
    bpy.context.scene.camera = cam
    return cam

def sun_along(direction):
    """A sun shining along a Blendity direction (Blendity's RenderLight.direction points where the light goes)."""
    bpy.ops.object.light_add(type="SUN", location=(0, 0, 10))
    sun = bpy.context.active_object
    sun.data.energy = 3.0
    sun.rotation_euler = Vector(at(*direction)).to_track_quat("-Z", "Y").to_euler()
    return sun

def use_one_gpu(device_type):
    """Cycles on the one card the Blendity rows use (every other device off); False when it isn't there."""
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = device_type
    prefs.get_devices()
    found = False
    for d in prefs.devices:
        d.use = d.type == device_type and GPU in d.name and not found
        found = found or d.use
    bpy.context.scene.cycles.device = "GPU"
    return found

def use_cpu():
    bpy.context.scene.cycles.device = "CPU"

def coverage():
    """The fraction of the current render's pixels that aren't the background: a camera that sees nothing gives 0."""
    path = os.path.join(tempfile.gettempdir(), "bench_blender_cover.png")
    scene = bpy.context.scene
    scene.render.filepath = path
    scene.render.film_transparent = True  # (the sky is bright: only geometry writes alpha)
    scene.render.image_settings.color_mode = "RGBA"
    bpy.ops.render.render(write_still=True)
    scene.render.film_transparent = False
    img = bpy.data.images.load(path)
    px = img.pixels[:]
    n = len(px) // 4
    lit = sum(1 for i in range(0, n, 97) if px[i * 4 + 3] > 0.5)
    bpy.data.images.remove(img)
    try: os.remove(path)
    except OSError: pass
    return lit / max(1, n // 97)

# ------------------------------------------------------------------ Edit Mode on a 50k-face mesh
def select_faces(ob, indices):
    bm = bmesh.from_edit_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    for f in bm.faces: f.select = False
    for i in indices: bm.faces[i].select = True
    bm.select_flush(True)
    bmesh.update_edit_mesh(ob.data)

def face_count(ob):
    return len(bmesh.from_edit_mesh(ob.data).faces)

def bench_enter_edit_mode(ob):
    """mode_set(EDIT) and the depsgraph update, the exit outside the timer (Mesh -> BMesh is the entry's work)."""
    bpy.ops.object.mode_set(mode="OBJECT")
    def enter():
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.context.view_layer.update()
    total = 0.0
    for _ in range(3):
        t0 = time.perf_counter()
        enter()
        total += (time.perf_counter() - t0) * 1000.0
        bpy.ops.object.mode_set(mode="OBJECT")
    row("Entering Edit Mode on the 50k-face mesh", total / 3, "ms",
        "mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after")

def bench_extrude(ob):
    """The extrude operator on faces 100-102 (as Blendity's row), with its undo step; undone outside the timer."""
    bpy.ops.object.mode_set(mode="EDIT")
    def extrude():
        in_view3d(bpy.ops.mesh.extrude_region_move, TRANSFORM_OT_translate={"value": (0.0, 0.0, 0.1)})
        bpy.ops.ed.undo_push(message="extrude")  # (headless Blender's operators push no step of their own; in the editor the operator does, as Blendity's editop does)
        bpy.context.view_layer.update()
    total = 0.0
    for _ in range(3):
        select_faces(ob, (100, 101, 102))
        bpy.ops.ed.undo_push(message="before")
        n0 = face_count(ob)
        t0 = time.perf_counter()
        extrude()
        total += (time.perf_counter() - t0) * 1000.0
        if face_count(ob) <= n0: raise RuntimeError("the extrude added no faces")
        bpy.ops.ed.undo()
        if face_count(ob) != n0: raise RuntimeError("undo did not restore the mesh")
    row("An operator: extrude three faces", total / 3, "ms",
        "bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after")

def bench_select_more(ob):
    def select_more():
        in_view3d(bpy.ops.mesh.select_more)
        bpy.context.view_layer.update()
    total = 0.0
    for _ in range(3):
        select_faces(ob, (100,))
        t0 = time.perf_counter()
        select_more()
        total += (time.perf_counter() - t0) * 1000.0
    row("A selection operator (Select More)", total / 3, "ms",
        "bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after")

def bench_undo(ob):
    """ed.undo of the extrude alone (Blender restores the BMesh from the step's copy); the redo outside the timer."""
    total = 0.0
    for _ in range(3):
        select_faces(ob, (100, 101, 102))
        bpy.ops.ed.undo_push(message="before")
        n0 = face_count(ob)
        in_view3d(bpy.ops.mesh.extrude_region_move, TRANSFORM_OT_translate={"value": (0.0, 0.0, 0.1)})
        bpy.ops.ed.undo_push(message="extrude")
        bpy.context.view_layer.update()
        if face_count(ob) <= n0: raise RuntimeError("the extrude added no faces")
        t0 = time.perf_counter()
        bpy.ops.ed.undo()
        bpy.context.view_layer.update()
        total += (time.perf_counter() - t0) * 1000.0
        if face_count(ob) != n0: raise RuntimeError("undo did not restore the mesh")
    row("Undo after it", total / 3, "ms",
        "ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted")

def bench_edit_mode():
    reset()
    ob = sphere_50k()
    bpy.context.view_layer.objects.active = ob
    bench_enter_edit_mode(ob)
    bench_extrude(ob)
    bench_select_more(ob)
    bench_undo(ob)
    bpy.ops.object.mode_set(mode="OBJECT")

# ------------------------------------------------------------------ Cycles: the stress `gpu` scene at its size
def checker_material(name, scale):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    tex = m.node_tree.nodes.new("ShaderNodeTexChecker")
    tex.inputs["Scale"].default_value = scale
    m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m

def uv_grid_material(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    img = bpy.data.images.new(name + "_grid", 256, 256)
    img.generated_type = "UV_GRID"
    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = img
    m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return m

def glass_material(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Transmission Weight"].default_value = 1.0
    bsdf.inputs["Roughness"].default_value = 0.0
    return m

def gpu_stress_scene():
    """Blendity's stress gpu scene: a 40 m checker floor, 400 UV spheres (32 x 16) 1.3 m apart with a UV grid, every
    7th glass, a sun, the camera 16 m back and 6 m up with a 55 degree vertical field of view."""
    reset()
    bpy.ops.mesh.primitive_plane_add(size=40)
    bpy.context.active_object.data.materials.append(checker_material("floor", 40.0))
    ball, glass = uv_grid_material("ball"), glass_material("glass")
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.5)
    proto = bpy.context.active_object
    for i in range(400):
        ob = proto if i == 0 else proto.copy()
        if i: bpy.context.collection.objects.link(ob)
        ob.location = at((i % 20 - 9.5) * 1.3, 0.5, (i // 20 - 9.5) * 1.3)
        ob.data = proto.data if i % 7 else proto.data.copy()
        if i % 7 == 0:
            ob.data.materials.clear()
            ob.data.materials.append(glass)
    proto.data.materials.append(ball)
    sun_along((-0.4, -1.0, 0.3))
    world = bpy.data.worlds.new("sky")
    world.color = (0.45, 0.52, 0.62)  # (Blendity's default gradient sky's zenith; its gradient has no Blender twin)
    bpy.context.scene.world = world
    camera_at((0, 6, -16), (0, 0, 0), 55.0)

def samples_per_second(render, counts):
    """The slope of render time over the sample count (the fixed cost of a render drops out); -1 when the fit isn't
    a rising line, with the residual reported."""
    times = [mean_ms(lambda n=n: render(n), 2) for n in counts]
    mean_n, mean_t = sum(counts) / len(counts), sum(times) / len(times)
    slope = sum((n - mean_n) * (t - mean_t) for n, t in zip(counts, times)) / sum((n - mean_n) ** 2 for n in counts)
    fixed = mean_t - slope * mean_n
    residual = max(abs(t - (fixed + slope * n)) for n, t in zip(counts, times))
    if slope <= 0: return -1.0, fixed, residual
    return 1000.0 / slope, fixed, residual

def bench_cycles():
    gpu_stress_scene()
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 1280, 720, 100
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.max_bounces = 4
    scene.cycles.samples = 4
    cover = coverage()
    for device_type, label in (("OPTIX", "OptiX"), ("CUDA", "CUDA"), ("CPU", "CPU")):
        try:
            if device_type == "CPU": use_cpu()
            elif not use_one_gpu(device_type):
                row("Cycles 1280x720, %s: samples/s" % label, -1, "samples/s", "no %s device whose name holds '%s'" % (device_type, GPU))
                continue
            def render(n):
                scene.cycles.samples = n
                bpy.ops.render.render(write_still=False)
            render(8)  # (kernels load, the BVH builds)
            counts = [16, 32, 64] if QUICK else [32, 64, 128]
            rate, fixed, residual = samples_per_second(render, counts)
            bvh = "Cycles' BVH2 in CUDA, a software BVH like Blendity's compute row" if device_type == "CUDA" else "the card's ray-tracing hardware, like Blendity's ray-query row" if device_type == "OPTIX" else "Cycles' CPU kernels, like Blendity's CPU row (Embree)"
            row("Cycles 1280x720, %s: samples/s" % label, rate, "samples/s",
                "the stress gpu scene at 1280x720 (%.0f %% of the frame covered), bpy.ops.render.render with %d, %d and %d samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, %.0f ms, drops out as Blendity's row leaves out its upload; worst fit residual %.0f ms); %s; %s. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky" % (100 * cover, counts[0], counts[1], counts[2], fixed, residual, bvh, "the CPU, no card" if device_type == "CPU" else "one card: the " + GPU))
        except Exception as e:
            row("Cycles 1280x720, %s: samples/s" % label, -1, "samples/s", "failed: %s" % e)

# ------------------------------------------------------------------ Smart UV Project + Pack Islands on the sphere
def bench_uv():
    reset()
    sphere_50k()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    in_view3d(bpy.ops.uv.smart_project, angle_limit=math.radians(66.0), island_margin=0.0)  # (warm: the operator's first call)
    row("Smart UV Project, 50k faces", mean_ms(lambda: in_view3d(bpy.ops.uv.smart_project, angle_limit=math.radians(66.0), island_margin=0.0)), "ms",
        "bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call")
    row("Pack Islands, 50k faces", mean_ms(lambda: in_view3d(bpy.ops.uv.pack_islands, margin=0.001, shape_method="AABB", rotate=True)), "ms",
        "bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3")
    bpy.ops.object.mode_set(mode="OBJECT")

# ------------------------------------------------------------------ a lightmap bake: the stress bake's 100-object scene
def uv_area(ob):
    """The area in UV space (0-1 squared) of a mesh's faces: the share of the atlas its charts cover."""
    uv = ob.data.uv_layers.active.data
    total = 0.0
    for poly in ob.data.polygons:
        pts = [uv[i].uv for i in poly.loop_indices]
        total += 0.5 * abs(sum(pts[i].x * pts[(i + 1) % len(pts)].y - pts[(i + 1) % len(pts)].x * pts[i].y for i in range(len(pts))))
    return total

def bench_bake():
    reset()
    scene = bpy.context.scene
    bpy.ops.mesh.primitive_plane_add(size=30)
    floor = bpy.context.active_object
    bpy.ops.mesh.primitive_cube_add(size=1)
    cube = bpy.context.active_object
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.5)
    sphere = bpy.context.active_object
    objects = [floor]
    for i in range(100):
        proto = sphere if i % 2 else cube
        ob = proto if i < 2 else proto.copy()
        if i >= 2:
            ob.data = proto.data.copy()  # (its own mesh data: lightmap_pack packs unique meshes, so shared data would give every sphere the same texels)
            bpy.context.collection.objects.link(ob)
        ob.location = at((i % 10 - 5.0) * 2.5, 0.5, (i // 10 - 5.0) * 2.5)
        objects.append(ob)
    sun_along((-0.4, -1.0, 0.3))
    world = bpy.data.worlds.new("sky")
    world.color = (0.45, 0.52, 0.62)  # (a sky, as Blendity's bake scene has; its gradient has no Blender twin)
    scene.world = world
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.max_bounces = 2
    use_cpu()
    img = bpy.data.images.new("bake", 1024, 1024, float_buffer=True)
    mat = bpy.data.materials.new("white")
    mat.use_nodes = True
    node = mat.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = img
    mat.node_tree.nodes.active = node
    for ob in objects:
        if not ob.data.materials: ob.data.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    for ob in objects: ob.select_set(True)
    bpy.context.view_layer.objects.active = floor
    bpy.ops.object.mode_set(mode="EDIT")  # (every selected mesh in Edit Mode together: one atlas for all of them)
    bpy.ops.mesh.select_all(action="SELECT")
    in_view3d(bpy.ops.uv.lightmap_pack, PREF_CONTEXT="ALL_FACES", PREF_PACK_IN_ONE=True, PREF_NEW_UVLAYER=False, PREF_BOX_DIV=12, PREF_MARGIN_DIV=0.1)
    bpy.ops.object.mode_set(mode="OBJECT")
    texels = sum(uv_area(ob) for ob in objects) * 1024.0 * 1024.0  # (the texels the charts cover, as Blendity counts its charts' texels)
    def bake():
        bpy.ops.object.bake(type="DIFFUSE", pass_filter={"DIRECT", "INDIRECT"}, margin=2)
    try:
        scene.cycles.samples = 1
        bake()  # (warm: the kernels and the scene's sync)
        scene.cycles.samples = 64
        seconds = mean_ms(bake, 1) / 1000.0
        row("Lightmap bake: texels a second", texels / seconds, "texels/s",
            "bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: %.1f s for the %s texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer" % (seconds, "{:,.0f}".format(texels)))
    except Exception as e:
        row("Lightmap bake: texels a second", -1, "texels/s", "bake failed: %s" % e)

# ------------------------------------------------------------------ GLB export of the stress scene
def bench_export():
    reset()
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=0.5)  # (subdivisions=4 is Blendity's ico_sphere(3): 1,280 faces)
    bpy.ops.object.shade_smooth()  # (shared vertices in the file, as Blendity writes them; flat shading would split every face's)
    proto = bpy.context.active_object
    for i in range(256):
        ob = proto if i == 0 else proto.copy()
        if i:
            ob.data = proto.data.copy()  # (its own mesh data: Blendity's export writes every object's mesh, so Blender must too)
            bpy.context.collection.objects.link(ob)
        ob.location = at((i % 16 - 8) * 1.6, 0, (i // 16 - 8) * 1.6)
    path = os.path.join(tempfile.gettempdir(), "bench_blender.glb")
    def export():
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_apply=True)
    export()  # (warm: the add-on's first call imports its modules)
    ms = mean_ms(export)
    size = os.path.getsize(path) if os.path.exists(path) else 0
    row("GLB export, 256 icospheres (327k triangles)", ms, "ms",
        "bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (%.1f MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call" % (size / 1e6))
    try: os.remove(path)
    except OSError: pass

for name, fn in (("edit mode", bench_edit_mode), ("uv", bench_uv), ("export", bench_export), ("cycles", bench_cycles), ("bake", bench_bake)):
    print("[bench_blender]", name)
    try:
        fn()
    except Exception as e:
        row("%s (failed)" % name, -1, "", "failed: %s" % e)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump({"blender": bpy.app.version_string, "quick": QUICK, "gpu": GPU, "rows": rows}, f, indent=1)
print("[bench_blender] wrote", OUT)
