// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Collision.h"
namespace locks {
// Same dimensions as the rendered tin. Open top, solid bottom and side wall.
inline float canDistance(Vec p) {
  float radial = std::sqrt(p.x * p.x + p.z * p.z);
  auto capped = [&](float radius, float lo, float hi) {
    float r = radial - radius, y = std::max(lo - p.y, p.y - hi);
    if (r <= 0 || y <= 0)
      return std::max(r, y);
    return std::sqrt(r * r + y * y);
  };
  return std::max(capped(.032f, -.155f, -.025f), -capped(.028f, -.151f, .1f));
}
inline Vec canNormal(Vec p) {
  constexpr float e = .00005f;
  return normal(
      {canDistance(p + Vec{e, 0, 0}) - canDistance(p - Vec{e, 0, 0}),
       canDistance(p + Vec{0, e, 0}) - canDistance(p - Vec{0, e, 0}),
       canDistance(p + Vec{0, 0, e}) - canDistance(p - Vec{0, 0, e})});
}
inline Pose looseDriverStart(const Ergonomics &c) {
  Quat q = multiply(axisAngle({0, 0, 1}, -.16f), axisAngle({1, 0, 0}, -pi / 2));
  return {q, holderPoint(c, 1) + Vec{-.010f, -.145f, 0}};
}
// Sequential contact impulses with rotational inertia: the tip settles on
// the bottom while the shaft/handle can lean on any point of the rim.
inline bool contactCan(Pose &pose, Vec &velocity, Vec &angular,
                       const Ergonomics &c, const ToolShape &shape) {
  Vec origin = holderPoint(c, 1);
  if (length(pose.p - origin) > .4f)
    return false;
  bool hit = false;
  constexpr float inverseInertia = 400.f;
  for (int pass = 0; pass < 8; ++pass) {
    float deepest = .00005f;
    Vec point{};
    for (size_t n = 0; n < shape.count; ++n) {
      Vec p = world(pose, shape.balls[n].p);
      float radius = shape.balls[n].r;
      float y = p.y - origin.y;
      if (y > -.025f + radius + .00005f || y < -.155f - radius - .00005f)
        continue;
      float d = canDistance(p - origin) - radius;
      if (d < deepest) {
        deepest = d;
        point = p;
      }
    }
    if (deepest >= .00005f)
      break;
    hit = true;
    Vec n = canNormal(point - origin), com = world(pose, toolCenter(1));
    Vec arm = point - com, torque = cross(arm, n);
    float mass = 1 + inverseInertia * dot(torque, torque);
    float correction = (.00006f - deepest) / mass;
    com = com + n * correction;
    Vec rotation = torque * (correction * inverseInertia);
    float angle = length(rotation);
    if (angle > 1e-7f)
      pose.q =
          unit(multiply(axisAngle(rotation, std::min(angle, .06f)), pose.q));
    pose.p = com - rotate(pose.q, toolCenter(1));
    Vec v = velocity + cross(angular, arm);
    float vn = dot(v, n);
    if (vn < 0) {
      float impulse = -vn / mass;
      velocity = velocity + n * impulse;
      angular = angular + torque * (impulse * inverseInertia);
      Vec tangent = v - n * vn;
      float speed = length(tangent);
      if (speed > 1e-6f) {
        Vec t = tangent * (1 / speed), rt = cross(arm, t);
        float friction = std::min(.65f * impulse,
                                  speed / (1 + inverseInertia * dot(rt, rt)));
        velocity = velocity - t * friction;
        angular = angular - rt * (friction * inverseInertia);
      }
    }
  }
  return hit;
}
} // namespace locks
