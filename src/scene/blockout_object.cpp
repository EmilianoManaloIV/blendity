// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): the two components' fields and the snapshot the kernel is given.
#include "blockout_object.h"
#include "blockout_piece_stack.h"  // (round 514: a piece's modifier stack)

#include "../core/hash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bl {

const char *const kBlockoutShapeNames[8] = {"Box", "Cylinder", "Sphere", "Wedge", "Stairs", "Arch", "Outline", "Mesh"};
const char *const kBlockoutOpNames[3] = {"Add", "Subtract", "Intersect"};
const char *const kBlockoutMethodNames[3] = {"Auto", "Exact", "Field"};
const char *const kBlockoutBlendDetailNames[3] = {"Low", "Medium", "High"};
const char *const kBlockoutWallNames[2] = {"Merge with neighbours", "Keep walls"};
const char *const kBlockoutOpeningNames[3] = {"None", "Door", "Window"};
const char *const kBlockoutClassNames[kBlockoutClasses] = {"Floor", "Walls", "Ceiling", "Outside", "Cut", "Solid", "Glass", "Leaf", "Roof"};
const char *const kBlockoutWindowStyleNames[kBlockoutWindowStyles] = {"Opening Only", "Single", "Double", "Arched", "Fixed", "Grid", "Round"};
const char *const kBlockoutDoorStyleNames[kBlockoutDoorStyles] = {"Opening Only", "Single", "Double", "Sliding", "Arched", "Revolving"};
const char *const kBlockoutSideNames[2] = {"Left", "Right"};

int blockout_door_style_from_word(const std::string &word) {
  std::string w;
  for (char c : word) w += (char)std::tolower((unsigned char)c);
  if (w == "opening" || w == "only" || w == "none" || w == "hole" || w == "openingonly") return kDoorOpeningOnly;
  if (w == "single") return kDoorSingle;
  if (w == "double") return kDoorDouble;
  if (w == "sliding" || w == "slide") return kDoorSliding;
  if (w == "arched" || w == "arch") return kDoorArched;
  if (w == "revolving" || w == "revolve" || w == "drum") return kDoorRevolving;
  return -1;
}

const char *const *blockout_style_names(int opening, int &count) {
  count = opening == 1 ? kBlockoutDoorStyles : kBlockoutWindowStyles;
  return opening == 1 ? kBlockoutDoorStyleNames : kBlockoutWindowStyleNames;
}

int blockout_style_from_word(int opening, const std::string &word) {
  return opening == 1 ? blockout_door_style_from_word(word) : opening == 2 ? blockout_window_style_from_word(word) : -1;
}

const char *blockout_style_words(int opening) {
  return opening == 1 ? "opening|single|double|sliding|arched|revolving" : "opening|single|double|arched|fixed|grid|round";
}

int blockout_window_style_from_word(const std::string &word) {
  std::string w;
  for (char c : word) w += (char)std::tolower((unsigned char)c);
  if (w == "opening" || w == "only" || w == "none" || w == "hole" || w == "openingonly") return kWindowOpeningOnly;
  if (w == "single") return kWindowSingle;
  if (w == "double") return kWindowDouble;
  if (w == "arched" || w == "arch") return kWindowArched;
  if (w == "fixed") return kWindowFixed;
  if (w == "grid") return kWindowGrid;
  if (w == "round" || w == "porthole") return kWindowRound;
  return -1;
}

namespace {

const char *const kMaterialFieldNames[kBlockoutClasses] = {"Floor Material", "Walls Material", "Ceiling Material", "Outside Material", "Cut Material", "Solid Material", "Glass Material",
                                                           "Leaf Material", "Roof Material"};
const char *const kMaterialHelp[kBlockoutClasses] = {
    "The material of every room's floor (a .mat from the project; none: the default material). A piece's own Floor Material wins over it.",
    "The material of the inside of every room's walls.",
    "The material of every room's ceiling.",
    "The material of the outer faces of every room's walls, floor and ceiling.",
    "The material of the faces a Subtract piece cut (a door's or window's reveal, a pocket's sides), and of what an Intersect piece trimmed.",
    "The material of the faces of a plain Add piece that is no room, and of a styled window's or door's frame, bars and sill board (and a Revolving door's ring). A face made of several pieces' faces in one plane takes the material of the first of them.",
    "The material of a styled window's pane (a thin sheet in its light, when its Glass is on).",
    "The material of a styled door's leaf object (one flat box, or the Wings of a Revolving door): the leaf takes it, not the Blockout's mesh.",
    "The material of the top faces of a Roof preset (the faces that look up; its underside, its gable ends and its eave's edge are Solid)."};

float safe_scale(float v) { return finite_bits(v) && v > 0.0f ? std::min(1000.0f, std::max(0.001f, v)) : 1.0f; }

/* The materials of a Blockout, as its reflect and the baked record's show them. */
void reflect_materials(Reflector &r, std::string *materials, int only_classes_mask) {
  for (int k = 0; k < kBlockoutClasses; k++) {
    if (!(only_classes_mask & (1 << k))) continue;
    r.asset(kMaterialFieldNames[k], materials[k], ".mat", "Material");
    r.help(kMaterialHelp[k]);
  }
}

}  // namespace

float Blockout::safe_texture_scale() const { return safe_scale(texture_scale); }

void Blockout::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help("The grid step, in metres, of the field path: every cluster of pieces in a build without Blender's libraries, and the\n"
         "blended ones later. A smaller cell is truer to the pieces and slower.");
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.help("Auto: exact where the build has it (Blender's libraries), else the field path. Exact: the exact boolean only - a build\n"
         "without Blender's libraries builds nothing and the status says why. Field: the field path for every cluster.");
  r.enumeration("Blend Detail", blend_detail, kBlockoutBlendDetailNames, 3);
  r.help("How finely a piece's Blend Radius is meshed: Low a third of the radius, Medium a sixth, High a twelfth (never coarser than Field Cell). "
         "Finer is rounder and slower. A radius below a thirty-second of Field Cell is built unblended and the status says so.");
  r.field("Default Wall Thickness", default_wall, 0.01f, 0.0f, 100.0f);
  r.help("What a new room's Wall, Floor and Ceiling start at, in metres. Rooms already made keep theirs.");
  r.field("Texture Scale", texture_scale, 0.01f, 0.001f, 1000.0f);
  r.help("How many metres a texture tile covers on every face (each face is projected on its own plane - a turned wall and a ramp's slope too): a wall\n"
         "and a floor of different pieces show a texture at one size, its pattern runs on across pieces that meet in a plane, and it stays put when a piece moves.");
  reflect_materials(r, material, (1 << kBlockoutClasses) - 1);
  if (r.serializing()) r.field("Slots Set", slots_set);  // (the record that the renderer's materials follow the build, not for hand editing)
}

void BlockoutBaked::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help((std::string("What Unbake gives the Blockout back. The pieces are under the disabled child '") + kBlockoutPiecesName + "'.").c_str());
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.enumeration("Blend Detail", blend_detail, kBlockoutBlendDetailNames, 3);
  r.field("Default Wall Thickness", default_wall, 0.01f, 0.0f, 100.0f);
  r.field("Texture Scale", texture_scale, 0.01f, 0.001f, 1000.0f);
  reflect_materials(r, material, (1 << kBlockoutClasses) - 1);
  if (!r.serializing()) return;  // (the records of the bake, not for hand editing)
  r.text("Baked Mesh", mesh_hash);
  r.text("Pieces Id", pieces_id);
  r.field("Slots Set", slots_set);
}

blockout::Settings Blockout::settings() const {
  blockout::Settings s;
  s.cell = finite_bits(cell) ? std::min(10.0f, std::max(0.01f, cell)) : 0.1f;
  s.path = (blockout::Path)std::min(2, std::max(0, method));
  s.blend_detail = std::min(2, std::max(0, blend_detail));
  return s;
}

namespace {

/* Which shapes have an inside to walk in: box, cylinder, sphere, wedge (a sloped ceiling), outline. */
bool shape_can_be_room(int shape) { return shape == 0 || shape == 1 || shape == 2 || shape == 3 || shape == 6; }

}  // namespace

/* A window's Style and the numbers its style reads (round 498): the rows another style does not use stay out of the Inspector. */
static void reflect_window_rows(Reflector &r, BlockoutPiece &p, bool all, int st) {
  if (!all && st == kWindowOpeningOnly) return;
  r.field("Frame Width", p.frame_width, 0.005f, kBlockoutMinSize, 1000.0f);
  r.help("The frame's width round the light, in metres; glazing bars are half of it, a Double's mullion all of it. The frame and the bars take at most seven tenths of the width and of the height, so a light is always left.");
  r.field("Frame Depth", p.frame_depth, 0.005f, kBlockoutMinSize, 1000.0f);
  r.help("How deep the frame (and the pane inside it) stands, centred in the window's Depth; at most the Depth.");
  if (all || st == kWindowGrid) {
    r.field("Panes Across", p.panes_across, 1, 12);
    r.help("Lights side by side, divided by vertical glazing bars (1 to 12).");
    r.field("Panes Up", p.panes_up, 1, 12);
    r.help("Lights one above the other, divided by horizontal glazing bars (1 to 12).");
  }
  if (all || st == kWindowArched) {
    r.field("Arch Rise", p.arch_rise, 0.01f, 0.0f, 1000.0f);
    r.help("How high the round head rises above the line it springs from, in metres: 0 is a half circle (half the Width). The head ends at the window's Head.");
  }
  r.field("Glass", p.glass);
  r.help("A thin pane (thinner than the frame) closes the light. Off: an open light.");
  if (all || (st != kWindowFixed && st != kWindowRound)) {
    r.field("Sill Board", p.sill_board);
    r.help("A board under the window that stands out past the wall on both faces. (Fixed and Round windows have none.)");
    r.field("Sill Overhang", p.sill_overhang, 0.005f, 0.0f, 1000.0f);
    r.help("How far the sill board reaches past the opening's sides and past the wall's two faces, in metres (0: flush with them).");
  }
}

/* A styled door's rows (round 499): only those the style reads. Frame Width, Frame Depth and Arch Rise are the window's rows, shared. */
static void reflect_door_rows(Reflector &r, BlockoutPiece &p, bool all, int st) {
  if (!all && st == kDoorOpeningOnly) return;
  const bool revolving = st == kDoorRevolving;
  if (all || !revolving) {
    r.field("Frame Width", p.frame_width, 0.005f, kBlockoutMinSize, 1000.0f);
    r.help("The frame's width round the opening (the two jambs and the head; there is no sill), in metres. It takes at most seven tenths of the width and of the height, so an opening is always left.");
    r.field("Frame Depth", p.frame_depth, 0.005f, kBlockoutMinSize, 1000.0f);
    r.help("How deep the frame (and a swung leaf's hinge) stands, centred in the door's Depth; at most the Depth.");
  }
  if (all || st == kDoorArched) {
    r.field("Arch Rise", p.arch_rise, 0.01f, 0.0f, 1000.0f);
    r.help("How high the round head rises above the line it springs from, in metres: 0 is a half circle (half the Width). The head ends at the door's Head. An Arched door's leaf is square: it stands up to the line the round head springs from, and the round head above it stays open.");
  }
  /* (Round 515: the leaf is an object under the door. For a Revolving door the Leaf row is its wings, `wings` - off by default: the drum alone is the blocked-out space.) */
  if (all || !revolving) {
    r.field("Leaf", p.leaf);
    r.help("The door's leaf: its own object under the door (an object with a mesh, pivot at the hinge) that Open Amount - or the Rotate tool - swings without rebuilding the wall. Off: no leaf object, an open doorway. The frame and the doorway are in the Blockout's mesh whatever the Leaf says; a closed leaf is not part of the lightmap bake.");
  }
  if (!all && revolving) {
    r.field("Leaf", p.wings);
    r.help("The Wings: four vanes in a cross, one object under the door, turning about the drum's axis with Open Amount (1 is a quarter turn). Off (the default): the drum is only the blocked-out space of a revolving door.");
  }
  if (all || p.leaf_on()) {
    r.field("Leaf Thickness", p.leaf_thickness, 0.005f, kBlockoutMinSize, 1000.0f);
    r.help("How thick the leaf (or each vane) is, through the wall, in metres. A leaf that swings in the frame is held to six tenths of the Frame Depth; a Sliding leaf on the wall's face may be as thick as you like.");
  }
  if (all || p.leaf_on()) {
    r.field("Open Amount", p.open_amount, 0.01f, 0.0f, 1.0f);
    r.help("0 closed to 1 open: a Single, Double or Arched leaf swings that fraction of 90 degrees about its hinge, into the room the door faces (its +Z side); a Sliding leaf slides that fraction of its own width along the wall; the Wings turn that fraction of a quarter turn. "
           "Only the leaf object moves - the Blockout's mesh is not built again, and nothing in front of the wall is cut. Turning a leaf with the Rotate tool writes the angle back here.");
  }
  if (all || ((st == kDoorSingle || st == kDoorArched) && p.leaf)) {
    r.enumeration("Hinge Side", p.hinge_side, kBlockoutSideNames, 2);
    r.help("Which jamb a Single (or Arched) leaf hangs on. Left: the hinge is on your left when you stand in the room the door opens into, facing the door (its +X side); Right is the other jamb. (A Double hangs one on each.)");
  }
  if (all || (st == kDoorSliding && p.leaf)) {
    r.enumeration("Slide Direction", p.slide_direction, kBlockoutSideNames, 2);
    r.help("Which way a Sliding leaf slides along the wall as Open Amount grows. Left: toward your left when you stand in the room the door opens into, facing the door (its +X side); Right is the other way.");
  }
  if (all || revolving) {
    r.field("Drum Radius", p.drum_radius, 0.01f, kBlockoutMinSize, 1000.0f);
    r.help("A Revolving door is the blocked-out space: a round drum of this radius cut through the wall, and a ring one wall-thickness thick round it that stands out of both faces of the wall. Its Wings (Leaf on) are a separate object inside the drum.");
  }
}

/* A window's or door's Style and the numbers its style reads (round 498; doors round 499): the rows another style does not use stay out of the Inspector. The Style
 * is one number, so a file keeps it once (`all`: the window's words, the widest range - a load holds it to what it is). */
static void reflect_opening_style(Reflector &r, BlockoutPiece &p, bool all) {
  const bool door = p.opening == 1 && !all;
  if (door) {
    r.enumeration("Style", p.style, kBlockoutDoorStyleNames, kBlockoutDoorStyles);
    r.help("What the door adds to the wall besides the hole. Opening Only: nothing (a plain cut). Single: a frame and one leaf hinged at one jamb. Double: a leaf on each jamb. "
           "Sliding: a leaf on the wall's face that slides along it. Arched: a Single with a round head (its square leaf stands up to where the head springs). Revolving: the blocked-out space - a round drum "
           "cut through the wall and a ring round it (Width is the two entries through the ring, at most eight tenths of the drum's width). A style brings its own size when it is chosen "
           "(Double 1.8 m wide: two 0.9 m leaves; Revolving 1.2 m) when the Width and Head are still the old style's; a size you set stays. The frame and a Revolving door's ring take the Solid material, the leaf the Leaf material, the hole's reveal the Cut material.");
    reflect_door_rows(r, p, false, std::min(kBlockoutDoorStyles - 1, std::max(0, p.style)));
    return;
  }
  r.enumeration("Style", p.style, kBlockoutWindowStyleNames, kBlockoutWindowStyles);
  r.help("What the window adds to the wall besides the hole. Opening Only: nothing (a plain cut). Single: a frame, a pane and a sill board. Double: two lights "
         "with a mullion (1.5 m wide against the others' 1.2 m: choosing a style gives its size while the Width and Head are still the old style's, a size you set stays; a Round window keeps its size when you switch away). Arched: a round head. Fixed: a frame and one pane, no sill board. Grid: Panes Across x Panes Up with glazing bars. Round: a porthole - a circle "
         "when it is made (Head = Sill + Width); change Width or Head afterwards and it is an ellipse. "
         "The frame, bars and sill board take the Solid material, the pane the Glass material, the hole's reveal the Cut material.");
  reflect_window_rows(r, p, all, std::min(kBlockoutWindowStyles - 1, std::max(0, p.style)));
  if (all) {  // (the door's own numbers: one row each, the window's shared ones above)
    r.field("Leaf", p.leaf);
    r.field("Wings", p.wings);  // (round 515: a Revolving door's Leaf row; a file of round 499 has none and loads with the drum alone)
    r.field("Leaf Thickness", p.leaf_thickness, 0.005f, kBlockoutMinSize, 1000.0f);
    r.field("Open Amount", p.open_amount, 0.01f, 0.0f, 1.0f);
    r.enumeration("Hinge Side", p.hinge_side, kBlockoutSideNames, 2);
    r.enumeration("Slide Direction", p.slide_direction, kBlockoutSideNames, 2);
    r.field("Drum Radius", p.drum_radius, 0.01f, kBlockoutMinSize, 1000.0f);
  }
}

/* A preset's own numbers (round 500): only the rows of the preset in use (`all`: every row, for the file). Radius, Height, Segments, Steps, Fill Under, Inner Radius, Size, Depth and the
 * outline are the shapes' rows, shown by BlockoutPiece::reflect for the presets that read them. Ridge Height is not a stored number: it is worked out from Pitch (and sets it). */
