// SPDX-License-Identifier: GPL-2.0-or-later
// macOS backend (compare blender/intern/ghost/intern/GHOST_SystemCocoa.mm).
// Uses only system frameworks: Cocoa + QuartzCore. Compile with -fobjc-arc.
#ifdef __APPLE__

#include "platform.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cstring>

namespace bl::platform {
struct Window;
}

@interface BLView : NSView
@property(nonatomic, assign) bl::platform::Window *owner;
@end

@interface BLWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) bl::platform::Window *owner;
@end

namespace bl::platform {

struct Window {
  NSWindow *window = nil;
  BLView *view = nil;
  BLWindowDelegate *delegate = nil;
  std::vector<Event> queue;
  std::function<void()> refresh;
  NSUInteger last_flags = 0;
  Cursor current = Cursor::Arrow;
};

void append_utf8(std::string &s, uint32_t cp) {
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
}

static int mods_from_flags(NSUInteger f) {
  int m = 0;
  if (f & NSEventModifierFlagShift) m |= MOD_SHIFT;
  if (f & NSEventModifierFlagCommand) m |= MOD_CTRL;  // Cmd acts as Ctrl, like Unity on macOS
  if (f & NSEventModifierFlagOption) m |= MOD_ALT;
  if (f & NSEventModifierFlagControl) m |= MOD_SUPER;
  return m;
}

static int map_keycode(unsigned short kc) {
  static const int letters[] = {
      /*0x00*/ KEY_A, KEY_S, KEY_D, KEY_F, KEY_H, KEY_G, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_NONE, KEY_B, KEY_Q, KEY_W,
      KEY_E, KEY_R,
      /*0x10*/ KEY_Y, KEY_T, KEY_1, KEY_2, KEY_3, KEY_4, KEY_6, KEY_5, KEY_EQUALS, KEY_9, KEY_7, KEY_MINUS, KEY_8,
      KEY_0, KEY_RBRACKET, KEY_O,
      /*0x20*/ KEY_U, KEY_LBRACKET, KEY_I, KEY_P, KEY_ENTER, KEY_L, KEY_J, KEY_APOSTROPHE, KEY_K, KEY_SEMICOLON,
      KEY_BACKSLASH, KEY_COMMA, KEY_SLASH, KEY_N, KEY_M, KEY_PERIOD,
      /*0x30*/ KEY_TAB, KEY_SPACE, KEY_GRAVE, KEY_BACKSPACE, KEY_NONE, KEY_ESCAPE, KEY_NONE, KEY_SUPER, KEY_SHIFT,
      KEY_NONE, KEY_ALT, KEY_CTRL, KEY_SHIFT, KEY_ALT, KEY_CTRL, KEY_NONE};
  if (kc < sizeof(letters) / sizeof(letters[0])) {
    int k = letters[kc];
    /* Command is our Ctrl; Control is Super (see mods_from_flags). */
    if (kc == 0x37) return KEY_CTRL;
    if (kc == 0x3B || kc == 0x3E) return KEY_SUPER;
    return k;
  }
  switch (kc) {
    case 0x4C: return KEY_ENTER;
    case 0x60: return KEY_F5;
    case 0x61: return KEY_F6;
    case 0x62: return KEY_F7;
    case 0x63: return KEY_F3;
    case 0x64: return KEY_F8;
    case 0x65: return KEY_F9;
    case 0x67: return KEY_F11;
    case 0x6D: return KEY_F10;
    case 0x6F: return KEY_F12;
    case 0x73: return KEY_HOME;
    case 0x74: return KEY_PAGE_UP;
    case 0x75: return KEY_DELETE;
    case 0x76: return KEY_F4;
    case 0x77: return KEY_END;
    case 0x78: return KEY_F2;
    case 0x79: return KEY_PAGE_DOWN;
    case 0x7A: return KEY_F1;
    case 0x7B: return KEY_LEFT;
    case 0x7C: return KEY_RIGHT;
    case 0x7D: return KEY_DOWN;
    case 0x7E: return KEY_UP;
  }
  return KEY_NONE;
}

static void push(Window *w, Event e) { w->queue.push_back(std::move(e)); }

}  // namespace bl::platform

using namespace bl::platform;

@implementation BLView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer {}  // contents are set directly in present()

- (void)mouseEvent:(NSEvent *)e type:(EventType)t button:(int)b {
  NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
  CGFloat s = [[self window] backingScaleFactor];
  Event ev;
  ev.type = t;
  ev.button = b;
  ev.x = (int)(p.x * s);
  ev.y = (int)(p.y * s);
  ev.mods = mods_from_flags([e modifierFlags]);
  push(self.owner, ev);
}
- (void)mouseDown:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseDown button:0]; }
- (void)mouseUp:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseUp button:0]; }
- (void)rightMouseDown:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseDown button:1]; }
- (void)rightMouseUp:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseUp button:1]; }
- (void)otherMouseDown:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseDown button:2]; }
- (void)otherMouseUp:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseUp button:2]; }
- (void)mouseMoved:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseMove button:0]; }
- (void)mouseDragged:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseMove button:0]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseMove button:1]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseEvent:e type:EventType::MouseMove button:2]; }

