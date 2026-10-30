// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 backend (compare blender/intern/ghost/intern/GHOST_SystemWin32.cc).
// Links only against system DLLs: user32, gdi32, shell32, comdlg32.
#ifdef _WIN32

#include "platform.h"
#include "../core/touch_gestures.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#ifdef _MSC_VER
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "gdi32.lib")
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "comdlg32.lib")
#  pragma comment(lib, "winmm.lib")
#endif

namespace bl::platform {

struct Window {
  HWND hwnd = nullptr;
  std::vector<Event> queue;
  std::function<void()> refresh;
  HCURSOR cursor = nullptr;
  float dpi = 1.0f;
  uint32_t high_surrogate = 0;
  int buttons_down = 0;
  bool in_refresh = false;
  /* Touch and pen (task 0075; task 0213: the fingers through WM_POINTER into the recognizer), and the pen's last
   * pressure. */
  TouchRecognizer touch;
  float pen_pressure = 1.0f;
  bool pen_barrel = false;
  float pen_tilt_x = 0.0f, pen_tilt_y = 0.0f;
  bool pen_eraser = false, pen_near = false;  // (near: a pen in range - a touch then is a palm, not a finger: task 0188)
  UINT32 pen_id = 0;  // (the pen's pointer: only its leaving clears pen_near)
  bool fullscreen = false;
  LONG_PTR saved_style = 0;
  WINDOWPLACEMENT saved_place{sizeof(WINDOWPLACEMENT)};
  int min_w = 640, min_h = 400;  // (a secondary window may be smaller: task 0187)
  bool placing = false;  // (moved by the program: a display's DPI change keeps the size asked for)
};

static std::string narrow(const wchar_t *w, int len = -1) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
  if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
  return s;
}

static std::wstring widen(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}

void append_utf8(std::string &s, uint32_t cp) {
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
}

static int current_mods() {
  int m = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) m |= MOD_SHIFT;
  if (GetKeyState(VK_CONTROL) & 0x8000) m |= MOD_CTRL;
  if (GetKeyState(VK_MENU) & 0x8000) m |= MOD_ALT;
  if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= MOD_SUPER;
  return m;
}

static int map_vk(WPARAM vk) {
  if (vk >= 'A' && vk <= 'Z') return (int)vk;
  if (vk >= '0' && vk <= '9') return (int)vk;
  if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + (int)(vk - VK_F1);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return KEY_0 + (int)(vk - VK_NUMPAD0);
  switch (vk) {
    case VK_SPACE: return KEY_SPACE;
    case VK_ESCAPE: return KEY_ESCAPE;
    case VK_RETURN: return KEY_ENTER;
    case VK_TAB: return KEY_TAB;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_DELETE: return KEY_DELETE;
    case VK_INSERT: return KEY_INSERT;
    case VK_LEFT: return KEY_LEFT;
    case VK_RIGHT: return KEY_RIGHT;
    case VK_UP: return KEY_UP;
    case VK_DOWN: return KEY_DOWN;
    case VK_HOME: return KEY_HOME;
    case VK_END: return KEY_END;
    case VK_PRIOR: return KEY_PAGE_UP;
    case VK_NEXT: return KEY_PAGE_DOWN;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return KEY_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return KEY_CTRL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return KEY_ALT;
    case VK_LWIN: case VK_RWIN: return KEY_SUPER;
    case VK_OEM_MINUS: case VK_SUBTRACT: return KEY_MINUS;
    case VK_OEM_PLUS: case VK_ADD: return KEY_EQUALS;
    case VK_OEM_4: return KEY_LBRACKET;
    case VK_OEM_6: return KEY_RBRACKET;
    case VK_OEM_1: return KEY_SEMICOLON;
    case VK_OEM_7: return KEY_APOSTROPHE;
    case VK_OEM_COMMA: return KEY_COMMA;
    case VK_OEM_PERIOD: case VK_DECIMAL: return KEY_PERIOD;
    case VK_OEM_2: case VK_DIVIDE: return KEY_SLASH;
    case VK_OEM_5: return KEY_BACKSLASH;
    case VK_OEM_3: return KEY_GRAVE;
  }
  return KEY_NONE;
}

static float window_dpi(HWND hwnd) {
  using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
  static auto fn = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  if (fn && hwnd) return fn(hwnd) / 96.0f;
  HDC dc = GetDC(nullptr);
  float d = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
  ReleaseDC(nullptr, dc);
  return d;
}

/* Is the mouse message being handled one Windows made from a pen? (Touch and pens mark theirs: MI_WP_SIGNATURE, with
 * 0x80 set for touch.) A mouse's or a finger's moves press at 1, whatever a pen did before. */
static bool message_from_pen() { return (GetMessageExtraInfo() & 0xFFFFFF80) == 0xFF515700; }
/* A mouse message Windows made from a touch (its signature with the touch bit). */
static bool message_from_touch() { return (GetMessageExtraInfo() & 0xFFFFFF80) == 0xFF515780; }

