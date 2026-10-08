// SPDX-License-Identifier: GPL-2.0-or-later
// Operating-system abstraction: one window, input events and a software
// framebuffer blit. This plays the role of Blender's GHOST library
// (blender/intern/ghost: GHOST_SystemWin32.cc, GHOST_SystemX11.cc,
// GHOST_SystemCocoa.mm) but with no OpenGL/Vulkan: we present CPU pixels.
// Theory: Game Engine Architecture Vol. I, ch. 9 "Human Interface Devices".
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bl::platform {

enum Key : int {
  KEY_NONE = 0,
  KEY_SPACE = ' ',
  KEY_0 = '0', KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
  KEY_A = 'A', KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
  KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
  KEY_ESCAPE = 256, KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_DELETE, KEY_INSERT,
  KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
  KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
  KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_SUPER,
  KEY_MINUS, KEY_EQUALS, KEY_LBRACKET, KEY_RBRACKET, KEY_SEMICOLON, KEY_APOSTROPHE,
  KEY_COMMA, KEY_PERIOD, KEY_SLASH, KEY_BACKSLASH, KEY_GRAVE,
  KEY_COUNT
};

enum Mod : int { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_SUPER = 8 };

enum class EventType {
  Invalid, Quit, Resize, Repaint, MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Text, Drop, FocusLost, DpiChanged
};

struct Event {
  EventType type = EventType::Invalid;
  int x = 0, y = 0;        // pixels, top-left origin
  int button = 0;          // 0 left, 1 right, 2 middle
  float wheel_x = 0, wheel_y = 0;
  int key = KEY_NONE;
  int mods = 0;
  bool repeat = false;
  uint32_t codepoint = 0;  // Text
  std::vector<std::string> paths;  // Drop (UTF-8)
};

enum class Cursor { Arrow, IBeam, ResizeH, ResizeV, Move, Hand };

struct Window;

Window *create_window(const char *title, int width, int height);
void destroy_window(Window *w);

/* Collects pending events. If none are queued it blocks for up to timeout_ms
 * (0 = never block). This is how the idle editor uses ~0% CPU. */
void poll_events(Window *w, std::vector<Event> &out, int timeout_ms);

/* Copies a 0xAARRGGBB pixel buffer (stride = width) to the window. */
void present(Window *w, const uint32_t *pixels, int width, int height);

void get_framebuffer_size(Window *w, int &width, int &height);
float dpi_scale(Window *w);
void set_title(Window *w, const std::string &title);
void set_cursor(Window *w, Cursor c);
std::string get_clipboard(Window *w);
void set_clipboard(Window *w, const std::string &utf8);

/* Called while the OS runs a modal loop (e.g. live window resize on Win32/macOS)
 * so the app can keep repainting. */
void set_refresh_callback(Window *w, std::function<void()> cb);


/* Native file dialogs (Blender's file browser plays this role; Unity uses
 * the OS dialogs): the Win32 common dialog, NSSavePanel / NSOpenPanel on
 * macOS, zenity or kdialog on Linux. A filter is a name and extensions with
 * their dot ({"Wavefront OBJ", {".obj"}}). Both return false when the user
 * cancels or no dialog is available (file_dialogs_available()). The save
 * dialog appends the chosen filter's extension when the name has none, and
 * reports which filter was chosen in *filter_index (0-based, in and out). */
struct FileFilter {
  std::string name;
  std::vector<std::string> extensions;
};
bool file_dialogs_available();
bool save_file_dialog(Window *w, const std::string &title, const std::string &initial_path, const std::vector<FileFilter> &filters,
                      std::string &out_path, int *filter_index = nullptr);
bool open_file_dialog(Window *w, const std::string &title, const std::string &initial_dir, const std::vector<FileFilter> &filters,
                      std::string &out_path);
/* UTF-8 helper shared by backends. */
void append_utf8(std::string &s, uint32_t cp);

}  // namespace bl::platform
