// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace locks {
bool InstallPinFavourite(uintptr_t imageBase);
bool InstallWristPinFavourite(uintptr_t moduleBase, bool version117);
void EnablePinFavourite(bool enabled);
unsigned TakePinFavourite();
} // namespace locks
