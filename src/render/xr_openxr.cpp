// SPDX-License-Identifier: GPL-2.0-or-later
// The OpenXR runtime behind xr::Runtime (round 383, task 0360): openxr_loader.dll loaded at run time (no link-time
// dependency: a build without it, or a PC without it, leaves VR off and says why), a Direct3D 11 device on the
// headset's adapter, a stereo session in the stage space (the floor's), and each eye's pixels uploaded into its
// swapchain image. Blender's intern/ghost/intern/GHOST_XrContext.cc / GHOST_XrSession.cc are the reference.
#if defined(BL_WITH_OPENXR) && defined(_WIN32)

#include "xr.h"

#include "../core/core.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace bl::xr {

namespace {

using PFN_D3D11CreateDevice_ = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                                 D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
using PFN_CreateDXGIFactory1_ = HRESULT(WINAPI *)(REFIID, void **);

template <class T>
void release(T *&p) {
  if (p) p->Release();
  p = nullptr;
}

/* The OpenXR functions a session uses, from the loader's xrGetInstanceProcAddr. */
struct Fns {
  PFN_xrGetInstanceProcAddr gipa = nullptr;
  PFN_xrCreateInstance CreateInstance = nullptr;
  PFN_xrDestroyInstance DestroyInstance = nullptr;
  PFN_xrGetInstanceProperties GetInstanceProperties = nullptr;
  PFN_xrResultToString ResultToString = nullptr;
  PFN_xrGetSystem GetSystem = nullptr;
  PFN_xrGetD3D11GraphicsRequirementsKHR GetD3D11Requirements = nullptr;
  PFN_xrCreateSession CreateSession = nullptr;
  PFN_xrDestroySession DestroySession = nullptr;
  PFN_xrEnumerateViewConfigurationViews EnumerateViews = nullptr;
  PFN_xrEnumerateReferenceSpaces EnumerateSpaces = nullptr;
  PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
  PFN_xrDestroySpace DestroySpace = nullptr;
  PFN_xrEnumerateSwapchainFormats EnumerateFormats = nullptr;
  PFN_xrCreateSwapchain CreateSwapchain = nullptr;
  PFN_xrDestroySwapchain DestroySwapchain = nullptr;
  PFN_xrEnumerateSwapchainImages EnumerateImages = nullptr;
  PFN_xrAcquireSwapchainImage AcquireImage = nullptr;
  PFN_xrWaitSwapchainImage WaitImage = nullptr;
  PFN_xrReleaseSwapchainImage ReleaseImage = nullptr;
  PFN_xrPollEvent PollEvent = nullptr;
  PFN_xrBeginSession BeginSession = nullptr;
  PFN_xrEndSession EndSession = nullptr;
  PFN_xrWaitFrame WaitFrame = nullptr;
  PFN_xrBeginFrame BeginFrame = nullptr;
  PFN_xrEndFrame EndFrame = nullptr;
  PFN_xrLocateViews LocateViews = nullptr;
  /* Round 384: the controllers. */
  PFN_xrStringToPath StringToPath = nullptr;
  PFN_xrCreateActionSet CreateActionSet = nullptr;
  PFN_xrCreateAction CreateAction = nullptr;
  PFN_xrSuggestInteractionProfileBindings SuggestBindings = nullptr;
  PFN_xrAttachSessionActionSets AttachActionSets = nullptr;
  PFN_xrSyncActions SyncActions = nullptr;
  PFN_xrGetActionStateFloat GetFloat = nullptr;
  PFN_xrGetActionStateBoolean GetBoolean = nullptr;
  PFN_xrGetActionStateVector2f GetVector2f = nullptr;
  PFN_xrCreateActionSpace CreateActionSpace = nullptr;
  PFN_xrLocateSpace LocateSpace = nullptr;
};

/* Round 384: the controllers' bindings, per interaction profile - (action, path under each hand). A profile the runtime
 * doesn't know is refused and skipped. */
struct Binding {
  int action;  // Act
  const char *path;
  int hands = 3;  // 1: the left only, 2: the right only, 3: both (Touch's A is on the right, X on the left - review 384)
};
enum Act { kAim, kGrip, kTrigger, kSqueeze, kStick, kPrimary, kActs };
struct Profile {
  const char *path;
  std::vector<Binding> bindings;
};
const std::vector<Profile> &profiles() {
  static const std::vector<Profile> p = {
      {"/interaction_profiles/khr/simple_controller", {{kAim, "input/aim/pose"}, {kGrip, "input/grip/pose"}, {kTrigger, "input/select/click"}, {kPrimary, "input/menu/click"}}},
      {"/interaction_profiles/oculus/touch_controller",
       {{kAim, "input/aim/pose"}, {kGrip, "input/grip/pose"}, {kTrigger, "input/trigger/value"}, {kSqueeze, "input/squeeze/value"}, {kStick, "input/thumbstick"},
        {kPrimary, "input/x/click", 1}, {kPrimary, "input/a/click", 2}}},
      {"/interaction_profiles/valve/index_controller",
       {{kAim, "input/aim/pose"}, {kGrip, "input/grip/pose"}, {kTrigger, "input/trigger/value"}, {kSqueeze, "input/squeeze/value"}, {kStick, "input/thumbstick"},
        {kPrimary, "input/a/click"}}},
      {"/interaction_profiles/htc/vive_controller",
       {{kAim, "input/aim/pose"}, {kGrip, "input/grip/pose"}, {kTrigger, "input/trigger/value"}, {kSqueeze, "input/squeeze/click"}, {kStick, "input/trackpad"},
        {kPrimary, "input/menu/click"}}},
      {"/interaction_profiles/microsoft/motion_controller",
       {{kAim, "input/aim/pose"}, {kGrip, "input/grip/pose"}, {kTrigger, "input/trigger/value"}, {kSqueeze, "input/squeeze/click"}, {kStick, "input/thumbstick"},
        {kPrimary, "input/menu/click"}}},
  };
  return p;
}

template <class F>
bool get(PFN_xrGetInstanceProcAddr gipa, XrInstance inst, const char *name, F &out) {
  PFN_xrVoidFunction f = nullptr;
  if (XR_FAILED(gipa(inst, name, &f)) || !f) return false;
  out = reinterpret_cast<F>(f);
  return true;
}

struct EyeChain {
  XrSwapchain chain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageD3D11KHR> images;
  int width = 0, height = 0;
};

class OpenXR final : public Runtime {
 public:
  ~OpenXR() override {
    destroy();
    if (loader_) FreeLibrary(loader_);
    if (d3d_) FreeLibrary(d3d_);
    if (dxgi_) FreeLibrary(dxgi_);
  }

