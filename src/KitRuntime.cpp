// SPDX-License-Identifier: GPL-3.0-only
#include "KitRuntime.h"
#include "Kit.h"
#include "HostInput.h"
#include "WorldPin.h"
#include "WorldLock.h"
#include "PinFavourite.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <wincrypt.h>

namespace locks {
namespace {
struct Element {
  union {
    double number;
    void *form;
  } data{};
  unsigned char type = 0;
};
struct Scripts {
  bool(__cdecl *call)(void *, void *, void *, Element *, uint8_t, ...);
  void *params, *extract, *format, *alt;
  void *(__cdecl *compile)(const char *);
};
Scripts *api = nullptr;
void *slotQuery = nullptr, *playQuery = nullptr, *stockQuery = nullptr,
     *drop = nullptr;
void *assignPin = nullptr, *holsterWeapon = nullptr;
std::atomic<void *> closeLockMenu{nullptr};
std::atomic<uintptr_t> closeupExitRequest{0};
std::atomic<unsigned> occupiedHands{0};
// These three TTWVR callers read only the per-hand loot preference from the
// config pointer. The VR settings screen continues to use the real config.
struct LootPreference {
  unsigned char unused[0x50]{};
  unsigned hands = 0;
};
static_assert(offsetof(LootPreference, hands) == 0x50);
using ReadPreference = const LootPreference *(__cdecl *)();
ReadPreference gamePreference = nullptr;
using CanShowWrist = bool(__cdecl *)();
CanShowWrist gameCanShowWrist = nullptr;
bool __cdecl kitCanShowWrist() {
  // Only TTWVR's automatic wrist-panel update asks through this call site.
  bool allowed = gameCanShowWrist();
  unsigned held = occupiedHands.load(std::memory_order_relaxed);
  static const bool trace = GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) > 0;
  if (trace && allowed) {
    static thread_local unsigned previous = ~0u;
    if (previous != held) {
      previous = held;
      Log("kit_auto_wrist occupied=%u allowed=%d", held, !held);
    }
  }
  return allowed && !held;
}
const LootPreference *__cdecl kitLootPreference() {
  auto config = gamePreference();
  unsigned occupied = occupiedHands.load(std::memory_order_relaxed);
  if (!occupied || !config) return config;
  unsigned value = lootHandsWithKit(config->hands, occupied);
  static constexpr LootPreference choices[]{{{},0}, {{},1}, {{},2}, {{},3}};
  return value < 4 ? &choices[value] : config;
}
void *visual[2][2][2]{};
uintptr_t host = 0;
struct Layout {
  unsigned bones, hands, bindings, positions, enabled, ready, units;
};
Layout layout{};
// TTWVR computes all eight world-space centers for its holster hit tests,
// including body-relative slots when the avatar skeleton is unavailable.
using ResolveCenter = Vec *(__cdecl *)(Vec *, const float *, const Vec *, const Vec *, float);
ResolveCenter resolveCenter = nullptr;
std::mutex centerMutex;
std::array<Vec, 8> nativeCenters{};
ULONGLONG centersTime = 0;
std::atomic<bool> trackCenters{false};
Vec *__cdecl observeCenter(Vec *out, const float *position, const Vec *origin,
                           const Vec *angles, float units) {
  auto result = resolveCenter(out, position, origin, angles, units);
  if (!trackCenters || !result)
    return result;
  const uintptr_t offset = uintptr_t(position) - (host + layout.positions);
  if (offset >= 8 * 16 || offset % 16)
    return result;
  // This verified call site walks the complete slot table in order.
  thread_local std::array<Vec, 8> batch;
  batch[offset / 16] = *result;
  if (offset == 7 * 16) {
    std::lock_guard guard(centerMutex);
    nativeCenters = batch;
    centersTime = GetTickCount64();
  }
  return result;
}
template <class T> T read(uintptr_t p) {
  return Readable((void *)p, sizeof(T)) ? *reinterpret_cast<const T *>(p) : T{};
}
bool belongsToSkeleton(uintptr_t node, uintptr_t skeleton) {
  for (unsigned level = 0; node && level < 14; ++level) {
    if (node == skeleton)
      return true;
    node = read<uintptr_t>(node + 0x18);
  }
  return false;
}
struct Transform {
  float r[9];
  Vec p;
  float scale;
  Vec point(Vec v) const {
    return p + Vec{r[0] * v.x + r[1] * v.y + r[2] * v.z,
                   r[3] * v.x + r[4] * v.y + r[5] * v.z,
                   r[6] * v.x + r[7] * v.y + r[8] * v.z} *
                   scale;
  }
};
std::string hashFile(HMODULE m) {
  wchar_t path[32768]{};
  if (!GetModuleFileNameW(m, path, 32768))
    return {};
  FILE *file = _wfopen(path, L"rb");
  if (!file)
    return {};
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                 CRYPT_VERIFYCONTEXT) &&
            CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
  std::array<BYTE, 32768> buf;
  size_t n;
  while (ok && (n = fread(buf.data(), 1, buf.size(), file)))
    ok = CryptHashData(hash, buf.data(), DWORD(n), 0);
  BYTE digest[32];
  DWORD size = sizeof(digest);
  ok = ok && !ferror(file) &&
       CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) && size == 32;
  if (hash)
    CryptDestroyHash(hash);
  if (provider)
    CryptReleaseContext(provider, 0);
  fclose(file);
  std::string out;
  if (ok)
    for (BYTE d : digest) {
      const char *hex = "0123456789abcdef";
      out += hex[d >> 4];
      out += hex[d & 15];
    }
  return out;
}
bool init() {
  auto m = GetModuleHandleW(L"TTWVR.dll");
  if (!m || !api || !api->compile || !api->call)
    return false;
  auto digest = hashFile(m);
  // Read-only bone and slot layouts shared with the private holster
  // calibration.
  const bool version117 = digest ==
      "119a085a5b0101f0618b8e076a421b4b89e30d72d9b6eae874f4e5af7bd3fb27" ||
      digest ==
      "ae7dc0d7ba94651af0ccd729122f623d3dbdf0b85c6c787838c04bbc721135d6";
  if (digest == "503266c4f4c2350b357c8c15021d8bde28973254745d9302b8e3cfde66489a27" || digest ==
      "63712922499b2155a3d2c6f54abbdfc1eaf95d08712b937c6276d6a8ae0d774d")
    layout = {286039420, 291467216, 285330768, 291471160,
              291471288, 286043596, 286058864};
  else if (version117)
    layout = {286154492, 291584816, 285445496, 291588760,
              291588888, 286158668, 286173944};
  else {
    Log("kit_disabled unknown_holster_layout=1");
    return false;
  }
  InstallHostInput(uintptr_t(m), version117);
  InstallHostXrSubmission(uintptr_t(m), version117);
  host = uintptr_t(m) - 0x10000000;
  slotQuery = api->compile("begin Function {}\nSetFunctionValue (Call "
                           "(CompileScript \"TTWVRLockKit/Slot.txt\"))\nend");
  stockQuery = api->compile("begin Function {}\nSetFunctionValue "
                            "(player.GetItemCount 0000000A)\nend");
  playQuery = api->compile(
      "begin Function {}\nSetFunctionValue (GetActiveMenuMode)\nend");
  holsterWeapon = api->compile("begin Function {}\nif player.IsWeaponOut\n"
                               "player.SetWeaponOut 0\nendif\nend");
  drop = api->compile("int beforePins\nint afterPins\nbegin Function {}\n"
                      "let beforePins := player.GetItemCount 0000000A\n"
                      "if beforePins > 0\n player.Drop 0000000A 1\nendif\n"
                      "let afterPins := player.GetItemCount 0000000A\n"
                      "SetFunctionValue (beforePins - afterPins)\nend");
  for (int hand = 0; hand < 2; hand++)
    for (int tool = 0; tool < 2; tool++)
      for (int show = 0; show < 2; show++) {
        char source[320];
        snprintf(
            source, sizeof(source),
            "begin Function {}\nSetFunctionValue (AttachModel player %d "
            "\"Bip01 %c Hand|TTWVRPhysicalLockpicking\\kit-%d-%d.nif\")\nend",
            show ? 3 : 2, hand ? 'R' : 'L', hand, tool);
        visual[hand][tool][show] = api->compile(source);
        if (!visual[hand][tool][show])
          return false;
      }
  closeLockMenu = api->compile(
      "begin Function {}\nClickMenuButton \"LockPickMenu#10\" 1\nend");
  assignPin =
      api->compile("int slot\nbegin Function {slot}\nSetFunctionValue (Call "
                   "(CompileScript \"TTWVRLockKit/AssignPin.txt\") slot)\nend");
  if (!slotQuery || !stockQuery || !playQuery || !drop || !assignPin || !closeLockMenu)
    return false;
  const uintptr_t site = host + (version117 ? 0x102288c3 : 0x10221573);
  const uintptr_t target = host + (version117 ? 0x10226840 : 0x1021f4f0);
  resolveCenter = reinterpret_cast<ResolveCenter>(target);
  const NativeCall centerHook{site, target, reinterpret_cast<void *>(observeCenter)};
  const bool poseInstalled = InstallWorldPinPose(host + (version117 ? 0x10218c85 : 0x10211945),
                                               host + (version117 ? 0x101f5820 : 0x101ee4e0),
                                               host + (version117 ? 0x10205405 : 0x101fe0c5),
                                               host + (version117 ? 0x10215fe0 : 0x1020eca0));
  Log("world_pin_post_animation installed=%d",poseInstalled);
  if (!poseInstalled) return false;
  gamePreference = reinterpret_cast<ReadPreference>(host + (version117 ? 0x1000b235 : 0x1000b172));
  const unsigned lootSites117[]{0x1012713b, 0x10127300, 0x1013b32a};
  const unsigned lootSites116[]{0x101262db, 0x101264a0, 0x1013a4aa};
  NativeCall lootHooks[3];
  for (unsigned i = 0; i < 3; ++i)
    lootHooks[i] = {host + (version117 ? lootSites117[i] : lootSites116[i]),
                    uintptr_t(gamePreference), reinterpret_cast<void *>(kitLootPreference)};
  Log("kit_hand_loot_filter installed=%d", InstallVerifiedCalls(lootHooks, 3));
  gameCanShowWrist = reinterpret_cast<CanShowWrist>(host + (version117 ? 0x1000255e : 0x10002545));
  const NativeCall wristHook{host + (version117 ? 0x10101aa6 : 0x10100fa6),
                             uintptr_t(gameCanShowWrist), reinterpret_cast<void *>(kitCanShowWrist)};
  Log("kit_auto_wrist_filter installed=%d", InstallVerifiedCalls(&wristHook, 1));
  Log("kit_native_centers installed=%d", InstallVerifiedCalls(&centerHook, 1));
  Log("bobby_pin_inventory_favourite installed=%d",
      InstallPinFavourite(uintptr_t(GetModuleHandleW(nullptr))));
  Log("bobby_pin_wrist_favourite installed=%d", InstallWristPinFavourite(uintptr_t(m), version117));
  return true;
}
int run(void *script) {
  Element out;
  if (!script || !api->call(script, nullptr, nullptr, &out, 0) ||
      out.type != 1 || !std::isfinite(out.data.number))
    return -1;
  return int(out.data.number);
}
struct Mount {
  Vec center;
  float radius = 0;
};
std::array<Mount, 8> previousMounts{};
ULONGLONG lastLocated = 0;
uintptr_t mountMenu = 0;
void locateHolsters(GameState &g) {
  // A paused lock menu can suspend TTWVR's avatar updates. Retain world
  // coordinates, then transform against the current tracking origin per frame.
  const auto now = GetTickCount64();
  if (g.menu && g.menu != mountMenu && now - lastLocated > 250)
    previousMounts = {};
  if (!g.menu)
    previousMounts = {};
  mountMenu = g.menu;
  if (g.menu)
    for (unsigned s = 0; s < 8; s++)
      if (previousMounts[s].radius > 0 &&
          WorldToTracking(previousMounts[s].center, g.kitCenters[s]))
        g.kitRadii[s] = previousMounts[s].radius;

  const bool bodyBonesReady = read<unsigned char>(host + layout.ready) != 0;
  float unit = read<float>(host + layout.units);
  if (!(unit >= 10 && unit <= 200))
    return;
  g.worldUnitsPerMeter = unit;
  std::array<Vec, 8> centers;
  bool fresh;
  {
    std::lock_guard guard(centerMutex);
    fresh = centersTime && now < centersTime + 180;
    centers = nativeCenters;
  }
  // The Pip-Boy can clear the full-body flag while the live player hands
  // and belt still belong to the same avatar. Ignore detached cached bones.
  uintptr_t skeleton = 0;
  if (!fresh && !bodyBonesReady) {
    const auto pelvis = read<uintptr_t>(host + layout.bones);
    if (pelvis) {
      skeleton = read<uintptr_t>(pelvis + 0x18);
      if (!belongsToSkeleton(read<uintptr_t>(host + layout.hands), skeleton) &&
          !belongsToSkeleton(read<uintptr_t>(host + layout.hands + 4), skeleton))
        skeleton = 0;
    }
  }
  int boneIndex[]{0, 3, 10, 11, 12, 13};
  for (int s = 0; s < 8; s++) {
    if (!read<unsigned char>(host + layout.enabled + s))
      continue;
    auto bind = host + layout.bindings + s * 20;
    if (read<unsigned char>(bind + 16))
      continue;
    float radius = read<float>(host + layout.positions + 16 * s + 12) * unit;
    if (!(radius >= .049f * unit && radius <= .451f * unit))
      continue;
    if (fresh && finite(centers[s]) &&
        WorldToTracking(centers[s], g.kitCenters[s])) {
      g.kitRadii[s] = radius / 70.f;
      previousMounts[s] = {centers[s], g.kitRadii[s]};
      lastLocated = now;
      continue;
    }
    int anchor = read<int>(bind);
    if (anchor < 0 || anchor >= 10)
      continue;
    uintptr_t bone = 0;
    if (anchor < 6) {
      bone = read<uintptr_t>(host + layout.bones + 4 * boneIndex[anchor]);
      if (!bodyBonesReady && (!skeleton || !belongsToSkeleton(bone, skeleton)))
        continue;
    } else {
      // TTWVR updates the rendered hand chain independently of the body cache.
      auto hand = read<uintptr_t>(host + layout.hands + 4 * (anchor % 2));
      bone = hand ? read<uintptr_t>(hand + 0x18) : 0;
      if (anchor < 8 && bone)
        bone = read<uintptr_t>(bone + 0x18);
    }
    if (!bone)
      continue;
    auto t = read<Transform>(bone + 0x68);
    if (!(t.scale > 0 && t.scale < 10))
      continue;
    Vec center = t.point(read<Vec>(bind + 4));
    if (!finite(center))
      continue;
    if (WorldToTracking(center, g.kitCenters[s])) {
      g.kitRadii[s] = radius / 70.f;
      previousMounts[s] = {center, g.kitRadii[s]};
      lastLocated = now;
    }
  }
}
KitRoles roles;
float pinHealth = 1; // Current pin, including when returned to the shared holster.
int shown[2]{-1, -1}, slot = 0, play = 0;
unsigned interrupted = 0, armed = 0;
uint64_t lastReliable[2]{};
std::atomic<KitInputSample> sample{KitInputSample{}};
uintptr_t lastMenu = 0;
ULONGLONG nextQuery = 0, nextHolster = 0;
bool initialized = false, available = false, clean = false;
void show(int h, int tool, bool value) {
  if (!value) BeforeWorldToolRemoval(h,tool);
  auto ok = run(visual[h][tool][value]);
  Log("kit_visual hand=%d tool=%d show=%d attach_result=%d", h, tool, value,
      ok);
  if (value && ok == 1) {
    shown[h] = tool;
    occupiedHands.fetch_or(1u << h, std::memory_order_relaxed);
  }
  if (!value) {
    shown[h] = -1;
    occupiedHands.fetch_and(~(1u << h), std::memory_order_relaxed);
  }
}
void clearVisuals() {
  for (int h = 0; h < 2; h++)
    if (shown[h] >= 0)
      show(h, shown[h], false);
}
} // namespace
void InitKit(void *scripts) { api = static_cast<Scripts *>(scripts); }
void ResetKit() {
  occupiedHands = 0;
  nextHolster = 0;
  pinHealth = 1;
  ResetWorldPin();
  trackCenters = false;
  {
    std::lock_guard guard(centerMutex);
    centersTime = 0;
  }
  EnablePinFavourite(false);
  TakePinFavourite();
  roles = {};
  armed = interrupted = 0;
  sample.store(KitInputSample{});
  lastMenu = 0;
  slot = 0;
  nextQuery = 0;
  clean = true;
  previousMounts = {};
  mountMenu = 0;
  lastLocated = 0;
}
unsigned KitReach(const GameState &g, const Vec hands[2], unsigned valid) {
  if (!g.kitSlot || g.kitSlot>8) return 0;
  auto nearest = [&](Vec point) {
    unsigned slot = 0;
    float best = 1e20f;
    for (unsigned s=0; s<8; ++s) {
      const float distance=length(point-g.kitCenters[s]);
      if (g.kitRadii[s]>0 && distance<=g.kitRadii[s] && distance<best) {
        best=distance; slot=s+1;
      }
    }
    return slot;
  };
  unsigned hits=0;
  for (unsigned h=0; h<2; ++h) {
    if (!(valid & (1u<<h))) continue;
    // Use the same visible hand that reaches for the sphere. The controller
    // remains useful when the avatar is blending between animations.
    if (((g.kitFingerPadsValid & (1u<<h)) && nearest(g.kitFingerPads[h])==g.kitSlot) ||
        ((g.kitPalmsValid & (1u<<h)) && nearest(g.kitPalms[h])==g.kitSlot) ||
        nearest(hands[h])==g.kitSlot)
      hits |= 1u<<h;
  }
  return hits;
}
void KitButtons(unsigned down, unsigned valid, unsigned inRange, int first, unsigned uncertainRelease) {
  unsigned bits = down | (valid << 2) | (inRange << 4) | (unsigned(first == 1) << 6);
  sample.store(KitInputSample::make(GetTickCount64(), bits, uncertainRelease));
}
bool RequestCloseupExit(uintptr_t menu) {
  if (!menu || !closeLockMenu.load()) return false;
  closeupExitRequest.store(menu);
  return true;
}
bool DismissLockMenu(const GameState &g) {
  const uintptr_t menu = g.menu;
  if (const auto requested = closeupExitRequest.exchange(0); requested && requested == menu && g.stage != 7) {
    run(closeLockMenu);
    Log("closeup_exit menu=%08x source=right_b", unsigned(menu));
    return true;
  }
  // A scripted Activate can bypass ShowOff's pre-activation event. Use the
  // native Exit button before publishing this menu to the VR workspace.
  static uintptr_t pending = 0;
  static ULONGLONG retryAt = 0, started = 0;
  if (!menu) {
    pending = 0;
    return false;
  }
  const auto now = GetTickCount64();
  const auto latest = sample.load();
  const unsigned input = latest.buttons();
  const bool drawing = latest.fresh(now) &&
      (input & (input >> 2) & (input >> 4) & armed & 3u);
  if (!available || slot < 1 || slot > 8 || lastMenu || (shown[0] < 0 && shown[1] < 0 && !drawing && pending != menu))
    return false;
  // If a UI replacement has removed the Exit tile, let the ordinary menu
  // handle input again rather than trapping the player in it.
  if (pending == menu && now - started > 1200) return false;
  if (pending != menu || now >= retryAt) {
    if (pending != menu) {
      started=now;
      Log("world_kit_cancel_activation drawn=%u drawing=%d",roles.drawn,drawing);
    }
    pending=menu;
    retryAt=now+100;
    run(closeLockMenu);
  }
  return true;
}
void TickKit(GameState &g) {
  if (!initialized) {
    initialized = true;
    available = init();
  }
  if (!available)
    return;
  if (const unsigned requested = TakePinFavourite()) {
    Element result;
    const bool assigned =
        api->call(assignPin, nullptr, nullptr, &result, 1, requested) &&
        result.type == 1 && result.data.number == requested;
    Log("bobby_pin_inventory_favourite slot=%u assigned=%d", requested, assigned);
    nextQuery = 0;
  }
  if (clean) {
    // A load can replace player 3D or restore a JIP attachment map.
    for (int h = 0; h < 2; h++)
      for (int t = 0; t < 2; t++)
        run(visual[h][t][0]);
    shown[0] = shown[1] = -1;
    clean = false;
  }
  const auto now = GetTickCount64();
  const auto latest = sample.load();
  const unsigned state = latest.buttons();
  const unsigned released = (state >> 2) & ~state & 3u;
  // A popup or wheel edit can arrive between the regular queries. Check the
  // same input sample that could cause a real inventory drop this tick.
  const bool releasingPin = latest.fresh(now) &&
      ((shown[0] == 0 && (released & 1u)) || (shown[1] == 0 && (released & 2u)));
  if (now >= nextQuery || g.menu != lastMenu || releasingPin) {
    int next = run(slotQuery);
    EnablePinFavourite(next >= 0);
    play = run(playQuery);
    if (next != slot) {
      ResetWorldLock(false);
      clearVisuals();
      roles = {};
      armed = interrupted = 0;
      slot = next;
      trackCenters = slot >= 1 && slot <= 8;
      previousMounts = {};
      lastLocated = 0;
      Log("kit_binding slot=%d active_menu=%d", slot, play);
    }
    nextQuery = now + 200;
  }
  g.kitSlot = (slot > 0 && slot <= 8) ? unsigned(slot) : 0;
  uintptr_t hands[2]{read<uintptr_t>(host+layout.hands), read<uintptr_t>(host+layout.hands+4)};
  if (!g.kitSlot) {
    WorldPinFrame(g,hands,-1,0,0,0,0,-1,pinHealth);
    return;
  }
  locateHolsters(g);
  if (g.menu || lastMenu) {
    if (g.menu != lastMenu) {
      if (g.menu) {
        auto previousGame=Snapshot();
        g.kitOpening=previousGame.kitOpening;
        g.kitTwist=previousGame.kitTwist;
        // A resting pin goes back to the kit as this interaction ends.
        // A close-up configured for the belt must start with that hand free.
        if (HasParkedPin()) roles.put(0);
      }
      WorldPinFrame(g,hands,-1,0,0,0,0,-1,pinHealth);
      clearVisuals();
      if (!g.menu) {
        roles = {};
        armed = interrupted = 0;
      }
    }
    lastMenu = g.menu;
    g.kitPinHand = roles.pinHand;
    g.kitDrawn = roles.drawn;
    if (g.menu)
      return;
  }
  // HUDMainMenu or no active menu. Suspend grabs in the Pip-Boy and pause
  // screens.
  bool gameplay = (play == 0 || play == 1004);
  g.kitGameplay = gameplay;
  unsigned valid = (state >> 2) & 3, down = state & 3,
           inRange = (state >> 4) & 3;
  if (!gameplay) {
    WorldPinFrame(g,hands,-1,0,0,0,0,-1,pinHealth);
    clearVisuals();
    roles = {};
    armed = interrupted = 0;
    return;
  }
  if (!latest.fresh(now))
    down = valid = inRange = 0;
  const unsigned pressed = down & armed & valid;
  if (pressed)
    Log("world_kit_press hands=%u valid=%u touching_holster=%u slot=%d previous_tools=%u",
        pressed,valid,inRange,slot,roles.drawn);
  for (int index = 0; index < 2; index++) {
    int h = (index + int((state >> 6) & 1)) % 2;
    unsigned bit = 1u << h;
    if (!(valid & bit)) {
      interrupted |= bit;
      // Pause contact immediately. A sustained disconnect returns only this
      // hand's tool to the kit without spending a pin.
      if (shown[h] >= 0 && now >= lastReliable[h] && now - lastReliable[h] >= 500) {
        int t = shown[h];
        show(h, t, false);
        roles.put(t);
      }
      armed &= ~bit;
      continue;
    }
    lastReliable[h] = latest.time();
    if (!(down & bit)) {
      if (shown[h] >= 0) {
        int t = shown[h];
        const bool intentional = !((interrupted | latest.uncertainRelease()) & bit);
        const bool parked = t == 0 && intentional && ParkWorldPin(h);
        if (t == 0 && intentional && !parked && !(inRange & bit) && run(drop) == 1)
          pinHealth = 1;
        show(h, t, false);
        if (!parked) roles.put(t);
      }
      armed |= bit;
    }
    interrupted &= ~bit;
    if ((down & bit) && (armed & bit)) {
      armed &= ~bit;
      if (shown[h] < 0 && NearParkedPin(h, g.worldUnitsPerMeter)) {
        show(h, 0, true);
        if (shown[h] == 0) {
          if (RegrabWorldPin(h)) roles.pinHand = h;
          else show(h, 0, false);
        }
      } else if ((inRange & bit) && shown[h] < 0) {
        auto next = roles;
        int t = next.draw(h, run(stockQuery) > 0);
        if (t >= 0) {
          show(h, t, true);
          if (shown[h] == t)
            roles = next;
        }
      }
    }
  }
  static unsigned trace = ~0u;
  static bool tracing =
      GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) > 0;
  static ULONGLONG nextTraceTime = 0;
  if (tracing && now >= nextTraceTime && slot > 0 && slot <= 8) {
    auto p = g.kitCenters[slot - 1];
    Log("kit_center slot=%d local=%.5f,%.5f,%.5f radius=%.4f", slot, p.x, p.y,
        p.z, g.kitRadii[slot - 1]);
    for (int i = 0; i < 8; i++)
      if (i + 1 != slot && g.kitRadii[i] > 0) {
        auto c = g.kitCenters[i];
        Log("kit_other slot=%d local=%.5f,%.5f,%.5f radius=%.4f", i + 1, c.x,
            c.y, c.z, g.kitRadii[i]);
      }
    nextTraceTime = now + 300;
  }
  unsigned nextTrace =
      (unsigned(slot) << 10) | (inRange << 6) | (valid << 4) | down;
  if (tracing && trace != nextTrace) {
    Log("kit_input slot=%d inRange=%u valid=%u down=%u", slot, inRange, valid,
        down);
    trace = nextTrace;
  }
  const bool wasParked = HasParkedPin();
  WorldPinFrame(g,hands,roles.drawn&1 ? roles.pinHand : -1,down,pressed,valid,inRange,
                roles.drawn&2 ? roles.handFor(1) : -1,pinHealth);
  if(g.worldPinBroken && roles.drawn & 1u){
    pinHealth = 1;
    if (shown[roles.pinHand] == 0) show(roles.pinHand,0,false);
    roles.put(0);
  } else if (wasParked && !HasParkedPin()) {
    roles.put(0);
  }
  if (shown[0] < 0 && shown[1] < 0) nextHolster = 0;
  else if (now >= nextHolster) {
    run(holsterWeapon); // Use TTWVR's normal put-away path so the host also releases the gun.
    nextHolster = now + 150;
  }
  g.kitDrawn = HasParkedPin() ? roles.drawn & ~1u : roles.drawn;
  g.kitPinHand = roles.pinHand;
  for (int h = 0; h < 2; h++)
    if (shown[h] >= 0)
      g.kitHands |= 1u << h;
}
} // namespace locks
