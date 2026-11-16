// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "TargetBounds.h"
#include "ToolGeometry.h"
namespace locks {
// Distances in metres, in the same lock frame as Render.cpp. The keyway is
// a bore through the circular face and rings; there is no mounting plate.
inline float boxDistance(Vec p, Vec half) {
  Vec q{std::abs(p.x) - half.x, std::abs(p.y) - half.y, std::abs(p.z) - half.z};
  return length({std::max(q.x, 0.f), std::max(q.y, 0.f), std::max(q.z, 0.f)}) +
         std::min(std::max({q.x, q.y, q.z}), 0.f);
}
inline float cylinderDistance(Vec p, float radius, float lo, float hi) {
  float r = std::hypot(p.x, p.y) - radius,
        z = std::abs(p.z - (lo + hi) * .5f) - (hi - lo) * .5f;
  return std::hypot(std::max(r, 0.f), std::max(z, 0.f)) +
         std::min(std::max(r, z), 0.f);
}
inline float housingDistance(Vec p, float angle = 0,
                             const TargetBounds &target = {}) {
  float body;
  if (target.style == LockStyle::Padlock) {
    body = std::min(padBodyDistance(p), shackleDistance(p));
    body = std::min(body, stapleDistance(p));
  } else if (target.style == LockStyle::RoundedSafe) {
    body = safeBodyDistance(p);
  } else {
    body = cylinderDistance(p, .075f, -.012f, .014f);
    body = std::min(body, cylinderDistance(p, .055f, .014f, .020f));
  }
  body = std::min(body, cylinderDistance(p, plugRadius(target.style), .020f, .024f));
  if (target.enabled)
    body = std::min(body, boxDistance(p - (target.low + target.high) * .5f,
                                      (target.high - target.low) * .5f));
  Vec hole = rotate(axisAngle({0, 0, 1}, angle * pi / 180), p);
  float bore = boxDistance(hole - Vec{0, 0, -.005f},
                           {keywayHalfWidth, keywayHeight(target.style), .050f});
  float result = std::max(body, -bore);
  // The inserted shaft can extend beyond the decorative plug into the target.
  if (target.enabled) {
    float channel = boxDistance(hole - Vec{0, 0, -.045f},
                                {keywayHalfWidth, keywayHeight(target.style), .09f});
    result = std::max(result, -channel);
  }
  return result;
}
inline Vec housingNormal(Vec p, float angle, const TargetBounds &target = {}) {
  constexpr float e = .00005f;
  return normal({housingDistance(p + Vec{e, 0, 0}, angle, target) -
                     housingDistance(p - Vec{e, 0, 0}, angle, target),
                 housingDistance(p + Vec{0, e, 0}, angle, target) -
                     housingDistance(p - Vec{0, e, 0}, angle, target),
                 housingDistance(p + Vec{0, 0, e}, angle, target) -
                     housingDistance(p - Vec{0, 0, e}, angle, target)});
}
struct ToolShape {
  struct Ball {
    Vec p;
    float r;
  };
  std::array<Ball, 256> balls{};
  size_t count = 0;
  float reach = 0;
  TargetBounds target;
  void add(Vec p, float r) {
    balls[count++] = {p, r};
    reach = std::max(reach, length(p) + r);
  }
  void line(Vec a, Vec b, float radius) {
    int steps = std::max(1, int(std::ceil(length(b - a) / .002f)));
    // Slightly overlapping balls conservatively cover the wire/shaft, not
    // just endpoints. A sideways wire cannot slip through the thin plate.
    float halfStep = length(b - a) / (2 * steps);
    float r = std::hypot(radius, halfStep);
    for (int n = 0; n <= steps; n++)
      add(a + (b - a) * (float(n) / steps), r);
  }
  ToolShape(int k, float opening = 1, float twist = 0, TargetBounds bounds = {})
      : target(bounds) {
    if (k == 0) {
      Vec previous = pinBendPoint(opening, 0, twist);
      line({}, previous, .0014f);
      for (int n = 1; n <= 12; n++) {
        Vec next = pinBendPoint(opening, n / 12.f, twist);
        line(previous, next, .0014f);
        previous = next;
      }
      for (int n = 1; n <= 40; n++) {
        Vec next = pinHandlePoint(opening, n / 40.f, twist);
        line(previous, next, .0014f);
        previous = next;
      }
      line(previous, previous + Vec{0, .002f, 0}, .0021f);
    } else {
      line({}, {0, 0, .042f}, .0021f);
      line({0, 0, .042f}, {0, 0, .112f}, .004f);
      line({0, 0, .112f}, {0, 0, .182f}, .0138f);
    }
  }
  float clearance(Pose pose, float angle, Vec *point = nullptr) const {
    // A cheap conservative bound for tools nowhere near the housing.
    float broad =
        boxDistance(pose.p - Vec{0, 0, .006f}, {.075f, .075f, .018f}) - reach;
    if (!target.enabled && target.style == LockStyle::Round && broad > .01f &&
        !point)
      return broad;
    float result = 1e9f;
    for (size_t n = 0; n < count; n++) {
      Vec p = world(pose, balls[n].p);
      float d = housingDistance(p, angle, target) - balls[n].r;
      if (d < result) {
        result = d;
        if (point)
          *point = p;
      }
    }
    return result;
  }
};
inline Pose interpolatePose(Pose a, Pose b, float t) {
  if (a.q.x * b.q.x + a.q.y * b.q.y + a.q.z * b.q.z + a.q.w * b.q.w < 0)
    b.q = {-b.q.x, -b.q.y, -b.q.z, -b.q.w};
  return {unit({a.q.x + (b.q.x - a.q.x) * t, a.q.y + (b.q.y - a.q.y) * t,
                a.q.z + (b.q.z - a.q.z) * t, a.q.w + (b.q.w - a.q.w) * t}),
          a.p + (b.p - a.p) * t};
}
struct HousingContact {
  Pose pose;
  bool hit = false;
  Vec normal;
};
inline HousingContact sweepHousing(const ToolShape &shape, Pose from, Pose to,
                                   float angle) {
  float cosine = std::abs(from.q.x * to.q.x + from.q.y * to.q.y +
                          from.q.z * to.q.z + from.q.w * to.q.w);
  // nlerp's maximum angular derivative is bounded by 4*tan(theta/4).
  float theta = 2 * std::acos(std::clamp(cosine, 0.f, 1.f));
  float motion = length(to.p - from.p) + 4 * std::tan(theta / 4) * shape.reach;
  Pose p = from;
  float t = 0;
  for (int n = 0; n < 64; n++) {
    float d = shape.clearance(p, angle);
    if (d < .00010f) {
      Vec point;
      shape.clearance(p, angle, &point);
      return {p, true, housingNormal(point, angle, shape.target)};
    }
    if (motion < 1e-7f || t >= 1)
      return {to, false, {}};
    t = std::min(1.f, t + .8f * d / motion);
    p = interpolatePose(from, to, t);
  }
  Vec point;
  shape.clearance(p, angle, &point);
  return {p, true, housingNormal(point, angle, shape.target)};
}
inline HousingContact moveAgainstHousing(const ToolShape &shape, Pose from,
                                         Pose to, float angle = 0) {
  // Withdrawals and a changed bend can begin with slight overlap. Push out
  // before sweeping; never let an embedded start bypass the collision test.
  for (int n = 0; n < 16; n++) {
    Vec point;
    float d = shape.clearance(from, angle, &point);
    if (d >= .0001f)
      break;
    from.p = from.p + housingNormal(point, angle, shape.target) * (.0003f - d);
  }
  auto result = sweepHousing(shape, from, to, angle);
  if (!result.hit)
    return result;
  Vec firstNormal = result.normal;
  // Slide on the surface. Keep rotation at contact: rotating a held tool
  // through the face must not make its remote end tunnel through the housing.
  for (int pass = 0; pass < 2; pass++) {
    Vec remainder = to.p - result.pose.p;
    remainder = remainder -
                result.normal * std::min(0.f, dot(remainder, result.normal));
    Pose start = result.pose;
    start.p = start.p + result.normal * .00025f;
    Pose end = start;
    end.p = end.p + remainder;
    auto slide = sweepHousing(shape, start, end, angle);
    result.pose = slide.pose;
    if (!slide.hit)
      break;
    result.normal = slide.normal;
  }
  result.hit = true;
  result.normal = firstNormal;
  return result;
}
} // namespace locks
