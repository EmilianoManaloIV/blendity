// SPDX-License-Identifier: GPL-2.0-or-later
// A reference board (task 0299, ADR 0089): PureRef's endless canvas as a project asset - images (and, from task 0300,
// notes and colour swatches) placed, scaled, turned, flipped and cropped on a board saved in Assets/Boards/<name>.board
// as JSON. Board units are the images' own pixels at 100%.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct BoardItem {
  enum Kind { Image = 0, Note = 1, Swatch = 2 };
  int kind = Image;
  uint64_t id = 0;
  std::string path;   // an image: asset-relative (or absolute when outside the project)
  std::string text;   // a note's words
  uint32_t color = 0xFFFFFFFF;  // a swatch's colour, a note's paper
  Vec2 pos;           // the centre, in board units
  float w = 0.0f, h = 0.0f;  // the item's own size before its scale (an image: its pixels, kept so a missing file keeps its place)
  float scale = 1.0f, angle = 0.0f;  // (angle in degrees, clockwise on screen)
  bool flip_x = false, flip_y = false;
  float crop[4] = {0.0f, 0.0f, 1.0f, 1.0f};  // u0 v0 u1 v1 of the image shown
  bool gray = false, locked = false;
  float opacity = 1.0f;
  int float_pad = 0;  // (task 0301: floated over the Draw tab)

  /* The shown size (the crop's part of the image, scaled). */
  Vec2 size() const { return {w * (crop[2] - crop[0]) * scale, h * (crop[3] - crop[1]) * scale}; }
  /* Board point -> the item's own frame: (0,0) its top left, (1,1) its bottom right of the shown part; flips applied. */
  Vec2 to_local(Vec2 p) const;
  Vec2 from_local(Vec2 l) const;
  bool contains(Vec2 p) const;
  /* The four corners in board units (top left, top right, bottom right, bottom left). */
  void corners(Vec2 out[4]) const;
};

struct Board {
  std::string name = "Board";
  std::vector<BoardItem> items;  // bottom to top
  float zoom = 1.0f;              // the view: screen = window centre + pan + board * zoom
  Vec2 pan;
  uint64_t next_id = 1;
  BoardItem *find(uint64_t id);
  const BoardItem *find(uint64_t id) const;
  /* The topmost item under a board point, or 0. */
  uint64_t item_at(Vec2 p) const;
  /* The bounds of the items (all, or those in `ids`), false when there are none. */
  bool bounds(const std::vector<uint64_t> &ids, Vec2 &lo, Vec2 &hi) const;
};

/* An image as the board shows it (task 0299): its own pixels' size, and the picture at most 2048 on its long side with
 * its mips (0xAARRGGBB); an error when it couldn't be read. Made on a loader thread. */
struct BoardPicture {
  struct Level {
    int w = 0, h = 0;
    std::vector<uint32_t> argb;
  };
  int w = 0, h = 0;
  std::vector<Level> levels;
  std::string error;
};

std::string board_to_json(const Board &b);
bool board_from_json(const std::string &text, Board &b, std::string &err);

}  // namespace bl
