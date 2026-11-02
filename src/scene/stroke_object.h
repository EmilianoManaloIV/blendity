// SPDX-License-Identifier: GPL-2.0-or-later
// Stroke objects (ADR 0084, task 0248): a brush that paints geometry. A Strokes component keeps a list of cards (one
// quad each, facing the camera at its creation) and ribbons (a drag's points swept into a strip); its object's mesh is
// rebuilt from the list on every edit, with custom normals - the card's own, the surface's at the dab, or the direction
// from a canopy centre (the trick that makes a bush of flat cards shade as one round volume). Renderers and exporters
// see a plain mesh.
#pragma once

#include "scene.h"

namespace bl {

struct StrokeCard {
  Vec3 center, facing, up;  // the quad's centre, its own normal (toward the camera at creation), its vertical
  Vec3 surface_normal;      // the surface's normal under the dab (Normals: Surface)
  float size = 0.2f;        // the quad's side in metres
};

struct StrokeRibbon {
  std::vector<Vec3> points;  // the drag's points on the surface, in the object's space
  Vec3 facing;               // the strip's normal (toward the camera at the first point)
  Vec3 surface_normal;       // the surface's normal at the first point
  float width = 0.05f;
  /* Round 385 (task 0362: sketching in VR), each optional - a stroke drawn in the air: */
  std::vector<float> widths;  // a width a point (the trigger's pressure); empty: `width` everywhere
  std::vector<Vec3> ups;      // the strip's normal a point (the controller's turn); empty: `facing` everywhere
  bool tube = false;          // a round tube (kTubeSides around, the width its diameter, capped) instead of a strip
  int material = 0;           // the material slot its faces use (a palette colour)
  float width_at(size_t i) const { return i < widths.size() ? widths[i] : width; }
  Vec3 up_at(size_t i) const { return i < ups.size() ? ups[i] : facing; }
};
constexpr int kTubeSides = 8;
constexpr int kMaxStrokeSlot = 255;  // (a ribbon's material slot is at most this)

struct Strokes : ComponentBase<Strokes> {
  static constexpr const char *kName = "Strokes";
  std::vector<StrokeCard> cards;
  std::vector<StrokeRibbon> ribbons;
  int normals = 0;  // 0 Own (each card its facing), 1 Surface (the surface's normal under the dab), 2 Canopy (from the centre)
  Vec3 canopy;      // the canopy centre, in the object's space
  void reflect(Reflector &r) override;
  /* The mesh from the list: a quad a card, a strip a ribbon, a UV square each (0..1 across a card; along a ribbon u
   * runs with its length), custom normals by `normals`; a tube a ring of kTubeSides a point, its normals out from the
 * path, with caps. Each ribbon's faces on its material slot. Empty for no strokes. */
  MeshPtr build_mesh() const;
  /* Round 385 (review): `prev` (this list's mesh without its last ribbon) with the last ribbon added - what build_mesh
   * gives, without building every stroke again when one is drawn. */
  MeshPtr with_last_ribbon(const Mesh &prev) const;
  void append_ribbon(Mesh &m, std::vector<Vec3> &custom, const StrokeRibbon &rb) const;
  Vec3 shade(Vec3 own, Vec3 surface, Vec3 at) const;  // the custom normal by `normals`
  size_t stroke_count() const { return cards.size() + ribbons.size(); }
};

/* The cards' and ribbons' text forms (the scene file; one stroke a line). */
std::string strokes_to_text(const Strokes &s);
bool strokes_from_text(const std::string &text, Strokes &s);

}  // namespace bl
