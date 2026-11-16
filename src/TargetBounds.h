// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LockStyle.h"
namespace locks {
struct TargetBounds {
  Vec low, high;
  bool enabled = false;
  LockStyle style = LockStyle::Round;
};
} // namespace locks
