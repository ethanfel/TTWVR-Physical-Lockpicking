// SPDX-License-Identifier: GPL-3.0-only
#include "PinFavourite.h"
#include "Shared.h"

#include <atomic>
#include <cstring>

namespace locks {
namespace {
using Entry = const void *;
using CanAssign = bool(__thiscall *)(void *, unsigned);
using Commit = void(__thiscall *)(void *, Entry);
CanAssign canAssign;
Commit commit;
Entry const *selection;
std::atomic<unsigned> pending{};
std::atomic<bool> available{};

bool isBobbyPin(Entry entry) {
  // Fallout 1.4.0.525 ContChangesEntry::type and TESForm::refID.
  if (!available || !Readable(entry, 12))
    return false;
  const auto form = static_cast<const void *const *>(entry)[2];
  return Readable(form, 16) &&
         static_cast<const uint32_t *>(form)[3] == 0x0000000a;
}
bool __fastcall canAssignSelected(void *tile, void *, unsigned trait) {
  if (isBobbyPin(*selection)) {
    Log("bobby_pin_inventory eligibility=1");
    return true;
  }
  return canAssign(tile, trait);
}
void __fastcall assignSelected(void *wheel, void *, Entry entry) {
  if (!isBobbyPin(entry)) {
    commit(wheel, entry);
    return;
  }
  // Native HotKeyWheel: active byte and zero-based highlighted segment.
  const auto *bytes = static_cast<const unsigned char *>(wheel);
  int index;
  std::memcpy(&index, bytes + 0x28, sizeof(index));
  if (bytes[0x24] && index >= 0 && index < 8)
    pending.store(unsigned(index + 1));
}
} // namespace

bool InstallPinFavourite(uintptr_t base) {
  selection = reinterpret_cast<Entry const *>(base + 0xdd9ea8);
  canAssign = reinterpret_cast<CanAssign>(base + 0x601230);
  commit = reinterpret_cast<Commit>(base + 0x3019e0);
  // Fallout's menu-click and controller assignment paths each check
  // eligibility and commit to the same wheel. Leave global gear use alone.
  const NativeCall hooks[] = {{base + 0x380541, uintptr_t(canAssign),
                               reinterpret_cast<void *>(canAssignSelected)},
                              {base + 0x381cbd, uintptr_t(canAssign),
                               reinterpret_cast<void *>(canAssignSelected)},
                              {base + 0x38059d, uintptr_t(commit),
                               reinterpret_cast<void *>(assignSelected)},
                              {base + 0x381d25, uintptr_t(commit),
                               reinterpret_cast<void *>(assignSelected)}};
  return InstallVerifiedCalls(hooks, sizeof(hooks) / sizeof(hooks[0]));
}

void EnablePinFavourite(bool enabled) { available.store(enabled); }
unsigned TakePinFavourite() { return pending.exchange(0); }
} // namespace locks
