// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Settings.h"
#include <cstdint>
namespace locks {
void ConfigureForceOdds(const Ergonomics &settings);
void InstallForceOdds(uintptr_t hostBias, bool version117);
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
