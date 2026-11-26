// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "HostLayout.h"
#include <cstdint>

namespace locks {
bool InstallPinFavourite(uintptr_t imageBase);
bool InstallWristPinFavourite(uintptr_t moduleBase, const HostLayout::WristInventory &inventory);
void EnablePinFavourite(bool enabled);
unsigned TakePinFavourite();
} // namespace locks