static void reflect_preset_rows(Reflector &r, BlockoutPiece &p, bool all) {
  const int k = p.preset;
  const bool column = k == kPresetColumn || k == kPresetPilaster, stairs = k == kPresetStairsL || k == kPresetStairsU;
  if (all || k == kPresetColumn) {
    r.field("Square Shaft", p.square);
    r.help("A square shaft instead of a round one (a Pilaster's is always square).");
  }
  if (all || column) {
    r.field("Base Height", p.base_height, 0.01f, 0.0f, 10000.0f);
    r.help("The plinth under the shaft, in metres (0: none). A plinth and a capital together take at most nine tenths of the height.");
    r.field("Capital Height", p.capital_height, 0.01f, 0.0f, 10000.0f);
    r.help("The block on top of the shaft, in metres (0: none).");
    r.field("Base Width", p.base_width, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("How wide the plinth is across (a square block; never narrower than the shaft). A Pilaster's stands out half as far as it is wide.");
    r.field("Capital Width", p.capital_width, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("How wide the capital is across (never narrower than the shaft).");
    r.field("Taper", p.taper, 0.01f, 0.05f, 2.0f);
    r.help("The shaft's width at its top as a share of its width at the bottom: 1 is straight, 0.8 narrows by a fifth. Held to 0.05 .. 2.");
  }
  if (all || k == kPresetRailing || k == kPresetRoof || stairs) {
    r.field("Length", p.length, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help(k == kPresetRailing ? "The run of the railing along X, in metres, when it has no outline (with one, it runs along each side of the outline)."
           : k == kPresetRoof  ? "The roof's length along X - the way the ridge runs - not counting the overhang."
                               : "How long each flight runs, in metres.");
  }
  if (all || k == kPresetRailing) {
    r.field("Post Spacing", p.post_spacing, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("The most the posts stand apart: they stand at both ends and evenly between, so 5 m at 1 m is 6 posts (at most 256 a side, 1024 a railing).");
    r.field("Post Size", p.post_size, 0.005f, kBlockoutMinSize, 10000.0f);
    r.help("A post's width, square.");
    r.field("Rail Height", p.rail_height, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("The top of the top rail above the piece's origin.");
    r.field("Rail Size", p.rail_size, 0.005f, kBlockoutMinSize, 10000.0f);
    r.help("The rails' thickness, square.");
    r.field("Bottom Rail", p.bottom_rail);
    r.help("A second rail low between the posts.");
  }
  if (all || k == kPresetRoof) {
    r.enumeration("Roof Style", p.roof_style, kBlockoutRoofStyleNames, kBlockoutRoofStyles);
    r.help("Gable: two slopes meeting at a ridge along X. Hip: four slopes. Shed: one slope rising towards +Z. Over an outline with square corners the roof is the union of one roof over each of its rectangles.");
    r.field("Pitch", p.pitch, 0.5f, 0.0f, 85.0f);
    r.help("The slope in degrees (0 to 85). Ridge Height is worked out from it, and setting Ridge Height sets it.");
    if (!r.serializing()) {
      float ridge = blockout_roof_ridge(p);
      const float before = ridge;
      r.field("Ridge Height", ridge, 0.01f, 0.0f, 10000.0f);
      r.help("How high the roof rises over its base: the Pitch's tangent times the half span (a Hip: the nearer half side; a Shed: the whole span). Setting it sets Pitch.");
      if (ridge != before) blockout_roof_set_ridge(p, ridge);
    }
    r.field("Overhang", p.overhang, 0.01f, 0.0f, 10000.0f);
    r.help("How far the roof's base reaches past the Length and the Span on every side; the pitch starts at its edge.");
    r.field("Thickness", p.thickness, 0.01f, 0.0f, 10000.0f);
    r.help("The roof sheet's thickness, measured straight down. 0: a solid roof.");
  }
  if (all || k == kPresetRoof || k == kPresetArcade) {
    r.field("Span", p.span, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help(k == kPresetRoof ? "The roof's span across (Z), not counting the overhang." : "The clear width of one arch, between two columns.");
  }
  if (all || stairs || k == kPresetSpiral) {
    r.field("Total Height", p.total_height, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("The height the last step's tread reaches. Stairs L and U climb half of it in each flight, with the landing at the middle.");
  }
  if (all || stairs) {
    r.field("Flight Width", p.stair_width, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("The width of the flights and the landing.");
  }
  if (all || k == kPresetSpiral) {
    r.field("Turn", p.turn, 1.0f, -1440.0f, 1440.0f);
    r.help("How far the steps turn round the post, in degrees (negative: the other way; at most 170 degrees a step).");
  }
  if (all || k == kPresetArcade) {
    r.field("Count", p.count, 1, 256);
    r.help("How many arches: they stand on Count + 1 columns.");
    r.field("Column Width", p.column_width, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("The width of a column along X.");
    r.field("Arch Thickness", p.arch_thickness, 0.01f, kBlockoutMinSize, 10000.0f);
    r.help("How wide the arch's ring is at its springing (at most the column's width).");
    if (!all) {  // (the window's and door's Arch Rise is the same number: the file has it once)
      r.field("Arch Rise", p.arch_rise, 0.01f, 0.0f, 10000.0f);
      r.help("How high the arch rises above its springing line: 0 is a half circle (half the Span).");
    }
  }
}

bool BlockoutPiece::is_room() const { return room && opening == 0 && shape_can_be_room(shape) && !is_preset(); }

void BlockoutPiece::reflect(Reflector &r) {
  const bool all = r.all_fields();
  const int k = std::min(7, std::max(0, shape));
  const bool is_opening = opening != 0, is_room = this->is_room();
  const bool room_shape = shape_can_be_room(k);
  const bool pre = is_preset();  // (round 500: a preset's rows - its own numbers and the shared ones it reads - replace the shape's)
  const int pk = pre ? preset : 0;
  const auto preset_is = [&](std::initializer_list<int> kinds) { for (int v : kinds) if (pk == v) return true; return false; };
  if (all || !is_opening) {
    r.enumeration("Preset", preset, kBlockoutPresetNames, kBlockoutPresets);
    r.help("An architectural piece that is one object whose numbers make its solids: a Column, Pilaster, Beam, Railing, Roof, Stairs with a landing (L or U), Spiral Stairs or Arcade. None: an ordinary shape. "
           "Changing it keeps the numbers the new preset shares with the old one (Radius, Height, Steps ...): the other rows start at what they were.");
  }
  if (all || (!is_opening && !pre)) {  // (an opening is a box that cuts: its shape and operation are not its to choose)
    r.enumeration("Shape", shape, kBlockoutShapeNames, 8);
    r.help("What the piece is: a box, a cylinder (up Y) or a sphere centred on the object; a wedge (a ramp: tall at -Z, sloping down to +Z), "
           "stairs (climbing towards +Z), an arch (standing in the XY plane), an outline (corners in the XZ plane, pulled up) or any mesh of the "
           "scene. A room's shape is its air.");
    r.greyed(is_room);
    r.enumeration("Operation", operation, kBlockoutOpNames, 3);
    r.help(is_room ? "A room's Operation is ignored: a room is its walls with its air cut out (Walls says whether they merge with its neighbours')."
                   : "Add joins the piece to the Blockout's mesh; Subtract cuts it out of everything added (a door, a window, a pocket); "
                     "Intersect keeps only what is inside it (two of them: what is inside both).");
    r.greyed(false);
    if (all || room_shape) {  // (stairs, an arch and a mesh have no inside to walk in)
      r.field("Room", room);
      r.help("A room: the shape is the air you walk in, and the piece brings its own walls, floor and ceiling. Rooms that overlap become one "
             "space, the wall between them gone where their air overlaps. A wedge's room has a sloped ceiling; an outline's walls are as thick "
             "all the way round.");
    }
  }
  if (all || is_room) {
    r.field("Wall", wall, 0.01f, 0.0f, 1000.0f);
    r.help("The wall's thickness, in metres in the Blockout's space, measured square to the room's own sides whatever the room's scale (up to "
           "1000 m; a cylinder's wall is measured at its facets' middles, and an elliptical cylinder's varies between its two axes). 0 on all three "
           "makes a room with no matter of its own: it only clears the rooms it overlaps.");
    r.field("Floor", floor, 0.01f, 0.0f, 1000.0f);
    r.help("The floor's thickness, below the air (metres).");
    r.field("Ceiling", ceiling, 0.01f, 0.0f, 1000.0f);
    r.help("The ceiling's thickness, above the air (metres). Open Top leaves it out.");
    r.enumeration("Walls", walls, kBlockoutWallNames, 2);
    r.help("Merge with neighbours: every room's air clears this room's walls, so rooms that overlap are one space. Keep walls: only this "
           "room's own air clears its walls - it keeps all of them inside another room.");
    r.field("Open Top", open_top);
    r.help("No ceiling: the walls end flat at the air's top.");
  }
  if (all || (!is_room && !pre)) {
    r.enumeration("Opening", opening, kBlockoutOpeningNames, 3);
    r.help("A door or a window: a box that cuts through the wall it stands in, sized by Width, Head and Sill. None: an ordinary piece.");
  }
  if (!all && pk == kPresetArcade) {
    r.field("Depth", depth, 0.01f, kBlockoutMinSize, 1000.0f);
    r.help("How deep the arcade is, along Z: the columns' and the arches' depth.");
  }
  if (all || is_opening) {
    r.field("Width", width, 0.01f, 0.0f, 1000.0f);
    r.help("Across the opening, in metres before the object's own scale.");
    r.field("Head", head, 0.01f, 0.0f, 1000.0f);
    r.help("The top of the opening above the floor line the piece stands on.");
    r.field("Sill", sill, 0.01f, 0.0f, 1000.0f);
    r.help("The bottom of the opening above the floor line: 0 for a door, about 0.9 m for a window.");
    r.field("Depth", depth, 0.01f, 0.0f, 1000.0f);
    r.help("Through the wall: the wall it was put in plus 0.1 m either side, so it always goes through (Depth = the wall's thickness + 0.2 m). A window's sill board reaches out from the wall's face, 0.1 m in from each end of the Depth, and Scale grows the wall part only.");
  }
  if (all || opening == 1 || opening == 2) reflect_opening_style(r, *this, all);
  if (all || pre) reflect_preset_rows(r, *this, all);
  const bool shaped = all || !is_opening;
  if (shaped && (all || (!pre && (k == 0 || k == 3 || k == 4)) || pk == kPresetBeam)) {
    r.field("Size", size);
    r.help(pk == kPresetBeam ? "A beam's Length along X, Depth (how tall it stands, up from the piece's origin) and Width across (Z), before the object's own scale."
         : k == 3 ? "Width (X), height (Y) at the tall end and length (Z) of the ramp, before the object's own scale."
                  : k == 4 ? "Width (X), height (Y) and length (Z) of the flight, before the object's own scale."
                           : "Width (X), height (Y) and depth (Z), before the object's own scale.");
  }
  if (shaped && (all || (!pre && (k == 1 || k == 2 || k == 5)) || preset_is({kPresetColumn, kPresetPilaster, kPresetSpiral}))) {
    r.field("Radius", radius, 0.01f, 0.0001f, 10000.0f);
    if (pre) r.help(pk == kPresetSpiral ? "The steps' outer radius." : "The shaft's half-width at the bottom of the shaft (a round shaft's radius, a square one's half side).");
    else if (k == 5) r.help("The arch's outer radius.");
  }
  if (shaped && (all || (!pre && (k == 1 || k == 5 || k == 6)) || preset_is({kPresetColumn, kPresetPilaster, kPresetArcade}))) {
    r.field("Height", height, 0.01f, 0.0001f, 10000.0f);
    if (pre) r.help(pk == kPresetArcade ? "How tall the columns are, up to where the arches spring." : "The whole height of the column, plinth and capital included.");
    else if (k == 5) r.help("How deep the arch is, along Z.");
    else if (k == 6) r.help("How far the outline is pulled up, along Y.");
  }
  if (shaped && (all || (!pre && (k == 1 || k == 2 || k == 5)) || preset_is({kPresetColumn, kPresetArcade}))) {
    r.field("Segments", segments, 3, 256);
    r.help(pre ? "Sides of a round shaft (a tapered one has at most 32); an arcade's arches are cut into half as many blocks." : k == 1 ? "Sides of the cylinder." : k == 5 ? "Blocks the arch is cut into (at least one for each quarter turn it spans)." : "Segments round the sphere.");
  }
  if (shaped && (all || k == 2)) {
    r.field("Rings", rings, 2, 128);
    r.help("Rings from pole to pole.");
  }
  if (shaped && (all || (!pre && k == 4) || preset_is({kPresetStairsL, kPresetStairsU, kPresetSpiral}))) {
    r.field("Steps", steps, 1, 256);
    r.help(pre ? "How many steps each flight (or the whole spiral) climbs in: 1 to 256." : "How many steps the flight climbs in.");
    r.field("Fill Under", fill_under);
    r.help(pre ? "On: the steps (and the mass a second flight stands on) go down to the ground. Off: a slab under the treads." : "On: each step stands on the ground. Off: a flight with a slab under its treads.");
  }
  if (shaped && (all || (!pre && k == 5) || pk == kPresetSpiral)) {
    r.field("Inner Radius", inner_radius, 0.01f, 0.0f, 10000.0f);
    r.help(pre ? "The post in the middle of the steps." : "The inside of the arch; it has to be less than the outer radius.");
  }
  if (shaped && (all || (!pre && k == 5))) {
    r.field("Angle", angle, 1.0f, 0.0f, 360.0f);
    r.help("How much of a circle the arch spans, in degrees: 180 a half circle, 360 a full ring.");
  }
  if (shaped && (all || (!pre && k == 6) || preset_is({kPresetRailing, kPresetRoof}))) {
    std::string text = blockout_outline_text(outline);
    const std::string before = text;
    r.text("Outline", text);
    r.help("The corners as 'x z; x z; ...' in the piece's XZ plane (3 to 256, either way round, not crossing itself). Drag the points in the Scene "
           "view, or click the + between two to add one.");
    if (text != before) blockout_outline_from_text(text, outline);
  }
  if (shaped && (all || (!pre && k == 7))) {
    r.object("Mesh Object", mesh_object, "MeshFilter");
    r.help("The object whose mesh is this piece (a closed mesh; an open one is skipped on the exact path). The piece is placed by its own "
           "transform; the object itself is no longer drawn, collided with, baked or exported while the piece is on.");
  }
  if (all || takes_blend()) {
    r.field("Blend Radius", blend, 0.01f, 0.0f, 100.0f);
    r.help("Melts the piece into what it touches, in metres in the Blockout's space: an Add piece gets a rounded fillet along the join, a "
           "Subtract piece a rounded rim. 0: a crisp join. What the blend does not reach stays as it was. Blend Detail on the Blockout sets how "
           "finely the rounded part is meshed. Rooms and Intersect pieces are not blended.");
  }
  /* Round 482: its own materials for the classes of face it makes, and where its faces' texture starts. */
  int shown = 0;
  for (int k = 0; k < kBlockoutClasses; k++) shown |= (all || has_class(k)) << k;
  reflect_materials(r, material, shown);
  r.field("Texture Offset U", texture_offset_u, 0.01f, -1000.0f, 1000.0f);
  r.help("Shifts the texture on this piece's faces along U, in tiles (one tile is the Blockout's Texture Scale in metres).");
  r.field("Texture Offset V", texture_offset_v, 0.01f, -1000.0f, 1000.0f);
  r.help("Shifts the texture on this piece's faces along V, in tiles.");
  r.field("Texture Rotation", texture_rotation, 1.0f, -360.0f, 360.0f);
  r.help("Turns the texture on this piece's faces, in degrees about the origin of the projection.");
}

bool BlockoutPiece::has_class(int cls) const {
  if (is_preset()) return cls == kClassSolid || (preset == kPresetRoof && cls == kClassRoof);
  if (is_room()) return cls <= kClassOutside;
  if (is_styled_window()) return cls == kClassCut || cls == kClassSolid || (cls == kClassGlass && glass);  // (round 498: the hole's reveal, the frame, the pane)
  if (is_styled_door()) return cls == kClassCut || cls == kClassSolid || (cls == kClassLeaf && leaf_on());  // (round 499: the reveal, the frame or ring, the leaf - round 515: an object of its own, which takes this piece's Leaf material)
  const bool adds = opening == 0 && operation == (int)blockout::Op::Add;
  return cls == (adds ? kClassSolid : kClassCut);
}

bool BlockoutPiece::takes_blend() const {
  return opening == 0 && !is_preset() && !is_room() && operation != (int)blockout::Op::Intersect && (shape == 0 || shape == 1 || shape == 2);
}

namespace {

float safe_len(float v) { return finite_bits(v) ? std::min(1e6f, std::fabs(v)) : v; }
float safe_thickness(float v) { return finite_bits(v) ? std::min(1000.0f, std::max(0.0f, v)) : 0.0f; }  // (the kernel's own limits)

/* The Blockout `g` is or lies in. */
const GameObject *blockout_at(const GameObject *g) {
  for (; g; g = g->parent)
    if (g->get<Blockout>()) return g;
  return nullptr;
}

bool at_or_below(const GameObject *g, const GameObject *top) {
  for (; g; g = g->parent)
    if (g == top) return true;
  return false;
}

const GameObject *named_object(const BlockoutPiece &bp) {
  return bp.owner && bp.owner->scene && !bp.mesh_object.empty() ? bp.owner->scene->find_by_name(bp.mesh_object) : nullptr;
}

/* Whether the mesh pieces of `from`, and of the Blockouts they use in turn, reach a piece of `goal`: a loop (the pieces' meshes would
 * need each other). A chain of more than eight Blockouts counts as one. */
bool reaches(const GameObject &from, const GameObject &goal, int depth) {
  if (depth > 8) return true;
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(from, pieces);
  for (const GameObject *p : pieces) {
    const BlockoutPiece *bp = p->get<BlockoutPiece>();
    if (!bp || bp->shape != (int)blockout::Shape::Mesh || bp->opening != 0) continue;
    const GameObject *next = blockout_at(named_object(*bp));
    if (next && (next == &goal || reaches(*next, goal, depth + 1))) return true;
  }
  return false;
}

/* The object a mesh piece uses, or null with the reason in `why`. */
const GameObject *resolve_mesh_piece(const BlockoutPiece &bp, std::string *why) {
  auto refuse = [&](const std::string &text) -> const GameObject * {
    if (why) *why = text;
    return nullptr;
  };
  if (bp.mesh_object.empty()) return refuse("no Mesh Object is chosen");
  const GameObject *bo = bp.owner ? blockout_of_piece(*bp.owner) : nullptr;
  const GameObject *t = named_object(bp);
  const std::string quoted = "'" + bp.mesh_object + "'";
  if (!bp.owner || !bp.owner->scene) return refuse("it is not in a scene");
  if (!t) return refuse(quoted + " is not in the scene");
  if (bo && at_or_below(t, bo)) return refuse(quoted + " is this Blockout or one of its pieces");
  if (bo) {
    const GameObject *other = blockout_at(t);
    if (other && reaches(*other, *bo, 0)) return refuse(quoted + " is built from this Blockout's own pieces");
  }
  const MeshFilter *mf = t->get<MeshFilter>();
  if (!mf || !mf->mesh) return refuse(quoted + " has no mesh");
  if (!t->evaluated_mesh_shared(0)) return refuse(quoted + "'s mesh is not ready");
  if (mf->mesh->face_count() == 0) return refuse(quoted + "'s mesh has no faces");
  return t;
}

}  // namespace

std::string BlockoutPiece::mesh_problem() const {
  std::string why;
  if (shape == (int)blockout::Shape::Mesh && opening == 0) resolve_mesh_piece(*this, &why);
  return why;
}

std::string blockout_outline_text(const std::vector<Vec2> &points) {
  std::string s;
  for (const Vec2 &q : points) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%.9g %.9g", s.empty() ? "" : "; ", (double)q.x, (double)q.y);
    s += buf;
  }
  return s;
}

/* Numbers separated by spaces, commas or semicolons; an odd count or a word that is no number is refused. */
bool blockout_outline_from_text(const std::string &text, std::vector<Vec2> &out) {
  std::vector<float> v;
  std::string tok;
  auto flush = [&]() -> bool {
    if (tok.empty()) return true;
    char *end = nullptr;
    const double d = std::strtod(tok.c_str(), &end);
    const bool whole = end != nullptr && end != tok.c_str() && *end == '\0';  // (read before the text is cleared: `end` points into it)
    tok.clear();
    if (!whole) return false;
    v.push_back((float)d);
    return true;
  };
  for (char c : text) {
    if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\n') {
      if (!flush()) return false;
    }
    else tok += c;
  }
  if (!flush() || v.size() % 2 != 0) return false;
  out.clear();
  for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back(Vec2(v[i], v[i + 1]));
  return true;
}

void blockout_piece_bounds(const BlockoutPiece &p, Vec3 &lo, Vec3 &hi) {
  if (blockout_preset_bounds(p, lo, hi)) return;  // (round 500: a preset's box: its bottom on the origin)
  const int k = std::min(7, std::max(0, p.shape));
  const float r = std::fabs(finite_bits(p.radius) ? p.radius : 0.0f), h = std::fabs(finite_bits(p.height) ? p.height : 0.0f) * 0.5f;
  if (k == 0 || k == 3 || k == 4) {
    const Vec3 s = p.box_size() * 0.5f;
    lo = Vec3(-std::fabs(s.x), -std::fabs(s.y), -std::fabs(s.z)), hi = Vec3(std::fabs(s.x), std::fabs(s.y), std::fabs(s.z));
  }
  else if (k == 1) lo = Vec3(-r, -h, -r), hi = Vec3(r, h, r);
  else if (k == 2) lo = Vec3(-r, -r, -r), hi = Vec3(r, r, r);
  else if (k == 5) lo = Vec3(-r, -r, -h), hi = Vec3(r, r, h);  // (the arch's circle: its rim reaches no further than the radius)
  else if (k == 6) {
    float x0 = 0.0f, x1 = 0.0f, z0 = 0.0f, z1 = 0.0f;
    for (size_t i = 0; i < p.outline.size(); i++) {
      const Vec2 q = p.outline[i];
      if (!finite_bits(q.x) || !finite_bits(q.y)) continue;
      x0 = i ? std::min(x0, q.x) : q.x, x1 = i ? std::max(x1, q.x) : q.x, z0 = i ? std::min(z0, q.y) : q.y, z1 = i ? std::max(z1, q.y) : q.y;
    }
    lo = Vec3(x0, -h, z0), hi = Vec3(x1, h, z1);
  }
  else {
    lo = Vec3(-0.5f), hi = Vec3(0.5f);
    if (const GameObject *src = p.owner ? named_object(p) : nullptr)
      if (const MeshFilter *mf = src->get<MeshFilter>(); mf && mf->mesh && mf->mesh->vert_count() > 0) {
        const AABB b = mf->mesh->bounds();
        lo = b.min, hi = b.max;
      }
  }
}

float blockout_floor_below_origin(const BlockoutPiece &p) {
  if (p.shape == (int)blockout::Shape::Arch) return 0.0f;
  Vec3 lo, hi;
  blockout_piece_bounds(p, lo, hi);
  return finite_bits(lo.y) ? -lo.y : 0.0f;
}

void blockout_outline_insert(BlockoutPiece &p, int segment) {
  const size_t n = p.outline.size();
  if (n < 2 || segment < 0 || (size_t)segment >= n || n >= 256) return;
  const Vec2 a = p.outline[(size_t)segment], b = p.outline[((size_t)segment + 1) % n];
  p.outline.insert(p.outline.begin() + segment + 1, Vec2(0.5f * (a.x + b.x), 0.5f * (a.y + b.y)));
}

void blockout_outline_move(BlockoutPiece &p, const BlockoutPiece &before, int index, float x, float z) {
  p = before;
  if (index < 0 || (size_t)index >= p.outline.size() || !finite_bits(x) || !finite_bits(z)) return;
  p.outline[(size_t)index] = Vec2(x, z);
}

Vec3 BlockoutPiece::box_size() const {
  if (opening == 0) return {safe_len(size.x), safe_len(size.y), safe_len(size.z)};
  /* A head at or under the sill leaves a box of no height: the kernel skips it and the status names it. */
  const float up = finite_bits(head) && finite_bits(sill) ? std::max(0.0f, head - sill) : std::numeric_limits<float>::quiet_NaN();
  return {safe_len(width), up, safe_len(depth)};
}

Mat4 BlockoutPiece::shape_offset() const {
  if (opening == 0) return Mat4::identity();
  const float mid = finite_bits(head) && finite_bits(sill) ? 0.5f * (head + sill) : 0.0f;
  return Mat4::translate({0.0f, mid, 0.0f});
}

/* Typed or scripted numbers can be anything. Counts are held to what the kernel takes. A size that is not finite, or none at
 * all, goes through as it is: the kernel skips such a piece and names it, so the Inspector can say which. */
blockout::Piece BlockoutPiece::to_piece(uint64_t id, const Mat4 &to_blockout, std::string *mesh_why, const GameObject **source) const {
  auto len = safe_len;
  blockout::Piece p;
  p.id = id;
  p.shape = (blockout::Shape)std::min(7, std::max(0, shape));
  p.op = (blockout::Op)std::min(2, std::max(0, operation));
  p.size = box_size();
  p.radius = len(radius);
  p.height = len(height);
  p.segments = std::min(256, std::max(3, segments));
  p.rings = std::min(128, std::max(2, rings));
  p.steps = std::min(256, std::max(1, steps));
  p.fill_under = fill_under;
  p.inner_radius = len(inner_radius);
  p.angle = finite_bits(angle) ? std::min(1e6f, std::max(-1e6f, angle)) : angle;
  p.outline = outline;  // (any count: the kernel names an outline of more than 256 or fewer than 3 corners)
  if (p.shape == blockout::Shape::Mesh && opening == 0) {
    std::string why;
    const GameObject *src = resolve_mesh_piece(*this, &why);
    if (mesh_why) *mesh_why = why;
    if (source) *source = src;
    if (src) {
      /* The evaluated mesh, held (the build reads it on a worker while an edit writes a copy), and a number for what it holds: the
       * object, the mesh's own identity and versions - an edit, a deformation, another mesh or another object each change it. */
      p.mesh = src->evaluated_mesh_shared(0);
      uint64_t key = hash_mix(kFnvBasis, src->id);
      key = hash_mix(key, src->evaluated_key(0));
      if (p.mesh) key = hash_mix(hash_mix(hash_mix(key, p.mesh->uid.v), p.mesh->version), p.mesh->deform_version);
      p.mesh_key = key;
    }
  }
  p.blend = finite_bits(blend) ? std::min(100.0f, std::max(0.0f, blend)) : 0.0f;
  p.to_blockout = to_blockout;
  if (opening != 0) {  // (a box that cuts, its middle between the sill and the head above the origin)
    p.shape = blockout::Shape::Box, p.op = blockout::Op::Subtract, p.blend = 0.0f;
    p.to_blockout = to_blockout * shape_offset();
    return p;
  }
  p.room = room && shape_can_be_room(shape);
  p.wall = safe_thickness(wall), p.floor = safe_thickness(floor), p.ceiling = safe_thickness(ceiling);
  p.keep_walls = walls == 1;
  p.open_top = open_top;
  return p;
}

bool blockout_room_growth(const blockout::Piece &p, RoomGrowth &out) {
  if (!p.room) return false;
  const float *f = p.to_blockout.m;  // (column-major: the columns are where the axes go)
  auto cross = [](const double a[3], const double b[3], double r[3]) {
    r[0] = a[1] * b[2] - a[2] * b[1], r[1] = a[2] * b[0] - a[0] * b[2], r[2] = a[0] * b[1] - a[1] * b[0];
  };
  auto length = [](const double v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
  double a[3], b[3], c[3], bc[3], ca[3], ab[3];
  for (int i = 0; i < 3; i++) a[i] = (double)f[i], b[i] = (double)f[4 + i], c[i] = (double)f[8 + i];
  cross(b, c, bc), cross(c, a, ca), cross(a, b, ab);
  const double det = std::fabs(a[0] * bc[0] + a[1] * bc[1] + a[2] * bc[2]);
  const double lbc = length(bc), lca = length(ca), lab = length(ab);
  if (!finite_bits(det) || !(det > 1e-12 * length(a) * length(b) * length(c)) || !(lbc > 0.0) || !(lca > 0.0) || !(lab > 0.0)) return false;
  const double tx = det / lbc, ty = det / lca, tz = det / lab;  // (the distance between a pair of the piece's opposite sides, per unit)
  const double wall = p.wall, floor = p.floor, ceiling = p.open_top ? 0.0 : p.ceiling;
  if (p.shape == blockout::Shape::Sphere) {
    out = {wall / tx, wall / tz, wall / ty, wall / ty};
    return true;
  }
  const double facet = p.shape == blockout::Shape::Cylinder ? std::cos(3.141592653589793 / (double)std::max(3, std::min(256, p.segments))) : 1.0;
  out = {wall / (tx * facet), wall / (tz * facet), floor / ty, ceiling / ty};
  return true;
}

Mat4 blockout_piece_matrix(const GameObject &piece, const GameObject &blockout) {
  Mat4 m = Mat4::identity();
  for (const GameObject *g = &piece; g && g != &blockout; g = g->parent) m = g->local_matrix() * m;
  return m;
}

/* ===================================================================== */
/* A styled window's pieces (round 498, task 0482)                        */
/* ===================================================================== */

namespace {

/* What a made piece is. The kernel adds before it subtracts, so a frame added next to the window's hole would be cut by it. The window is therefore made as
 * a blank (Add: the whole hole's shape, as deep as the frame) that the Subtract pieces carve: the reveal in front of and behind the frame (the whole hole's
 * shape), and the lights (one cell each, so the bars between them stay) - cut short of the pane, which stays as a thin sheet of the blank. What is left in
 * the wall is the frame, the bars and the pane; with no wall behind it (a window in the air) the blank stands there as well. The faces of the Subtract
 * pieces that bound what is left are the frame's and the pane's, so the part says which class each side is (window_face_class). */
enum WindowPart : int {
  kPartBlank = 1, kPartBoard, kPartRevealFront, kPartRevealBack, kPartLight, kPartLightFront, kPartLightBack,
  /* Round 499, a door's: a Revolving door's ring (Add) and its drum (Subtract). The numbers 8 to 17 were round 499's swung-leaf cutters and leaf; round 515 made the leaf an
   * object of its own (ADR 0136), so they are gone and the ring and the drum keep theirs. */
  kPartRing = 18, kPartDrum = 19
};
constexpr int kPartDoorFlag = 0x20;  // (added to the part of anything a door makes: its Glass is the Leaf class)
constexpr uint64_t kBoardFlag = 0x8000;  // (in the index field: the window has a sill board, so the hole's floor is its top)

uint64_t window_piece_id(uint64_t owner, int part, uint32_t index, bool board) {
  return kBlockoutGeneratedBit | ((uint64_t)part << 48) | ((uint64_t)(index & 0x7FFF) | (board ? kBoardFlag : 0)) << 32 | (owner & 0xFFFFFFFFull);
}
int window_part(uint64_t id) { return (int)((id >> 48) & 0x7F); }
bool window_board(uint64_t id) { return ((id >> 32) & kBoardFlag) != 0; }

/* A window's numbers made safe, in its own space (x across, y up from its floor line, z through the wall). */
struct WindowShape {
  int style = 0;
  float w = 0, sill = 0, head = 0, h = 0, depth = 0;
  float fw = 0, bw = 0;     // frame width, bar width
  float fd = 0, gt = 0;     // frame depth, glass thickness
  int nx = 1, ny = 1;       // lights across and up
  float rise = 0;           // Arched: the head's rise (its semi-axis up)
  bool glass = false, board = false;  // (a window's pane and sill board; a door has neither: its leaf is an object, ADR 0136)
  float overhang = 0;
  float board_t = 0.05f;    // the sill board's thickness
  /* Round 499: a door's shape. `arched` and `round` say what the head and the whole are (the style numbers of a door and a window differ). */
  bool door = false, arched = false, round = false, sliding = false, revolving = false, doubled = false;
  float open = 0;           // the leaf's Open Amount, held to 0..1 (NaN: 0)
  int hinge = 0, slide = 0; // 0 Left (-X), 1 Right (+X)
  float drum = 0, ring = 0; // Revolving: the drum's radius and the ring's thickness (the wall's)
  float entry = 0;          // Revolving: the width of the two entries cut through the ring (the door's Width, held under the drum's diameter)
};

constexpr float kBoardThickness = 0.05f;
constexpr float kOpeningOvershoot = 0.1f;  // (blockout_stand_opening: Depth is the wall's thickness + 0.2)
constexpr float kSmallest = 1e-4f;  // (a part thinner than this is left out: the kernel would skip it and name it)
constexpr float kSlidingLeafMax = 10.0f;  // (a Sliding leaf stands on the wall's face and may be thick; 10 m is the most a number of it makes)
constexpr float kDrumMax = 100.0f;        // (a Revolving drum's largest radius: a bigger number is held to it, as the other pieces' sizes are held to kBlockoutMaxSize)
constexpr float kEntryShare = 1.6f;       // (the entries through a Revolving door's ring are at most this many drum radii wide: eight tenths of its diameter)
constexpr float kLeafShare = 0.6f;  // (a leaf in the frame is at most this much of the frame's depth, so that it and its hinge stay inside the frame's slab)

bool window_shape(const BlockoutPiece &bp, WindowShape &s) {
  const bool door = bp.is_styled_door();
  if (!door && !bp.is_styled_window()) return false;
  /* Only the numbers the style reads must be finite: a NaN Drum Radius on a Single door is read by nothing and does not turn it into the plain hole. A window reads the
   * numbers it did in round 498. */
  const bool round_door = door && bp.style == kDoorRevolving;
  std::vector<float> vals{bp.width, bp.head, bp.sill, bp.depth};
  if (!door) vals.insert(vals.end(), {bp.frame_width, bp.frame_depth, bp.arch_rise, bp.sill_overhang});
  else {
    if (!round_door) vals.push_back(bp.frame_width), vals.push_back(bp.frame_depth);
    if (bp.style == kDoorArched) vals.push_back(bp.arch_rise);
    if (round_door) vals.push_back(bp.drum_radius);
  }
  for (float v : vals)
    if (!finite_bits(v)) return false;
  s.door = door;
  s.style = bp.style;
  s.arched = door ? bp.style == kDoorArched : bp.style == kWindowArched;
  s.round = !door && bp.style == kWindowRound;
  s.sliding = door && bp.style == kDoorSliding, s.revolving = door && bp.style == kDoorRevolving, s.doubled = door && bp.style == kDoorDouble;
  s.w = std::fabs(bp.width), s.depth = std::fabs(bp.depth), s.sill = bp.sill, s.head = bp.head, s.h = bp.head - bp.sill;
  if (!(s.w >= kBlockoutMinSize) || !(s.h >= kBlockoutMinSize) || !(s.depth >= kBlockoutMinSize) || s.w > kBlockoutMaxSize || s.h > kBlockoutMaxSize ||
      s.depth > kBlockoutMaxSize || std::fabs(s.sill) > kBlockoutMaxSize)
    return false;
  s.fd = std::min(s.depth, std::max(kBlockoutMinSize, std::fabs(bp.frame_depth)));
  s.gt = std::min(0.03f, 0.4f * s.fd);
  s.glass = bp.glass;
  s.board = !door && bp.sill_board && s.style != kWindowFixed && s.style != kWindowRound;
  if (door) {
    /* The leaf: the thickness asked for, held inside the frame's slab when the leaf stands in the frame (a Sliding leaf is on the wall's face and may be any thickness); a
     * Revolving door has none. NaN is not asked (the numbers above are finite); an Open Amount that is NaN is closed. */
    const float lt = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, finite_bits(bp.leaf_thickness) ? std::fabs(bp.leaf_thickness) : 0.04f));
    s.glass = false;  // (round 515: the leaf is an object of its own, not a pane in the Blockout's mesh)
    s.gt = s.sliding ? std::min(lt, kSlidingLeafMax) : std::min(lt, kLeafShare * s.fd);
    s.open = finite_bits(bp.open_amount) ? std::min(1.0f, std::max(0.0f, bp.open_amount)) : 0.0f;
    s.hinge = bp.hinge_side == 1 ? 1 : 0, s.slide = bp.slide_direction == 1 ? 1 : 0;
    s.drum = std::min(kDrumMax, std::max(kBlockoutMinSize, finite_bits(bp.drum_radius) ? std::fabs(bp.drum_radius) : 1.0f));
    s.ring = std::max(0.05f, s.depth - 2.0f * kOpeningOvershoot);  // (one wall's thickness: a door's Depth is the wall and 0.1 m each side)
    s.entry = std::min(s.w, kEntryShare * s.drum);
    if (s.revolving) return true;
  }
  s.overhang = std::min(kBlockoutMaxSize, std::max(0.0f, bp.sill_overhang));  // (a negative reach is none)
  if (s.overhang < 0.002f) s.overhang = 0.0f;  // (a board standing out less than 2 mm is flush with the wall: no sliver of it)
  s.board_t = kBoardThickness;
  s.nx = door ? 1 : s.style == kWindowDouble ? 2 : s.style == kWindowGrid ? std::min(12, std::max(1, bp.panes_across)) : 1;
  s.ny = door ? 1 : s.style == kWindowGrid ? std::min(12, std::max(1, bp.panes_up)) : 1;
  float fw = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, std::fabs(bp.frame_width)));
  const float bar_ratio = !door && s.style == kWindowDouble ? 1.0f : 0.5f;
  /* Frame and bars together never take more than seven tenths of either side: a light is always left (a frame wider than half the window is held to it). A door's
   * frame is its two jambs and its head: one frame's width up, none at the bottom. */
  const float across = 2.0f + (float)(s.nx - 1) * bar_ratio, up = (door ? 1.0f : 2.0f) + (float)(s.ny - 1) * bar_ratio;
  fw = std::min(fw, std::min(0.7f * s.w / across, 0.7f * s.h / up));
  if (s.arched) {
    s.rise = bp.arch_rise > 0.0f ? bp.arch_rise : 0.5f * s.w;
    s.rise = std::min(s.h, std::max(kBlockoutMinSize, s.rise));
    fw = std::min(fw, 0.45f * s.rise);
  }
  s.fw = std::max(1e-3f, fw), s.bw = s.fw * bar_ratio;
  /* A swung leaf is at most as thick as the frame is wide: its outer face then stays half a leaf inside the reveal's plane (x = +-Width / 2) at any angle, where a thicker one
   * lay on it or past it near Open Amount 1 (the door sweep's seed 1830). */
  if (door && !s.sliding && !s.arched) s.gt = std::min(s.gt, s.fw);
  return finite_bits(s.fw) && finite_bits(s.rise) && s.fw > 0.0f;
}

/* The field path (a grid of `cell` metres) cannot show a part much finer than a cell: a 6 cm frame or a 3 cm pane at 0.1 m comes out as a ragged skin with
 * many unresolved places - two planes closer than a cell put two corners in one cell, all along the frame. Parts too fine for the cell are the frame or a bar
 * under 1.5 cells wide, a frame shallower than that, a pane thinner than that, a frame face within 1.5 cells of the wall's face (flush with it is fine) and a
 * sill board that stands out of the wall (its overhang is far less than a cell and its end lies near the reveals' ends). Then the window is made as the grid can
 * show it: no pane, no standing sill board, the frame and bars held at one cell, the frame as deep as the wall when its faces were that near to the wall's; and
 * the status says what Field Cell shows the real thing. False: with that the
 * lights would close (a light under 1.5 cells), so the window is the plain hole. */
bool fit_to_grid(WindowShape &s, float cell, BlockoutWindowFit &fit) {
  const float limit = 1.499f * cell, wall_half = std::max(0.5f * s.fd, 0.5f * s.depth - kOpeningOvershoot);
  fit.door = s.door;
  if (s.revolving) {  // (a drum under a cell and a half is no round passage the grid shows: the plain hole; a thin ring is held at a cell)
    if (s.drum < limit) return fit.plain = true, fit.narrowest = s.drum, false;
    if (s.entry < limit) return fit.plain = true, fit.narrowest = s.entry, false;  // (the entries are the passage: none the grid shows, no door)
    if (s.ring < limit) fit.coarse = true, fit.narrowest = s.ring, fit.frame = s.ring, s.ring = cell;
    return true;
  }
  const float rails = s.door ? 1.0f : 2.0f;  // (a door has no bottom rail: its frame is the two jambs and the head)
  const bool bars = s.nx > 1 || s.ny > 1;
  const float narrow = bars ? std::min(s.fw, s.bw) : s.fw, gap = std::fabs(wall_half - 0.5f * s.fd);
  const bool shallow = s.fd < limit, near_face = gap > 1e-4f && gap < limit, short_board = s.board && s.overhang > 1e-4f && s.overhang < limit;  // (a board ending near the cutters' ends - 0.1 m past the wall - measured no worse on the grid: 837 unresolved places in 200 arrangements at cell 0.02 against 820)
  const bool thin_pane = s.glass && s.gt < limit;
  /* The lights are checked first, with the numbers as they are: a Grid of 12 x 12 on 0.6 m has lights of 12 mm whatever else is fine. */
  const float lw0 = (s.w - 2.0f * s.fw - (float)(s.nx - 1) * s.bw) / (float)s.nx, lh0 = (s.h - rails * s.fw - (float)(s.ny - 1) * s.bw) / (float)s.ny;
  const bool closed_lights = lw0 < limit || lh0 < limit;
  if (!(narrow < limit) && !shallow && !near_face && !short_board && !thin_pane && !closed_lights) return true;
  fit.coarse = true, fit.frame = s.fw;
  fit.narrowest = std::min(narrow, std::min(shallow ? s.fd : 1e30f, std::min(near_face ? gap : 1e30f, short_board && s.overhang < limit ? s.overhang : 1e30f)));
  if (thin_pane) fit.narrowest = std::min(fit.narrowest, s.gt);
  if (closed_lights) fit.narrowest = std::min(fit.narrowest, std::min(lw0, lh0));
  s.glass = false;
  s.fw = std::max(s.fw, cell), s.bw = bars ? std::max(s.bw, cell) : s.fw;  // (held at one cell)
  if (shallow || near_face) s.fd = 2.0f * wall_half;
  /* (A board that stands out of the wall is a sliver at this cell, and its end lies within a cell of the reveals' ends: it is left out. With none standing out it is
   * inside the wall, whose matter it adds to, and changes nothing.) */
  if (short_board) s.board = false;
  const float lw = (s.w - 2.0f * s.fw - (float)(s.nx - 1) * s.bw) / (float)s.nx, lh = (s.h - rails * s.fw - (float)(s.ny - 1) * s.bw) / (float)s.ny;
  const bool arched_ok = !s.arched || (s.head - s.fw) - std::max(s.head - s.rise, s.sill + (s.door ? 0.0f : s.fw)) >= cell;
  if (lw >= limit && lh >= limit && arched_ok) return true;
  fit.plain = true;
  return false;
}

/* The pieces being made: each is placed by `base` (the window's matrix in the Blockout's space) times a matrix of the window's own space. */
struct WindowMaker {
  const WindowShape &s;
  uint64_t owner;
  const Mat4 &base;
  std::vector<blockout::Piece> &out;
  uint32_t index = 0;

  blockout::Piece start(int part, blockout::Op op, blockout::Shape shape) {
    blockout::Piece p;
    p.id = window_piece_id(owner, part | (s.door ? kPartDoorFlag : 0), index++, s.board);
    p.op = op, p.shape = shape;
    return p;
  }
  void box(int part, blockout::Op op, float x0, float x1, float y0, float y1, float z0, float z1) {
    if (!(x1 - x0 > kSmallest) || !(y1 - y0 > kSmallest) || !(z1 - z0 > kSmallest)) return;
    blockout::Piece p = start(part, op, blockout::Shape::Box);
    p.size = Vec3(x1 - x0, y1 - y0, z1 - z0);
    p.to_blockout = base * Mat4::translate(Vec3(0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1)));
    out.push_back(std::move(p));
  }
  /* A vertical column of `radius` from y0 to y1 about the window's axis (a Revolving door's drum and ring). */
  void column(int part, blockout::Op op, float radius, float y0, float y1) {
    if (!(radius > kSmallest) || !(y1 - y0 > kSmallest)) return;
    blockout::Piece p = start(part, op, blockout::Shape::Cylinder);
    p.radius = radius, p.height = y1 - y0, p.segments = 32;
    p.to_blockout = base * Mat4::translate(Vec3(0.0f, 0.5f * (y0 + y1), 0.0f));
    out.push_back(std::move(p));
  }
  /* The upper half of an ellipse of semi-axes a (x) and b (y) standing on y = spring, z0 to z1 deep (a filled arch, as the Subtract tool's). */
  void arch(int part, blockout::Op op, float a, float b, float spring, float z0, float z1) {
    if (!(a > kSmallest) || !(b > kSmallest) || !(z1 - z0 > kSmallest)) return;
    blockout::Piece p = start(part, op, blockout::Shape::Arch);
    p.radius = 1.0f, p.inner_radius = 0.0f, p.angle = 180.0f, p.segments = 16, p.height = z1 - z0;
    Mat4 m = Mat4::translate(Vec3(0.0f, spring, 0.5f * (z0 + z1)));
    m.m[0] = a, m.m[5] = b;  // (the ellipse is the unit arch stretched: the matrix's own columns)
    p.to_blockout = base * m;
    out.push_back(std::move(p));
  }
  /* An ellipse of semi-axes a (x) and b (y) round (0, cy), z0 to z1 deep: a cylinder turned so its axis runs through the wall. */
  void ellipse(int part, blockout::Op op, float a, float b, float cy, float z0, float z1) {
    if (!(a > kSmallest) || !(b > kSmallest) || !(z1 - z0 > kSmallest)) return;
    blockout::Piece p = start(part, op, blockout::Shape::Cylinder);
    p.radius = 1.0f, p.height = z1 - z0, p.segments = 32;
    Mat4 m(0);  // (x -> a along X, the cylinder's axis y -> +Z, its z -> -b along Y: no turn computed, so no rounding in it)
    m.m[0] = a, m.m[6] = 1.0f, m.m[9] = -b, m.m[15] = 1.0f;
    m.m[13] = cy, m.m[14] = 0.5f * (z0 + z1);
    p.to_blockout = base * m;
    out.push_back(std::move(p));
  }
  /* The window's whole shape pulled in by `inset` (0: the hole itself), z0 to z1 deep. `down`: the box part reaches that far below the floor line (into the sill
   * board, which is wider and deeper: the same solid, but the two Add pieces overlap instead of touching face to face in the plane y = sill). */
  void whole(int part, blockout::Op op, float inset, float z0, float z1, float down = 0.0f) {
    const float a = 0.5f * s.w - inset;
    /* A door has no bottom rail: its light runs down to the floor line, so only the sides and the head are pulled in. */
    const float lo = s.sill + (s.door ? 0.0f : inset);
    if (s.round) return ellipse(part, op, a, 0.5f * s.h - inset, s.sill + 0.5f * s.h, z0, z1);
    if (!s.arched) return box(part, op, -a, a, lo - down, s.head - inset, z0, z1);
    /* The round head springs from the hole's line, but a light (inset > 0) never springs below its own floor: a tall rise would otherwise cut through the
     * bottom rail. */
    const float spring = std::max(s.head - s.rise, lo);
    box(part, op, -a, a, lo - down, spring, z0, z1);
    arch(part, op, a, (s.head - inset) - spring, spring, z0, z1);
  }
  /* The lights: one cell for each pane, the bars left between them. */
  void lights(int part, float z0, float z1) {
    if (s.nx == 1 && s.ny == 1) return whole(part, blockout::Op::Subtract, s.fw, z0, z1);
    const float x_lo = -0.5f * s.w + s.fw, y_lo = s.sill + s.fw;
    const float cw = (s.w - 2.0f * s.fw - (float)(s.nx - 1) * s.bw) / (float)s.nx, ch = (s.h - 2.0f * s.fw - (float)(s.ny - 1) * s.bw) / (float)s.ny;
    for (int j = 0; j < s.ny; j++)
      for (int i = 0; i < s.nx; i++) {
        const float x0 = x_lo + (float)i * (cw + s.bw), y0 = y_lo + (float)j * (ch + s.bw);
        box(part, blockout::Op::Subtract, x0, x0 + cw, y0, y0 + ch, z0, z1);
      }
  }
};

/* Which side of a generated piece looks along Z: `plus` +Z, else -Z (a box's 4 and 5, an arch's 2 and 3, a cylinder's bottom and top - its axis is turned onto Z). */
uint32_t window_depth_side(const blockout::Piece &p, bool plus) {
  switch (p.shape) {
    case blockout::Shape::Arch: return plus ? 3u : 2u;
    case blockout::Shape::Cylinder: return plus ? 2u : 1u;
    default: return plus ? 5u : 4u;
  }
}

/* The class of a face of a window's part (round 498). The frame, the bars and the sill board are Solid, the pane Glass, the reveal Cut. Most of what shows
 * is a face of a Subtract piece (the wall holds the frame): the face of the front reveal that looks at the frame is the frame's front, a light's sides are
 * the frame's and bars' inner sides, the face of a light's front half that looks at the pane is the pane's, and with a sill board the hole's floor is its top. */
int window_face_class(const blockout::Piece &p, uint32_t side) {
  const int raw = window_part(p.id), part = raw & ~kPartDoorFlag;
  const int sheet = (raw & kPartDoorFlag) ? kClassLeaf : kClassGlass;  // (what a thin sheet in the light is: a window's pane, a door's leaf)
  switch (part) {
    case kPartBlank:
    case kPartBoard: return kClassSolid;
    case kPartRevealFront:
    case kPartRevealBack: {
      const bool toward_frame = side == window_depth_side(p, part == kPartRevealFront);
      const bool board_top = window_board(p.id) && p.shape == blockout::Shape::Box && side == 2;  // (a box's -Y: the hole's floor)
      return toward_frame || board_top ? kClassSolid : kClassCut;
    }
    case kPartLightFront: return side == window_depth_side(p, true) ? sheet : kClassSolid;
    case kPartLightBack: return side == window_depth_side(p, false) ? sheet : kClassSolid;
    case kPartRing: return kClassSolid;
    case kPartDrum: return kClassCut;
    default: return kClassSolid;  // (a light with no pane: the frame's inner sides)
  }
}

/* The pieces of a styled door: the frame (blank, reveals, the clear opening cut out of it - no bottom rail), or for a Revolving door its drum and ring. The leaf is no piece: it is an
 * object of its own under the door (round 515, ADR 0136), so the doorway is the same whatever Open Amount, Hinge Side, Leaf and Leaf Thickness say. */
void door_pieces(WindowMaker &mk) {
  using blockout::Op;
  const WindowShape &s = mk.s;
  const float half_d = 0.5f * s.depth, half_f = 0.5f * s.fd;
  if (s.revolving) {  // (the drum cut through the wall and a ring of the wall's thickness round it)
    mk.column(kPartRing, Op::Add, s.drum + s.ring, s.sill, s.head);
    mk.column(kPartDrum, Op::Subtract, s.drum, s.sill, s.head);
    /* The two entries: a box of the door's Width (held to 1.6 drum radii) from the floor line to the head, along the door's Z through the ring on both sides (and 5 cm past it),
     * so that one walks in at one side, through the drum and out at the other. The ring is closed on either side of them. */
    const float reach = s.drum + s.ring + 0.05f;
    mk.box(kPartDrum, Op::Subtract, -0.5f * s.entry, 0.5f * s.entry, s.sill, s.head, -reach, reach);
    return;
  }
  mk.whole(kPartBlank, Op::Add, 0.0f, -half_f, half_f);
  mk.whole(kPartRevealFront, Op::Subtract, 0.0f, -half_d, -half_f);
  mk.whole(kPartRevealBack, Op::Subtract, 0.0f, half_f, half_d);
  mk.lights(kPartLight, -half_f, half_f);
}

}  // namespace

bool blockout_window_pieces(const BlockoutPiece &bp, uint64_t owner, const Mat4 &to_blockout, std::vector<blockout::Piece> &out, float grid_cell, BlockoutWindowFit *fit) {
  WindowShape s;
  if (owner > 0xFFFFFFFFull || !window_shape(bp, s)) return false;
  if (grid_cell > 0.0f) {
    BlockoutWindowFit f;
    f.id = owner;
    const bool built = fit_to_grid(s, grid_cell, f);
    if (fit) *fit = f;
    if (!built) return false;
  }
  using blockout::Op;
  const size_t first = out.size();
  WindowMaker mk{s, owner, to_blockout, out};
  if (s.door) {
    door_pieces(mk);
    return out.size() != first;
  }
  const float half_d = 0.5f * s.depth, half_f = 0.5f * s.fd, half_g = 0.5f * s.gt;
  /* The Add pieces: the blank the frame is left of, and the sill board under the window (it stands out past the wall on every side). */
  mk.whole(kPartBlank, Op::Add, 0.0f, -half_f, half_f, s.board ? 0.5f * s.board_t : 0.0f);
  if (s.board) {
    const float x = 0.5f * s.w + s.overhang, z = std::max(half_f, half_d - kOpeningOvershoot) + s.overhang;  // (the reach counts from the wall's faces: a made opening is 0.1 m deeper than its wall each side)
    mk.box(kPartBoard, Op::Add, -x, x, s.sill - s.board_t, s.sill, -z, z);
  }
  /* The Subtract pieces: the hole round the frame, and the lights. */
  mk.whole(kPartRevealFront, Op::Subtract, 0.0f, -half_d, -half_f);
  mk.whole(kPartRevealBack, Op::Subtract, 0.0f, half_f, half_d);
  /* The lights stop at the frame's planes, where the reveals end. (Round 498's review asked for them to run out through the reveals, so that cutters never end in one
   * plane; the exact-path sweep got worse with it - 69 arrangements with small faces on top of others against 65 - so they stop here.) */
  if (s.glass) {
    mk.lights(kPartLightFront, -half_f, -half_g);
    mk.lights(kPartLightBack, half_g, half_f);
  }
  else mk.lights(kPartLight, -half_f, half_f);
  if (out.size() == first) return false;
  return true;
}

void blockout_window_set_style(BlockoutPiece &p, int style) { blockout_opening_set_style(p, style); }  // (held to the words of the piece's own kind: safe for a door)

void blockout_opening_set_style(BlockoutPiece &p, int style) {
  const int before = p.style, top = (p.opening == 1 ? kBlockoutDoorStyles : kBlockoutWindowStyles) - 1;
  p.style = std::min(top, std::max(0, style));
  blockout_piece_edited(p, before, p.opening);
}

void blockout_opening_preset(int opening, int style, float &width, float &head) {
  if (opening == 1) {
    width = style == kDoorDouble ? 1.8f : style == kDoorRevolving ? 1.2f : 0.9f;
    head = 2.1f;
    return;
  }
  width = style == kWindowDouble ? 1.5f : 1.2f;
  head = 2.0f;
}

void blockout_piece_edited(BlockoutPiece &p, int style_before, int opening_before) {
  /* Style is shared by doors and windows (round 499): a piece that is no opening has none, and one switched between Door and Window starts at Opening Only - a window's
   * Grid is no door's Revolving. A door keeps its own. */
  if (p.opening != 0) p.preset = 0;  // (round 500: a door or window is no preset)
  if (p.preset < 0 || p.preset >= kBlockoutPresets) p.preset = 0;
  if (p.preset > 0) p.room = false;
  if (p.opening == 0 || p.opening != opening_before) p.style = 0;
  p.style = std::min(p.opening == 1 ? kBlockoutDoorStyles - 1 : kBlockoutWindowStyles - 1, std::max(0, p.style));
  /* A new style brings its preset size, but only over the old style's: a width or head somebody typed is theirs. The opening's centre does not move (Width grows about it). */
  if (p.opening != 0 && p.opening == opening_before && p.style != style_before && style_before >= 0 && style_before < (p.opening == 1 ? kBlockoutDoorStyles : kBlockoutWindowStyles) &&
      !(p.opening == 2 && style_before == kWindowRound)) {
    float ow, oh, nw, nh;
    blockout_opening_preset(p.opening, style_before, ow, oh);
    blockout_opening_preset(p.opening, p.style, nw, nh);
    /* By value: a size typed that equals the old style's preset counts as untouched. A Round window is the exception: its Head is its circle's (Sill + Width), which is no
     * preset, so leaving Round keeps the numbers (round 498's rule, which its tests pin: Round to Single stays 1.2 x 2.1). */
    if (p.width == ow && p.head == oh) p.width = nw, p.head = nh;
  }
  /* A Round window starts as a circle: Head = Sill + Width. Width or Head may be changed afterwards (an ellipse). */
  if (p.opening == 2 && p.style == kWindowRound && style_before != kWindowRound && finite_bits(p.width) && finite_bits(p.sill) && p.width > 0.0f)
    p.head = std::min(kBlockoutMaxSize, p.sill + p.width);
}

size_t blockout_user_piece_count(const std::vector<blockout::Piece> &pieces) {
  size_t n = 0;
  std::unordered_set<uint64_t> windows;
  for (const blockout::Piece &p : pieces) {
    if (!blockout_id_generated(p.id)) n++;
    else if (windows.insert(blockout_piece_owner(p.id)).second) n++;
  }
  return n;
}

namespace {

/* The surface notes of a piece (round 482): its own materials, and where its texture starts. */
BlockoutSurface::Piece surface_of(const BlockoutPiece &bp, uint64_t id) {
  BlockoutSurface::Piece sp;
  sp.id = id;
  for (int k = 0; k < kBlockoutClasses; k++)
    if (k != kClassLeaf) sp.materials[k] = bp.material[k];  // (round 515: the Leaf material is read by the leaf object, no face of the mesh has it)
  sp.offset_u = finite_bits(bp.texture_offset_u) ? bp.texture_offset_u : 0.0f;
  sp.offset_v = finite_bits(bp.texture_offset_v) ? bp.texture_offset_v : 0.0f;
  sp.rotation = finite_bits(bp.texture_rotation) ? bp.texture_rotation : 0.0f;
  return sp;
}

/* A styled window: its made pieces, each with the window's surface notes. False: the window is built plain. */
bool collect_window(const BlockoutPiece &bp, uint64_t id, const Mat4 &m, std::vector<blockout::Piece> &pieces, BlockoutSnapshotNotes *notes, float grid_cell) {
  std::vector<blockout::Piece> made;
  BlockoutWindowFit fit;
  const bool built = blockout_window_pieces(bp, id, m, made, grid_cell, &fit);
  if (notes && (fit.coarse || fit.plain)) notes->window_fits.push_back(fit);
  if (!built) return false;
  for (blockout::Piece &p : made) {
    if (notes) notes->surface.pieces.push_back(surface_of(bp, p.id));
    pieces.push_back(std::move(p));
  }
  return true;
}

/* A preset: its made pieces, each with the preset's surface notes; what it says about the grid goes to the status as a warning line. */
void collect_preset(const GameObject &g, const BlockoutPiece &bp, const Mat4 &m, std::vector<blockout::Piece> &pieces, BlockoutSnapshotNotes *notes, float grid_cell) {
  std::vector<blockout::Piece> made;
  std::vector<std::string> said;
  const bool built = blockout_preset_pieces(bp, g.id, m, made, grid_cell, &said);
  if (!built) {
    if (notes) notes->refused.push_back({g.id, said.empty() ? "its numbers make nothing" : said[0]});
    return;
  }
  if (notes)
    for (const std::string &line : said) notes->swing_cuts.push_back(std::string(kBlockoutPresetNames[std::min(kBlockoutPresets - 1, std::max(0, bp.preset))]) + " '" + g.name + "': " + line);
  for (blockout::Piece &p : made) {
    if (notes) notes->surface.pieces.push_back(surface_of(bp, p.id));
    pieces.push_back(std::move(p));
  }
}

/* A styled door or window whose size is past what the pieces are held to (kBlockoutMaxSize: the numbers are finite, so the plain hole the kernel gets for a style it cannot
 * make would be one of 1e6 m and spoil every later build): it is left out and the status names it, before any piece of it is made. */
constexpr float kStyledDoorMax = 100.0f;  // (a styled door bigger than this in any size is left out: the kernel meets float limits long before 10000 m - 1000 m wide Arched doors gave faces on top of each other on the exact path)
bool styled_too_large(const BlockoutPiece &bp) {
  const float v[] = {bp.width, bp.depth, bp.sill, bp.head};
  for (float x : v)
    if (!finite_bits(x)) return false;
  return std::fabs(bp.width) > kStyledDoorMax || std::fabs(bp.depth) > kStyledDoorMax || std::fabs(bp.sill) > kStyledDoorMax || std::fabs(bp.head) > kStyledDoorMax ||
         std::fabs(bp.head - bp.sill) > kStyledDoorMax;
}

/* Depth first, in the hierarchy's order. `to_here` is the product of the local matrices from the Blockout down to `g`'s
 * parent. A Blockout below is not entered: it is not this one's piece. */
void collect(const GameObject &g, const Mat4 &to_here, std::vector<blockout::Piece> &pieces, BlockoutSnapshotNotes *notes, float grid_cell) {
  for (const GameObject *c : g.children) {
    if (!c->active || c->get<Blockout>()) continue;
    const Mat4 m = to_here * c->local_matrix();
    const BlockoutPiece *bp = c->get<BlockoutPiece>();
    if (bp && bp->enabled && bp->is_styled_door() && styled_too_large(*bp)) {  // (a window's huge numbers keep round 498's way)
      if (notes) notes->refused.push_back({c->id, "its size is over 100 m: a styled door that large is left out"});
      collect(*c, m, pieces, notes, grid_cell);
      continue;
    }
    if (bp && bp->enabled && bp->is_preset()) {  // (round 500: one piece, several kernel pieces; a preset that cannot be made is left out and named, never a plain box)
      collect_preset(*c, *bp, m, pieces, notes, grid_cell);
      collect(*c, m, pieces, notes, grid_cell);
      continue;
    }
    const size_t first = pieces.size();  // (round 546: an opening's pieces from here on are copied for each of its placements)
    if (bp && bp->enabled && bp->is_styled_opening() && collect_window(*bp, c->id, m, pieces, notes, grid_cell)) {
      blockout_opening_copies(*c, m, first, pieces, notes ? &notes->surface : nullptr);
      collect(*c, m, pieces, notes, grid_cell);
      continue;
    }
    if (bp && bp->enabled) {
      std::string why;
      const GameObject *source = nullptr;
      blockout::Piece p = bp->to_piece(c->id, m, &why, &source);
      /* A mesh piece with no usable mesh is not the kernel's to meet: it is named here and the Blockout's status says why. */
      if (!why.empty()) {
        if (notes) notes->refused.push_back({c->id, why});
      }
      else {
        if (notes) blockout_piece_stack_note(*c, p, pieces.size(), notes->surface);  // (round 514: a stack on the piece, run on the build's worker)
        pieces.push_back(std::move(p));
        if (source && notes) notes->sources.push_back(source->id);
        if (notes) notes->surface.pieces.push_back(surface_of(*bp, c->id));  // (round 482: what the faces of this piece are given besides their shape)
        if (bp->opening != 0) blockout_opening_copies(*c, m, first, pieces, notes ? &notes->surface : nullptr);  // (round 546)
      }
    }
    collect(*c, m, pieces, notes, grid_cell);
  }
}

}  // namespace

void blockout_piece_objects(const GameObject &blockout, std::vector<const GameObject *> &out) {
  for (const GameObject *c : blockout.children) {
    if (!c->active || c->get<Blockout>()) continue;
    const BlockoutPiece *bp = c->get<BlockoutPiece>();
    if (bp && bp->enabled) out.push_back(c);
    blockout_piece_objects(*c, out);
  }
}

void blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &pieces, blockout::Settings &settings, BlockoutSnapshotNotes *notes) {
  pieces.clear();
  if (notes) notes->refused.clear(), notes->sources.clear(), notes->window_fits.clear(), notes->swing_cuts.clear(), notes->surface = BlockoutSurface();
  const Blockout *b = blockout.get<Blockout>();
  settings = b ? b->settings() : blockout::Settings();
  if (notes && b) {
    notes->surface.scale = b->safe_texture_scale();
    for (int k = 0; k < kBlockoutClasses; k++)
      if (k != kClassLeaf) notes->surface.materials[k] = b->material[k];  // (the Leaf material is the leaf object's)
  }
  /* A styled window meshed on the grid is made as the grid can show it (fit_to_grid). Whether it is: the Method says Field, or Auto in a build with no exact
   * path. (A cluster that Auto sends to the grid for a blend or an open mesh is not known here: its window is made for the exact path.) */
  const bool grid = settings.path == blockout::Path::Field || (settings.path == blockout::Path::Auto && !blockout::crisp_available());
  collect(blockout, Mat4::identity(), pieces, notes, grid && finite_bits(settings.cell) && settings.cell > 0.0f ? settings.cell : 0.0f);
}

uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings, const BlockoutSnapshotNotes *notes) {
  uint64_t h = hash_mix(kFnvBasis, pieces.size());
  if (notes)
    for (const auto &r : notes->refused) h = hash_mix(hash_mix(h, r.first), fnv1a(r.second.data(), r.second.size()));
  h = mix_float(h, settings.cell);
  h = hash_mix(h, (uint64_t)settings.path);
  h = hash_mix(h, (uint64_t)(uint32_t)settings.blend_detail);
  h = hash_mix(h, (uint64_t)settings.max_cells);
  if (notes) {  // (round 482: a material, the scale or a piece's offset changes the slots and the UVs: the mesh is built again)
    const BlockoutSurface &s = notes->surface;
    h = mix_float(h, s.scale);
    for (const std::string &m : s.materials) h = hash_mix(h, fnv1a(m.data(), m.size()));
    for (const BlockoutSurface::Piece &sp : s.pieces) {
      h = hash_mix(h, sp.id);
      for (const std::string &m : sp.materials) h = hash_mix(h, fnv1a(m.data(), m.size()));
      h = mix_float(mix_float(mix_float(h, sp.offset_u), sp.offset_v), sp.rotation);
    }
    for (const BlockoutSurface::Stack &st : s.stacks) h = hash_mix(hash_mix(h, st.index), st.key);  // (round 514: a piece's stack)
  }
  for (const blockout::Piece &p : pieces) {
    h = hash_mix(h, p.id);
    h = hash_mix(h, (uint64_t)p.shape | (uint64_t)p.op << 8 | (uint64_t)(uint32_t)p.segments << 16 | (uint64_t)(uint32_t)p.rings << 40);
    for (int a = 0; a < 3; a++) h = mix_float(h, p.size[a]);
    h = mix_float(h, p.radius);
    h = mix_float(h, p.height);
    h = mix_float(h, p.blend);
    /* The round 480 shapes' numbers. */
    h = hash_mix(h, (uint64_t)(uint32_t)p.steps | (uint64_t)p.fill_under << 32 | (uint64_t)p.outline.size() << 33);
    h = mix_float(mix_float(h, p.inner_radius), p.angle);
    for (const Vec2 &q : p.outline) h = mix_float(mix_float(h, q.x), q.y);
    if (!p.points.empty()) h = mix_bytes(hash_mix(h, p.points.size()), p.points.data(), p.points.size() * sizeof(Vec3));  // (round 500: a convex piece's corners)
    h = hash_mix(h, p.mesh_key);
    h = hash_mix(h, (uint64_t)p.room | (uint64_t)p.keep_walls << 1 | (uint64_t)p.open_top << 2);
    h = mix_bytes(h, p.to_blockout.m, sizeof(p.to_blockout.m));
    if (!p.room) continue;  // (the thicknesses are read by a room only)
    h = mix_float(h, p.wall);
    h = mix_float(h, p.floor);
    h = mix_float(h, p.ceiling);
  }
  return h;
}

GameObject *blockout_of_piece(const GameObject &piece) {
  for (GameObject *a = piece.parent; a; a = a->parent)
    if (a->get<Blockout>()) return a;
  return nullptr;
}

/* ---- The objects mesh pieces use (round 480) ---- */

namespace {

/* The set the walk publishes. Read from any thread (the draw path, a collider build, a bake's gather): a count first, so a scene with no mesh
 * piece - nearly all of them - never takes the lock. */
std::mutex g_sources_lock;
std::unordered_set<uint64_t> g_sources;
std::atomic<size_t> g_sources_count{0};

}  // namespace

bool blockout_uses_as_piece(const GameObject &g) {
  if (g_sources_count.load(std::memory_order_relaxed) == 0) return false;
  std::lock_guard<std::mutex> lock(g_sources_lock);
  return g_sources.count(g.id) != 0;
}

void blockout_publish_piece_sources(std::vector<uint64_t> &&ids) {
  std::lock_guard<std::mutex> lock(g_sources_lock);
  g_sources.clear();
  g_sources.insert(ids.begin(), ids.end());
  g_sources_count.store(g_sources.size(), std::memory_order_relaxed);
}

/* ===================================================================== */
/* Handles                                                                */
/* ===================================================================== */

namespace {

BlockoutHandle make_handle(BlockoutHandle::Kind kind, int axis, Vec3 at, Vec3 dir, const char *name) {
  BlockoutHandle h;
  h.kind = kind, h.axis = axis, h.at = at, h.dir = dir, h.name = name;
  return h;
}

/* The direction in the XZ plane toward `toward` (+X when it is on the axis): which side of a round shape a handle sits on. */
Vec3 flat_toward(Vec3 toward) {
  Vec3 d(toward.x, 0.0f, toward.z);
  const float l = length(d);
  return l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
}

float finite_or(float v, float fallback) { return finite_bits(v) ? v : fallback; }

/* `v` along one axis and nothing along the others. (Written out: a local Vec3 filled through operator[] - which reaches
 * past its first member - and then copied was compiled by Apple clang as if only x had been written; the +-Y and +-Z
 * handles then had no place and no direction. Found by CI's macOS job in round 479.) */
Vec3 along_axis(int axis, float v) { return Vec3(axis == 0 ? v : 0.0f, axis == 1 ? v : 0.0f, axis == 2 ? v : 0.0f); }

/* The Blend handle (round 481): one more handle on a piece that takes a Blend radius, a radius outside the point of the piece's
 * surface that faces the viewer most - but never one whose line runs along the view ray (the mouse could not pull it: see
 * Editor::blockout_handle_view), so a candidate looking straight at the viewer is passed over for the next. The candidates are
 * the faces' middles of a box or a cylinder (a cylinder's side too, half way to its silhouette) and a sphere's surface half way
 * to its silhouette. A radius of 0 still has a place: a tenth of the piece's reach out, so the handle can be taken and pulled. */
void add_blend_handle(const BlockoutPiece &p, Vec3 toward, std::vector<BlockoutHandle> &out) {
  if (!p.takes_blend()) return;
  struct Candidate {
    Vec3 at, dir;
  };
  std::vector<Candidate> cands;
  float reach = 0.0f;  // the piece's largest half extent
  if (p.shape == 0) {
    const Vec3 h(std::fabs(finite_or(p.size.x, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.y, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.z, 0.0f)) * 0.5f);
    reach = std::max(h.x, std::max(h.y, h.z));
    for (int k = 0; k < 6; k++) {
      const int axis = k / 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      cands.push_back({along_axis(axis, s * (axis == 0 ? h.x : axis == 1 ? h.y : h.z)), along_axis(axis, s)});
    }
  }
  else {
    const float r = std::fabs(finite_or(p.radius, 0.0f)), hh = p.shape == 1 ? std::fabs(finite_or(p.height, 0.0f)) * 0.5f : r;
    reach = std::max(r, hh);
    const Vec3 ft = flat_toward(toward);
    if (p.shape == 1) {
      cands.push_back({Vec3(0.0f, hh, 0.0f), Vec3(0, 1, 0)});
      cands.push_back({Vec3(0.0f, -hh, 0.0f), Vec3(0, -1, 0)});
      const Vec3 side(-ft.z, 0.0f, ft.x), mid = normalize(ft + side);
      cands.push_back({mid * r, mid});
    }
    else {
      Vec3 d = cross(toward, Vec3(0.0f, 1.0f, 0.0f));
      if (!(length(d) > 1e-4f * std::max(1e-6f, length(toward)))) d = cross(toward, Vec3(1.0f, 0.0f, 0.0f));
      const float l = length(d), lt = length(toward);
      d = l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
      const Vec3 t = lt > 1e-6f && finite_bits(lt) ? toward * (1.0f / lt) : Vec3(0.0f, 1.0f, 0.0f);
      Vec3 mid = t * 0.8f + d * 0.6f;
      mid = mid * (1.0f / length(mid));
      cands.push_back({mid * r, mid});
    }
  }
  const float blend = finite_bits(p.blend) ? std::max(0.0f, p.blend) : 0.0f;
  const float offset = std::max(blend, std::max(0.05f, 0.1f * reach));
  float best = -1e30f;
  const Candidate *pick = nullptr;
  for (const Candidate &c : cands) {
    const Vec3 view = toward - c.at;
    const float lv = length(view);
    const float facing = lv > 1e-9f && finite_bits(lv) ? dot(c.dir, view) / lv : 0.0f;
    if (facing > 0.95f || facing <= best) continue;
    best = facing, pick = &c;
  }
  if (!pick) pick = &cands.back();
  out.push_back(make_handle(BlockoutHandle::Blend, 0, pick->at + pick->dir * offset, pick->dir, "blend"));
}

}  // namespace

/* A preset's handles (round 500): its box's top, and the faces along X and Z or the radius that its main sizes are - what each does is blockout_handle_apply's preset case. */
static void preset_handles(const BlockoutPiece &p, std::vector<BlockoutHandle> &out) {
  using H = BlockoutHandle;
  Vec3 lo, hi;
  blockout_preset_bounds(p, lo, hi);
  const float mx = 0.5f * (lo.x + hi.x), mz = 0.5f * (lo.z + hi.z), my = 0.5f * (lo.y + hi.y);
  auto add = [&](int axis, Vec3 at, Vec3 dir, const char *name) { out.push_back(make_handle(H::Preset, axis, at, dir, name)); };
  const int k = p.preset;
  add(1, {mx, hi.y, mz}, {0, 1, 0}, "top");
  if (p.outline.size() >= 3 && (k == kPresetRailing || k == kPresetRoof)) return;  // (an outline's sides are its corners', not Length and Span, which it does not read: only the top)
  if (k == kPresetColumn || k == kPresetPilaster || k == kPresetSpiral) add(3, {hi.x, my, mz}, {1, 0, 0}, "side");
  if (k == kPresetBeam || k == kPresetRailing || k == kPresetRoof) {
    add(0, {hi.x, my, mz}, {1, 0, 0}, "+x");
    add(0, {lo.x, my, mz}, {-1, 0, 0}, "-x");
  }
  if (k == kPresetBeam || k == kPresetRoof || k == kPresetArcade) {
    add(2, {mx, my, hi.z}, {0, 0, 1}, "+z");
    add(2, {mx, my, lo.z}, {0, 0, -1}, "-z");
  }
  if (k == kPresetStairsL || k == kPresetStairsU) add(2, {mx, my, hi.z}, {0, 0, 1}, "+z");
}

float blockout_preset_handle_number(const BlockoutPiece &p, int axis) {
  const int k = p.preset;
  if (axis == 1) {
    switch (k) {
      case kPresetColumn:
      case kPresetPilaster:
      case kPresetArcade: return p.height;
      case kPresetBeam: return p.size.y;
      case kPresetRailing: return p.rail_height;
      case kPresetRoof: return blockout_roof_ridge(p);
      default: return p.total_height;
    }
  }
  if (axis == 3) return p.radius;
  if (axis == 0) return k == kPresetBeam ? p.size.x : p.length;
  return k == kPresetBeam ? p.size.z : k == kPresetRoof ? p.span : k == kPresetArcade ? p.depth : p.length;
}

void blockout_handles(const BlockoutPiece &p, Vec3 toward, const RoomGrowth *growth, std::vector<BlockoutHandle> &out) {
  out.clear();
  using H = BlockoutHandle;
  if (p.opening != 0) {
    const float w = std::fabs(finite_or(p.width, 0.0f)) * 0.5f, head = finite_or(p.head, 0.0f), sill = finite_or(p.sill, 0.0f), mid = 0.5f * (head + sill);
    out.push_back(make_handle(H::OpenWidth, 0, {w, mid, 0.0f}, {1, 0, 0}, "+x"));
    out.push_back(make_handle(H::OpenWidth, 0, {-w, mid, 0.0f}, {-1, 0, 0}, "-x"));
    out.push_back(make_handle(H::OpenHead, 1, {0.0f, head, 0.0f}, {0, 1, 0}, "head"));
    out.push_back(make_handle(H::OpenSill, 1, {0.0f, sill, 0.0f}, {0, -1, 0}, "sill"));
    return;
  }
  if (p.is_preset()) return preset_handles(p, out);
  const int shape = std::min(7, std::max(0, p.shape));
  const bool room = p.is_room() && growth;
  if (shape == 7) return;  // (a mesh piece is placed with its object, not stretched)
  if (shape == 5) {
    /* An arch: its outer radius at the top of the ring, its depth by the two faces along Z. */
    const float r = std::fabs(finite_or(p.radius, 0.0f)), hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    const float mid = 0.5f * (r + std::min(r, std::fabs(finite_or(p.inner_radius, 0.0f))));
    out.push_back(make_handle(H::CylRadius, 0, {0.0f, r, 0.0f}, {0, 1, 0}, "radius"));
    out.push_back(make_handle(H::CylCap, 2, {0.0f, mid, hh}, {0, 0, 1}, "+z"));
    out.push_back(make_handle(H::CylCap, 2, {0.0f, mid, -hh}, {0, 0, -1}, "-z"));
    return;
  }
  if (shape == 6) {
    /* An outline: a point handle on each corner of its top and a "+" in the middle of each side, and the height by the top and the
     * bottom. They lie in the top ring's plane, which a drag keeps. */
    const float hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    const size_t n = p.outline.size();
    float cx = 0.0f, cz = 0.0f;
    for (size_t i = 0; i < n; i++) {
      const Vec2 q = p.outline[i];
      if (!finite_bits(q.x) || !finite_bits(q.y)) continue;
      cx += q.x / (float)n, cz += q.y / (float)n;
      out.push_back(make_handle(H::OutlinePoint, (int)i, {q.x, hh, q.y}, {0, 1, 0}, ("p" + std::to_string(i)).c_str()));
      const Vec2 b = p.outline[(i + 1) % n];
      if (n < 256 && finite_bits(b.x) && finite_bits(b.y))
        out.push_back(make_handle(H::OutlineAdd, (int)i, {0.5f * (q.x + b.x), hh, 0.5f * (q.y + b.y)}, {0, 1, 0}, ("+" + std::to_string(i)).c_str()));
    }
    out.push_back(make_handle(H::CylCap, 1, {cx, hh, cz}, {0, 1, 0}, "top"));
    out.push_back(make_handle(H::CylCap, 1, {cx, -hh, cz}, {0, -1, 0}, "bottom"));
    return;
  }
  if (shape == 0 || shape == 3 || shape == 4) {
    const Vec3 h(std::fabs(finite_or(p.size.x, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.y, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.z, 0.0f)) * 0.5f);
    static const char *const names[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};
    for (int k = 0; k < 6; k++) {
      const int axis = k / 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      const float half = axis == 0 ? h.x : axis == 1 ? h.y : h.z;
      out.push_back(make_handle(H::BoxFace, axis, along_axis(axis, s * half), along_axis(axis, s), names[k]));
    }
    add_blend_handle(p, toward, out);
    if (!room) return;
    /* The wall nearest the viewer: its outer face's middle. */
    float best = 1e30f;
    Vec3 at(0.0f), dir(1.0f, 0.0f, 0.0f);
    for (int k = 0; k < 4; k++) {
      const int axis = k < 2 ? 0 : 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      const double g = axis == 0 ? growth->x : growth->z;
      const Vec3 a = along_axis(axis, s * ((axis == 0 ? h.x : h.z) + (float)g)), d = along_axis(axis, s);
      const float dist = length(toward - a);
      if (dist < best) best = dist, at = a, dir = d;
    }
    out.push_back(make_handle(H::Thickness, dir.x != 0.0f ? 0 : 2, at, dir, "wall"));
    return;
  }
  const float r = std::fabs(finite_or(p.radius, 0.0f));
  if (shape == 1) {
    const float hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    out.push_back(make_handle(H::CylCap, 1, {0.0f, hh, 0.0f}, {0, 1, 0}, "top"));
    out.push_back(make_handle(H::CylCap, 1, {0.0f, -hh, 0.0f}, {0, -1, 0}, "bottom"));
    /* On the silhouette - square to the view - so the handle's line is never the viewer's own: the mouse can always pull it. */
    const Vec3 ft = flat_toward(toward), d(-ft.z, 0.0f, ft.x);
    out.push_back(make_handle(H::CylRadius, 0, d * r, d, "side"));
    if (room) out.push_back(make_handle(H::Thickness, 0, d * (r + (float)growth->x), d, "wall"));
    add_blend_handle(p, toward, out);
    return;
  }
  /* A direction square to the view (the sphere's silhouette), turned about Y; about X when the viewer is straight above or below. */
  Vec3 d = cross(toward, Vec3(0.0f, 1.0f, 0.0f));
  if (!(length(d) > 1e-4f * std::max(1e-6f, length(toward)))) d = cross(toward, Vec3(1.0f, 0.0f, 0.0f));
  const float l = length(d);
  d = l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
  out.push_back(make_handle(H::SphereRadius, 0, d * r, d, "radius"));
  add_blend_handle(p, toward, out);
}

/* A preset's handle moved `delta` along its way (round 500). The top is the piece's main height (a roof's: its Ridge Height, which sets Pitch); a face along X or Z its length, width or
 * span (the opposite face stays, so the origin moves half way; with `both` they move together); "side" its radius. Returns the origin's move. */
static Vec3 preset_handle_apply(BlockoutPiece &p, const BlockoutPiece &before, const BlockoutHandle &h, float delta, bool both) {
  const int k = before.preset;
  auto grown = [&](float was, float d) { return std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, std::fabs(was) + d)); };
  if (h.axis == 1) {
    switch (k) {
      case kPresetColumn:
      case kPresetPilaster:
      case kPresetArcade: p.height = grown(before.height, delta); break;
      case kPresetBeam: p.size.y = grown(before.size.y, delta); break;
      case kPresetRailing: p.rail_height = grown(before.rail_height, delta); break;
      case kPresetRoof: blockout_roof_set_ridge(p, std::max(0.0f, blockout_roof_ridge(before) + delta)); break;
      default: p.total_height = grown(before.total_height, delta); break;
    }
    return Vec3(0.0f);
  }
  if (h.axis == 3) {
    p.radius = grown(before.radius, delta);
    if (k == kPresetSpiral && p.inner_radius >= p.radius) p.inner_radius = std::max(0.0f, p.radius - kBlockoutMinSize);
    return Vec3(0.0f);
  }
  const float step = both ? 2.0f * delta : delta;
  float *field = nullptr;
  float was = 0.0f;
  if (h.axis == 0) field = k == kPresetBeam ? &p.size.x : &p.length, was = k == kPresetBeam ? before.size.x : before.length;
  else if (k == kPresetBeam) field = &p.size.z, was = before.size.z;
  else if (k == kPresetRoof) field = &p.span, was = before.span;
  else if (k == kPresetArcade) field = &p.depth, was = before.depth;
  else field = &p.length, was = before.length;  // (stairs: the run, from the start the piece's origin stands on)
  const float now = grown(was, step);
  *field = now;
  if (both || k == kPresetStairsL || k == kPresetStairsU) return Vec3(0.0f);
  return h.dir * (0.5f * (now - std::fabs(was)));
}