static void push(Window *w, Event e) {
  const bool pointer = e.type == EventType::MouseMove || e.type == EventType::MouseDown || e.type == EventType::MouseUp;
  /* (A mouse message Windows made from a touch only where the touch path below couldn't take it: an old Windows.) */
  if ((e.type == EventType::MouseMove || e.type == EventType::MouseDown) && w->pen_near && message_from_touch()) return;  // (a palm while the pen is near; a button-up always passes)
  if (pointer && message_from_touch()) e.pointer = Pointer::Touch;
  if (pointer && message_from_pen())
    e.pen = true, e.pointer = Pointer::Pen, e.eraser = w->pen_eraser, e.tilt_x = w->pen_tilt_x, e.tilt_y = w->pen_tilt_y, e.barrel = w->pen_barrel;
  if (e.type == EventType::MouseMove || e.type == EventType::MouseDown) e.pressure = message_from_pen() ? w->pen_pressure : 1.0f;
  w->queue.push_back(std::move(e));
}

/* The recognizer's steps as events (task 0213): one finger as the left button tagged Touch, two as a Gesture. */
static void push_touch_steps(Window *w, const std::vector<TouchStep> &steps) {
  for (const TouchStep &s : steps) {
    Event e;
    e.x = (int)std::lround(s.x), e.y = (int)std::lround(s.y);
    e.mods = current_mods();
    e.pointer = Pointer::Touch;
    switch (s.kind) {
      case TouchStep::Down: e.type = EventType::MouseDown; break;
      case TouchStep::Move: e.type = EventType::MouseMove; break;
      case TouchStep::Up: e.type = EventType::MouseUp; break;
      case TouchStep::Cancel: e.type = EventType::MouseUp, e.cancel = true; break;
      case TouchStep::Gesture: e.type = EventType::Gesture, e.zoom = s.zoom, e.pan_x = s.pan_x, e.pan_y = s.pan_y, e.rotate = s.rotate; break;
    }
    w->queue.push_back(std::move(e));
  }
}

/* Windows 8's pointer calls, looked up so the program still starts where they're missing. */
using GetPointerTypeFn = BOOL(WINAPI *)(UINT32, POINTER_INPUT_TYPE *);
using GetPointerPenInfoFn = BOOL(WINAPI *)(UINT32, POINTER_PEN_INFO *);
using GetPointerInfoFn = BOOL(WINAPI *)(UINT32, POINTER_INFO *);

static double touch_clock() { return GetTickCount64() / 1000.0; }

/* A finger (task 0213): into the recognizer, and handled here - Windows then makes no mouse messages or gestures of
 * it. False for a pen or a mouse (they go on to Windows' mouse messages) or where the pointer calls are missing. */
