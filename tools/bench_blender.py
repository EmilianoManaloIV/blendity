# SPDX-License-Identifier: GPL-2.0-or-later
"""Blender as the benchmark (task 0178): the scenes and operations Blendity's stress rows time, run in Blender headless,
so docs/PERFORMANCE.md's Pass 13 table has a "Blender 5.2" column measured on the same machine.

    "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b --python tools/bench_blender.py -- --out build/bench/blender.json [--quick] [--gpu 4070]

Task 0199: Blender's own viewport, with its window (not -b), at the size Blendity's windowed check opens:

    "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" --window-geometry 0 0 2560 1369 --python tools/bench_blender.py -- --viewport --out build/bench/blender_viewport.json

Task 0201: every editor feature's twin of Blendity's stress `blender_parity` rows (PERFORMANCE.md's Pass 15), headless:

    "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" -b --python tools/bench_blender.py -- --parity --out build/bench/blender_parity.json

Task 0202: the parity rows that need Blender's window (its viewport and playback), run by tools/bench_ui.py:

    "C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe" --window-geometry 0 0 2560 1369 --python tools/bench_blender.py -- --parity-ui --out build/bench/blender_parity_ui.json

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


# ------------------------------------------------------------------ Blender's own viewport (task 0199)
def clear_scene():
    """The objects, meshes and materials let go - with Blender's UI up, read_factory_settings would reload the window."""
    for ob in list(bpy.data.objects): bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes): bpy.data.meshes.remove(me)
    for ma in list(bpy.data.materials): bpy.data.materials.remove(ma)

def view3d_shading(kind, through_camera):
    for area in bpy.context.window_manager.windows[0].screen.areas:
        if area.type != "VIEW_3D": continue
        sp = area.spaces.active
        sp.shading.type = kind
        sp.overlay.show_overlays = True
        if through_camera and bpy.context.scene.camera: sp.region_3d.view_perspective = "CAMERA"

def view3d_regions():
    window = bpy.context.window_manager.windows[0]
    return [a.spaces.active.region_3d for a in window.screen.areas if a.type == "VIEW_3D"]

def redraw_ms(iterations, step=None):
    """The whole window drawn and swapped once per iteration (wm.redraw_timer DRAW_WIN_SWAP), a frame's mean in ms.
    Task 0202: between iterations the view's distance is nudged by 1e-4 (an orbit's change, too small to see), so
    Workbench and EEVEE draw the scene again each time: with a still view they show their finished picture and
    draw only the overlays. `step(i)`, when given, runs before each draw (a frame of playback)."""
    window = bpy.context.window_manager.windows[0]
    views = view3d_regions()
    def nudge(i):
        for rv3d in views:
            rv3d.view_distance += 1e-4 if i % 2 == 0 else -1e-4
            rv3d.view_camera_zoom += 1e-3 if i % 2 == 0 else -1e-3  # (seen through the camera, the distance does nothing)
    with bpy.context.temp_override(window=window, screen=window.screen):
        for i in range(10):  # (warm: shaders, batches)
            nudge(i)
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=1)
        for _ in range(600):  # (EEVEE compiles its materials in the background: until they are, it draws placeholders)
            if not bpy.app.is_job_running("SHADER_COMPILATION"): break
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=5)
        t0 = time.perf_counter()
        for i in range(iterations):
            if step: step(i)
            nudge(i)
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=1)
        return (time.perf_counter() - t0) * 1000.0 / iterations

def bench_viewport():
    n = 30 if QUICK else 200
    window = bpy.context.window_manager.windows[0]
    size = "%dx%d" % (window.width, window.height)
    global reset
    reset = clear_scene  # (the scene builders start from an empty scene; the window stays)
    clear_scene()
    bpy.ops.mesh.primitive_cube_add()  # (the startup's cube, as Blendity's starter scene has one)
    view3d_shading("SOLID", False)
    row("Viewport: a whole window drawn, a cube (Solid)", redraw_ms(n), "ms",
        "wm.redraw_timer DRAW_WIN_SWAP, %d iterations after 10 warm, the window %s; Blendity's row: Blendity --windowed-check, the canvas idle and the Scene view drawn every frame by the card (the starter scene)" % (n, size))
    gpu_stress_scene()
    view3d_shading("SOLID", True)
    row("Viewport: 400 spheres through the camera (Solid)", redraw_ms(n), "ms",
        "wm.redraw_timer DRAW_WIN_SWAP, %d iterations, the window %s, the stress gpu scene seen through its camera, Solid shading; Blendity's row: --windowed-check --command \"stress 400 Sphere\" --command \"shading solid\"" % (n, size))
    view3d_shading("MATERIAL", True)
    row("Viewport: 400 spheres through the camera (Material Preview)", redraw_ms(n), "ms",
        "wm.redraw_timer DRAW_WIN_SWAP, %d iterations, the window %s, Material Preview (EEVEE); Blendity's row: the same with Shaded shading" % (n, size))