Vec3 blockout_handle_apply(BlockoutPiece &p, const BlockoutPiece &before, const BlockoutHandle &h, float delta, bool both, float to_blockout_scale) {
  p = before;
  if (!finite_bits(delta)) return Vec3(0.0f);
  const float k = both ? 2.0f : 1.0f;
  /* A size moved by k * delta, stopped at the smallest allowed; the delta that was really made follows from it. */
  auto stretch = [&](float &size, float was) {
    const float now = std::max(kBlockoutMinSize, was + k * delta);
    size = now;
    return (now - was) / k;
  };
  switch (h.kind) {
    case BlockoutHandle::BoxFace: {
      const float eff = stretch(p.size[h.axis], std::fabs(before.size[h.axis]));
      return both ? Vec3(0.0f) : h.dir * (eff * 0.5f);
    }
    case BlockoutHandle::CylCap: {
      const float eff = stretch(p.height, std::fabs(before.height));
      return both ? Vec3(0.0f) : h.dir * (eff * 0.5f);
    }
    case BlockoutHandle::CylRadius:
    case BlockoutHandle::SphereRadius:
      p.radius = std::max(kBlockoutMinSize, std::fabs(before.radius) + delta);  // (the centre stays: both sides move)
      return Vec3(0.0f);
    case BlockoutHandle::Thickness:
      p.wall = std::min(1000.0f, std::max(0.0f, safe_thickness(before.wall) + delta * (finite_bits(to_blockout_scale) ? to_blockout_scale : 1.0f)));
      return Vec3(0.0f);
    case BlockoutHandle::Blend: {
      /* In the Blockout's space, like the radius itself: the delta (own units along the handle) times the way's scale there. */
      const float was = finite_bits(before.blend) ? std::max(0.0f, before.blend) : 0.0f;
      p.blend = std::min(100.0f, std::max(0.0f, was + delta * (finite_bits(to_blockout_scale) ? to_blockout_scale : 1.0f)));
      return Vec3(0.0f);
    }
    case BlockoutHandle::OpenWidth:
      p.width = std::max(kBlockoutMinSize, std::fabs(before.width) + 2.0f * delta);  // (both ends: the opening stays centred)
      return Vec3(0.0f);
    case BlockoutHandle::OpenHead:
      p.head = std::max(before.sill + kBlockoutMinSize, before.head + delta);
      return Vec3(0.0f);
    case BlockoutHandle::OpenSill:
      p.sill = std::min(before.head - kBlockoutMinSize, std::max(0.0f, before.sill - delta));  // (the handle points down)
      return Vec3(0.0f);
    case BlockoutHandle::Preset: return preset_handle_apply(p, before, h, delta, both);
    case BlockoutHandle::OutlinePoint:  // (these move in a plane: blockout_outline_move / _insert)
    case BlockoutHandle::OutlineAdd:
      return Vec3(0.0f);
  }
  return Vec3(0.0f);
}

