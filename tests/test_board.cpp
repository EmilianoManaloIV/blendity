// SPDX-License-Identifier: GPL-2.0-or-later
// The reference board (tasks 0299-0302): PureRef's canvas as a window - images dropped, pasted and dragged in, the
// selection's handles, the board's keys and undo, its file.
#include "test_common.h"

#include "../src/image/image.h"
#include "../src/render/canvas.h"
#include "../src/scene/board.h"

#include <cmath>

using namespace bl;

namespace {

/* A small PNG in the test project's Assets: a w x h picture, its left half `a`, its right half `b` (0xAARRGGBB). */
std::string write_test_png(const std::string &dir, const std::string &name, int w, int h, uint32_t a, uint32_t b) {
  std::vector<uint32_t> px((size_t)w * h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) px[(size_t)y * w + x] = x < w / 2 ? a : b;
  fs::make_dirs(dir);
  const std::string path = fs::join(dir, name);
  fs::write_file(path, encode_png(px.data(), w, h, w, true));
  return path;
}

struct BoardRig {
  Editor ed;
  /* Each test its own board (they share the test project's Assets/Boards). */
  explicit BoardRig(const std::string &name = "") {
    ed.init_headless(1600, 900);
    ed.record_buttons_for_test(true);
    ed.command("layout Drawing");
    step();
    ed.command("window Board");
    for (int i = 0; i < 3; i++) step();
    const std::string refs = fs::join(ed.assets_dir_for_test(), "Textures/References");
    for (const DirEntry &e : fs::list(refs))  // (earlier runs' copies and pastes: a test's copy must really be made)
      if (!e.is_dir) fs::remove_file(fs::join(refs, e.name));
    const std::string n = name.empty() ? strprintf("Test Board %u", (unsigned)(std::rand() % 1000000)) : name;
    for (const char *ext : {".board"}) fs::remove_file(fs::join(fs::join(ed.assets_dir_for_test(), "Boards"), n + ext));
    const std::string papers = fs::join(ed.assets_dir_for_test(), "Papers");  // (earlier tests' papers: review 326)
    for (const DirEntry &e : fs::list(papers))
      if (!e.is_dir) fs::remove_file(fs::join(papers, e.name));
    ed.command("board new " + n);
  }
  void step(std::vector<platform::Event> events = {}) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  }
  static platform::Event ev(platform::EventType t, int x, int y, int key = 0, int button = 0, int mods = 0) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y, e.key = key, e.button = button, e.mods = mods;
    return e;
  }
  Vec2 mid() const {
    const Recti r = ed.board_rect_for_test();
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
  }
  void drag(Vec2 a, Vec2 b, int button = 0, int mods = 0) {
    using platform::EventType;
    step({ev(EventType::MouseMove, (int)a.x, (int)a.y, 0, 0, mods)});
    step({ev(EventType::MouseDown, (int)a.x, (int)a.y, 0, button, mods)});
    for (int i = 1; i <= 6; i++) {
      const Vec2 p = a + (b - a) * (i / 6.0f);
      step({ev(EventType::MouseMove, (int)std::lround(p.x), (int)std::lround(p.y), 0, 0, mods)});
    }
    step({ev(EventType::MouseUp, (int)b.x, (int)b.y, 0, button, mods)});
    step();
  }
  void key(int k, int mods = 0) {
    const Vec2 m = mid();
    step({ev(platform::EventType::MouseMove, (int)m.x, (int)m.y)});
    step({ev(platform::EventType::KeyDown, (int)m.x, (int)m.y, k, 0, mods)});
    step({ev(platform::EventType::KeyUp, (int)m.x, (int)m.y, k, 0, mods)});
  }
  std::string assets() const { return ed.assets_dir_for_test(); }
  /* A recorded button or menu item clicked by its key (task 0305). */
  bool click(const std::string &key) {
    Recti b;
    if (!ed.button_rect_for_test(key, b)) return false;
    const int x = b.x + b.w / 2, y = b.y + b.h / 2;
    step({ev(platform::EventType::MouseMove, x, y)});
    step({ev(platform::EventType::MouseDown, x, y)});
    step({ev(platform::EventType::MouseUp, x, y)});
    step();
    return true;
  }
  /* The pictures loading on their thread landed, then a frame. */
  void settle() {
    ed.board_wait_for_test();
    step();
  }
};

