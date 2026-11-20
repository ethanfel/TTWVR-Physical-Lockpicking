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
constexpr float plugFaceZ = .024f;
inline float housingDistance(Vec p, Quat slotRotation, const TargetBounds &target) {
  float body;
  if (target.style == LockStyle::Padlock) {
    body = std::min(padBodyDistance(p), shackleDistance(p));
    body = std::min(body, stapleDistance(p));
  } else if (target.style == LockStyle::RoundedSafe) {
    body = safeBodyDistance(p);
  } else {
    body = cylinderDistance(p, roundFrameRadius, -.012f, .014f);
    body = std::min(body, cylinderDistance(p, roundShoulderRadius, .014f, .020f));
  }
  body = std::min(body, cylinderDistance(p, plugRadius(target.style), .020f, plugFaceZ));
  if (target.enabled)
    body = std::min(body, boxDistance(p - (target.low + target.high) * .5f,
                                      (target.high - target.low) * .5f));
  if (target.panel)
    body = std::min(body, p.z - target.panel->front(p));
  Vec hole = rotate(slotRotation, p);
  float bore = boxDistance(hole - Vec{0, 0, -.005f},
                           {keywayWidth(target.style), keywayHeight(target.style), .050f});
  float result = std::max(body, -bore);
  // The inserted shaft can extend beyond the decorative plug into the target.
  if (target.enabled || target.panel) {
    float channel = boxDistance(hole - Vec{0, 0, -.045f},
                                {keywayWidth(target.style), keywayHeight(target.style), .09f});
    result = std::max(result, -channel);
  }
  return result;
}
inline float housingDistance(Vec p, float angle = 0, const TargetBounds &target = {}) {
  return housingDistance(p, axisAngle({0, 0, 1}, angle * pi / 180), target);
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
  std::array<Ball, 512> balls{};
  size_t count = 0;
  float reach = 0;
  TargetBounds target;
  bool driver = false;
  float driverStopZ = mouth(1).z - driverBearingDepth;
  void add(Vec p, float r) {
    p=p*target.toolScale[driver];
    r*=target.toolScale[driver];
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
      : target(bounds), driver(k == 1) {
    if (k == 0) {
      Vec previous = pinBendPoint(opening, 0, twist);
      line({}, previous, pinWireRadius);
      for (int n = 1; n <= 12; n++) {
        Vec next = pinBendPoint(opening, n / 12.f, twist);
        line(previous, next, pinWireRadius);
        previous = next;
      }
      for (int n = 1; n <= 40; n++) {
        Vec next = pinHandlePoint(opening, n / 40.f, twist);
        line(previous, next, pinWireRadius);
        previous = next;
      }
      line(previous, previous + Vec{0, .002f, 0}, .0021f);
    } else {
      int rows = int(std::ceil(driverBladeEnd/.0012f));
      for (int z=0;z<=rows;z++) {
        float t=float(z)/rows, width=driverBladeTipHalfWidth+
                                      (driverBladeHalfWidth-driverBladeTipHalfWidth)*t;
        int columns=int(std::ceil(2*width/.0012f));
        for(int x=0;x<=columns;x++)
          add({0,-width+2*width*x/columns,driverBladeEnd*t},driverBladeHalfThickness);
      }
      line({0,0,driverBladeEnd},{0,0,driverShaftStart},.004f);
      line({0, 0, .042f}, {0, 0, .112f}, .004f);
      line({0, 0, .112f}, {0, 0, .182f}, .0138f);
    }
  }
  float distance(Vec point, Quat slotRotation) const {
    float d=housingDistance(point,slotRotation,target);
    if (!driver && target.style==LockStyle::Padlock) {
      // The slot is a narrow opening in the face, with room to angle a
      // prepared wire behind it. A constant-width tunnel binds a slightly
      // tilted shaft after a few millimetres. Keep a solid lip and back wall.
      Vec p=rotate(slotRotation,point);
      constexpr float throat=plugFaceZ-.002f, slope=.62f;
      constexpr float wallNormal=1.f/1.176605f; // 1/sqrt(1+slope*slope)
      float flare=(throat-p.z)*slope;
      float inside=std::min({(keywayWidth(target.style)+flare-std::abs(p.x))*wallNormal,
                             (keywayHeight(target.style)+flare-std::abs(p.y))*wallNormal,
                             throat-p.z, p.z+.135f});
      d=std::max(d,inside);
    }
    if(driver) {
      Vec p=rotate(slotRotation,point);
      // A driver-only internal bearing. The pin keeps its deeper channel.
      float front=driverStopZ;
      float stop=boxDistance(p-Vec{0,0,front-.075f},
                            {keywayWidth(target.style),keywayHeight(target.style),.075f});
      d=std::min(d,stop);
    }
    return d;
  }
  float distance(Vec point, float angle) const {
    return distance(point, axisAngle({0, 0, 1}, angle * pi / 180));
  }
  Vec normalAt(Vec p,float angle) const {
    constexpr float e=.00005f;
    return normal({distance(p+Vec{e,0,0},angle)-distance(p-Vec{e,0,0},angle),
                   distance(p+Vec{0,e,0},angle)-distance(p-Vec{0,e,0},angle),
                   distance(p+Vec{0,0,e},angle)-distance(p-Vec{0,0,e},angle)});
  }
  float clearance(Pose pose, float angle, Vec *point = nullptr) const {
    // A cheap conservative bound for tools nowhere near the housing.
    float broad =
        boxDistance(pose.p - Vec{0, 0, .006f}, {roundFrameRadius, roundFrameRadius, .018f}) - reach;
    if (!target.enabled && !target.panel && target.style == LockStyle::Round && broad > .01f &&
        !point)
      return broad;
    float result = 1e9f;
    auto rotation = axisAngle({0, 0, 1}, angle * pi / 180);
    for (size_t n = 0; n < count; n++) {
      Vec p = world(pose, balls[n].p);
      float d = distance(p, rotation) - balls[n].r;
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
  Quat delta = unit(multiply(to.q, inverse(from.q)));
  Vec axis = normal({delta.x, delta.y, delta.z});
  // atan2 retains tiny rotations whose quaternion dot rounds to one.
  float theta = 2 * std::atan2(length({delta.x, delta.y, delta.z}), std::abs(delta.w));
  // nlerp's maximum angular derivative is bounded by 4*tan(theta/4).
  float linear = length(to.p - from.p), angular = 4 * std::tan(theta / 4);
  std::array<float, 512> speed{};
  for (size_t n = 0; n < shape.count; n++)
    speed[n] = linear + angular * length(cross(axis, rotate(from.q, shape.balls[n].p)));
  Pose p = from;
  auto rotation = axisAngle({0, 0, 1}, angle * pi / 180);
  float t = 0;
  for (int n = 0; n < 64; n++) {
    float advance = 1 - t;
    for (size_t j = 0; j < shape.count; j++) {
      Vec point = world(p, shape.balls[j].p);
      float d = shape.distance(point, rotation) - shape.balls[j].r;
      // Use each wire sample's own motion bound. A nearly stationary shaft
      // beside the keyway must not throttle the distant, freely moving grip.
      // Positive grazing clearance remains usable down to five microns.
      if (d < .000005f)
        return {p, true, shape.normalAt(point, angle)};
      if (speed[j] > 1e-7f)
        advance = std::min(advance, .8f * d / speed[j]);
    }
    if (t >= 1)
      return {to, false, {}};
    t = std::min(1.f, t + advance);
    p = interpolatePose(from, to, t);
  }
  Vec point;
  shape.clearance(p, angle, &point);
  return {p, true, shape.normalAt(point, angle)};
}
inline HousingContact moveAgainstHousing(const ToolShape &shape, Pose from,
                                         Pose to, float angle = 0) {
  // The tip translates linearly, and reach encloses every ball at all rotations.
  // A sweep wholly in front of all solid geometry cannot touch it.
  float front = std::max({plugFaceZ, safeFront, padFront, shackleRadius,
                          stapleRadius + stapleWire, mouth(1, shape.target.style).z - driverBearingDepth});
  if (shape.target.enabled)
    front = std::max(front, shape.target.high.z);
  if (shape.target.panel) front=std::max(front,shape.target.panel->highest);
  if (std::min(from.p.z, to.p.z) - shape.reach > front + .00001f)
    return {to, false, {}};
  // Withdrawals and a changed bend can begin with slight overlap. Push out
  // before sweeping; never let an embedded start bypass the collision test.
  for (int n = 0; n < 16; n++) {
    Vec point;
    float d = shape.clearance(from, angle, &point);
    if (d >= .000005f)
      break;
    from.p = from.p + shape.normalAt(point, angle) * (.0003f - d);
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
