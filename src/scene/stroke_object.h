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
};

struct Strokes : ComponentBase<Strokes> {
  static constexpr const char *kName = "Strokes";
  std::vector<StrokeCard> cards;
  std::vector<StrokeRibbon> ribbons;
  int normals = 0;  // 0 Own (each card its facing), 1 Surface (the surface's normal under the dab), 2 Canopy (from the centre)
  Vec3 canopy;      // the canopy centre, in the object's space
  void reflect(Reflector &r) override;
  /* The mesh from the list: a quad a card, a strip a ribbon, a UV square each (0..1 across a card; along a ribbon u
   * runs with its length), custom normals by `normals`. Empty for no strokes. */
  MeshPtr build_mesh() const;
  size_t stroke_count() const { return cards.size() + ribbons.size(); }
};

/* The cards' and ribbons' text forms (the scene file; one stroke a line). */
std::string strokes_to_text(const Strokes &s);
bool strokes_from_text(const std::string &text, Strokes &s);

}  // namespace bl
