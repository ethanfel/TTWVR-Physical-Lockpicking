// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LockStyle.h"
#include <cstdint>
namespace locks {
// Outward depth sampled on the closed target's visible triangles. Empty
// cells include alpha-tested openings. Coordinates match the hardware mesh.
struct MountSurfaceGrid {
  static constexpr int side = 81;
  static constexpr float spacing = .004f;
  std::array<int16_t, side * side> depths;
  float highest = .16f; // Maximum supported by the asset format before loading.
  void measureHeight() {
    highest = *std::max_element(depths.begin(), depths.end()) * .00001f;
  }
  float front(Vec p) const {
    float u = p.x / spacing + side / 2, v = p.y / spacing + side / 2;
    if (!(u >= 0 && u <= side - 1 && v >= 0 && v <= side - 1))
      return -1e6f;
    int x = int(u + .5f), y = int(v + .5f);
    auto d = depths[y * side + x];
    return d == INT16_MIN ? -1e6f : d * .00001f;
  }
};

struct TargetBounds {
  Vec low, high;
  bool enabled = false;
  LockStyle style = LockStyle::Round;
  float toolScale[2]{1,1};
  const MountSurfaceGrid *panel = nullptr;
};
} // namespace locks