void tests_round322() {
  test("the Board window: a tab beside the Draw tab in the Drawing layout (window Board brings it forward), side by side with it in Drawing with References; an item's frame maps both ways", [] {
    BoardRig r;
    CHECK(r.ed.window_rect_for_test(WindowKind::Board).w > 600);
    r.ed.command("layout Drawing with References");
    for (int i = 0; i < 3; i++) r.step();
    const Recti w = r.ed.window_rect_for_test(WindowKind::Board), d = r.ed.window_rect_for_test(WindowKind::Draw);
    std::printf("    the Board %d,%d %dx%d; the Draw tab %d,%d %dx%d\n", w.x, w.y, w.w, w.h, d.x, d.y, d.w, d.h);
    CHECK(w.w > 200 && w.h > 200 && d.w > 200 && w.x >= d.right() - 2);
    BoardItem it;
    it.w = 200, it.h = 100, it.pos = {50, 20}, it.scale = 1.5f, it.angle = 30.0f, it.flip_x = true;
    const Vec2 p = it.from_local({0.25f, 0.75f}), back = it.to_local(p);
    CHECK(std::fabs(back.x - 0.25f) < 1e-4f && std::fabs(back.y - 0.75f) < 1e-4f && it.contains(p));
    Board b;
    b.items.push_back(it);
    b.items[0].id = 7, b.items[0].path = "Textures/a.png", b.items[0].crop[2] = 0.5f, b.items[0].gray = true;
    Board read;
    std::string err;
    CHECK(board_from_json(board_to_json(b), read, err) && read.items.size() == 1 && read.items[0].crop[2] == 0.5f && read.items[0].gray && read.items[0].flip_x);
    CHECK(!board_from_json("{\"items\": []}", read, err));  // (not a board: refused)
  });
  test("images dropped on the board land in a row at the drop point; a second later the board keeps itself in Assets/Boards and reopens as it was", [] {
    BoardRig r("Dropped Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "red.png", 64, 32, 0xFFFF0000, 0xFFFF0000);
    const std::string b = write_test_png(fs::join(fs::temp_dir(), "blendity_board_test"), "blue.png", 40, 40, 0xFF0000FF, 0xFF0000FF);  // (outside the project)
    const Vec2 m = r.mid();
    platform::Event drop = BoardRig::ev(platform::EventType::Drop, (int)m.x, (int)m.y);
    drop.paths = {a, b};
    r.step({BoardRig::ev(platform::EventType::MouseMove, (int)m.x, (int)m.y), drop});
    r.settle();
    const Board &bd = r.ed.board_for_test();
    std::printf("    dropped: %zu items", bd.items.size());
    CHECK(bd.items.size() == 2);
    if (bd.items.size() == 2) {
      std::printf(" at (%.0f, %.0f) and (%.0f, %.0f); the second kept as %s", bd.items[0].pos.x, bd.items[0].pos.y, bd.items[1].pos.x, bd.items[1].pos.y, bd.items[1].path.c_str());
      CHECK(bd.items[0].w == 64.0f && bd.items[1].pos.x > bd.items[0].pos.x + 32.0f);
      CHECK(bd.items[1].path.find("References") != std::string::npos);  // (from outside the project: copied in)
    }
    std::printf("\n");
    /* The pixel under the first image's middle is red. */
    if (!bd.items.empty()) {
      const Vec2 s = r.ed.board_to_screen_for_test(bd.items[0].pos);
      const Image &fb = r.ed.main_fb_for_test();
      CHECK((fb.pixels[(size_t)s.y * fb.width + (size_t)s.x] & 0xFFFFFF) == 0xFF0000u);
    }
    for (int i = 0; i < 70; i++) r.step();  // (1.4 s)
    const std::string file = r.ed.board_file_for_test();
    std::printf("    kept in %s\n", file.c_str());
    CHECK(!file.empty() && fs::exists(file));
    BoardRig r2("Another");
    r2.ed.command("board open Dropped Board");
    CHECK(r2.ed.board_for_test().items.size() == 2);
  });
  test("pasted image bytes (the clipboard's) are saved in Assets/Textures/References and placed at the view's middle; Ctrl+V over the board does it", [] {
    BoardRig r;
    std::vector<uint32_t> px(16 * 16, 0xFF00FF00);
    r.ed.board_paste_bytes_for_test(encode_png(px.data(), 16, 16, 16, true));
    r.key('V', platform::MOD_CTRL);
    r.settle();
    const Board &bd = r.ed.board_for_test();
    std::printf("    pasted: %zu items%s\n", bd.items.size(), bd.items.empty() ? "" : (", kept as " + bd.items[0].path).c_str());
    CHECK(bd.items.size() == 1 && bd.items[0].w == 16.0f && bd.items[0].path.find("References/Pasted ") != std::string::npos);  // (the next free number: earlier runs keep theirs)
  });
  test("the selection's handles by drags: an image moves; a corner scales it about the opposite corner; the turn handle turns it in 15 degree steps with Ctrl; a marquee picks what it meets, Shift adds; each drag one undo step (Ctrl+Z over the board)", [] {
    BoardRig r;
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "grey.png", 100, 100, 0xFF808080, 0xFF808080);
    r.ed.command("board add " + a + " 0 0");
    r.ed.command("board add " + a + " 300 0");
    r.settle();
    r.ed.command("board zoom 1");
    r.ed.command("board select 0");
    r.step();
    const Board &bd = r.ed.board_for_test();
    CHECK(bd.items.size() == 2);
    if (bd.items.size() != 2) return;
    const Vec2 c0 = r.ed.board_to_screen_for_test(bd.items[0].pos);
    r.drag(c0, c0 + Vec2{40, 20});
    std::printf("    moved to (%.1f, %.1f)\n", bd.items[0].pos.x, bd.items[0].pos.y);
    CHECK(std::fabs(bd.items[0].pos.x - 40.0f / bd.zoom) < 1.5f && std::fabs(bd.items[0].pos.y - 20.0f / bd.zoom) < 1.5f);
    /* The bottom-right corner dragged out: twice the size, the top-left corner kept. */
    Vec2 c[4];
    bd.items[0].corners(c);
    const Vec2 tl = c[0], br = r.ed.board_to_screen_for_test(c[2]);
    r.drag(br, br + (br - r.ed.board_to_screen_for_test(tl)));
    bd.items[0].corners(c);
    std::printf("    scaled %.3f; the top left (%.1f, %.1f), was (%.1f, %.1f)\n", bd.items[0].scale, c[0].x, c[0].y, tl.x, tl.y);
    CHECK(std::fabs(bd.items[0].scale - 2.0f) < 0.05f && length(c[0] - tl) < 2.0f);
    /* The turn handle, 22 px above the top's middle, swung about 40 degrees with Ctrl: 45. */
    Vec2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    for (const Vec2 &p : c) {
      const Vec2 s = r.ed.board_to_screen_for_test(p);
      lo = {std::min(lo.x, s.x), std::min(lo.y, s.y)}, hi = {std::max(hi.x, s.x), std::max(hi.y, s.y)};
    }
    const Vec2 centre = (lo + hi) * 0.5f, handle{centre.x, lo.y - 22.0f};
    const float rr = length(handle - centre), a40 = 40.0f * kDeg2Rad;
    const Vec2 to = centre + Vec2{std::sin(a40), -std::cos(a40)} * rr;
    r.drag(handle, to, 0, platform::MOD_CTRL);
    std::printf("    turned %.2f degrees\n", bd.items[0].angle);
    CHECK(std::fabs(bd.items[0].angle - 45.0f) < 0.01f);
    /* Ctrl+Z three times over the board: back where it began. */
    for (int i = 0; i < 3; i++) r.key('Z', platform::MOD_CTRL);
    CHECK(bd.items[0].angle == 0.0f && bd.items[0].scale == 1.0f && bd.items[0].pos.x == 0.0f);
    r.key('Y', platform::MOD_CTRL);
    CHECK(std::fabs(bd.items[0].pos.x - 40.0f / bd.zoom) < 1.5f);
    /* A marquee round the second image only; then Shift+click the first. */
    const Vec2 s1 = r.ed.board_to_screen_for_test(bd.items[1].pos);
    r.drag(s1 + Vec2{-90, -90}, s1 + Vec2{90, 90});
    std::printf("    the marquee picked %zu\n", r.ed.board_sel_for_test().size());
    CHECK(r.ed.board_sel_for_test().size() == 1 && r.ed.board_sel_for_test()[0] == bd.items[1].id);
    const Vec2 s0 = r.ed.board_to_screen_for_test(bd.items[0].pos);
    r.step({BoardRig::ev(platform::EventType::MouseMove, (int)s0.x, (int)s0.y)});
    r.step({BoardRig::ev(platform::EventType::MouseDown, (int)s0.x, (int)s0.y, 0, 0, platform::MOD_SHIFT)});
    r.step({BoardRig::ev(platform::EventType::MouseUp, (int)s0.x, (int)s0.y, 0, 0, platform::MOD_SHIFT)});
    CHECK(r.ed.board_sel_for_test().size() == 2);
  });
  test("the board's keys over it: Ctrl+D duplicates, Delete deletes, Ctrl+A selects all, Ctrl+] and Ctrl+[ restack, Home fits, F frames; a Project window image dragged onto it lands there", [] {
    BoardRig r;
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "k.png", 50, 50, 0xFF202020, 0xFF202020);
    r.ed.command("board add " + a + " 0 0");
    r.ed.command("board add " + a + " 100 0");
    r.settle();
    r.ed.command("board select 0");
    const Board &bd = r.ed.board_for_test();
    const uint64_t first = bd.items.empty() ? 0 : bd.items[0].id;
    r.key('D', platform::MOD_CTRL);
    CHECK(bd.items.size() == 3);
    r.key(platform::KEY_DELETE);
    CHECK(bd.items.size() == 2);
    r.key('A', platform::MOD_CTRL);
    CHECK(r.ed.board_sel_for_test().size() == 2);
    r.ed.command("board select 0");
    r.key(platform::KEY_RBRACKET, platform::MOD_CTRL);
    CHECK(bd.items.back().id == first);
    r.key(platform::KEY_LBRACKET, platform::MOD_CTRL);
    CHECK(bd.items.front().id == first);
    r.key(platform::KEY_EQUALS, platform::MOD_CTRL);  // (task 0303: a pen display's dial, Photoshop's zoom keys)
    const float z0 = bd.zoom;
    r.key(platform::KEY_MINUS, platform::MOD_CTRL);
    std::printf("    Ctrl+- after Ctrl+=: %.3f of %.3f\n", bd.zoom, z0);
    CHECK(std::fabs(bd.zoom * 1.25f - z0) < 1e-3f && r.ed.run_action_for_test("board.zoom_in") && r.ed.run_action_for_test("board.zoom_out"));
    r.ed.command("board zoom 30");
    r.key(platform::KEY_HOME);
    const float fitted = bd.zoom;
    r.key('F');
    std::printf("    Home: zoom %.3f; F (one image): %.3f\n", fitted, bd.zoom);
    CHECK(fitted < 30.0f && bd.zoom > fitted);
    for (const char *id : {"board.select_all", "board.duplicate", "board.delete", "board.paste", "board.fit", "board.frame", "board.front", "board.back"})
      CHECK(r.ed.run_action_for_test(id));
    CHECK(r.ed.run_action_for_test("board.delete"));  // (nothing picked: still the board's - Delete never reaches the scene from over the board)
    /* A Project window image pressed, dragged onto the board and let go. */
    const size_t before = bd.items.size();
    const Vec2 m = r.mid();
    r.step({BoardRig::ev(platform::EventType::MouseMove, 40, 860)});
    r.step({BoardRig::ev(platform::EventType::MouseDown, 40, 860)});
    r.ed.start_asset_drag_for_test(a);
    for (int i = 1; i <= 6; i++) r.step({BoardRig::ev(platform::EventType::MouseMove, 40 + (int)((m.x - 40) * i / 6), 860 + (int)((m.y - 860) * i / 6))});
    r.step({BoardRig::ev(platform::EventType::MouseUp, (int)m.x, (int)m.y)});
    r.settle();
    std::printf("    a Project drag: %zu -> %zu items\n", before, bd.items.size());
    CHECK(bd.items.size() == before + 1);
  });
  test("the board's console: new, open, save, list, add, select, move, scale, turn, duplicate, delete, front, back, fit, frame, zoom, undo, redo", [] {
    BoardRig r("Console Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "c.png", 20, 20, 0xFF00FFFF, 0xFF00FFFF);
    r.ed.command("board add " + a + " 10 20");
    r.settle();
    const Board &bd = r.ed.board_for_test();
    CHECK(bd.items.size() == 1 && bd.items[0].pos.x == 10.0f && bd.items[0].pos.y == 20.0f);
    r.ed.command("board move 5 -5");
    r.ed.command("board scale 2");
    r.ed.command("board turn 90");
    CHECK(bd.items[0].pos.x == 15.0f && bd.items[0].scale == 2.0f && std::fabs(bd.items[0].angle - 90.0f) < 1e-4f);
    r.ed.command("board duplicate");
    r.ed.command("board front");
    r.ed.command("board back");
    r.ed.command("board delete");
    CHECK(bd.items.size() == 1);
    r.ed.command("board undo");
    CHECK(r.ed.board_for_test().items.size() == 2);
    r.ed.command("board redo");
    CHECK(r.ed.board_for_test().items.size() == 1);
    r.ed.command("board select none");
    r.ed.command("board select all");
    r.ed.command("board fit");
    r.ed.command("board frame");
    r.ed.command("board zoom 0.5");
    r.ed.command("board turn nan");  // (refused: nothing changes)
    CHECK(std::isfinite(r.ed.board_for_test().items[0].angle));
    r.ed.command("board save");
    r.ed.command("board list");
    r.ed.command("board new Second");
    CHECK(r.ed.board_for_test().items.empty() && r.ed.board_for_test().name == "Second");
    r.ed.command("board open Console Board");
    CHECK(r.ed.board_for_test().items.size() == 1);
  });
}


