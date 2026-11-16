// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Interaction.h"
#include "LockStyle.h"
#include "Settings.h"
namespace locks {
// Working clearance inside the uniformly scaled original keyhole recess.
constexpr float lockPlugRadius = .05084f;
constexpr float keywayHalfWidth = .0071f, keywayHalfHeight = .0294f;
inline float plugRadius(LockStyle style) {
  return style == LockStyle::Padlock ? padPlugRadius : lockPlugRadius;
}
inline float keywayHeight(LockStyle style) {
  return keywayHalfHeight * (style == LockStyle::Padlock ? padPlugYScale : 1.f);
}
constexpr float pinEntryMargin = .0017f;
inline Quat unit(Quat q) {
  float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return n > 1e-6f && std::isfinite(n)
             ? Quat{q.x / n, q.y / n, q.z / n, q.w / n}
             : Quat{};
}
inline Quat axisAngle(Vec axis, float angle) {
  Vec v = normal(axis) * std::sin(angle / 2);
  return {v.x, v.y, v.z, std::cos(angle / 2)};
}
inline Pose compose(Pose a, Pose b) {
  return {unit(multiply(a.q, b.q)), world(a, b.p)};
}
inline Pose relative(Pose a, Pose b) {
  return {unit(multiply(inverse(a.q), b.q)), local(a, b.p)};
}
inline Vec bounded(Vec v, float limit) {
  float n = length(v);
  return n > limit ? v * (limit / n) : v;
}
// A precision grip is easier to read when the palm is behind the wire.
// Only the displayed hand is corrected; controller poses and tool input stay
// untouched. The pads keep contact 10 mm nearer the end of the held leg.
inline Pose presentPinHand(Pose tracked, Vec contact, Vec pinchAnchor,
                           float maxMove, float maxAngle) {
  Pose shown = tracked;
  float angle = std::clamp(maxAngle, 0.f, 20.f) * pi / 180;
  shown.q = unit(multiply(tracked.q, axisAngle({1, 0, 0}, -angle)));
  shown.p = tracked.p + bounded(contact - world(shown, pinchAnchor),
                                std::clamp(maxMove, 0.f, .04f));
  return shown;
}
inline Vec angularVelocity(Quat a, Quat b, float dt) {
  Quat d = unit(multiply(b, inverse(a)));
  if (d.w < 0)
    d = {-d.x, -d.y, -d.z, -d.w};
  Vec v{d.x, d.y, d.z};
  float n = length(v);
  return n > 1e-5f && dt > 1e-5f
             ? bounded(v * (2 * std::atan2(n, d.w) / (n * dt)), 12.f)
             : Vec{};
}
// The pin keeps its held leg still while the free leg unfolds into an L.
// Coordinates remain tip-relative for insertion, physics and rendering.
inline Vec pinHinge(float opening, float twist = 0) {
  float a = std::clamp(opening, 0.f, 1.8f) * pi / 2;
  return {.075f * std::sin(a) * std::sin(twist), -.075f * std::cos(a),
          .075f * std::sin(a) * std::cos(twist)};
}
inline bool pinUsable(float opening) {
  return opening >= 80.f / 90 && opening <= 130.f / 90;
}
// Only a still-folded pin needs preparation. Poor partial bends remain
// usable; their quality continuously changes handling, never the solution.
inline bool pinCanEngage(float opening) { return opening >= .4f; }
inline float pinQuality(float opening) {
  float degrees = opening * 90;
  float error =
      degrees < 90 ? (90 - degrees) / 55 : std::max(0.f, degrees - 105) / 42;
  return 1 - .85f * std::clamp(error, 0.f, 1.f);
}
inline float pinMaxDepth(float opening) {
  // Leave the held leg clear of the front face, even on an under-bend.
  return std::clamp(.075f - .085f * std::max(0.f, std::cos(opening * pi / 2)) -
                        .008f,
                    .003f, .067f);
}
inline float pinHandlingQuality(float opening, float alignment, float depth) {
  return pinQuality(opening) * std::clamp(alignment, .25f, 1.f) *
         (.75f + .25f * std::clamp(depth / pinMaxDepth(opening), 0.f, 1.f));
}
inline Segment toolHandle(int k, float opening = 1, float twist = 0) {
  if (k == 0) {
    Vec hinge = pinHinge(opening, twist);
    return {hinge + Vec{.006f * std::max(0.f, 1 - opening), .050f, 0},
            hinge + Vec{.006f * std::max(0.f, 1 - opening), .080f, 0}};
  }
  return {{0, 0, .112f}, {0, 0, .182f}};
}
inline Vec handleCenter(int k, float opening = 1, float twist = 0) {
  auto h = toolHandle(k, opening, twist);
  return (h.a + h.b) * .5f;
}
inline Vec toolCenter(int k, float opening = 1, float twist = 0) {
  if (k == 0)
    return pinHinge(opening, twist) * .75f +
           Vec{.003f * std::max(0.f, 1 - opening), .02125f, 0};
  auto h = toolHandle(k, opening, twist);
  return (h.a + h.b) * (1.f / 3);
}
inline Vec mouth(int k) { return {0, k == 0 ? .0027f : -.0027f, .026f}; }
inline Vec pinPivot(float opening = 1) {
  // An under-bent handle needs a little more stand-off to avoid the face.
  return {0, .0027f, .034f + .087f * std::max(0.f, std::cos(opening * pi / 2))};
}
inline Vec socket(int k) {
  return k == 0 ? pinPivot() - pinHinge(1) : mouth(1) - Vec{0, 0, .018f};
}
inline float pinBearing(Vec hand, Vec pivot = pinPivot()) {
  Vec v = hand - pivot;
  return std::atan2(v.x, v.y) * 180 / pi;
}
inline Quat pinAlignment(float opening, float twist);
inline Pose seatedPose(int k, float pick, float cylinder, float opening = 1,
                       float twist = 0) {
  Quat q = axisAngle({0, 0, 1}, -(k == 0 ? pick - 90 : cylinder) * pi / 180);
  if (k == 0)
    q = unit(multiply(q, pinAlignment(opening, twist)));
  return {q, k == 0 ? pinPivot(opening) - rotate(q, pinHinge(opening, twist))
                    : socket(1)};
}
inline Pose engagedPinPose(float pick, float depth, float opening,
                           float twist) {
  Pose p = seatedPose(0, pick, 0, opening, twist);
  p.p = mouth(0) - Vec{0, 0, std::clamp(depth, 0.f, pinMaxDepth(opening))};
  return p;
}
inline Pose tremblingPinPose(Pose p, float degrees) {
  // Visual roll about the keyway: no change to input, collision or solution.
  Quat shake = axisAngle({0, 0, 1}, degrees * pi / 180);
  return {unit(multiply(shake, p.q)), mouth(0) + rotate(shake, p.p - mouth(0))};
}
constexpr int pinBoxCapacity = 18;
inline int pinsInBox(int inventory, bool activeOutside) {
  return std::clamp((inventory < 0 ? 1 : inventory) - (activeOutside ? 1 : 0),
                    0, pinBoxCapacity);
}
inline Vec holderPoint(const Ergonomics &c, int k) {
  return {c.holderSide[k], c.lockBelowEyes - c.holderBelowEyes[k],
          c.lockDistance - c.holderForward[k]};
}
inline Pose holderTool(const Ergonomics &c, int k, float opening,
                       float twist = 0) {
  Quat q = k == 0 ? Quat{} : axisAngle({1, 0, 0}, -pi / 2);
  return {q, holderPoint(c, k) - rotate(q, handleCenter(k, opening, twist))};
}
inline Vec pinBendPoint(float opening, float t, float twist = 0) {
  Vec h = pinHinge(opening, twist),
      spread{.006f * std::max(0.f, 1 - opening), 0, 0};
  Vec a = h - normal(h) * .004f, b = h + spread + Vec{0, .004f, 0};
  Vec control =
      h + spread * .5f - Vec{0, .006f * std::max(0.f, 1 - opening), 0};
  return a * ((1 - t) * (1 - t)) + control * (2 * t * (1 - t)) + b * (t * t);
}
inline Vec pinHandlePoint(float opening, float t, float twist = 0) {
  return pinHinge(opening, twist) +
         Vec{.006f * std::max(0.f, 1 - opening) + .0015f * std::sin(t * 8 * pi),
             .004f + .081f * t, 0};
}
inline Quat shortestArc(Vec from, Vec to) {
  from = normal(from);
  to = normal(to);
  float d = std::clamp(dot(from, to), -1.f, 1.f);
  if (d < -.9999f) {
    Vec axis =
        cross(from, std::abs(from.x) < .8f ? Vec{1, 0, 0} : Vec{0, 1, 0});
    return axisAngle(axis, pi);
  }
  Vec v = cross(from, to);
  return unit({v.x, v.y, v.z, 1 + d});
}
inline Quat pinAlignment(float opening, float twist) {
  Quat align = shortestArc(pinHinge(opening, twist), {0, 0, 1});
  Vec up = rotate(align, {0, 1, 0});
  return unit(multiply(axisAngle({0, 0, 1}, std::atan2(up.x, up.y)), align));
}
inline Pose turnWithCylinder(Pose p, float degrees) {
  Quat q = axisAngle({0, 0, 1}, -degrees * pi / 180);
  return {unit(multiply(q, p.q)), rotate(q, p.p)};
}
inline float pinPoseAngle(Pose p, float cylinder) {
  // Search is the visible held leg's angle in the rotating cylinder frame,
  // independent of the contact's lateral position or an off-centre hand.
  Vec up =
      rotate(axisAngle({0, 0, 1}, cylinder * pi / 180), rotate(p.q, {0, 1, 0}));
  return 90 + std::atan2(up.x, up.y) * 180 / pi;
}
inline Pose limitPinSearch(Pose p, Vec contact, float cylinder) {
  float angle = pinPoseAngle(p, cylinder);
  float excess = angle - std::clamp(angle, 0.f, 180.f);
  Quat q = axisAngle({0, 0, 1}, excess * pi / 180);
  return {unit(multiply(q, p.q)), contact + rotate(q, p.p - contact)};
}
inline Pose heldTool(Pose hand, int k, float opening = 1, float twist = 0) {
  // The runtime's aim orientation gives a familiar pointing direction. The
  // handle is centred on the tracked grip; stored orientation is not inherited.
  return {hand.q, hand.p - rotate(hand.q, handleCenter(k, opening, twist))};
}
inline Pose aimedTool(Pose hand, Vec pivot, float depth = .018f) {
  Vec toward = hand.p - pivot;
  toward.z = std::max(toward.z, .04f);
  // Keep the thin blade beneath the pin. A 20-degree cone gives modest tilt
  // while both cross-sections stay inside the rotating central keyway.
  toward.y = std::min(toward.y, 0.f);
  float transverse = std::hypot(toward.x, toward.y),
        limit = toward.z * std::tan(20 * pi / 180);
  if (transverse > limit) {
    toward.x *= limit / transverse;
    toward.y *= limit / transverse;
  }
  Quat q =
      unit(multiply(shortestArc(rotate(hand.q, {0, 0, 1}), toward), hand.q));
  return {q, pivot - rotate(q, {0, 0, depth})};
}
inline Pose guidedPin(Pose hand, Vec tip, float opening, float twist) {
  Vec pivot = tip + Vec{0, 0, .075f};
  float angle = std::clamp(90 + pinBearing(hand.p, pivot), 0.f, 180.f);
  return {unit(multiply(axisAngle({0, 0, 1}, -(angle - 90) * pi / 180),
                        pinAlignment(opening, twist))),
          tip};
}
inline Quat rotateTowards(Quat current, Quat target, float degrees) {
  Quat delta = unit(multiply(target, inverse(current)));
  if (delta.w < 0)
    delta = {-delta.x, -delta.y, -delta.z, -delta.w};
  Vec v{delta.x, delta.y, delta.z};
  float n = length(v);
  if (n < 1e-6f)
    return current;
  float angle = 2 * std::atan2(n, delta.w);
  return unit(
      multiply(axisAngle(v, std::min(angle, degrees * pi / 180)), current));
}
inline float wristRoll(Quat start, Quat current) {
  Quat delta = unit(multiply(inverse(start), current));
  if (delta.z * delta.z + delta.w * delta.w < 1e-8f)
    return 0;
  return std::remainder(-2 * std::atan2(delta.z, delta.w) * 180 / pi, 360.f);
}
} // namespace locks