- (void)scrollWheel:(NSEvent *)e {
  NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
  CGFloat s = [[self window] backingScaleFactor];
  Event ev;
  ev.type = EventType::Wheel;
  ev.x = (int)(p.x * s);
  ev.y = (int)(p.y * s);
  float k = [e hasPreciseScrollingDeltas] ? 0.1f : 1.0f;
  ev.wheel_x = (float)[e scrollingDeltaX] * k;
  ev.wheel_y = (float)[e scrollingDeltaY] * k;
  ev.mods = mods_from_flags([e modifierFlags]);
  push(self.owner, ev);
}

- (void)keyDown:(NSEvent *)e {
  Event ev;
  ev.type = EventType::KeyDown;
  ev.key = map_keycode([e keyCode]);
  ev.mods = mods_from_flags([e modifierFlags]);
  ev.repeat = [e isARepeat];
  if (ev.key != KEY_NONE) push(self.owner, ev);
  if (ev.mods & MOD_CTRL) return;
  NSString *chars = [e characters];
  for (NSUInteger i = 0; i < [chars length]; i++) {
    unichar c = [chars characterAtIndex:i];
    if (c < 32 || c == 127 || (c >= 0xF700 && c <= 0xF8FF)) continue;  // control & function keys
    Event t;
    t.type = EventType::Text;
    t.codepoint = c;
    push(self.owner, t);
  }
}
- (void)keyUp:(NSEvent *)e {
  Event ev;
  ev.type = EventType::KeyUp;
  ev.key = map_keycode([e keyCode]);
  ev.mods = mods_from_flags([e modifierFlags]);
  if (ev.key != KEY_NONE) push(self.owner, ev);
}
- (void)flagsChanged:(NSEvent *)e {
  NSUInteger now = [e modifierFlags];
  NSUInteger before = self.owner->last_flags;
  struct { NSUInteger flag; int key; } map[] = {{NSEventModifierFlagShift, KEY_SHIFT},
                                                {NSEventModifierFlagCommand, KEY_CTRL},
                                                {NSEventModifierFlagOption, KEY_ALT},
                                                {NSEventModifierFlagControl, KEY_SUPER}};
  for (auto &m : map) {
    bool was = before & m.flag, is = now & m.flag;
    if (was == is) continue;
    Event ev;
    ev.type = is ? EventType::KeyDown : EventType::KeyUp;
    ev.key = m.key;
    ev.mods = mods_from_flags(now);
    push(self.owner, ev);
  }
  self.owner->last_flags = now;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSPasteboard *pb = [sender draggingPasteboard];
  NSArray *urls = [pb readObjectsForClasses:@[ [NSURL class] ]
                                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  Event ev;
  ev.type = EventType::Drop;
  NSPoint p = [self convertPoint:[sender draggingLocation] fromView:nil];
  CGFloat s = [[self window] backingScaleFactor];
  ev.x = (int)(p.x * s);
  ev.y = (int)(p.y * s);
  for (NSURL *u in urls) ev.paths.push_back([[u path] UTF8String]);
  if (!ev.paths.empty()) push(self.owner, ev);
  return YES;
}
@end

@implementation BLWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
  push(self.owner, {EventType::Quit});
  return NO;
}
- (void)windowDidResize:(NSNotification *)n {
  Event e;
  e.type = EventType::Resize;
  push(self.owner, e);
  if (self.owner->refresh) self.owner->refresh();
}
- (void)windowDidChangeBackingProperties:(NSNotification *)n { push(self.owner, {EventType::DpiChanged}); }
- (void)windowDidResignKey:(NSNotification *)n { push(self.owner, {EventType::FocusLost}); }
@end

namespace bl::platform {

Window *create_window(const char *title, int width, int height) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    /* Minimal app menu so Cmd+Q works; it routes through windowShouldClose. */
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    NSMenu *app_menu = [[NSMenu alloc] init];
    [app_menu addItemWithTitle:@"Quit Blendity" action:@selector(performClose:) keyEquivalent:@"q"];
    [app_item setSubmenu:app_menu];
    [NSApp setMainMenu:bar];
    [NSApp finishLaunching];

    auto *w = new Window();
    NSRect frame = NSMakeRect(0, 0, width, height);
    w->window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    w->view = [[BLView alloc] initWithFrame:frame];
    w->view.owner = w;
    [w->view setWantsLayer:YES];
    w->view.layer.contentsGravity = kCAGravityResize;
    w->view.layer.magnificationFilter = kCAFilterNearest;
    [w->view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    w->delegate = [[BLWindowDelegate alloc] init];
    w->delegate.owner = w;
    [w->window setReleasedWhenClosed:NO];  // ARC owns the window
    [w->window setDelegate:w->delegate];
    [w->window setContentView:w->view];
    [w->window setTitle:[NSString stringWithUTF8String:title]];
    [w->window setAcceptsMouseMovedEvents:YES];
    [w->window setMinSize:NSMakeSize(640, 400)];
    [w->window center];
    [w->window zoom:nil];
    [w->window makeKeyAndOrderFront:nil];
    [w->window makeFirstResponder:w->view];
    [NSApp activateIgnoringOtherApps:YES];
    return w;
  }
}

