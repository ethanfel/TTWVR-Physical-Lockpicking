// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LockStyle.h"
#include "ModelDimensions.h"
#include "Props.h"
#include <map>
namespace locks {
enum class LockMeshSize { Unchanged, Round, RoundPlug, Padlock };
inline PropVertex roundLockVertex(PropVertex v) {
  v.p.x *= lockSizeRatio;
  v.p.y *= lockSizeRatio;
  v.n = normal({v.n.x / lockSizeRatio, v.n.y / lockSizeRatio, v.n.z});
  return v;
}
inline void fitSlotAxis(float &p, float &n, float sourceSlot, float slot,
                        float sourceRadius, float radius) {
  float slope;
  if (std::abs(p) <= sourceSlot) {
    slope = slot / sourceSlot;
    p *= slope;
  } else {
    slope = (radius - slot) / (sourceRadius - sourceSlot);
    p = std::copysign(slot + (std::abs(p) - sourceSlot) * slope, p);
  }
  n /= slope;
}
// Fit the textured recess to the approved interaction aperture. Styling the
// housing must not silently resize the pin, reach or grip coordinates.
inline PropVertex plugVertex(PropVertex v, bool pad) {
  float radius = pad ? padPlugRadius : lockPlugRadius;
  fitSlotAxis(v.p.x, v.n.x, .0071f, demoKeywayHalfWidth, assetPlugRadius,
              radius);
  fitSlotAxis(v.p.y, v.n.y, .0294f,
              demoKeywayHalfHeight,
              assetPlugRadius, radius);
  v.n = normal(v.n);
  return v;
}
inline PropVertex padlockPlugVertex(PropVertex v) {
  return plugVertex(v, true);
}
inline PropVertex fitLockVertex(PropVertex v, LockMeshSize size) {
  return size == LockMeshSize::Round       ? roundLockVertex(v)
         : size == LockMeshSize::RoundPlug ? plugVertex(v, false)
         : size == LockMeshSize::Padlock   ? padlockPlugVertex(v)
                                           : v;
}
// The native UI plug is an open face: its original frame hides the missing
// outer wall. Custom housings need a skirt welded to the actual front boundary.
inline PropMesh makePlugSkirt(const PropMesh &source, bool pad) {
  PropMesh result;
  std::map<std::array<int, 3>, unsigned> welded;
  std::vector<unsigned> ids;
  for (const auto &v : source.vertices) {
    std::array<int, 3> key{int(std::lround(v.p.x * 1e7f)),
                           int(std::lround(v.p.y * 1e7f)),
                           int(std::lround(v.p.z * 1e7f))};
    auto pair = welded.emplace(key, unsigned(welded.size()));
    ids.push_back(pair.first->second);
  }
  struct Edge {
    unsigned count = 0, a = 0, b = 0;
  };
  std::map<std::pair<unsigned, unsigned>, Edge> edges;
  for (auto face : source.triangles)
    for (int n = 0; n < 3; n++) {
      unsigned a = face[n], b = face[(n + 1) % 3];
      auto key = std::minmax(ids[a], ids[b]);
      auto &e = edges[{key.first, key.second}];
      e.count++;
      e.a = a;
      e.b = b;
    }
  auto emit = [&](PropVertex a, PropVertex b, PropVertex c) {
    Vec n = normal(cross(b.p - a.p, c.p - a.p));
    if (dot(n, Vec{a.p.x + b.p.x + c.p.x, a.p.y + b.p.y + c.p.y, 0}) < 0) {
      std::swap(b, c);
      n = n * -1;
    }
    a.n = b.n = c.n = n;
    unsigned first = unsigned(result.vertices.size());
    result.vertices.insert(result.vertices.end(), {a, b, c});
    result.triangles.push_back({first, first + 1, first + 2});
  };
  for (const auto &item : edges) {
    const auto &e = item.second;
    if (e.count != 1)
      continue;
    auto a = source.vertices[e.a], b = source.vertices[e.b];
    if (std::hypot(a.p.x, a.p.y) < assetPlugRadius * .9f ||
        std::hypot(b.p.x, b.p.y) < assetPlugRadius * .9f ||
        std::abs(a.p.z - .024f) > .00001f || std::abs(b.p.z - .024f) > .00001f)
      continue;
    a = plugVertex(a, pad);
    b = plugVertex(b, pad);
    float angle = std::atan2(a.p.y, a.p.x),
          delta = std::atan2(b.p.y, b.p.x) - angle;
    if (delta > pi)
      delta -= 2 * pi;
    if (delta < -pi)
      delta += 2 * pi;
    int sections = std::max(1, int(std::ceil(std::abs(delta) * 96 / (2 * pi))));
    auto top = [&](float t) {
      auto v = a;
      v.p = a.p + (b.p - a.p) * t;
      // Use the atlas's plain steel patch. Repeating the face boundary UV
      // down the wall has zero texture area and stretches its black seam.
      v.u = .68f + .16f * (angle + delta * t + pi) / (2 * pi);
      v.v = .14f;
      return v;
    };
    auto bottom = [&](float t) {
      auto v = top(t);
      float r = (pad ? padPlugRadius : lockPlugRadius) + .0005f;
      float q = angle + delta * t;
      v.p = {r * std::cos(q), r * std::sin(q), .0195f};
      v.v = .22f;
      return v;
    };
    for (int j = 0; j < sections; j++) {
      float t = float(j) / sections, u = float(j + 1) / sections;
      auto A = top(t), B = top(u), C = bottom(t), D = bottom(u);
      emit(A, B, C);
      emit(B, D, C);
    }
  }
  return result;
}
} // namespace locks
