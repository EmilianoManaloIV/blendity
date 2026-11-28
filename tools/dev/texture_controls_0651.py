"""Replay the Textures and Paint pointer fixtures on a verified Windows package."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import traceback
import zlib

from gesture_sweep_0640 import ENV_DIRECTORIES, click, png_rows, write_json


PACKAGE_SHA = '0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad'
SURFACE = 'PointerSurface'
SIZE = '1600x900'
TEXTURES = 'layout L(14:0)'
TOOLS = '@paint-tools'
TEXTURE_KEY = 'panel/texture_window.cpp/draw_textures_window/'
PAINT_KEY = 'panel/panels.cpp/draw_paint_tools/'
MAPS = ('Albedo', 'Normal', 'Roughness', 'Metallic', 'Height', 'Emission', 'AO')


LAYER_SETTINGS = {
    'layer': {
        'Blend': 'L1_Blend',
        'Opacity': 'L1_Opacity',
        'Use Mask': 'L1_Use_Mask',
        'Writes Albedo': 'L1_Writes_Albedo',
        'Writes Emission': 'L1_Writes_Emission',
        'Writes Height': 'L1_Writes_Height',
        'Writes Metallic': 'L1_Writes_Metallic',
        'Writes Roughness': 'L1_Writes_Roughness',
    },
    'math': {'Operation': 'L2_Operation', 'Plane': 'L2_Plane', 'With': 'L2_With'},
}


def run(app, work, name, commands, size=SIZE):
    folder = work / name
    folder.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith('BLENDITY_')}
    env.update(BLENDITY_VIEWPORT_DEVICE='cpu', BLENDITY_GI_DEVICE='cpu', BLENDITY_THREADS='4', BLENDITY_LIVELINK_AUTOSTART='0')
    for key, subdir in ENV_DIRECTORIES.items():
        directory = folder / subdir
        directory.mkdir(exist_ok=True)
        env[key] = str(directory)
    paint_ui = folder / 'paint-tools.blendity-ui'
    if TOOLS in commands:
        paint_ui.write_text('ui_scale=1\nlayout=L(12:0)\nworkspace=Replay\nfocus=8\nalltools=0\n', encoding='ascii')
    argv = [str(app), '--headless-screenshot', str(folder / 'screen.png'), '--size', size, '--frames', '3']
    for command in commands:
        if command == TOOLS:
            command = 'ui load ' + paint_ui.as_posix()
        argv += ('--cmd', command)
    write_json(folder / 'invocation.json', argv)
    with (folder / 'editor.log').open('wb') as log:
        subprocess.run(argv, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
    if not (folder / 'screen.png').is_file():
        raise ValueError(f'Editor did not finish {folder}')
    return folder


def starter(layout=TEXTURES):
    return [layout, 'ui scale 1', 'select Cube']


def surface(layout=TEXTURES):
    return starter(layout) + [f'surface new {SURFACE}', f'settex {SURFACE} Width 256', f'settex {SURFACE} Height 256',
                              'surface set L0 Roughness 0.13', 'surface add Checker', 'surface set L1 Opacity 0.67',
                              'surface set L1 Color 0.7,0.08,0.2', 'surface set L1 Color 2 0.1,0.4,0.7',
                              'perf wait', 'perf frames 3']


def calibrate(app, work):
    run(app, work, 'textures', surface())
    run(app, work, 'paint-tools', surface(TOOLS) + ['paint on', 'perf frames 3'], '1600x1100')
    run(app, work, 'paint-canvas', surface() + ['paint on', 'paint 2d on', 'perf wait'], '1600x1100')
    run(app, work, 'filter', surface() + ['surface add Math', 'perf wait', 'perf frames 3'])


def inspect_controls(app, work, points):
    for name in ('new', 'size', 'output', 'smart'):
        base = starter() + ['perf frames 3'] if name == 'new' else surface()
        run(app, work, name, [*base, click(points[name])])
    for name, base in (('layer', surface()), ('math', surface() + ['surface add Math', 'surface set L2 Plane 4', 'perf wait'])):
        if name + '_prepare' in points:
            run(app, work, name, [*base, *(click(point) for point in points[name + '_prepare'])],
                '1600x1100' if name == 'math' else SIZE)


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def ptex(folder, name=SURFACE):
    return folder / 'project/Assets/Textures' / f'{name}.ptex'


def properties(path):
    return dict(line.split(maxsplit=1) for line in path.read_text(encoding='utf-8').splitlines()[1:] if line.strip())


def save():
    return ['perf frames 3', 'perf wait', 'save Assets/Scenes/Replay.scene']


def crop(folder, rect):
    x, y, w, h = rect
    rows = png_rows(folder / 'screen.png', y + h)[1]  # image decoder reused from the earlier exact-key replay
    return tuple(row[x * 3:(x + w) * 3] for row in rows[y:y + h])


def map_hashes(folder):
    files = {name: folder / 'project/Exports/Paint' / f'{SURFACE}_{name}.png' for name in MAPS}
    return {name: sha(path) for name, path in files.items()}


def drag(point, dx):
    x, y = point
    return f'perf drag at {x} {y} {x + dx} {y} frames 4'


def texture_cases(app, work, points, rows, failures):
    def sample(tag, gestures=(), commands=(), base=None):
        return run(app, work, tag, [*(surface() if base is None else base), *(click(points[key]) for key in gestures),
                                    *commands, *save()])

    def check(suffix, tag, result, *controls):
        rows.append({'key': (TEXTURE_KEY + suffix if not suffix.startswith('menu/') else suffix),
                     'case': tag.name, 'result': result, 'controls': [case.name for case in controls]})

    normal = sample('texture-control', ['texture_off_target'])
    before = properties(ptex(normal))
    inside = points['preview_crop']
    normal_view = crop(normal, inside)

    def tile():
        tiled = sample('texture-tiled', ['tile'])
        restored = sample('texture-tile-reverted', ['tile', 'tile'])
        require(properties(ptex(tiled)) == properties(ptex(restored)) == before, 'Preview tiling changed the asset')
        view = crop(tiled, inside)
        period = points['tile_period']
        require(view != normal_view and crop(restored, inside) == normal_view, 'The preview tiled state did not change and restore')
        require(view[:-period] == view[period:] and all(row[:-period * 3] == row[period * 3:] for row in view),
                'The tiled preview does not repeat by half the pane in both directions')
        check('button/1 x', tiled, 'Preview changed to a repeated 2×2 pattern. The texture asset was identical.', normal)
        check('button/2 x 2', restored, 'Second click restored the 1× preview pixels and left the asset identical.', tiled, normal)

    def output():
        output = sample('texture-output-roughness', ['output', 'output_roughness'])
        rough = crop(output, inside)
        require(properties(ptex(output)) == before and rough != normal_view, 'Output selection should change only the visible preview')
        require(all(all(a == b == c for a, b, c in zip(line[0::3], line[1::3], line[2::3])) for line in rough),
                'The selected preview output was not grayscale')
        check('combo/tex_out', output, 'The Color preview changed to a grayscale Roughness output with unchanged asset fields.', normal)

    def size():
        resize = sample('texture-size-512', ['size', 'size_512'])
        resize_undo = sample('texture-size-undo', ['size', 'size_512'], ['perf frames 3', 'undo'])
        require(properties(ptex(resize)) == {**before, 'Width': '512', 'Height': '512'}, 'Expected only Width/Height at 512')
        require(properties(ptex(resize_undo)) == before, 'Undo did not restore 256×256 size')
        check('combo/tex_size', resize, 'Persisted Width and Height became 512. Undo restored the 256×256 texture.', normal, resize_undo)

    def smart():
        picked = sample('texture-pick-painted-metal', ['smart', 'smart_painted_metal'])
        require(properties(ptex(picked)) == before and crop(picked, points['smart_crop']) != crop(normal, points['smart_crop']),
                'Smart picker did not independently change its label with unchanged stack')
        applied = sample('texture-apply-painted-metal', ['smart', 'smart_painted_metal', 'smart_apply'])
        applied_undo = sample('texture-apply-undo', ['smart', 'smart_painted_metal', 'smart_apply'], ['perf frames 3', 'undo'])
        result = properties(ptex(applied))
        require(result['Layers'] == '"Fill;Checker;Group;"' and result.get('L2_Name') == '"Painted Metal"',
                'Apply did not append the chosen group')
        require(all(result.get(key) == value for key, value in before.items() if key != 'Layers') and
                properties(ptex(applied_undo)) == before, 'Apply changed the existing layers or Undo failed to restore the stack')
        check('combo/smat_pick', picked, 'Picker changed visibly without changing the stack. Apply in the companion case confirmed Painted Metal.', normal, applied)
        check('button/Apply', applied, 'Appended Painted Metal as a Group. Undo restored the original stack.', picked, applied_undo)

    def new():
        menu_prefix = 'menu/texture_window.cpp/draw_textures_window/tex_new/menu_item/'
        empty_base = starter() + ['perf frames 3']
        no_asset = sample('texture-new-control', ['texture_off_target'], base=empty_base)
        menu = run(app, work, 'texture-new-menu', [*empty_base, click(points['new'])])
        cancel = sample('texture-new-dismiss', ['new', 'texture_off_target'], base=empty_base)
        require(crop(no_asset, points['new_menu_crop']) != crop(menu, points['new_menu_crop']) and
                crop(no_asset, points['new_menu_crop']) == crop(cancel, points['new_menu_crop']), 'New popup did not visibly open and dismiss')
        require(all(not list((case / 'project/Assets/Textures').glob('*.ptex')) for case in (no_asset, menu, cancel)),
                'Opening or dismissing New created a texture')
        check('button/New', menu, 'New displayed its popup and an outside click dismissed it. Neither created a texture.', no_asset, cancel)
        for is_surface, name, item in ((False, 'empty', 'Empty'), (True, 'surface', 'Surface on the Selected Object')):
            case = sample('texture-new-' + name, ['new', 'new_' + name], base=empty_base)
            files = list((case / 'project/Assets/Textures').glob('*.ptex'))
            require(len(files) == 1 and properties(files[0]).get('Surface', '0') == str(int(is_surface)), f'New {item} produced the wrong texture kind')
            require(properties(files[0]).get('Layers', '""') == ('"Fill;"' if is_surface else '""'), f'New {item} produced the wrong initial layers')
            if is_surface:
                require(scene_maps(case, 'Cube').get('Base_Map') == f'"Assets/Textures/{files[0].name}#albedo" 0', 'New surface did not point Cube at its texture')
                require(scene_maps(case, 'Sphere') == scene_maps(no_asset, 'Sphere'), 'New surface altered Sphere')
            check(menu_prefix + item, case, f'Created one {files[0].name} with Surface {int(is_surface)}.' +
                  (' Cube referenced it and Sphere was unchanged.' if is_surface else ''), no_asset, cancel)

    def assign():
        decoy = [*surface(), 'select Sphere', 'newmat Other', 'assignmat Assets/Materials/Other.mat', 'perf frames 3']
        assign_before = sample('texture-assign-control', ['texture_off_target'], base=decoy)
        assign = sample('texture-assigned', ['assign'], base=decoy)
        sphere = scene_maps(assign, 'Sphere')
        for field, output_name, linear in (('Base_Map', 'albedo', 0), ('Normal_Map', 'normal', 1),
                                            ('Roughness_Map', 'roughness', 1), ('Metallic_Map', 'metallic', 1)):
            require(sphere.get(field) == f'"Assets/Textures/{SURFACE}.ptex#{output_name}" {linear}', f'Assign did not bind Sphere {field}')
        require(scene_maps(assign_before, 'Sphere') != sphere and scene_maps(assign_before, 'Cube') == scene_maps(assign, 'Cube'),
                'Assign did not isolate the selected sphere')
        check('button/Assign to Selection', assign, 'Only selected Sphere acquired the four PointerSurface maps.', assign_before)

    def mesh_maps():
        maps_base = [*starter(), 'select Sphere', 'newmat Decoy', 'assignmat Assets/Materials/Decoy.mat', 'surface new DecoySurface',
                     'settex DecoySurface Width 256', 'settex DecoySurface Height 256', *surface()]
        maps_fresh = sample('texture-bake-control', ['texture_off_target'], base=maps_base)
        prior_bake = [*maps_base, 'meshmaps 256 4', 'perf wait']
        old_maps = sample('texture-existing-maps-control', ['texture_off_target'], base=prior_bake)
        load = sample('texture-load-maps', ['load_maps'], base=prior_bake)
        baked = sample('texture-bake-maps', ['bake_maps'], base=maps_base)
        require(properties(ptex(old_maps)).get('Mesh_Maps', '""') == '""', 'The preexisting bake already linked the surface')
        for tag in (load, baked):
            require(properties(ptex(tag)).get('Mesh_Maps') == '"Assets/Textures/MeshMaps/Cube"', f'{tag.name} did not link mesh maps')
            require(properties(ptex(tag, 'DecoySurface')) == properties(ptex(maps_fresh, 'DecoySurface')), f'{tag.name} modified the decoy surface')
            files = {path.stem.split('_')[-1]: path for path in (tag / 'project/Assets/Textures/MeshMaps').glob('Cube_*.png')}
            require(set(files) == {'normal', 'position', 'curvature', 'ao', 'thickness', 'id'}, f'{tag.name} lacks the six mesh maps')
            for path in files.values():
                require(path.read_bytes()[16:24] == b'\x00\x00\x01\x00' * 2 and png_rows(path, 256)[0] == 256,
                        f'{path} did not decode at the expected dimensions')
        require(all(sha(path) == sha(old_maps / path.relative_to(load)) for path in
                    (load / 'project/Assets/Textures/MeshMaps').glob('Cube_*.png')), 'Load changed the existing bake')
        require(not list((maps_fresh / 'project/Assets/Textures/MeshMaps').glob('*.png')), 'Fresh no-op control has unexpected mesh maps')
        check('button/Load Mesh Maps', load, 'Linked the six preexisting decoded Cube maps with identical PNG bytes.', old_maps)
        check('button/Bake All Maps', baked, 'Created and linked six decoded Cube map names in a fresh fixture. Decoy surface remained unchanged.', maps_fresh)


    for scenario in (tile, output, size, smart, new, assign, mesh_maps):
        run_check("textures/" + scenario.__name__, scenario, failures)


def scene_maps(folder, name):
    scene = (folder / 'project/Assets/Scenes/Replay.scene').read_text(encoding='utf-8')
    match = re.search(rf'^object \d+ \d+ "{re.escape(name)}" [^\n]*\n(.*?)(?=^object |\Z)', scene, re.M | re.S)
    require(match, f'Saved scene does not contain {name}')
    slot = re.search(r'^  Materials \d+ (\d+)', match[1], re.M)
    if not slot:
        return {}
    block = re.search(rf'^material {slot[1]}(?=[ \t\n])[^\n]*\n(.*?)^end$', scene, re.M | re.S)
    return dict(re.findall(r'^  ([A-Za-z]+_Map) (.+)$', block[1], re.M)) if block else {}


def validate_layer_trials(points):
    for group, settings in LAYER_SETTINGS.items():
        for trial in points.get(group + '_trials', ()):
            field = trial['field']
            require(field in settings and trial['setting'] == settings[field],
                    f'{group} manifest property does not match the registered field {field!r}')


def layer_cases(app, work, points, rows, failures):
    for group, base in (('layer', surface()), ('math', surface() + ['surface add Math', 'surface set L2 Plane 4', 'perf wait'])):
        trials = points.get(group + '_trials', ())
        if not trials:
            continue
        expand = [click(point) for point in points[group + '_prepare']]
        init = [*base, *expand]
        size = '1600x1100' if group == 'math' else SIZE
        preview = points['math_preview_crop'] if group == 'math' else points['preview_crop']
        control = run(app, work, group + '-control', [*init, click(points['texture_off_target']), *save()], size)
        before = properties(ptex(control))
        def trial(test):
            tag = group + '-' + test['field'].lower().replace(' ', '-')
            action = [click(point) for point in test['clicks']]
            if 'dx' in test:
                action = [drag(test['clicks'][0], test['dx'])]
            positive = run(app, work, tag, [*init, *action, *save()], size)
            after = properties(ptex(positive))
            setting = LAYER_SETTINGS[group][test['field']]
            require(set(before) == set(after) and {key for key in before if before[key] != after[key]} == {setting},
                    f'Expected a change only to {setting}: {tag}')
            if 'want' in test:
                require(after[setting] == test['want'], f'Unexpected {setting} after pointer event: {after[setting]}')
            moved = test.get('render', False)
            if moved:
                require(crop(control, preview) != crop(positive, preview),
                        f'{tag} left the preview pixels unchanged')
            undone = run(app, work, tag + '-undo', [*init, *action, 'perf frames 3', 'undo', *save()], size)
            require(properties(ptex(undone)) == before, f'Undo did not restore {setting}: {tag}')
            if moved:
                require(crop(control, preview) == crop(undone, preview),
                        f'Undo did not restore {tag} preview')
            rows.append({'key': f'field/{"TextureLayer" if group == "layer" else "MathLayer"}/{test["field"]}',
                         'case': tag, 'result': f'{setting} {before[setting]} → {after[setting]}; Undo restored the original texture.' +
                         (' Preview pixels changed and recovered.' if moved else ''),
                         'controls': [control.name, undone.name]})

        for test in trials:
            run_check(group + "/" + test["field"], lambda: trial(test), failures)


def paint_base(layout):
    return surface(layout) + ['paint on', 'paint 2d on', 'paint channels all', 'paint size 48', 'paint hardness 0.8',
                              'paint opacity 1', 'paint flow 1', 'paint color 1 0.08 0.02', 'paint roughness 0.88',
                              'paint metallic 0.77', 'paint height 0.91', 'paint emission 0.2 0.12 0.4', 'perf frames 3']


def paint_finish():
    return ['perf frames 3', 'perf wait', 'export textures separate Exports/Paint', *save()]


def paint_cases(app, work, points, rows, failures):
    base = paint_base(TOOLS)
    settle = [TEXTURES, 'perf frames 3', 'perf wait']
    splash = click(points['paint_canvas'])
    finish = paint_finish()

    def sample(name, controls=(), mark=True, undo=False):
        gestures = [splash] if mark else []
        if undo:
            gestures += ['perf frames 3', 'undo']
        return run(app, work, name, [*base, *controls, *settle, *gestures, *finish], '1600x1100')

    toggles = {'Color': ('Albedo',), 'Roughness': ('Roughness',), 'Metallic': ('Metallic',),
               'Height': ('Height', 'Normal'), 'Emission': ('Emission',)}
    untouched = map_hashes(sample('paint-no-dab', mark=False))
    applied = map_hashes(sample('paint-all-channels'))
    undone = map_hashes(sample('paint-undo', undo=True))
    off_target = map_hashes(sample('paint-off-target', [click(points['tools_off_target'])]))
    disabled = {field: sample('paint-disable-' + field.lower(), [click(points['paint_' + field.lower()])])
                for field in toggles}
    numeric = {}
    for field in ('Roughness', 'Metallic', 'Height'):
        slug = field.lower()
        point = points.get('paint_value_' + slug)
        if point is not None:
            numeric[field] = (sample('paint-no-move-' + slug, [drag(point, 0)]),
                              sample('paint-value-' + slug, [drag(point, -21)]))
    require(all(applied[name] != untouched[name] for name in MAPS if name != 'AO'), 'All-channel dab did not reach every painted map')
    require(applied['AO'] == untouched['AO'], 'All-channel dab unexpectedly affected AO')
    require(undone == untouched, 'Undo did not restore the seven PNGs')
    require(off_target == applied, 'The off-target tool click changed the dab')
    def toggle(field, folder):
        affected = toggles[field]
        hashes = map_hashes(folder)
        want = {name: untouched[name] if name in affected else applied[name] for name in MAPS}
        require(hashes == want, f'Paints {field} did not independently route the dab: {folder}')
        rows.append({'key': PAINT_KEY + 'field/Paints ' + field, 'case': folder.name,
                     'result': f'{", ".join(affected)} matched no-dab output. Other maps matched the all-enabled dab.',
                     'controls': ['paint-no-dab', 'paint-all-channels', 'paint-undo', 'paint-off-target']})

    def value(field, baseline, altered):
        affected = toggles[field]
        require(map_hashes(baseline) == applied, f'No-motion edit changed {field}')
        hashes = map_hashes(altered)
        require(all((hashes[name] != applied[name]) == (name in affected) for name in MAPS), f'{field} did not independently change its mapped value')
        require(all(hashes[name] != untouched[name] for name in affected), f'{field} disabled its map rather than changing the value')
        rows.append({'key': PAINT_KEY + 'field/' + field, 'case': altered.name,
                     'result': f'Drag changed {", ".join(affected)} while the other maps matched the same dab.',
                     'controls': [baseline.name, 'paint-all-channels', 'paint-no-dab']})


    for field, folder in disabled.items():
        run_check('paint/toggle/' + field, lambda: toggle(field, folder), failures)
    for field, (baseline, altered) in numeric.items():
        run_check('paint/value/' + field, lambda: value(field, baseline, altered), failures)


def paint_alpha(path):
    data = path.read_bytes()
    expected = (256, 256, 8, 6, 0, 0, 0)
    require(data.startswith(b"\x89PNG\r\n\x1a\n") and struct.unpack_from('>IIBBBBB', data, 16) == expected,
            f'Expected 256×256 RGBA paint sidecar: {path}')
    pieces = []
    offset = 8
    while offset + 12 <= len(data):
        length, kind = struct.unpack_from('>I4s', data, offset)
        if kind == b'IDAT':
            pieces.append(data[offset + 8:offset + 8 + length])
        offset += length + 12
    raw = zlib.decompress(b''.join(pieces))
    stride = 256 * 4 + 1
    require(len(raw) == 256 * stride, f'Unexpected RGBA payload length: {path}')
    prev = bytes(256)
    result = bytearray()
    for start in range(0, len(raw), stride):
        method = raw[start]
        line = bytearray(raw[start + 4:start + stride:4])
        if method:
            for x, alpha in enumerate(line):
                left = line[x - 1] if x else 0
                up = prev[x]
                up_left = prev[x - 1] if x else 0
                if method == 4:
                    p = left + up - up_left
                    prediction = min((left, up, up_left), key=lambda value: abs(p - value))
                else:
                    prediction = (0, left, up, (left + up) // 2)[method]
                line[x] = (alpha + prediction) & 255
        prev = line
        result.extend(line)
    return result


def edge_cases(app, work, points, findings):
    def sample(tag, commands):
        return run(app, work, tag, [*paint_base(TEXTURES), *commands, *paint_finish()], '1600x1100')

    near = click(points['paint_left_edge'])
    untouched = sample('edge-outside-control', [click(points['paint_outside'])])
    dab = sample('edge-left-dab', [near])
    undo_step = [near, 'perf frames 3', 'undo']
    undo = sample('edge-undo', undo_step)
    repeated_undo = sample('edge-undo-existing-layer', [*undo_step, click(points['paint_canvas']), 'perf frames 3', 'undo'])
    removed = sample('edge-undo-added-layer', [*undo_step, 'perf frames 3', 'undo'])
    start, end = points['paint_internal_cross']
    stroke = f'perf drag at {start[0]} {start[1]} {end[0]} {end[1]} frames 8'
    crossing = sample('edge-tiled-crossing', [click(points['paint_tile']), stroke])

    def sidecar(case, name):
        base = properties(ptex(case))['L2_Images'].strip('"')
        return case / 'project' / (base + '_' + name + '.png')

    def channel_map(case):
        path = sidecar(case, 'channels')
        require(path.read_bytes()[16:24] == b'\x00\x00\x01\x00' * 2, 'Coverage map must be 256×256')
        return [row[0::3] for row in png_rows(path, 256)[1]]

    def changed_columns(texels):
        return {x for line in texels for x, bits in enumerate(line) if bits}

    before = properties(ptex(untouched))
    restored = properties(ptex(undo))
    require(before['Layers'] == '"Fill;Checker;"' and 'L2_Images' not in before, 'Outside click created paint storage')
    require(restored['Layers'] == '"Fill;Checker;Paint;"' and restored['L2_Stamp'] == '0' and
            all(restored[key] == value for key, value in before.items() if key != 'Layers'),
            'Undo should retain only an empty Paint layer on top of the unchanged stack')
    require(properties(ptex(repeated_undo)) == restored and
            (repeated_undo / 'editor.log').read_text(encoding='utf-8', errors='replace').count('Undo Paint') == 2,
            'A second stroke and Undo did not restore the existing empty Paint layer')
    require(properties(ptex(removed)) == before and map_hashes(removed) == map_hashes(untouched),
            'Undo Add Paint Layer did not restore the unpainted stack and exports')
    require('Undo Add Paint Layer' in (removed / 'editor.log').read_text(encoding='utf-8', errors='replace'),
            'The second Undo did not reach the auto-created layer edit')
    for case in (undo, repeated_undo):
        require(not any(any(line) for line in channel_map(case)) and
                all(not any(paint_alpha(sidecar(case, name))) for name in ('color', 'emit')) and
                map_hashes(case) == map_hashes(untouched),
                'Undo did not clear edge coverage and alpha, or restore all seven exported maps')
    left = channel_map(dab)
    center = channel_map(crossing)
    left_cols, cross_cols = changed_columns(left), changed_columns(center)
    require(0 in left_cols and max(left_cols) < 64, 'A single 2D dab did not clip at the physical edge')
    require({0, 255} <= cross_cols and cross_cols.isdisjoint(range(80, 176)),
            'A drag across the tiled junction did not stay near both texture edges')
    require({bits for entry in (left, center) for line in entry for bits in line} == {0, 31}, 'Unexpected painted channel bits')
    findings.update(
        case='edge-left-dab', controls=['edge-outside-control', 'edge-undo', 'edge-undo-existing-layer', 'edge-undo-added-layer', 'edge-tiled-crossing'],
        dab_texels=sum(bool(bits) for line in left for bits in line), dab_columns=[min(left_cols), max(left_cols)],
        cross_left_texels=sum(bool(bits) for line in center for bits in line[:80]),
        cross_right_texels=sum(bool(bits) for line in center for bits in line[176:]),
        undo='First Undo restored all seven exports and kept an empty Paint layer with zero stamp, alpha and channel coverage. Undo on that existing layer restored the same layer and exports.',
        consecutive_undo='Undo Add Paint Layer removed the first stroke’s automatic layer. The ptex fields and exports match the unpainted control.',
        files_after_layer_removal=sorted(path.name for path in ptex(removed).parent.glob(f'{SURFACE}_L2_*.png')),
        scope='2D texture UV canvas. A single dab clips its footprint, while a drag across the tiled junction paints both edges.',
    )


def run_check(name, action, failures):
    try:
        action()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        failures[name] = traceback.format_exc()
        print(failures[name], flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--expected-sha256', default=PACKAGE_SHA,
                        help='SHA-256 of the Windows editor (defaults to the round 0651 package)')
    parser.add_argument('--register', type=Path, help='Exact-key register for the editor under test')
    parser.add_argument('--points', type=Path, help='Coordinate manifest for the frozen editor and 1600-pixel workspace')
    parser.add_argument('--inspect', action='store_true', help='Capture popup and unfolded layer anchors from --points')
    parser.add_argument('--groups', nargs='+', choices=('textures', 'layers', 'paint', 'edge'), default=('textures', 'layers', 'paint', 'edge'))
    args = parser.parse_args()
    expected = args.expected_sha256.lower()
    if not re.fullmatch(r'[0-9a-f]{64}', expected):
        parser.error('--expected-sha256 must be a SHA-256 hexadecimal digest')
    if expected != PACKAGE_SHA and args.register is None:
        parser.error('--register is required for a different Windows editor package')
    app, work = args.app.resolve(), args.work.resolve()
    package = sha(app)
    if package != expected:
        raise ValueError(f'Editor package mismatch: expected {expected}, found {package}')
    register = (args.register or Path(__file__).resolve().parents[2] / 'docs/perf/0630-inventory.tsv').resolve()
    work.mkdir(parents=True)
    write_json(work / 'identity.json', {'package_sha256': package, 'expected_package_sha256': expected,
                                       'driver_sha256': sha(Path(__file__)), 'register': str(register),
                                       'register_sha256': sha(register), 'points_sha256': sha(args.points) if args.points else None,
                                       'size': SIZE, 'paint_size': '1600x1100', 'executable': str(app)})
    if args.points is None:
        calibrate(app, work)
        return
    points = json.loads(args.points.read_text(encoding='utf-8-sig'))
    validate_layer_trials(points)
    write_json(work / 'points.json', points)
    if args.inspect:
        inspect_controls(app, work, points)
        return
    with register.open(encoding='utf-8', newline='') as file:
        prior = {row['key']: row for row in csv.DictReader(file, delimiter='\t')}
    rows, failures = [], {}
    groups = {'textures': texture_cases, 'layers': layer_cases, 'paint': paint_cases}
    edge = {}
    for name in args.groups:
        if name == 'edge':
            run_check(name, lambda: edge_cases(app, work, points, edge), failures)
        else:
            run_check(name, lambda: groups[name](app, work, points, rows, failures), failures)
    keys = [row['key'] for row in rows]
    require(len(keys) == len(set(keys)) and set(keys) <= prior.keys(), 'Unknown or duplicate exact key')
    for row in rows:
        previous = prior[row['key']]
        row.update(action=previous['name'], status='end-state', prior=previous['status'], source=previous['source'],
                   evidence=str(work / row['case']), controls=[str(work / name) for name in row['controls']])
    write_json(work / 'checks.json', {'rows': rows, 'failures': failures})
    if edge:
        write_json(work / 'edge.json', edge)
    print(f'{len(rows)} distinct exact keys checked; {len(failures)} failures', flush=True)
    raise SystemExit(bool(failures) or not (rows or edge))


if __name__ == '__main__':
    main()
