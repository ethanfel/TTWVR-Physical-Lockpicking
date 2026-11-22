// SPDX-License-Identifier: GPL-3.0-only
#include "Interaction.h"
#include "KitRuntime.h"
#include "HostInput.h"
#include "MotionShaders.h"
#include "WorldLock.h"
#include "NativeScene.h"
#include "Shared.h"
#include "Settings.h"
#include <algorithm>
#include <atomic>
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
bool InstallVerifiedCalls(const NativeCall *hooks, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const auto &h = hooks[i];
    int32_t rel = 0;
    if (!Readable((void *)h.site, 5) || *(unsigned char *)h.site != 0xe8) {
      Log("native_bypass call_signature=0");
      return false;
    }
    memcpy(&rel, (void *)(h.site + 1), 4);
    if (h.site + 5 + rel != h.target) {
      Log("native_bypass call_target=0");
      return false;
    }
  }
  for (size_t n = 0; n < count; n++)
    if (!WriteCall(hooks[n].site, hooks[n].replacement)) {
      while (n) {
        --n;
        WriteCall(hooks[n].site, reinterpret_cast<void *>(hooks[n].target));
      }
      return false;
    }

  return true;
}
// This call belongs to the native success branch, after UnLock and XP.
// Player activation, scripted activation and starting the native menu use other sites.
static bool doorForm(uintptr_t ref) {
  if (!ref || !Readable(reinterpret_cast<void *>(ref + 0x20), sizeof(uintptr_t))) return false;
  auto form = *reinterpret_cast<uintptr_t *>(ref + 0x20);
  return form && Readable(reinterpret_cast<void *>(form + 4), 1) &&
         *reinterpret_cast<unsigned char *>(form + 4) == 0x1c;
}
static bool physicalDoor(uintptr_t ref) {
  if (!doorForm(ref)) return false;
  if (IsWorldUnlockPending(ref)) return true;
  auto c = Current();
  return c.valid && *reinterpret_cast<uintptr_t *>(c.menu + 0x6c) == ref;
}
bool __fastcall ActivateAfterUnlock(uintptr_t ref, void *, uintptr_t actor,
                                   int a, int b, int c) {
  if (physicalDoor(ref)) {
    Log("door_unlocked_waiting_for_player ref=%08x", *reinterpret_cast<unsigned *>(ref + 0xc));
    return false;
  }
  using Activate = bool(__thiscall *)(uintptr_t, uintptr_t, int, int, int);
  return reinterpret_cast<Activate>(gameBase + 0x173170)(ref, actor, a, b, c);
}
// Draw3DElements already skips the vanilla lock when its model is absent.
static std::atomic<uintptr_t> physicalMenu{0};
static void *__fastcall StockModelForDraw(void *model, void *) {
  const auto hidden = physicalMenu.load(std::memory_order_relaxed);
  if (hidden && hidden == *reinterpret_cast<const uintptr_t *>(gameBase + 0xdda204)) return nullptr;
  using GetModel = void *(__thiscall *)(void *);
  return reinterpret_cast<GetModel>(gameBase + 0x159450)(model);
}
void InstallNative() {
  gameBase = uintptr_t(GetModuleHandleW(nullptr));
  const NativeCall hooks[] = {{gameBase + 0x39094d, gameBase + 0x623390,
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
                   reinterpret_cast<void *>(InputMode)},
                  {gameBase + 0x38f956, gameBase + 0x173170,
                   reinterpret_cast<void *>(ActivateAfterUnlock)}};
  originalMode = reinterpret_cast<ModeFn>(gameBase + 0xb71d0);
  originalAxis = reinterpret_cast<AxisFn>(gameBase + 0x623390);
  originalLimit = reinterpret_cast<LimitFn>(gameBase + 0xebd0);
  tileSet = reinterpret_cast<SetFloatFn>(gameBase + 0x6012d0);
  tileGet = reinterpret_cast<GetFloatFn>(gameBase + 0x6011b0);
  if (!InstallVerifiedCalls(hooks, sizeof(hooks) / sizeof(hooks[0]))) return;
  const NativeCall draw{gameBase + 0x38e7ea, gameBase + 0x159450,
                        reinterpret_cast<void *>(StockModelForDraw)};
  Log("stock_lock_draw_filter installed=%d", InstallVerifiedCalls(&draw, 1));
  nativeReady = true;
  Log("native_input_hooks_ready verified_calls=14 manual_door_activation=1");
}
// A zero alpha on the menu root does not hide the independently styled text.
static uintptr_t LockContentBranch(uintptr_t menu, uintptr_t root) {
  // Walk from the registered skill tile to the content branch beneath the
  // menu. This also works with replacement layouts that rename the branch.
  auto tile = *reinterpret_cast<const uintptr_t *>(menu + 0x2c);
  for (unsigned depth = 0; depth < 32 && tile && tile != root; ++depth) {
    if (!Readable(reinterpret_cast<const void *>(tile), 0x3c)) break;
    const auto parent = *reinterpret_cast<const uintptr_t *>(tile + 0x28);
    if (parent == root) return tile;
    tile = parent;
  }
  return 0;
}
struct StockLockMenu {
  uintptr_t menu = 0, root = 0, content = 0;
  float alpha = 255.f, visible = 1.f;

