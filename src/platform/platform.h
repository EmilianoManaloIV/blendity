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
#include <utility>
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
  Invalid, Quit, Resize, Repaint, MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Text, Drop, FocusLost, DpiChanged,
  Gesture  // two fingers on a touch screen or pad (task 0075): zoom, pan, rotate
};

/* What made a pointer event (task 0213, ADR 0062): one finger comes as the mouse's left button, tagged Touch. */
enum class Pointer { Mouse, Pen, Touch };

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
  /* Gesture: this event's change - the fingers' spread as a factor (2: twice as far apart), their middle's move in
   * pixels, their turn in degrees (counter-clockwise). */
  float zoom = 1.0f, pan_x = 0.0f, pan_y = 0.0f, rotate = 0.0f;
  float pressure = 1.0f;  // MouseMove / MouseDown from a pen: how hard it presses (0..1); 1 for a mouse or a finger
  /* From a pen (task 0188): its tilt from upright in degrees (x toward the right, y toward the user), and whether it
   * is the eraser end. */
  bool pen = false, eraser = false;
  float tilt_x = 0.0f, tilt_y = 0.0f;
  /* Task 0213: what made it (pen is pointer == Pen), a pen's side button held, and a MouseUp that takes its press back
   * (a finger joined by a second: nothing clicks, a box select is dropped). */
  Pointer pointer = Pointer::Mouse;
  bool barrel = false, cancel = false;
};

enum class Cursor { Arrow, IBeam, ResizeH, ResizeV, Move, Hand };

struct Window;

Window *create_window(const char *title, int width, int height);
/* Another window beside the first (task 0187): its client area at (x, y) on the screen, width x height pixels, not
 * maximised, as small as 240 x 160. Events for it are gathered by polling the first window and collected by polling
 * this one (with no wait). */
Window *create_secondary_window(const char *title, int x, int y, int width, int height);
void destroy_window(Window *w);

/* A display: its desktop rectangle in pixels, its scale (1 = 96 dpi) and name; the primary one first. */
struct Monitor {
  int x = 0, y = 0, width = 0, height = 0;
  float dpi = 1.0f;
  std::string name;
  bool primary = false;
};
/* Every display, at least one (X11 without RandR: the screen as one display). */
std::vector<Monitor> monitors();
/* The window's client area on the desktop, and moving / resizing it there. */
void get_window_rect(Window *w, int &x, int &y, int &width, int &height);
void set_window_rect(Window *w, int x, int y, int width, int height);
/* Borderless over its whole display (task 0188: a render shown on a second screen), and back. */
void set_fullscreen(Window *w, bool on);

/* Collects pending events. If none are queued it blocks for up to timeout_ms
 * (0 = never block). This is how the idle editor uses ~0% CPU. */
void poll_events(Window *w, std::vector<Event> &out, int timeout_ms);
/* The same with a wait in seconds, not rounded to milliseconds (task 0084: the frame pacer's waits). */
void poll_events_for(Window *w, std::vector<Event> &out, double seconds);

/* Copies a 0xAARRGGBB pixel buffer (stride = width) to the window. */
void present(Window *w, const uint32_t *pixels, int width, int height);
/* Copies only these runs of rows [first, second) of it (the rest of the window keeps what it showed), after a whole
 * present of the same size (task 0084). Where a system can't, the whole buffer goes, once. */
void present_rows(Window *w, const uint32_t *pixels, int width, int height, const std::vector<std::pair<int, int>> &rows);

void get_framebuffer_size(Window *w, int &width, int &height);
/* The system's own handles for a GPU swapchain (task 0193): the window (Win32: the HWND; X11: the Window id) and its
 * display (X11: the Display*; null elsewhere). Null where the card can't present yet (macOS: task 0194). */
void *native_handle(Window *w);
void *native_display(Window *w);
float dpi_scale(Window *w);
/* Task 0218: the laptop in its tablet posture - folded back, or its keyboard detached (Windows' slate mode). False
 * where the system doesn't say (Linux, macOS, a desktop). */
bool tablet_posture();
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
/* A folder (task 0271): the system's chooser; false on cancel or when there is none. */
bool folder_dialog(Window *w, const std::string &title, const std::string &initial_dir, std::string &out_path);
/* UTF-8 helper shared by backends. */
void append_utf8(std::string &s, uint32_t cp);

}  // namespace bl::platform