/* Round 322's review: what a real board of real pictures needs - nothing lost on closing, the right picture for a file of
 * the same name, HDR shown, an unreadable file taken off, the last board opened again, one undo step a drop, no undo
 * mid-drag, BMP from the clipboard. */
void tests_round322_review() {
  test("the board keeps a change made in its last second when Blendity closes, and saves while hidden; the board used last opens again in the next session", [] {
    BoardRig r("Closing Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "close.png", 20, 20, 0xFF112233, 0xFF112233);
    r.ed.command("board add " + a + " 0 0");
    r.settle();
    r.ed.command("window Draw");  // (the board hidden behind the Draw tab)
    r.ed.command("board move 50 0");
    CHECK(r.ed.board_dirty_for_test());
    r.ed.request_close_for_test();
    const std::string file = r.ed.board_file_for_test();
    std::string text;
    CHECK(!r.ed.board_dirty_for_test() && fs::read_file(file, text) && text.find("\"pos\":[50") != std::string::npos);
    Editor next;
    next.init_headless(1200, 800);
    next.brush_pref_for_test("board_last", "Closing Board");
    next.command("board list");
    std::printf("    the next session's board: %s with %zu items\n", next.board_for_test().name.c_str(), next.board_for_test().items.size());
    CHECK(next.board_for_test().name == "Closing Board" && next.board_for_test().items.size() == 1);
  });
  test("two different files of the same name from outside the project are kept as image.png and image 2.png (the second never shows the first's picture); the same file again is used again", [] {
    BoardRig r("Names Board");
    const std::string outside = fs::join(fs::temp_dir(), "blendity_board_names");
    const std::string cat = write_test_png(fs::join(outside, "a"), "image.png", 8, 8, 0xFFFF8800, 0xFFFF8800);
    const std::string car = write_test_png(fs::join(outside, "b"), "image.png", 8, 8, 0xFF0088FF, 0xFF0088FF);
    r.ed.command("board add " + cat + " 0 0");
    r.ed.command("board add " + car + " 20 0");
    r.ed.command("board add " + cat + " 40 0");
    r.settle();
    const Board &bd = r.ed.board_for_test();
    CHECK(bd.items.size() == 3);
    if (bd.items.size() != 3) return;
    std::printf("    kept as %s, %s, %s\n", bd.items[0].path.c_str(), bd.items[1].path.c_str(), bd.items[2].path.c_str());
    CHECK(bd.items[0].path != bd.items[1].path && bd.items[1].path.find("image 2.png") != std::string::npos && bd.items[2].path == bd.items[0].path);
  });
  test("an HDR image is shown (tone-mapped), not Missing; an unreadable file is taken off the board and its copy deleted", [] {
    BoardRig r("Pictures Board");
    std::vector<float> rgb(16 * 16 * 3, 0.0f);
    for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 4.0f, rgb[i + 1] = 0.5f;  // (red past 1, green mid)
    const std::string hdr = fs::join(fs::join(r.assets(), "Textures"), "sky.hdr");
    fs::make_dirs(fs::parent(hdr));
    CHECK(write_hdr(hdr, rgb.data(), 16, 16));
    r.ed.command("board add " + hdr + " 0 0");
    r.settle();
    r.ed.command("board zoom 8");
    r.step();
    const Board &bd = r.ed.board_for_test();
    CHECK(bd.items.size() == 1 && bd.items[0].w == 16.0f);
    if (!bd.items.empty()) {
      const Vec2 s = r.ed.board_to_screen_for_test(bd.items[0].pos);
      const Image &fb = r.ed.main_fb_for_test();
      const uint32_t px = fb.pixels[(size_t)s.y * fb.width + (size_t)s.x];
      std::printf("    the HDR's middle on screen %08X\n", (unsigned)px);
      CHECK(((px >> 16) & 255) == 255 && ((px >> 8) & 255) > 150 && ((px >> 8) & 255) < 220);  // (red clamped, 0.5 linear about 188 in sRGB)
    }
    const std::string junk_dir = fs::join(fs::temp_dir(), "blendity_board_junk");
    fs::make_dirs(junk_dir);
    const std::string junk = fs::join(junk_dir, "broken.png");
    fs::write_file(junk, "not a picture at all");
    r.ed.command("board add " + junk + " 50 0");
    r.settle();
    std::printf("    after an unreadable file: %zu items; its copy there %d\n", bd.items.size(), (int)fs::exists(fs::join(r.assets(), "Textures/References/broken.png")));
    CHECK(bd.items.size() == 1 && !fs::exists(fs::join(r.assets(), "Textures/References/broken.png")));
  });
  test("a drop of two images is one undo step; Ctrl+Z in the middle of a drag waits for it (the drag is one step after); the history keeps 100 steps; an item of no size catches no click", [] {
    BoardRig r("Undo Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "u1.png", 30, 30, 0xFF445566, 0xFF445566);
    const std::string b = write_test_png(fs::join(r.assets(), "Textures"), "u2.png", 30, 30, 0xFF665544, 0xFF665544);
    const Vec2 m = r.mid();
    platform::Event drop = BoardRig::ev(platform::EventType::Drop, (int)m.x, (int)m.y);
    drop.paths = {a, b};
    r.step({BoardRig::ev(platform::EventType::MouseMove, (int)m.x, (int)m.y), drop});
    r.settle();
    const Board &bd = r.ed.board_for_test();
    CHECK(bd.items.size() == 2 && r.ed.board_undo_depth_for_test() == 1);
    r.ed.command("board zoom 2");
    r.ed.command("board select 0");
    r.step();
    using platform::EventType;
    const Vec2 s = r.ed.board_to_screen_for_test(bd.items[0].pos);
    r.step({BoardRig::ev(EventType::MouseMove, (int)s.x, (int)s.y)});
    r.step({BoardRig::ev(EventType::MouseDown, (int)s.x, (int)s.y)});
    for (int i = 1; i <= 3; i++) r.step({BoardRig::ev(EventType::MouseMove, (int)s.x + i * 10, (int)s.y)});
    r.step({BoardRig::ev(EventType::KeyDown, (int)s.x + 30, (int)s.y, 'Z', 0, platform::MOD_CTRL)});
    r.step({BoardRig::ev(EventType::KeyUp, (int)s.x + 30, (int)s.y, 'Z', 0, platform::MOD_CTRL)});
    r.step({BoardRig::ev(EventType::MouseUp, (int)s.x + 30, (int)s.y)});
    r.step();
    std::printf("    Ctrl+Z mid-drag: %zu items, %d steps, moved to %.1f\n", bd.items.size(), r.ed.board_undo_depth_for_test(), bd.items.empty() ? 0.0f : bd.items[0].pos.x);
    CHECK(bd.items.size() == 2 && r.ed.board_undo_depth_for_test() == 2);
    r.ed.command("board undo");
    r.ed.command("board undo");
    CHECK(r.ed.board_for_test().items.empty());
    r.ed.command("board redo");
    r.ed.command("board select all");
    for (int i = 0; i < 120; i++) r.ed.command("board move 1 0");
    CHECK(r.ed.board_undo_depth_for_test() == 100);
    BoardItem none;
    none.pos = {0, 0};
    CHECK(!none.contains({0, 0}) && !none.contains({500, 500}));
  });
  test("BMP bytes from the clipboard (a screenshot's) are kept as they came (a .bmp, read on the loader thread); a turned image's empty corner doesn't catch a click", [] {
    BoardRig r("Bitmap Board");
    /* A 4 x 2 32-bit BMP, bottom-up, blue. */
    std::string bmp = "BM";
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) bmp += (char)((v >> (8 * k)) & 255); };
    auto u16 = [&](uint16_t v) { bmp += (char)(v & 255), bmp += (char)(v >> 8); };
    u32(54 + 4 * 2 * 4), u32(0), u32(54);
    u32(40), u32(4), u32(2), u16(1), u16(32), u32(0), u32(4 * 2 * 4), u32(2835), u32(2835), u32(0), u32(0);
    for (int i = 0; i < 8; i++) bmp += std::string("\xFF\x00\x00\xFF", 4);  // (B G R A)
    r.ed.board_paste_bytes_for_test(bmp);
    r.ed.command("board paste");
    r.settle();
    const Board &bd = r.ed.board_for_test();
    std::printf("    a BMP pasted: %zu items%s\n", bd.items.size(), bd.items.empty() ? "" : (", " + bd.items[0].path).c_str());
    CHECK(bd.items.size() == 1 && bd.items[0].w == 4.0f && bd.items[0].path.find(".bmp") != std::string::npos);
    /* A 100 x 100 image turned 45 degrees: the corner of its bounds is empty board. */
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "turned.png", 100, 100, 0xFF808080, 0xFF808080);
    r.ed.command("board new Turned Board");
    r.ed.command("board add " + a + " 0 0");
    r.settle();
    r.ed.command("board select 0");
    r.ed.command("board turn 45");
    r.ed.command("board zoom 1");
    r.ed.command("board select none");
    r.step();
    const Vec2 corner = r.ed.board_to_screen_for_test({-62.0f, -62.0f});  // (inside the bounds, outside the turned square)
    r.step({BoardRig::ev(platform::EventType::MouseMove, (int)corner.x, (int)corner.y)});
    r.step({BoardRig::ev(platform::EventType::MouseDown, (int)corner.x, (int)corner.y)});
    r.step({BoardRig::ev(platform::EventType::MouseUp, (int)corner.x, (int)corner.y)});
    CHECK(r.ed.board_sel_for_test().empty());
  });
}