/* ===================================================================== */
/* Materials, UVs and shading of the result (round 482)                   */
/* ===================================================================== */

int blockout_face_class(const blockout::Piece &piece, uint32_t side) {
  if (blockout_id_generated(piece.id) && window_part(piece.id) != 0 && side != blockout::kSideBlend)  // (part 0: a plain opening's copy, round 546 - a plain piece)
    return window_part(piece.id) & 0x40 ? blockout_preset_face_class(piece, side) : window_face_class(piece, side);
  const bool plain = !piece.room || side == blockout::kSideBlend;  // (a blend only comes from a piece that is no room)
  if (plain) return piece.op == blockout::Op::Add ? kClassSolid : kClassCut;
  switch (blockout::side_class(piece, side)) {
    case blockout::SideClass::Floor: return kClassFloor;
    case blockout::SideClass::Wall: return kClassWalls;
    case blockout::SideClass::Ceiling: return kClassCeiling;
    case blockout::SideClass::Outside: return kClassOutside;
    case blockout::SideClass::Cut: return kClassCut;
  }
  return kClassOutside;
}

namespace {

/* The texture coordinates of a face's corners, on the face's own plane: the basis comes from the face's unit normal alone, so metres over the
 * scale are the coordinates on any flat face - square to the axes or turned, a ramp's slope too - and the faces on opposite sides of a wall
 * each read the way the Cube's faces do. It is a pure function of the face's own corners, its piece's offset and the scale, so a face that kept
 * its corners keeps its UVs to the bit. A normal within 1e-6 of an axis in cosine (about 0.08 degrees) is that axis exactly, so two faces of
 * different pieces on one axis plane get the same basis and agree at shared corners (a turned plane only to rounding). A face within about 2.6
 * degrees of horizontal (|n.y| > 0.999) takes the top / bottom basis: on a nearly flat ramp the texture turns there, a seam at that angle. A
 * face of a blended join is not flat: it is projected on its own average plane, and where it meets a neighbour of another plane the texture has
 * a seam - accepted. */
void project_face(Mesh &mesh, size_t f, double scale, const BlockoutSurface::Piece *piece) {
  const uint32_t first = mesh.face_offsets[f], last = mesh.face_offsets[f + 1];
  double n[3] = {0, 0, 0};  // Newell's normal: right for n-gons with many corners
  for (uint32_t c = first; c < last; c++) {
    const Vec3 p = mesh.positions[mesh.corner_verts[c]], q = mesh.positions[mesh.corner_verts[c + 1 < last ? c + 1 : first]];
    n[0] += ((double)p.y - q.y) * ((double)p.z + q.z);
    n[1] += ((double)p.z - q.z) * ((double)p.x + q.x);
    n[2] += ((double)p.x - q.x) * ((double)p.y + q.y);
  }
  const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (!(len > 0.0) || !std::isfinite(len)) n[0] = 0, n[1] = 1, n[2] = 0;  // (a face of no area: any plane)
  else n[0] /= len, n[1] /= len, n[2] /= len;
  for (int a = 0; a < 3; a++)
    if (std::fabs(n[a]) > 1.0 - 1e-6) {  // (snapped: an exact axis)
      const double sign = n[a] < 0 ? -1.0 : 1.0;
      n[0] = n[1] = n[2] = 0.0, n[a] = sign;
      break;
    }
  /* The directions U and V grow along are the engine's Cube primitive's on each of its six faces (primitives::cube, UvKind::CubeCross), so a texture
   * reads the same way on a Blockout wall as on a cube beside it: on a face that stands up, U along up x normal and V up; on one that looks up or
   * down, U along -X and V along -Z (up) or +Z (down). (The Cube's top and bottom are mirrored against its sides by the cross product; matched anyway.) */
  const bool vertical = std::fabs(n[1]) > 0.999;
  double u_axis[3], v_axis[3];
  if (vertical) {
    const double d = -n[0];  // (-X taken into the face's plane)
    u_axis[0] = -1.0 - n[0] * d, u_axis[1] = -n[1] * d, u_axis[2] = -n[2] * d;
  }
  else u_axis[0] = n[2], u_axis[1] = 0.0, u_axis[2] = -n[0];  // (up x normal)
  const double ul = std::sqrt(u_axis[0] * u_axis[0] + u_axis[1] * u_axis[1] + u_axis[2] * u_axis[2]);
  for (double &x : u_axis) x /= ul > 0.0 ? ul : 1.0;
  if (vertical) {  // (V = U x normal)
    v_axis[0] = u_axis[1] * n[2] - u_axis[2] * n[1], v_axis[1] = u_axis[2] * n[0] - u_axis[0] * n[2], v_axis[2] = u_axis[0] * n[1] - u_axis[1] * n[0];
  }
  else {  // (V = normal x U: up)
    v_axis[0] = n[1] * u_axis[2] - n[2] * u_axis[1], v_axis[1] = n[2] * u_axis[0] - n[0] * u_axis[2], v_axis[2] = n[0] * u_axis[1] - n[1] * u_axis[0];
  }
  const bool turned = piece && piece->rotation != 0.0f;
  const double rad = turned ? (double)piece->rotation * 3.14159265358979323846 / 180.0 : 0.0;
  const double cs = std::cos(rad), sn = std::sin(rad);
  for (uint32_t c = first; c < last; c++) {
    const Vec3 p = mesh.positions[mesh.corner_verts[c]];
    double u = ((double)p.x * u_axis[0] + (double)p.y * u_axis[1] + (double)p.z * u_axis[2]) / scale;
    double v = ((double)p.x * v_axis[0] + (double)p.y * v_axis[1] + (double)p.z * v_axis[2]) / scale;
    if (turned) {
      const double ru = u * cs - v * sn, rv = u * sn + v * cs;
      u = ru, v = rv;
    }
    if (piece) u += piece->offset_u, v += piece->offset_v;
    mesh.uvs[c] = Vec2((float)u, (float)v);
  }
}

/* Whether a face on this side of the piece is one of a curved surface (a cylinder's side, a sphere, an arch's two curves): those shade smooth. */
bool curved_side(const blockout::Piece &p, uint32_t side) {
  if (side == blockout::kSideBlend) return false;
  const uint32_t base = side >= blockout::kSideShell ? side - blockout::kSideShell : side;
  switch (p.shape) {
    case blockout::Shape::Sphere: return true;
    case blockout::Shape::Cylinder: return base == 0;
    case blockout::Shape::Arch: return base == 0 || base == 1;
    default: return false;
  }
}

}  // namespace

