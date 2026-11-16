// SPDX-License-Identifier: GPL-3.0-only
#include "Interaction.h"
#include "Shared.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace locks {
std::wstring folder;
std::mutex sharedMutex;
GameState game;
Control control;
static bool nativeReady = false;
bool NativeReady() { return nativeReady; }
static uintptr_t gameBase = 0;
using AxisFn = int(__thiscall *)(void *);
using SetFloatFn = void(__thiscall *)(void *, uint32_t, float, bool);
using GetFloatFn = float(__thiscall *)(void *, uint32_t);
static AxisFn originalAxis = nullptr;
using ModeFn = unsigned char(__thiscall *)(void *);
static ModeFn originalMode = nullptr;
using LimitFn = float(__cdecl *)(float, float);
static LimitFn originalLimit = nullptr;
static SetFloatFn tileSet = nullptr;
static GetFloatFn tileGet = nullptr;
void Log(const char *fmt, ...) {
  static std::mutex mutex;
  std::lock_guard guard(mutex);
  if (folder.empty())
    return;
  if (auto *f = _wfopen((folder + L"\\TTWVRPhysicalLockpicking.log").c_str(),
                        L"ab")) {
    fprintf(f, "tick=%lu ", GetTickCount());
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
  }
}
GameState Snapshot() {
  std::lock_guard guard(sharedMutex);
  return game;
}
bool Readable(const void *p, size_t n) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p, &m, sizeof(m)) || m.State != MEM_COMMIT ||
      (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    return false;
  return uintptr_t(p) >= uintptr_t(m.BaseAddress) &&
         uintptr_t(p) + n <= uintptr_t(m.BaseAddress) + m.RegionSize;
}
struct PluginInfo {
  uint32_t infoVersion;
  const char *name;
  uint32_t version;
};
struct Nvse {
  uint32_t version, runtime, editor, isEditor;
  void *registerCommand, *setOpcode;
  void *(__cdecl *query)(uint32_t);
  uint32_t(__cdecl *handle)();
};
struct Message {
  const char *sender;
  uint32_t type, length;
  void *data;
};
struct Messaging {
  uint32_t version;
  bool(__cdecl *listen)(uint32_t, const char *, void(__cdecl *)(Message *));
};
Control Current() {
  std::lock_guard guard(sharedMutex);
  auto c = control;
  if (!nativeReady || !c.menu || c.menu != game.menu ||
      GetTickCount64() - c.stamp > 150 || c.fallback)
    c.valid = false;
  return c;
}
unsigned char __fastcall InputMode(void *self, void *) {
  auto original = originalMode(self);
  auto c = Current();
  static int last = -1;
  if (c.valid && last != original) {
    last = original;
    Log("native_input_route original_gamepad=%u physical_axes=1", original);
  }
  // Route just the lock menu's mode checks through the existing axis hooks.
  // Global controller settings and all other game menus remain untouched.
  return c.valid ? 1 : original;
}
int __fastcall PickAxis(void *self, void *) {
  auto c = Current();
  if (!c.valid)
    return originalAxis(self);
  auto tile = *reinterpret_cast<void **>(c.menu + 0x68);
  auto trait = *reinterpret_cast<uint32_t *>(gameBase + 0xdda1fc);
  auto track = *reinterpret_cast<void **>(c.menu + 0x64);
  if (Readable(tile, 0x3c) && Readable(track, 0x3c) && std::isfinite(c.pick)) {
    float width = tileGet(track, 0xfb1);
    if (std::isfinite(width) && width > 0)
      tileSet(tile, trait, std::clamp(c.pick, 0.f, 180.f) * width / 180.f,
              true);
  }
  return 0; // Native update derives resistance from the requested angle.
}
int __fastcall TorqueAxis(void *self, void *) {
  auto c = Current();
  if (!c.valid)
    return originalAxis(self);
  // Releasing wrist pressure at a jam must also leave the native jam state.
  auto state = Snapshot();
  if (state.stage == 4 && c.targetAngle < state.cylinder - .5f)
    return 0;
  return c.turning ? -int(32767.f * std::clamp(c.torque, 0.f, 1.f)) : 0;
}
float __cdecl CylinderLimit(float proposed, float resistanceLimit) {
  float native = originalLimit(proposed, resistanceLimit);
  auto c = Current();
  // This clamp can only reduce the game's permitted rotation. Native
  // resistance, breakage and success checks run afterwards, unchanged.
  return c.valid && std::isfinite(c.targetAngle)
             ? capCylinder(native, c.turning ? c.targetAngle : 0.f)
             : native;
}

