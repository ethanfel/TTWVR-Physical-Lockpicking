// SPDX-License-Identifier: GPL-3.0-only
#include "ForceOdds.h"
#include "Shared.h"
#include "WorldLock.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace locks {
namespace {
using Chance = int(__cdecl *)(int, int);
using TileText = const char *(__cdecl *)(void *);
Chance nativeChance = nullptr, previewChance = nullptr;
TileText originalText = nullptr;
uintptr_t exe = 0;
bool enabled = false, overrideInstalled = false, hoverInstalled = false, trace = false;
int configuredChance = -1;
thread_local int clickChance = -1;

int __cdecl chanceForRoll(int skill, int threshold) {
  if (clickChance < 0) return nativeChance(skill, threshold);
  Log("force_lock_roll skill=%d threshold=%d override=%d", skill, threshold, clickChance);
  return clickChance;
}

// TTWVR can collect a different HUD target for each hand. Resolve IDs and run
// NVSE eligibility checks only on the game thread, outside the HUD capture.
struct Hover {
  unsigned id = 0;
  ULONGLONG seen = 0, checked = 0;
  int chance = -1;
};
std::array<Hover, 4> hovered{};
std::mutex hoverMutex;

int requestChance(unsigned id, ULONGLONG now) {
  std::lock_guard guard(hoverMutex);
  for (auto &entry : hovered) {
    if (entry.id != id) continue;
    entry.seen = now;
    return entry.checked && now - entry.checked < 500 ? entry.chance : -1;
  }
  auto oldest = std::min_element(hovered.begin(), hovered.end(),
      [](const Hover &a, const Hover &b) { return a.seen < b.seen; });
  *oldest = {id, now, 0, -1};
  return -1;
}

const char *__cdecl difficultyText(void *tile) {
  const char *text = originalText(tile);
  if (!text || !*text) return text;
  auto hud = *reinterpret_cast<uintptr_t *>(exe + 0xdd96c0);
  if (!hud || *reinterpret_cast<void **>(hud + 0xac) != tile) return text;
  auto ref = *reinterpret_cast<uintptr_t *>(hud + 0x1b8);
  if (!ref) return text;
  auto id = *reinterpret_cast<unsigned *>(ref + 0xc);
  if (!id) return text;
  int chance = requestChance(id, GetTickCount64());
  if (chance < 0 || (chance == 0 && configuredChance != 0)) return text;
  // The caller immediately copies this field into its 64-byte difficulty line.
  thread_local char label[64];
  int size = std::snprintf(label, sizeof(label), "%s | Force Lock %d%%", text, chance);
  if (size < 0 || size >= int(sizeof(label))) return text;
  if (trace) {
    thread_local unsigned lastId = 0;
    thread_local int lastChance = -1;
    if (lastId != id || lastChance != chance) {
      Log("force_lock_hover ref=%08x label=%s", id, label);
      lastId = id;
      lastChance = chance;
    }
  }
  return label;
}
} // namespace

void ConfigureForceOdds(const Ergonomics &settings) {
  enabled = settings.enableForceLock && settings.driverMode == DriverMode::AxialTwist;
  configuredChance = settings.forceLockChanceOverride;
  trace = GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) != 0;
}