std::vector<std::string> blockout_finish_mesh(Mesh &mesh, const blockout::Faces &faces, const std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface) {
  const size_t count = mesh.face_count();
  const bool named = faces.piece.size() == count && faces.side.size() == count;
  const bool own = surface.pieces.size() == pieces.size();  // (the surface's pieces are the snapshot's, in its order)
  std::unordered_map<uint64_t, size_t> index;
  for (size_t i = 0; i < pieces.size(); i++) index.emplace(pieces[i].id, i);
  /* The candidates for a face's material, in the fixed order: the Blockout's six, then each piece's six. */
  const size_t candidates = (size_t)kBlockoutClasses * (1 + (own ? pieces.size() : 0));
  std::vector<uint8_t> used(candidates, 0);
  std::vector<uint32_t> face_candidate(count, (uint32_t)kClassOutside);
  mesh.uvs.assign(mesh.corner_verts.size(), Vec2(0.0f, 0.0f));
  const double scale = surface.scale > 0.0f ? (double)surface.scale : 1.0;
  for (size_t f = 0; f < count; f++) {
    const auto at = named ? index.find(faces.piece[f]) : index.end();
    const BlockoutSurface::Piece *sp = own && at != index.end() ? &surface.pieces[at->second] : nullptr;
    if (at != index.end()) {
      const int cls = blockout_face_class(pieces[at->second], faces.side[f]);
      const bool overridden = sp && !sp->materials[cls].empty();
      face_candidate[f] = overridden ? (uint32_t)(kBlockoutClasses * (1 + at->second) + (size_t)cls) : (uint32_t)cls;
    }
    used[face_candidate[f]] = 1;
    project_face(mesh, f, scale, sp);
  }
  auto path_of = [&](size_t cand) -> const std::string & {
    if (cand < (size_t)kBlockoutClasses) return surface.materials[cand];
    return surface.pieces[cand / kBlockoutClasses - 1].materials[cand % kBlockoutClasses];
  };
  std::vector<std::string> slots;
  std::vector<int32_t> slot_of(candidates, 0);
  for (size_t cand = 0; cand < candidates; cand++) {
    if (!used[cand]) continue;
    const std::string &path = path_of(cand);
    const auto same = std::find(slots.begin(), slots.end(), path);
    slot_of[cand] = (int32_t)(same - slots.begin());
    if (same == slots.end()) slots.push_back(path);
  }
  mesh.face_material.assign(count, 0);
  for (size_t f = 0; f < count; f++) mesh.face_material[f] = slot_of[face_candidate[f]];
  /* Smooth by angle (40 degrees) for the faces that are not flat - a blended join, a curved side - and flat for every other face, whatever
   * angle it meets its neighbour at (a 30 degree ramp does not shade into its floor). */
  mesh.smooth = true, mesh.smooth_angle = 40.0f;
  mesh.face_smooth.assign(count, 0);
  for (size_t f = 0; f < count && named; f++) {
    const auto at = index.find(faces.piece[f]);
    const bool flat = f < faces.flat.size() ? faces.flat[f] != 0 : true;
    mesh.face_smooth[f] = !flat || faces.side[f] == blockout::kSideBlend || (at != index.end() && curved_side(pieces[at->second], faces.side[f])) ||
                          (at != index.end() && blockout_piece_stacked(surface, at->second, pieces[at->second]));  // (round 514: a stacked piece shades by angle - not one whose stack was refused)
  }
  mesh.touch();
  return slots;
}

