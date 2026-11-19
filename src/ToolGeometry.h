// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Interaction.h"
#include "LockStyle.h"
#include "ModelDimensions.h"
#include "Settings.h"
namespace locks {
// Approved demo aperture; decorative housings do not set tool dimensions.
constexpr float assetKeywayHalfWidth = .0071f, assetKeywayHalfHeight = .0294f;
constexpr float keywayHalfWidth = demoKeywayHalfWidth;
constexpr float keywayHalfHeight = demoKeywayHalfHeight;
inline float keywayWidth(LockStyle) { return keywayHalfWidth; }
inline float plugRadius(LockStyle style) {
  return style == LockStyle::Padlock ? padPlugRadius : lockPlugRadius;
}
inline float keywayHeight(LockStyle) { return keywayHalfHeight; }
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
// Use the same tracked hand frame before pickup, while held and at the lock.
// Blade alignment is a fixed metal-to-handle transform, not a wrist rotation.
inline Pose driverHandPose(Pose tracked) {
  return tracked;
}
// The blade's broad edge runs along local Y, aligned with a thumb-up grip.
// Rotate only the metal relative to the fitted handle. Blend the round shaft
// back to the unmodified handle, so baked finger contact does not change.
inline float driverMetalAngle(float z) {
  return (pi / 2) * std::clamp((driverHandleStart-z) /
                              (driverHandleStart-driverShaftStart), 0.f, 1.f);
}
inline Vec driverMetalPosition(Vec p) {
  return rotate(axisAngle({0,0,1},driverMetalAngle(p.z)),p);
}
inline Vec driverMetalNormal(Vec p, Vec n) {
  Quat q=axisAngle({0,0,1},driverMetalAngle(p.z));
  Vec v=rotate(q,n), t=rotate(q,p);
  if(p.z>driverShaftStart && p.z<driverHandleStart) {
    float slope=-(pi/2)/(driverHandleStart-driverShaftStart);
    v.z-=slope*(-t.y*v.x+t.x*v.y);
  }
  return normal(v);
}
// Fit existing and freshly prepared TTWProp5 vertices before applying the
// fixed metal orientation. Fade the width adjustment into the round neck.
inline void fitPreparedDriverBlade(Vec &p, Vec &n) {
  if (p.z >= driverShaftStart)
    return;
  bool neck = p.z > driverBladeEnd;
  float span = neck ? driverShaftStart - driverBladeEnd : driverBladeEnd;
  float t = (p.z - (neck ? driverBladeEnd : 0.f)) / span;
  float source = neck ? propBladeHalfWidth : propBladeTipHalfWidth;
  float fitted = neck ? driverBladeHalfWidth : driverBladeTipHalfWidth;
  float sourceDelta = (neck ? propBladeNeckHalfWidth : propBladeHalfWidth) - source;
  float fittedDelta = (neck ? propBladeNeckHalfWidth : driverBladeHalfWidth) - fitted;
  float sourceWidth = source + t*sourceDelta;
  float scale = (fitted + t*fittedDelta) / sourceWidth;
  float slope = (fittedDelta - scale*sourceDelta) / (span*sourceWidth);
  n = normal({n.x/scale, n.y, n.z - (n.x/scale)*p.x*slope});
  p.x *= scale;
}
constexpr float driverBearingDepth=.014f;
// The held leg extends down from the thumb/index pinch in a natural raised
// hand pose. Mirror its diagonal across hands, with the working leg facing
// forward. Use this frame for both input and drawing, independent of aim rays.
inline Quat pinGripOrientation(int hand) {
  Quat diagonal = axisAngle({0, 0, 1}, (hand ? -145.f : 145.f) * pi / 180);
  // Mirroring the diagonal alone leaves the working leg on the palm side of
  // the right-hand pinch. Turn around the held leg, preserving its downward
  // direction and the exact left-hand frame.
  return hand ? unit(multiply(diagonal, axisAngle({0, 1, 0}, pi))) : diagonal;
}
inline Pose pinGripPose(Pose tracked, Vec anchor, int hand) {
  return {unit(multiply(tracked.q, pinGripOrientation(hand))), world(tracked, anchor)};
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
  return {pinShaftLength * std::sin(a) * std::sin(twist), -pinShaftLength * std::cos(a),
          pinShaftLength * std::sin(a) * std::cos(twist)};
}
inline void bendPin(Vec initialTip, Vec movement, float &opening, float &twist) {
  Vec free = initialTip + (length(movement) <= .004f ? Vec{} : movement);
  if (length(free) <= .012f) return;
  free = normal(free);
  opening = std::clamp(std::acos(std::clamp(free.y,-1.f,1.f)) * 2 / pi, 0.f, 1.8f);
  if (std::hypot(free.x,free.z) > .001f)
    twist = std::atan2(-free.x,-free.z);
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
  return std::clamp(pinShaftLength - .085f * std::max(0.f, std::cos(opening * pi / 2)) -
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
  return {{0, 0, driverHandleStart}, {0, 0, driverLength}};
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
// All housings share the working aperture and lower blade bearing.
// Decoration must not change how the same two tools fit.
inline Vec mouth(int k, LockStyle style = LockStyle::Round) {
  return {0, k == 0 ? .0027f : -keywayHeight(style) + .0055f, .026f};
}
inline Pose aimedTool(Pose hand, Vec pivot, float depth = driverInsertionDepth,
                      LockStyle style = LockStyle::Round);
inline float leverBearing(Vec hand) {
  return std::atan2(hand.y, hand.x) * 180 / pi;
}
inline float leverTravel(Vec start, Vec current, float minimumRadius = .025f) {
  if (std::hypot(start.x, start.y) < minimumRadius ||
      std::hypot(current.x, current.y) < minimumRadius)
    return 0;
  return std::remainder(leverBearing(start) - leverBearing(current), 360.f);
}
inline Vec pinPivot(float opening = 1) {
  // An under-bent handle needs a little more stand-off to avoid the face.
  return {0, .0027f, .034f + .087f * std::max(0.f, std::cos(opening * pi / 2))};
}
inline Vec socket(int k) {
  return k == 0 ? pinPivot() - pinHinge(1) : mouth(1) - Vec{0, 0, driverInsertionDepth};
}
inline float pinBearing(Vec hand, Vec pivot = pinPivot()) {
  Vec v = hand - pivot;
  return std::atan2(v.x, v.y) * 180 / pi;
}
inline Quat pinAlignment(float opening, float twist);
inline Pose seatedPose(int k, float pick, float cylinder, float opening = 1,
                       float twist = 0) {
  if (k == 1)
    return compose({axisAngle({0, 0, 1}, -cylinder * pi / 180), {}},
                   aimedTool({{}, {.070f, -.035f, .150f}}, mouth(1)));
  Quat q = axisAngle({0, 0, 1}, -(pick - 90) * pi / 180);
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
  Vec a = h - normal(h) * pinBendRadius, b = h + spread + Vec{0, pinBendRadius, 0};
  Vec control =
      h + spread * .5f - Vec{0, .006f * std::max(0.f, 1 - opening), 0};
  return a * ((1 - t) * (1 - t)) + control * (2 * t * (1 - t)) + b * (t * t);
}
inline Vec pinHandlePoint(float opening, float t, float twist = 0) {
  return pinHinge(opening, twist) +
         Vec{.006f * std::max(0.f, 1 - opening) + pinWaveAmplitude * std::sin(t * 8 * pi),
             pinBendRadius + pinHandleLength * t, 0};
}
// The pads grip 10 mm nearer the end than the motion-control origin.
inline Vec presentedPinContact(float opening, float twist) {
  float station=std::clamp((.075f-pinBendRadius)/pinHandleLength,0.f,1.f);
  return pinHandlePoint(opening,station,twist);
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
inline Pose limitPinSearch(Pose p, Vec contact, float cylinder,
                           float span = 270, float previous = 90) {
  float angle = pinPoseAngle(p, cylinder);
  float excess = std::remainder(angle - boundPinSearch(angle, span, previous),360.f);
  Quat q = axisAngle({0, 0, 1}, excess * pi / 180);
  return {unit(multiply(q, p.q)), contact + rotate(q, p.p - contact)};
}
inline Pose heldTool(Pose hand, int k, float opening = 1, float twist = 0) {
  // The tool-specific grip frame supplies its held direction and centre.
  // Stored orientation is not inherited when a loose tool is acquired.
  return {hand.q, hand.p - rotate(hand.q, handleCenter(k, opening, twist))};
}
inline Pose aimedTool(Pose hand, Vec pivot, float depth, LockStyle) {
  Vec toward = hand.p - pivot;
  toward.z = std::max(toward.z, .055f);
  // Keep the handle out to either side and below the pin. The blade's broad
  // axis follows the slot, independent of the controller's axial roll.
  float side = toward.x < 0 ? -1.f : 1.f;
  // A deeply seated flat blade has limited tilt inside the narrow keyway.
  // These bounds retain clearance across either handed approach.
  toward.x = side * std::clamp(std::abs(toward.x), toward.z * .38f, toward.z * .40f);
  toward.y = std::clamp(toward.y, -toward.z * .60f, -toward.z * .50f);
  float transverse = std::hypot(toward.x, toward.y),
        limit = toward.z * std::tan(50 * pi / 180);
  if (transverse > limit) {
    toward.x *= limit / transverse;
    toward.y *= limit / transverse;
  }
  Vec shaft = normal(toward);
  Quat q = shortestArc({0, 0, 1}, shaft);
  Vec broad = rotate(q, {0, 1, 0});
  Vec slotUp{0,1,0};
  Vec up = normal(slotUp - shaft * dot(slotUp, shaft));
  float roll = std::atan2(dot(shaft, cross(broad, up)), dot(broad, up));
  q = unit(multiply(axisAngle(shaft, roll), q));
  return {q, pivot - rotate(q, {0, 0, depth})};
}
inline Pose guidedPin(Pose hand, Vec tip, float opening, float twist) {
  Vec pivot = tip + Vec{0, 0, .075f};
  float angle = boundPinSearch(90 + pinBearing(hand.p, pivot),270);
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
inline Pose presentDriverHand(Pose tracked, Pose tool, Pose localGrip,
                              bool inserted, float maxMove, float maxAngle) {
  Quat fitted = unit(multiply(tool.q, inverse(localGrip.q)));
  Pose shown{inserted ? fitted : rotateTowards(tracked.q, fitted, maxAngle),
             tracked.p};
  Vec contact = world(tool, handleCenter(1));
  Vec correction = contact - world(shown, localGrip.p);
  shown.p = shown.p + (inserted ? correction : bounded(correction, maxMove));
  return shown;
}
inline float wristRoll(Quat start, Quat current) {
  Quat delta = unit(multiply(inverse(start), current));
  if (delta.z * delta.z + delta.w * delta.w < 1e-8f)
    return 0;
  return std::remainder(-2 * std::atan2(delta.z, delta.w) * 180 / pi, 360.f);
}
} // namespace locks