  void update(uintptr_t currentMenu, uintptr_t currentRoot, bool hide) {
    if (menu != currentMenu || root != currentRoot) *this = {};
    if ((!hide && !menu) || !Readable(reinterpret_cast<void *>(currentRoot), 0x3c)) return;
    constexpr unsigned alphaTrait = 0xfa9, visibleTrait = 0xfa3;
    auto *rootTile = reinterpret_cast<void *>(currentRoot);
    if (hide) {
      if (!menu) {
        alpha = tileGet(rootTile, alphaTrait);
        if (!std::isfinite(alpha)) return;
        menu = currentMenu;
        root = currentRoot;
        tileSet(rootTile, alphaTrait, 0.f, true);
      }
      if (!content) {
        auto branch = LockContentBranch(menu, root);
        if (branch) {
          auto *tile = reinterpret_cast<void *>(branch);
          visible = tileGet(tile, visibleTrait);
          if (std::isfinite(visible)) {
            content = branch;
            tileSet(tile, visibleTrait, 0.f, true);
          }
        }
      }
    } else {
      if (content && content == LockContentBranch(menu, root))
        tileSet(reinterpret_cast<void *>(content), visibleTrait, visible, true);
      tileSet(rootTile, alphaTrait, alpha, true);
      *this = {};
    }
  }
};
void __cdecl OnMessage(Message *m) {
  if (m && (m->type == 2 || m->type == 6 || m->type == 8 || m->type == 14)) ResetKit();
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
  const bool completing = CompleteWorldLock(next);
  const auto root = next.menu ? *reinterpret_cast<const uintptr_t *>(next.menu + 4) : 0;
  static StockLockMenu stockMenu;
  const bool closing = next.menu && next.menu == physicalMenu.load(std::memory_order_relaxed) && next.stage == 7;
  const bool hideMenu = nativeReady && next.menu &&
                        (completing || NativeScenePresented(next.menu) || closing);
  physicalMenu.store(hideMenu ? next.menu : 0, std::memory_order_relaxed);
  stockMenu.update(next.menu, root, hideMenu);
  if (completing) return;
  if (DismissLockMenu(next)) return;
  UpdateTarget(next);
  // Latch the approached face for the attempt. Leaning through the close-up
  // or moving the camera cannot change which way the lock opens.
  static uintptr_t directionMenu = 0, directionTarget = 0;
  static DoorTurn doorTurn;
  static std::optional<std::bitset<128>> hiddenParts;
  if (next.menu != directionMenu || next.target != directionTarget) {
    doorTurn = CloseupDoorTurn(next);
    hiddenParts = CloseupHiddenParts(next);
    directionMenu = next.menu;
    directionTarget = next.target;
  }
  next.doorTurn = doorTurn;
  next.closeupHiddenParts = hiddenParts;
  UpdateInventory(next);
  TickKit(next);
  TickVisibleLocks(next.target);
  NativeSceneTick(next);
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
  p->name = "TTWVR Physical Lockpicking";
  p->version = 99;  // Beta 0.2, revision 13.
  return n && !n->isEditor && n->version >= 6 && n->runtime == 0x040020d0;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const locks::Nvse *n) {
  using namespace locks;
  InitFolder();
  wchar_t enabled[8]{}, disabled[8]{};
  GetEnvironmentVariableW(L"TTWVR_PHYSICAL_LOCKPICKING", enabled, 8);
  GetEnvironmentVariableW(L"DISABLE_TTWVR_PHYSICAL_LOCKPICKING", disabled, 8);
  if (!wcscmp(enabled, L"0") || disabled[0]) {
    Log("plugin_bypass disabled=1");
    return true;
  }
  Log("plugin_loaded runtime=%08x probe_only=0 version=99 release=\"Beta 0.2\" build=stock-lock-visuals", n->runtime);
  const DWORD iniStatus = EnsureDefaultSettings(folder);
  if (iniStatus == ERROR_SUCCESS)
    Log("settings_created file=TTWVRPhysicalLockpicking.ini");
  else if (iniStatus != ERROR_ALREADY_EXISTS)
    Log("settings_create_failed win32_error=%lu", iniStatus);
  InitMotionShaders();
  InitInventory(n->query(6));
  InitKit(n->query(6));
  InitWorldLocks(n->query(6), n->query(5), n->query(3));
  InitNativeScene(n->query(6));
  auto *m = static_cast<Messaging *>(n->query(2));
  return m && m->listen(n->handle(), "NVSE", OnMessage);
}