/* ===================================================================== */
/* A door's leaf as an object (round 515, task 0497, ADR 0136)            */
/* ===================================================================== */

void BlockoutLeaf::reflect(Reflector &r) {
  if (r.inspector()) {
    /* The Inspector shows the door's own numbers here (editing them edits the door): the leaf has none of its own to edit. The note above them, and the door they belong to, is
     * drawn by the editor (draw_blockout_leaf_extras). */
    const GameObject *door_object = owner ? owner->parent : nullptr;
    BlockoutPiece *door_piece = door_object ? door_object->get<BlockoutPiece>() : nullptr;
    if (!door_piece || !door_piece->is_styled_door()) return;
    r.field("Open Amount", door_piece->open_amount, 0.01f, 0.0f, 1.0f);
    r.help("The door's Open Amount: 0 closed to 1 open. Turning this leaf with the Rotate tool writes the angle here; typing it here turns the leaf.");
    if (door_piece->style == kDoorSingle || door_piece->style == kDoorArched) {
      r.enumeration("Hinge Side", door_piece->hinge_side, kBlockoutSideNames, 2);
      r.help("The door's Hinge Side: which jamb the leaf hangs on. Changing it makes the leaf again on the other jamb.");
    }
    return;
  }
  r.field("Index", index, 0, 255);
  r.text("Made From", made_from);
  r.field("Open Seen", open_seen, 0.01f, 0.0f, 1.0f);
  r.text("Door", door);
  r.text("Made By", made_by);
}