bool WriteCall(uintptr_t address, void *target) {
  DWORD old = 0;
  if (!VirtualProtect((void *)address, 5, PAGE_EXECUTE_READWRITE, &old))
    return false;
  int32_t rel = int32_t(uintptr_t(target) - (address + 5));
  memcpy((void *)(address + 1), &rel, 4);
  FlushInstructionCache(GetCurrentProcess(), (void *)address, 5);
  DWORD ignored;
  VirtualProtect((void *)address, 5, old, &ignored);
  return true;
}
void InstallNative() {
  gameBase = uintptr_t(GetModuleHandleW(nullptr));
  struct Hook {
    uintptr_t site, target;
    void *replacement;
  };
  Hook hooks[] = {{gameBase + 0x39094d, gameBase + 0x623390,
                   reinterpret_cast<void *>(PickAxis)},
                  {gameBase + 0x38ed2a, gameBase + 0x623390,
                   reinterpret_cast<void *>(TorqueAxis)},
                  {gameBase + 0x38f516, gameBase + 0xebd0,
                   reinterpret_cast<void *>(CylinderLimit)},
                  {gameBase + 0x38ed0d, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f1c3, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f1df, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f41d, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f439, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f53b, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38fad4, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38faf5, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38fc00, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x390930, gameBase + 0xb71d0,
                   reinterpret_cast<void *>(InputMode)}};
  for (auto h : hooks) {
    int32_t rel = 0;
    if (!Readable((void *)h.site, 5) || *(unsigned char *)h.site != 0xe8) {
      Log("native_bypass call_signature=0");
      return;
    }
    memcpy(&rel, (void *)(h.site + 1), 4);
    if (h.site + 5 + rel != h.target) {
      Log("native_bypass call_target=0");
      return;
    }
  }
  originalMode = reinterpret_cast<ModeFn>(gameBase + 0xb71d0);
  originalAxis = reinterpret_cast<AxisFn>(gameBase + 0x623390);
  originalLimit = reinterpret_cast<LimitFn>(gameBase + 0xebd0);
  tileSet = reinterpret_cast<SetFloatFn>(gameBase + 0x6012d0);
  tileGet = reinterpret_cast<GetFloatFn>(gameBase + 0x6011b0);
  for (unsigned n = 0; n < sizeof(hooks) / sizeof(hooks[0]); n++)
    if (!WriteCall(hooks[n].site, hooks[n].replacement)) {
      while (n) {
        --n;
        WriteCall(hooks[n].site, reinterpret_cast<void *>(hooks[n].target));
      }
      return;
    }

  nativeReady = true;
  Log("native_input_hooks_ready verified_calls=13 native_outcomes_unchanged=1");
}
void __cdecl OnMessage(Message *m) {
  if (m && m->type == 18) {
    InstallNative();
    return;
  }
  if (!m || m->type != 20)
    return;
  // New Vegas 1.4.0.525: LockPickMenu singleton. Only read here; never unlock
  // by force.
  auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  auto address = reinterpret_cast<uintptr_t *>(base + 0xdda204);
  GameState next;
  if (Readable(address, 4)) {
    auto p = reinterpret_cast<const unsigned char *>(*address);
    if (Readable(p, 0xe4) &&
        *reinterpret_cast<const uintptr_t *>(p) == base + 0xc7439c) {
      next.menu = reinterpret_cast<uintptr_t>(p);
      next.target = *reinterpret_cast<const uintptr_t *>(p + 0x6c);
      next.stage = *reinterpret_cast<const uint32_t *>(p + 0x28);
      next.level = *reinterpret_cast<const uint32_t *>(p + 0x70);
      next.skill = *reinterpret_cast<const uint32_t *>(p + 0x74);
      auto tile = *reinterpret_cast<void *const *>(p + 0x68);
      next.pick = 90;
      auto track = *reinterpret_cast<void *const *>(p + 0x64);
      if (nativeReady && Readable(tile, 0x3c) && Readable(track, 0x3c)) {
        float width = tileGet(track, 0xfb1);
        if (std::isfinite(width) && width > 0)
          next.pick =
              std::clamp(180.f *
                             tileGet(tile, *reinterpret_cast<const uint32_t *>(
                                               base + 0xdda1fc)) /
                             width,
                         0.f, 180.f);
      }
      next.cylinder = *reinterpret_cast<const float *>(p + 0x8c);
      next.health = *reinterpret_cast<const float *>(p + 0x94);
    }
  }
  UpdateTarget(next);
  UpdateInventory(next);
  std::lock_guard guard(sharedMutex);
  if (next.menu != game.menu || (next.stage != game.stage && next.stage >= 4))
    Log("menu_transition pointer=%p target=%p stage=%u level=%u skill=%u "
        "pick=%.3f cylinder=%.3f health=%.3f",
        (void *)next.menu, (void *)next.target, next.stage, next.level,
        next.skill, next.pick, next.cylinder, next.health);
  if (next.targetKind != game.targetKind || next.target != game.target)
    Log("target_object kind=%u reference=%08x base=%08x", next.targetKind,
        next.targetRefId, next.targetBaseId);
  game = next;
}
void InitFolder() {
  if (!folder.empty())
    return;
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&InitFolder), &self);
  wchar_t path[32768]{};
  GetModuleFileNameW(self, path, 32768);
  folder = path;
  folder.resize(folder.find_last_of(L"\\/"));
}

} // namespace locks
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const locks::Nvse *n,
                                                       locks::PluginInfo *p) {
  if (!p)
    return false;
  p->infoVersion = 1;
  p->name = "TTWVR Physical Lockpicking Prototype";
  p->version = 24;
  return n && !n->isEditor && n->version >= 6 && n->runtime == 0x040020d0;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const locks::Nvse *n) {
  using namespace locks;
  InitFolder();
  wchar_t enabled[8]{}, disabled[8]{};
  GetEnvironmentVariableW(L"TTWVR_PHYSICAL_LOCKPICKING", enabled, 8);
  GetEnvironmentVariableW(L"DISABLE_TTWVR_PHYSICAL_LOCKPICKING", disabled, 8);
  if (wcscmp(enabled, L"1") || disabled[0]) {
    Log("plugin_bypass disabled=1");
    return true;
  }
  Log("plugin_loaded runtime=%08x probe_only=0", n->runtime);
  // The test installer registers an opt-in layer in this game prefix only.
  // Do not force an undiscoverable explicit layer: initialization must bypass
  // safely.
  InitInventory(n->query(6));
  auto *m = static_cast<Messaging *>(n->query(2));
  return m && m->listen(n->handle(), "NVSE", OnMessage);
}