void destroy_window(Window *w) {
  if (!w) return;
  [w->window setDelegate:nil];
  [w->window close];
  delete w;
}

void poll_events(Window *w, std::vector<Event> &out, int timeout_ms) {
  @autoreleasepool {
    NSDate *until = (timeout_ms > 0 && w->queue.empty()) ? [NSDate dateWithTimeIntervalSinceNow:timeout_ms / 1000.0]
                                                         : [NSDate distantPast];
    for (;;) {
      NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
      if (!e) break;
      [NSApp sendEvent:e];
      until = [NSDate distantPast];
    }
  }
  for (auto &e : w->queue) out.push_back(std::move(e));
  w->queue.clear();
}

void present(Window *w, const uint32_t *pixels, int width, int height) {
  @autoreleasepool {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate((void *)pixels, width, height, 8, (size_t)width * 4, cs,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;  // copies pixels
    if (img) {
      [CATransaction begin];
      [CATransaction setDisableActions:YES];
      w->view.layer.contents = (__bridge id)img;
      [CATransaction commit];
      CGImageRelease(img);
    }
    if (ctx) CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
  }
}

void get_framebuffer_size(Window *w, int &width, int &height) {
  NSRect b = [w->view bounds];
  CGFloat s = [w->window backingScaleFactor];
  width = (int)(b.size.width * s);
  height = (int)(b.size.height * s);
}

float dpi_scale(Window *w) { return (float)[w->window backingScaleFactor]; }

void set_title(Window *w, const std::string &title) {
  [w->window setTitle:[NSString stringWithUTF8String:title.c_str()]];
}

void set_cursor(Window *w, Cursor c) {
  if (c == w->current) return;
  w->current = c;
  switch (c) {
    case Cursor::Arrow: [[NSCursor arrowCursor] set]; break;
    case Cursor::IBeam: [[NSCursor IBeamCursor] set]; break;
    case Cursor::ResizeH: [[NSCursor resizeLeftRightCursor] set]; break;
    case Cursor::ResizeV: [[NSCursor resizeUpDownCursor] set]; break;
    case Cursor::Move: [[NSCursor openHandCursor] set]; break;
    case Cursor::Hand: [[NSCursor pointingHandCursor] set]; break;
  }
}

std::string get_clipboard(Window *) {
  NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  return s ? std::string([s UTF8String]) : std::string();
}

void set_clipboard(Window *, const std::string &utf8) {
  NSPasteboard *pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  [pb setString:[NSString stringWithUTF8String:utf8.c_str()] forType:NSPasteboardTypeString];
}

void set_refresh_callback(Window *w, std::function<void()> cb) { w->refresh = std::move(cb); }

/* ------------------------------------------------------------ File dialogs */

bool file_dialogs_available() { return true; }

static NSArray *dialog_types(const std::vector<FileFilter> &filters) {
  NSMutableArray *types = [NSMutableArray array];
  for (const FileFilter &f : filters)
    for (const std::string &e : f.extensions) [types addObject:[NSString stringWithUTF8String:e.c_str() + 1]];
  return types;
}

bool save_file_dialog(Window *, const std::string &title, const std::string &initial_path, const std::vector<FileFilter> &filters,
                      std::string &out_path, int *filter_index) {
  @autoreleasepool {
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = [NSString stringWithUTF8String:title.c_str()];
    std::string dir = initial_path, name;
    size_t slash = dir.find_last_of('/');
    if (slash != std::string::npos) {
      name = dir.substr(slash + 1);
      dir = dir.substr(0, slash);
      panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir.c_str()]];
    }
    else
      name = dir;
    panel.nameFieldStringValue = [NSString stringWithUTF8String:name.c_str()];
    /* NSSavePanel has no filter popup of its own: the chosen filter's types. */
    const int k = filter_index ? std::max(0, std::min(*filter_index, (int)filters.size() - 1)) : 0;
    if (!filters.empty()) {
      std::vector<FileFilter> one(1, filters[(size_t)k]);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      panel.allowedFileTypes = dialog_types(one);
#pragma clang diagnostic pop
    }
    if ([panel runModal] != NSModalResponseOK) return false;
    out_path = [[panel.URL path] UTF8String];
    if (filter_index) *filter_index = k;
    return true;
  }
}

bool open_file_dialog(Window *, const std::string &title, const std::string &initial_dir, const std::vector<FileFilter> &filters,
                      std::string &out_path) {
  @autoreleasepool {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = [NSString stringWithUTF8String:title.c_str()];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (!initial_dir.empty()) panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:initial_dir.c_str()]];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (!filters.empty()) panel.allowedFileTypes = dialog_types(filters);
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return false;
    out_path = [[panel.URL path] UTF8String];
    return true;
  }
}

}  // namespace bl::platform

#endif  // __APPLE__
