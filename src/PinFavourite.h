// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace locks {
bool InstallPinFavourite(uintptr_t imageBase);
void EnablePinFavourite(bool enabled);
unsigned TakePinFavourite();
} // namespace locks
