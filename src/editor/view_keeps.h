// SPDX-License-Identifier: GPL-2.0-or-later
/* Round 506 (task 0490): what the Scene view's keeps hold between frames - the Editor's `keeps_` member.
 *
 * A record per Editor, so it lives and dies with it (the probe maps it holds are let go with the Editor). Main thread only.
 * The draw-item lists' generations are unique for the process (next_items_gen): one number never names two lists, so a
 * keep that stored one can't be answered by another Editor's. */
#pragma once

#include "../core/view_perf.h"
#include "../render/gpu_raster.h"
#include "../render/raster.h"
#include "../render/reflection_probe.h"

#include <vector>

namespace bl::view_keeps {

/* The list slots the per-list keeps (the refusal check) hold one entry each for: the draw-item lists (3 gathers x tangents) and Solid's.
   A Scene view and a Game view drawing every frame each keep their own. */
constexpr int kListSlots = 8;

/* A walk's record: the scene as it was (as Editor::WalkMark records it), the frame it was made in, and the change counters. */
struct Stamp {
  const void *scene = nullptr;
  uint64_t serial = 0, epoch = 0, frame = 0, structure = 0;
  bool valid = false;
};

/* make_lighting's scene walks: the lights (with their baked mode), the reflection probes, and the sun the world's sky is drawn
 * from. `key` holds what else they are made from (the arguments, Unity's gamma, the shown bake, the probe bakes). `tracked` are
 * the objects the walks read the placement of (lights, probes), with the world matrix they had: a scene whose change counter
 * moved only by transforms (a cube dragged) and whose tracked objects stand where they stood is not walked again. */
struct LightingKeep {
  Stamp stamp;
  uint64_t key = 0;
  std::vector<RenderLight> lights;
  std::vector<RenderProbe> probes;
  struct Tracked {
    uint64_t id;
    float world[16];
  };
  std::vector<Tracked> tracked;
  Vec3 sun{0.35f, 0.75f, -0.55f};
  bool valid = false;
};

/* gpu_view_unsupported's walk over the items: the reason (a string literal, or null) for one list of draw items under one
 * generation of the materials' textures and one answer of "can the card highlight faces". */
struct UnsupportedKeep {
  uint64_t gen = 0, tex_gen = 0;
  bool highlight = false, valid = false;
  const char *why = nullptr;
};

/* instances_key's hash of the items: the key a slot's kept array was stored under, the list and the global key it was made for. */
struct InstanceKeyKeep {
  uint64_t gen = 0, global = 0, key = 0;
  bool valid = false;
};

struct PerEditor {
  LightingKeep lighting[2];  // [rasterized]: the views' and the path tracer's / bakes' own lists
  UnsupportedKeep unsupported[kListSlots];
  InstanceKeyKeep instance_key[gpu::Raster::kSlots];
  uint64_t items_gen[3][2] = {};  // the generation of each kept draw-item list (ItemsCache [variant][tangents])
  uint64_t solid_gen[2] = {};     // ... and of Solid shading's ([game]), with the lights its environment was built with
  std::vector<RenderLight> solid_lights;
  uint64_t solid_lights_gen = 0;
  bool solid_lights_gamma = false;  // (Unity's Lights Gamma the kept lights were made under: it is not in the scene hash)
  /* The list render_deferred / render_solid is working on - the kept one itself, not a copy - its generation and slot; null while
   * no frame is (the keeps read it only for the very vector they are handed). */
  const void *cur_items = nullptr;
  uint64_t cur_gen = 0;
  int cur_slot = 0;
};

/* The list a frame is working on, for the keeps below it (PerEditor::cur_*), put back as it was when the frame is done with it
 * (a render inside another's). */
struct CurrentList {
  PerEditor &p;
  const void *was_items;
  uint64_t was_gen;
  int was_slot;
  CurrentList(PerEditor &pe, const void *items, uint64_t gen, int slot) : p(pe), was_items(pe.cur_items), was_gen(pe.cur_gen), was_slot(pe.cur_slot) {
    p.cur_items = items, p.cur_gen = gen, p.cur_slot = slot;
  }
  ~CurrentList() { p.cur_items = was_items, p.cur_gen = was_gen, p.cur_slot = was_slot; }
  CurrentList(const CurrentList &) = delete;
  CurrentList &operator=(const CurrentList &) = delete;
};

/* A draw-item list's generation: unique for the process, so one number never names two lists. */
inline uint64_t next_items_gen() {
  static uint64_t n = 0;
  return ++n;
}

/* The generation of `items` when it is the list the frame is working on (and kept, so unchanged under that number), else 0. */
inline uint64_t gen_of(const PerEditor &p, const void *items) { return p.cur_items == items ? p.cur_gen : 0; }

}  // namespace bl::view_keeps
