## 2026-11-19 (round 601): Choose installed Source Engine content

**Asked:** Choose among models and maps in a local Steam Workshop item. Resolve their dependencies from the item and other content roots.

**Added (task 0583, ADR 0154)**
- File > Import > Source Engine Assets lists model and map paths from an installed folder, loose file, or VPK. The list shows the provider for the selected entry.
- VPKs inside a Workshop archive are listed with their own provider. An inner map can use its outer package’s resources, and a skipped child produces a warning without hiding healthy siblings.
- The resolver searches the map's embedded ZIP first, then the item and any additional roots in order. Add a root after a missing dependency is reported and retry the chosen entry.
- Source conversion stages project assets before publication. Cancel or a publication failure discards fresh files, and explicit reimport reuses edited assets by their virtual identity even when same-named resources change order. Workshop item IDs distinguish otherwise identical entries and other providers need corroborating entry bytes for relocation. Legacy assets remain eligible for migration when they return after an intervening import. The Learn tab walks through choosing an item and its dependencies.

**Checked:** Independent Windows tests passed 899 checks in 18 cases, including failed or canceled preparation, offline native scene reload, reordered same-name assets, distinct Steam items, provider relocation and returning legacy assets. The GCC resolver read the nested map and outer material from CS2 Workshop item 3070193546, matching independent CRCs. The [joined verification record](../docs/tasks/0588-workshop-verification.md) includes the hosted Linux fixture groups and Windows editor stress.

The legacy packed-material test checks that split and embedded images share a per-model directory under the selected import cache. The Linux full-suite run exposed its former assumption that a scratch home always selected the cache location.

<!-- README -->
after: MikuMikuDance characters;
- **Steam Workshop**: [import installed characters and maps](docs/guides/workshop.md) from loose files or VPKs.

<!-- docs/USABILITY.md -->
after: | A Godot project's scenes couldn't be opened here
| A Workshop item can hold multiple models and maps, with dependencies in a separate item or game folder. (round 601) | File > Import > Source Engine Assets lists the item's virtual paths and providers and lets you add roots for missing dependencies. |

<!-- docs/ARCHITECTURE.md -->
after: | `scene/import.*` | OBJ + MTL and FBX
| `scene/source_content.*` | Ordered, read-only virtual content mounts for loose Source files, multipart and nested VPKs, and BSP ZIPs. Archive members stay virtual. | Source SDK 2013, VPK v1/v2, PKWARE ZIP APPNOTE | Workshop content resolver |
