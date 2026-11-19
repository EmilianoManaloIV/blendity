# Workshop fixture provenance

`generate.py` creates the original mannequin, flat floor, two plinth placements and checker textures in
`layout/`. The generator and its generated fixtures are supplied under GPL-2.0-or-later. The installed-title
and Workshop samples in the verification record are separate and stay outside this directory.

From the repository root, regenerate into an empty directory with
`py tests/fixtures/source_workshop/generate.py <scratch-output>` on Windows or substitute `python3` on Linux.
Compare the resulting file tree against `layout/`. The ZIP inside the classic BSP uses fixed timestamps
and a fixed creator system.
`blendity_tests --group source_workshop` reads the checked-in outputs, so running the test requires no
Python, Steam account, game install, network access or native Valve tools.

| Fixture layout | Format coverage |
| --- | --- |
| `steamapps/common/SourceFilmmaker/game/workshop` | Two loose MDL v49 entries with VVD v4 and DX90 VTX v7 companions. BSP v20, v4 static prop records, embedded ZIP with two textured materials and one prop model used twice. VTF 7.2 RGBA8888, VMT cutout and tangent-space normal map. |
| `steamapps/workshop/content/546560/1` | Source 2 VPK v2 with a compiled model and map. Resource header v12, KV3 v1, standalone VBIB, VMAT and RGBA8888 VTEX. |
| `steamapps/workshop/content/730/1` | A multipart VPK v2 with a nested map VPK under `maps/`, an alternate child with a conflicting mesh path, and an additional game VPK mount for the character material. The map's materials come from the outer VPK. |
| `steamapps/workshop/content/730/2` | Two child packages each publish the same map path and different geometry. A truncated child and a child with an unsupported version exercise named mount diagnostics. |

The floor and plinth use different virtual material and texture paths with the same basename `enamel`.
All positions, weights, UVs, joint positions and baked texture pixels come from the generator. The fixture
uses bone names observed in real files so it exercises the same editor Auto IK name matching.

Independent byte checks verified the classic model companion checksums and inverse bind offsets, both BSP
static prop placements, and the embedded ZIP and VPK entry CRCs. ValveResourceFormat 21.1.7798 listed the
Alyx and multipart CS2 directories and read model, mesh, map, world, world node, material and texture blocks
from both layouts. Its exported base color PNGs had the expected cutout and opaque sample pixels. The fixture
VPKs carry entry CRCs and omit whole-package checksums and signatures.

## Format and naming references

- Valve's Source SDK 2013 defines the classic records used here. [Studio model and vertex headers](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/studio.h), [optimized vertex/index records](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/optimize.h), [BSP](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/bspfile.h), [static props](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/gamebspfile.h) and [VTF](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/vtf/vtf.h).
- ValveResourceFormat documents the interpretation of Source 2 header offsets, KV3 streams,
  vertex buffers and world placement records. [Resource header](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/Resource.cs), [binary KV3](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/BinaryKV3.cs), [vertex/index buffers](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/Blocks/VBIB.cs), [skeleton](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/ModelAnimation/Skeleton.cs), [placements](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/Renderer/Renderer/World/WorldNodeLoader.cs), [VMAT](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/Material.cs) and [VTEX](https://github.com/ValveResourceFormat/ValveResourceFormat/blob/8d9f7d9da62a1a0189c6d00050e9653c762656de/ValveResourceFormat/Resource/ResourceTypes/Texture.cs).
- Steam documents how to locate the installed Workshop item after it downloads. ValvePak provides the VPK
  directory entry and archive layout. [Steamworks](https://partner.steamgames.com/doc/features/workshop/implementation#ItemInstallation), [ValvePak revision](https://github.com/ValveResourceFormat/ValvePak/tree/c3add494825b3f9076ebf44550eb1e9b16b3c534).
- Classic uses the ValveBiped naming family and a second model with Scout's bilateral `bip_upperArm`,
  `bip_lowerArm`, `bip_hip` and `bip_knee` joints. Scout's names and parentage came from the raw MDL v49 header
  and bone table. [Scout sample revision](https://github.com/gkjohnson/source-engine-model-loader-models/tree/506af1ce5e6054dc70973e85af1b4f5c08add1c8).
- The AlyxMP avatar and Valve's CS2 SAS agent use `arm_upper`, `arm_lower`, `leg_upper` and `leg_lower` with
  suffixes `_L` and `_R`. The fixtures include non-deforming helper names for the Alyx case. [AlyxMP compiled avatar](https://github.com/Requiem-Software/AlyxMP/blob/84685425f2c1771995c7f269fe1e1c0852a72d5f/mod/game/hlvr/models/alyxmp/avatar.vmdl_c), [Counter-Strike 2 app](https://store.steampowered.com/app/730/CounterStrike_2/).