void InstallForceOdds(uintptr_t hostBias, bool version117) {
  if (!enabled) return;
  exe = uintptr_t(GetModuleHandleW(nullptr));
  auto site = exe + 0x3903d9;
  // This is the chance calculation after the random draw and directly before
  // the unsigned comparison. Preserve the active game or tweaks-plugin formula.
  constexpr unsigned char after[]{0x83,0xc4,0x08,0x3b,0xf0,0x73,0x28};
  auto bytes = reinterpret_cast<const unsigned char *>(site);
  if (!Readable(bytes-1,13) || bytes[-1] != 0x50 || *bytes != 0xe8 ||
      std::memcmp(bytes+5,after,sizeof(after))) {
    Log("force_lock_odds unavailable=roll_layout");
    return;
  }
  int32_t offset;
  std::memcpy(&offset,bytes+1,sizeof(offset));
  auto target = site + 5 + offset;
  MEMORY_BASIC_INFORMATION page{};
  if (!VirtualQuery(reinterpret_cast<void *>(target), &page, sizeof(page)) ||
      page.State != MEM_COMMIT || (page.Protect & PAGE_GUARD) ||
      !(page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
    Log("force_lock_odds unavailable=chance_function");
    return;
  }
  nativeChance = reinterpret_cast<Chance>(target);
  // Some tweaks replace just the roll with a wrapper that consults the open
  // menu's pin position. The stock preview has its own two-argument formula.
  auto previewSite = exe + 0x38de26;
  auto previewBytes = reinterpret_cast<const unsigned char *>(previewSite);
  if (Readable(previewBytes-1,10) && previewBytes[-1] == 0x50 && previewBytes[0] == 0xe8 &&
      !std::memcmp(previewBytes+5,"\x83\xc4\x08\x50",4)) {
    int32_t previewOffset;
    std::memcpy(&previewOffset,previewBytes+1,sizeof(previewOffset));
    if (previewSite + 5 + previewOffset == exe + 0x248d30)
      previewChance = reinterpret_cast<Chance>(exe + 0x248d30);
  }
  if (configuredChance >= 0) {
    const NativeCall roll{site,target,reinterpret_cast<void *>(chanceForRoll)};
    overrideInstalled = InstallVerifiedCalls(&roll,1);
    if (!overrideInstalled) return;
  }
  // In both supported hosts the floating activation panel copies HUD tile 33
  // here, including when the hand points away from the camera's crosshair.
  auto reader = hostBias + (version117 ? 0x1011d5a0u : 0x1011ca70u);
  originalText = reinterpret_cast<TileText>(reader);
  const NativeCall hover{hostBias + (version117 ? 0x101379beu : 0x10136b3eu),
                         reader, reinterpret_cast<void *>(difficultyText)};
  hoverInstalled = InstallVerifiedCalls(&hover,1);
  Log("force_lock_odds hover=%d override=%d roll_hook=%d",hoverInstalled,configuredChance,overrideInstalled);
}

void TickForceHover() {
  if (!hoverInstalled) return;
  auto now = GetTickCount64();
  std::array<unsigned,4> pending{};
  {
    std::lock_guard guard(hoverMutex);
    for (size_t i=0; i<hovered.size(); ++i) {
      auto &entry = hovered[i];
      if (entry.id && now-entry.seen < 300 && (!entry.checked || now-entry.checked >= 250))
        pending[i] = entry.id;
    }
  }
  for (auto id : pending) {
    if (!id) continue;
    int chance = -1, level = HoverLockLevel(id);
    auto player = *reinterpret_cast<uintptr_t *>(exe + 0xddea3c);
    if (player && level >= 0 && level <= 100) {
      int rank = reinterpret_cast<int(__cdecl *)(int)>(exe + 0x30b40)(level);
      if (rank >= 0 && rank <= 4) {
        int threshold = reinterpret_cast<const int *>(exe + 0xdda28c)[rank];
        int skill = reinterpret_cast<int(__thiscall *)(uintptr_t,int)>(exe + 0x26ef20)(player+0xa4,0x24);
        if (skill >= threshold) {
          if (configuredChance >= 0) chance = configuredChance;
          else if (previewChance) chance = std::clamp(previewChance(skill,threshold),0,100);
        }
      }
    }
    std::lock_guard guard(hoverMutex);
    for (auto &entry : hovered) if (entry.id == id) {
      entry.chance = chance;
      entry.checked = now;
    }
  }
}

bool ForceAttemptAllowed() { return enabled && (configuredChance < 0 || overrideInstalled); }
ScopedForceChance::ScopedForceChance() : previous(clickChance) { clickChance = configuredChance; }
ScopedForceChance::~ScopedForceChance() { clickChance = previous; }
} // namespace locks
