// SPDX-License-Identifier: GPL-3.0-only
#include "Shared.h"
#include <cmath>
#include <cstddef>
namespace locks {
namespace {
// Prefix of xNVSE's public NVSEScriptInterface and Array Element ABI.
// https://github.com/xNVSE/NVSE/blob/master/nvse/nvse/PluginAPI.h
struct Element {
  union {
    double number;
    void *form;
  } data{};
  unsigned char type = 0;
};
static_assert(offsetof(Element, type) == 8);
struct Scripts {
  bool(__cdecl *call)(void *, void *, void *, Element *, uint8_t, ...);
  void *params, *extract, *format, *callAlt;
  void *(__cdecl *compile)(const char *);
};
Scripts *scripts = nullptr;
void *countScript = nullptr;
void *dropScript = nullptr;
bool attempted = false;
uintptr_t conditionMenu = 0;
float freshCondition = 0;
struct Request {
  uint64_t id = 0;
  uintptr_t menu = 0, target = 0;
};
Request pending;
uint64_t sequence = 0, completed = 0;
bool committed = false;
int remaining = -1;
const char *countSource = R"(ref item
begin Function {}
 let item := GetFormFromMod "FalloutNV.esm" "00000A"
 if item
  SetFunctionValue (player.GetItemCount item)
 else
  SetFunctionValue -1
 endif
end
)";
// Create first, remove exactly one only after creation succeeds, and roll back
// the new reference if the count did not fall by one. No RemoveAllItems or
// free replacement grants. Executed on the game thread, never OpenXR's thread.
const char *dropSource = R"(ref target
int placement
ref item
ref dropped
ref cell
int before
float dx
float dy
float dist
float px
float py
float pz
float offset
float ux
float uy
begin Function {target, placement}
 let item := GetFormFromMod "FalloutNV.esm" "00000A"
 if eval !item || !target
  SetFunctionValue 0
  return
 endif
 let before := player.GetItemCount item
 if before < 1
  SetFunctionValue 0
  return
 endif
 let px := player.GetPos X
 let py := player.GetPos Y
 let pz := (player.GetPos Z) + 12
 let dx := px - (target.GetPos X)
 let dy := py - (target.GetPos Y)
 let dist := sqrt (dx*dx + dy*dy)
 ; Keep the pickup on the player's side of the lock and at floor height.
 if eval dist > 24 && dist < 500 && (player.GetInSameCell target)
  let px := (target.GetPos X) + dx*24/dist
  let py := (target.GetPos Y) + dy*24/dist
 endif
 ; Spread consecutive pickups laterally. Stacking paused Havok bodies at
 ; identical coordinates lets them bury or scatter one another on resume.
 let ux := 1
 let uy := 0
 if dist > 1
  let ux := dx/dist
  let uy := dy/dist
 endif
 let offset := ((placement % 7) - 3) * 10
 let px := px - uy*offset + ux*((placement / 7) % 3)*10
 let py := py + ux*offset + uy*((placement / 7) % 3)*10
 ; JIP creates at the final coordinates before queuing the model/physics
 ; load. Do not PlaceAtMe at the player then move an uninitialized 3D body.
 let cell := player.GetParentCell
 let dropped := PlaceAtCell item 1 cell px py pz
 if eval !dropped
  SetFunctionValue 0
  return
 endif
 player.RemoveItem item 1 1
 if (player.GetItemCount item) != (before - 1)
  dropped.Disable
  dropped.MarkForDelete
  SetFunctionValue 0
  return
 endif
 SetFunctionValue dropped
end
)";
int Count() {
  Element e;
  return scripts && countScript &&
                 scripts->call(countScript, nullptr, nullptr, &e, 0) &&
                 e.type == 1 && std::isfinite(e.data.number) &&
                 e.data.number >= 0 && e.data.number < 1e7
             ? int(e.data.number)
             : -1;
}
} // namespace
void InitInventory(void *api) { scripts = static_cast<Scripts *>(api); }
uint64_t RequestPinDrop(uintptr_t menu, uintptr_t target) {
  std::lock_guard guard(sharedMutex);
  if (pending.id || !menu || !target)
    return 0;
  pending = {++sequence, menu, target};
  return pending.id;
}
bool PinDropResult(uint64_t id, bool &ok, int &count) {
  std::lock_guard guard(sharedMutex);
  if (!id || completed != id)
    return false;
  ok = committed;
  count = remaining;
  return true;
}
void UpdateInventory(GameState &state) {
  if (state.menu != conditionMenu) {
    conditionMenu = state.menu;
    freshCondition = state.menu && std::isfinite(state.health) &&
                             state.health > 0 && state.health < 10
                         ? state.health
                         : 0;
  }
  if (state.menu && state.target && !attempted) {
    attempted = true;
    if (scripts && scripts->compile && scripts->call) {
      countScript = scripts->compile(countSource);
      dropScript = scripts->compile(dropSource);
    }
    Log("inventory_scripts count=%d drop=%d", countScript != nullptr,
        dropScript != nullptr);
  }
  if (state.menu && state.target && countScript && dropScript) {
    state.pinCount = Count();
    state.inventoryReady = state.pinCount >= 0;
  }
  Request request;
  {
    std::lock_guard guard(sharedMutex);
    request = pending;
  }
  if (!request.id)
    return;
  bool success = false;
  uint32_t refId = 0;
  int before = state.pinCount;
  // A menu close, fallback, load, target change or break cancels pending work.
  Control c;
  {
    std::lock_guard guard(sharedMutex);
    c = control;
  }
  if (state.inventoryReady && before > 0 && state.menu == request.menu &&
      state.target == request.target && state.stage == 2 && c.valid &&
      !c.fallback && GetTickCount64() - c.stamp < 150 &&
      Readable(reinterpret_cast<void *>(request.target), 0x68)) {
    Element e;
    bool called = scripts->call(dropScript, nullptr, nullptr, &e, 2,
                                reinterpret_cast<void *>(request.target),
                                int((request.id - 1) % 21));
    state.pinCount = Count();
    success = called && e.type == 2 && Readable(e.data.form, 0x68) &&
              state.pinCount == before - 1;
    if (success)
      refId = *reinterpret_cast<const uint32_t *>(
          static_cast<const unsigned char *>(e.data.form) + 0xc);
    // If inventory changed, never restore a visual copy based on a missing
    // return value. Native count remains the authority.
    if (state.pinCount == before - 1)
      success = true;
  }
  if (success && state.pinCount > 0 && freshCondition > 0 &&
      Readable(reinterpret_cast<void *>(state.menu), 0xe4)) {
    // A genuinely spent inventory pin has been replaced. Reset only its
    // condition to this attempt's native initial value; difficulty, solution,
    // cylinder limits and success logic remain entirely native.
    *reinterpret_cast<float *>(state.menu + 0x94) = freshCondition;
    Log("pin_replaced condition_before=%.3f condition_after=%.3f", state.health,
        freshCondition);
    state.health = freshCondition;
  }
  Log("pin_world_drop id=%llu committed=%d before=%d after=%d reference=%08x "
      "target=%p placement=%u",
      static_cast<unsigned long long>(request.id), success, before,
      state.pinCount, refId, (void *)request.target,
      unsigned((request.id - 1) % 21));
  {
    std::lock_guard guard(sharedMutex);
    completed = request.id;
    committed = success;
    remaining = state.pinCount;
    pending = {};
  }
}
} // namespace locks
