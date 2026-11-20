// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Shared.h"
namespace locks {
void InitKit(void *scripts);
void ResetKit();
bool RequestCloseupExit(uintptr_t menu);
bool DismissLockMenu(const GameState &g);
void TickKit(GameState &);
void KitButtons(unsigned down, unsigned valid, unsigned inRange, int first, unsigned uncertainRelease, unsigned holsterDraw);
unsigned KitReach(const GameState &, const Vec hands[2], unsigned valid);
} // namespace locks
