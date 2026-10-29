// SPDX-License-Identifier: GPL-2.0-or-later
// X11 backend (compare blender/intern/ghost/intern/GHOST_SystemX11.cc).
// Only needs libX11, which every X11/XWayland desktop ships.
#if !defined(_WIN32) && !defined(__APPLE__)

#include "platform.h"
#include "../core/pen_axes.h"
#include "../core/touch_gestures.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <sys/select.h>
#include <time.h>

/* Task 0219: fingers and pens through XInput 2 - its header where the system has it (libxi-dev), the library opened at
 * run time, so building needs nothing new and a system without libXi keeps the mouse's events. */
#if defined(__has_include)
#  if __has_include(<X11/extensions/XInput2.h>)
#    include <X11/extensions/XInput2.h>
#    include <dlfcn.h>
#    define BL_X11_XI2 1
#  endif
#endif

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cctype>
#include <cstdio>
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
  /* Task 0219: fingers into the recognizer; a pen's axes by device (the tablets found at the start). */
  TouchRecognizer touch;
  int xi_opcode = -1;
  struct PenDevice {
    int pressure = -1, tilt_x = -1, tilt_y = -1;
    double pmin = 0, pmax = 1, xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool eraser = false;
  };
  std::unordered_map<int, PenDevice> pens;
  float pen_pressure = 1.0f, pen_tilt_x = 0.0f, pen_tilt_y = 0.0f;
  bool pen_eraser = false;
  double pen_seen = -1.0;  // (when a pen's own event last came: the core events just after, where it is, are the pen's)
  double pen_root_x = 0.0, pen_root_y = 0.0;
};

static double mono_seconds() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