  bool load(std::string &why) {
    loader_ = LoadLibraryExA("openxr_loader.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);  // (beside Blendity only: never one planted elsewhere - review 383)
    if (!loader_) return fail(why, "no openxr_loader.dll beside Blendity (the build with the OpenXR SDK copies it there)");
    f_.gipa = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader_, "xrGetInstanceProcAddr"));
    if (!f_.gipa || !get(f_.gipa, XR_NULL_HANDLE, "xrCreateInstance", f_.CreateInstance)) return fail(why, "openxr_loader.dll has no xrGetInstanceProcAddr");
    return true;
  }

  bool create(float scale, std::string &why) override {
    if (!instance(why) || !system(why) || !device(why) || !session(why) || !space(why)) {
      destroy();
      return false;
    }
    actions();  // (without them VR still shows the scene, only without hands)
    if (!swapchains(scale, why)) {
      destroy();
      return false;
    }
    return true;
  }

  Event poll_event() override {
    for (;;) {
      XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
      if (f_.PollEvent(inst_, &ev) != XR_SUCCESS) return Event::None;
      if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) return Event::LossPending;
      if (ev.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
      switch (reinterpret_cast<XrEventDataSessionStateChanged &>(ev).state) {
        case XR_SESSION_STATE_READY: return Event::Ready;
        case XR_SESSION_STATE_STOPPING: return Event::Stopping;
        case XR_SESSION_STATE_EXITING: return Event::Exiting;
        case XR_SESSION_STATE_LOSS_PENDING: return Event::LossPending;
        case XR_SESSION_STATE_FOCUSED: return Event::Focused;
        case XR_SESSION_STATE_VISIBLE: return Event::Visible;
        default: continue;
      }
    }
  }

