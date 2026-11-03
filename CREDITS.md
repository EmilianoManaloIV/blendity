# Credits

Blendity stands on the work of others. Ideas are borrowed with their names attached; code only with its licence
(`licenses/README.md`). The two books in `docs/` (`Blendity-Architecture.pdf`, `Blendity-User-Guide.pdf`) carry the
full reference list, every paper, manual and library with the pages where it is used.

## Authorship
Blendity is copyright Emiliano Manalo and contributors. Its source, tests, documentation and books are written with
Anthropic's Claude through Claude Code, under a human's direction and review (`docs/AI_DISCLOSURE.md`): the user decides
what Blendity is for, approves every task's specification and decision record before code is written, reviews every
pull request and merges it. The model's outputs belong to the user who directed them under Anthropic's terms, and are licensed to the extent
copyright subsists in them (the human selection, direction, review and editing); the `Co-Authored-By` trailer on each
commit is attribution, not a licence grant. Everything is licensed GPL-2.0-or-later
(binaries GPL-3.0-or-later, ADR 0002); nothing of anyone else's code is claimed as the project's own.

## The programs Blendity follows
| Who | What Blendity takes from them |
|---|---|
| **The Blender Foundation and the Blender authors** (GPL-2.0-or-later) | The design and the source every subsystem mirrors: meshes, BMesh's operators, UV unwrapping, the Principled BSDF, EEVEE's and Cycles' rendering, colour management, armatures, skins and IK, the modifier stack, the file formats; the libraries Blender bundles, used behind switches; the small pieces copied into `extern/` (ufbx, fast_float, MikkTSpace, the Hosek-Wilkie sky, QuadriFlow, cuew, libc_compat) and the sky evaluation ported from Cycles. Every Learn lesson names the Blender source file behind it. |
| **Unity Technologies** | The editor's shape: the windows, docking, navigation, the Q W E R Y tools and gizmos, the Inspector, Play mode, lighting modes, lightmaps and probe volumes, the Animation window, Humanoid retargeting as Mecanim reads it, `.anim`, `.mat`, prefab, scene and package files, LOD Groups, colliders, ProBuilder's shapes. |
| **Epic Games** (Unreal Engine) | The Unreal keymap preset and editor feel, FBX collision naming (UCX_), the Sequencer's camera cuts, the Unreal Live Link bridge's target. The weight inpainting method (Abdrashitov et al. 2023). |
| **The Godot Engine contributors** | The `.tscn` format and the Godot bridge's target. |
| **Autodesk** (Maya, 3ds Max) | The Maya and 3ds Max keymap presets and the Maya editor feel. |
| **Trimble** (SketchUp) | Push/Pull's semantics, the drawing tools and inference snapping, Follow Me, the SketchUp preset. |
| **Adobe** (Photoshop, Substance 3D Painter, Mixamo) | The Adobe keymap preset and the brush keys, the 27 blend modes, Free Transform and the Layers window; mesh maps, the layer stack, smart masks and smart materials; the marker auto-rig's workflow and the bone naming the humanoid map knows. |
| **UModeler, Inc.** and **Plasticity** | Drawing onto faces, Follow, Lathe, Slice; construction planes and lines, the perpendicular snap, Shell, Thicken, Draft, Radial Array, Fillet. |
| **Maxon** (ZBrush) and **Pixologic** (Sculptris) | Sculpt Mode's brushes, masks, polygroups, DynaMesh and ZRemesher's class of result; Dyntopo / Sculptris Pro. |
| **Sketchbook, Inc.** (SketchBook Pro), **Idyllic Pixel** (PureRef), **the Krita Foundation** | The Draw tab's guides, toolbar and puck; the reference board; the canvas-turning keys. |
| **VRChat Inc.**, **lilxyzw** (lilToon, MIT), **Poiyomi**, **the VRM Consortium** (UniVRM / MToon, MIT), **pixiv** (VRoid), **the MMD community**, **absolute-quantum** (the CATS Blender plugin, MIT), **REDSIM** (VRC Light Volumes) | The avatar's components, the performance ranks and the Avatar window's tools; lilToon's lighting model re-implemented from reading its shader; the material properties read by the converter; VRM and MMD import; Light Volumes. |
| **Nekki** (Cascadeur) | The ideas behind physics for animation: the centre of mass, contacts, ballistic flight, AutoPhysics, AutoPosing. Ideas only. |
| **DreamWorks Animation** (OpenMoonRay, Apache-2.0) | An optional external renderer, run as a program of its own. |
| **Carnegie Mellon University** (the Graphics Lab Motion Capture Database) | The data the inbetweening network and the pose prior were trained on: "The data used in this project was obtained from mocap.cs.cmu.edu. The database was created with funding from NSF EIA-0196217." |
| **Microsoft** (ONNX Runtime, MIT), **NVIDIA** (OptiX, CUDA), **The Khronos Group** (Vulkan, glTF, OpenXR), **Intel**, **Pixar**, **the Academy Software Foundation** and every library's authors | The optional downloads and the libraries listed with their versions and licences in `licenses/README.md`. |
| **Anthropic** (Claude, Claude Code) and **GitHub** (Actions, Releases) | The development environment and the build, test and release machinery. |

## The books and the papers
The Learn tab teaches from *Fundamentals of Computer Graphics* (Marschner and Shirley, 5th ed.) and *Game Engine
Architecture* (Gregory, vol. I and II); the HCI work applies Card, Moran and Newell's keystroke-level model, Nielsen's
heuristics, Fitts's law, Polson's cognitive walkthroughs, Brooke's SUS and the WCAG. The features built from research
papers are listed in `research/FEATURES.md` with each paper's authors; the renderers' and solvers' sources are cited
in the code and in Learn > References: Catmull and Clark, Taubin, Lévy et al., Sheffer et al., Wang et al. (PartUV),
Mikkelsen, Newell, Thiedemann et al., Amanatides and Woo, Dachsbacher and Stamminger, Cook and Torrance, Walter et
al., Heitz, Schlick, Karis, Hill, Heitz, Hill and McGuire, Shirley and Chiu, Cranley and Patterson, Wald, Möller and
Trumbore, Duff et al., Ruppert et al., Dammertz et al., Hosek and Wilkie, Ramamoorthi and Hanrahan, Kang et al.,
Perlin, Meyer et al., Welman, Jakobsen, Kugelstadt and Schömer, Macklin et al., Daviet, Akyürek and Sahillioğlu,
Jimenez, McTaggart, Kajiya and Kay, Motomura (Arc System Works), Brown, Sutherland and Hodgman, Hammersley, Lorensen
and Cline, Shoemake, Phong, Igarashi et al., Chen et al. (3-Sweep), Gryaditskaya et al. and Hähnlein et al., Jacobson
et al., Gibson, Huang et al. (QuadriFlow), Baxter et al. (IMPaSTo), Abdrashitov et al., Oreshkin et al. (ProtoRes),
Okabe and Ito. Thank you.
