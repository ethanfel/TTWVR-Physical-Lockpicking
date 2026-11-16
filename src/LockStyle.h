// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
namespace locks {
enum class LockStyle : unsigned { Round = 0, Padlock = 1, RoundedSafe = 2 };
// Metres in the existing lock frame. The tool contact plane stays unchanged.
constexpr float padHalfX = .038f, padHalfY = .032f, padCorner = .005f;
constexpr float padBack = -.014f, padFront = .020f;
constexpr float shackleHalfWidth = .024f, shackleShoulder = .050f;
constexpr float shackleBottom = .023f, shackleRightBottom = .006f,
                shackleRadius = .004f;
constexpr float stapleY = shackleShoulder + shackleHalfWidth;
constexpr float stapleRadius = .008f, stapleWire = .002f;
constexpr float padPlugRadius = .025f, padPlugYScale = .48f;
constexpr float safeHalf = .073f, safeCorner = .014f, safeBevel = .004f;
constexpr float safeBack = -.012f, safeFront = .020f;
inline float safeOutlineDistance(float x, float y, float inset = 0) {
  float a = std::abs(x) - (safeHalf - safeCorner),
        b = std::abs(y) - (safeHalf - safeCorner);
  return std::hypot(std::max(a, 0.f), std::max(b, 0.f)) +
         std::min(std::max(a, b), 0.f) - safeCorner + inset;
}
inline float safeBodyDistance(Vec p) {
  float a = safeOutlineDistance(p.x, p.y),
        b = std::abs(p.z - (safeBack + safeFront) * .5f) -
            (safeFront - safeBack) * .5f;
  float body = std::hypot(std::max(a, 0.f), std::max(b, 0.f)) +
               std::min(std::max(a, b), 0.f);
  return std::max(body, (a + p.z - (safeFront - safeBevel)) * .70710678f);
}
inline Vec safeRim(float angle, float z, float inset = 0) {
  float c = std::cos(angle), s = std::sin(angle), lo = 0, hi = .11f;
  for (int i = 0; i < 24; i++) {
    float m = (lo + hi) * .5f;
    if (safeOutlineDistance(c * m, s * m, inset) > 0) hi = m;
    else lo = m;
  }
  return {c * (lo + hi) * .5f, s * (lo + hi) * .5f, z};
}
inline float padOutlineDistance(float x, float y) {
  float a = std::abs(x) - (padHalfX - padCorner),
        b = std::abs(y) - (padHalfY - padCorner);
  return std::hypot(std::max(a, 0.f), std::max(b, 0.f)) +
         std::min(std::max(a, b), 0.f) - padCorner;
}
inline float padBodyDistance(Vec p) {
  float a = padOutlineDistance(p.x, p.y),
        b = std::abs(p.z - (padBack + padFront) * .5f) -
            (padFront - padBack) * .5f;
  return std::hypot(std::max(a, 0.f), std::max(b, 0.f)) +
         std::min(std::max(a, b), 0.f);
}
inline float shackleDistance(Vec p) {
  float y = p.y - shackleShoulder;
  if (y >= 0)
    return std::hypot(std::hypot(p.x, y) - shackleHalfWidth, p.z) -
           shackleRadius;
  float dy =
      p.y - std::clamp(p.y, p.x >= 0 ? shackleRightBottom : shackleBottom,
                       shackleShoulder);
  return length({std::abs(p.x) - shackleHalfWidth, dy, p.z}) - shackleRadius;
}
inline float stapleDistance(Vec p) {
  return std::hypot(std::hypot(p.y - stapleY, p.z) - stapleRadius, p.x) -
         stapleWire;
}
// Mesh and signed-distance collision share the same rounded rectangle.
inline Vec padRim(float angle, float z) {
  float c = std::cos(angle), s = std::sin(angle), lo = 0, hi = .11f;
  for (int i = 0; i < 24; i++) {
    float m = (lo + hi) * .5f;
    if (padOutlineDistance(c * m, s * m) > 0)
      hi = m;
    else
      lo = m;
  }
  return {c * (lo + hi) * .5f, s * (lo + hi) * .5f, z};
}
} // namespace locks
