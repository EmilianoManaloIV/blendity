// SPDX-License-Identifier: GPL-2.0-or-later
// X11 backend (compare blender/intern/ghost/intern/GHOST_SystemX11.cc).
// Only needs libX11, which every X11/XWayland desktop ships.
#if !defined(_WIN32) && !defined(__APPLE__)

#include "platform.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <sys/select.h>

#include <cstdlib>
#include <cstring>

namespace bl::platform {

struct Window {
  Display *dpy = nullptr;
  ::Window win = 0;
  GC gc = nullptr;
  XImage *image = nullptr;
  std::vector<uint32_t> image_pixels;
  int img_w = 0, img_h = 0;
  XIM im = nullptr;
  XIC ic = nullptr;
  Atom wm_delete, clipboard, utf8, targets, prop, xdnd_aware, xdnd_enter, xdnd_position, xdnd_status, xdnd_drop,
      xdnd_finished, xdnd_selection, xdnd_action_copy, uri_list;
  ::Window xdnd_source = 0;
  int xdnd_x = 0, xdnd_y = 0;
  std::string clipboard_text;
  std::vector<Event> queue;
  std::function<void()> refresh;
  float dpi = 1.0f;
  int width = 0, height = 0;
  Cursor current = Cursor::Arrow;
};

void append_utf8(std::string &s, uint32_t cp) {
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
}

static uint32_t decode_utf8(const char *&p, const char *end) {
  unsigned char c = (unsigned char)*p++;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : 1);
  uint32_t cp = c & (0x3F >> n);
  for (int i = 0; i < n && p < end; i++) cp = (cp << 6) | ((unsigned char)*p++ & 0x3F);
  return cp;
}

static int mods_from_state(unsigned int s) {
  int m = 0;
  if (s & ShiftMask) m |= MOD_SHIFT;
  if (s & ControlMask) m |= MOD_CTRL;
  if (s & Mod1Mask) m |= MOD_ALT;
  if (s & Mod4Mask) m |= MOD_SUPER;
  return m;
}

static int map_keysym(KeySym ks) {
  if (ks >= XK_a && ks <= XK_z) return KEY_A + (int)(ks - XK_a);
  if (ks >= XK_A && ks <= XK_Z) return KEY_A + (int)(ks - XK_A);
  if (ks >= XK_0 && ks <= XK_9) return KEY_0 + (int)(ks - XK_0);
  if (ks >= XK_KP_0 && ks <= XK_KP_9) return KEY_0 + (int)(ks - XK_KP_0);
  if (ks >= XK_F1 && ks <= XK_F12) return KEY_F1 + (int)(ks - XK_F1);
  switch (ks) {
    case XK_space: return KEY_SPACE;
    case XK_Escape: return KEY_ESCAPE;
    case XK_Return: case XK_KP_Enter: return KEY_ENTER;
    case XK_Tab: case XK_ISO_Left_Tab: return KEY_TAB;
    case XK_BackSpace: return KEY_BACKSPACE;
    case XK_Delete: case XK_KP_Delete: return KEY_DELETE;
    case XK_Insert: return KEY_INSERT;
    case XK_Left: return KEY_LEFT;
    case XK_Right: return KEY_RIGHT;
    case XK_Up: return KEY_UP;
    case XK_Down: return KEY_DOWN;
    case XK_Home: return KEY_HOME;
    case XK_End: return KEY_END;
    case XK_Page_Up: return KEY_PAGE_UP;
    case XK_Page_Down: return KEY_PAGE_DOWN;
    case XK_Shift_L: case XK_Shift_R: return KEY_SHIFT;
    case XK_Control_L: case XK_Control_R: return KEY_CTRL;
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return KEY_ALT;
    case XK_Super_L: case XK_Super_R: return KEY_SUPER;
    case XK_minus: case XK_KP_Subtract: return KEY_MINUS;
    case XK_equal: case XK_plus: case XK_KP_Add: return KEY_EQUALS;
    case XK_bracketleft: return KEY_LBRACKET;
    case XK_bracketright: return KEY_RBRACKET;
    case XK_semicolon: return KEY_SEMICOLON;
    case XK_apostrophe: return KEY_APOSTROPHE;
    case XK_comma: return KEY_COMMA;
    case XK_period: case XK_KP_Decimal: return KEY_PERIOD;
    case XK_slash: case XK_KP_Divide: return KEY_SLASH;
    case XK_backslash: return KEY_BACKSLASH;
    case XK_grave: return KEY_GRAVE;
  }
  return KEY_NONE;
}

static float query_dpi(Display *dpy) {
  if (const char *env = std::getenv("BLENDITY_SCALE")) return (float)std::atof(env);
  if (char *rms = XResourceManagerString(dpy)) {
    if (const char *p = std::strstr(rms, "Xft.dpi:")) {
      float dpi = (float)std::atof(p + 8);
      if (dpi > 0) return dpi / 96.0f;
    }
  }
  return 1.0f;
}

