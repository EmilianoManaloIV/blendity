// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 backend (compare blender/intern/ghost/intern/GHOST_SystemWin32.cc).
// Links only against system DLLs: user32, gdi32, shell32, comdlg32.
#ifdef _WIN32

#include "platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#ifdef _MSC_VER
#  pragma comment(lib, "user32.lib")
#  pragma comment(lib, "gdi32.lib")
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "comdlg32.lib")
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

static void push(Window *w, Event e) { w->queue.push_back(std::move(e)); }

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
      push(w, {EventType::Repaint});
      if (w->refresh && !w->in_refresh) { w->in_refresh = true; w->refresh(); w->in_refresh = false; }
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
      mmi->ptMinTrackSize.x = 640;
      mmi->ptMinTrackSize.y = 400;
      return 0;
    }
    case WM_DPICHANGED: {
      w->dpi = HIWORD(wp) / 96.0f;
      RECT *r = (RECT *)lp;
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
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

Window *create_window(const char *title, int width, int height) {
  /* Per-monitor DPI awareness so text stays crisp (loaded dynamically for Win7+). */
  using SetDpiCtxFn = BOOL(WINAPI *)(HANDLE);
  if (auto fn = (SetDpiCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
    fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
  else
    SetProcessDPIAware();

  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"BlendityWindow";
  wc.hIcon = make_icon(64);
  wc.hIconSm = make_icon(32);
  RegisterClassExW(&wc);

  auto *w = new Window();
  float scale = window_dpi(nullptr);
  RECT r{0, 0, (LONG)(width * scale), (LONG)(height * scale)};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  std::wstring wt = widen(title);
  w->hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, wt.c_str(), WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                            inst, nullptr);
  if (!w->hwnd) {
    delete w;
    return nullptr;
  }
  SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
  w->dpi = window_dpi(w->hwnd);
  DragAcceptFiles(w->hwnd, TRUE);
  ShowWindow(w->hwnd, SW_SHOWMAXIMIZED);
  UpdateWindow(w->hwnd);
  return w;
}

void destroy_window(Window *w) {
  if (!w) return;
  DestroyWindow(w->hwnd);
  delete w;
}

void poll_events(Window *w, std::vector<Event> &out, int timeout_ms) {
  MSG msg;
  bool any = PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE) || !w->queue.empty();
  if (!any && timeout_ms > 0) MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)timeout_ms, QS_ALLINPUT);
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) w->queue.push_back({EventType::Quit});
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
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

void get_framebuffer_size(Window *w, int &width, int &height) {
  RECT r;
  GetClientRect(w->hwnd, &r);
  width = r.right - r.left;
  height = r.bottom - r.top;
}

float dpi_scale(Window *w) { return w->dpi; }

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
  if (!OpenClipboard(w->hwnd)) return out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (auto *p = (const wchar_t *)GlobalLock(h)) {
      out = narrow(p);
      GlobalUnlock(h);
    }
  }
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

}  // namespace bl::platform

#endif  // _WIN32
