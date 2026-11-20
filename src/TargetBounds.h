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
// Prepared door vertices use the front anchor, with its mounting surface at Z=-.013.
inline Pose reverseDoorPose(Vec anchorDelta) {
  const Quat flip{0, 1, 0, 0};
  const Vec surface{0, 0, -.013f};
  return {flip, surface - rotate(flip, surface + anchorDelta)};
}
inline TargetBounds reverseDoorBounds(TargetBounds b, Pose face) {
  const Vec lo = world(face, b.high), hi = world(face, b.low);
  b.low = {lo.x, hi.y, std::min(lo.z, -.014f)};
  b.high = {hi.x, lo.y, -.013f}; // Leave the keyhole clear of projecting knobs.
  return b;
}
// Target bounds are in the front lock frame, with the lock at X=0.
inline int doorTwistDirection(const TargetBounds &b, bool reverse, float reverseAnchorX) {
  if (!b.enabled) return 1;
  const float center = (b.low.x + b.high.x) * .5f;
  const float lockX = reverse ? center - reverseAnchorX : -center;
  // Hardware at the center, and containers, use the conventional clockwise turn.
  return lockX > (b.high.x - b.low.x) * .025f ? -1 : 1;
}
} // namespace locks