static std::string percent_decode(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    }
    else out += s[i];
  }
  return out;
}

Window *create_window(const char *title, int width, int height) {
  XInitThreads();
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) return nullptr;
  auto *w = new Window();
  w->dpy = dpy;
  w->dpi = query_dpi(dpy);
  int screen = DefaultScreen(dpy);
  w->width = (int)(width * w->dpi);
  w->height = (int)(height * w->dpi);
  w->win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, w->width, w->height, 0, BlackPixel(dpy, screen),
                               BlackPixel(dpy, screen));
  XSelectInput(dpy, w->win,
               ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                   StructureNotifyMask | FocusChangeMask);
  XStoreName(dpy, w->win, title);
  Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
  w->utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  XChangeProperty(dpy, w->win, net_name, w->utf8, 8, PropModeReplace, (const unsigned char *)title, (int)std::strlen(title));
  w->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dpy, w->win, &w->wm_delete, 1);
  w->clipboard = XInternAtom(dpy, "CLIPBOARD", False);
  w->targets = XInternAtom(dpy, "TARGETS", False);
  w->prop = XInternAtom(dpy, "BLENDITY_SEL", False);
  w->xdnd_aware = XInternAtom(dpy, "XdndAware", False);
  w->xdnd_enter = XInternAtom(dpy, "XdndEnter", False);
  w->xdnd_position = XInternAtom(dpy, "XdndPosition", False);
  w->xdnd_status = XInternAtom(dpy, "XdndStatus", False);
  w->xdnd_drop = XInternAtom(dpy, "XdndDrop", False);
  w->xdnd_finished = XInternAtom(dpy, "XdndFinished", False);
  w->xdnd_selection = XInternAtom(dpy, "XdndSelection", False);
  w->xdnd_action_copy = XInternAtom(dpy, "XdndActionCopy", False);
  w->uri_list = XInternAtom(dpy, "text/uri-list", False);
  long version = 5;
  XChangeProperty(dpy, w->win, w->xdnd_aware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);

  XSizeHints hints{};
  hints.flags = PMinSize;
  hints.min_width = 640;
  hints.min_height = 400;
  XSetWMNormalHints(dpy, w->win, &hints);

  w->gc = XCreateGC(dpy, w->win, 0, nullptr);
  XkbSetDetectableAutoRepeat(dpy, True, nullptr);
  w->im = XOpenIM(dpy, nullptr, nullptr, nullptr);
  if (w->im)
    w->ic = XCreateIC(w->im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, w->win,
                      XNFocusWindow, w->win, nullptr);
  XMapWindow(dpy, w->win);
  XFlush(dpy);
  return w;
}

void destroy_window(Window *w) {
  if (!w) return;
  if (w->image) {
    w->image->data = nullptr;  // we own the pixels
    XDestroyImage(w->image);
  }
  if (w->ic) XDestroyIC(w->ic);
  if (w->im) XCloseIM(w->im);
  XFreeGC(w->dpy, w->gc);
  XDestroyWindow(w->dpy, w->win);
  XCloseDisplay(w->dpy);
  delete w;
}

static void send_xdnd_status(Window *w, bool accept) {
  XClientMessageEvent m{};
  m.type = ClientMessage;
  m.display = w->dpy;
  m.window = w->xdnd_source;
  m.message_type = w->xdnd_status;
  m.format = 32;
  m.data.l[0] = (long)w->win;
  m.data.l[1] = accept ? 1 : 0;
  m.data.l[4] = accept ? (long)w->xdnd_action_copy : 0;
  XSendEvent(w->dpy, w->xdnd_source, False, NoEventMask, (XEvent *)&m);
}

