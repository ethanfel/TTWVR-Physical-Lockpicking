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

using WristCommit = void(__cdecl *)(void *, int);
WristCommit wristCommit;
const int *wristTab;
const void *const *wristSelection;

bool isPinForm(const void *form) {
  return Readable(form, 16) && static_cast<const uint32_t *>(form)[3] == 0x0000000a;
}
bool isBobbyPin(Entry entry) {
  // Fallout 1.4.0.525 ContChangesEntry::type and TESForm::refID.
  if (!available || !Readable(entry, 12))
    return false;
  return isPinForm(static_cast<const void *const *>(entry)[2]);
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
// TTWVR's wrist inventory owns its own row list and favorites wheel.
bool __stdcall allowWristRow(const void *const *row) {
  // TTWVR handles an empty selection after the tab check.
  return !row || *wristTab < 3 || (available && isPinForm(row[0]));
}
void __cdecl assignWrist(void *player, int index) {
  if (!isPinForm(*wristSelection)) {
    wristCommit(player, index);
    return;
  }
  if (available && index >= 0 && index < 8) {
    pending.store(unsigned(index + 1));
    Log("bobby_pin_wrist_favourite requested=%d", index + 1);
  }
}
} // namespace

bool InstallWristPinFavourite(uintptr_t base, const HostLayout::WristInventory &inventory) {
  const uintptr_t gate = base + inventory.gate;
  wristTab = reinterpret_cast<const int *>(base + inventory.tab);
  wristSelection = reinterpret_cast<const void *const *>(base + inventory.selection);
  wristCommit = reinterpret_cast<WristCommit>(base + inventory.commit.target);
  const NativeCall commitHook{base + inventory.commit.site,
                              uintptr_t(wristCommit), reinterpret_cast<void *>(assignWrist)};

  // Original i386 gate: null row or category < 3 follows the native accept path.
  // The absolute tab operand is relocated by the Windows loader.
  unsigned char expected[]{0x85,0xf6,0x74,0x32,0x83,0x3d,0,0,0,0,0x03,0x7c,0x29};
  static_assert(sizeof(wristTab) == 4);
  std::memcpy(expected + 6, &wristTab, sizeof(wristTab));
  if (!Readable(reinterpret_cast<void *>(gate), sizeof(expected)) ||
      std::memcmp(reinterpret_cast<void *>(gate), expected, sizeof(expected)) != 0)
    return false;

  // Preserve the host registers, pass its selected row (ESI), and branch to
  // the same two destinations. POPAD preserves the result of TEST AL,AL.
  unsigned char filter[]{0x60,0x56,0xe8,0,0,0,0,0x84,0xc0,0x61,0x75,0x2a,0x90};
  const uint32_t delta = uint32_t(uintptr_t(allowWristRow) - (gate + 7));
  std::memcpy(filter + 3, &delta, sizeof(delta));
  DWORD old;
  auto *site = reinterpret_cast<void *>(gate);
  if (!VirtualProtect(site, sizeof(filter), PAGE_EXECUTE_READWRITE, &old))
    return false;
  const bool installed = InstallVerifiedCalls(&commitHook, 1);
  if (installed) {
    std::memcpy(site, filter, sizeof(filter));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(filter));
  }
  DWORD unused;
  VirtualProtect(site, sizeof(filter), old, &unused);
  return installed;
}

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
