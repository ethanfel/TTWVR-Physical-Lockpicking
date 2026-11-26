// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "HostLayout.h"
#include "Settings.h"
#include <cstdint>
namespace locks {
void ConfigureForceOdds(const Ergonomics &settings);
void InstallForceOdds(uintptr_t host, HostLayout::Call hoverText);
void TickForceHover();
bool ForceAttemptAllowed();
class ScopedForceChance {
  int previous;
public:
  ScopedForceChance();
  ~ScopedForceChance();
  ScopedForceChance(const ScopedForceChance &) = delete;
};
} // namespace locks
