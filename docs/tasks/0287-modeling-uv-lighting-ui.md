# Task 0287 - Modeling, UV and lighting verbs without the console (round 307)

Part of the console-only-to-UI plan (tasks 0271-0273, 0286-0290). Every control calls the code the console calls.

## What
| Verb | Now reached by |
| --- | --- |
| `meshop mirror_y` | Mesh Tools: Mirror Y (beside Mirror X and Z) |
| `customnormals clear` | Mesh Tools: Clear Custom Normals |
| `refimage <path> side / top` | GameObject > Reference Image > Front / Side / Top, each listing Browse... and the project's images (a headless or dialog-less Linux run still has the list) |
| `aimlight x y z` | Inspector, a Light: Aim X / Y / Z fields and "Aim at These Numbers" under the eyedropper |
| `guide px py pz dx dy dz` | Edit Mode tools: Guide Through X / Y / Z, Guide Direction (X, Y, Z) and Add Guide Line |
| `wpclean <threshold>` | Weight Paint: Clean Below (Clean used 0.01 always) |
| `uv material +<name>` | the UV editor's material list: a check per slot, several at once; All materials clears it |
| `uv others dim / hide` | the same list: Others Dimmed / Others Hidden |
| `uv parts <bound> multi / single`, `uv parts semantic on / off` | the UV editor's Parts button: the stretch bound (1.1, 1.25, 1.5, 2), Several Tiles (UDIM), Part Tree from PartField |

Left out: a guide in any direction (the axes cover the construction lines artists ask for; the console keeps the
free direction, and Ctrl+click along an edge already lays one along an edge).

## Acceptance
- `tests_round307` (tests/test_editor.cpp) by real clicks: Mirror Y doubles the cube's faces; the Side reference is
  turned 90 degrees from the Front one; the light's forward becomes the direction to the typed point; Add Guide Line
  adds one along X; Clean is clicked with its threshold drawn; two slots picked and the others hidden; Parts sets the
  bound to 1.5 and several tiles.
- The whole suite in the dependency-free non-LTCG build; the chain on main.
