# Architecture decision records

Short records of decisions that shape the code: what was decided, why, and what it costs. Write one when
a change adds a subsystem, changes the data model or a documented convention, adds a dependency, or
changes how the project is built, tested or released. The human approves an ADR before the code that
depends on it is written.

Records are never rewritten after acceptance; a later decision supersedes an earlier one by number.

## Index

Open each record for its status, amendments and superseding decisions. The numbered files skip
`0137`, `0139`, `0141`, `0142`, `0143` and `0144`. These gaps are preserved.

| ADR | Title |
|---|---|
| [0001](0001-optional-blender-libraries.md) | Blender's libraries are optional, with dependency-free fallbacks |
| [0002](0002-gpl-licensing.md) | GPL licensing (source GPL-2.0-or-later, binaries GPL-3.0-or-later) |
| [0003](0003-copy-on-write-meshes.md) | Copy-on-write meshes and scene snapshots for undo |
| [0004](0004-sketchup-push-pull.md) | SketchUp semantics for Push/Pull |
| [0005](0005-tool-settings-in-helpers.md) | Tool settings live in the F9 panel and drag helpers |
| [0006](0006-subtree-publishing-and-prs.md) | Subtree publishing and human-merged pull requests |
| [0007](0007-camera-filters.md) | Camera filters are components with a raster stage and an image stage |
| [0008](0008-camera-filter-stack.md) | One Camera Filters stack per camera, with effects chosen by category |
| [0009](0009-view-render-cache.md) | Reuse a view's picture while a content hash says nothing it shows has changed |
| [0010](0010-baked-lighting-data.md) | Baked lighting: lightmaps keyed to what was baked, UVs kept in the bake |
| [0011](0011-voxel-gi.md) | Voxel GI on the CPU, per pixel, feeding the baked lighting |
| [0012](0012-probe-volumes.md) | Probe volumes: adaptive bricks of ambient-cube probes, baked and kept live |
| [0013](0013-gpu-viewport.md) | A GPU viewport, decided by a measured spike |
| [0014](0014-gi-on-the-gpu.md) | Global illumination on the GPU: compute for gathers and bakes, the CPU as reference |
| [0015](0015-render-hash-memo.md) | A per-object render-hash memo keyed by versions |
| [0016](0016-active-camera-and-filter-frames.md) | An explicit Active Camera, and filters that see every picture a camera makes |
| [0017](0017-procedural-texture-assets.md) | Procedural texture assets: a layer stack evaluated to images |
| [0018](0018-axis-materials.md) | Axis materials: sub-materials chosen by the surface's slope, projected triplanar |
| [0019](0019-texture-bake.md) | Texture bake: lightmap-style UVs, the material evaluated per texel, one atlas per object |
| [0020](0020-toon-shading.md) | Shading models on materials, and inverted-hull outlines |
| [0021](0021-gpu-background-open.md) | GPU devices open in the background, from a pool of ready renderers |
| [0022](0022-deform-only-path.md) | Deforming modifiers move positions in place (`deform_version`, `deform_serial`) |
| [0023](0023-mean-value-cage.md) | The Mesh Deform modifier binds with mean value coordinates |
| [0024](0024-vertex-groups.md) | Vertex groups are a mesh layer every operator carries |
| [0025](0025-bones-and-skins.md) | Bones are GameObjects; a Skin modifier deforms by them; constraints are solved after the update |
| [0026](0026-minimum-spec-and-quality-tiers.md) | A minimum machine, quality tiers that cap the editor's views, cached shadows |
| [0027](0027-workspaces.md) | Layouts for each stage of the work, each with its own set of tools |
| [0028](0028-inpainted-skin-weights.md) | Skin weights by inpainting (Epic's "Robust Skin Weights Transfer via Weight Inpainting") |
| [0029](0029-animation-clips.md) | Animation clips, Unity's way |
| [0030](0030-clip-sequences.md) | Sequences of clips on a timeline, with triggers, instead of a state machine |
| [0031](0031-rig-controls.md) | Rig controls as a component; FK solved before IK |
| [0032](0032-ffmpeg.md) | FFmpeg linked for video, as Blender does |
| [0033](0033-updates-and-library-fallback.md) | Updates from GitHub Releases; Blender's libraries optional at run time |
| [0034](0034-render-mesh-in-place.md) | The render mesh follows a positions-only edit in place |
| [0035](0035-eight-wide-shading.md) | Plain-material pixels lit 8 at a time (AVX2), the scalar shader as the reference |
| [0036](0036-views-on-the-gpu.md) | The views' 3D on the GPU, the CPU renderer the reference and the fallback |
| [0037](0037-cuda-optix-build-time-sdks.md) | CUDA and OptiX through build-time SDKs that Blender's libraries don't bundle |
| [0038](0038-texture-sampling-contract.md) | One texture-sampling contract for the CPU and GPU views |
| [0039](0039-cheaper-while-interacting.md) | A view's picture may be cheaper only while you interact |
| [0040](0040-overlap-check-reused-local-background.md) | The overlap check is reused by content, local where it can be, on a worker only for huge meshes |
| [0041](0041-moonray-external-renderer.md) | MoonRay is an external renderer: a separate program, downloaded on request, run in WSL on Windows |
| [0042](0042-body-physics-for-animation.md) | The body for physics in animation: masses from the skin, corrections written as keys |
| [0043](0043-autophysics-centroidal-qp.md) | AutoPhysics as a centroidal trajectory optimisation, solved by Blendity's own QP |
| [0044](0044-active-ragdoll-jolt.md) | The active ragdoll on Jolt's ragdoll, events in the clip, keys out |
| [0045](0045-animation-assistants.md) | Animation assistants: switched on by the animator, written as keys, procedural walking |
| [0046](0046-ai-inbetweening.md) | AI inbetweening: a small network trained on CMU, ONNX Runtime as an optional download |
| [0047](0047-imported-characters.md) | Imported characters: materials as assets, shape keys, and MMD, VRM, BVH and Collada readers of our own |
| [0048](0048-anime-rendering.md) | Anime rendering: toon styles and painted control (the 2D look continues in task 0149) |
| [0049](0049-hair-and-soft-tissue-physics.md) | Realistic hair and soft-tissue physics on Spring Bones |
| [0050](0050-area-lights-emissive-planes.md) | Area lights are emissive planes |
| [0051](0051-gpu-view-one-submission.md) | A GPU view is one submission - its passes and its copies out together; no deferred flush |
| [0052](0052-path-tracer-device-workers.md) | Persistent device workers and two submissions in flight for the path tracer |
| [0053](0053-edit-operators-one-copy.md) | an Edit Mode operator copies the mesh once; per-array copy-on-write declined |
| [0054](0054-shipping-optix.md) | Shipping OptiX - committed PTX, host headers fetched from NVIDIA at build, the driver at run time |
| [0055](0055-live-link.md) | Live Link - a local TCP server, lines of JSON and glTF binaries, one way out |
| [0056](0056-what-a-unit-test-is-for.md) | What a unit test is for; measurements live in stress |
| [0057](0057-part-based-uv-unwrapping.md) | Part-based UV unwrapping - PartUV's recursion on a geometric part tree, ABF++ beside LSCM |
| [0058](0058-editing-window.md) | The Editing window - a linear-float adjustment stack, non-destructive, shared with the camera filters |
| [0059](0059-secondary-windows.md) | Secondary OS windows - one framebuffer and UI context per window, one scene, one simulation tick |
| [0060](0060-interaction-principles.md) | Interaction principles - Unity's structure, Blender 2.8's reach, scaffolding for newcomers |
| [0061](0061-native-gpu-views.md) | native GPU views - overlays and presentation on the card, the CPU path as the fallback |
| [0062](0062-pointer-kinds-and-touch.md) | pointer kinds, and one touch recognizer every platform feeds |
| [0063](0063-sketch-canvases-and-guides.md) | the sketch - strokes as scene data, guides as tools, corrections at the pen's lift |
| [0064](0064-guide-camera-and-construction-lines.md) | the perspective guide is the camera; canvases in the scene; construction lines are scene guides |
| [0065](0065-solids-from-construction-lines.md) | solids from the construction lines - a few strokes fix a solid, the artist places and refines it |
| [0066](0066-the-drawing-over-the-model.md) | the drawing over the model - layers true from their view, corrections in the plane, strokes into the mesh |
| [0067](0067-lifting-a-scaffold.md) | lifting a scaffold - the spike's finding and what is kept |
| [0068](0068-the-wheel.md) | the Wheel - one radial menu of palette entries |
| [0069](0069-custom-panels-and-user-actions.md) | custom panels, user actions in the keymap, the UI file |
| [0070](0070-a-kind-once-per-window.md) | a window kind once per window, with a view state of its own |
| [0071](0071-the-sketch-tool-in-a-view.md) | the sketch tool in a view; every stroke a place in the scene |
| [0072](0072-layers-composited-as-photoshop-does.md) | sketch layers composited as Photoshop composites, edited in a panel the pen can work |
| [0073](0073-the-module-registry-and-tabs-that-fit.md) | one module registry; tabs that fit, fold and follow the stage |
| [0074](0074-sculpting-under-the-brush-and-delta-undo.md) | sculpting touches only what is under the brush; a stroke's undo is a delta |
| [0075](0075-dynamic-subdivision-by-local-re-evaluation.md) | Dynamic Subdivision - the subdivided surface follows a dab by local re-evaluation |
| [0076](0076-dynamesh-by-winding-columns-and-surface-nets.md) | DynaMesh - a uniform retopology from winding columns and surface nets |
| [0077](0077-zremesher-spike-quadriflow.md) | a ZRemesher-class retopology through QuadriFlow (a spike) |
| [0078](0078-sculptris-pro-dynamic-tessellation.md) | Sculptris Pro - the mesh tessellated under the brush as it moves |
| [0079](0079-mesh-maps.md) | mesh maps - the surface's geometry baked into its UV layout |
| [0080](0080-layer-stack-on-a-surface.md) | the layer stack on an object's surface - masks as stacks, mesh-map generators, rect evaluation |
| [0081](0081-projection-painting-and-tile-undo.md) | painting on the model by projection, with tile undo |
| [0082](0082-smart-materials-and-texture-sets.md) | smart materials as saved groups, texture sets as packings, 2D painting on the same brush |
| [0083](0083-hand-painted-look.md) | the hand-painted look - stamps, wet mixing and baked light as layers, an unlit shading model |
| [0084](0084-stylized-environments.md) | stylized environments - strokes as a mesh with custom normals, scatter as children, fog as a scene setting |
| [0085](0085-hci-methods-and-foundations.md) | the HCI pass over the whole editor - the methods, the foundations, the measuring sticks |
| [0086](0086-livelink-discovery.md) | Live Link bridges that find Blendity - a discovery file, auto-start, a hello |
| [0087](0087-puppet-posing.md) | Puppet Posing - a learned pose prior under exact IK, keys on the artist's own rig |
| [0088](0088-sketch-brushes.md) | Sketch brushes - SketchBook's brushes as fields on vector strokes |
| [0089](0089-reference-board.md) | The reference board - a project asset of its own, not scene data |
| [0090](0090-papers.md) | Papers - the Draw tab's Paper source draws on sketches of their own |
| [0091](0091-shader-nodes-as-layers.md) | Blender's shader nodes as the texture stack's layers and options |
| [0092](0092-volume-fog.md) | a Volume (fog) material as a surface type |
| [0093](0093-camera-flatten.md) | Camera Flatten - a camera-dependent placement, shaded as the true one |
| [0094](0094-view-dependent-deformation.md) | view-dependent deformation - the camera views' own evaluated meshes |
| [0095](0095-performance-budget.md) | a performance budget - VRChat's ranks, counted from what an engine draws |
| [0096](0096-vr-openxr.md) | VR through OpenXR, its loader loaded at run time, eyes shown through Direct3D 11 |
| [0097](0097-vrc-light-volumes.md) | VRC Light Volumes - Blendity's component, its bakes on disk and on the wire |
| [0098](0098-unity-lightmaps.md) | Unity's baked lightmaps in Blendity - asked of Unity, kept as Blendity's own baked lighting |
| [0099](0099-unity-scene-look.md) | A Unity scene's look as Unity shows it - fog by view depth, the skybox, post-processing, lights' shadows, reflection probes (round 401, task 0378) |
| [0100](0100-liltoon-avatar.md) | lilToon's lighting model in Blendity; VRChat's avatar components as native components (round 402, task 0379) |
| [0101](0101-unity-components-as-data.md) | Unity's components as editable data; VRChat's SDK classes named by their fileIDs (round 403, task 0380) |
| [0102](0102-unity-anim.md) | Unity's .anim as a clip format |
| [0103](0103-humanoid-retarget.md) | Humanoid retargeting by the parts' turns |
| [0104](0104-books-references-licensing.md) | The two books, the generated reference list, and the licensing statement for an AI-assisted GPL project |
| [0105](0105-responsiveness-budget.md) | The responsiveness budget - nothing halts the editor, and the suite that measures it |
| [0106](0106-renders-and-bakes-on-workers.md) | Renders and bakes on workers - the snapshot, the background pool, what the main thread leaves alone |
| [0107](0107-geometry-and-files-on-workers.md) | Geometry and files on workers - the mesh job, the slow stack's hook, the clone that saves |
| [0108](0108-approximations-on-the-card.md) | Approximations on the card instead of refusals - the Scene view stays on the GPU for a Unity world |
| [0109](0109-the-cpus-share-of-a-card-frame.md) | The CPU's share of a card frame - corners kept on the card, lights that can't reach the view left out, a renderer's Shadows Off |
| [0110](0110-filters-and-liltoon-on-the-card.md) | The Game view's post-processing on the card, lilToon's own model on the card |
| [0111](0111-binary-mesh-chunks-in-the-scene-file.md) | Binary mesh chunks inside the scene file, and the writers' and readers' inner loops on every core |
| [0112](0112-allocators-and-the-profiler.md) | oneTBB's allocator for the Windows build with Blender's libraries, and Tracy for developers |
| [0113](0113-the-bakes-quality-target.md) | The bake's quality target - measured against Unity's own bake |
| [0114](0114-directional-lightmaps.md) | Directional lightmaps - a direction page per lightmap, decoded as Unity's |
| [0115](0115-the-scenes-change-counter.md) | The scene's change counter - the views' scene hash kept while nothing changed |
| [0116](0116-l2-sh-probes.md) | Light probes as L2 spherical harmonics, and Unity's probes over their tetrahedra |
| [0117](0117-shadowmask-and-subtractive.md) | Shadowmask and Subtractive |
| [0118](0118-four-mask-lights-and-distance-shadowmask.md) | Four mask lights and Distance Shadowmask |
| [0119](0119-sh-deringing-and-light-probe-groups.md) | Windowed probe SH, and Light Probe Groups as objects |
| [0120](0120-the-drawing-solver.md) | The drawing solver - evaluated UVs, projection onto the surface, cleanup after drawing |
| [0121](0121-light-tree.md) | A light tree picks emissive triangles by where the sample is |
| [0122](0122-variance-guided-lightmap-filter.md) | The lightmap filter is guided by each texel's measured noise |
| [0123](0123-changelog-fragments.md) | Rounds write changelog fragments; the pass write-up folds them into CHANGELOG and README |
| [0124](0124-skinning-once-and-only-what-moved.md) | A skinned mesh is deformed once, only when its own bones moved, and follows in place |
| [0125](0125-the-world-stays-kept-while-a-character-moves.md) | The world stays kept while a character moves - the card's instances and see-through lists by item |
| [0126](0126-one-solve-a-frame-placed-right-and-a-rig-registry.md) | One constraint solve a frame, after the tools and before the views; rig lookups from a registry |
| [0127](0127-the-pose-solver-on-a-pose-buffer.md) | The full-body pose solver works on a pose buffer, not on the scene |
| [0128](0128-the-blockout-kernel.md) | The Blockout kernel - an exact path and a field path, chosen per cluster |
| [0129](0129-the-blockout-object.md) | The Blockout object - pieces as children, built from a snapshot on a worker |
| [0130](0130-a-blend-in-a-box-of-its-own.md) | A blend in a box of its own, stitched to the rest |
| [0131](0131-unity-smoothness.md) | A Unity material's smoothness - one number a material now, a texel later |
| [0132](0132-integration-prs.md) | The integration PR - one pull request that carries every green stack |
| [0133](0133-ci-build-once-shards-and-gates.md) | CI builds once per platform, shards the sanitizer tests, and keeps the four check names as gate jobs |
| [0134](0134-local-groups-ci-whole-suite.md) | A round runs its own test groups locally; the whole suite is CI's |
| [0135](0135-a-blockout-takes-scale-in-its-numbers.md) | A Blockout takes scale in its numbers, never in a transform |
| [0136](0136-a-door-leaf-is-its-own-object.md) | A door's leaf is its own object, not part of the Blockout's mesh |
| [0138](0138-modifiers-on-a-blockout-piece.md) | Modifiers on a Blockout piece |
| [0140](0140-a-bakes-pages-go-to-the-card-before-it-lands.md) | A bake's lightmap pages go to the card before the bake lands |
| [0145](0145-keyboard-focus.md) | Keyboard focus - how the keys reach a window's controls |
| [0146](0146-skinning-on-the-card.md) | Skinning on the card, the CPU the reference and the fallback |
| [0147](0147-point-clouds-and-gaussian-splats.md) | Point clouds and Gaussian splats - a structure of arrays beside Mesh |
| [0148](0148-quaternion-rotation-tracks.md) | Quaternion rotation tracks in clips, and converting a clip's rotation between Euler and Quaternion |
| [0149](0149-an-openings-frame-and-glass-are-objects.md) | A styled opening's frame and glass are objects, like its leaf, so each part takes a modifier stack |
| [0150](0150-modifiers-on-a-door-or-window.md) | Modifiers on a door or window |
| [0151](0151-traced-shadows-in-the-views.md) | The views' traced shadows - ray queries on the rasterizer's own device |
| [0152](0152-colour-attributes.md) | Colour attributes on Mesh, and the Vertex Paint mode |
| [0153](0153-panoramic-cameras-above-the-renderers.md) | Panoramic cameras are drawn above the renderers, from cube faces |
| [0154](0154-steam-workshop-import.md) | Import installed Steam Workshop assets into native projects |
| [0155](0155-local-incremental-builds.md) | Reuse verified MSVC objects for local editor builds |
| [0156](0156-hosted-msvc-provisioning.md) | Provision MSVC 14.51 on hosted Windows build runners |
| [0157](0157-unity-gloss-edit.md) | A measured Unity roughness merges into each material state |

## Template

```markdown
# NNNN: <decision in a few words>

- **Status:** proposed | accepted | superseded by NNNN
- **Date:** YYYY-MM-DD
- **Requirements:** R-xx, Q-xx, C-xx

## Context
The problem and the forces at play (what we must keep working, what users asked for).

## Decision
What we do, stated so code review can check it.

## Consequences
What becomes easier, what becomes harder, what we give up, what to watch.
```