/* The second review of round 322: a picture landing while a drag or an undo holds the items, a pasted file that isn't a
 * picture, the cache trimmed. */
void tests_round322_review2() {
  test("a picture that lands while a press holds the items, or between two undo steps, still gives its image its size (never Loading for the session)", [] {
    BoardRig r("Landing Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "l1.png", 40, 20, 0xFF203040, 0xFF203040);
    const std::string b = write_test_png(fs::join(r.assets(), "Textures"), "l2.png", 30, 30, 0xFF403020, 0xFF403020);
    r.ed.command("board add " + a + " 0 0");
    r.settle();
    r.ed.command("board zoom 2");
    r.ed.command("board select 0");
    r.step();
    const Board &bd = r.ed.board_for_test();
    /* A press on the first image; the second added and landed meanwhile; the release without a move. */
    using platform::EventType;
    const Vec2 s = r.ed.board_to_screen_for_test(bd.items[0].pos);
    r.step({BoardRig::ev(EventType::MouseMove, (int)s.x, (int)s.y)});
    r.step({BoardRig::ev(EventType::MouseDown, (int)s.x, (int)s.y)});
    r.ed.command("board add " + b + " 100 0");  // (held off: a drag is under way)
    r.step({BoardRig::ev(EventType::MouseUp, (int)s.x, (int)s.y)});
    r.ed.command("board add " + b + " 100 0");
    r.step({BoardRig::ev(EventType::MouseDown, (int)s.x, (int)s.y)});  // (the next press: the picture lands under it)
    r.settle();
    r.step({BoardRig::ev(EventType::MouseUp, (int)s.x, (int)s.y)});
    r.step();
    std::printf("    after a press over the landing: %zu items, the second %.0f x %.0f\n", bd.items.size(), bd.items.size() > 1 ? bd.items[1].w : 0.0f, bd.items.size() > 1 ? bd.items[1].h : 0.0f);
    CHECK(bd.items.size() == 2 && bd.items[1].w == 30.0f);
    /* An undo step taken while a picture was loading, undone after it landed. */
    const std::string c = write_test_png(fs::join(r.assets(), "Textures"), "l3.png", 12, 24, 0xFF102030, 0xFF102030);
    r.ed.command("board add " + c + " 200 0");
    r.ed.command("board move 1 0");  // (its undo step holds the image without its size)
    r.settle();
    r.ed.command("board undo");
    r.step();
    const Board &after = r.ed.board_for_test();
    std::printf("    after an undo over a step taken while loading: the third %.0f x %.0f\n", after.items.size() > 2 ? after.items[2].w : 0.0f, after.items.size() > 2 ? after.items[2].h : 0.0f);
    CHECK(after.items.size() == 3 && after.items[2].w == 12.0f && after.items[2].h == 24.0f);
  });
  test("data that isn't a picture - a ZIP's first bytes - is refused by the decoders (it once read as a 23,000 x 65,000 TGA); a new board lets the old board's pictures go", [] {
    std::string zip = "PK\x03\x04\x14\x00\x00\x00\x08\x00\x5C\x5B\xFF\xFF\xFF\xFF";
    zip += std::string(64, '\x01');
    Bitmap bmp;
    std::string err;
    CHECK(!decode_image(zip, bmp, err));
    std::printf("    a ZIP's bytes: refused (%s)\n", err.c_str());
    BoardRig r("Cache Board");
    const std::string a = write_test_png(fs::join(r.assets(), "Textures"), "c1.png", 10, 10, 0xFF101010, 0xFF101010);
    r.ed.command("board add " + a + " 0 0");
    r.settle();
    CHECK(r.ed.board_pictures_for_test() == 1);
    r.ed.command("board new Empty Board");
    CHECK(r.ed.board_pictures_for_test() == 0);
  });
}

/* The sum of a picture's pixels' colour (a change in it: a drawing changed). */
uint64_t picture_sum(const std::shared_ptr<BoardPicture> &p) {
  uint64_t s = 0;
  if (!p || p->levels.empty()) return 0;
  for (uint32_t v : p->levels[0].argb) s += ((v >> 16) & 255) + ((v >> 8) & 255) + (v & 255);
  return s;
}

const BoardItem *first_paper(const Board &b) {
  for (const BoardItem &it : b.items)
    if (it.kind == BoardItem::Paper) return &it;
  return nullptr;
}

/* Round 325 (task 0305): papers on the board - one place for the references and the sketches. */
void tests_round325() {
  test("papers on the board: Boards > New Paper Here puts a paper on it; double-click opens it on the Draw tab with the source Paper; a stroke drawn there shows on the board's picture of it", [] {
    BoardRig r;
    using platform::EventType;
    CHECK(r.click("board:Boards") && r.click("menu:New Paper Here"));
    const BoardItem *it = first_paper(r.ed.board_for_test());
    CHECK(it != nullptr);
    if (!it) return;
    const std::string rel = it->path;
    std::printf("    the paper item %s, %.0f x %.0f\n", rel.c_str(), it->w, it->h);
    CHECK(std::fabs(it->w / it->h - 1.41421356f) < 0.01f);
    auto pic = r.ed.paper_thumb_for_test(rel);
    CHECK(pic && !pic->levels.empty());
    const uint64_t blank = picture_sum(pic);
    /* Double-click it: the Draw tab, on that paper. */
    r.ed.command("board fit");
    r.step();
    const Vec2 at = r.ed.board_to_screen_for_test(r.ed.board_for_test().items.back().pos);
    for (int k = 0; k < 2; k++) {
      r.step({BoardRig::ev(EventType::MouseMove, (int)at.x, (int)at.y)});
      r.step({BoardRig::ev(EventType::MouseDown, (int)at.x, (int)at.y)});
      r.step({BoardRig::ev(EventType::MouseUp, (int)at.x, (int)at.y)});
    }
    for (int i = 0; i < 3; i++) r.step();
    std::printf("    after the double-click: source %d, paper %s\n", r.ed.draw_source_for_test(), r.ed.paper_name_for_test().c_str());
    CHECK(r.ed.draw_source_for_test() == 2 && r.ed.paper_rel_for_test(r.ed.paper_name_for_test()) == rel);
    /* A stroke on the pad. */
    r.ed.command("sketch correct none");
    const Recti pad = r.ed.pad_rect_for_test();
    const Vec2 c{pad.x + pad.w * 0.5f, pad.y + pad.h * 0.5f};
    r.step({BoardRig::ev(EventType::MouseMove, (int)c.x - 80, (int)c.y)});
    r.step({BoardRig::ev(EventType::MouseDown, (int)c.x - 80, (int)c.y)});
    for (int i = 1; i <= 12; i++) r.step({BoardRig::ev(EventType::MouseMove, (int)c.x - 80 + i * 14, (int)c.y + i * 3)});
    r.step({BoardRig::ev(EventType::MouseUp, (int)c.x + 88, (int)c.y + 36)});
    CHECK(r.ed.paper_for_test().strokes.size() == 1);
    r.ed.set_clock_for_test(r.ed.clock_for_test() + 0.5);  // (past the thumbnail's quarter second)
    r.step();
    pic = r.ed.paper_thumb_for_test(rel);
    const uint64_t drawn = picture_sum(pic);
    std::printf("    the board's picture of it: %llu -> %llu\n", (unsigned long long)blank, (unsigned long long)drawn);
    CHECK(drawn < blank);  // (a dark stroke on white paper)
    /* Double-clicking the open paper again, and New Paper Here, keep its history (review 326). */
    const int depth = r.ed.paper_undo_depth_for_test();
    r.ed.command("window Board");
    for (int i = 0; i < 3; i++) r.step();
    r.ed.command("board select all");
    r.ed.command("board openpaper");
    r.ed.command("window Board");
    r.ed.command("board newpaper");
    std::printf("    the paper's undo steps: %d, after opening it again and New Paper Here %d (paper %s)\n", depth, r.ed.paper_undo_depth_for_test(), r.ed.paper_name_for_test().c_str());
    CHECK(depth >= 1 && r.ed.paper_undo_depth_for_test() == depth && r.ed.paper_rel_for_test(r.ed.paper_name_for_test()) == rel);
    r.ed.command("paper delete");
    r.ed.command("draw source scene");
    r.ed.command("sketch off");
  });
  test("papers on the board: the same paper on two boards is one drawing; renaming it keeps both boards linked; Duplicate Board and Delete Board; a board with a paper saves and opens again", [] {
    BoardRig r;
    r.ed.command("paper new Round325 Shared");
    const std::string name = r.ed.paper_name_for_test();
    const std::string one = strprintf("Round325 One %u", (unsigned)(std::rand() % 100000)), two = strprintf("Round325 Two %u", (unsigned)(std::rand() % 100000));
    r.ed.command("board rename " + one);
    r.ed.command("board addpaper " + name);
    r.ed.command("board save");
    r.ed.command("board new " + two);
    r.ed.command("board addpaper " + name);
    r.ed.command("board save");
    const BoardItem *second = first_paper(r.ed.board_for_test());
    CHECK(second && second->path == r.ed.paper_rel_for_test(name));
    CHECK(r.ed.paper_thumb_for_test(r.ed.paper_rel_for_test(name)) != nullptr);
    /* Rename: this board in memory, the other in its file. */
    r.ed.command("paper rename Round325 Renamed");
    const std::string rel = r.ed.paper_rel_for_test("Round325 Renamed");
    CHECK(first_paper(r.ed.board_for_test()) && first_paper(r.ed.board_for_test())->path == rel);
    r.ed.command("board open " + one);
    const BoardItem *first = first_paper(r.ed.board_for_test());
    std::printf("    board One names %s\n", first ? first->path.c_str() : "nothing");
    CHECK(first && first->path == rel && first->kind == BoardItem::Paper);
    /* Duplicate and delete the board. */
    r.ed.command("board copy");
    CHECK(r.ed.board_for_test().name == one + " copy" && first_paper(r.ed.board_for_test()));
    r.ed.command("board trash");
    CHECK(r.ed.board_for_test().name != one + " copy");
    for (const std::string &n : std::vector<std::string>{one, two}) {
      r.ed.command("board open " + n);
      r.ed.command("board trash");
    }
    r.ed.command("paper open Round325 Renamed");
    r.ed.command("paper delete");
  });
}

/* Round 326 (task 0306): a board exported as a PNG, a JPEG or a PDF. */
void tests_round326() {
  test("board export: two images as a PNG of the asked size with each image's colour in its place; the selection only; a transparent PNG; a JPEG that decodes; a one-page PDF whose xref points at its objects and whose picture decodes; Export... in the Boards menu (time printed)", [] {
    BoardRig r;
    const std::string dir = fs::join(r.assets(), "Textures/Round326");
    const std::string red = write_test_png(dir, "red.png", 64, 32, 0xFFFF0000, 0xFFFF0000);
    const std::string blue = write_test_png(dir, "blue.png", 64, 32, 0xFF0000FF, 0xFF0000FF);
    r.ed.command("board add " + red + " 0 0");
    r.ed.command("board add " + blue + " 200 0");
    r.settle();
    const std::string out = fs::join(fs::temp_dir(), "blendity_round326");
    fs::make_dirs(out);
    /* PNG, the whole board, 1000 px wide, no margin. */
    const std::string png = fs::join(out, "board.png");
    fs::remove_file(png);
    const double t0 = now_seconds();
    r.ed.command("board export " + png + " size 1000 bg white");
    r.ed.board_export_wait_for_test();
    const double t1 = now_seconds();
    Bitmap b;
    std::string err;
    CHECK(load_image(png, b, err));
    std::printf("    the PNG %d x %d in %.1f ms\n", b.width, b.height, (t1 - t0) * 1000.0);
    CHECK(b.width == 1000 && b.height > 100 && b.height < 300);
    auto px = [&](const Bitmap &bm, float fx, float fy) {
      const int x = (int)(fx * bm.width), y = (int)(fy * bm.height);
      const uint8_t *p = &bm.rgba8[((size_t)y * bm.width + x) * 4];
      return std::array<int, 4>{p[0], p[1], p[2], p[3]};
    };
    /* The red image's middle is on the left, the blue one's on the right. */
    const auto l = px(b, 0.12f, 0.5f), rr = px(b, 0.88f, 0.5f);
    std::printf("    left %d %d %d, right %d %d %d\n", l[0], l[1], l[2], rr[0], rr[1], rr[2]);
    CHECK(l[0] > 200 && l[2] < 60 && rr[2] > 200 && rr[0] < 60);
    /* The selection only (the blue one), transparent around it. */
    r.ed.command("board select none");
    r.ed.command("board select 1");
    const std::string sel = fs::join(out, "sel.png");
    r.ed.command("board export " + sel + " size 256 selection bg none");
    r.ed.board_export_wait_for_test();
    Bitmap s;
    CHECK(load_image(sel, s, err) && s.width == 256);
    const auto mid = px(s, 0.5f, 0.5f), corner = px(s, 0.01f, 0.02f);
    std::printf("    the selection: middle %d %d %d %d, corner alpha %d\n", mid[0], mid[1], mid[2], mid[3], corner[3]);
    CHECK(mid[2] > 200 && mid[0] < 60 && corner[3] == 0);
    /* JPEG. */
    const std::string jpg = fs::join(out, "board.jpg");
    r.ed.command("board export " + jpg + " size 512");
    r.ed.board_export_wait_for_test();
    Bitmap j;
    CHECK(load_image(jpg, j, err) && j.width == 512);
    const auto jl = px(j, 0.12f, 0.5f);
    CHECK(jl[0] > 180 && jl[2] < 80);
    /* PDF: the header, an xref whose offsets start objects, and the picture's JPEG decoding. */
    const std::string pdf = fs::join(out, "board.pdf");
    r.ed.command("board export " + pdf + " size 512");
    r.ed.board_export_wait_for_test();
    std::string text;
    CHECK(fs::read_file(pdf, text) && text.compare(0, 8, "%PDF-1.4") == 0);
    const size_t sx = text.rfind("startxref\n");
    const size_t xref = sx == std::string::npos ? 0 : (size_t)std::atoll(text.c_str() + sx + 10);
    CHECK(xref > 0 && text.compare(xref, 4, "xref") == 0);
    int objects_ok = 0;
    for (int k = 1; k <= 5; k++) {
      const size_t e = xref + 5 + 4 + 20 * (size_t)k;  // ("xref\n" "0 6\n" then 20-byte entries, the 0th the free one)
      const size_t at = e + 1 < text.size() ? (size_t)std::atoll(text.c_str() + e) : 0;
      objects_ok += at > 0 && text.compare(at, std::to_string(k).size() + 6, std::to_string(k) + " 0 obj") == 0;
    }
    const size_t js = text.find("\xFF\xD8"), je = text.find("\xFF\xD9", js);
    Bitmap pj;
    const bool jpeg_ok = js != std::string::npos && je != std::string::npos && decode_image(text.substr(js, je + 2 - js), pj, err) && pj.width == 512;
    std::printf("    the PDF: %zu bytes, %d of 5 objects where the xref says, its picture %s\n", text.size(), objects_ok, jpeg_ok ? "decodes" : "DOESN'T decode");
    CHECK(objects_ok == 5 && jpeg_ok);
    /* Export... is in the Boards menu and opens its settings. */
    /* Export... in the Boards menu: a panel whose settings stay open while chosen (review 326). */
    CHECK(r.click("board:Boards") && r.click("menu:Export..."));
    r.step();
    CHECK(r.click("export:Format") && r.click("menu:PDF"));
    Recti still;
    CHECK(r.ed.button_rect_for_test("export:Size", still) && r.click("export:Size") && r.click("menu:2048 px"));
    r.step();
    CHECK(r.ed.button_rect_for_test("export:Export", still));
    for (const char *f : {"board.png", "sel.png", "board.jpg", "board.pdf"}) fs::remove_file(fs::join(out, f));
    for (const char *f : {"red.png", "blue.png"}) fs::remove_file(fs::join(dir, f));
  });
  test("board export (review 326): an image larger than the board's 2048 px copy is read again at full size - 1 px stripes stay red and blue, not the copy's purple - and the editor isn't stopped while it is written", [] {
    BoardRig r;
    const std::string dir = fs::join(r.assets(), "Textures/Round326");
    fs::make_dirs(dir);
    const int w = 3000, h = 60;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) px[(size_t)y * w + x] = (x / 2) % 2 ? 0xFF0000FFu : 0xFFFF0000u;  // (2 px stripes)
    const std::string stripes = fs::join(dir, "stripes.png");
    fs::write_file(stripes, encode_png(px.data(), w, h, w, false));
    r.ed.command("board add " + stripes + " 0 0");
    r.settle();
    const std::string out = fs::join(fs::join(fs::temp_dir(), "blendity_round326"), "stripes.png");
    r.ed.command("board export " + out + " size 3240");  // (the image ~3000 px across: past its 2048 px copy)
    const bool started = r.ed.board_for_test().items.size() == 1;
    r.step();  // (a frame while it loads and writes: the editor goes on)
    r.ed.board_export_wait_for_test();
    Bitmap b;
    std::string err;
    CHECK(started && load_image(out, b, err));
    int pure = 0, n = 0;
    const int y = b.height / 2;
    for (int x = b.width / 4; x < b.width * 3 / 4; x++, n++) {
      const uint8_t *p = &b.rgba8[((size_t)y * b.width + x) * 4];
      pure += (p[0] > 200 && p[2] < 60) || (p[2] > 200 && p[0] < 60);
    }
    std::printf("    %d x %d: %d of %d pixels across the middle pure red or blue\n", b.width, b.height, pure, n);
    CHECK(b.width == 3240 && pure * 2 > n);
    fs::remove_file(out);
    fs::remove_file(stripes);
  });
}

}  // namespace

void run_board_tests() {
  tests_round322();
  tests_round322_review();
  tests_round322_review2();
  tests_round325();
  tests_round326();
}