float blockout_door_open(const BlockoutPiece &p) { return finite_bits(p.open_amount) ? std::min(1.0f, std::max(0.0f, p.open_amount)) : 0.0f; }

namespace {

constexpr float kHalfPi = 1.57079632679489661923f;

BlockoutLeafPlan make_leaf_plan(BlockoutLeafPlan::Kind kind, int index, const char *name, Vec3 origin, float sign, float travel) {
  BlockoutLeafPlan p;
  p.kind = kind, p.index = index, p.name = name, p.origin = origin, p.sign = sign, p.travel = travel;
  return p;
}

/* A slab in the leaf's own space (low and high corners), kept only when none of its sides is thinner than a part the kernel would also leave out. */
void add_slab(BlockoutLeafPlan &p, Vec3 lo, Vec3 hi) {
  if (hi.x - lo.x > kSmallest && hi.y - lo.y > kSmallest && hi.z - lo.z > kSmallest) p.boxes.push_back({lo, hi});
}

}  // namespace

void blockout_leaf_plans(const BlockoutPiece &bp, std::vector<BlockoutLeafPlan> &out) {
  out.clear();
  WindowShape s;
  if (!bp.is_styled_door() || !bp.leaf_on() || styled_too_large(bp) || !window_shape(bp, s)) return;
  using Plan = BlockoutLeafPlan;
  if (s.revolving) {  // (the wings: a post and four vanes in a cross, turning about the drum's axis; they stop just short of the drum's wall)
    const float r = 0.96f * s.drum, t = std::min(s.gt, r), h = s.head - s.sill;
    Plan p = make_leaf_plan(Plan::Wings, 0, "Wings", Vec3(0.0f, s.sill, 0.0f), 1.0f, 0.0f);
    add_slab(p, Vec3(-0.5f * t, 0.0f, -0.5f * t), Vec3(0.5f * t, h, 0.5f * t));
    add_slab(p, Vec3(0.5f * t, 0.0f, -0.5f * t), Vec3(r, h, 0.5f * t));
    add_slab(p, Vec3(-r, 0.0f, -0.5f * t), Vec3(-0.5f * t, h, 0.5f * t));
    add_slab(p, Vec3(-0.5f * t, 0.0f, 0.5f * t), Vec3(0.5f * t, h, r));
    add_slab(p, Vec3(-0.5f * t, 0.0f, -r), Vec3(0.5f * t, h, -0.5f * t));
    if (!p.boxes.empty()) out.push_back(std::move(p));
    return;
  }
  if (s.sliding) {  // (a slab on the wall's face, its back on the plane the reveal stops at, a frame's width wider than the opening each side and as much above it)
    const float cover = s.fw, width = s.w + 2.0f * cover, face = std::max(0.5f * s.fd, 0.5f * s.depth - kOpeningOvershoot);
    Plan p = make_leaf_plan(Plan::Slide, 0, "Leaf", Vec3(0.0f, s.sill, face), s.slide == 1 ? -1.0f : 1.0f, width);  // (Left is +X: see Hinge Side)
    add_slab(p, Vec3(-0.5f * width, 0.0f, 0.0f), Vec3(0.5f * width, s.head + cover - s.sill, s.gt));
    if (!p.boxes.empty()) out.push_back(std::move(p));
    return;
  }
  /* Hinged: the clear opening between the jambs, from the floor line to the frame's head (an Arched door's: to the line its round head springs from). Each leaf hangs on a jamb's inner
   * edge, at the frame's middle depth, and reaches toward the other jamb. */
  const float clear = s.w - 2.0f * s.fw;
  const float top = s.arched ? std::max(s.head - s.rise, s.sill) : s.head - s.fw;
  if (!(clear >= kBlockoutMinSize) || !(top - s.sill > kBlockoutMinSize)) return;
  auto hinged = [&](int index, const char *name, float sign, float length) {
    Plan p = make_leaf_plan(Plan::Swing, index, name, Vec3(sign * 0.5f * clear, s.sill, 0.0f), sign, 0.0f);
    const float far = -sign * length;
    add_slab(p, Vec3(std::min(0.0f, far), 0.0f, -0.5f * s.gt), Vec3(std::max(0.0f, far), top - s.sill, 0.5f * s.gt));
    if (!p.boxes.empty()) out.push_back(std::move(p));
  };
  if (s.doubled) {  // (the +X jamb is Left, as for a Single: Leaf L hangs there)
    hinged(0, "Leaf L", 1.0f, 0.5f * clear);
    hinged(1, "Leaf R", -1.0f, 0.5f * clear);
  }
  else hinged(0, "Leaf", s.hinge == 1 ? -1.0f : 1.0f, clear);
}

void blockout_leaf_pose(const BlockoutLeafPlan &plan, float open, Vec3 &position, float &yaw_degrees) {
  position = plan.origin, yaw_degrees = 0.0f;
  if (plan.kind == BlockoutLeafPlan::Slide) position.x += plan.sign * open * plan.travel;
  else yaw_degrees = plan.sign * open * 90.0f;
}

bool blockout_leaf_open_of(const BlockoutLeafPlan &plan, const Transform &local, float &open) {
  if (plan.kind == BlockoutLeafPlan::Slide) return false;
  const Vec3 forward = local.rotation.rotate(Vec3(0.0f, 0.0f, 1.0f)), up = local.rotation.rotate(Vec3(0.0f, 1.0f, 0.0f));
  if (!(up.y > 0.9995f)) return false;  // (tipped over by hand: it is just an object turned)
  open = plan.sign * std::atan2(forward.x, forward.z) / kHalfPi;
  return finite_bits(open);
}

