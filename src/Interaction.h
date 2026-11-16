// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
namespace locks {
// Shared by hit testing and mesh construction, in lock-local metres.
struct Segment {
  Vec a, b;
};
inline Segment pickHandle(float angle) {
  float t = (angle - 90) * pi / 180;
  Vec p{std::sin(t) * .12f, std::cos(t) * .12f, .09f};
  return {p, p + Vec{.009f, -.002f, .012f}};
}
// Straight shaft along the lock's local Z axis. Twist changes only surface
// orientation, never the handle centerline or its distance from the hand.
inline constexpr Vec screwdriverTip{0, 0, .033f};
inline Segment screwdriverHandle() { return {{0, 0, .145f}, {0, 0, .215f}}; }
inline Vec screwdriverRadial(float clockwise) {
  float a = -clockwise * pi / 180;
  return {std::cos(a), std::sin(a), 0};
}
inline Quat multiply(Quat a, Quat b) {
  Vec av{a.x, a.y, a.z}, bv{b.x, b.y, b.z};
  Vec v = av * b.w + bv * a.w + cross(av, bv);
  return {v.x, v.y, v.z, a.w * b.w - dot(av, bv)};
}
inline float shaftTwist(Pose lock, Quat start, Quat current) {
  // Swing/twist decomposition about the actual shaft (lock-local Z), not
  // Euler roll or an assumed controller mounting angle.
  Quat delta = multiply(
      multiply(inverse(lock.q), multiply(current, inverse(start))), lock.q);
  if (delta.z * delta.z + delta.w * delta.w < 1e-8f)
    return 0;
  return std::remainder(-2 * std::atan2(delta.z, delta.w) * 180 / pi, 360.f);
}
inline float distanceTo(Vec p, Segment s) {
  Vec ab = s.b - s.a;
  float d = dot(ab, ab);
  float t = d > 1e-8f ? std::clamp(dot(p - s.a, ab) / d, 0.f, 1.f) : 0;
  return length(p - (s.a + ab * t));
}
inline float capCylinder(float nativeLimit, float handAngle) {
  return std::min(nativeLimit, std::clamp(handAngle, 0.f, 90.f));
}
struct Grab {
  bool armed = false, held = false;
  void reset() { armed = held = false; }
  bool update(float trigger, bool near) {
    if (trigger <= .2f) {
      held = false;
      armed = true;
      return false;
    }
    if (trigger >= .65f && armed) {
      armed = false;
      held = near;
      return held;
    }
    return false;
  }
};
struct Interaction {
  Grab grabs[2];
  int pickHand = -1, screwdriverHand = -1;
  float pick = 90, target = 0, pickOffset = 0, startCylinder = 0;
  Quat startOrientation;
  bool pickNear = false, screwdriverNear = false;
  void reset(float angle = 90) {
    for (auto &grab : grabs)
      grab.reset();
    pickHand = screwdriverHand = -1;
    pick = std::clamp(angle, 0.f, 180.f);
    target = 0;
    pickNear = screwdriverNear = false;
  }
  void drop(int hand) {
    grabs[hand].reset();
    if (pickHand == hand)
      pickHand = -1;
    if (screwdriverHand == hand)
      screwdriverHand = -1;
  }
  void update(Pose lock, Pose left, Pose right, float lt, float rt,
              float cylinder) {
    Pose hands[2]{left, right};
    float triggers[2]{lt, rt};
    Vec points[2]{local(lock, left.p), local(lock, right.p)};
    // Release both owners before arbitration, so a same-frame handoff does
    // not depend on which controller is processed first.
    for (int n = 0; n < 2; n++) {
      if (triggers[n] <= .2f)
        grabs[n].update(triggers[n], false);
      if (!grabs[n].held) {
        if (pickHand == n)
          pickHand = -1;
        if (screwdriverHand == n)
          screwdriverHand = -1;
      }
    }
    pickNear = screwdriverNear = false;
    for (int n = 0; n < 2; n++) {
      float dp = distanceTo(points[n], pickHandle(pick));
      float ds = distanceTo(points[n], screwdriverHandle());
      pickNear |= dp <= .04f;
      screwdriverNear |= ds <= .045f;
      if (grabs[n].held)
        continue;
      bool canPick = pickHand < 0 && dp <= .04f;
      bool canScrew = screwdriverHand < 0 && ds <= .045f;
      bool choosePick = canPick && (!canScrew || dp <= ds);
      if (!grabs[n].update(triggers[n], canPick || canScrew))
        continue;
      if (choosePick) {
        pickHand = n;
        pickOffset = pick - pickDegrees(lock, hands[n].p);
      } else {
        screwdriverHand = n;
        startOrientation = hands[n].q;
        startCylinder = cylinder;
      }
    }
    if (screwdriverHand >= 0 &&
        distanceTo(points[screwdriverHand], screwdriverHandle()) > .10f)
      drop(screwdriverHand);
    target = 0;
    if (screwdriverHand >= 0) {
      float delta =
          shaftTwist(lock, startOrientation, hands[screwdriverHand].q);
      if (std::abs(delta) < 1.5f)
        delta = 0;
      target = std::clamp(startCylinder + delta, 0.f, 90.f);
      // Avoid quaternion rounding just below native completion at 90 degrees.
      if (target >= 89.99f)
        target = 90.f;
    }
    if (pickHand >= 0) {
      float requested =
          (target > .5f || cylinder > .5f)
              ? pick
              : std::clamp(pickDegrees(lock, hands[pickHand].p) + pickOffset,
                           0.f, 180.f);
      if (distanceTo(points[pickHand], pickHandle(requested)) > .08f)
        drop(pickHand);
      else
        pick = requested;
    }
  }
};
} // namespace locks