def run_viewport():
    print("[bench_blender] viewport")
    try:
        try:
            bench_viewport()
        except Exception as e:
            row("viewport (failed)", -1, "", "failed: %s" % e)
        os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
        with open(OUT, "w", encoding="utf-8") as f:
            json.dump({"blender": bpy.app.version_string, "quick": QUICK, "gpu": GPU, "rows": rows}, f, indent=1)
        print("[bench_blender] wrote", OUT)
    finally:
        bpy.ops.wm.quit_blender()  # (a windowed Blender left running would hold the bench)
    return None

if arg("--viewport", False) is not False:
    bpy.app.timers.register(run_viewport, first_interval=3.0)  # (once the window is up)

# ------------------------------------------------------------------ Blender parity (task 0201)
# The twins of Blendity's stress `blender_parity` rows, under the same names (tools/bench_table.py joins them). Headless
# Blender draws nothing: an editor row is the operator, its undo step where Blender's editor pushes one, and the
# depsgraph update. Means of three after a warm call unless a row says otherwise.

def resident_mb():
    """This process's resident memory (Windows: the working set; elsewhere /proc), in MB; -1 when unknown."""
    try:
        import ctypes
        from ctypes import wintypes
        class PMC(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                        ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        pmc = PMC()
        pmc.cb = ctypes.sizeof(PMC)
        k32 = ctypes.windll.kernel32
        k32.GetCurrentProcess.restype = wintypes.HANDLE
        k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
        if k32.K32GetProcessMemoryInfo(k32.GetCurrentProcess(), ctypes.byref(pmc), pmc.cb): return pmc.WorkingSetSize / (1024.0 * 1024.0)
    except Exception:
        pass
    try:
        with open("/proc/self/statm") as f: return int(f.read().split()[1]) * 4096 / (1024.0 * 1024.0)
    except Exception:
        return -1.0

def parity_sphere(big):
    """Blendity's parity spheres: 256 x 196 segments (50,176 faces) and 1024 x 490 (501,760)."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=1024 if big else 256, ring_count=490 if big else 196, radius=1.0)
    return bpy.context.active_object

def cube_grid(n):
    """n objects sharing one 1 m cube (Blendity's make_grid_scene: 1.6 m apart in a square)."""
    reset()
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    proto = bpy.context.active_object
    side = math.ceil(math.sqrt(n))
    coll = bpy.context.collection
    for i in range(n):
        ob = proto if i == 0 else bpy.data.objects.new("obj", proto.data)
        if i: coll.objects.link(ob)
        ob.location = at((i % side - side * 0.5) * 1.6, 0.0, (i // side - side * 0.5) * 1.6)
    bpy.context.view_layer.update()
    return proto

def timed_update(fn):
    t0 = time.perf_counter()
    fn()
    bpy.context.view_layer.update()
    return (time.perf_counter() - t0) * 1000.0

def parity_files():
    import shutil
    bpy.context.preferences.filepaths.save_version = 0  # (no .blend1 backup renamed inside the timer)
    tmp = tempfile.mkdtemp(prefix="blender_parity_")
    def save_open(what, build, name):
        build()
        path = os.path.join(tmp, name)
        bpy.ops.wm.save_as_mainfile(filepath=path, compress=False, copy=True)  # (warm)
        save = mean_ms(lambda: bpy.ops.wm.save_as_mainfile(filepath=path, compress=False, copy=True))
        size = os.path.getsize(path)
        row("Save a scene: " + what, save, "ms", "wm.save_as_mainfile, uncompressed (%.1f MB written); Blendity's row: its .scene text written" % (size / 1e6))
        opened = mean_ms(lambda: bpy.ops.wm.open_mainfile(filepath=path, load_ui=False))
        row("Open it: " + what, opened, "ms", "wm.open_mainfile without its UI; Blendity's row: the file read and parsed into a scene, not swapped into an editor")
        os.remove(path)
    save_open("10,000 cubes", lambda: cube_grid(10000), "parity_cubes.blend")
    def big():
        reset()
        parity_sphere(True)
        bpy.context.view_layer.update()
    save_open("a 500k-face mesh", big, "parity_big.blend")
    formats = [
        ("OBJ", ".obj", lambda p: bpy.ops.wm.obj_export(filepath=p), lambda p: bpy.ops.wm.obj_import(filepath=p)),
        ("FBX", ".fbx", lambda p: bpy.ops.export_scene.fbx(filepath=p), lambda p: bpy.ops.import_scene.fbx(filepath=p)),
        ("glTF (GLB)", ".glb", lambda p: bpy.ops.export_scene.gltf(filepath=p, export_format="GLB"), lambda p: bpy.ops.import_scene.gltf(filepath=p)),
        ("STL", ".stl", lambda p: bpy.ops.wm.stl_export(filepath=p), lambda p: bpy.ops.wm.stl_import(filepath=p)),
        ("PLY", ".ply", lambda p: bpy.ops.wm.ply_export(filepath=p), lambda p: bpy.ops.wm.ply_import(filepath=p)),
        ("USD (USDA)", ".usda", lambda p: bpy.ops.wm.usd_export(filepath=p), lambda p: bpy.ops.wm.usd_import(filepath=p)),
    ]
    for label, ext, export, imp in formats:
        path = os.path.join(tmp, "parity_sphere" + ext)
        try:
            reset()
            parity_sphere(False)
            export(path)  # (warm)
            ms = mean_ms(lambda: export(path))
            row("Export the 50k sphere as " + label, ms, "ms", "the exporter's operator at its defaults (%.1f MB written)" % (os.path.getsize(path) / 1e6))
        except Exception as e:
            row("Export the 50k sphere as " + label, -1, "ms", "failed: %s" % e)
            continue
        try:
            total = 0.0
            for _ in range(3):
                reset()
                total += timed_update(lambda: imp(path))
            faces = sum(len(o.data.polygons) for o in bpy.data.objects if o.type == "MESH")
            row("Import the 50k sphere from " + label, total / 3, "ms", "the importer's operator at its defaults + the depsgraph update (%d faces read)" % faces)
        except Exception as e:
            row("Import the 50k sphere from " + label, -1, "ms", "failed: %s" % e)
    shutil.rmtree(tmp, ignore_errors=True)

def parity_objects():
    cube_grid(10000)
    what = "10,000 objects"
    def deselect(): bpy.ops.object.select_all(action="DESELECT")
    total = 0.0
    for _ in range(3):
        deselect()
        total += timed_update(lambda: bpy.ops.object.select_all(action="SELECT"))
    row("Select all, " + what, total / 3, "ms", "object.select_all + the depsgraph update; Blendity's row: selectall and the frame that shows it")
    total = 0.0
    for _ in range(3):
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.ed.undo_push(message="before")
        def dup():
            in_view3d(bpy.ops.object.duplicate, linked=True)
            bpy.ops.ed.undo_push(message="duplicate")
        total += timed_update(dup)
        bpy.ops.ed.undo()
    row("Duplicate " + what, total / 3, "ms", "object.duplicate linked (Alt+D: the copies share the mesh, as Blendity's copy-on-write copies do; Shift+D would copy 10,000 meshes) + its undo step + the depsgraph update; undone outside the timer")
    total = 0.0
    for _ in range(3):
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.ed.undo_push(message="before")
        def delete():
            in_view3d(bpy.ops.object.delete)
            bpy.ops.ed.undo_push(message="delete")
        total += timed_update(delete)
        bpy.ops.ed.undo()
    row("Delete " + what, total / 3, "ms", "object.delete + its undo step + the depsgraph update; undone outside the timer")
    total = 0.0
    for _ in range(3):
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.ed.undo_push(message="before")
        in_view3d(bpy.ops.object.delete)
        bpy.ops.ed.undo_push(message="delete")
        total += timed_update(bpy.ops.ed.undo)
        if len(bpy.data.objects) < 10000: raise RuntimeError("the undo did not bring the objects back")
    row("Undo of the delete, " + what, total / 3, "ms", "ed.undo of the delete (the memfile step read back) + the depsgraph update")

def select_all_mesh(ob, select=True):
    bm = bmesh.from_edit_mesh(ob.data)
    for v in bm.verts: v.select = select
    for e in bm.edges: e.select = select
    for f in bm.faces: f.select = select
    bmesh.update_edit_mesh(ob.data)

def meridian_edge(ob):
    """The edge at the equator, angle 0, going down: a loop cut through it is a ring (Blendity's parity_meridian_edge)."""
    bm = bmesh.from_edit_mesh(ob.data)
    bm.verts.ensure_lookup_table()
    v0 = min(bm.verts, key=lambda v: (v.co - Vector((1.0, 0.0, 0.0))).length)
    e = min((e for e in v0.link_edges), key=lambda e: e.other_vert(v0).co.z if abs(e.other_vert(v0).co.y) < 1e-4 else 9.0)
    return e.index

def parity_edit_mode(big):
    reset()
    ob = parity_sphere(big)
    on = " (500k faces)" if big else " (50k faces)"
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="OBJECT")
    total = 0.0
    for _ in range(3):
        total += timed_update(lambda: bpy.ops.object.mode_set(mode="EDIT"))
        bpy.ops.object.mode_set(mode="OBJECT")
    row("Enter Edit Mode" + on, total / 3, "ms", "mode_set(EDIT) + the depsgraph update (the Mesh made a BMesh); headless Blender builds no draw cache")
    bpy.ops.object.mode_set(mode="EDIT")
    def op_row(name, select, op, method, undone=True):
        total = 0.0
        for _ in range(3):
            select()
            bpy.ops.ed.undo_push(message="before")
            n0 = face_count(ob)
            def run():
                op()
                bpy.ops.ed.undo_push(message=name)
            total += timed_update(run)
            if undone:
                bpy.ops.ed.undo()
                if face_count(ob) != n0: raise RuntimeError("undo did not restore the mesh")
        row(name + on, total / 3, "ms", method + " + its undo step + the depsgraph update; undone outside the timer")
    def sel_none(): select_all_mesh(ob, False)
    total = 0.0
    for _ in range(3):
        sel_none()
        total += timed_update(lambda: in_view3d(bpy.ops.mesh.select_all, action="SELECT"))
    row("Select all" + on, total / 3, "ms", "mesh.select_all + the depsgraph update (no undo step timed: Blendity's selection pushes none)")
    total = 0.0
    for _ in range(3):
        select_faces(ob, (100,))
        total += timed_update(lambda: in_view3d(bpy.ops.mesh.select_linked))
    row("Select linked from one face" + on, total / 3, "ms", "mesh.select_linked from face 100 + the depsgraph update")
    three = lambda: select_faces(ob, (100, 101, 102))
    everything = lambda: select_all_mesh(ob, True)
    op_row("Extrude three faces", three, lambda: in_view3d(bpy.ops.mesh.extrude_region_shrink_fatten, TRANSFORM_OT_shrink_fatten={"value": 0.5}),
           "mesh.extrude_region_shrink_fatten on faces 100-102, 0.5 along their normals")
    op_row("Inset three faces", three, lambda: in_view3d(bpy.ops.mesh.inset, thickness=0.01, use_individual=False), "mesh.inset, region, thickness 0.01")
    if not big:
        op_row("Bevel three faces' edges", three, lambda: in_view3d(bpy.ops.mesh.bevel, offset=0.005, segments=3, affect="EDGES"),
               "mesh.bevel on faces 100-102's edges, offset 0.005, 3 segments")
        def cut():
            idx = meridian_edge(ob)
            in_view3d(bpy.ops.mesh.loopcut_slide, MESH_OT_loopcut={"number_cuts": 1, "edge_index": idx, "object_index": 0},
                      TRANSFORM_OT_edge_slide={"value": 0.0})
        try:
            op_row("Loop cut", sel_none, cut, "mesh.loopcut_slide, 1 cut, through the meridian edge at the equator (a ring of 256 faces)")
        except Exception as e:
            row("Loop cut" + on, -1, "ms", "failed: %s" % e)
        op_row("Subdivide every edge", everything, lambda: in_view3d(bpy.ops.mesh.subdivide, number_cuts=1), "mesh.subdivide, 1 cut, everything selected")
        total = 0.0
        for _ in range(3):
            everything()
            bpy.ops.ed.undo_push(message="before")
            in_view3d(bpy.ops.mesh.subdivide, number_cuts=1)
            bpy.ops.ed.undo_push(message="subdivide")
            total += timed_update(bpy.ops.ed.undo)
        row("Undo of the subdivide" + on, total / 3, "ms", "ed.undo of mesh.subdivide + the depsgraph update")
    op_row("Merge by distance, everything", everything, lambda: in_view3d(bpy.ops.mesh.remove_doubles, threshold=0.001),
           "mesh.remove_doubles, 0.001, everything selected (the rings beside the poles merge)")
    op_row("Triangulate every face", everything, lambda: in_view3d(bpy.ops.mesh.quads_convert_to_tris), "mesh.quads_convert_to_tris, everything selected")
    total = 0.0
    for _ in range(3):
        everything()
        bpy.ops.ed.undo_push(message="before")
        in_view3d(bpy.ops.mesh.quads_convert_to_tris)
        bpy.ops.ed.undo_push(message="triangulate")
        total += timed_update(bpy.ops.ed.undo)
    row("Undo of the triangulate" + on, total / 3, "ms", "ed.undo of mesh.quads_convert_to_tris + the depsgraph update")
    total = 0.0
    for _ in range(3):
        bpy.ops.object.mode_set(mode="EDIT")
        total += timed_update(lambda: bpy.ops.object.mode_set(mode="OBJECT"))
    row("Exit Edit Mode" + on, total / 3, "ms", "mode_set(OBJECT) + the depsgraph update (the BMesh written back to the Mesh)")

def parity_modifiers():
    def evaluate(name, add, method):
        total, faces = 0.0, 0
        for _ in range(3):
            reset()
            ob = parity_sphere(False)
            bpy.context.view_layer.update()
            add(ob)
            def read():
                nonlocal faces
                faces = len(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.polygons)
            total += timed_update(lambda: None) + mean_ms(read, 1)
        row("Modifier: %s on the 50k sphere" % name, total / 3, "ms",
            "%s, the depsgraph update and the evaluated mesh read (a subdivision is built when read); %d faces out" % (method, faces))
    def subsurf(level):
        def add(ob):
            m = ob.modifiers.new("Subdivision", "SUBSURF")
            m.levels = level
            m.use_limit_surface = False  # (Blendity subdivides a step at a time, as Blender's without the limit surface)
        return add
    evaluate("Subdivision level 1", subsurf(1), "Subdivision Surface, viewport level 1, the limit surface off (OpenSubdiv on the CPU; the sphere's poles have 256 edges)")
    evaluate("Subdivision level 2", subsurf(2), "Subdivision Surface, viewport level 2, the limit surface off")
    total, faces = 0.0, 0
    for _ in range(3):
        reset()
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        ob = bpy.context.active_object
        bpy.ops.object.mode_set(mode="EDIT")
        in_view3d(bpy.ops.mesh.subdivide, number_cuts=63)
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.context.view_layer.update()
        subsurf(1)(ob)
        def read():
            nonlocal faces
            faces = len(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.polygons)
        total += timed_update(lambda: None) + mean_ms(read, 1)
    row("Modifier: Subdivision level 1 on a 24,576-quad cube", total / 3, "ms",
        "Subdivision Surface level 1, the limit surface off, on a cube cut 64 x 64 a side; %d faces out" % faces)
    def mirror(ob):
        m = ob.modifiers.new("Mirror", "MIRROR")
        m.use_mirror_merge, m.merge_threshold = True, 0.001
    evaluate("Mirror", mirror, "Mirror on X, merging at 0.001")
    def array(ob):
        ob.modifiers.new("Array", "ARRAY").count = 10
    evaluate("Array x10", array, "Array, 10 copies a mesh-size apart")
    def solidify(ob):
        m = ob.modifiers.new("Solidify", "SOLIDIFY")
        m.thickness, m.use_rim = 0.01, True
    evaluate("Solidify", solidify, "Solidify, 0.01 thick, rim filled")
    def bevel(ob):
        m = ob.modifiers.new("Bevel", "BEVEL")
        m.width, m.segments, m.limit_method = 0.001, 1, "NONE"
    evaluate("Bevel", bevel, "Bevel, width 0.001, 1 segment, every edge (no limit)")
    def decimate(ob):
        m = ob.modifiers.new("Decimate", "DECIMATE")
        m.ratio = 0.5
    evaluate("Decimate 0.5", decimate, "Decimate collapse, ratio 0.5")
    def boolean(ob):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=256, ring_count=196, radius=1.0, location=at(0.5, 0.0, 0.0))
        cutter = bpy.context.active_object
        cutter.hide_set(True)
        m = ob.modifiers.new("Boolean", "BOOLEAN")
        m.operation, m.object = "UNION", cutter
        try: m.solver = "MANIFOLD"
        except TypeError: m.solver = "EXACT"
        bpy.context.view_layer.objects.active = ob
    evaluate("Boolean union with a second 50k sphere", boolean, "Boolean union, the Manifold solver (Blendity's too)")
    reset()
    ob = parity_sphere(False)
    ob.modifiers.new("Subdivision", "SUBSURF").levels = 1
    ob.modifiers.new("Mirror", "MIRROR")
    bpy.context.view_layer.update()
    total = 0.0
    for k in range(3):
        ob.data.vertices[1000].co.z += 0.001
        ob.data.update()
        total += timed_update(lambda: None) + mean_ms(lambda: len(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.polygons), 1)
    row("Modifier stack again after a vertex moved (Subdivision 1 + Mirror)", total / 3, "ms", "one vertex moved, mesh.update, the depsgraph update and the evaluated mesh read")

def parity_uv_anim_physics():
    reset()
    ob = parity_sphere(False)
    for e in ob.data.edges:  # (a seam down the meridian at angle 0, pole to pole: a closed sphere has no disk to unwrap to)
        a, b = (ob.data.vertices[i].co for i in e.vertices)
        e.use_seam = all(abs(p.y) < 1e-4 and p.x >= -1e-4 for p in (a, b))
    bpy.ops.object.mode_set(mode="EDIT")
    select_all_mesh(ob, True)
    in_view3d(bpy.ops.uv.unwrap, method="CONFORMAL", margin=0.02)  # (warm)
    ms = mean_ms(lambda: in_view3d(bpy.ops.uv.unwrap, method="CONFORMAL", margin=0.02))
    bpy.ops.object.mode_set(mode="OBJECT")
    row("Unwrap the 50k sphere", ms, "ms", "uv.unwrap, Conformal (LSCM, Blendity's), a seam down one meridian, margin 0.02")
    # 1,000 objects keyed on frames 1, 30 and 60.
    proto = cube_grid(1000)
    for i, o in enumerate(bpy.data.objects):
        base = o.location.copy()
        for f, dy in ((1, 0.0), (30, float(i % 7)), (60, 0.0)):
            o.location = base + Vector((0.0, 0.0, dy))
            o.keyframe_insert("location", frame=f)
    scene = bpy.context.scene
    frames = [2 + (k % 58) for k in range(30)]
    t0 = time.perf_counter()
    for f in frames: scene.frame_set(f)
    row("A frame of 1,000 keyframed objects", (time.perf_counter() - t0) * 1000.0 / len(frames), "ms",
        "scene.frame_set at a new frame each time (location keys on frames 1, 30, 60), the depsgraph update included")
    # Ten shape keys on the 50k sphere at half weight, one key's value changed and the mesh evaluated again.
    reset()
    ob = parity_sphere(False)
    ob.shape_key_add(name="Basis")
    for k in range(10):
        key = ob.shape_key_add(name="Key %d" % k)
        for i, v in enumerate(ob.data.vertices): key.data[i].co = v.co * (1.0 + 0.01 * (k + 1))
        key.value = 0.5
    bpy.context.view_layer.update()
    keys = ob.data.shape_keys.key_blocks
    total = 0.0
    for k in range(20):
        keys[1].value = 0.5 + 0.001 * (k % 2)
        total += timed_update(lambda: None)
    row("Ten shape keys on the 50k sphere, evaluated", total / 20, "ms", "one key's value changed, the depsgraph update that mixes all ten")
    # 1,000 rigid boxes falling onto a plane.
    reset()
    bpy.ops.mesh.primitive_plane_add(size=200.0)
    ground = bpy.context.active_object
    bpy.ops.rigidbody.world_add()
    scene = bpy.context.scene
    scene.render.fps = 60
    scene.rigidbody_world.substeps_per_frame = 1  # (Jolt steps once a 60 Hz frame)
    scene.rigidbody_world.point_cache.frame_end = 250
    bpy.context.view_layer.objects.active = ground
    bpy.ops.rigidbody.object_add(type="PASSIVE")
    side = math.ceil(1000 ** (1.0 / 3.0))
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    proto = bpy.context.active_object
    boxes = [proto]
    for i in range(1, 1000):
        o = bpy.data.objects.new("Box", proto.data)
        bpy.context.collection.objects.link(o)
        boxes.append(o)
    for i, o in enumerate(boxes):
        o.location = at((i % side) * 1.1 - side * 0.55, 1.0 + (i // (side * side)) * 1.1, ((i // side) % side) * 1.1 - side * 0.55)
    bpy.ops.object.select_all(action="DESELECT")
    for o in boxes: o.select_set(True)
    bpy.context.view_layer.objects.active = proto
    bpy.ops.rigidbody.objects_add(type="ACTIVE")
    scene.frame_set(1)
    t0 = time.perf_counter()
    for f in range(2, 32): scene.frame_set(f)
    row("1,000 rigid boxes falling: a frame", (time.perf_counter() - t0) * 1000.0 / 30, "ms",
        "scene.frame_set through the first 30 frames at 60 fps (Bullet, %d substep a frame, %d solver iterations), mean" %
        (scene.rigidbody_world.substeps_per_frame, scene.rigidbody_world.solver_iterations))

def stress_spheres(count):
    """Blendity's `stress 400 Sphere`: 24 x 16 spheres of radius 0.5 in a cube-root grid 1.5 m apart, 8 colours, the
    camera 20 m back and 6 m up looking down 3 degrees with a 60 degree vertical field of view, a sun."""
    reset()
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=0.5)
    proto = bpy.context.active_object
    mats = []
    for i in range(8):
        m = bpy.data.materials.new("Stress %d" % i)
        m.diffuse_color = (0.35 + 0.08 * i, 0.45 + 0.05 * ((i * 3) % 8), 0.9 - 0.07 * i, 1.0)
        if m.node_tree: m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = m.diffuse_color
        mats.append(m)
    side = math.ceil(count ** (1.0 / 3.0))
    seed = 1234
    for i in range(count):
        ob = proto if i == 0 else bpy.data.objects.new("Sphere", proto.data)
        if i: bpy.context.collection.objects.link(ob)
        x, y, z = i % side, (i // side) % side, i // (side * side)
        ob.location = at((x - side * 0.5) * 1.5, y * 1.5 + 0.5, (z - side * 0.5) * 1.5)
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        if not ob.material_slots: ob.data.materials.append(None)
        ob.material_slots[0].link = "OBJECT"
        ob.material_slots[0].material = mats[(seed >> 24) % 8]
    sun_along((-0.4, -1.0, 0.3))
    camera_at((0, 6, -20), (0, 5, -1), 60.0)

def parity_render():
    stress_spheres(400)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 1920, 1080, 100
    scene.render.filepath = os.path.join(tempfile.gettempdir(), "bench_blender_f12.png")
    bpy.ops.render.render()  # (warm: shaders compiled)
    ms = mean_ms(lambda: bpy.ops.render.render())
    row("F12 at 1920x1080, 400 spheres (raster)", ms, "ms", "render.render with EEVEE at its default %d samples, Blendity's stress 400 Sphere built alike (a sun for its starter light)" % scene.eevee.taa_render_samples)

def parity_memory():
    """Run first, as Blendity's: the resident set grows only when the heap does."""
    reset()
    before = resident_mb()
    cube_grid(10000)
    after = resident_mb()
    row("Memory: 10,000 cubes", after - before if before >= 0 else -1, "MB", "the resident set's growth when the scene is made (one shared cube mesh) and evaluated")
    reset()
    before = resident_mb()
    parity_sphere(True)
    bpy.context.view_layer.update()
    after = resident_mb()
    row("Memory: a 500k-face mesh", after - before if before >= 0 else -1, "MB", "the resident set's growth for the mesh and its evaluated copy (headless: no draw cache)")

def run_parity():
    for name, fn in (("memory", parity_memory), ("files", parity_files), ("objects", parity_objects), ("edit mode 50k", lambda: parity_edit_mode(False)),
                     ("edit mode 500k", lambda: parity_edit_mode(True)), ("modifiers", parity_modifiers), ("uv, animation, physics", parity_uv_anim_physics),
                     ("render", parity_render)):
        print("[bench_blender] parity:", name)
        try:
            fn()
        except Exception as e:
            row("parity %s (failed)" % name, -1, "", "failed: %s" % e)
    os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"blender": bpy.app.version_string, "quick": QUICK, "gpu": GPU, "rows": rows}, f, indent=1)
    print("[bench_blender] wrote", OUT)


# ------------------------------------------------------------------ Blender parity, with its window (task 0202)
# Blender's viewport timed on the scenes Blendity's `benchscene` command builds: the whole window drawn and swapped
# (wm.redraw_timer DRAW_WIN_SWAP), as Blendity's --windowed-check draws and presents its window with the views drawn
# again each frame; playback as ANIM_STEP (a frame stepped and the window drawn). Run with the window up (not -b).

def bench_scene(name):
    """Blendity's `benchscene <name>` built alike, in an empty scene, everything it made selected for the view to
    frame (the caller then leaves only a sphere selected, as Blendity does)."""
    made = []
    if name in ("sphere50k", "sphere500k"):
        made.append(parity_sphere(name == "sphere500k"))
    elif name == "million":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=256, ring_count=196, radius=1.0)
        proto = bpy.context.active_object
        for i in range(10):
            ob = proto if i == 0 else bpy.data.objects.new("Sphere %d" % i, proto.data)
            if i: bpy.context.collection.objects.link(ob)
            ob.location = at((i - 4.5) * 2.5, 1.0, 0.0)
            made.append(ob)
    elif name == "cubes10k":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        proto = bpy.context.active_object
        for i in range(10000):
            ob = proto if i == 0 else bpy.data.objects.new("Cube %d" % i, proto.data)
            if i: bpy.context.collection.objects.link(ob)
            ob.location = at((i % 100 - 50) * 1.6, 0.0, (i // 100 - 50) * 1.6)
            made.append(ob)
    elif name == "keyed1k":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        proto = bpy.context.active_object
        for i in range(1000):
            ob = proto if i == 0 else bpy.data.objects.new("Box %d" % i, proto.data)
            if i: bpy.context.collection.objects.link(ob)
            base = Vector(at((i % 32 - 16) * 1.6, 0.0, (i // 32 - 16) * 1.6))
            for f, dy in ((1, 0.0), (30, float(i % 7)), (60, 0.0)):
                ob.location = base + Vector((0.0, 0.0, dy))
                ob.keyframe_insert("location", frame=f)
            made.append(ob)
        scene = bpy.context.scene
        scene.frame_start, scene.frame_end = 1, 60
        scene.frame_set(1)
    bpy.ops.object.select_all(action="DESELECT")
    for ob in made: ob.select_set(True)
    bpy.context.view_layer.objects.active = made[-1]
    bpy.context.view_layer.update()
    return made

def frame_selected():
    window = bpy.context.window_manager.windows[0]
    for area in window.screen.areas:
        if area.type != "VIEW_3D": continue
        region = next(r for r in area.regions if r.type == "WINDOW")
        with bpy.context.temp_override(window=window, screen=window.screen, area=area, region=region):
            bpy.ops.view3d.view_selected()

def redraw_anim_ms(iterations):
    """Playback: each iteration sets the next frame (1-60, the keys' evaluation) and draws the window."""
    scene = bpy.context.scene
    return redraw_ms(iterations, lambda i: scene.frame_set(1 + (i + 1) % 60))

def bench_parity_ui():
    n = 30 if QUICK else 200
    window = bpy.context.window_manager.windows[0]
    size = "%dx%d" % (window.width, window.height)
    global reset
    reset = clear_scene  # (with the UI up, read_factory_settings would reload the window)
    rows_spec = [
        ("Window: 50k-face sphere, Solid", "sphere50k", "SOLID", False),
        ("Window: 500k-face sphere, Solid", "sphere500k", "SOLID", False),
        ("Window: a million triangles, Solid", "million", "SOLID", False),
        ("Window: a million triangles, Material Preview / Shaded", "million", "MATERIAL", False),
        ("Window: 10,000 cubes, Solid", "cubes10k", "SOLID", False),
        ("Window: Edit Mode on the 50k sphere", "sphere50k", "SOLID", True),
        ("Window: Edit Mode on the 500k sphere", "sphere500k", "SOLID", True),
    ]
    def settle(scene_name, edit):
        made = bench_scene(scene_name)
        frame_selected()
        if scene_name not in ("sphere50k", "sphere500k"): bpy.ops.object.select_all(action="DESELECT")
        if scene_name == "keyed1k": made[0].select_set(True)  # (one box, as Blendity's Animation window needs)
        if edit:
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_all(action="DESELECT")  # (Blendity's Edit Mode starts with nothing selected)
        return made
    for label, scene_name, shading, edit in rows_spec:
        try:
            clear_scene()
            settle(scene_name, edit)
            view3d_shading(shading, False)
            row(label, redraw_ms(n), "ms",
                "wm.redraw_timer DRAW_WIN_SWAP once a frame, the view nudged 1e-4 between (so the scene is drawn again, not the finished picture shown), "
                "%d frames after 10 warm (and EEVEE's shaders compiled), the window %s" % (n, size))
        except Exception as e:
            row(label, -1, "ms", "failed: %s" % e)
        finally:
            if bpy.context.object and bpy.context.object.mode != "OBJECT": bpy.ops.object.mode_set(mode="OBJECT")
    try:
        clear_scene()
        settle("keyed1k", False)
        view3d_shading("SOLID", False)
        row("Window: playing 1,000 keyframed objects", redraw_anim_ms(n), "ms",
            "each frame scene.frame_set (the next of 60) and wm.redraw_timer DRAW_WIN_SWAP once, the view nudged 1e-4, %d frames, the window %s" % (n, size))
    except Exception as e:
        row("Window: playing 1,000 keyframed objects", -1, "ms", "failed: %s" % e)

def run_parity_ui():
    print("[bench_blender] parity, with the window")
    try:
        try:
            bench_parity_ui()
        except Exception as e:
            row("parity ui (failed)", -1, "", "failed: %s" % e)
        os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
        with open(OUT, "w", encoding="utf-8") as f:
            json.dump({"blender": bpy.app.version_string, "quick": QUICK, "gpu": GPU, "rows": rows}, f, indent=1)
        print("[bench_blender] wrote", OUT)
    finally:
        bpy.ops.wm.quit_blender()
    return None

if arg("--parity-ui", False) is not False:
    bpy.app.timers.register(run_parity_ui, first_interval=3.0)  # (once the window is up)


def run_headless():
    for name, fn in (("edit mode", bench_edit_mode), ("uv", bench_uv), ("export", bench_export), ("cycles", bench_cycles), ("bake", bench_bake)):
        print("[bench_blender]", name)
        try:
            fn()
        except Exception as e:
            row("%s (failed)" % name, -1, "", "failed: %s" % e)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"blender": bpy.app.version_string, "quick": QUICK, "gpu": GPU, "rows": rows}, f, indent=1)
    print("[bench_blender] wrote", OUT)

if arg("--parity", False) is not False:
    run_parity()
elif arg("--viewport", False) is False and arg("--parity-ui", False) is False:
    run_headless()
