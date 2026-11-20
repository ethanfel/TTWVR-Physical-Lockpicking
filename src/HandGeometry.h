// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
namespace locks {
struct SceneVertex { Vec p, c, uv, n; };
// A pending game-thread packet owns its immutable geometry even if the renderer switches targets.
using LocalVertices = std::shared_ptr<const std::vector<SceneVertex>>;
struct HandVertex {
  Vec open, closed;
  float u, v;
  Vec normalOpen, normalClosed, pinched, normalPinched;
};
struct HandMesh {
  Vec pinchPoint;
  Pose driverGrip;
  bool hasDriverGrip = false;
  bool hasPinch = false;
  std::vector<HandVertex> vertices;
  std::vector<std::array<uint32_t, 3>> triangles;

  // A pending scene packet can still own the previous finger shape.
  LocalVertices nativeVertices(float curl, bool pinch) {
    if (cached && curl == cachedCurl && pinch == cachedPinch)
      return cached;
    auto shape = std::make_shared<std::vector<SceneVertex>>();
    shape->reserve(triangles.size() * 3);
    for (const auto &tri : triangles)
      for (auto index : tri) {
        const auto &v = vertices[index];
        Vec p = v.open * (1 - curl) + (pinch ? v.pinched : v.closed) * curl;
        Vec n = normal(v.normalOpen * (1 - curl) +
                       (pinch ? v.normalPinched : v.normalClosed) * curl);
        shape->push_back({p, {1, 1, 1}, {v.u, v.v, 1}, n});
      }
    cachedCurl = curl;
    cachedPinch = pinch;
    cached = std::move(shape);
    return cached;
  }

private:
  LocalVertices cached;
  float cachedCurl = 0;
  bool cachedPinch = false;
};

// The close-up shades skin against a fixed light in workspace coordinates.
inline Vec handLocalLight(Quat rotation) {
  return rotate(inverse(rotation), normal({-.3f, .6f, 1}));
}
inline float handShade(Vec n, Vec light) {
  return .55f + .45f * std::abs(dot(n, light));
}
} // namespace locks