/* The recognizer's steps as events (task 0213's model): one finger as the left button tagged Touch, two a Gesture. */
static void push_touch_steps(Window *w, const std::vector<TouchStep> &steps) {
  for (const TouchStep &s : steps) {
    Event e;
    e.x = (int)s.x, e.y = (int)s.y;
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

/* A core pointer event just after a pen's own: the pen's, with its pressure, tilt and eraser; its side buttons (2, 3)
 * as the right button held with the barrel, as Windows sends them. */
static void tag_pen(Window *w, Event &e, unsigned core_button, double root_x, double root_y) {
  if (w->pen_seen < 0.0 || mono_seconds() - w->pen_seen > 0.1) return;
  /* (Only where the pen is: a pen resting or hovering on its tablet keeps sending, and the mouse moved meanwhile is the
   * mouse's.) */
  if (std::abs(root_x - w->pen_root_x) > 1.5 || std::abs(root_y - w->pen_root_y) > 1.5) return;
  e.pointer = Pointer::Pen, e.pen = true;
  e.pressure = e.type == EventType::MouseUp ? 1.0f : w->pen_pressure;
  e.tilt_x = w->pen_tilt_x, e.tilt_y = w->pen_tilt_y, e.eraser = w->pen_eraser;
  if (core_button == 2 || core_button == 3) e.button = 1, e.barrel = true;
}

#ifdef BL_X11_XI2
/* libXi's calls, looked up at run time. */
struct XiApi {
  Status (*query_version)(Display *, int *, int *) = nullptr;
  int (*select_events)(Display *, ::Window, XIEventMask *, int) = nullptr;
  XIDeviceInfo *(*query_device)(Display *, int, int *) = nullptr;
  void (*free_device_info)(XIDeviceInfo *) = nullptr;
  bool ok = false;
};

static XiApi &xi() {
  static XiApi api = [] {
    XiApi a;
    void *h = dlopen("libXi.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!h) return a;
    a.query_version = (Status(*)(Display *, int *, int *))dlsym(h, "XIQueryVersion");
    a.select_events = (int (*)(Display *, ::Window, XIEventMask *, int))dlsym(h, "XISelectEvents");
    a.query_device = (XIDeviceInfo * (*)(Display *, int, int *)) dlsym(h, "XIQueryDevice");
    a.free_device_info = (void (*)(XIDeviceInfo *))dlsym(h, "XIFreeDeviceInfo");
    a.ok = a.query_version && a.select_events && a.query_device && a.free_device_info;
    return a;
  }();
  return api;
}

/* The tablets: slave pointers with a pressure axis, their axes found by their labels. */
static void find_pens(Window *w, std::vector<int> &ids) {
  int n = 0;
  XIDeviceInfo *devs = xi().query_device(w->dpy, XIAllDevices, &n);
  for (int i = 0; devs && i < n; i++) {
    const XIDeviceInfo &d = devs[i];
    if (d.use != XISlavePointer && d.use != XIFloatingSlave) continue;
    bool touch_screen = false;
    for (int c = 0; c < d.num_classes; c++) touch_screen = touch_screen || d.classes[c]->type == XITouchClass;
    if (touch_screen) continue;  // (its contacts come as touches; a pressure axis doesn't make it a pen)
    Window::PenDevice pen;
    for (int c = 0; c < d.num_classes; c++) {
      if (d.classes[c]->type != XIValuatorClass) continue;
      const XIValuatorClassInfo *v = (const XIValuatorClassInfo *)d.classes[c];
      char *name = v->label ? XGetAtomName(w->dpy, v->label) : nullptr;
      const pen::Axis axis = pen::axis_of(name ? name : "");
      if (name) XFree(name);
      if (axis == pen::Axis::Pressure) pen.pressure = v->number, pen.pmin = v->min, pen.pmax = v->max;
      if (axis == pen::Axis::TiltX) pen.tilt_x = v->number, pen.xmin = v->min, pen.xmax = v->max;
      if (axis == pen::Axis::TiltY) pen.tilt_y = v->number, pen.ymin = v->min, pen.ymax = v->max;
    }
    if (pen.pressure < 0) continue;
    pen.eraser = pen::eraser_device(d.name ? d.name : "");
    w->pens[d.deviceid] = pen;
    ids.push_back(d.deviceid);
  }
  if (devs) xi().free_device_info(devs);
}

static int ignore_x_error(Display *, XErrorEvent *) { return 0; }

/* Touches from the master devices (so X makes no pointer events of them for this window), the tablets' own motion for
 * their axes (a slave device's: the master's core events still come, carrying the position and buttons), and the
 * device list's changes (a pen's device added on its first use, a tablet plugged in). */
static void xi_select(Window *w) {
  unsigned char touch_bits[XIMaskLen(XI_LASTEVENT)] = {};
  XISetMask(touch_bits, XI_TouchBegin);
  XISetMask(touch_bits, XI_TouchUpdate);
  XISetMask(touch_bits, XI_TouchEnd);
  unsigned char all_bits[XIMaskLen(XI_LASTEVENT)] = {};
  XISetMask(all_bits, XI_HierarchyChanged);
  std::vector<XIEventMask> masks{{XIAllMasterDevices, (int)sizeof(touch_bits), touch_bits}, {XIAllDevices, (int)sizeof(all_bits), all_bits}};
  std::vector<int> pens;
  w->pens.clear();
  find_pens(w, pens);
  unsigned char pen_bits[XIMaskLen(XI_LASTEVENT)] = {};
  XISetMask(pen_bits, XI_Motion);
  XISetMask(pen_bits, XI_ButtonPress);
  XISetMask(pen_bits, XI_ButtonRelease);
  for (int id : pens) masks.push_back({id, (int)sizeof(pen_bits), pen_bits});
  /* (A device gone between the list and the selection is an X error: ignored, not the default handler's exit.) */
  XSync(w->dpy, False);
  int (*old)(Display *, XErrorEvent *) = XSetErrorHandler(ignore_x_error);
  xi().select_events(w->dpy, w->win, masks.data(), (int)masks.size());
  XSync(w->dpy, False);
  XSetErrorHandler(old);
}

static void xi_setup(Window *w) {
  if (!xi().ok) return;
  int op = 0, ev = 0, err = 0, major = 2, minor = 2;
  if (!XQueryExtension(w->dpy, "XInputExtension", &op, &ev, &err)) return;
  if (xi().query_version(w->dpy, &major, &minor) != Success || major < 2 || (major == 2 && minor < 2)) return;
  w->xi_opcode = op;
  xi_select(w);
}

static void handle_xi(Window *w, const XIDeviceEvent *ev, int type) {
  const double now = mono_seconds();
  if (type == XI_HierarchyChanged) {
    xi_select(w);
    return;
  }
  if (type == XI_TouchBegin || type == XI_TouchUpdate || type == XI_TouchEnd) {
    std::vector<TouchStep> steps;
    const bool palm = w->pen_seen >= 0.0 && now - w->pen_seen < 0.3;  // (a pen in use: a touch is a palm)
    if (palm) w->touch.clear(steps);
    else if (type == XI_TouchBegin) w->touch.down((uint64_t)ev->detail, (float)ev->event_x, (float)ev->event_y, now, steps);
    else if (type == XI_TouchUpdate) w->touch.move((uint64_t)ev->detail, (float)ev->event_x, (float)ev->event_y, now, steps);
    else w->touch.up((uint64_t)ev->detail, (float)ev->event_x, (float)ev->event_y, now, steps);
    push_touch_steps(w, steps);
    return;
  }
  auto it = w->pens.find(ev->sourceid);
  if (it == w->pens.end()) it = w->pens.find(ev->deviceid);
  if (it == w->pens.end()) return;
  const Window::PenDevice &pen = it->second;
  const unsigned char *mask = ev->valuators.mask;
  const int len = ev->valuators.mask_len;
  double v = 0.0;
  if (pen::valuator(mask, len, ev->valuators.values, pen.pressure, v)) w->pen_pressure = pen::unit(v, pen.pmin, pen.pmax);
  if (pen::valuator(mask, len, ev->valuators.values, pen.tilt_x, v)) w->pen_tilt_x = pen::tilt_degrees(v, pen.xmin, pen.xmax);
  if (pen::valuator(mask, len, ev->valuators.values, pen.tilt_y, v)) w->pen_tilt_y = pen::tilt_degrees(v, pen.ymin, pen.ymax);
  w->pen_eraser = pen.eraser;
  w->pen_seen = now;
  w->pen_root_x = ev->root_x, w->pen_root_y = ev->root_y;
}
#endif

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

/* Blendity's own windows (each on its own display connection): a copy in one is pasted in another from here, as
 * the owning connection isn't polled while another window asks it (task 0187). */
static std::vector<Window *> &our_windows() {
  static std::vector<Window *> w;
  return w;
}

/* A window on its own display connection (each window polls its own), at (x, y) with a minimum size. */
static Window *make_window(const char *title, int x, int y, int width, int height, bool scale, int min_w, int min_h) {
  XInitThreads();
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) return nullptr;
  auto *w = new Window();
  w->dpy = dpy;
  w->dpi = query_dpi(dpy);
  int screen = DefaultScreen(dpy);
  w->width = scale ? (int)(width * w->dpi) : width;
  w->height = scale ? (int)(height * w->dpi) : height;
  w->win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), x, y, w->width, w->height, 0, BlackPixel(dpy, screen),
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
  hints.flags = PMinSize | PWinGravity | (x || y ? PPosition | USPosition : 0);
  hints.win_gravity = StaticGravity;  // (the client area where asked: the window manager's frame around it, no creep)
  hints.min_width = min_w;
  hints.min_height = min_h;
  hints.x = x, hints.y = y;
  XSetWMNormalHints(dpy, w->win, &hints);

  w->gc = XCreateGC(dpy, w->win, 0, nullptr);
  XkbSetDetectableAutoRepeat(dpy, True, nullptr);
  w->im = XOpenIM(dpy, nullptr, nullptr, nullptr);
  if (w->im)
    w->ic = XCreateIC(w->im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, w->win,
                      XNFocusWindow, w->win, nullptr);
#ifdef BL_X11_XI2
  xi_setup(w);  // (task 0219)
#endif
  XMapWindow(dpy, w->win);
  if (x || y) XMoveWindow(dpy, w->win, x, y);
  XFlush(dpy);
  our_windows().push_back(w);
  return w;
}

Window *create_window(const char *title, int width, int height) { return make_window(title, 0, 0, width, height, true, 640, 400); }

Window *create_secondary_window(const char *title, int x, int y, int width, int height) {
  return make_window(title, x, y, std::max(240, width), std::max(160, height), false, 240, 160);
}

std::vector<Monitor> monitors() {
  std::vector<Monitor> out;
  Display *dpy = XOpenDisplay(nullptr);
  Monitor m;
  m.primary = true, m.name = "Screen";
  m.width = 1920, m.height = 1080;  // (no display to ask: a common one)
  if (dpy) {
    const int s = DefaultScreen(dpy);
    m.width = DisplayWidth(dpy, s), m.height = DisplayHeight(dpy, s), m.dpi = query_dpi(dpy);
    XCloseDisplay(dpy);
  }
  out.push_back(m);
  return out;
}

void get_window_rect(Window *w, int &x, int &y, int &width, int &height) {
  ::Window child;
  XTranslateCoordinates(w->dpy, w->win, DefaultRootWindow(w->dpy), 0, 0, &x, &y, &child);
  width = w->width, height = w->height;
}

/* The window manager's fullscreen state (EWMH _NET_WM_STATE_FULLSCREEN), asked for with a client message. */
void set_fullscreen(Window *w, bool on) {
  XEvent e{};
  e.xclient.type = ClientMessage;
  e.xclient.window = w->win;
  e.xclient.message_type = XInternAtom(w->dpy, "_NET_WM_STATE", False);
  e.xclient.format = 32;
  e.xclient.data.l[0] = on ? 1 : 0;  // (_NET_WM_STATE_ADD / _REMOVE)
  e.xclient.data.l[1] = (long)XInternAtom(w->dpy, "_NET_WM_STATE_FULLSCREEN", False);
  e.xclient.data.l[3] = 1;
  XSendEvent(w->dpy, DefaultRootWindow(w->dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
  XFlush(w->dpy);
}

void set_window_rect(Window *w, int x, int y, int width, int height) {
  XMoveResizeWindow(w->dpy, w->win, x, y, (unsigned)std::max(1, width), (unsigned)std::max(1, height));
  XFlush(w->dpy);
}

void destroy_window(Window *w) {
  if (!w) return;
  auto &ours = our_windows();
  ours.erase(std::remove(ours.begin(), ours.end(), w), ours.end());
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
      tag_pen(w, e, 0, xe.xmotion.x_root, xe.xmotion.y_root);
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
        tag_pen(w, e, b, xe.xbutton.x_root, xe.xbutton.y_root);
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

void poll_events(Window *w, std::vector<Event> &out, int timeout_ms) { poll_events_for(w, out, timeout_ms / 1000.0); }

void poll_events_for(Window *w, std::vector<Event> &out, double seconds) {
  if (w->touch.holding()) seconds = std::min(seconds, 0.02);  // (a finger's held press is let through on time)
  if (XPending(w->dpy) == 0 && w->queue.empty() && seconds > 0.0) {
    int fd = ConnectionNumber(w->dpy);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    const long us = (long)(seconds * 1e6);
    timeval tv{us / 1000000, us % 1000000};
    select(fd + 1, &fds, nullptr, nullptr, &tv);
  }
  while (XPending(w->dpy)) {
    XEvent xe;
    XNextEvent(w->dpy, &xe);
    if (XFilterEvent(&xe, None)) continue;
#ifdef BL_X11_XI2
    if (xe.xcookie.type == GenericEvent && xe.xcookie.extension == w->xi_opcode && XGetEventData(w->dpy, &xe.xcookie)) {
      handle_xi(w, (const XIDeviceEvent *)xe.xcookie.data, xe.xcookie.evtype);
      XFreeEventData(w->dpy, &xe.xcookie);
      continue;
    }
#endif
    handle_event(w, xe);
  }
  std::vector<TouchStep> held;
  w->touch.poll(mono_seconds(), held);
  push_touch_steps(w, held);
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

void present_rows(Window *w, const uint32_t *pixels, int width, int height, const std::vector<std::pair<int, int>> &rows) {
  if (!w->image || w->img_w != width || w->img_h != height) return present(w, pixels, width, height);
  for (auto [y0, y1] : rows) {
    y0 = std::max(0, y0), y1 = std::min(height, y1);
    if (y1 <= y0) continue;
    std::memcpy(w->image_pixels.data() + (size_t)y0 * width, pixels + (size_t)y0 * width, (size_t)width * (y1 - y0) * 4);
    XPutImage(w->dpy, w->win, w->gc, w->image, 0, y0, 0, y0, width, y1 - y0);
  }
  XFlush(w->dpy);
}

void *native_handle(Window *w) { return w ? (void *)(uintptr_t)w->win : nullptr; }  // (task 0193)
void *native_display(Window *w) { return w ? (void *)w->dpy : nullptr; }

void get_framebuffer_size(Window *w, int &width, int &height) {
  width = w->width;
  height = w->height;
}

float dpi_scale(Window *w) { return w->dpi; }

bool tablet_posture() { return false; }  // (task 0218: X11 doesn't say)

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
  if (owner == None) return {};
  for (Window *o : our_windows())
    if (o->win == owner) return o->clipboard_text;  // (this window's or another of ours)
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

/* ------------------------------------------------------------ File dialogs */
/* X11 has no dialog of its own: zenity (GTK) or kdialog (KDE) when installed. */

static bool have_tool(const char *name) {
  std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

static std::string sh_quote(const std::string &s) {
  std::string r = "'";
  for (char c : s) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return r + "'";
}

static bool run_dialog_command(const std::string &cmd, std::string &out) {
  FILE *p = popen(cmd.c_str(), "r");
  if (!p) return false;
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) s.append(buf, n);
  const int status = pclose(p);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  if (status != 0 || s.empty()) return false;
  out = s;
  return true;
}

bool file_dialogs_available() { return have_tool("zenity") || have_tool("kdialog"); }

/* A folder (task 0271): zenity's or kdialog's directory chooser. */
bool folder_dialog(Window *, const std::string &title, const std::string &initial_dir, std::string &out_path) {
  std::string cmd;
  if (have_tool("zenity")) cmd = "zenity --file-selection --directory --title=" + sh_quote(title) + " --filename=" + sh_quote(initial_dir.empty() ? std::string("./") : initial_dir + "/");
  else if (have_tool("kdialog")) cmd = "kdialog --title " + sh_quote(title) + " --getexistingdirectory " + sh_quote(initial_dir.empty() ? std::string(".") : initial_dir);
  else return false;
  return run_dialog_command(cmd + " 2>/dev/null", out_path);
}

static bool x11_file_dialog(bool save, const std::string &title, const std::string &initial, const std::vector<FileFilter> &filters,
                            std::string &out, int *filter_index) {
  std::string cmd;
  if (have_tool("zenity")) {
    cmd = "zenity --file-selection --title=" + sh_quote(title);
    if (save) cmd += " --save --confirm-overwrite";
    cmd += " --filename=" + sh_quote(initial);
    const int start = filter_index ? *filter_index : 0;
    /* zenity shows the first filter first. */
    for (size_t k = 0; k < filters.size(); k++) {
      const FileFilter &f = filters[(k + (size_t)std::max(0, start)) % filters.size()];
      std::string pat;
      for (const std::string &e : f.extensions) pat += " *" + e;
      cmd += " --file-filter=" + sh_quote(f.name + " |" + pat);
    }
  }
  else if (have_tool("kdialog")) {
    std::string pat;
    for (const FileFilter &f : filters) {
      std::string p;
      for (const std::string &e : f.extensions) p += (p.empty() ? "*" : " *") + e;
      pat += (pat.empty() ? "" : "\n") + p + "|" + f.name;
    }
    cmd = std::string("kdialog --title ") + sh_quote(title) + (save ? " --getsavefilename " : " --getopenfilename ") + sh_quote(initial) + " " +
          sh_quote(pat);
  }
  else
    return false;
  cmd += " 2>/dev/null";
  if (!run_dialog_command(cmd, out)) return false;
  /* The filter is whichever one the extension belongs to; none typed: the first. */
  size_t dot = out.find_last_of('.'), slash = out.find_last_of('/');
  std::string ext = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? out.substr(dot) : "";
  for (char &c : ext) c = (char)std::tolower((unsigned char)c);
  int chosen = filter_index ? *filter_index : 0;
  bool matched = false;
  for (size_t k = 0; k < filters.size() && !matched; k++)
    for (const std::string &e : filters[k].extensions)
      if (e == ext) {
        chosen = (int)k;
        matched = true;
      }
  if (save && !matched && !filters.empty() && chosen >= 0 && chosen < (int)filters.size() && !filters[(size_t)chosen].extensions.empty())
    out += filters[(size_t)chosen].extensions[0];
  if (filter_index) *filter_index = chosen;
  return true;
}

bool save_file_dialog(Window *, const std::string &title, const std::string &initial_path, const std::vector<FileFilter> &filters,
                      std::string &out_path, int *filter_index) {
  return x11_file_dialog(true, title, initial_path, filters, out_path, filter_index);
}

bool open_file_dialog(Window *, const std::string &title, const std::string &initial_dir, const std::vector<FileFilter> &filters,
                      std::string &out_path) {
  std::string dir = initial_dir;
  if (!dir.empty() && dir.back() != '/') dir += '/';
  return x11_file_dialog(false, title, dir, filters, out_path, nullptr);
}

}  // namespace bl::platform

#endif
