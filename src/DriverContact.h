// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ToolGeometry.h"
namespace locks {
// Translational side loading at the lower keyway, in the lock's own frame.
// Upright: pushing the handle right loads the lower blade left (clockwise).
// The loading direction follows the admitted cylinder: right, then down/right,
// then down. Neither wrist roll nor the hand's bearing about the centre is an
// input. This is a finite-travel VR control, not a force/rigid-body simulation.
constexpr float driverSideTravel = .012f; // 7.5 degrees per mm along the rotating slot side
inline Vec driverLoadDirection(float cylinder) {
  return rotate(axisAngle({0, 0, 1}, -cylinder * pi / 180), {1, 0, 0});
}
inline float driverSideTurn(Vec motion, float cylinder) {
  return dot(motion, driverLoadDirection(cylinder)) * (90.f / driverSideTravel);
}
// Forgiving haptic contact, sampled from the actual hand-held tool. Axial
// roll never selects a mandatory wrist pose or changes the visible blade.
struct DriverContact {
  bool touching = false;
  float depth = 0;
};
inline DriverContact driverContact(Pose blade, float cylinder, LockStyle style,
                                   bool wasTouching = false) {
  Pose p = turnWithCylinder(blade, -cylinder);
  Vec shaft = rotate(p.q, {0, 0, 1});
  float depth = mouth(1, style).z - p.p.z;
  // Retain contact during ordinary pressure/tilt changes. Pulling clear or
  // moving away cancels torque but never releases the held tool.
  if (shaft.z < .20f || depth < (wasTouching ? -.008f : -.001f) ||
      depth > (wasTouching ? .080f : .040f))
    return {};
  Vec entry = p.p + shaft * (depth / shaft.z);
  float margin = wasTouching ? .014f : .004f;
  bool inSlot = std::abs(entry.x) <= keywayWidth(style) + margin &&
                entry.y >= -keywayHeight(style) - margin &&
                entry.y <= margin;
  return {inSlot, depth};
}
// A loaded handle moves around the blade's actual crossing of the front face.
// Recompute it from the admitted pose each frame: this is a local contact
// response, not a remembered attachment or a canonical insertion position.
inline Pose driverLeverPose(Pose from, Pose requested, float cylinder,
                            LockStyle style, float scale=1) {
  constexpr float face = .024f; // front of the rotating plug in Collision.h
  Vec shaft = rotate(from.q, {0, 0, 1});
  if (shaft.z <= .20f)
    return requested;
  float along = (face - from.p.z) / shaft.z;
  if (along <= 0 || along >= driverBladeEnd*scale)
    return requested; // no blade crossing: ordinary free handling
  Vec entry = from.p + shaft * along;
  Vec slot = turnWithCylinder({{}, entry}, -cylinder).p;
  if (std::abs(slot.x) > keywayWidth(style) + .0005f ||
      slot.y < -keywayHeight(style) - .0005f || slot.y > .0005f)
    return requested;
  Vec grip = world(from, handleCenter(1)*scale);
  Vec motion = world(requested, handleCenter(1)*scale) - grip;
  Vec arm = grip - entry;
  Quat swing = shortestArc(arm, arm + Vec{motion.x, motion.y, 0});
  Quat rotation = unit(multiply(swing, requested.q));
  // Sideways travel swings the rigid tool, keeping its current engagement
  // depth. In/out travel still slides it freely, and controller wrist motion
  // remains in requested.q. The ordinary solid sweeps admit the final pose.
  Vec movedGrip = entry + rotate(swing, arm) + Vec{0, 0, motion.z};
  return {rotation, movedGrip - rotate(rotation, handleCenter(1)*scale)};
}
} // namespace locks
