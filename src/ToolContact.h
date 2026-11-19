// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Collision.h"
namespace locks {
// Exact distance to a capped circular frustum in its (radius, axial) plane.
inline float taperedDriverDistance(Vec p, float lo, float hi, float r0,
                                   float r1) {
  Vec q{std::hypot(p.x, p.y), p.z, 0};
  float radius = r0 + (r1 - r0) * std::clamp((p.z - lo) / (hi - lo), 0.f, 1.f);
  float distance = std::min({distanceTo(q, {{r0, lo, 0}, {r1, hi, 0}}),
                             distanceTo(q, {{0, lo, 0}, {r0, lo, 0}}),
                             distanceTo(q, {{0, hi, 0}, {r1, hi, 0}})});
  return p.z >= lo && p.z <= hi && q.x <= radius ? -distance : distance;
}
inline float driverDistance(Vec p) {
  // Conservative signed distance to the broad flat blade and its neck.
  // Local Y is the broad axis, matching the blade in the thumb-up grip.
  auto taperedBox = [&](float lo, float hi, float x0, float x1,
                        float y0, float y1) {
    float sx = (x1-x0)/(hi-lo), sy = (y1-y0)/(hi-lo);
    return std::max({(std::abs(p.y)-x0-sx*(p.z-lo))/std::sqrt(1+sx*sx),
                     (std::abs(p.x)-y0-sy*(p.z-lo))/std::sqrt(1+sy*sy),
                     lo-p.z, p.z-hi});
  };
  float blade = taperedBox(0, driverBladeEnd, driverBladeTipHalfWidth,
                           driverBladeHalfWidth, driverBladeHalfThickness,
                           driverBladeHalfThickness);
  return std::min(
      {blade,
       taperedBox(driverBladeEnd, driverShaftStart, driverBladeHalfWidth,
                  .0017f, driverBladeHalfThickness, .0034f),
       taperedDriverDistance(p, driverShaftStart, driverHandleStart, .004f,
                             .002f),
       cylinderDistance(p, .0138f, driverHandleStart, driverLength)});
}
inline float scaledDriverDistance(Vec p,float scale) {
  return driverDistance(p*(1/scale))*scale;
}
inline float pinDriverClearance(const ToolShape &pin, Pose pinPose,
                                Pose driverPose) {
  float result = 1e9f;
  for (size_t n = 0; n < pin.count; n++) {
    auto b = pin.balls[n];
    Vec p = local(driverPose, world(pinPose, b.p));
    result = std::min(result, scaledDriverDistance(p,pin.target.toolScale[1]) - b.r);
  }
  return result;
}
// The native cylinder can move a supported pin into yesterday's driver pose.
// Recover that externally introduced overlap before sweeping new hand motion;
// a sweep starting inside both solids can otherwise freeze the driver there.
inline Pose recoverDriverContact(const ToolShape &pin, Pose pinPose,
                                 const ToolShape &driver, Pose pose,
                                 float cylinder) {
  auto distance=[&](Vec p){return scaledDriverDistance(p,driver.target.toolScale[1]);};
  constexpr float e = .00002f;
  for (int pass = 0; pass < 24; ++pass) {
    pose = moveAgainstHousing(driver, pose, pose, cylinder).pose;
    // A moving slot can pinch a tilted blade between opposing walls. If
    // local normal projection has not separated it, let the face push it
    // outward instead of returning an embedded pose as an admitted start.
    for (int lift = 0; lift < 32 && driver.clearance(pose,cylinder) < .000005f; ++lift)
      pose.p.z += .0005f;
    float gap = 1e9f;
    Vec closest;
    for (size_t n = 0; n < pin.count; ++n) {
      Vec p = local(pose, world(pinPose, pin.balls[n].p));
      float d = distance(p) - pin.balls[n].r;
      if (d < gap) { gap = d; closest = p; }
    }
    if (gap >= .000005f)
      return pose;
    Vec gradient = normal({
        distance(closest + Vec{e, 0, 0}) - distance(closest - Vec{e, 0, 0}),
        distance(closest + Vec{0, e, 0}) - distance(closest - Vec{0, e, 0}),
        distance(closest + Vec{0, 0, e}) - distance(closest - Vec{0, 0, e})});
    pose.p = pose.p - rotate(pose.q, gradient) * (.00005f - gap);
  }
  return pose;
}
inline float poseTravel(Pose a, Pose b, float reach) {
  float cosine =
      std::abs(a.q.x * b.q.x + a.q.y * b.q.y + a.q.z * b.q.z + a.q.w * b.q.w);
  float theta = 2 * std::acos(std::clamp(cosine, 0.f, 1.f));
  return length(b.p - a.p) + 4 * std::tan(theta / 4) * reach;
}
struct ToolContact {
  Pose pin, driver;
  bool hit = false;
};
inline ToolContact sweepToolContact(const ToolShape &shape, Pose pinFrom,
                                    Pose pinTo, Pose driverFrom,
                                    Pose driverTo) {
  auto angularBound = [](Pose from, Pose to, Vec &axis) {
    Quat delta = unit(multiply(to.q, inverse(from.q)));
    axis = normal({delta.x, delta.y, delta.z});
    float angle =
        2 * std::atan2(length({delta.x, delta.y, delta.z}), std::abs(delta.w));
    return 4 * std::tan(angle / 4);
  };
  Vec pinAxis, driverAxis;
  float pinAngular = angularBound(pinFrom, pinTo, pinAxis),
        driverAngular = angularBound(driverFrom, driverTo, driverAxis),
        pinLinear = length(pinTo.p - pinFrom.p),
        driverLinear = length(driverTo.p - driverFrom.p);
  std::array<float, 512> speed{};
  for (size_t n = 0; n < shape.count; n++) {
    Vec offset = rotate(pinFrom.q, shape.balls[n].p);
    float pinMotion = pinLinear + pinAngular * length(cross(pinAxis, offset));
    // Driver rotation acts on a point measured from its origin. This bound
    // includes any translation during the sweep; pure pin-axis rotation does
    // not falsely assign the handle's speed to the stationary inserted wire.
    float driverRadius =
        length(pinFrom.p + offset - driverFrom.p) + pinMotion + driverLinear;
    speed[n] = pinMotion + driverLinear + driverAngular * driverRadius;
  }
  float t = 0;
  Pose p = pinFrom, d = driverFrom;
  for (int pass = 0; pass < 192; pass++) {
    if (t >= 1)
      return {pinTo, driverTo, false};
    float advance = 1 - t;
    for (size_t n = 0; n < shape.count; n++) {
      if (speed[n] < 1e-7f)
        continue;
      auto ball = shape.balls[n];
      float gap = scaledDriverDistance(local(d, world(p, ball.p)),shape.target.toolScale[1]) - ball.r;
      if (gap <= .000005f) {
        // Permit a deliberate escape from a pre-existing overlap, using a
        // bounded 50-micron probe. Never deepen it or skip a thin blade.
        float probe = std::min(.00005f / speed[n], 1 - t);
        Pose pp = interpolatePose(pinFrom, pinTo, t + probe),
             dd = interpolatePose(driverFrom, driverTo, t + probe);
        float next = scaledDriverDistance(local(dd, world(pp, ball.p)),shape.target.toolScale[1]) - ball.r;
        if (next <= gap + .00000001f)
          return {p, d, true};
        advance = std::min(advance, probe);
      } else
        advance = std::min(advance, .8f * gap / speed[n]);
    }
    t = std::min(1.f, t + advance);
    p = interpolatePose(pinFrom, pinTo, t);
    d = interpolatePose(driverFrom, driverTo, t);
  }
  return {p, d, true};
}
} // namespace locks