MeshPtr blockout_leaf_mesh(const BlockoutLeafPlan &plan, float texture_scale) {
  auto mesh = std::make_shared<Mesh>();
  mesh->name = plan.name;
  static const uint32_t kFaces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
  for (const auto &box : plan.boxes) {
    const Vec3 lo = box.first, hi = box.second, centre = (lo + hi) * 0.5f;
    const uint32_t first = (uint32_t)mesh->positions.size();
    for (int i = 0; i < 8; i++) mesh->add_vert(Vec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
    for (const auto &f : kFaces) {
      uint32_t v[4] = {first + f[0], first + f[1], first + f[2], first + f[3]};
      const Vec3 a = mesh->positions[v[0]], b = mesh->positions[v[1]], c = mesh->positions[v[2]];
      if (dot(cross(b - a, c - a), (a + c) * 0.5f - centre) < 0.0f) std::swap(v[1], v[3]);  // (faces outward, as the Cube's)
      mesh->add_face({v[0], v[1], v[2], v[3]});
    }
  }
  mesh->uvs.assign(mesh->corner_verts.size(), Vec2(0.0f, 0.0f));
  const double scale = texture_scale > 0.0f && finite_bits(texture_scale) ? (double)texture_scale : 1.0;
  for (size_t f = 0; f < mesh->face_count(); f++) project_face(*mesh, f, scale, nullptr);
  mesh->face_material.assign(mesh->face_count(), 0);
  mesh->touch();
  return mesh;
}

uint64_t blockout_leaf_key(const BlockoutLeafPlan &plan, float texture_scale, const std::string &material) {
  uint64_t h = hash_mix(kFnvBasis, (uint64_t)plan.kind | (uint64_t)(uint32_t)plan.index << 8 | (uint64_t)plan.boxes.size() << 40);
  h = fnv1a(plan.name, std::strlen(plan.name), h);
  for (int a = 0; a < 3; a++) h = mix_float(h, plan.origin[a]);
  h = mix_float(mix_float(h, plan.sign), plan.travel);
  for (const auto &box : plan.boxes)
    for (int a = 0; a < 3; a++) h = mix_float(mix_float(h, box.first[a]), box.second[a]);
  h = mix_float(h, texture_scale);
  return fnv1a(material.data(), material.size(), h);
}

uint64_t blockout_mesh_hash(const Mesh &m) {
  uint64_t h = hash_mix(kFnvBasis, m.positions.size());
  h = mix_bytes(h, m.positions.data(), m.positions.size() * sizeof(Vec3));
  h = hash_mix(h, m.face_count());
  h = mix_bytes(h, m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  h = mix_bytes(h, m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  h = mix_bytes(h, m.uvs.data(), m.uvs.size() * sizeof(Vec2));
  h = mix_bytes(h, m.face_material.data(), m.face_material.size() * sizeof(int32_t));
  h = mix_bytes(h, m.face_smooth.data(), m.face_smooth.size());
  h = mix_bytes(h, m.sharp_edges.data(), m.sharp_edges.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.seams.data(), m.seams.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.loose_edges.data(), m.loose_edges.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.custom_normals.data(), m.custom_normals.size() * sizeof(Vec3));
  /* An all-zero polygroup list or sculpt mask is no list and no mask (the scene file drops them): equal to none for the hash. */
  const bool any_group = std::any_of(m.face_group.begin(), m.face_group.end(), [](int32_t v) { return v != 0; });
  const bool any_mask = std::any_of(m.sculpt_mask.begin(), m.sculpt_mask.end(), [](float v) { return v != 0.0f; });
  if (any_group) h = mix_bytes(h, m.face_group.data(), m.face_group.size() * sizeof(int32_t));
  if (any_mask) h = mix_bytes(h, m.sculpt_mask.data(), m.sculpt_mask.size() * sizeof(float));
  h = mix_float(h, m.smooth_angle);
  for (const VertexGroup &g : m.vertex_groups) h = mix_bytes(hash_mix(h, fnv1a(g.name.data(), g.name.size())), g.weights.data(), g.weights.size() * sizeof(float));
  for (const ShapeKey &k : m.shape_keys) h = mix_bytes(hash_mix(h, fnv1a(k.name.data(), k.name.size())), k.delta.data(), k.delta.size() * sizeof(Vec3));
  return hash_mix(h, (uint64_t)m.smooth | (uint64_t)m.seams_sharp << 1 | (uint64_t)m.vertex_groups.size() << 8 | (uint64_t)m.shape_keys.size() << 24);
}

GameObject *blockout_pieces_holder(const GameObject &baked) {
  const BlockoutBaked *bk = baked.get<BlockoutBaked>();
  const uint64_t id = bk ? std::strtoull(bk->pieces_id.c_str(), nullptr, 10) : 0;
  GameObject *by_name = nullptr;
  for (GameObject *c : baked.children) {
    if (id && c->id == id) return c;  // (a child, so a number that means another object in a reloaded scene is not taken)
    if (!by_name && c->name == kBlockoutPiecesName && !c->get<Blockout>()) by_name = c;
  }
  return by_name;
}

void blockout_baked_sources(const GameObject &baked, std::vector<uint64_t> &out) {
  const GameObject *holder = blockout_pieces_holder(baked);
  if (!holder) return;
  std::function<void(const GameObject &)> walk = [&](const GameObject &g) {
    for (const GameObject *c : g.children) {
      if (!c->active || c->get<Blockout>()) continue;
      const BlockoutPiece *bp = c->get<BlockoutPiece>();
      if (bp && bp->enabled && bp->shape == (int)blockout::Shape::Mesh && bp->opening == 0)
        if (const GameObject *src = resolve_mesh_piece(*bp, nullptr)) out.push_back(src->id);
      walk(*c);
    }
  };
  walk(*holder);
}

const BlockoutHandle *blockout_handle_named(const std::vector<BlockoutHandle> &handles, const std::string &name) {
  std::string n;
  for (char c : name) n += (char)std::tolower((unsigned char)c);
  if (n.size() == 2 && (n[1] == '+' || n[1] == '-')) std::swap(n[0], n[1]);  // ("x+" is "+x")
  if (n == "radius") n = "side";
  for (const BlockoutHandle &h : handles)
    if (h.name == n || (n == "side" && h.name == "radius")) return &h;
  if (n == "width")
    for (const BlockoutHandle &h : handles)
      if (h.kind == BlockoutHandle::OpenWidth && h.name == "+x") return &h;
  return nullptr;
}

/* ===================================================================== */
/* Scale (round 496, task 0480, ADR 0135)                                 */
/* ===================================================================== */

namespace {
/* Two round axes in one radius: the one that changed more, so a cylinder scaled along X (or Z) stays round and a scale of both takes the larger. */
float round_pick(float a, float b) {
  const float la = std::fabs(std::log(std::max(a, 1e-12f))), lb = std::fabs(std::log(std::max(b, 1e-12f)));
  return la >= lb ? a : b;
}
}  // namespace

bool blockout_piece_scale(BlockoutPiece &p, Vec3 f) {
  if (p.shape == (int)blockout::Shape::Mesh && p.opening == 0) return false;
  for (int a = 0; a < 3; a++) f[a] = finite_bits(f[a]) ? std::fabs(f[a]) : 1.0f;
  /* A number grown, stopped at the smallest size; a factor of exactly 1 leaves it as it is (a scale by nothing changes no bit). */
  auto grow = [](float &v, float k) {
    if (k == 1.0f || !finite_bits(v)) return;
    v = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, std::fabs(v) * k));
  };
  auto place = [](float &v, float k) {  // (an outline's corner: any sign, kept in range)
    if (finite_bits(v)) v = std::max(-kBlockoutMaxSize, std::min(kBlockoutMaxSize, v * k));
  };
  auto round_k = [](float a, float b) { return round_pick(a, b); };
  if (p.opening != 0) {  // (a door or window: width across, sill to head up, depth through the wall)
    grow(p.width, f.x);
    if (f.y != 1.0f && finite_bits(p.sill)) p.sill = std::min(kBlockoutMaxSize - kBlockoutMinSize, std::max(0.0f, p.sill * f.y));
    if (f.y != 1.0f && finite_bits(p.head))
      p.head = std::min(kBlockoutMaxSize, std::max(finite_bits(p.sill) ? p.sill + kBlockoutMinSize : kBlockoutMinSize, p.head * f.y));
    /* Depth is the wall's thickness + 0.2 (0.1 m through each face, blockout_stand_opening): the scale grows the wall part and keeps the overshoot. */
    if (f.z != 1.0f && finite_bits(p.depth) && std::fabs(p.depth) > 2.0f * kOpeningOvershoot)
      p.depth = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, (std::fabs(p.depth) - 2.0f * kOpeningOvershoot) * f.z + 2.0f * kOpeningOvershoot));
    else grow(p.depth, f.z);
    /* Round 498: a window's style numbers grow with it - the frame's width in the window's plane, its depth and the board's reach through the wall, the head's rise
     * with the height. (Pane counts stay: a bigger window has bigger panes.) A zero rise is a half circle and follows the width by itself. */
    if (p.opening == 2) {
      grow(p.frame_width, round_k(f.x, f.y));
      grow(p.frame_depth, f.z);
      if (p.sill_overhang > 0.0f) grow(p.sill_overhang, round_k(f.x, f.z));  // (0 stays 0)
      if (p.arch_rise > 0.0f) grow(p.arch_rise, f.y);
    }
    /* Round 499: a door's frame and head as a window's; the leaf's thickness through the wall (as the frame's depth); the drum's radius with the width (a round
     * piece: the width's factor, so a drum scaled with its door stays round). Open Amount and the sides are not sizes. */
    if (p.opening == 1) {
      grow(p.frame_width, round_k(f.x, f.y));
      grow(p.frame_depth, f.z);
      grow(p.leaf_thickness, f.z);
      grow(p.drum_radius, f.x);
      if (p.arch_rise > 0.0f) grow(p.arch_rise, f.y);
    }
    return true;
  }
  if (p.is_preset()) {
    /* Round 500: sizes grow along their own axes - Post Spacing too (it is a size: a railing scaled by 2 has the same posts twice as far apart) - and a size of 0 (no plinth, a solid
     * roof, no overhang, no post) stays 0. Counts, angles and Taper stay. A roof's Pitch follows the ratio of its rise to its run (f.y over f.z); an Arcade's half circle (Arch Rise 0)
     * is made explicit when the scale is uneven, so that it grows with f.y and not f.x. */
    const float xz = round_k(f.x, f.z);
    auto grow = [&](float &v, float k) {
      if (v == 0.0f) return;
      if (k == 1.0f || !finite_bits(v)) return;
      v = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, std::fabs(v) * k));
    };
    switch (p.preset) {
      case kPresetColumn:
      case kPresetPilaster:
        grow(p.radius, xz), grow(p.height, f.y), grow(p.base_height, f.y), grow(p.capital_height, f.y), grow(p.base_width, xz), grow(p.capital_width, xz);
        break;
      case kPresetBeam:
        for (int a = 0; a < 3; a++) grow(p.size[a], f[a]);
        break;
      case kPresetRailing:
        grow(p.length, f.x), grow(p.rail_height, f.y), grow(p.post_size, xz), grow(p.rail_size, xz), grow(p.post_spacing, f.x);
        for (Vec2 &q : p.outline) place(q.x, f.x), place(q.y, f.z);
        break;
      case kPresetRoof:
        grow(p.length, f.x), grow(p.span, f.z), grow(p.overhang, xz), grow(p.thickness, f.y);
        for (Vec2 &q : p.outline) place(q.x, f.x), place(q.y, f.z);
        if (f.y != f.z && finite_bits(p.pitch))
          p.pitch = std::min(85.0f, std::max(0.0f, std::atan(std::tan(std::min(85.0f, std::max(0.0f, p.pitch)) * 3.14159265f / 180.0f) * f.y / std::max(1e-6f, f.z)) * 180.0f / 3.14159265f));
        break;
      case kPresetStairsL:
      case kPresetStairsU: grow(p.stair_width, xz), grow(p.total_height, f.y), grow(p.length, f.z); break;  // (the second flight's width runs along Z: the width follows the farther of X and Z)
      case kPresetSpiral: grow(p.radius, xz), grow(p.inner_radius, xz), grow(p.total_height, f.y); break;
      case kPresetArcade:
        if (p.arch_rise == 0.0f && f.y != f.x && finite_bits(p.span)) p.arch_rise = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, 0.5f * std::fabs(p.span) * f.y));
        else if (p.arch_rise > 0.0f) grow(p.arch_rise, f.y);
        grow(p.span, f.x), grow(p.column_width, f.x), grow(p.arch_thickness, f.x), grow(p.height, f.y), grow(p.depth, f.z);
        break;
      default: break;
    }
    return true;
  }
  switch ((blockout::Shape)p.shape) {
    case blockout::Shape::Convex:
      return false;
    case blockout::Shape::Box:
    case blockout::Shape::Wedge:
    case blockout::Shape::Stairs:
      for (int a = 0; a < 3; a++) grow(p.size[a], f[a]);
      break;
    case blockout::Shape::Cylinder:
      grow(p.radius, round_k(f.x, f.z));
      grow(p.height, f.y);
      break;
    case blockout::Shape::Sphere:
      grow(p.radius, round_k(round_k(f.x, f.y), f.z));
      break;
    case blockout::Shape::Arch: {
      const float k = round_k(f.x, f.y);  // (the ring lies in the piece's XY plane)
      grow(p.radius, k);
      /* The inner radius may be 0 (a filled arch, the Subtract tool's): it scales with a floor of 0 and stays below the radius, as F9 keeps it. */
      if (k != 1.0f && finite_bits(p.inner_radius) && finite_bits(p.radius))
        p.inner_radius = std::max(0.0f, std::min(std::fabs(p.inner_radius) * k, p.radius - kBlockoutMinSize));
      grow(p.height, f.z);  // (an arch's depth)
      break;
    }
    case blockout::Shape::Outline:
      for (Vec2 &q : p.outline) {
        place(q.x, f.x);
        place(q.y, f.z);  // (the outline's second number is z)
      }
      grow(p.height, f.y);
      break;
    case blockout::Shape::Mesh:
      return false;
  }
  if (finite_bits(p.blend) && p.blend > 0.0f) p.blend = std::min(100.0f, p.blend * std::min(f.x, std::min(f.y, f.z)));
  return true;
}

namespace {

bool is_numbers_piece(const GameObject &g) {
  const BlockoutPiece *bp = g.get<BlockoutPiece>();
  return bp && (bp->shape != (int)blockout::Shape::Mesh || bp->opening != 0);
}

bool has_piece_below(const GameObject &g) {
  for (const GameObject *c : g.children)
    if (c->get<BlockoutPiece>() || has_piece_below(*c)) return true;
  return false;
}

/* An object with geometry of its own (a mesh piece, a plain mesh object, a baked Blockout) that holds pieces: its scale is the geometry's, so it grows
 * as a Leaf's does and is no Carrier (the pieces below follow through their world axes). A Blockout is no such object though it has a mesh. */
bool has_own_geometry(const GameObject &g) {
  if (g.get<Blockout>()) return false;
  if (g.get<BlockoutPiece>()) return !is_numbers_piece(g);
  return g.get<MeshFilter>() || g.get<BlockoutBaked>();
}

bool is_carrier(const GameObject &g) { return g.get<Blockout>() || (has_piece_below(g) && !has_own_geometry(g)); }

/* A scale that goes through the numbers: a Blockout (or anything inside one) that is a piece with numbers, is a Blockout, or has pieces below it. */
bool takes_scale_in_numbers(const GameObject &g) {
  if (g.get<Blockout>()) return true;
  if (!blockout_of_piece(g)) return false;
  return is_numbers_piece(g) || has_piece_below(g);
}

void collect_scale(const GameObject &g, BlockoutScaleSet &out, Vec3 outer) {
  if (out.has(g.id)) return;
  if (g.get<BlockoutLeaf>()) return;  // (round 515: a door's leaf object takes its size and place from the door's numbers, which the scale changes: the editor makes it again)
  if (g.get<Blockout>()) {  // (the scale a fold takes out of this Blockout's own transform: what a room's wall in its space grows by)
    const Vec3 s = g.local().scale;
    for (int a = 0; a < 3; a++) outer[a] *= finite_bits(s[a]) && s[a] != 0.0f ? std::fabs(s[a]) : 1.0f;  // (a Blockout inside a Blockout: the product, both are folded out of the walls below)
  }
  BlockoutScaleSet::Entry e;
  e.id = g.id, e.local0 = g.local(), e.world0 = g.world_matrix(), e.outer = outer;
  if (is_numbers_piece(g)) {
    e.role = BlockoutScaleSet::Numbers;
    e.before = *g.get<BlockoutPiece>();
  }
  else e.role = is_carrier(g) ? BlockoutScaleSet::Carrier : BlockoutScaleSet::Leaf;
  out.entries.push_back(std::move(e));
  out.ids.insert(g.id);
  for (const GameObject *c : g.children) collect_scale(*c, out, outer);
}

float sign_of(float v) { return v < 0.0f ? -1.0f : v > 0.0f ? 1.0f : 0.0f; }  // (a zero axis stays zero: the object stays flat there)

int depth_of(const GameObject &g) {
  int n = 0;
  for (const GameObject *a = g.parent; a; a = a->parent) n++;
  return n;
}

/* How much longer the world matrix `d` makes the vector `m` than `now` (the object's own axis as it stands) is; 1 for what cannot be told. */
float axis_growth(const Mat4 &d, const Vec3 &m, const Vec3 &now) {
  const float have = length(now), want = length(d.dir(m));
  if (!(have > 1e-12f) || !(length(m) > 1e-12f) || !finite_bits(want) || !finite_bits(have)) return 1.0f;  // (a flat piece stays flat: nothing to grow)
  const float r = want / have;
  return std::fabs(r - 1.0f) < 1e-6f ? 1.0f : r;
}

/* What a round piece's fold leaves on its own Scale so the world shape stays: the round axes were made one radius (the farther factor), the rest of each axis's
 * factor is the piece's Scale (it never shears: the piece's own axes). 1 1 1 for every other shape and for an even scale. */
Vec3 round_residual(const BlockoutPiece &p, const Vec3 &f) {
  Vec3 r(1.0f, 1.0f, 1.0f);
  auto share = [&](int a, int b, float k) {
    for (int i : {a, b})
      if (k > 0.0f && f[i] > 0.0f && std::fabs(f[i] / k - 1.0f) > 1e-5f) r[i] = f[i] / k;
  };
  if (p.opening != 0) return r;
  if (p.is_preset()) {  // (round 500: the round things of a preset - a column's shaft, a spiral - stay round)
    if (p.preset == kPresetColumn || p.preset == kPresetPilaster || p.preset == kPresetSpiral) share(0, 2, round_pick(f.x, f.z));
    return r;
  }
  switch ((blockout::Shape)p.shape) {
    case blockout::Shape::Cylinder: share(0, 2, round_pick(f.x, f.z)); break;
    case blockout::Shape::Arch: share(0, 1, round_pick(f.x, f.y)); break;
    case blockout::Shape::Sphere: {
      const float k = round_pick(round_pick(f.x, f.y), f.z);
      share(0, 1, k);
      share(2, 2, k);
      break;
    }
    default: break;
  }
  return r;
}

}  // namespace

void blockout_scale_collect(const std::vector<GameObject *> &roots, BlockoutScaleSet &out) {
  out = BlockoutScaleSet();
  std::vector<const GameObject *> order;  // (shallowest first, so a child selected before its parent still has its parent before it in the set)
  for (const GameObject *g : roots)
    if (g && takes_scale_in_numbers(*g)) order.push_back(g);
  std::stable_sort(order.begin(), order.end(), [](const GameObject *a, const GameObject *b) { return depth_of(*a) < depth_of(*b); });
  for (const GameObject *g : order) collect_scale(*g, out, Vec3(1.0f, 1.0f, 1.0f));
}

void blockout_scale_restore(Scene &scene, const BlockoutScaleSet &set) {
  for (const BlockoutScaleSet::Entry &e : set.entries) {
    GameObject *g = scene.find(e.id);
    if (!g) continue;
    g->set_local(e.local0);
    if (e.role != BlockoutScaleSet::Numbers) continue;
    if (BlockoutPiece *bp = g->get<BlockoutPiece>()) *bp = e.before;
  }
}

void blockout_scale_apply(Scene &scene, const BlockoutScaleSet &set, const Mat4 &d, bool fold) {
  for (const BlockoutScaleSet::Entry &e : set.entries) {  // (parents first: a child reads its parent as it is now)
    GameObject *g = scene.find(e.id);
    if (!g) continue;
    Vec3 at = d.point(e.world0.translation());
    if (!finite_bits(at.x) || !finite_bits(at.y) || !finite_bits(at.z)) at = e.world0.translation();  // (a typed 1e39 is inf: the object stays where it is)
    Transform t = e.local0;
    if (e.role == BlockoutScaleSet::Carrier && !fold) {
      g->set_local(t);
      g->set_world_position(at);
      continue;
    }
    /* The object's own scale set aside (a mirror stays): what is left of its axes is its parents' and its turn. */
    for (int a = 0; a < 3; a++) t.scale[a] = sign_of(e.local0.scale[a]);
    g->set_local(t);
    if (e.role == BlockoutScaleSet::Carrier) {
      g->set_world_position(at);
      if (fold)
        if (Blockout *b = g->get<Blockout>()) {  // (a texture tile keeps its size in the world; the Field Cell is left as it is: it only sets how fine the build is)
          const float u = std::cbrt(e.outer.x * e.outer.y * e.outer.z);
          if (finite_bits(u) && finite_bits(b->texture_scale) && std::fabs(u - 1.0f) > 1e-6f) b->texture_scale = std::min(1000.0f, std::max(0.001f, b->texture_scale * u));
        }
      continue;
    }
    const Mat4 &now = g->world_matrix();
    Vec3 f;
    for (int a = 0; a < 3; a++) f[a] = axis_growth(d, e.world0.column(a), now.column(a));
    if (e.role == BlockoutScaleSet::Numbers) {
      if (BlockoutPiece *bp = g->get<BlockoutPiece>()) {
        *bp = e.before;
        const bool changed = blockout_piece_scale(*bp, f);
        /* A fillet is metres in the Blockout's space, like a wall: the piece's own old scale is no growth of it; only the user's scale along the piece's world axes
         * and, in a fold, the Blockout's own folded scale count. */
        if (changed && bp->opening == 0 && finite_bits(e.before.blend) && e.before.blend > 0.0f) {
          float k = std::min(e.outer.x, std::min(e.outer.y, e.outer.z));
          if (!fold) {
            k = 1e30f;
            for (int a = 0; a < 3; a++) k = std::min(k, axis_growth(d, e.world0.column(a), e.world0.column(a)));
          }
          bp->blend = std::min(100.0f, e.before.blend * (finite_bits(k) && k > 0.0f ? k : 1.0f));
        }
        if (fold) {  // (a round piece with an uneven fold keeps the rest of the ratio on its own Scale: the world shape stays)
          const Vec3 rr = round_residual(*bp, f);
          for (int a = 0; a < 3; a++) t.scale[a] = sign_of(e.local0.scale[a]) * rr[a];
          if (t.scale.x != sign_of(e.local0.scale.x) || t.scale.y != sign_of(e.local0.scale.y) || t.scale.z != sign_of(e.local0.scale.z)) g->set_local(t);
        }
        if (fold && bp->room) {  // (a wall is in the Blockout's space: only the Blockout's own scale, folded out, would have stretched it)
          const float wall_k = std::sqrt(e.outer.x * e.outer.z);
          auto thick = [](float &v, float k) {
            if (k != 1.0f && finite_bits(v)) v = std::min(1000.0f, std::max(0.0f, v * k));
          };
          thick(bp->wall, wall_k), thick(bp->floor, e.outer.y), thick(bp->ceiling, e.outer.y);
        }
      }
    }
    else {
      for (int a = 0; a < 3; a++) t.scale[a] *= f[a];
      g->set_local(t);
    }
    g->set_world_position(at);
  }
}

bool blockout_scale_uneven(const Vec3 &s) {
  float lo = 1e30f, hi = 0.0f;
  for (int a = 0; a < 3; a++) {
    const float v = std::fabs(s[a]);
    lo = std::min(lo, v), hi = std::max(hi, v);
  }
  return finite_bits(hi) && hi > lo * 1.0001f;
}

void blockout_uneven_scales(const GameObject &blockout, std::vector<BlockoutUnevenScale> &out) {
  for (const GameObject *a = &blockout; a; a = a->parent)
    if (blockout_scale_uneven(a->local().scale)) out.push_back({a->id, a != &blockout});
  std::function<void(const GameObject &)> walk = [&](const GameObject &g) {
    for (const GameObject *c : g.children) {
      if (c->get<Blockout>()) continue;
      if (has_piece_below(*c) && !has_own_geometry(*c) && blockout_scale_uneven(c->local().scale)) out.push_back({c->id, false});
      walk(*c);
    }
  };
  walk(blockout);
}

}  // namespace bl