static bool on_touch_pointer(Window *w, HWND hwnd, UINT msg, WPARAM wp) {
  static auto get_type = (GetPointerTypeFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerType");
  static auto get_info = (GetPointerInfoFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerInfo");
  const UINT32 id = GET_POINTERID_WPARAM(wp);
  POINTER_INPUT_TYPE type = PT_POINTER;
  POINTER_INFO pi{};
  if (!get_type || !get_info || !get_type(id, &type) || type != PT_TOUCH || !get_info(id, &pi)) return false;
  POINT p = pi.ptPixelLocation;
  ScreenToClient(hwnd, &p);
  std::vector<TouchStep> steps;
  const double now = touch_clock();
  const bool canceled = (pi.pointerFlags & POINTER_FLAG_CANCELED) != 0;
  if (w->pen_near || canceled || msg == WM_POINTERCAPTURECHANGED) w->touch.clear(steps);  // (a palm while the pen is near; a touch Windows took away)
  else if (msg == WM_POINTERDOWN) w->touch.down(id, (float)p.x, (float)p.y, now, steps);
  else if (msg == WM_POINTERUPDATE) w->touch.move(id, (float)p.x, (float)p.y, now, steps);
  else if (msg == WM_POINTERUP) w->touch.up(id, (float)p.x, (float)p.y, now, steps);
  push_touch_steps(w, steps);
  return true;
}

/* A pen's pressure, noted from its pointer messages before Windows turns them into mouse messages (which carry it). */
static void note_pointer(Window *w, WPARAM wp) {
  static auto get_type = (GetPointerTypeFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerType");
  static auto get_pen = (GetPointerPenInfoFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerPenInfo");
  const UINT32 id = GET_POINTERID_WPARAM(wp);
  POINTER_INPUT_TYPE type = PT_POINTER;
  POINTER_PEN_INFO pen{};
  if (!get_type || !get_pen || !get_type(id, &type) || type != PT_PEN || !get_pen(id, &pen)) return;
  w->pen_pressure = (pen.penMask & PEN_MASK_PRESSURE) ? std::max(0.0f, std::min(pen.pressure / 1024.0f, 1.0f)) : 1.0f;
  w->pen_tilt_x = (pen.penMask & PEN_MASK_TILT_X) ? (float)pen.tiltX : 0.0f;
  w->pen_tilt_y = (pen.penMask & PEN_MASK_TILT_Y) ? (float)pen.tiltY : 0.0f;
  w->pen_eraser = (pen.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
  w->pen_barrel = (pen.penFlags & PEN_FLAG_BARREL) != 0;
  w->pen_near = true;
  w->pen_id = id;
}

static void mouse_button(Window *w, int button, bool down, LPARAM lp) {
  Event e;
  e.type = down ? EventType::MouseDown : EventType::MouseUp;
  e.button = button;
  e.x = GET_X_LPARAM(lp);
  e.y = GET_Y_LPARAM(lp);
  e.mods = current_mods();
  if (down) {
    if (w->buttons_down++ == 0) SetCapture(w->hwnd);
  }
  else if (w->buttons_down > 0 && --w->buttons_down == 0) {
    ReleaseCapture();
  }
  push(w, e);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  Window *w = (Window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (!w) return DefWindowProcW(hwnd, msg, wp, lp);
  switch (msg) {
    case WM_CLOSE: push(w, {EventType::Quit}); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      /* Drawn here when the editor can (its refresh); otherwise asked for by an event. Not both: the event would
       * draw the same frame again (task 0084). */
      if (!w->refresh || w->in_refresh) {
        push(w, {EventType::Repaint});
        return 0;
      }
      w->in_refresh = true;
      w->refresh();
      w->in_refresh = false;
      return 0;
    }
    case WM_SIZE: {
      Event e;
      e.type = EventType::Resize;
      e.x = LOWORD(lp);
      e.y = HIWORD(lp);
      push(w, e);
      /* Repaint during the modal resize loop. */
      if (w->refresh && !w->in_refresh) { w->in_refresh = true; w->refresh(); w->in_refresh = false; }
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto *mmi = (MINMAXINFO *)lp;
      mmi->ptMinTrackSize.x = w->min_w;
      mmi->ptMinTrackSize.y = w->min_h;
      return 0;
    }
    case WM_DPICHANGED: {
      w->dpi = HIWORD(wp) / 96.0f;
      RECT *r = (RECT *)lp;
      if (!w->placing) SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      push(w, {EventType::DpiChanged});
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(w->cursor ? w->cursor : LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_KILLFOCUS: push(w, {EventType::FocusLost}); break;
    case WM_MOUSEMOVE: {
      Event e;
      e.type = EventType::MouseMove;
      e.x = GET_X_LPARAM(lp);
      e.y = GET_Y_LPARAM(lp);
      e.mods = current_mods();
      push(w, e);
      return 0;
    }
    case WM_LBUTTONDOWN: mouse_button(w, 0, true, lp); return 0;
    case WM_LBUTTONUP: mouse_button(w, 0, false, lp); return 0;
    case WM_RBUTTONDOWN: mouse_button(w, 1, true, lp); return 0;
    case WM_RBUTTONUP: mouse_button(w, 1, false, lp); return 0;
    case WM_MBUTTONDOWN: mouse_button(w, 2, true, lp); return 0;
    case WM_MBUTTONUP: mouse_button(w, 2, false, lp); return 0;
    case WM_CAPTURECHANGED: w->buttons_down = 0; break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      Event e;
      e.type = EventType::Wheel;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      e.x = p.x;
      e.y = p.y;
      float d = GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
      if (msg == WM_MOUSEWHEEL) e.wheel_y = d; else e.wheel_x = d;
      e.mods = current_mods();
      push(w, e);
      return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
      if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;  // let Alt+F4 close
      Event e;
      e.type = down ? EventType::KeyDown : EventType::KeyUp;
      e.key = map_vk(wp);
      e.mods = current_mods();
      e.repeat = down && (lp & (1 << 30));
      if (e.key != KEY_NONE) push(w, e);
      return 0;
    }
    case WM_SYSCHAR: return 0;  // swallow Alt+key beeps
    case WM_CHAR: {
      uint32_t c = (uint32_t)wp;
      if (c >= 0xD800 && c <= 0xDBFF) { w->high_surrogate = c; return 0; }
      if (c >= 0xDC00 && c <= 0xDFFF && w->high_surrogate) {
        c = 0x10000 + ((w->high_surrogate - 0xD800) << 10) + (c - 0xDC00);
        w->high_surrogate = 0;
      }
      if (c >= 32 && c != 127) {
        Event e;
        e.type = EventType::Text;
        e.codepoint = c;
        push(w, e);
      }
      return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
      if (on_touch_pointer(w, hwnd, msg, wp)) return 0;
      note_pointer(w, wp);
      break;  // (on to DefWindowProc: the pen still arrives as the mouse)
    case WM_POINTERUP:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
      if (msg != WM_POINTERLEAVE && on_touch_pointer(w, hwnd, msg, wp)) return 0;
      if (msg == WM_POINTERLEAVE && GET_POINTERID_WPARAM(wp) == w->pen_id) w->pen_near = false;  // (the pen out of range: touches are fingers again)
      w->pen_pressure = 1.0f;  // (lifted or gone: nothing presses)
      w->pen_barrel = false;
      break;
    case WM_SETTINGCHANGE:
      /* Task 0218: the laptop folded or unfolded - a frame, so Tablet Mode follows at once. */
      if (lp && std::wcscmp((const wchar_t *)lp, L"ConvertibleSlateMode") == 0) push(w, Event{EventType::Repaint});
      break;
    case WM_DROPFILES: {
      HDROP drop = (HDROP)wp;
      Event e;
      e.type = EventType::Drop;
      POINT p;
      DragQueryPoint(drop, &p);
      e.x = p.x;
      e.y = p.y;
      UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < n; i++) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring buf(len + 1, L'\0');
        DragQueryFileW(drop, i, &buf[0], len + 1);
        buf.resize(len);
        std::string s = narrow(buf.c_str(), (int)buf.size());
        for (auto &ch : s)
          if (ch == '\\') ch = '/';
        e.paths.push_back(s);
      }
      DragFinish(drop);
      push(w, e);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Draws the app icon procedurally: a Unity-like cube outline over Blender orange. */
static HICON make_icon(int size) {
  std::vector<uint32_t> px(size * size, 0);
  float c = size * 0.5f, r = size * 0.46f;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      float dx = x + 0.5f - c, dy = y + 0.5f - c;
      if (dx * dx + dy * dy <= r * r) px[y * size + x] = 0xFFE87D0D;
    }
  /* Isometric cube edges. */
  auto line = [&](float x0, float y0, float x1, float y1) {
    int steps = size * 2;
    for (int i = 0; i <= steps; i++) {
      float t = i / (float)steps;
      int x = (int)(x0 + (x1 - x0) * t), y = (int)(y0 + (y1 - y0) * t);
      for (int oy = 0; oy < size / 24 + 1; oy++)
        for (int ox = 0; ox < size / 24 + 1; ox++)
          if (x + ox < size && y + oy < size) px[(y + oy) * size + x + ox] = 0xFFFFFFFF;
    }
  };
  float s = size * 0.26f;
  float top = c - s, mid = c, bot = c + s;
  line(c, top - s * 0.3f, c + s, mid - s * 0.6f);
  line(c, top - s * 0.3f, c - s, mid - s * 0.6f);
  line(c - s, mid - s * 0.6f, c, mid - 0.0f);
  line(c + s, mid - s * 0.6f, c, mid - 0.0f);
  line(c, mid, c, bot + s * 0.5f);
  line(c - s, mid - s * 0.6f, c - s, bot - s * 0.1f);
  line(c + s, mid - s * 0.6f, c + s, bot - s * 0.1f);
  line(c - s, bot - s * 0.1f, c, bot + s * 0.5f);
  line(c + s, bot - s * 0.1f, c, bot + s * 0.5f);

  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = size;
  bi.bV5Height = -size;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void *bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color) return nullptr;
  memcpy(bits, px.data(), px.size() * 4);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmColor = color;
  ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color);
  DeleteObject(mask);
  return icon;
}

/* The window class, once (the first window registers it; later ones reuse it). */
static const wchar_t *window_class() {
  static bool registered = false;
  if (registered) return L"BlendityWindow";
  /* Per-monitor DPI awareness so text stays crisp (loaded dynamically for Win7+). */
  using SetDpiCtxFn = BOOL(WINAPI *)(HANDLE);
  if (auto fn = (SetDpiCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
    fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
  else
    SetProcessDPIAware();
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"BlendityWindow";
  wc.hIcon = make_icon(64);
  wc.hIconSm = make_icon(32);
  RegisterClassExW(&wc);
  registered = true;
  return L"BlendityWindow";
}

/* A window with its outer rectangle (x, y, w, h; CW_USEDEFAULT for the system's place). */
static Window *make_window(const char *title, int x, int y, int ow, int oh) {
  auto *w = new Window();
  std::wstring wt = widen(title);
  w->hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, window_class(), wt.c_str(), WS_OVERLAPPEDWINDOW, x, y, ow, oh, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
  if (!w->hwnd) {
    delete w;
    return nullptr;
  }
  SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
  w->dpi = window_dpi(w->hwnd);
  DragAcceptFiles(w->hwnd, TRUE);
  /* Task 0217: a pen held still is the editor's (its quick menu), not Windows' press-and-hold right click. */
  SetPropW(w->hwnd, L"MicrosoftTabletPenServiceProperty", (HANDLE)(UINT_PTR)0x00000001);  // (TABLET_DISABLE_PRESSANDHOLD)
  return w;
}

static void frame_for_client(RECT &r, UINT dpi);
static UINT dpi_at(int x, int y);

Window *create_window(const char *title, int width, int height) {
  window_class();  // (the DPI awareness first: the scale below reads it)
  float scale = window_dpi(nullptr);
  RECT r{0, 0, (LONG)(width * scale), (LONG)(height * scale)};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  Window *w = make_window(title, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top);
  if (!w) return nullptr;
  ShowWindow(w->hwnd, SW_SHOWMAXIMIZED);
  UpdateWindow(w->hwnd);
  return w;
}

Window *create_secondary_window(const char *title, int x, int y, int width, int height) {
  window_class();
  RECT r{x, y, x + std::max(240, width), y + std::max(160, height)};
  frame_for_client(r, dpi_at(x + width / 2, y + height / 2));
  Window *w = make_window(title, r.left, r.top, r.right - r.left, r.bottom - r.top);
  if (!w) return nullptr;
  w->min_w = 240, w->min_h = 160;
  w->placing = true;  // (shown on its display at the size asked for)
  ShowWindow(w->hwnd, SW_SHOWNORMAL);
  UpdateWindow(w->hwnd);
  w->placing = false;
  return w;
}

static BOOL CALLBACK add_monitor(HMONITOR m, HDC, LPRECT, LPARAM data) {
  auto *out = (std::vector<Monitor> *)data;
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(m, &mi)) return TRUE;
  Monitor mon;
  mon.x = mi.rcMonitor.left, mon.y = mi.rcMonitor.top;
  mon.width = mi.rcMonitor.right - mi.rcMonitor.left, mon.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
  mon.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
  mon.name = narrow(mi.szDevice);
  using GetDpiForMonitorFn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
  static auto dpi_fn = (GetDpiForMonitorFn)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
  UINT dx = 96, dy = 96;
  if (dpi_fn && dpi_fn(m, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy) == S_OK) mon.dpi = dx / 96.0f;
  out->push_back(mon);
  return TRUE;
}

std::vector<Monitor> monitors() {
  std::vector<Monitor> out;
  EnumDisplayMonitors(nullptr, nullptr, add_monitor, (LPARAM)&out);
  std::stable_sort(out.begin(), out.end(), [](const Monitor &a, const Monitor &b) { return a.primary && !b.primary; });
  if (out.empty()) out.push_back({0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1.0f, "Display", true});
  return out;
}

void get_window_rect(Window *w, int &x, int &y, int &width, int &height) {
  RECT r;
  GetClientRect(w->hwnd, &r);
  POINT p{0, 0};
  ClientToScreen(w->hwnd, &p);
  x = p.x, y = p.y, width = r.right - r.left, height = r.bottom - r.top;
}

/* The frame around a client rectangle at a display's DPI (the system's DPI where Windows has no per-DPI call). */
static void frame_for_client(RECT &r, UINT dpi) {
  using AdjustForDpiFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
  static auto fn = (AdjustForDpiFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
  if (fn) fn(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
  else AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
}

static UINT dpi_at(int x, int y) {
  using GetDpiForMonitorFn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
  static auto fn = (GetDpiForMonitorFn)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
  UINT dx = 96, dy = 96;
  if (fn) fn(MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST), 0, &dx, &dy);
  return dx;
}

void set_fullscreen(Window *w, bool on) {
  if (w->fullscreen == on) return;
  w->fullscreen = on;
  w->placing = true;  // (the size asked for, whatever the display's DPI)
  if (on) {
    w->saved_style = GetWindowLongPtrW(w->hwnd, GWL_STYLE);
    GetWindowPlacement(w->hwnd, &w->saved_place);  // (its normal rectangle even when maximised)
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongPtrW(w->hwnd, GWL_STYLE, w->saved_style & ~WS_OVERLAPPEDWINDOW);
    const RECT &m = mi.rcMonitor;
    SetWindowPos(w->hwnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    w->placing = false;
    return;
  }
  SetWindowLongPtrW(w->hwnd, GWL_STYLE, w->saved_style);
  SetWindowPlacement(w->hwnd, &w->saved_place);
  SetWindowPos(w->hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  w->placing = false;
}

void set_window_rect(Window *w, int x, int y, int width, int height) {
  RECT r{x, y, x + width, y + height};
  frame_for_client(r, dpi_at(x + width / 2, y + height / 2));
  w->placing = true;
  ShowWindow(w->hwnd, SW_SHOWNORMAL);
  SetWindowPos(w->hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
  w->placing = false;
}

void destroy_window(Window *w) {
  if (!w) return;
  DestroyWindow(w->hwnd);
  delete w;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#  define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* Waits that end when asked (task 0084): Windows rounds a wait's timeout up to its system timer's tick, 15.6 ms
 * unless a program raises it, and a 60 fps cap then ran at 33 fps. A high-resolution waitable timer (Windows 10
 * 1803 and later) isn't rounded; where there is none, the tick is raised to 1 ms for this process instead. One timer,
 * for the thread that polls the window's events (only ever the editor's main thread). */
static HANDLE precise_timer() {
  static const HANDLE timer = [] {
    HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!h) timeBeginPeriod(1);
    return h;
  }();
  return timer;
}

/* Until input arrives (or is already waiting) or `seconds` have passed. */
static void wait_for_input(double seconds) {
  const DWORD ms = (DWORD)std::ceil(seconds * 1000.0);
  HANDLE timer = precise_timer();
  LARGE_INTEGER due;
  due.QuadPart = -(LONGLONG)(seconds * 1e7);  // (relative, in 100 ns)
  if (!timer || !SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
    MsgWaitForMultipleObjectsEx(0, nullptr, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    return;
  }
  MsgWaitForMultipleObjectsEx(1, &timer, ms + 2, QS_ALLINPUT, MWMO_INPUTAVAILABLE);  // (+2: never hangs on the timer)
  CancelWaitableTimer(timer);
}

void poll_events(Window *w, std::vector<Event> &out, int timeout_ms) { poll_events_for(w, out, timeout_ms / 1000.0); }

void poll_events_for(Window *w, std::vector<Event> &out, double seconds) {
  MSG msg;
  bool any = PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE) || !w->queue.empty();
  if (w->touch.holding()) seconds = std::min(seconds, 0.02);  // (a finger's held press is let through on time)
  if (!any && seconds > 0.0) wait_for_input(seconds);
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) w->queue.push_back({EventType::Quit});
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  std::vector<TouchStep> held;
  w->touch.poll(touch_clock(), held);
  push_touch_steps(w, held);
  for (auto &e : w->queue) out.push_back(std::move(e));
  w->queue.clear();
}

void present(Window *w, const uint32_t *pixels, int width, int height) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(w->hwnd);
  SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, pixels, &bi, DIB_RGB_COLORS);
  ReleaseDC(w->hwnd, dc);
}

void present_rows(Window *w, const uint32_t *pixels, int width, int height, const std::vector<std::pair<int, int>> &rows) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(w->hwnd);
  for (auto [y0, y1] : rows) {
    y0 = std::max(0, y0), y1 = std::min(height, y1);
    if (y1 <= y0) continue;
    bi.bmiHeader.biHeight = -(y1 - y0);  // (the band alone, top-down)
    SetDIBitsToDevice(dc, 0, y0, width, y1 - y0, 0, 0, 0, y1 - y0, pixels + (size_t)y0 * width, &bi, DIB_RGB_COLORS);
  }
  ReleaseDC(w->hwnd, dc);
}

void *native_handle(Window *w) { return w ? (void *)w->hwnd : nullptr; }
void *native_display(Window *) { return nullptr; }

void get_framebuffer_size(Window *w, int &width, int &height) {
  RECT r;
  GetClientRect(w->hwnd, &r);
  width = r.right - r.left;
  height = r.bottom - r.top;
}

float dpi_scale(Window *w) { return w->dpi; }

/* The convertible's posture: Windows' slate mode means something only on a machine that has one. Many desktops and
 * ordinary laptops read 0 (slate) too, so - as Chromium does - only a touch screen, undocked, on a mobile or slate
 * power role is asked. */
bool tablet_posture() {
  if (GetSystemMetrics(SM_MAXIMUMTOUCHES) <= 0 || GetSystemMetrics(SM_SYSTEMDOCKED) != 0) return false;
  using RoleFn = int(WINAPI *)(ULONG);  // (PowerDeterminePlatformRoleEx: 2 mobile, 8 slate)
  static HMODULE pp = LoadLibraryW(L"powrprof.dll");
  static auto role_of = pp ? (RoleFn)GetProcAddress(pp, "PowerDeterminePlatformRoleEx") : nullptr;
  const int role = role_of ? role_of(2 /* POWER_PLATFORM_ROLE_V2 */) : 0;
  if (role != 2 && role != 8) return false;
  return GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0;  // (0: slate - the keyboard away)
}

void set_title(Window *w, const std::string &title) { SetWindowTextW(w->hwnd, widen(title).c_str()); }

void set_cursor(Window *w, Cursor c) {
  LPCTSTR id = IDC_ARROW;
  switch (c) {
    case Cursor::Arrow: id = IDC_ARROW; break;
    case Cursor::IBeam: id = IDC_IBEAM; break;
    case Cursor::ResizeH: id = IDC_SIZEWE; break;
    case Cursor::ResizeV: id = IDC_SIZENS; break;
    case Cursor::Move: id = IDC_SIZEALL; break;
    case Cursor::Hand: id = IDC_HAND; break;
  }
  HCURSOR hc = LoadCursor(nullptr, id);
  if (hc != w->cursor) {
    w->cursor = hc;
    SetCursor(hc);
  }
}

std::string get_clipboard(Window *w) {
  std::string out;
  if (!w || !OpenClipboard(w->hwnd)) return out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (auto *p = (const wchar_t *)GlobalLock(h)) {
      out = narrow(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

/* A DIB (as the clipboard holds a screenshot) given its file header, to be read as a BMP: the pixels start after the
 * header, the colour masks of a 40-byte header with BI_BITFIELDS (12 bytes; 16 with alpha) and the palette. */
std::string dib_as_bmp(const void *dib, size_t size) {
  if (!dib || size < sizeof(BITMAPINFOHEADER)) return {};
  const auto *bi = (const BITMAPINFOHEADER *)dib;
  const size_t colors = bi->biClrUsed ? bi->biClrUsed : (bi->biBitCount <= 8 ? (size_t)1 << bi->biBitCount : 0);
  const bool info_header = bi->biSize == sizeof(BITMAPINFOHEADER);  // (not a V4 / V5 header, whose masks are inside it)
  const size_t masks = info_header && bi->biCompression == BI_BITFIELDS ? 12 : info_header && bi->biCompression == 6 /* BI_ALPHABITFIELDS */ ? 16 : 0;
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4D42;
  fh.bfSize = (DWORD)(sizeof(fh) + size);
  fh.bfOffBits = (DWORD)(sizeof(fh) + bi->biSize + masks + colors * 4);
  std::string out((const char *)&fh, sizeof(fh));
  out.append((const char *)dib, size);
  return out;
}

static std::string clipboard_bytes(UINT format) {
  std::string out;
  HANDLE h = GetClipboardData(format);
  const void *p = h ? GlobalLock(h) : nullptr;
  if (!p) return out;
  out.assign((const char *)p, GlobalSize(h));
  GlobalUnlock(h);
  return out;
}

std::string get_clipboard_file(Window *w) {
  if (!w || !OpenClipboard(w->hwnd)) return {};
  std::string out;
  if (auto drop = (HDROP)GetClipboardData(CF_HDROP); drop && DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) > 0) {
    const UINT len = DragQueryFileW(drop, 0, nullptr, 0);
    std::vector<wchar_t> path((size_t)len + 1, L'\0');
    DragQueryFileW(drop, 0, path.data(), len + 1);
    out = narrow(path.data());
  }
  CloseClipboard();
  return out;
}

static std::string dib_as_bmp_from_clipboard() {
  const std::string dib = clipboard_bytes(CF_DIB);
  return dib_as_bmp(dib.data(), dib.size());
}

std::string get_clipboard_image(Window *w) {
  if (!w || !OpenClipboard(w->hwnd)) return {};
  std::string out = clipboard_bytes(RegisterClipboardFormatW(L"PNG"));  // (browsers and most paint programs put a PNG)
  if (out.size() < 8 || out.compare(1, 3, "PNG") != 0) out = dib_as_bmp_from_clipboard();  // (a screenshot: the DIB)
  CloseClipboard();
  return out;
}

void set_clipboard(Window *w, const std::string &utf8) {
  std::wstring ws = widen(utf8);
  if (!OpenClipboard(w->hwnd)) return;
  EmptyClipboard();
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (ws.size() + 1) * sizeof(wchar_t));
  if (h) {
    auto *p = (wchar_t *)GlobalLock(h);
    memcpy(p, ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
  }
  CloseClipboard();
}

void set_refresh_callback(Window *w, std::function<void()> cb) { w->refresh = std::move(cb); }

/* ------------------------------------------------------------ File dialogs */

bool file_dialogs_available() { return true; }

/* "Name (*.a;*.b)\0*.a;*.b\0...\0\0" */
static std::wstring dialog_filter(const std::vector<FileFilter> &filters) {
  std::wstring f;
  for (const FileFilter &ff : filters) {
    std::string pat;
    for (const std::string &e : ff.extensions) pat += (pat.empty() ? "*" : ";*") + e;
    if (pat.empty()) pat = "*.*";
    f += widen(ff.name + " (" + pat + ")");
    f += L'\0';
    f += widen(pat);
    f += L'\0';
  }
  f += L'\0';
  return f;
}

static bool run_file_dialog(Window *w, bool save, const std::string &title, const std::string &initial, const std::vector<FileFilter> &filters,
                            std::string &out, int *filter_index) {
  std::wstring path = widen(initial);
  for (wchar_t &c : path)
    if (c == L'/') c = L'\\';
  std::wstring dir, name = path;
  if (!save) {
    dir = path;
    name.clear();
  }
  else {
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      dir = path.substr(0, slash);
      name = path.substr(slash + 1);
    }
  }
  std::vector<wchar_t> buf(32768, L'\0');
  std::copy(name.begin(), name.begin() + std::min(name.size(), buf.size() - 1), buf.begin());
  const std::wstring filter = dialog_filter(filters), wtitle = widen(title);
  const int start = filter_index ? std::max(0, std::min(*filter_index, (int)filters.size() - 1)) : 0;
  std::wstring def_ext;
  if (!filters.empty() && !filters[(size_t)start].extensions.empty()) def_ext = widen(filters[(size_t)start].extensions[0].substr(1));
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = w ? w->hwnd : nullptr;
  ofn.lpstrFilter = filters.empty() ? nullptr : filter.c_str();
  ofn.nFilterIndex = (DWORD)start + 1;
  ofn.lpstrFile = buf.data();
  ofn.nMaxFile = (DWORD)buf.size();
  ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
  ofn.lpstrTitle = wtitle.c_str();
  ofn.lpstrDefExt = def_ext.empty() ? nullptr : def_ext.c_str();  // appended when the name has no extension
  ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
  /* The dialog runs its own message loop: no repaints of our window from
   * inside it (the editor is in the middle of a frame), and no stale capture. */
  const bool was = w ? w->in_refresh : false;
  if (w) {
    w->in_refresh = true;
    if (w->buttons_down) {
      w->buttons_down = 0;
      ReleaseCapture();
    }
  }
  const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
  if (w) w->in_refresh = was;
  if (!ok) return false;
  out = narrow(buf.data());
  if (filter_index) *filter_index = (int)ofn.nFilterIndex - 1;
  return true;
}

bool save_file_dialog(Window *w, const std::string &title, const std::string &initial_path, const std::vector<FileFilter> &filters,
                      std::string &out_path, int *filter_index) {
  return run_file_dialog(w, true, title, initial_path, filters, out_path, filter_index);
}

bool open_file_dialog(Window *w, const std::string &title, const std::string &initial_dir, const std::vector<FileFilter> &filters,
                      std::string &out_path) {
  return run_file_dialog(w, false, title, initial_dir, filters, out_path, nullptr);
}

/* The shell's folder chooser (task 0271), COM initialised for it on this thread; the initial folder through its callback. */
static int CALLBACK folder_dialog_start(HWND h, UINT msg, LPARAM, LPARAM data) {
  if (msg == BFFM_INITIALIZED && data) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
  return 0;
}

/* The Vista folder picker: true when chosen; `made` false when the dialog couldn't be created (the caller falls back). */
static bool pick_folder_vista(HWND owner, const std::wstring &title, const std::wstring &start, std::string &out_path, bool &made) {
  IFileOpenDialog *dlg = nullptr;
  made = SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)));
  if (!made) return false;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(title.c_str());
  IShellItem *folder = nullptr;
  if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
    dlg->SetFolder(folder);
    folder->Release();
  }
  bool ok = false;
  IShellItem *item = nullptr;
  if (SUCCEEDED(dlg->Show(owner)) && SUCCEEDED(dlg->GetResult(&item))) {
    PWSTR path = nullptr;
    ok = SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path;
    if (ok) out_path = narrow(path);
    if (path) CoTaskMemFree(path);
    item->Release();
  }
  dlg->Release();
  return ok;
}

bool folder_dialog(Window *w, const std::string &title, const std::string &initial_dir, std::string &out_path) {
  const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const std::wstring wtitle = widen(title), start = widen(initial_dir);
  const bool was_refresh = w ? w->in_refresh : false;
  if (w) {
    w->in_refresh = true;
    if (w->buttons_down) w->buttons_down = 0, ReleaseCapture();
  }
  bool made = false;
  const bool chosen = pick_folder_vista(w ? w->hwnd : nullptr, wtitle, start, out_path, made);
  if (w) w->in_refresh = was_refresh;
  if (made) {
    if (SUCCEEDED(co)) CoUninitialize();
    return chosen;
  }
  BROWSEINFOW bi{};
  bi.hwndOwner = w ? w->hwnd : nullptr;
  bi.lpszTitle = wtitle.c_str();
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  bi.lpfn = folder_dialog_start;
  bi.lParam = start.empty() ? 0 : (LPARAM)start.c_str();
  const bool was = w ? w->in_refresh : false;
  if (w) {
    w->in_refresh = true;
    if (w->buttons_down) w->buttons_down = 0, ReleaseCapture();
  }
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (w) w->in_refresh = was;
  bool ok = false;
  if (pidl) {
    wchar_t buf[MAX_PATH * 4] = {0};
    ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    if (ok) out_path = narrow(buf);
    CoTaskMemFree(pidl);
  }
  if (SUCCEEDED(co)) CoUninitialize();
  return ok;
}

}  // namespace bl::platform

#endif  // _WIN32
