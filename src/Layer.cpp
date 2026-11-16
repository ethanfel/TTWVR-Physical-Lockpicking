// SPDX-License-Identifier: GPL-3.0-only
#include "Feedback.h"
#include "Render.h"
#include "Shared.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <openxr_loader_negotiation.h>
#include <openxr_platform.h>
#include <set>
#include <vector>
namespace locks {
namespace {
struct Instance {
  XrInstance handle = XR_NULL_HANDLE;
  PFN_xrGetInstanceProcAddr get = nullptr;
  PFN_xrCreateSession CreateSession = nullptr;
  PFN_xrDestroySession DestroySession = nullptr;
  PFN_xrDestroyInstance DestroyInstance = nullptr;
  PFN_xrEndFrame EndFrame = nullptr;
  PFN_xrCreateActionSet CreateActionSet = nullptr;
  PFN_xrCreateAction CreateAction = nullptr;
  PFN_xrDestroyActionSet DestroyActionSet = nullptr;
  PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings =
      nullptr;
  PFN_xrPathToString PathToString = nullptr;
  PFN_xrStringToPath StringToPath = nullptr;
  PFN_xrAttachSessionActionSets AttachSessionActionSets = nullptr;
  PFN_xrSyncActions SyncActions = nullptr;
  PFN_xrCreateActionSpace CreateActionSpace = nullptr;
  PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
  PFN_xrDestroySpace DestroySpace = nullptr;
  PFN_xrLocateSpace LocateSpace = nullptr;
  PFN_xrLocateViews LocateViews = nullptr;
  PFN_xrGetActionStateFloat GetActionStateFloat = nullptr;
  PFN_xrGetActionStateBoolean GetActionStateBoolean = nullptr;
  PFN_xrGetActionStatePose GetActionStatePose = nullptr;
  PFN_xrGetActionStateVector2f GetActionStateVector2f = nullptr;
  PFN_xrApplyHapticFeedback ApplyHapticFeedback = nullptr;
  PFN_xrStopHapticFeedback StopHapticFeedback = nullptr;
  PFN_xrCreateSwapchain CreateSwapchain = nullptr;
  PFN_xrDestroySwapchain DestroySwapchain = nullptr;
  PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
  PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
  PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
  PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
  PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
  PFN_xrPollEvent PollEvent = nullptr;
  XrActionSet set = XR_NULL_HANDLE;
  XrAction grip = XR_NULL_HANDLE, aim = XR_NULL_HANDLE, hold = XR_NULL_HANDLE,
           squeeze = XR_NULL_HANDLE, squeezeClick = XR_NULL_HANDLE,
           back = XR_NULL_HANDLE, haptic = XR_NULL_HANDLE;
  XrPath hands[2]{};
  bool inputs = false, ready = false;
  std::map<XrAction, unsigned> backHands;
#ifdef LOCKPICK_TEST_INPUT
  XrActionSet probeSet = XR_NULL_HANDLE;
  XrAction probeFloat = XR_NULL_HANDLE, probeBoolean = XR_NULL_HANDLE;
#endif
};
struct Eye {
  XrSwapchain chain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageD3D11KHR> images;
};
struct Session {
  std::shared_ptr<Instance> api;
  XrSession handle = XR_NULL_HANDLE;
  ID3D11Device *device = nullptr;
  XrSpace localSpace = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE,
          handSpaces[2]{}, aimSpaces[2]{};
  Eye eyes[2];
  std::unique_ptr<Renderer> renderer;
  bool focused = false, attached = false, failed = false, positioned = false;
  bool presenting = false, captureInputs = false;
  unsigned inputTrace = ~0u;
  int tracedStock = -2;
#ifdef LOCKPICK_TEST_INPUT
  unsigned probeTrace = ~0u;
#endif
  uintptr_t menu = 0, bypassMenu = 0;
  Pose lock;
  PhysicalInteraction interaction;
  uint64_t dropRequest = 0;
  XrTime interactionTime = 0;
  unsigned frames = 0, capturePoll = ~0u, captureSource = 0;
  bool captureStill = false, captureGame = false;
  uint64_t captureRequestPoll = 0;
  HapticFeedback feedback;
  TargetOpening opening;
  XrTime openingTime = 0;
};
void PollCapture(Session &s);
std::map<XrInstance, std::shared_ptr<Instance>> instances;
std::map<XrSession, std::unique_ptr<Session>> sessions;
template <class T> bool Resolve(Instance &i, const char *n, T &f) {
  PFN_xrVoidFunction p = nullptr;
  if (XR_FAILED(i.get(i.handle, n, &p)) || !p)
    return false;
  f = reinterpret_cast<T>(p);
  return true;
}
XrPath Path(Instance &i, const char *n) {
  XrPath p = 0;
  i.StringToPath(i.handle, n, &p);
  return p;
}
std::vector<XrActionSuggestedBinding> Bindings(Instance &i,
                                               const char *profile) {
  std::vector<XrActionSuggestedBinding> b;
  bool touch = strstr(profile, "touch_controller"),
       index = strstr(profile, "index_controller"),
       wmr = strstr(profile, "microsoft/motion_controller");
  if (!touch && !index && !wmr)
    return b;
  for (int h = 0; h < 2; h++) {
    std::string root = h ? "/user/hand/right" : "/user/hand/left";
    auto add = [&](XrAction a, const char *component) {
      auto p = Path(i, (root + component).c_str());
      if (p)
        b.push_back({a, p});
    };
    add(i.grip, "/input/grip/pose");
    add(i.aim, "/input/aim/pose");
    add(i.hold, "/input/trigger/value");
    add(wmr ? i.squeezeClick : i.squeeze,
        wmr ? "/input/squeeze/click" : "/input/squeeze/value");
#ifdef LOCKPICK_TEST_INPUT
    if (wmr && i.probeFloat) {
      add(i.probeFloat, "/input/trigger/value");
      add(i.probeBoolean, "/input/trigger/value");
    }
#endif
    add(i.haptic, "/output/haptic");
  }
  return b;
}
bool CreateInputs(Instance &i) {
  XrActionSetCreateInfo c{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy(c.actionSetName, "ttwvr_lockpick");
  strcpy(c.localizedActionSetName, "Physical lockpicking");
  c.priority = UINT32_MAX; // Claim grab inputs only while this menu is active.
  if (XR_FAILED(i.CreateActionSet(i.handle, &c, &i.set))) {
    return false;
  }
  i.hands[0] = Path(i, "/user/hand/left");
  i.hands[1] = Path(i, "/user/hand/right");
  auto action = [&](XrAction &a, const char *n, XrActionType t) {
    XrActionCreateInfo ac{XR_TYPE_ACTION_CREATE_INFO};
    strcpy(ac.actionName, n);
    strcpy(ac.localizedActionName, n);
    ac.actionType = t;
    ac.countSubactionPaths = 2;
    ac.subactionPaths = i.hands;
    return XR_SUCCEEDED(i.CreateAction(i.set, &ac, &a));
  };
  if (!action(i.grip, "lock_grip", XR_ACTION_TYPE_POSE_INPUT) ||
      !action(i.aim, "lock_aim", XR_ACTION_TYPE_POSE_INPUT) ||
      !action(i.hold, "lock_hold", XR_ACTION_TYPE_FLOAT_INPUT) ||
      !action(i.squeeze, "lock_squeeze", XR_ACTION_TYPE_FLOAT_INPUT) ||
      !action(i.squeezeClick, "lock_squeeze_click",
              XR_ACTION_TYPE_BOOLEAN_INPUT) ||
      !action(i.back, "lock_fallback", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
      !action(i.haptic, "lock_feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT))
    return false;
#ifdef LOCKPICK_TEST_INPUT
  // Same-priority competing game-style actions exercise the real runtime
  // input route; file-injected tool poses alone cannot test this conflict.
  XrActionSetCreateInfo pc{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy(pc.actionSetName, "lock_route_probe");
  strcpy(pc.localizedActionSetName, "Lock input routing probe");
  pc.priority = UINT32_MAX;
  if (XR_FAILED(i.CreateActionSet(i.handle, &pc, &i.probeSet)))
    return false;
  for (int n = 0; n < 2; n++) {
    XrActionCreateInfo ac{XR_TYPE_ACTION_CREATE_INFO};
    strcpy(ac.actionName, n ? "probe_boolean" : "probe_float");
    strcpy(ac.localizedActionName, ac.actionName);
    ac.actionType =
        n ? XR_ACTION_TYPE_BOOLEAN_INPUT : XR_ACTION_TYPE_FLOAT_INPUT;
    ac.countSubactionPaths = 2;
    ac.subactionPaths = i.hands;
    if (XR_FAILED(i.CreateAction(i.probeSet, &ac,
                                 n ? &i.probeBoolean : &i.probeFloat)))
      return false;
  }
#endif
  // TTWVR does not suggest a WMR profile; this also enables the private
  // simulator.
  auto b = Bindings(i, "/interaction_profiles/microsoft/motion_controller");
  XrInteractionProfileSuggestedBinding sb{
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  sb.interactionProfile =
      Path(i, "/interaction_profiles/microsoft/motion_controller");
  sb.countSuggestedBindings = b.size();
  sb.suggestedBindings = b.data();
  auto r = i.SuggestInteractionProfileBindings(i.handle, &sb);
  Log("wmr_bindings result=%d", r);
  return true;
}
void StopFeedback(Session &s) {
  auto &i = *s.api;
  unsigned active = s.feedback.active(GetTickCount64());
  for (int n = 0; n < 2; n++)
    if (active & (1u << n)) {
      XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
      hi.action = i.haptic;
      hi.subactionPath = i.hands[n];
      i.StopHapticFeedback(s.handle, &hi);
    }
  s.feedback.reset();
}
struct AppChain {
  Session *session = nullptr;
  XrSwapchainCreateInfo desc{};
  std::vector<XrSwapchainImageD3D11KHR> images;
  struct Acquired {
    unsigned index;
    bool waited = false;
  };
  std::deque<Acquired> acquired;
  bool copyAllowed = false;
  unsigned releasedFrame = 0;
};
std::map<XrSwapchain, AppChain> appChains;
XrResult XRAPI_CALL CreateAppChain(XrSession h, const XrSwapchainCreateInfo *c,
                                   XrSwapchain *out) {
  auto it = sessions.find(h);
  if (it == sessions.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &s = *it->second;
  auto info = *c;
  bool color = (c->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
               !(c->createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT);
  if (color)
    info.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
  auto r = s.api->CreateSwapchain(h, &info, out);
  bool allowed = color;
  if (XR_FAILED(r) && info.usageFlags != c->usageFlags) {
    r = s.api->CreateSwapchain(h, c, out);
    allowed = false;
  }
  if (XR_SUCCEEDED(r)) {
    auto &chain = appChains[*out];
    chain.session = &s;
    chain.desc = *c;
    chain.desc.next = nullptr;
    chain.copyAllowed = allowed;
    unsigned count = 0;
    if (s.device && XR_SUCCEEDED(s.api->EnumerateSwapchainImages(
                        *out, 0, &count, nullptr))) {
      chain.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
      if (XR_FAILED(s.api->EnumerateSwapchainImages(
              *out, count, &count,
              reinterpret_cast<XrSwapchainImageBaseHeader *>(
                  chain.images.data()))))
        chain.images.clear();
    }
    Log("background_chain size=%ux%u array=%u samples=%u format=%lld copy=%d",
        c->width, c->height, c->arraySize, c->sampleCount, (long long)c->format,
        allowed);
  }
  return r;
}
XrResult XRAPI_CALL DestroyAppChain(XrSwapchain h) {
  auto it = appChains.find(h);
  if (it == appChains.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &s = *it->second.session;
  auto r = s.api->DestroySwapchain(h);
  if (XR_SUCCEEDED(r)) {
    if (s.renderer)
      s.renderer->background.Erase(h);
    appChains.erase(it);
  }
  return r;
}
XrResult XRAPI_CALL AcquireAppImage(XrSwapchain h,
                                    const XrSwapchainImageAcquireInfo *c,
                                    uint32_t *index) {
  auto it = appChains.find(h);
  if (it == appChains.end())
    return XR_ERROR_HANDLE_INVALID;
  auto r = it->second.session->api->AcquireSwapchainImage(h, c, index);
  if (XR_SUCCEEDED(r))
    it->second.acquired.push_back({*index, false});
  return r;
}
XrResult XRAPI_CALL WaitAppImage(XrSwapchain h,
                                 const XrSwapchainImageWaitInfo *c) {
  auto it = appChains.find(h);
  if (it == appChains.end())
    return XR_ERROR_HANDLE_INVALID;
  auto r = it->second.session->api->WaitSwapchainImage(h, c);
  if (r == XR_SUCCESS)
    for (auto &a : it->second.acquired)
      if (!a.waited) {
        a.waited = true;
        break;
      }
  return r;
}
XrResult XRAPI_CALL ReleaseAppImage(XrSwapchain h,
                                    const XrSwapchainImageReleaseInfo *c) {
  auto it = appChains.find(h);
  if (it == appChains.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &chain = it->second;
  auto &s = *chain.session;
  PollCapture(s);
  // Queue copies while the application still owns its successfully waited
  // image. Never inspect a runtime texture after release or copy on the CPU.
  if (!chain.acquired.empty() && chain.acquired.front().waited &&
      chain.copyAllowed && s.renderer &&
      ((s.interaction.config.useGameBackground && Snapshot().menu) ||
       s.captureGame) &&
      chain.acquired.front().index < chain.images.size())
    s.renderer->background.Save(
        h, chain.images[chain.acquired.front().index].texture, s.frames + 1);
  auto r = s.api->ReleaseSwapchainImage(h, c);
  if (XR_SUCCEEDED(r) && !chain.acquired.empty()) {
    chain.releasedFrame = s.frames + 1;
    chain.acquired.pop_front();
  }
  if (XR_FAILED(r) && s.renderer)
    s.renderer->background.Erase(h);
  return r;
}
void Clean(Session &s) {
  StopFeedback(s);
  auto &i = *s.api;
  for (auto it = appChains.begin(); it != appChains.end();)
    if (it->second.session == &s)
      it = appChains.erase(it);
    else
      ++it;
  s.renderer.reset();
  for (auto &e : s.eyes)
    if (e.chain) {
      i.DestroySwapchain(e.chain);
      e.chain = XR_NULL_HANDLE;
    }
  for (auto sp : {s.handSpaces[0], s.handSpaces[1], s.aimSpaces[0],
                  s.aimSpaces[1], s.viewSpace, s.localSpace})
    if (sp)
      i.DestroySpace(sp);
  if (s.device) {
    s.device->Release();
    s.device = nullptr;
  }
}
void Neutral() {
  std::lock_guard guard(sharedMutex);
  control = {};
}
bool ReadyRenderer(Session &s) {
  auto &i = *s.api;
  if (s.renderer)
    return true;
  if (s.failed || !s.device)
    return false;
  s.renderer = std::make_unique<Renderer>();
  if (!s.renderer->Init(s.device)) {
    s.renderer.reset();
    s.failed = true;
    Log("render_bypass init_failed=1");
    return false;
  }
  uint32_t count = 0;
  if (XR_FAILED(i.EnumerateSwapchainFormats(s.handle, 0, &count, nullptr))) {
    s.failed = true;
    return false;
  }
  std::vector<int64_t> formats(count);
  i.EnumerateSwapchainFormats(s.handle, count, &count, formats.data());
  int64_t format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  if (std::find(formats.begin(), formats.end(), format) == formats.end())
    format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (std::find(formats.begin(), formats.end(), format) == formats.end()) {
    s.failed = true;
    return false;
  }
  for (auto &e : s.eyes) {
    XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    c.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                   XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    c.format = format;
    c.sampleCount = c.faceCount = c.arraySize = c.mipCount = 1;
    c.width = c.height = Renderer::size;
    if (XR_FAILED(i.CreateSwapchain(s.handle, &c, &e.chain))) {
      s.failed = true;
      return false;
    }
    if (XR_FAILED(i.EnumerateSwapchainImages(e.chain, 0, &count, nullptr))) {
      s.failed = true;
      return false;
    }
    e.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_FAILED(i.EnumerateSwapchainImages(
            e.chain, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(e.images.data())))) {
      s.failed = true;
      return false;
    }
  }
  Log("stereo_renderer_ready eye_size=%u original_images_untouched=1",
      Renderer::size);
  return true;
}
XrResult XRAPI_CALL CreateSession(XrInstance h, const XrSessionCreateInfo *c,
                                  XrSession *out) {
  auto it = instances.find(h);
  if (it == instances.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &i = *it->second;
  auto r = i.CreateSession(h, c, out);
  if (XR_FAILED(r))
    return r;
  auto s = std::make_unique<Session>();
  s->api = it->second;
  s->handle = *out;
  for (auto *p = static_cast<const XrBaseInStructure *>(c->next); p;
       p = p->next)
    if (p->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
      s->device =
          reinterpret_cast<const XrGraphicsBindingD3D11KHR *>(p)->device;
      if (s->device)
        s->device->AddRef();
    }
  XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space.poseInReferenceSpace.orientation.w = 1;
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  i.CreateReferenceSpace(*out, &space, &s->localSpace);
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  i.CreateReferenceSpace(*out, &space, &s->viewSpace);
  if (i.inputs)
    for (int n = 0; n < 2; n++) {
      XrActionSpaceCreateInfo a{XR_TYPE_ACTION_SPACE_CREATE_INFO};
      a.action = i.grip;
      a.subactionPath = i.hands[n];
      a.poseInActionSpace.orientation.w = 1;
      i.CreateActionSpace(*out, &a, &s->handSpaces[n]);
      a.action = i.aim;
      i.CreateActionSpace(*out, &a, &s->aimSpaces[n]);
    }
  Log("session_created d3d11=%d spaces=%d", s->device != nullptr,
      s->localSpace != XR_NULL_HANDLE);
  sessions[*out] = std::move(s);
  return r;
}
XrResult XRAPI_CALL DestroySession(XrSession h) {
  auto it = sessions.find(h);
  if (it == sessions.end())
    return XR_ERROR_HANDLE_INVALID;
  auto i = it->second->api;
  Neutral();
  Clean(*it->second);
  auto r = i->DestroySession(h);
  sessions.erase(it);
  return r;
}
XrResult XRAPI_CALL Suggest(XrInstance h,
                            const XrInteractionProfileSuggestedBinding *c) {
  auto &i = *instances.at(h);
  if (!i.inputs)
    return i.SuggestInteractionProfileBindings(h, c);
  char p[XR_MAX_PATH_LENGTH]{};
  uint32_t n = 0;
  i.PathToString(h, c->interactionProfile, sizeof(p), &n, p);
  for (uint32_t n = 0; n < c->countSuggestedBindings; n++) {
    auto binding = c->suggestedBindings[n];
    char path[XR_MAX_PATH_LENGTH]{};
    uint32_t size = 0;
    if (XR_SUCCEEDED(
            i.PathToString(h, binding.binding, sizeof(path), &size, path))) {
      if (!strcmp(path, "/user/hand/right/input/b/click") ||
          !strcmp(path, "/user/hand/right/input/menu/click"))
        i.backHands[binding.action] |= 2;
      if (!strcmp(path, "/user/hand/left/input/menu/click"))
        i.backHands[binding.action] |= 1;
    }
  }
  auto extra = Bindings(i, p);
  std::vector<XrActionSuggestedBinding> b(
      c->suggestedBindings, c->suggestedBindings + c->countSuggestedBindings);
  b.insert(b.end(), extra.begin(), extra.end());
  auto copy = *c;
  copy.countSuggestedBindings = b.size();
  copy.suggestedBindings = b.data();
  auto r = i.SuggestInteractionProfileBindings(h, &copy);
  Log("bindings profile=%s extra=%u result=%d", p, unsigned(extra.size()), r);
  if (XR_FAILED(r)) {
    Log("bindings_bypass profile=%s result=%d", p, r);
    return i.SuggestInteractionProfileBindings(h, c);
  }
  return r;
}
XrResult XRAPI_CALL Attach(XrSession h,
                           const XrSessionActionSetsAttachInfo *c) {
  auto &s = *sessions.at(h);
  auto &i = *s.api;
  if (!i.inputs)
    return i.AttachSessionActionSets(h, c);
  std::vector<XrActionSet> sets(c->actionSets,
                                c->actionSets + c->countActionSets);
  sets.push_back(i.set);
#ifdef LOCKPICK_TEST_INPUT
  sets.push_back(i.probeSet);
#endif
  auto copy = *c;
  copy.actionSets = sets.data();
  copy.countActionSets = sets.size();
  auto r = i.AttachSessionActionSets(h, &copy);
  s.attached = XR_SUCCEEDED(r);
  Log("input_attached result=%d", r);
  if (XR_FAILED(r))
    return i.AttachSessionActionSets(h, c);
  return r;
}
bool OwnAction(const Instance &i, XrAction action) {
  return action == i.grip || action == i.aim || action == i.hold ||
         action == i.squeeze || action == i.squeezeClick || action == i.back ||
         action == i.haptic;
}
bool PassInput(const Session &s, const XrActionStateGetInfo *get) {
  const auto &i = *s.api;
  if (!s.captureInputs || OwnAction(i, get->action))
    return true;
  auto back = i.backHands.find(get->action);
  unsigned hand = get->subactionPath == i.hands[0]   ? 1
                  : get->subactionPath == i.hands[1] ? 2
                                                     : 3;
  return back != i.backHands.end() && (back->second & hand);
}
XrResult XRAPI_CALL StateFloat(XrSession h, const XrActionStateGetInfo *get,
                               XrActionStateFloat *out) {
  auto &s = *sessions.at(h);
  auto r = s.api->GetActionStateFloat(h, get, out);
  if (XR_SUCCEEDED(r) && !PassInput(s, get)) {
    out->currentState = 0;
    out->changedSinceLastSync = XR_FALSE;
    out->lastChangeTime = 0;
    out->isActive = XR_FALSE;
  }
  return r;
}
XrResult XRAPI_CALL StateBoolean(XrSession h, const XrActionStateGetInfo *get,
                                 XrActionStateBoolean *out) {
  auto &s = *sessions.at(h);
  auto r = s.api->GetActionStateBoolean(h, get, out);
  if (XR_SUCCEEDED(r) && !PassInput(s, get)) {
    out->currentState = XR_FALSE;
    out->changedSinceLastSync = XR_FALSE;
    out->lastChangeTime = 0;
    out->isActive = XR_FALSE;
  }
  return r;
}
XrResult XRAPI_CALL StateVector(XrSession h, const XrActionStateGetInfo *get,
                                XrActionStateVector2f *out) {
  auto &s = *sessions.at(h);
  auto r = s.api->GetActionStateVector2f(h, get, out);
  if (XR_SUCCEEDED(r) && !PassInput(s, get)) {
    out->currentState = {};
    out->changedSinceLastSync = XR_FALSE;
    out->lastChangeTime = 0;
    out->isActive = XR_FALSE;
  }
  return r;
}
XrResult XRAPI_CALL AppHaptic(XrSession h, const XrHapticActionInfo *info,
                              const XrHapticBaseHeader *feedback) {
  auto &s = *sessions.at(h);
  if (s.captureInputs && info->action != s.api->haptic)
    return XR_SUCCESS; // Original rumble must not overwrite the physical cues.
  return s.api->ApplyHapticFeedback(h, info, feedback);
}
XrResult XRAPI_CALL AppStopHaptic(XrSession h, const XrHapticActionInfo *info) {
  auto &s = *sessions.at(h);
  if (s.captureInputs && info->action != s.api->haptic)
    return XR_SUCCESS;
  return s.api->StopHapticFeedback(h, info);
}
XrResult XRAPI_CALL AppActionSet(XrInstance h, const XrActionSetCreateInfo *c,
                                 XrActionSet *out) {
  auto r = instances.at(h)->CreateActionSet(h, c, out);
  Log("app_action_set name=%s priority=%u result=%d", c->actionSetName,
      c->priority, r);
  return r;
}
XrResult XRAPI_CALL Sync(XrSession h, const XrActionsSyncInfo *c) {
  auto &s = *sessions.at(h);
  auto &i = *s.api;
  auto g = Snapshot();
  bool active = s.attached && s.focused && !s.failed && NativeReady() &&
                g.menu && g.menu != s.bypassMenu;
  bool capture = active && s.presenting;
  if (capture != s.captureInputs)
    Log("input_capture active=%d original_back_preserved=1", capture);
  s.captureInputs = capture; // Stable for every read until the next sync.
  std::vector<XrActiveActionSet> sets;
  if (c->countActiveActionSets)
    sets.assign(c->activeActionSets,
                c->activeActionSets + c->countActiveActionSets);
  if (active)
    sets.push_back({i.set, XR_NULL_PATH});
#ifdef LOCKPICK_TEST_INPUT
  sets.push_back({i.probeSet, XR_NULL_PATH});
#endif
  auto copy = *c;
  copy.activeActionSets = sets.data();
  copy.countActiveActionSets = sets.size();
  auto r = i.SyncActions(h, &copy);
#ifdef LOCKPICK_TEST_INPUT
  if (XR_SUCCEEDED(r)) {
    for (int n = 0; n < 2; n++) {
      XrActionStateGetInfo q{XR_TYPE_ACTION_STATE_GET_INFO};
      q.action = i.probeFloat;
      q.subactionPath = i.hands[n];
      XrActionStateFloat raw{XR_TYPE_ACTION_STATE_FLOAT},
          filtered{XR_TYPE_ACTION_STATE_FLOAT}, own{XR_TYPE_ACTION_STATE_FLOAT};
      i.GetActionStateFloat(h, &q, &raw);
      StateFloat(h, &q, &filtered);
      q.action = i.hold;
      i.GetActionStateFloat(h, &q, &own);
      q.action = i.probeBoolean;
      XrActionStateBoolean rb{XR_TYPE_ACTION_STATE_BOOLEAN},
          fb{XR_TYPE_ACTION_STATE_BOOLEAN};
      i.GetActionStateBoolean(h, &q, &rb);
      StateBoolean(h, &q, &fb);
      unsigned bits = (capture ? 1u : 0u) | (raw.currentState > .5f ? 2u : 0u) |
                      (filtered.currentState > .5f ? 4u : 0u) |
                      (own.currentState > .5f ? 8u : 0u) |
                      (rb.currentState ? 16u : 0u) |
                      (fb.currentState ? 32u : 0u);
      unsigned mask = 63u << (n * 6);
      if ((s.probeTrace & mask) != (bits << (n * 6))) {
        s.probeTrace = (s.probeTrace & ~mask) | (bits << (n * 6));
        Log("input_route_probe hand=%d capture=%d raw=%.1f filtered=%.1f "
            "own=%.1f raw_boolean=%d filtered_boolean=%d",
            n, capture, raw.currentState, filtered.currentState,
            own.currentState, rb.currentState, fb.currentState);
      }
    }
  }
#endif
  return r;
}
XrResult XRAPI_CALL Poll(XrInstance h, XrEventDataBuffer *event) {
  auto &i = *instances.at(h);
  auto r = i.PollEvent(h, event);
  if (XR_SUCCEEDED(r) &&
      (event->type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING ||
       event->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)) {
    bool reset =
        event->type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
    if (event->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      auto *e = reinterpret_cast<XrEventDataSessionStateChanged *>(event);
      reset = e->state != XR_SESSION_STATE_FOCUSED;
      auto entry = sessions.find(e->session);
      if (entry != sessions.end())
        entry->second->focused = !reset;
    }
    if (reset) {
      Neutral();
      for (auto &entry : sessions)
        if (entry.second->api->handle == h) {
          entry.second->positioned = false;
          entry.second->interaction.reset(Snapshot().pick);
          StopFeedback(*entry.second);
        }
    }
  }
  return r;
}
bool Input(Session &s, XrTime time, Control &out, Scene &scene,
           const GameState &g) {
  auto &i = *s.api;
  if (!s.attached)
    return false;
  XrSpaceLocation hands[2] = {{XR_TYPE_SPACE_LOCATION},
                              {XR_TYPE_SPACE_LOCATION}};
  float holds[2]{}, triggers[2]{}, squeezes[2]{};
  unsigned activeMask = 0, aimMask = 0;
  Quat aimOrientation[2];
  for (int n = 0; n < 2; n++) {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = i.grip;
    get.subactionPath = i.hands[n];
    XrActionStatePose active{XR_TYPE_ACTION_STATE_POSE};
    if (XR_FAILED(i.GetActionStatePose(s.handle, &get, &active)) ||
        !active.isActive)
      return false;
    if (XR_FAILED(
            i.LocateSpace(s.handSpaces[n], s.localSpace, time, &hands[n])) ||
        (hands[n].locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                   XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                   XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                                   XR_SPACE_LOCATION_POSITION_TRACKED_BIT)) !=
            (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
             XR_SPACE_LOCATION_POSITION_VALID_BIT |
             XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
             XR_SPACE_LOCATION_POSITION_TRACKED_BIT))
      return false;
    aimOrientation[n] = pose(hands[n].pose).q;
    get.action = i.aim;
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    constexpr auto orientation = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                 XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if (s.aimSpaces[n] &&
        XR_SUCCEEDED(i.GetActionStatePose(s.handle, &get, &active)) &&
        active.isActive &&
        XR_SUCCEEDED(i.LocateSpace(s.aimSpaces[n], s.localSpace, time, &aim)) &&
        (aim.locationFlags & orientation) == orientation) {
      aimOrientation[n] = pose(aim.pose).q;
      aimMask |= 1u << n;
    }
    get.action = i.hold;
    XrActionStateFloat hold{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_SUCCEEDED(i.GetActionStateFloat(s.handle, &get, &hold)) &&
        hold.isActive)
      triggers[n] = hold.currentState, activeMask |= 1u << n;
    get.action = i.squeeze;
    XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_SUCCEEDED(i.GetActionStateFloat(s.handle, &get, &squeeze)) &&
        squeeze.isActive)
      squeezes[n] = squeeze.currentState, activeMask |= 4u << n;
    get.action = i.squeezeClick;
    XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_SUCCEEDED(i.GetActionStateBoolean(s.handle, &get, &click)) &&
        click.isActive)
      squeezes[n] = click.currentState ? 1.f : 0.f, activeMask |= 16u << n;
    holds[n] = std::max(triggers[n], squeezes[n]);
  }

  Pose left = pose(hands[0].pose), right = pose(hands[1].pose);
#ifdef LOCKPICK_TEST_INPUT
  // Developer build only. Positions are relative to the displayed lock.
  if (auto *file =
          _wfopen((folder + L"\\lockpick-test-input.txt").c_str(), L"rb")) {
    float lx, ly, lz, rx, ry, rz, roll, lh, rh;
    if (fscanf(file, "%f %f %f %f %f %f %f %f %f", &lx, &ly, &lz, &rx, &ry, &rz,
               &roll, &lh, &rh) == 9) {
      left.p = world(s.lock, {lx, ly, lz});
      right.p = world(s.lock, {rx, ry, rz});
      // Optional left roll enables swapped-hand trials. Compose poses in
      // lock space so the test does not depend on the headset orientation.
      float leftRoll = 0;
      if (fscanf(file, "%f", &leftRoll) != 1)
        leftRoll = 0;
      float half = roll * pi / 360, leftHalf = leftRoll * pi / 360;
      left.q =
          multiply(s.lock.q, {0, 0, std::sin(leftHalf), std::cos(leftHalf)});
      right.q = multiply(s.lock.q, {0, 0, std::sin(half), std::cos(half)});
      // Optional full tool-oriented poses for insertion/physics trials.
      Quat lq, rq;
      if (fscanf(file, "%f %f %f %f %f %f %f %f", &lq.x, &lq.y, &lq.z, &lq.w,
                 &rq.x, &rq.y, &rq.z, &rq.w) == 8) {
        left.q = multiply(s.lock.q, unit(lq));
        right.q = multiply(s.lock.q, unit(rq));
      }
      aimOrientation[0] = left.q;
      aimOrientation[1] = right.q;
      if (lh >= 0)
        holds[0] = lh;
      if (rh >= 0)
        holds[1] = rh;
    }
    fclose(file);
  }
#endif
  if (!finite(left.p) || !finite(right.p))
    return false;
  scene.hands[0] = left;
  scene.hands[1] = right;
  scene.holds[0] = holds[0];
  scene.holds[1] = holds[1];
  scene.tracked = true;
  auto &input = s.interaction;
  float dt = s.interactionTime && time > s.interactionTime
                 ? float(double(time - s.interactionTime) * 1e-9)
                 : 0;
  s.interactionTime = time;
  Pose pinches[2]{{aimOrientation[0], world(left, s.renderer->PinchPoint(0))},
                  {aimOrientation[1], world(right, s.renderer->PinchPoint(1))}};
  bool dropped = false;
  int remaining = -1;
  if (s.dropRequest && PinDropResult(s.dropRequest, dropped, remaining)) {
    input.resolveDrop(dropped, remaining);
    s.dropRequest = 0;
  }
  input.worldDrops = input.config.dropPins && g.inventoryReady && g.target;
  if (s.dropRequest)
    input.dropPending = true;
  input.syncInventory(g.inventoryReady, g.pinCount);
  input.update(s.lock, compose(left, s.renderer->DriverGrip(0)),
               compose(right, s.renderer->DriverGrip(1)), holds[0], holds[1],
               g.cylinder, dt, g.stage, g.health, pinches);
  if (input.droppedEvents && !s.dropRequest) {
    s.dropRequest = RequestPinDrop(g.menu, g.target);
    if (!s.dropRequest)
      input.resolveDrop(false, g.pinCount);
  }
  input.syncInventory(g.inventoryReady, g.pinCount);
  scene.pinchHands = (input.pickHand >= 0 ? 1u << input.pickHand : 0u) |
                     (input.bendHand >= 0 ? 1u << input.bendHand : 0u);
  scene.config = input.config;
  scene.pinStock = g.inventoryReady ? g.pinCount : -1;
  scene.storedPinCount = input.box.count;
  scene.storedPins = input.box.pins;
  for (int k = 0; k < 2; k++) {
    const auto &tool = input.tools[k];
    int hand = tool.owner;
    if (hand < 0)
      continue;
    scene.holds[hand] = 1;
    if (k == 0) {
      Pose held = compose(s.lock, tool.pose);
      Vec contact = world(held, handleCenter(0, tool.opening, tool.bendYaw) +
                                    Vec{0, .010f, 0});
      scene.hands[hand] = presentPinHand(
          scene.hands[hand], contact, s.renderer->PinchPoint(hand),
          input.config.handConstraint, input.config.handAngle);
      continue;
    }
    if (!tool.inserted && !tool.inserting && !tool.blocked)
      continue;
    Pose actual = scene.hands[hand];
    Pose localGrip = s.renderer->DriverGrip(hand);
    Pose desired = compose(s.lock, tool.pose);
    scene.hands[hand].q =
        rotateTowards(actual.q, multiply(desired.q, inverse(localGrip.q)),
                      input.config.handAngle);
    Vec contact = world(desired, handleCenter(k, tool.opening, tool.bendYaw));
    Vec current = world(scene.hands[hand], localGrip.p);
    scene.hands[hand].p =
        actual.p + bounded(contact - current, input.config.handConstraint);
  }
  if (input.bendHand >= 0) {
    int h = input.bendHand;
    scene.holds[h] = 1;
    Vec contact = world(s.lock, input.tools[0].pose.p);
    Vec current = world(scene.hands[h], s.renderer->PinchPoint(h));
    scene.hands[h].p = scene.hands[h].p +
                       bounded(contact - current, input.config.handConstraint);
  }
  for (int k = 0; k < 2; k++)
    scene.tools[k] = input.tools[k];
  scene.tools[0].pose = tremblingPinPose(scene.tools[0].pose, input.pinTremor);
  out.pick = input.pick;
  out.targetAngle = input.target;
  out.turning = input.screwdriverHand >= 0 && input.target > .5f;
  out.torque = out.turning ? 1.f : 0.f;
  scene.pick = input.pick;
  scene.engaged = input.screwdriverHand >= 0;
  scene.pickHeld = input.pickHand >= 0;
  scene.pickNear = input.pickNear;
  scene.screwdriverNear = input.screwdriverNear;
  unsigned trace = unsigned(input.pickHand + 1) |
                   (unsigned(input.screwdriverHand + 1) << 2) |
                   (holds[0] >= .65f ? 16u : 0u) |
                   (holds[1] >= .65f ? 32u : 0u) | (activeMask << 6) |
                   (input.tools[0].inserted ? 4096u : 0u) |
                   (input.tools[1].inserted ? 8192u : 0u) | (aimMask << 14) |
                   (input.tools[0].visible ? 65536u : 0u) |
                   (input.tools[1].visible ? 131072u : 0u) |
                   (input.tools[0].inserting ? 262144u : 0u) |
                   (input.tools[1].inserting ? 524288u : 0u) |
                   (unsigned(input.bendHand + 1) << 20) |
                   (pinUsable(input.tools[0].opening) ? 4194304u : 0u);
  if (trace != s.inputTrace || scene.pinStock != s.tracedStock ||
      s.frames % 90 == 0 || (input.pinResisting && s.frames % 8 == 0)) {
    s.inputTrace = trace;
    s.tracedStock = scene.pinStock;
    Log("tool_input left_trigger=%.3f right_trigger=%.3f left_grip=%.3f "
        "right_grip=%.3f active_mask=%u pick_near=%d "
        "screwdriver_near=%d pick_hand=%d screwdriver_hand=%d target=%.2f "
        "aim_mask=%u",
        triggers[0], triggers[1], squeezes[0], squeezes[1], activeMask,
        input.pickNear, input.screwdriverNear, input.pickHand,
        input.screwdriverHand, input.target, aimMask);
    for (int k = 0; k < 2; k++) {
      const auto &t = input.tools[k];
      Log("tool_body kind=%d owner=%d inserted=%d visible=%d "
          "position=%.5f,%.5f,%.5f rotation=%.5f,%.5f,%.5f,%.5f "
          "velocity=%.3f,%.3f,%.3f inserting=%d progress=%.3f opening=%.3f "
          "holstered=%d bend_yaw=%.3f quality=%.3f blocked=%d depth=%.4f "
          "alignment=%.3f tremor=%.3f stock=%d",
          k, t.owner, t.inserted, t.visible, t.pose.p.x, t.pose.p.y, t.pose.p.z,
          t.pose.q.x, t.pose.q.y, t.pose.q.z, t.pose.q.w, t.velocity.x,
          t.velocity.y, t.velocity.z, t.inserting, t.insertProgress, t.opening,
          t.holstered, t.bendYaw,
          pinHandlingQuality(t.opening, t.alignment, t.pinDepth), t.blocked,
          t.pinDepth, t.alignment, k == 0 ? input.pinTremor : 0.f,
          scene.pinStock);
    }
  }
  if (s.frames % 90 == 0 || input.storedEvents || input.droppedEvents) {
    int sleeping = 0;
    for (int n = 0; n < input.box.count; n++)
      sleeping += input.box.pins[n].sleeping;
    Log("pin_box shown=%d asleep=%d inventory=%d", input.box.count, sleeping,
        scene.pinStock);
  }
  if (s.frames % 90 == 0 || input.withdrawnEvents || (input.recovered & 1) ||
      (input.target > .5f && s.frames % 8 == 0)) {
    const auto &p = input.tools[0];
    Vec hand = p.owner >= 0 ? local(s.lock, pinches[p.owner].p) : Vec{};
    Vec grip = world(p.pose, handleCenter(0, p.opening, p.bendYaw));
    Log("pin_constraint contact=%.5f,%.5f,%.5f hand=%.5f,%.5f,%.5f "
        "gap=%.4f pose_pick=%.3f requested_pick=%.3f cylinder=%.3f "
        "recovered=%u",
        p.pinContact.x, p.pinContact.y, p.pinContact.z, hand.x, hand.y, hand.z,
        p.owner >= 0 ? length(hand - grip) : 0.f,
        pinPoseAngle(p.pose, g.cylinder), input.pick, g.cylinder,
        input.recovered & 1u);
  }
  if (input.rummagedHands && s.frames % 15 == 0)
    Log("pin_box_contact hands=%u shown=%d", input.rummagedHands,
        input.box.count);
  if (input.housingHands)
    Log("housing_contact hands=%u", input.housingHands);
  if (input.storedEvents)
    Log("tool_stored mask=%u", input.storedEvents);
  if (input.preparedHands)
    Log("pin_prepared hands=%u opening=%.3f", input.preparedHands,
        input.tools[0].opening);
  if (input.insertedEvents || input.withdrawnEvents || input.recovered)
    Log("physical_tools inserted_mask=%u withdrawn_mask=%u recovered_mask=%u",
        input.insertedEvents, input.withdrawnEvents, input.recovered);
  float condition = std::clamp(g.health / (1.f + g.skill * .01f), 0.f, 1.f);
  unsigned held = (holds[0] > .2f ? 1u : 0u) | (holds[1] > .2f ? 2u : 0u);
  unsigned insertedHands = 0;
  for (int k = 0; k < 2; k++)
    if ((input.insertedEvents & (1u << k)) && input.tools[k].owner >= 0)
      insertedHands |= 1u << input.tools[k].owner;
  auto feedback = s.feedback.update(
      GetTickCount64(), g.stage, condition, g.cylinder, input.target,
      input.screwdriverHand, input.pickHand, held, insertedHands,
      input.preparedHands, input.housingHands);
  for (int hand = 0; hand < 2; hand++) {
    XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
    hi.action = i.haptic;
    hi.subactionPath = i.hands[hand];
    if (feedback.stop & (1u << hand))
      i.StopHapticFeedback(s.handle, &hi);
    auto pulse = feedback.pulses[hand];
    if (pulse.cue != Cue::None) {
      XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
      vibration.duration = XrDuration(pulse.durationMs) * 1000000;
      vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
      vibration.amplitude = pulse.amplitude;
      auto result = i.ApplyHapticFeedback(
          s.handle, &hi, reinterpret_cast<XrHapticBaseHeader *>(&vibration));
      Log("haptic cue=%s hand=%d amplitude=%.3f duration_ms=%u condition=%.3f "
          "pressure=%.3f interval_ms=%u target=%.2f cylinder=%.2f result=%d",
          cueName(pulse.cue), hand, pulse.amplitude, pulse.durationMs,
          condition, feedback.pressure, pulse.intervalMs, input.target,
          g.cylinder, result);
    }
  }
  return true;
}
void PollCapture(Session &s) {
  if (s.capturePoll == s.frames)
    return;
  s.capturePoll = s.frames;
  uint64_t now = GetTickCount64();
  if (now - s.captureRequestPoll >= 250) {
    s.captureRequestPoll = now;
    s.captureStill =
        GetFileAttributesW((folder + L"\\lockpick-capture.request").c_str()) !=
        INVALID_FILE_ATTRIBUTES;
    if (!s.renderer &&
        (s.captureStill ||
         GetFileAttributesW((folder + L"\\lockpick-video.request").c_str()) !=
             INVALID_FILE_ATTRIBUTES))
      ReadyRenderer(s);
  }
  s.captureGame = false;
  if (!s.renderer)
    return;
  s.renderer->video.Tick();
  s.renderer->video.Begin();
  s.captureGame = s.captureStill || s.renderer->video.WantsFrame();
  if (!s.captureStill && !s.renderer->video.Active()) {
    s.renderer->gameCapture.Reset();
    if (!Snapshot().menu)
      s.renderer->background.Clear();
    s.captureSource = 0;
  }
}
void CaptureSource(Session &s, unsigned source) {
  if (s.captureSource != source) {
    Log("capture_source source=%s frame=%u eyes=2",
        source == 1 ? "game" : "lockpicking", s.frames);
    s.captureSource = source;
  }
}
bool CaptureImage(Session &s, const XrSwapchainSubImage &sub,
                  BackgroundView &view) {
  auto it = appChains.find(sub.swapchain);
  if (it == appChains.end() || it->second.session != &s ||
      !it->second.copyAllowed || !it->second.releasedFrame)
    return false;
  // A submitted quad may legitimately reuse its last released image. Only use
  // our copy of that exact release; never read an image owned by the runtime.
  view = s.renderer->background.View(sub.swapchain, sub.imageArrayIndex,
                                     it->second.releasedFrame);
  view.rect = sub.imageRect;
  return view.image && view.rect.offset.x >= 0 && view.rect.offset.y >= 0 &&
         view.rect.extent.width > 0 && view.rect.extent.height > 0 &&
         uint64_t(view.rect.offset.x) + view.rect.extent.width <= view.width &&
         uint64_t(view.rect.offset.y) + view.rect.extent.height <= view.height;
}
bool CaptureGameLayers(Session &s, const XrFrameEndInfo *c) {
  if (!c->layerCount)
    return false;
  auto locate = [&](XrSpace space, Pose &out) {
    if (space == s.localSpace) {
      out = {};
      return true;
    }
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    auto flags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                 XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (XR_FAILED(s.api->LocateSpace(space, s.localSpace, c->displayTime,
                                     &location)) ||
        (location.locationFlags & flags) != unsigned(flags))
      return false;
    out = pose(location.pose);
    return true;
  };
  XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  bool haveView = false;
  for (unsigned n = 0; n < c->layerCount; n++) {
    if (c->layers[n]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
      continue;
    auto *p =
        reinterpret_cast<const XrCompositionLayerProjection *>(c->layers[n]);
    Pose space;
    if (p->viewCount != 2 || !locate(p->space, space))
      return false;
    for (unsigned eye = 0; eye < 2; eye++) {
      Pose v = compose(space, pose(p->views[eye].pose));
      views[eye].pose = {{v.q.x, v.q.y, v.q.z, v.q.w}, {v.p.x, v.p.y, v.p.z}};
      views[eye].fov = p->views[eye].fov;
    }
    haveView = true;
    break;
  }
  if (!haveView) {
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = c->displayTime;
    li.space = s.localSpace;
    XrViewState state{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    auto flags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    if (XR_FAILED(
            s.api->LocateViews(s.handle, &li, &state, 2, &count, views)) ||
        count != 2 || (state.viewStateFlags & flags) != unsigned(flags))
      return false;
  }
  std::vector<CaptureLayer> layers[2];
  for (unsigned n = 0; n < c->layerCount; n++) {
    auto *base = c->layers[n];
    Pose space;
    if (!locate(base->space, space))
      return false;
    if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
      auto *p = reinterpret_cast<const XrCompositionLayerProjection *>(base);
      if (p->viewCount != 2)
        return false;
      for (unsigned eye = 0; eye < 2; eye++) {
        CaptureLayer layer;
        layer.flags = base->layerFlags;
        if (!CaptureImage(s, p->views[eye].subImage, layer.image))
          return false;
        Pose source = compose(space, pose(p->views[eye].pose));
        if (length(source.p - pose(views[eye].pose).p) > .015f)
          return false;
        layer.image.fov = p->views[eye].fov;
        layer.image.rotation =
            multiply(inverse(source.q), pose(views[eye].pose).q);
        layers[eye].push_back(layer);
      }
    } else if (base->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
      auto *q = reinterpret_cast<const XrCompositionLayerQuad *>(base);
      for (unsigned eye = 0; eye < 2; eye++) {
        if (q->eyeVisibility != XR_EYE_VISIBILITY_BOTH &&
            q->eyeVisibility !=
                (eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT))
          continue;
        CaptureLayer layer;
        layer.quad = true;
        layer.flags = base->layerFlags;
        if (!CaptureImage(s, q->subImage, layer.image))
          return false;
        Pose quad = compose(space, pose(q->pose));
        for (unsigned corner = 0; corner < 4; corner++) {
          Vec point{(corner & 1 ? .5f : -.5f) * q->size.width,
                    (corner & 2 ? -.5f : .5f) * q->size.height, 0};
          auto f = views[eye].fov;
          layer.corners[corner] =
              clip(pose(views[eye].pose), world(quad, point), f.angleLeft,
                   f.angleRight, f.angleDown, f.angleUp);
        }
        layers[eye].push_back(layer);
      }
    } else
      return false;
  }
  // Finish both private GPU images before any readback/acknowledgment.
  ID3D11Texture2D *images[2]{};
  for (unsigned eye = 0; eye < 2; eye++) {
    images[eye] = s.renderer->GameFrame(eye, views[eye].fov, layers[eye]);
    if (!images[eye])
      return false;
  }
  bool captured = true;
  for (unsigned eye = 0; eye < 2; eye++) {
    s.renderer->video.Copy(eye, images[eye]);
    if (s.captureStill) {
      auto path =
          folder + (eye ? L"\\lockpick-right.ppm" : L"\\lockpick-left.ppm");
      captured = s.renderer->Capture(images[eye], path.c_str()) && captured;
    }
  }
  if (captured && s.captureSource != 1)
    Log("capture_game layers=%u eye_layers=%u,%u", c->layerCount,
        unsigned(layers[0].size()), unsigned(layers[1].size()));
  return captured;
}
XrResult SubmitGame(Session &s, const XrFrameEndInfo *c) {
  bool captured = s.renderer && s.captureGame && CaptureGameLayers(s, c);
  auto result = s.api->EndFrame(s.handle, c);
  if (s.renderer)
    s.renderer->video.End(captured && XR_SUCCEEDED(result), 1);
  if (captured && XR_SUCCEEDED(result)) {
    CaptureSource(s, 1);
    if (s.captureStill) {
      DeleteFileW((folder + L"\\lockpick-capture.request").c_str());
      s.captureStill = false;
    }
  } else if (s.captureGame && s.frames % 90 == 0) {
    Log("capture_wait source=game layers=%u copied=%d result=%d", c->layerCount,
        captured, result);
  }
  return result;
}
XrResult XRAPI_CALL EndFrame(XrSession h, const XrFrameEndInfo *c) {
  auto it = sessions.find(h);
  if (it == sessions.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &s = *it->second;
  auto &i = *s.api;
  auto g = Snapshot();
  PollCapture(s);
  s.frames++;
  if (g.menu != s.menu) {
    StopFeedback(s);
    s.menu = g.menu;
    s.positioned = false;
    s.opening = {};
    s.openingTime = 0;
    s.bypassMenu = 0;
    s.interaction.config = {};
    if (auto *file = _wfopen(
            (folder + L"\\TTWVRPhysicalLockpicking.ini").c_str(), L"rb")) {
      char text[8193]{};
      auto count = fread(text, 1, sizeof(text) - 1, file);
      fclose(file);
      s.interaction.config.load(std::string(text, count));
    }
    s.interaction.reset(g.pick);
    Log("xr_menu menu=%p stage=%u", (void *)s.menu, g.stage);
    for (unsigned n = 0; n < c->layerCount; n++) {
      Log("background_layer index=%u type=%d", n, int(c->layers[n]->type));
      if (c->layers[n]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
        auto *p = reinterpret_cast<const XrCompositionLayerProjection *>(
            c->layers[n]);
        for (unsigned v = 0; v < p->viewCount; v++)
          Log("background_projection eye=%u rect=%d,%d,%d,%d array=%u", v,
              p->views[v].subImage.imageRect.offset.x,
              p->views[v].subImage.imageRect.offset.y,
              p->views[v].subImage.imageRect.extent.width,
              p->views[v].subImage.imageRect.extent.height,
              p->views[v].subImage.imageArrayIndex);
      }
    }
  }
  auto fallback = folder + L"\\lockpick-fallback.request";
  bool requested =
      GetFileAttributesW(fallback.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (requested)
    DeleteFileW(fallback.c_str());
  if (((GetAsyncKeyState(VK_F8) & 1) || requested) && g.menu) {
    s.bypassMenu = g.menu;
    Log("fallback requested=F8");
  }
  if (!g.menu || !s.focused || !NativeReady() || s.bypassMenu == g.menu ||
      s.failed || !s.localSpace || !s.viewSpace || !ReadyRenderer(s)) {
    s.interaction.reset(g.pick);
    s.presenting = false;
    StopFeedback(s);
    Neutral();
    return SubmitGame(s, c);
  }
  XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(
          i.LocateSpace(s.viewSpace, s.localSpace, c->displayTime, &head)) ||
      (head.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                             XR_SPACE_LOCATION_POSITION_VALID_BIT)) !=
          (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
           XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
    s.interaction.reset(g.pick);
    s.presenting = false;
    StopFeedback(s);
    Neutral();
    return SubmitGame(s, c);
  }
  if (!s.positioned) {
    auto &config = s.interaction.config;
    s.lock =
        placeLock(pose(head.pose), config.lockDistance, config.lockBelowEyes);
    Log("lock_placement distance=%.3f below_eyes=%.3f wrist_turn=%.1f "
        "holders=%d preparation=%d position=%.3f,%.3f,%.3f upright=1",
        config.lockDistance, config.lockBelowEyes, config.fullTurnWrist,
        config.useHolders, config.requirePreparation, s.lock.p.x, s.lock.p.y,
        s.lock.p.z);
    s.positioned = true;
  }
  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
  li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  li.displayTime = c->displayTime;
  li.space = s.localSpace;
  XrViewState state{XR_TYPE_VIEW_STATE};
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  uint32_t count = 0;
  if (XR_FAILED(i.LocateViews(h, &li, &state, 2, &count, views)) ||
      count != 2 ||
      (state.viewStateFlags & (XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                               XR_VIEW_STATE_POSITION_VALID_BIT)) !=
          (XR_VIEW_STATE_ORIENTATION_VALID_BIT |
           XR_VIEW_STATE_POSITION_VALID_BIT)) {
    s.interaction.reset(g.pick);
    s.presenting = false;
    StopFeedback(s);
    Neutral();
    return SubmitGame(s, c);
  }
  Scene scene{s.lock, s.interaction.pick, g.cylinder};
  scene.targetKind = s.interaction.config.useTargetObject ? g.targetKind : 0;
#ifdef LOCKPICK_TEST_INPUT
  float previewTime = -1;
  if (auto *file =
          _wfopen((folder + L"\\lockpick-target-preview.txt").c_str(), L"rb")) {
    unsigned kind = 0;
    float t = 0;
    if (fscanf(file, "%u %f", &kind, &t) == 2 && kind <= targetKinds &&
        std::isfinite(t)) {
      scene.targetKind = kind;
      previewTime = std::clamp(t, 0.f, 10.f);
    }
    fclose(file);
  }
#endif
  const auto *object = s.renderer->Target(scene.targetKind);
  s.interaction.targetBounds = object ? object->bounds : TargetBounds{};
  float openDt =
      s.openingTime ? float(double(c->displayTime - s.openingTime) * 1e-9) : 0;
  s.openingTime = c->displayTime;
  bool wasOpening = s.opening.opened;
  s.opening.Step(g.menu, object ? scene.targetKind : 0, g.stage, g.health,
                 g.cylinder, openDt);
  if (!wasOpening && s.opening.opened)
    Log("target_opening kind=%u native_stage=%u cylinder=%.3f",
        scene.targetKind, g.stage, g.cylinder);
  scene.targetOpenTime = s.opening.time;
#ifdef LOCKPICK_TEST_INPUT
  if (previewTime >= 0)
    scene.targetOpenTime = previewTime;
#endif
  if (object && s.frames % 90 == 0)
    Log("target_present kind=%u opening=%d time=%.3f style=%u",
        scene.targetKind, s.opening.opened, scene.targetOpenTime,
        unsigned(object->bounds.style));
  Control out;
  out.menu = g.menu;
  out.stamp = GetTickCount64();
  bool tracked = Input(s, c->displayTime, out, scene, g);
  if (!tracked) {
    s.interaction.reset(g.pick);
    s.presenting = false;
    StopFeedback(s);
    Neutral();
    return SubmitGame(s, c);
  }
  if (s.bypassMenu == g.menu) {
    s.interaction.reset(g.pick);
    s.presenting = false;
    StopFeedback(s);
    Neutral();
    return SubmitGame(s, c);
  }
  BackgroundView backdrop[2];
  if (scene.config.useGameBackground)
    for (unsigned n = 0; n < c->layerCount; n++) {
      if (c->layers[n]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
        continue;
      auto *p =
          reinterpret_cast<const XrCompositionLayerProjection *>(c->layers[n]);
      if (p->viewCount != 2)
        continue;
      XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
      auto flags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                   XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
      if (XR_FAILED(i.LocateSpace(p->space, s.localSpace, c->displayTime,
                                  &location)) ||
          (location.locationFlags & flags) != unsigned(flags))
        continue;
      bool valid = true;
      for (unsigned eye = 0; eye < 2; eye++) {
        auto &sub = p->views[eye].subImage;
        auto &bg = backdrop[eye];
        bg = s.renderer->background.View(sub.swapchain, sub.imageArrayIndex,
                                         s.frames);
        bg.rect = sub.imageRect;
        bg.fov = p->views[eye].fov;
        Pose source = compose(pose(location.pose), pose(p->views[eye].pose));
        bg.rotation = multiply(inverse(source.q), pose(views[eye].pose).q);
        valid &=
            bg.image && bg.rect.offset.x >= 0 && bg.rect.offset.y >= 0 &&
            bg.rect.extent.width > 0 && bg.rect.extent.height > 0 &&
            uint64_t(bg.rect.offset.x) + bg.rect.extent.width <= bg.width &&
            uint64_t(bg.rect.offset.y) + bg.rect.extent.height <= bg.height &&
            length(source.p - pose(views[eye].pose).p) < .015f;
      }
      if (valid)
        break;
      backdrop[0] = {};
      backdrop[1] = {};
    }
  if (s.frames % 180 == 0)
    Log("background_frame frame=%u stereo=%d", s.frames,
        backdrop[0].image && backdrop[1].image);
  XrCompositionLayerProjectionView projectionViews[2] = {
      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  bool rendered = true;
  // Latch once for the entire stereo pair. A request arriving between eyes
  // must wait for the next frame, never combine an old left with a new right.
  std::wstring request = folder + L"\\lockpick-capture.request";
  bool capture = s.captureStill;
  bool captured = capture;
  for (unsigned eye = 0; eye < 2; eye++) {
    auto &e = s.eyes[eye];
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo ac{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo re{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(i.AcquireSwapchainImage(e.chain, &ac, &index))) {
      rendered = false;
      break;
    }
    if (XR_FAILED(i.WaitSwapchainImage(e.chain, &wi))) {
      s.failed = true;
      rendered = false;
      break;
    }
    bool drawn = index < e.images.size() &&
                 s.renderer->Draw(e.images[index].texture, views[eye], scene,
                                  backdrop[eye]);
    if (drawn)
      s.renderer->video.Copy(eye, e.images[index].texture);
    if (drawn && capture) {
      auto path =
          folder + (eye ? L"\\lockpick-right.ppm" : L"\\lockpick-left.ppm");
      captured = s.renderer->Capture(e.images[index].texture, path.c_str()) &&
                 captured;
    }
    auto released = i.ReleaseSwapchainImage(e.chain, &re);
    if (!drawn || XR_FAILED(released)) {
      rendered = false;
      break;
    }
    auto &pv = projectionViews[eye];
    pv.pose = views[eye].pose;
    pv.fov = views[eye].fov;
    pv.subImage.swapchain = e.chain;
    pv.subImage.imageRect.extent = {int32_t(Renderer::size),
                                    int32_t(Renderer::size)};
  }
  if (!rendered) {
    s.failed = true;
    Neutral();
    StopFeedback(s);
    Log("render_bypass frame_failed=1");
    return SubmitGame(s, c);
  }
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  projection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  projection.space = s.localSpace;
  projection.viewCount = 2;
  projection.views = projectionViews;
  std::vector<const XrCompositionLayerBaseHeader *> layers;
  if (c->layerCount)
    layers.assign(c->layers, c->layers + c->layerCount);
  layers.push_back(
      reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection));
  auto copy = *c;
  copy.layers = layers.data();
  copy.layerCount = layers.size();
  auto r = i.EndFrame(h, &copy);
  s.renderer->video.End(XR_SUCCEEDED(r), 2);
  if (XR_SUCCEEDED(r) && s.captureGame) CaptureSource(s, 2);
  if (captured && XR_SUCCEEDED(r)) {
    DeleteFileW(request.c_str());
    s.captureStill = false;
  }
  out.valid = XR_SUCCEEDED(r) && tracked;
  s.presenting = out.valid;
  if (!out.valid)
    StopFeedback(s);
  {
    std::lock_guard guard(sharedMutex);
    control = out;
  }
  if (s.frames % 90 == 0)
    Log("stereo_lock rendered=1 eyes=2 result=%d tracked=%d stage=%u pick=%.2f "
        "native_pick=%.2f cylinder=%.2f health=%.3f torque=%.2f",
        r, tracked, g.stage, s.interaction.pick, g.pick, g.cylinder, g.health,
        out.torque);
  return r;
}
XrResult XRAPI_CALL Destroy(XrInstance h) {
  auto it = instances.find(h);
  if (it == instances.end())
    return XR_ERROR_HANDLE_INVALID;
  auto i = it->second;
  for (auto iter = sessions.begin(); iter != sessions.end();)
    if (iter->second->api == i) {
      Clean(*iter->second);
      iter = sessions.erase(iter);
    } else
      ++iter;
  Neutral();
  if (i->set)
    i->DestroyActionSet(i->set);
#ifdef LOCKPICK_TEST_INPUT
  if (i->probeSet)
    i->DestroyActionSet(i->probeSet);
#endif
  PFN_xrDestroyInstance destroy = i->DestroyInstance;
  if (!destroy && !Resolve(*i, "xrDestroyInstance", destroy))
    return XR_ERROR_FUNCTION_UNSUPPORTED;
  auto r = destroy(h);
  instances.erase(it);
  return r;
}
XrResult XRAPI_CALL Get(XrInstance h, const char *n, PFN_xrVoidFunction *f) {
  if (!n || !f)
    return XR_ERROR_VALIDATION_FAILURE;
  auto it = instances.find(h);
  if (it == instances.end())
    return XR_ERROR_HANDLE_INVALID;
  auto &i = *it->second;
  if (!i.ready && strcmp(n, "xrDestroyInstance"))
    return i.get(h, n, f);
  if (!strcmp(n, "xrGetInstanceProcAddr")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Get);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrCreateSwapchain")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(CreateAppChain);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrDestroySwapchain")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(DestroyAppChain);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrAcquireSwapchainImage")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(AcquireAppImage);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrWaitSwapchainImage")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(WaitAppImage);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrReleaseSwapchainImage")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(ReleaseAppImage);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrCreateSession")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(CreateSession);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrDestroySession")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(DestroySession);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrEndFrame")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(EndFrame);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrDestroyInstance")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Destroy);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrSuggestInteractionProfileBindings")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Suggest);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrAttachSessionActionSets")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Attach);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrSyncActions")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Sync);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrApplyHapticFeedback")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(AppHaptic);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrStopHapticFeedback")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(AppStopHaptic);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrCreateActionSet")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(AppActionSet);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrGetActionStateFloat")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(StateFloat);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrGetActionStateBoolean")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(StateBoolean);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrGetActionStateVector2f")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(StateVector);
    return XR_SUCCESS;
  }
  if (!strcmp(n, "xrPollEvent")) {
    *f = reinterpret_cast<PFN_xrVoidFunction>(Poll);
    return XR_SUCCESS;
  }
  return i.get(h, n, f);
}
XrResult XRAPI_CALL Create(const XrInstanceCreateInfo *c,
                           const XrApiLayerCreateInfo *l, XrInstance *h) {
  if (!l || !l->nextInfo || !h)
    return XR_ERROR_INITIALIZATION_FAILED;
  auto next = *l;
  next.nextInfo = l->nextInfo->next;
  auto r = l->nextInfo->nextCreateApiLayerInstance(c, &next, h);
  if (XR_FAILED(r))
    return r;
  InitFolder();
  auto i = std::make_shared<Instance>();
  i->handle = *h;
  i->get = l->nextInfo->nextGetInstanceProcAddr;
  bool ready = true;
  ready = Resolve(*i, "xrCreateSession", i->CreateSession) && ready;
  ready = Resolve(*i, "xrDestroySession", i->DestroySession) && ready;
  ready = Resolve(*i, "xrDestroyInstance", i->DestroyInstance) && ready;
  ready = Resolve(*i, "xrEndFrame", i->EndFrame) && ready;
  ready = Resolve(*i, "xrCreateActionSet", i->CreateActionSet) && ready;
  ready = Resolve(*i, "xrCreateAction", i->CreateAction) && ready;
  ready = Resolve(*i, "xrDestroyActionSet", i->DestroyActionSet) && ready;
  ready = Resolve(*i, "xrSuggestInteractionProfileBindings",
                  i->SuggestInteractionProfileBindings) &&
          ready;
  ready = Resolve(*i, "xrPathToString", i->PathToString) && ready;
  ready = Resolve(*i, "xrStringToPath", i->StringToPath) && ready;
  ready =
      Resolve(*i, "xrAttachSessionActionSets", i->AttachSessionActionSets) &&
      ready;
  ready = Resolve(*i, "xrSyncActions", i->SyncActions) && ready;
  ready = Resolve(*i, "xrCreateActionSpace", i->CreateActionSpace) && ready;
  ready =
      Resolve(*i, "xrCreateReferenceSpace", i->CreateReferenceSpace) && ready;
  ready = Resolve(*i, "xrDestroySpace", i->DestroySpace) && ready;
  ready = Resolve(*i, "xrLocateSpace", i->LocateSpace) && ready;
  ready = Resolve(*i, "xrLocateViews", i->LocateViews) && ready;
  ready = Resolve(*i, "xrGetActionStateFloat", i->GetActionStateFloat) && ready;
  ready =
      Resolve(*i, "xrGetActionStateBoolean", i->GetActionStateBoolean) && ready;
  ready = Resolve(*i, "xrGetActionStatePose", i->GetActionStatePose) && ready;
  ready = Resolve(*i, "xrGetActionStateVector2f", i->GetActionStateVector2f) &&
          ready;
  ready = Resolve(*i, "xrApplyHapticFeedback", i->ApplyHapticFeedback) && ready;
  ready = Resolve(*i, "xrStopHapticFeedback", i->StopHapticFeedback) && ready;
  ready = Resolve(*i, "xrCreateSwapchain", i->CreateSwapchain) && ready;
  ready = Resolve(*i, "xrDestroySwapchain", i->DestroySwapchain) && ready;
  ready = Resolve(*i, "xrEnumerateSwapchainFormats",
                  i->EnumerateSwapchainFormats) &&
          ready;
  ready =
      Resolve(*i, "xrEnumerateSwapchainImages", i->EnumerateSwapchainImages) &&
      ready;
  ready =
      Resolve(*i, "xrAcquireSwapchainImage", i->AcquireSwapchainImage) && ready;
  ready = Resolve(*i, "xrWaitSwapchainImage", i->WaitSwapchainImage) && ready;
  ready =
      Resolve(*i, "xrReleaseSwapchainImage", i->ReleaseSwapchainImage) && ready;
  ready = Resolve(*i, "xrPollEvent", i->PollEvent) && ready;
  i->ready = ready;
  i->inputs = ready && CreateInputs(*i);
  instances[*h] = i;
  Log("xr_layer_active inputs=%d app=%s", i->inputs,
      c->applicationInfo.applicationName);
  return r;
}
} // namespace
} // namespace locks
extern "C" __declspec(dllexport) XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo *l, const char *,
                                   XrNegotiateApiLayerRequest *r) {
  if (!l || !r || l->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
      l->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
      l->structSize != sizeof(*l) ||
      r->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
      r->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
      r->structSize != sizeof(*r) ||
      l->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
      l->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
    return XR_ERROR_INITIALIZATION_FAILED;
  r->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
  r->layerApiVersion =
      std::min<XrVersion>(XR_CURRENT_API_VERSION, l->maxApiVersion);
  if (r->layerApiVersion < l->minApiVersion)
    return XR_ERROR_INITIALIZATION_FAILED;
  r->getInstanceProcAddr = locks::Get;
  r->createApiLayerInstance = locks::Create;
  return XR_SUCCESS;
}