static void handle_event(Window *w, XEvent &xe) {
  Display *dpy = w->dpy;
  switch (xe.type) {
    case ClientMessage: {
      auto &cm = xe.xclient;
      if ((Atom)cm.data.l[0] == w->wm_delete) { w->queue.push_back({EventType::Quit}); break; }
      if (cm.message_type == w->xdnd_enter) { w->xdnd_source = (::Window)cm.data.l[0]; break; }
      if (cm.message_type == w->xdnd_position) {
        w->xdnd_source = (::Window)cm.data.l[0];
        int rx = (int)((cm.data.l[2] >> 16) & 0xFFFF), ry = (int)(cm.data.l[2] & 0xFFFF);
        ::Window child;
        XTranslateCoordinates(dpy, DefaultRootWindow(dpy), w->win, rx, ry, &w->xdnd_x, &w->xdnd_y, &child);
        send_xdnd_status(w, true);
        break;
      }
      if (cm.message_type == w->xdnd_drop) {
        w->xdnd_source = (::Window)cm.data.l[0];
        XConvertSelection(dpy, w->xdnd_selection, w->uri_list, w->xdnd_selection, w->win, (Time)cm.data.l[2]);
        break;
      }
      break;
    }
    case SelectionNotify: {
      if (xe.xselection.property == None) break;
      Atom type;
      int format;
      unsigned long count, after;
      unsigned char *data = nullptr;
      XGetWindowProperty(dpy, w->win, xe.xselection.property, 0, 1 << 24, True, AnyPropertyType, &type, &format,
                         &count, &after, &data);
      if (data && xe.xselection.selection == w->xdnd_selection) {
        Event e;
        e.type = EventType::Drop;
        e.x = w->xdnd_x;
        e.y = w->xdnd_y;
        std::string list((const char *)data, count);
        size_t pos = 0;
        while (pos < list.size()) {
          size_t eol = list.find('\n', pos);
          std::string line = list.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
          pos = eol == std::string::npos ? list.size() : eol + 1;
          while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
          if (line.rfind("file://", 0) == 0) {
            line = line.substr(7);
            size_t slash = line.find('/');
            if (slash != std::string::npos) line = line.substr(slash);  // strip hostname
            e.paths.push_back(percent_decode(line));
          }
        }
        if (!e.paths.empty()) w->queue.push_back(e);
        XClientMessageEvent m{};
        m.type = ClientMessage;
        m.display = dpy;
        m.window = w->xdnd_source;
        m.message_type = w->xdnd_finished;
        m.format = 32;
        m.data.l[0] = (long)w->win;
        m.data.l[1] = 1;
        m.data.l[2] = (long)w->xdnd_action_copy;
        XSendEvent(dpy, w->xdnd_source, False, NoEventMask, (XEvent *)&m);
      }
      else if (data && xe.xselection.selection == w->clipboard) {
        w->clipboard_text.assign((const char *)data, count);
      }
      if (data) XFree(data);
      break;
    }
    case SelectionRequest: {
      auto &req = xe.xselectionrequest;
      XSelectionEvent ev{};
      ev.type = SelectionNotify;
      ev.display = req.display;
      ev.requestor = req.requestor;
      ev.selection = req.selection;
      ev.target = req.target;
      ev.time = req.time;
      ev.property = None;
      if (req.target == w->targets) {
        Atom supported[] = {w->targets, w->utf8, XA_STRING};
        XChangeProperty(dpy, req.requestor, req.property, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, 3);
        ev.property = req.property;
      }
      else if (req.target == w->utf8 || req.target == XA_STRING) {
        XChangeProperty(dpy, req.requestor, req.property, req.target, 8, PropModeReplace,
                        (const unsigned char *)w->clipboard_text.data(), (int)w->clipboard_text.size());
        ev.property = req.property;
      }
      XSendEvent(dpy, req.requestor, False, NoEventMask, (XEvent *)&ev);
      break;
    }
    case ConfigureNotify:
      if (xe.xconfigure.width != w->width || xe.xconfigure.height != w->height) {
        w->width = xe.xconfigure.width;
        w->height = xe.xconfigure.height;
        Event e;
        e.type = EventType::Resize;
        e.x = w->width;
        e.y = w->height;
        w->queue.push_back(e);
      }
      break;
    case Expose: w->queue.push_back({EventType::Repaint}); break;
    case FocusOut: w->queue.push_back({EventType::FocusLost}); break;
    case MotionNotify: {
      Event e;
      e.type = EventType::MouseMove;
      e.x = xe.xmotion.x;
      e.y = xe.xmotion.y;
      e.mods = mods_from_state(xe.xmotion.state);
      w->queue.push_back(e);
      break;
    }
    case ButtonPress:
    case ButtonRelease: {
      unsigned b = xe.xbutton.button;
      Event e;
      e.x = xe.xbutton.x;
      e.y = xe.xbutton.y;
      e.mods = mods_from_state(xe.xbutton.state);
      if (b >= 4 && b <= 7) {
        if (xe.type == ButtonRelease) break;
        e.type = EventType::Wheel;
        if (b == 4) e.wheel_y = 1;
        if (b == 5) e.wheel_y = -1;
        if (b == 6) e.wheel_x = 1;
        if (b == 7) e.wheel_x = -1;
      }
      else {
        e.type = xe.type == ButtonPress ? EventType::MouseDown : EventType::MouseUp;
        e.button = b == 1 ? 0 : (b == 3 ? 1 : 2);
      }
      w->queue.push_back(e);
      break;
    }
    case KeyPress:
    case KeyRelease: {
      KeySym ks = XLookupKeysym(&xe.xkey, 0);
      Event e;
      e.type = xe.type == KeyPress ? EventType::KeyDown : EventType::KeyUp;
      e.key = map_keysym(ks);
      e.mods = mods_from_state(xe.xkey.state);
      if (e.key != KEY_NONE) w->queue.push_back(e);
      if (xe.type == KeyPress) {
        char buf[64];
        int n = 0;
        KeySym sym;
        if (w->ic) {
          Status st;
          n = Xutf8LookupString(w->ic, &xe.xkey, buf, sizeof(buf) - 1, &sym, &st);
          if (st != XLookupChars && st != XLookupBoth) n = 0;
        }
        else {
          n = XLookupString(&xe.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
        }
        const char *p = buf, *end = buf + (n > 0 ? n : 0);
        while (p < end) {
          uint32_t cp = decode_utf8(p, end);
          if (cp >= 32 && cp != 127 && !(e.mods & MOD_CTRL)) {
            Event t;
            t.type = EventType::Text;
            t.codepoint = cp;
            w->queue.push_back(t);
          }
        }
      }
      break;
    }
  }
}

void poll_events(Window *w, std::vector<Event> &out, int timeout_ms) {
  if (XPending(w->dpy) == 0 && w->queue.empty() && timeout_ms > 0) {
    int fd = ConnectionNumber(w->dpy);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    select(fd + 1, &fds, nullptr, nullptr, &tv);
  }
  while (XPending(w->dpy)) {
    XEvent xe;
    XNextEvent(w->dpy, &xe);
    if (XFilterEvent(&xe, None)) continue;
    handle_event(w, xe);
  }
  for (auto &e : w->queue) out.push_back(std::move(e));
  w->queue.clear();
}

void present(Window *w, const uint32_t *pixels, int width, int height) {
  if (!w->image || w->img_w != width || w->img_h != height) {
    if (w->image) {
      w->image->data = nullptr;
      XDestroyImage(w->image);
    }
    w->image_pixels.resize((size_t)width * height);
    Visual *vis = DefaultVisual(w->dpy, DefaultScreen(w->dpy));
    int depth = DefaultDepth(w->dpy, DefaultScreen(w->dpy));
    w->image = XCreateImage(w->dpy, vis, depth, ZPixmap, 0, (char *)w->image_pixels.data(), width, height, 32, 0);
    w->img_w = width;
    w->img_h = height;
  }
  std::memcpy(w->image_pixels.data(), pixels, (size_t)width * height * 4);
  XPutImage(w->dpy, w->win, w->gc, w->image, 0, 0, 0, 0, width, height);
  XFlush(w->dpy);
}

void get_framebuffer_size(Window *w, int &width, int &height) {
  width = w->width;
  height = w->height;
}

float dpi_scale(Window *w) { return w->dpi; }

void set_title(Window *w, const std::string &title) {
  XStoreName(w->dpy, w->win, title.c_str());
  Atom net_name = XInternAtom(w->dpy, "_NET_WM_NAME", False);
  XChangeProperty(w->dpy, w->win, net_name, w->utf8, 8, PropModeReplace, (const unsigned char *)title.c_str(),
                  (int)title.size());
}

void set_cursor(Window *w, Cursor c) {
  if (c == w->current) return;
  w->current = c;
  unsigned shape = XC_left_ptr;
  switch (c) {
    case Cursor::Arrow: shape = XC_left_ptr; break;
    case Cursor::IBeam: shape = XC_xterm; break;
    case Cursor::ResizeH: shape = XC_sb_h_double_arrow; break;
    case Cursor::ResizeV: shape = XC_sb_v_double_arrow; break;
    case Cursor::Move: shape = XC_fleur; break;
    case Cursor::Hand: shape = XC_hand2; break;
  }
  ::Cursor xc = XCreateFontCursor(w->dpy, shape);
  XDefineCursor(w->dpy, w->win, xc);
  XFreeCursor(w->dpy, xc);
}

std::string get_clipboard(Window *w) {
  ::Window owner = XGetSelectionOwner(w->dpy, w->clipboard);
  if (owner == w->win) return w->clipboard_text;
  if (owner == None) return {};
  XConvertSelection(w->dpy, w->clipboard, w->utf8, w->prop, w->win, CurrentTime);
  /* Wait briefly for the owner to answer. */
  for (int i = 0; i < 100; i++) {
    XEvent xe;
    if (XCheckTypedWindowEvent(w->dpy, w->win, SelectionNotify, &xe)) {
      handle_event(w, xe);
      return w->clipboard_text;
    }
    timeval tv{0, 2000};
    select(0, nullptr, nullptr, nullptr, &tv);
  }
  return {};
}

void set_clipboard(Window *w, const std::string &utf8) {
  w->clipboard_text = utf8;
  XSetSelectionOwner(w->dpy, w->clipboard, w->win, CurrentTime);
}

void set_refresh_callback(Window *w, std::function<void()> cb) { w->refresh = std::move(cb); }

}  // namespace bl::platform

#endif