  bool begin_session(std::string &why) override {
    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    return check(f_.BeginSession(session_, &bi), "xrBeginSession", why);
  }

  void end_session() override {
    if (session_ != XR_NULL_HANDLE) f_.EndSession(session_);
  }

  void destroy() override {
    for (XrSpace &h : hand_spaces_)
      if (h != XR_NULL_HANDLE) f_.DestroySpace(h), h = XR_NULL_HANDLE;
    hands_ready_ = false;
    for (EyeChain &e : chains_)
      if (e.chain != XR_NULL_HANDLE) f_.DestroySwapchain(e.chain);
    chains_.clear();
    if (space_ != XR_NULL_HANDLE) f_.DestroySpace(space_);
    if (session_ != XR_NULL_HANDLE) f_.DestroySession(session_);
    if (inst_ != XR_NULL_HANDLE && f_.DestroyInstance) f_.DestroyInstance(inst_);
    space_ = XR_NULL_HANDLE, session_ = XR_NULL_HANDLE, inst_ = XR_NULL_HANDLE;
    release(ctx_);
    release(dev_);
  }

  bool wait_frame(bool &should_render) override {
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(f_.WaitFrame(session_, nullptr, &fs))) return false;
    time_ = fs.predictedDisplayTime;
    should_render = fs.shouldRender == XR_TRUE;
    return true;
  }

  bool begin_frame() override { return XR_SUCCEEDED(f_.BeginFrame(session_, nullptr)); }

  bool locate_eyes(std::vector<Eye> &eyes) override {
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = time_;
    li.space = space_;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t n = 0;
    views_.assign(chains_.size(), {XR_TYPE_VIEW});
    if (XR_FAILED(f_.LocateViews(session_, &li, &vs, (uint32_t)views_.size(), &n, views_.data())) || n != chains_.size()) return false;
    if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return false;
    const bool placed = vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT;  // (tracking lost a moment: the last place kept, not a black frame - review 383)
    if (placed) last_positions_.assign(n, XrVector3f{});
    eyes.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      XrView &v = views_[i];
      if (placed) last_positions_[i] = v.pose.position;
      if (!placed && i < last_positions_.size()) v.pose.position = last_positions_[i];
      eyes[i].pose = {Quat(v.pose.orientation.x, v.pose.orientation.y, v.pose.orientation.z, v.pose.orientation.w),
                      Vec3(v.pose.position.x, v.pose.position.y, v.pose.position.z)};
      eyes[i].fov = {v.fov.angleLeft, v.fov.angleRight, v.fov.angleUp, v.fov.angleDown};
      eyes[i].width = chains_[i].width, eyes[i].height = chains_[i].height;
    }
    return true;
  }

  bool submit_eye(int i, const uint32_t *pixels, int width, int height) override {
    if (i < 0 || i >= (int)chains_.size() || width != chains_[(size_t)i].width || height != chains_[(size_t)i].height) return false;
    EyeChain &e = chains_[(size_t)i];
    uint32_t index = 0;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(f_.AcquireImage(e.chain, nullptr, &index))) return false;
    const bool ready = XR_SUCCEEDED(f_.WaitImage(e.chain, &wi));
    if (ready) upload(e.images[index].texture, pixels, width, height);
    f_.ReleaseImage(e.chain, nullptr);  // (released either way: an image held would run the chain dry - review 383)
    return ready;
  }

  bool end_frame(bool rendered) override {
    std::vector<XrCompositionLayerProjectionView> pv(chains_.size(), {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
    for (size_t i = 0; i < pv.size() && i < views_.size(); i++) {
      pv[i].pose = views_[i].pose, pv[i].fov = views_[i].fov;
      pv[i].subImage.swapchain = chains_[i].chain;
      pv[i].subImage.imageRect = {{0, 0}, {chains_[i].width, chains_[i].height}};
    }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = space_;
    layer.viewCount = (uint32_t)pv.size();
    layer.views = pv.data();
    const XrCompositionLayerBaseHeader *layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = time_;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = rendered && views_.size() == chains_.size() ? 1 : 0;
    ei.layers = layers;
    return XR_SUCCEEDED(f_.EndFrame(session_, &ei));
  }

  std::string name() const override { return name_; }

  /* Round 384: the controllers' input read for this frame, and where each hand's aim and grip are at its display time. */
  bool locate_hands(Hand hands[2]) override {
    if (!hands_ready_) return false;
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (XR_FAILED(f_.SyncActions(session_, &si))) return false;
    bool any = false;
    for (int i = 0; i < 2; i++) any = hand(i, hands[i]) || any;
    return any;
  }
  float standing_lift() const override { return local_ ? 1.6f : 0.0f; }

 private:
  static bool fail(std::string &why, const std::string &what) {
    why = what;
    return false;
  }

  bool check(XrResult r, const char *what, std::string &why) const {
    if (XR_SUCCEEDED(r)) return true;
    char text[XR_MAX_RESULT_STRING_SIZE] = "";
    if (f_.ResultToString && inst_ != XR_NULL_HANDLE) f_.ResultToString(inst_, r, text);
    why = std::string(what) + " failed: " + (text[0] ? text : std::to_string((int)r));
    return false;
  }

  /* Round 384: an action set of the hands' aim and grip poses, trigger, squeeze, stick and primary button, its bindings
   * suggested for each known controller, attached to the session; each hand's poses as spaces. Without it VR still
   * shows the scene, only without hands. */
  void actions() {
    if (!hands_ok_) return;
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(si.actionSetName, "blendity", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(si.localizedActionSetName, "Blendity", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_FAILED(f_.CreateActionSet(inst_, &si, &set_))) return;
    f_.StringToPath(inst_, "/user/hand/left", &hand_paths_[0]);
    f_.StringToPath(inst_, "/user/hand/right", &hand_paths_[1]);
    const struct {
      const char *name;
      XrActionType type;
    } defs[kActs] = {{"aim", XR_ACTION_TYPE_POSE_INPUT},       {"grip", XR_ACTION_TYPE_POSE_INPUT},     {"trigger", XR_ACTION_TYPE_FLOAT_INPUT},
                     {"squeeze", XR_ACTION_TYPE_FLOAT_INPUT}, {"stick", XR_ACTION_TYPE_VECTOR2F_INPUT}, {"primary", XR_ACTION_TYPE_BOOLEAN_INPUT}};
    for (int a = 0; a < kActs; a++) {
      XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
      std::strncpy(ci.actionName, defs[a].name, XR_MAX_ACTION_NAME_SIZE - 1);
      std::strncpy(ci.localizedActionName, defs[a].name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
      ci.actionType = defs[a].type;
      ci.countSubactionPaths = 2;
      ci.subactionPaths = hand_paths_;
      if (XR_FAILED(f_.CreateAction(set_, &ci, &acts_[a]))) return;
    }
    for (const Profile &p : profiles()) suggest(p);
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &set_;
    if (XR_FAILED(f_.AttachActionSets(session_, &ai))) return;
    for (int i = 0; i < 2; i++) {
      hand_spaces_[i * 2] = action_space(acts_[kAim], hand_paths_[i]);
      hand_spaces_[i * 2 + 1] = action_space(acts_[kGrip], hand_paths_[i]);
    }
    hands_ready_ = true;
  }

  void suggest(const Profile &p) {
    std::vector<XrActionSuggestedBinding> b;
    const char *sides[2] = {"/user/hand/left/", "/user/hand/right/"};
    for (const Binding &bd : p.bindings)
      for (int s = 0; s < 2; s++) {
        XrPath path = XR_NULL_PATH;
        if (!(bd.hands & (1 << s))) continue;
        if (XR_SUCCEEDED(f_.StringToPath(inst_, (std::string(sides[s]) + bd.path).c_str(), &path))) b.push_back({acts_[bd.action], path});
      }
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    if (XR_FAILED(f_.StringToPath(inst_, p.path, &sb.interactionProfile))) return;
    sb.countSuggestedBindings = (uint32_t)b.size();
    sb.suggestedBindings = b.data();
    const XrResult r = f_.SuggestBindings(inst_, &sb);  // (a profile the runtime doesn't know: refused, the others stand)
    if (XR_FAILED(r)) Log::info("VR View: %s refused %s's bindings (%d)", name_.c_str(), p.path, (int)r);
  }

  XrSpace action_space(XrAction action, XrPath hand) {
    XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    ci.action = action;
    ci.subactionPath = hand;
    ci.poseInActionSpace.orientation.w = 1.0f;
    XrSpace s = XR_NULL_HANDLE;
    f_.CreateActionSpace(session_, &ci, &s);
    return s;
  }

  static Pose pose_of(const XrPosef &p) {
    return {Quat(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w), Vec3(p.position.x, p.position.y, p.position.z)};
  }

  bool locate(XrSpace s, Pose &out) const {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (s == XR_NULL_HANDLE || XR_FAILED(f_.LocateSpace(s, space_, time_, &loc))) return false;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) return false;
    out = pose_of(loc.pose);
    return true;
  }

  bool hand(int i, Hand &h) const {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.subactionPath = hand_paths_[i];
    XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
    XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};
    XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
    gi.action = acts_[kTrigger];
    h.trigger = XR_SUCCEEDED(f_.GetFloat(session_, &gi, &f)) && f.isActive ? f.currentState : 0.0f;
    gi.action = acts_[kSqueeze];
    h.squeeze = XR_SUCCEEDED(f_.GetFloat(session_, &gi, &f)) && f.isActive ? f.currentState : 0.0f;
    gi.action = acts_[kStick];
    h.stick = XR_SUCCEEDED(f_.GetVector2f(session_, &gi, &v)) && v.isActive ? Vec2(v.currentState.x, v.currentState.y) : Vec2();
    gi.action = acts_[kPrimary];
    h.primary = XR_SUCCEEDED(f_.GetBoolean(session_, &gi, &b)) && b.isActive && b.currentState;
    h.tracked = locate(hand_spaces_[i * 2], h.aim) && locate(hand_spaces_[i * 2 + 1], h.grip);
    return h.tracked;
  }

  bool instance(std::string &why) {
    const char *exts[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ci.applicationInfo.applicationName, "Blendity", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(ci.applicationInfo.engineName, "Blendity", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    const XrResult r = f_.CreateInstance(&ci, &inst_);
    if (r == XR_ERROR_RUNTIME_UNAVAILABLE || r == XR_ERROR_RUNTIME_FAILURE) return fail(why, "no OpenXR runtime answered (SteamVR, the Oculus app or Windows Mixed Reality sets one)");
    if (r == XR_ERROR_EXTENSION_NOT_PRESENT) return fail(why, "the OpenXR runtime has no Direct3D 11 support (XR_KHR_D3D11_enable)");
    if (!check(r, "xrCreateInstance", why)) return false;
    bool ok = get(f_.gipa, inst_, "xrDestroyInstance", f_.DestroyInstance) && get(f_.gipa, inst_, "xrGetInstanceProperties", f_.GetInstanceProperties) &&
              get(f_.gipa, inst_, "xrResultToString", f_.ResultToString) && get(f_.gipa, inst_, "xrGetSystem", f_.GetSystem) &&
              get(f_.gipa, inst_, "xrGetD3D11GraphicsRequirementsKHR", f_.GetD3D11Requirements) && get(f_.gipa, inst_, "xrCreateSession", f_.CreateSession) &&
              get(f_.gipa, inst_, "xrDestroySession", f_.DestroySession) && get(f_.gipa, inst_, "xrEnumerateViewConfigurationViews", f_.EnumerateViews) &&
              get(f_.gipa, inst_, "xrEnumerateReferenceSpaces", f_.EnumerateSpaces) && get(f_.gipa, inst_, "xrCreateReferenceSpace", f_.CreateReferenceSpace) &&
              get(f_.gipa, inst_, "xrDestroySpace", f_.DestroySpace) && get(f_.gipa, inst_, "xrEnumerateSwapchainFormats", f_.EnumerateFormats) &&
              get(f_.gipa, inst_, "xrCreateSwapchain", f_.CreateSwapchain) && get(f_.gipa, inst_, "xrDestroySwapchain", f_.DestroySwapchain) &&
              get(f_.gipa, inst_, "xrEnumerateSwapchainImages", f_.EnumerateImages) && get(f_.gipa, inst_, "xrAcquireSwapchainImage", f_.AcquireImage) &&
              get(f_.gipa, inst_, "xrWaitSwapchainImage", f_.WaitImage) && get(f_.gipa, inst_, "xrReleaseSwapchainImage", f_.ReleaseImage) &&
              get(f_.gipa, inst_, "xrPollEvent", f_.PollEvent) && get(f_.gipa, inst_, "xrBeginSession", f_.BeginSession) &&
              get(f_.gipa, inst_, "xrEndSession", f_.EndSession) && get(f_.gipa, inst_, "xrWaitFrame", f_.WaitFrame) &&
              get(f_.gipa, inst_, "xrBeginFrame", f_.BeginFrame) && get(f_.gipa, inst_, "xrEndFrame", f_.EndFrame) &&
              get(f_.gipa, inst_, "xrLocateViews", f_.LocateViews);
    if (!ok) return fail(why, "the OpenXR runtime lacks a function a session needs");
    hands_ok_ = get(f_.gipa, inst_, "xrStringToPath", f_.StringToPath) && get(f_.gipa, inst_, "xrCreateActionSet", f_.CreateActionSet) &&
                get(f_.gipa, inst_, "xrCreateAction", f_.CreateAction) && get(f_.gipa, inst_, "xrSuggestInteractionProfileBindings", f_.SuggestBindings) &&
                get(f_.gipa, inst_, "xrAttachSessionActionSets", f_.AttachActionSets) && get(f_.gipa, inst_, "xrSyncActions", f_.SyncActions) &&
                get(f_.gipa, inst_, "xrGetActionStateFloat", f_.GetFloat) && get(f_.gipa, inst_, "xrGetActionStateBoolean", f_.GetBoolean) &&
                get(f_.gipa, inst_, "xrGetActionStateVector2f", f_.GetVector2f) && get(f_.gipa, inst_, "xrCreateActionSpace", f_.CreateActionSpace) &&
                get(f_.gipa, inst_, "xrLocateSpace", f_.LocateSpace);
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(f_.GetInstanceProperties(inst_, &ip))) name_ = ip.runtimeName;
    return true;
  }

  bool system(std::string &why) {
    XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO};
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult r = f_.GetSystem(inst_, &gi, &system_);
    if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) return fail(why, "no headset: " + name_ + " sees none connected (plug it in and turn it on)");
    return check(r, "xrGetSystem", why);
  }

  /* A Direct3D 11 device on the adapter the runtime names (its LUID), at its minimum feature level. */
  bool device(std::string &why) {
    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!check(f_.GetD3D11Requirements(inst_, system_, &req), "xrGetD3D11GraphicsRequirementsKHR", why)) return false;
    d3d_ = LoadLibraryA("d3d11.dll");
    dxgi_ = LoadLibraryA("dxgi.dll");
    auto create_device = d3d_ ? reinterpret_cast<PFN_D3D11CreateDevice_>(GetProcAddress(d3d_, "D3D11CreateDevice")) : nullptr;
    auto create_factory = dxgi_ ? reinterpret_cast<PFN_CreateDXGIFactory1_>(GetProcAddress(dxgi_, "CreateDXGIFactory1")) : nullptr;
    if (!create_device || !create_factory) return fail(why, "no Direct3D 11 on this PC");
    IDXGIFactory1 *factory = nullptr;
    IDXGIAdapter1 *adapter = nullptr;
    if (FAILED(create_factory(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) return fail(why, "no DXGI factory");
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
      DXGI_ADAPTER_DESC1 d;
      adapter->GetDesc1(&d);
      if (std::memcmp(&d.AdapterLuid, &req.adapterLuid, sizeof(LUID)) == 0) break;
      release(adapter);
    }
    release(factory);
    if (!adapter) return fail(why, "the headset's graphics card wasn't found");
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr = create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &dev_, nullptr, &ctx_);
    release(adapter);
    if (FAILED(hr)) return fail(why, "Direct3D 11 refused a device on the headset's graphics card");
    return true;
  }

  bool session(std::string &why) {
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = dev_;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding;
    ci.systemId = system_;
    return check(f_.CreateSession(inst_, &ci, &session_), "xrCreateSession", why);
  }

  /* The stage (the floor's origin, standing scale), else the local space (seated, the head's start). */
  bool space(std::string &why) {
    uint32_t n = 0;
    f_.EnumerateSpaces(session_, 0, &n, nullptr);
    std::vector<XrReferenceSpaceType> types(n);
    f_.EnumerateSpaces(session_, n, &n, types.data());
    XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    local_ = std::find(types.begin(), types.end(), XR_REFERENCE_SPACE_TYPE_STAGE) == types.end();
    ci.referenceSpaceType = local_ ? XR_REFERENCE_SPACE_TYPE_LOCAL : XR_REFERENCE_SPACE_TYPE_STAGE;
    ci.poseInReferenceSpace.orientation.w = 1.0f;
    return check(f_.CreateReferenceSpace(session_, &ci, &space_), "xrCreateReferenceSpace", why);
  }

  /* A swapchain per eye at `scale` of the recommended size, in the first of the formats Blendity's pixels fit
   * (0xAARRGGBB: B, G, R, A in memory; sRGB-encoded). */
  bool swapchains(float scale, std::string &why) {
    uint32_t nv = 0;
    f_.EnumerateViews(inst_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, nullptr);
    std::vector<XrViewConfigurationView> vc(nv, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!check(f_.EnumerateViews(inst_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nv, &nv, vc.data()), "xrEnumerateViewConfigurationViews", why)) return false;
    format_ = pick_format();
    if (!format_) return fail(why, "the runtime offers no 8-bit BGRA or RGBA swapchain format");
    const float s = scale > 0.05f && scale <= 1.0f ? scale : 0.5f;
    for (const XrViewConfigurationView &v : vc) {
      EyeChain e;
      e.width = std::max(16, (int)(v.recommendedImageRectWidth * s)), e.height = std::max(16, (int)(v.recommendedImageRectHeight * s));
      XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
      ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
      ci.format = format_;
      ci.sampleCount = 1, ci.faceCount = 1, ci.arraySize = 1, ci.mipCount = 1;
      ci.width = (uint32_t)e.width, ci.height = (uint32_t)e.height;
      if (!check(f_.CreateSwapchain(session_, &ci, &e.chain), "xrCreateSwapchain", why)) return false;
      uint32_t ni = 0;
      f_.EnumerateImages(e.chain, 0, &ni, nullptr);
      e.images.assign(ni, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
      f_.EnumerateImages(e.chain, ni, &ni, reinterpret_cast<XrSwapchainImageBaseHeader *>(e.images.data()));
      chains_.push_back(std::move(e));
    }
    return !chains_.empty() || fail(why, "the runtime offered no eyes");
  }

  int64_t pick_format() {
    uint32_t n = 0;
    f_.EnumerateFormats(session_, 0, &n, nullptr);
    std::vector<int64_t> formats(n);
    f_.EnumerateFormats(session_, n, &n, formats.data());
    for (int64_t want : {(int64_t)DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, (int64_t)DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, (int64_t)DXGI_FORMAT_B8G8R8A8_UNORM, (int64_t)DXGI_FORMAT_R8G8B8A8_UNORM})
      if (std::find(formats.begin(), formats.end(), want) != formats.end()) return want;
    return 0;
  }

  bool bgra() const { return format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format_ == DXGI_FORMAT_B8G8R8A8_UNORM; }
  bool srgb() const { return format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format_ == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }

  /* A pixel as the swapchain takes it: red and blue swapped for an RGBA format, each channel made linear for a plain
   * UNORM one (the compositor reads it as linear: sRGB-encoded values would look washed out - review 383). */
  uint32_t texel(uint32_t p) const {
    if (!srgb()) p = (p & 0xFF000000u) | (uint32_t)linear_[(p >> 16) & 255] << 16 | (uint32_t)linear_[(p >> 8) & 255] << 8 | linear_[p & 255];
    return bgra() ? p : (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
  }

  /* The eye's pixels into its swapchain image. */
  void upload(ID3D11Texture2D *tex, const uint32_t *pixels, int width, int height) {
    if (bgra() && srgb()) {
      ctx_->UpdateSubresource(tex, 0, nullptr, pixels, (UINT)width * 4, 0);
      return;
    }
    if (!srgb() && !linear_ready_) {
      for (int i = 0; i < 256; i++) {
        const float c = i / 255.0f;
        linear_[i] = (uint8_t)std::lround(255.0f * (c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f)));
      }
      linear_ready_ = true;
    }
    converted_.resize((size_t)width * (size_t)height);
    for (size_t i = 0; i < converted_.size(); i++) converted_[i] = texel(pixels[i]);
    ctx_->UpdateSubresource(tex, 0, nullptr, converted_.data(), (UINT)width * 4, 0);
  }

  HMODULE loader_ = nullptr, d3d_ = nullptr, dxgi_ = nullptr;
  Fns f_;
  XrInstance inst_ = XR_NULL_HANDLE;
  XrSystemId system_ = XR_NULL_SYSTEM_ID;
  XrSession session_ = XR_NULL_HANDLE;
  XrSpace space_ = XR_NULL_HANDLE;
  XrTime time_ = 0;
  ID3D11Device *dev_ = nullptr;
  ID3D11DeviceContext *ctx_ = nullptr;
  int64_t format_ = 0;
  std::vector<EyeChain> chains_;
  std::vector<XrView> views_;
  std::vector<uint32_t> converted_;
  uint8_t linear_[256] = {};
  bool linear_ready_ = false, local_ = false;
  std::vector<XrVector3f> last_positions_;
  bool hands_ok_ = false, hands_ready_ = false;
  XrActionSet set_ = XR_NULL_HANDLE;
  XrAction acts_[kActs] = {};
  XrPath hand_paths_[2] = {XR_NULL_PATH, XR_NULL_PATH};
  XrSpace hand_spaces_[4] = {XR_NULL_HANDLE, XR_NULL_HANDLE, XR_NULL_HANDLE, XR_NULL_HANDLE};  // (left aim, left grip, right aim, right grip)
  std::string name_ = "the OpenXR runtime";
};

}  // namespace

std::unique_ptr<Runtime> make_openxr_runtime(std::string &why) {
  auto rt = std::make_unique<OpenXR>();
  if (!rt->load(why)) return nullptr;
  return rt;
}

}  // namespace bl::xr

#endif
